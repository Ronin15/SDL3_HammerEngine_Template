/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/CollisionManager.hpp"
#include <algorithm>
#include <cstring>

bool AIManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    AI_INFO("AIManager already initialized");
    return true;
  }

  try {
    // Initialize behavior type mappings
    m_behaviorTypeMap["Wander"] = BehaviorType::Wander;
    m_behaviorTypeMap["Guard"] = BehaviorType::Guard;
    m_behaviorTypeMap["Patrol"] = BehaviorType::Patrol;
    m_behaviorTypeMap["Follow"] = BehaviorType::Follow;
    m_behaviorTypeMap["Chase"] = BehaviorType::Chase;
    m_behaviorTypeMap["Attack"] = BehaviorType::Attack;
    m_behaviorTypeMap["Flee"] = BehaviorType::Flee;
    m_behaviorTypeMap["Idle"] = BehaviorType::Idle;

    // Pre-allocate storage for better performance
    constexpr size_t INITIAL_CAPACITY = 1000;
    m_storage.reserve(INITIAL_CAPACITY);
    m_entityToIndex.reserve(INITIAL_CAPACITY);

    // Reserve capacity for assignment queues to prevent reallocations during
    // batch processing
    m_pendingAssignments.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);
    m_pendingAssignmentIndex.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);

    // Initialize double buffers
    m_storage.doubleBuffer[0].reserve(INITIAL_CAPACITY);
    m_storage.doubleBuffer[1].reserve(INITIAL_CAPACITY);

    // Initialize lock-free message queue
    for (auto &msg : m_lockFreeMessages) {
      msg.ready.store(false, std::memory_order_relaxed);
    }

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false;

    // No NPCSpawn handler in AIManager: state owns creation; AI manages
    // behavior only.

    // Initialize PathfinderManager (centralized pathfinding service)
    if (!PathfinderManager::Instance().isInitialized()) {
      if (!PathfinderManager::Instance().init()) {
        AI_ERROR("Failed to initialize PathfinderManager");
        return false;
      }
      AI_INFO("PathfinderManager initialized successfully");
    }

    AI_INFO("AIManager initialized successfully");
    return true;

  } catch (const std::exception &e) {
    AI_ERROR("Failed to initialize AIManager: " + std::string(e.what()));
    return false;
  }
}

void AIManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return;
  }

  AI_INFO("AIManager shutting down...");

  // Mark as shutting down
  m_isShutdown = true;
  m_initialized.store(false, std::memory_order_release);

  // Stop accepting new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // FIRE-AND-FORGET: No futures to wait for - let tasks drain naturally
  // Brief sleep allows ThreadSystem to process remaining tasks
  if (m_assignmentInProgress.load(std::memory_order_acquire)) {
    AI_INFO("Waiting for async assignments to drain...");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    m_assignmentInProgress.store(false, std::memory_order_release);
  }

  {
    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
    std::lock_guard<std::mutex> assignmentsLock(m_assignmentsMutex);
    std::lock_guard<std::mutex> messagesLock(m_messagesMutex);

    // Clean all behaviors first (with exception handling)
    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.behaviors[i] && m_storage.entities[i]) {
        try {
          m_storage.behaviors[i]->clean(m_storage.entities[i]);
        } catch (const std::exception &e) {
          AI_ERROR("Exception cleaning behavior during shutdown: " +
                   std::string(e.what()));
        } catch (...) {
          AI_ERROR("Unknown exception cleaning behavior during shutdown");
        }
      }
    }

    // Clear all storage
    m_storage.hotData.clear();
    m_storage.entities.clear();
    m_storage.behaviors.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.doubleBuffer[0].clear();
    m_storage.doubleBuffer[1].clear();

    m_entityToIndex.clear();
    m_behaviorTemplates.clear();
    m_behaviorCache.clear();
    m_behaviorTypeCache.clear();
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
    m_messageQueue.clear();
  }

  // Note: PathfinderManager is a singleton and will be shut down separately

  // Reset all counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);

  AI_INFO("AIManager shutdown complete");
}

void AIManager::prepareForStateTransition() {
  AI_INFO("Preparing AIManager for state transition...");

  // Pause AI processing to prevent new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // FIRE-AND-FORGET: No futures to wait for - let tasks drain naturally
  // Brief sleep allows ThreadSystem to process remaining tasks
  if (m_assignmentInProgress.load(std::memory_order_acquire)) {
    AI_DEBUG("Waiting for async assignments to drain...");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    m_assignmentInProgress.store(false, std::memory_order_release);
  }

  // Process and clear any pending messages
  processMessageQueue();

  // Clear message queues completely
  {
    std::lock_guard<std::mutex> lock(m_messagesMutex);
    m_messageQueue.clear();
    // Reset lock-free message indices
    m_messageReadIndex.store(0, std::memory_order_relaxed);
    m_messageWriteIndex.store(0, std::memory_order_relaxed);
    for (auto &msg : m_lockFreeMessages) {
      msg.ready.store(false, std::memory_order_relaxed);
    }
  }

  // Clear assignment queues
  {
    std::lock_guard<std::mutex> lock(m_assignmentsMutex);
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
  }

  // Clean up all entities safely and clear managed entities list
  {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

    // Clean all behaviors
    size_t cleanedCount = 0;
    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.behaviors[i] && m_storage.entities[i]) {
        try {
          m_storage.behaviors[i]->clean(m_storage.entities[i]);
          cleanedCount++;
        } catch (const std::exception &e) {
          AI_ERROR("Exception cleaning behavior: " + std::string(e.what()));
        }
      }
    }

    if (cleanedCount > 0) {
      AI_INFO("Cleaned " + std::to_string(cleanedCount) + " AI behaviors");
    }

    // Clear all storage completely
    m_storage.hotData.clear();
    m_storage.entities.clear();
    m_storage.behaviors.clear();
    m_storage.lastUpdateTimes.clear();
    m_entityToIndex.clear();

    // Clear double buffers to prevent stale data
    m_storage.doubleBuffer[0].clear();
    m_storage.doubleBuffer[1].clear();
    m_storage.currentBuffer.store(0, std::memory_order_release);

    // Clear managed entities list
    m_managedEntities.clear();

    AI_DEBUG("Cleaned up all entities for state transition");
  }

  // Legacy pathfinding state cleared - all pathfinding now handled by
  // PathfinderManager

  // Reset all counters and stats
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);
  m_lastCleanupFrame.store(0, std::memory_order_relaxed);

  // Reset performance tracking
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_globalStats = AIPerformanceStats{};
    std::fill(m_behaviorStats.begin(), m_behaviorStats.end(), AIPerformanceStats{});
  }

  // Clear player reference completely
  {
    std::lock_guard<std::shared_mutex> lock(m_entitiesMutex);
    m_playerEntity.reset();
  }

  // Reset threading state
  m_lastWasThreaded.store(false, std::memory_order_relaxed);
  m_lastOptimalWorkerCount.store(0, std::memory_order_relaxed);
  m_lastAvailableWorkers.store(0, std::memory_order_relaxed);
  m_lastAIBudget.store(0, std::memory_order_relaxed);

  // Don't call resetBehaviors() as we've already done comprehensive cleanup
  // above Just clear the behavior caches
  {
    std::lock_guard<std::shared_mutex> lock(m_behaviorsMutex);
    m_behaviorCache.clear();
    m_behaviorTypeCache.clear();
  }

  // Reset pause state to false so next state starts unpaused
  m_globallyPaused.store(false, std::memory_order_release);

  AI_INFO("AIManager state transition complete - all state cleared and reset");
}

void AIManager::update([[maybe_unused]] float deltaTime) {
  if (!m_initialized.load(std::memory_order_acquire) ||
      m_globallyPaused.load(std::memory_order_acquire)) {
    return;
  }

  auto startTime = std::chrono::high_resolution_clock::now();

  try {
    // Do not carry over AI update futures across frames to avoid races with
    // render. Any previous frame's update work must be completed within its
    // frame.

    // Process pending assignments first so new entities are picked up this
    // frame
    processPendingBehaviorAssignments();

    // Use atomic active count - no iteration needed
    size_t activeCount = m_activeEntityCount.load(std::memory_order_relaxed);
    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);

    // Early exit if no active entities
    if (activeCount == 0) {
      processMessageQueue();
      m_lastWasThreaded.store(false, std::memory_order_relaxed);
      return;
    }

    // Get total entity count (used for buffer sizing)
    size_t entityCount;
    {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      entityCount = m_storage.size();
    }

    if (entityCount == 0) {
      return;
    }

    // Lock-free double buffer swap
    int currentBuffer = m_storage.currentBuffer.load(std::memory_order_acquire);
    int nextBuffer = 1 - currentBuffer;

    // Get player position for distance calculations
    // Distance calculation moved to processBatch to avoid redundant iteration
    // OPTIMIZATION: Reduced update frequency from every 8 to every 16 frames
    // Distance-based culling doesn't need frame-perfect accuracy
    EntityPtr player = m_playerEntity.lock();
    bool shouldUpdateDistances = player && (currentFrame % 16 == 0);
    Vector2D playerPos = player ? player->getPosition() : Vector2D(0, 0);

    // THREAD-SAFE DOUBLE BUFFERING: Copy data BEFORE starting async tasks
    // PERFORMANCE OPTIMIZATION: Reduced copy frequency from 60 to 120 frames
    // Only copy when entity count changes or on periodic refresh
    bool entityCountChanged =
        (m_storage.doubleBuffer[nextBuffer].size() != m_storage.hotData.size());

    if (entityCountChanged || currentFrame % 120 == 0) {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      m_storage.doubleBuffer[nextBuffer] = m_storage.hotData;

      // Debug logging for buffer copy events (only log occasionally to avoid spam)
      if (currentFrame % 600 == 0) {
        AI_DEBUG("Double buffer copy: " + std::to_string(m_storage.hotData.size()) +
                " entities (" + (entityCountChanged ? "count changed" : "periodic refresh") + ")");
      }
    } else {
      // Use current buffer data - no copy needed
      nextBuffer = currentBuffer;
    }

    // Determine threading strategy based on ACTIVE entity count instead of
    // total storage size to avoid unnecessary threading after resets
    const size_t threadingThreshold = std::max<size_t>(
        1, m_threadingThreshold.load(std::memory_order_acquire));
    bool useThreading = (activeCount >= threadingThreshold &&
                         m_useThreading.load(std::memory_order_acquire) &&
                         HammerEngine::ThreadSystem::Exists());

    if (useThreading) {
      auto &threadSystem = HammerEngine::ThreadSystem::Instance();
      size_t availableWorkers =
          static_cast<size_t>(threadSystem.getThreadCount());

      // Check queue pressure before submitting tasks
      size_t queueSize = threadSystem.getQueueSize();
      size_t queueCapacity = threadSystem.getQueueCapacity();
      if (queueCapacity == 0) {
        // Defensive: treat as high pressure if capacity unknown
        queueCapacity = 1;
      }
      size_t pressureThreshold =
          static_cast<size_t>(queueCapacity * HammerEngine::QUEUE_PRESSURE_CRITICAL); // Use unified threshold

      if (queueSize > pressureThreshold) {
        // Graceful degradation: fallback to single-threaded processing
        AI_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                 std::to_string(queueCapacity) +
                 "), using single-threaded processing");
        m_lastWasThreaded.store(false, std::memory_order_relaxed);
        m_lastThreadBatchCount.store(1, std::memory_order_relaxed);

        // Pre-fetch all data with single lock
        PreFetchedBatchData preFetchedData;
        preFetchedData.reserve(entityCount);
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          auto &workBuffer = m_storage.doubleBuffer[nextBuffer];
          for (size_t i = 0; i < entityCount && i < m_storage.size(); ++i) {
            if (i < workBuffer.size() && workBuffer[i].active) {
              preFetchedData.entities.push_back(m_storage.entities[i]);
              preFetchedData.behaviors.push_back(m_storage.behaviors[i]);
              preFetchedData.hotDataCopy.push_back(workBuffer[i]);
              if (i < m_storage.halfWidths.size()) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.halfWidths[i]));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.halfHeights[i]));
              } else if (m_storage.entities[i]) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.entities[i]->getWidth() * 0.5f));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.entities[i]->getHeight() * 0.5f));
              } else {
                preFetchedData.halfWidths.push_back(16.0f);
                preFetchedData.halfHeights.push_back(16.0f);
              }
            } else {
              preFetchedData.entities.push_back(nullptr);
              preFetchedData.behaviors.push_back(nullptr);
              preFetchedData.halfWidths.push_back(16.0f);
              preFetchedData.halfHeights.push_back(16.0f);
              if (i < workBuffer.size()) {
                preFetchedData.hotDataCopy.push_back(workBuffer[i]);
              } else {
                preFetchedData.hotDataCopy.push_back(AIEntityData::HotData{});
              }
            }
          }
        }

        // Use deferred collision update for consistency
        std::vector<CollisionManager::KinematicUpdate> collisionUpdates;
        collisionUpdates.reserve(entityCount);
        processBatch(0, entityCount, deltaTime, nextBuffer, playerPos, shouldUpdateDistances, preFetchedData, collisionUpdates);

        // Submit collision updates
        if (!collisionUpdates.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.updateKinematicBatchSOA(collisionUpdates);
        }

        // Swap buffers atomically
        if (nextBuffer != currentBuffer) {
          m_storage.currentBuffer.store(nextBuffer, std::memory_order_release);
        }
        // Single-threaded path completes within this frame; continue to stats
      }

      HammerEngine::WorkerBudget budget =
          HammerEngine::calculateWorkerBudget(availableWorkers);

      // Use WorkerBudget system properly with threshold-based buffer allocation
      size_t optimalWorkerCount = budget.getOptimalWorkerCount(
          budget.aiAllocated, entityCount, threadingThreshold);

      // Store thread allocation info for debug output
      m_lastOptimalWorkerCount.store(optimalWorkerCount,
                                     std::memory_order_relaxed);
      m_lastAvailableWorkers.store(availableWorkers, std::memory_order_relaxed);
      m_lastAIBudget.store(budget.aiAllocated, std::memory_order_relaxed);
      m_lastWasThreaded.store(true, std::memory_order_relaxed);

      // Use unified batch calculation for consistency across all managers
      double queuePressure = static_cast<double>(queueSize) / queueCapacity;
      auto [batchCount, batchSize] = HammerEngine::calculateBatchStrategy(
          HammerEngine::AI_BATCH_CONFIG,
          entityCount,
          threadingThreshold,
          optimalWorkerCount,
          queuePressure
      );

      // Debug logging for high queue pressure
      if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
        AI_DEBUG("High queue pressure (" +
                 std::to_string(static_cast<int>(queuePressure * 100)) +
                 "%), using larger batches");
      }

      // Debug thread allocation info periodically
      if (batchCount <= 1) {
        // Avoid thread overhead when only one batch would run
        m_lastWasThreaded.store(false, std::memory_order_relaxed);
        m_lastThreadBatchCount.store(1, std::memory_order_relaxed);

        // Pre-fetch all data with single lock
        PreFetchedBatchData preFetchedData;
        preFetchedData.reserve(entityCount);
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          auto &workBuffer = m_storage.doubleBuffer[nextBuffer];
          for (size_t i = 0; i < entityCount && i < m_storage.size(); ++i) {
            if (i < workBuffer.size() && workBuffer[i].active) {
              preFetchedData.entities.push_back(m_storage.entities[i]);
              preFetchedData.behaviors.push_back(m_storage.behaviors[i]);
              preFetchedData.hotDataCopy.push_back(workBuffer[i]);
              if (i < m_storage.halfWidths.size()) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.halfWidths[i]));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.halfHeights[i]));
              } else if (m_storage.entities[i]) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.entities[i]->getWidth() * 0.5f));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.entities[i]->getHeight() * 0.5f));
              } else {
                preFetchedData.halfWidths.push_back(16.0f);
                preFetchedData.halfHeights.push_back(16.0f);
              }
            } else {
              preFetchedData.entities.push_back(nullptr);
              preFetchedData.behaviors.push_back(nullptr);
              preFetchedData.halfWidths.push_back(16.0f);
              preFetchedData.halfHeights.push_back(16.0f);
              if (i < workBuffer.size()) {
                preFetchedData.hotDataCopy.push_back(workBuffer[i]);
              } else {
                preFetchedData.hotDataCopy.push_back(AIEntityData::HotData{});
              }
            }
          }
        }

        // Single batch - still use deferred collision update for consistency
        std::vector<CollisionManager::KinematicUpdate> collisionUpdates;
        collisionUpdates.reserve(entityCount);
        processBatch(0, entityCount, deltaTime, nextBuffer, playerPos, shouldUpdateDistances, preFetchedData, collisionUpdates);

        // Submit collision updates
        if (!collisionUpdates.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.updateKinematicBatchSOA(collisionUpdates);
        }
      } else {
        size_t entitiesPerBatch = entityCount / batchCount;
        size_t remainingEntities = entityCount % batchCount;

        // PERFORMANCE OPTIMIZATION: Use batchEnqueueTasks() for O(1) lock acquisition
        // instead of O(N) individual enqueue calls. This reduces ThreadSystem mutex
        // contention significantly when submitting multiple batches.

        // PRE-FETCH OPTIMIZATION: Copy ALL entity data ONCE with a single lock
        // This eliminates serialized lock acquisitions per batch (CRITICAL for parallel performance)
        PreFetchedBatchData preFetchedData;
        preFetchedData.reserve(entityCount);

        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          auto &workBuffer = m_storage.doubleBuffer[nextBuffer];

          for (size_t i = 0; i < entityCount && i < m_storage.size(); ++i) {
            if (i < workBuffer.size() && workBuffer[i].active) {
              preFetchedData.entities.push_back(m_storage.entities[i]);
              preFetchedData.behaviors.push_back(m_storage.behaviors[i]);
              preFetchedData.hotDataCopy.push_back(workBuffer[i]);

              // Get extents while we have the lock
              if (i < m_storage.halfWidths.size()) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.halfWidths[i]));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.halfHeights[i]));
              } else if (m_storage.entities[i]) {
                preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.entities[i]->getWidth() * 0.5f));
                preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.entities[i]->getHeight() * 0.5f));
              } else {
                preFetchedData.halfWidths.push_back(16.0f);
                preFetchedData.halfHeights.push_back(16.0f);
              }
            } else {
              // Add nulls to maintain index alignment
              preFetchedData.entities.push_back(nullptr);
              preFetchedData.behaviors.push_back(nullptr);
              preFetchedData.halfWidths.push_back(16.0f);
              preFetchedData.halfHeights.push_back(16.0f);
              if (i < workBuffer.size()) {
                preFetchedData.hotDataCopy.push_back(workBuffer[i]);
              } else {
                preFetchedData.hotDataCopy.push_back(AIEntityData::HotData{});
              }
            }
          }
        }  // Lock released - batches can now run in parallel without locks!

        // DEFERRED COLLISION UPDATE OPTIMIZATION:
        // Create per-batch collision update vectors to eliminate CollisionManager lock contention.
        // Each batch accumulates its updates independently, then we merge and submit once.
        std::vector<std::vector<CollisionManager::KinematicUpdate>> batchCollisionUpdates(batchCount);
        for (size_t i = 0; i < batchCount; ++i) {
          size_t estimatedSize = entitiesPerBatch + (i == batchCount - 1 ? remainingEntities : 0);
          batchCollisionUpdates[i].reserve(estimatedSize);
        }

        // Atomic counter for batch completion synchronization
        std::atomic<size_t> remainingBatches{batchCount};
        std::mutex completionMutex;
        std::condition_variable completionCV;

        // Build task vector for batch submission
        std::vector<std::function<void()>> tasks;
        tasks.reserve(batchCount);

        for (size_t i = 0; i < batchCount; ++i) {
          size_t start = i * entitiesPerBatch;
          size_t end = start + entitiesPerBatch;

          // Add remaining entities to last batch
          if (i == batchCount - 1) {
            end += remainingEntities;
          }

          // Capture pre-fetched data by reference (safe - we wait for completion)
          tasks.push_back([this, start, end, deltaTime, nextBuffer, playerPos,
                          shouldUpdateDistances, &preFetchedData, &batchCollisionUpdates, i,
                          &remainingBatches, &completionMutex, &completionCV]() {
            try {
              processBatch(start, end, deltaTime, nextBuffer, playerPos,
                          shouldUpdateDistances, preFetchedData, batchCollisionUpdates[i]);
            } catch (const std::exception &e) {
              AI_ERROR(std::string("Exception in AI batch: ") + e.what());
            } catch (...) {
              AI_ERROR("Unknown exception in AI batch");
            }

            // Signal completion
            if (remainingBatches.fetch_sub(1, std::memory_order_acq_rel) == 1) {
              std::lock_guard<std::mutex> lock(completionMutex);
              completionCV.notify_one();
            }
          });
        }

        // Single mutex acquisition for entire batch submission (O(1) instead of O(N))
        threadSystem.batchEnqueueTasks(tasks, HammerEngine::TaskPriority::High, "AI_Batch");

        // Wait for all batches to complete to maintain update->render safety
        std::unique_lock<std::mutex> lock(completionMutex);
        completionCV.wait(lock, [&remainingBatches]() {
          return remainingBatches.load(std::memory_order_acquire) == 0;
        });

        // DEFERRED COLLISION UPDATE: Merge all batch updates and submit once
        // This reduces CollisionManager lock acquisitions from O(batches) to O(1)
        std::vector<CollisionManager::KinematicUpdate> mergedCollisionUpdates;
        size_t totalUpdates = 0;
        for (const auto& batchUpdates : batchCollisionUpdates) {
          totalUpdates += batchUpdates.size();
        }
        mergedCollisionUpdates.reserve(totalUpdates);

        for (auto& batchUpdates : batchCollisionUpdates) {
          mergedCollisionUpdates.insert(mergedCollisionUpdates.end(),
                                        std::make_move_iterator(batchUpdates.begin()),
                                        std::make_move_iterator(batchUpdates.end()));
        }

        // Single collision system update - eliminates lock contention
        if (!mergedCollisionUpdates.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.updateKinematicBatchSOA(mergedCollisionUpdates);
        }
      }

      m_lastThreadBatchCount.store(batchCount, std::memory_order_relaxed);

    } else {
      // Single-threaded processing
      m_lastThreadBatchCount.store(1, std::memory_order_relaxed);

      // Pre-fetch all data with single lock
      PreFetchedBatchData preFetchedData;
      preFetchedData.reserve(entityCount);
      {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        auto &workBuffer = m_storage.doubleBuffer[nextBuffer];
        for (size_t i = 0; i < entityCount && i < m_storage.size(); ++i) {
          if (i < workBuffer.size() && workBuffer[i].active) {
            preFetchedData.entities.push_back(m_storage.entities[i]);
            preFetchedData.behaviors.push_back(m_storage.behaviors[i]);
            preFetchedData.hotDataCopy.push_back(workBuffer[i]);
            if (i < m_storage.halfWidths.size()) {
              preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.halfWidths[i]));
              preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.halfHeights[i]));
            } else if (m_storage.entities[i]) {
              preFetchedData.halfWidths.push_back(std::max(1.0f, m_storage.entities[i]->getWidth() * 0.5f));
              preFetchedData.halfHeights.push_back(std::max(1.0f, m_storage.entities[i]->getHeight() * 0.5f));
            } else {
              preFetchedData.halfWidths.push_back(16.0f);
              preFetchedData.halfHeights.push_back(16.0f);
            }
          } else {
            preFetchedData.entities.push_back(nullptr);
            preFetchedData.behaviors.push_back(nullptr);
            preFetchedData.halfWidths.push_back(16.0f);
            preFetchedData.halfHeights.push_back(16.0f);
            if (i < workBuffer.size()) {
              preFetchedData.hotDataCopy.push_back(workBuffer[i]);
            } else {
              preFetchedData.hotDataCopy.push_back(AIEntityData::HotData{});
            }
          }
        }
      }

      // Use deferred collision update for consistency
      std::vector<CollisionManager::KinematicUpdate> collisionUpdates;
      collisionUpdates.reserve(entityCount);
      processBatch(0, entityCount, deltaTime, nextBuffer, playerPos, shouldUpdateDistances, preFetchedData, collisionUpdates);

      // Submit collision updates
      if (!collisionUpdates.empty()) {
        auto &cm = CollisionManager::Instance();
        cm.updateKinematicBatchSOA(collisionUpdates);
      }
    }

    // Swap buffers atomically only if we actually changed buffers
    if (nextBuffer != currentBuffer) {
      m_storage.currentBuffer.store(nextBuffer, std::memory_order_release);
    }

    // Process lock-free message queue
    processMessageQueue();

    // Performance tracking
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // Periodic cleanup and stats (balanced frequency)
    if (currentFrame % 300 == 0) {
      // cleanupInactiveEntities() moved to GameEngine background processing

      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);

      std::lock_guard<std::mutex> statsLock(m_statsMutex);
      m_globalStats.addSample(duration, entityCount);

      if (entityCount > 0) {
        double avgDuration =
            m_globalStats.updateCount > 0
                ? (m_globalStats.totalUpdateTime / m_globalStats.updateCount)
                : 0.0;

        // All pathfinding now handled by PathfinderManager

        bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);
        if (wasThreaded) {
          size_t optimalWorkers =
              m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
          size_t availableWorkers =
              m_lastAvailableWorkers.load(std::memory_order_relaxed);
          size_t aiBudget = m_lastAIBudget.load(std::memory_order_relaxed);
          size_t batchCountLogged = std::max(
              static_cast<size_t>(1),
              m_lastThreadBatchCount.load(std::memory_order_relaxed));

          AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                   ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                   ", Entities/sec: " +
                   std::to_string(m_globalStats.entitiesPerSecond) +
                   " [Threaded: " + std::to_string(optimalWorkers) + "/" +
                   std::to_string(availableWorkers) +
                   " workers, Budget: " + std::to_string(aiBudget) +
                   ", Batches: " + std::to_string(batchCountLogged) + "]");
        } else {
          AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                   ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                   ", Entities/sec: " +
                   std::to_string(m_globalStats.entitiesPerSecond) +
                   " [Single-threaded]");
        }

        // Pathfinding statistics now handled by PathfinderManager
        // No legacy pathfinding statistics in AIManager
      }
    }

  } catch (const std::exception &e) {
    AI_ERROR("Exception in AIManager::update: " + std::string(e.what()));
  }
}

void AIManager::registerBehavior(const std::string &name,
                                 std::shared_ptr<AIBehavior> behavior) {
  if (!behavior) {
    AI_ERROR("Attempted to register null behavior with name: " + name);
    return;
  }

  std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
  m_behaviorTemplates[name] = behavior;

  // Clear cache when adding new behavior
  m_behaviorCache.clear();
  m_behaviorTypeCache.clear();

  AI_INFO("Registered behavior: " + name);
}

bool AIManager::hasBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior>
AIManager::getBehavior(const std::string &name) const {
  // Check cache first for O(1) lookup
  auto cacheIt = m_behaviorCache.find(name);
  if (cacheIt != m_behaviorCache.end()) {
    return cacheIt->second;
  }

  // Cache miss - lookup and cache result
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  auto it = m_behaviorTemplates.find(name);
  std::shared_ptr<AIBehavior> result =
      (it != m_behaviorTemplates.end()) ? it->second : nullptr;

  // Cache the result (even if nullptr)
  m_behaviorCache[name] = result;
  return result;
}

void AIManager::assignBehaviorToEntity(EntityPtr entity,
                                       const std::string &behaviorName) {
  if (!entity) {
    AI_ERROR("Cannot assign behavior to null entity");
    return;
  }

  // Get behavior template (use cache for performance)
  std::shared_ptr<AIBehavior> behaviorTemplate = getBehavior(behaviorName);
  if (!behaviorTemplate) {
    AI_ERROR("Behavior not found: " + behaviorName);
    return;
  }

  // Clone behavior for this entity
  auto behavior = behaviorTemplate->clone();
  behavior->init(entity);

  // Find or create entity entry
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_entityToIndex.find(entity);
  if (indexIt != m_entityToIndex.end()) {
    // Update existing entity
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      // Clean up old behavior
      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(entity);
      }

      // Assign new behavior
      m_storage.behaviors[index] = behavior;
      m_storage.hotData[index].behaviorType =
          static_cast<uint8_t>(inferBehaviorType(behaviorName));

      // Update active state and counter
      if (!m_storage.hotData[index].active) {
        m_storage.hotData[index].active = true;
        m_activeEntityCount.fetch_add(1, std::memory_order_relaxed);
      }

      // Refresh extents
      if (index < m_storage.halfWidths.size()) {
        float halfW = std::max(1.0f, entity->getWidth() * 0.5f);
        float halfH = std::max(1.0f, entity->getHeight() * 0.5f);
        m_storage.halfWidths[index] = halfW;
        m_storage.halfHeights[index] = halfH;
      }

      AI_INFO("Updated behavior for existing entity to: " + behaviorName);
    }
  } else {
    // Add new entity
    size_t newIndex = m_storage.size();

    // Add to hot data
    AIEntityData::HotData hotData{};
    hotData.position = entity->getPosition();
    hotData.lastPosition = hotData.position;
    hotData.distanceSquared = 0.0f;
    hotData.frameCounter = 0;
    hotData.priority = DEFAULT_PRIORITY;
    hotData.behaviorType =
        static_cast<uint8_t>(inferBehaviorType(behaviorName));
    hotData.active = true;
    hotData.shouldUpdate = true;

    m_storage.hotData.push_back(hotData);
    m_storage.entities.push_back(entity);
    m_storage.behaviors.push_back(behavior);

    // Increment active counter for new entity
    m_activeEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_storage.lastUpdateTimes.push_back(0.0f);
    m_storage.halfWidths.push_back(std::max(1.0f, entity->getWidth() * 0.5f));
    m_storage.halfHeights.push_back(std::max(1.0f, entity->getHeight() * 0.5f));

    // Update index map
    m_entityToIndex[entity] = newIndex;

    AI_INFO("Added new entity with behavior: " + behaviorName);
  }

  m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
}

void AIManager::unassignBehaviorFromEntity(EntityPtr entity) {
  if (!entity)
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.size()) {
      // Decrement active counter if entity was active
      if (m_storage.hotData[index].active) {
        m_storage.hotData[index].active = false;
        m_activeEntityCount.fetch_sub(1, std::memory_order_relaxed);
      }

      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(entity);
      }
    }
  }
}

bool AIManager::entityHasBehavior(EntityPtr entity) const {
  if (!entity)
    return false;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].active &&
           m_storage.behaviors[it->second] != nullptr;
  }

  return false;
}

void AIManager::queueBehaviorAssignment(EntityPtr entity,
                                        const std::string &behaviorName) {
  if (!entity || behaviorName.empty()) {
    AI_ERROR("Invalid behavior assignment request");
    return;
  }

  std::lock_guard<std::mutex> lock(m_assignmentsMutex);

  // Check for duplicate assignments
  auto it = m_pendingAssignmentIndex.find(entity);
  if (it != m_pendingAssignmentIndex.end()) {
    // Update existing assignment
    it->second = behaviorName;
  } else {
    // Add new assignment
    m_pendingAssignments.emplace_back(entity, behaviorName);
    m_pendingAssignmentIndex[entity] = behaviorName;
  }
}

size_t AIManager::processPendingBehaviorAssignments() {
  // FIRE-AND-FORGET: No futures tracking - ThreadSystem monitors queue health
  // If we're still processing previous assignments, don't start new ones
  if (m_assignmentInProgress.load(std::memory_order_acquire)) {
    return 0; // Come back next frame
  }

  std::vector<PendingAssignment> toProcess;
  {
    std::lock_guard<std::mutex> lock(m_assignmentsMutex);
    if (m_pendingAssignments.empty()) {
      return 0;
    }
    // Move all pending assignments to local vector
    toProcess = std::move(m_pendingAssignments);
    m_pendingAssignmentIndex.clear();
  }

  size_t assignmentCount = toProcess.size();
  if (assignmentCount == 0) {
    return 0;
  }

  // For small batches, process synchronously on main thread
  if (assignmentCount <= 100) {
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // For large batches, process asynchronously
  bool useThreading = m_useThreading.load(std::memory_order_acquire) &&
                      HammerEngine::ThreadSystem::Exists();

  if (!useThreading) {
    // Fall back to synchronous processing
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Async processing for large batches
  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;

  // Safety check: If queue is too full, process synchronously
  if (queuePressure > 0.7) {
    AI_DEBUG("ThreadSystem queue pressure high (" +
             std::to_string(queuePressure * 100) +
             "%) - processing assignments synchronously");
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Calculate optimal batching
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t optimalWorkerCount =
      budget.getOptimalWorkerCount(budget.aiAllocated, assignmentCount, 1000);

  size_t minAssignmentsPerBatch = 1000;
  size_t maxBatches = 4;
  
  // Skip pathfinding coordination for small entity counts to avoid overhead
  size_t assignmentThreadingThreshold =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  if (assignmentCount > assignmentThreadingThreshold / 2) {
    // Removed pathfinding load coordination
  }
  if (queuePressure > 0.5) {
    minAssignmentsPerBatch = 1500;
    maxBatches = 2;
  } else if (queuePressure < 0.25) {
    minAssignmentsPerBatch = 800;
    maxBatches = 4;
  }

  size_t batchCount =
      std::min(optimalWorkerCount, assignmentCount / minAssignmentsPerBatch);
  batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));
  size_t assignmentsPerBatch = assignmentCount / batchCount;
  size_t remaining = assignmentCount % batchCount;

  // Set the flag to indicate async processing is starting
  m_assignmentInProgress.store(true, std::memory_order_release);

  // Submit async batches - FIRE-AND-FORGET (no futures tracking)
  size_t start = 0;
  for (size_t i = 0; i < batchCount; ++i) {
    size_t end =
        start + assignmentsPerBatch + (i == batchCount - 1 ? remaining : 0);

    // Copy the batch data (we need to own this data for async processing)
    std::vector<PendingAssignment> batchData(toProcess.begin() + start,
                                             toProcess.begin() + end);

    // Fire-and-forget: Use shared_ptr to track completion of last batch
    auto completionFlag = (i == batchCount - 1)
        ? std::make_shared<std::atomic<bool>>(false)
        : nullptr;

    threadSystem.enqueueTask(
        [this, batchData = std::move(batchData), completionFlag]() {
          // Check if AIManager is shutting down
          if (m_isShutdown) {
            if (completionFlag) completionFlag->store(true, std::memory_order_release);
            return; // Don't process assignments during shutdown
          }

          for (const auto &assignment : batchData) {
            if (assignment.entity) {
              try {
              assignBehaviorToEntity(assignment.entity,
                                     assignment.behaviorName);
            } catch (const std::exception &e) {
              AI_ERROR("Exception during async behavior assignment: " +
                       std::string(e.what()));
            } catch (...) {
              AI_ERROR("Unknown exception during async behavior assignment");
            }
          }
        }

        // Last batch resets the in-progress flag
        if (completionFlag) {
          completionFlag->store(true, std::memory_order_release);
          m_assignmentInProgress.store(false, std::memory_order_release);
        }
      },
      HammerEngine::TaskPriority::High, "AI_AssignmentBatch");
    start = end;
  }

  // Async assignment processing statistics are now tracked in periodic summary
  return assignmentCount; // Return immediately, don't wait
}

void AIManager::setPlayerForDistanceOptimization(EntityPtr player) {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
  m_playerEntity = player;
}

EntityPtr AIManager::getPlayerReference() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return m_playerEntity.lock();
}

Vector2D AIManager::getPlayerPosition() const {
  if (auto player = getPlayerReference()) {
    return player->getPosition();
  }
  return Vector2D{0.0f, 0.0f};
}

bool AIManager::isPlayerValid() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return !m_playerEntity.expired();
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority) {
  if (!entity) {
    AI_ERROR("Cannot register null entity for updates");
    return;
  }

  // Clamp priority to valid range
  priority = std::max(AI_MIN_PRIORITY, std::min(AI_MAX_PRIORITY, priority));

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Check if entity already exists
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end()) {
    // Update priority for existing entity
    size_t index = it->second;
    if (index < m_storage.size()) {
      m_storage.hotData[index].priority = static_cast<uint8_t>(priority);
    }
  } else {
    // Add managed entity info
    EntityUpdateInfo info;
    info.entityWeak = entity;
    info.priority = priority;
    info.frameCounter = 0;
    info.lastUpdateTime = getCurrentTimeNanos();

    m_managedEntities.push_back(info);
  }
}

void AIManager::unregisterEntityFromUpdates(EntityPtr entity) {
  if (!entity)
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Remove from managed entities
  m_managedEntities.erase(
      std::remove_if(m_managedEntities.begin(), m_managedEntities.end(),
                     [&entity](const EntityUpdateInfo &info) {
                       auto e = info.entityWeak.lock();
                       return !e || e == entity;
                     }),
      m_managedEntities.end());

  // Mark as inactive in main storage
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    // Decrement active counter if entity was active
    if (m_storage.hotData[it->second].active) {
      m_storage.hotData[it->second].active = false;
      m_activeEntityCount.fetch_sub(1, std::memory_order_relaxed);
    }
  }
}

void AIManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
  AI_INFO((paused ? "AI processing paused" : "AI processing resumed"));
}

bool AIManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

void AIManager::resetBehaviors() {
  AI_INFO("Resetting all AI behaviors");

  std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
  std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);

  // Clean up all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.entities[i]) {
      m_storage.behaviors[i]->clean(m_storage.entities[i]);
    }
  }

  // Clear all data
  m_storage.hotData.clear();
  m_storage.entities.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_entityToIndex.clear();
  m_managedEntities.clear();

  // Clear double buffers to prevent stale data synchronization issues
  m_storage.doubleBuffer[0].clear();
  m_storage.doubleBuffer[1].clear();
  m_storage.currentBuffer.store(0, std::memory_order_release);

  // Reset counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
  m_useThreading.store(useThreading, std::memory_order_release);

  if (maxThreads > 0) {
    m_maxThreads = maxThreads;
  }

  AI_INFO("Threading configured: " +
         std::string(useThreading ? "enabled" : "disabled") +
         " with max threads: " + std::to_string(m_maxThreads));
}

void AIManager::setThreadingThreshold(size_t threshold) {
  threshold = std::max(static_cast<size_t>(1), threshold);
  m_threadingThreshold.store(threshold, std::memory_order_release);
  AI_INFO("AI threading threshold set to " + std::to_string(threshold) +
          " entities");
}

size_t AIManager::getThreadingThreshold() const {
  return m_threadingThreshold.load(std::memory_order_acquire);
}

void AIManager::configurePriorityMultiplier(float multiplier) {
  m_priorityMultiplier.store(multiplier, std::memory_order_release);
}

AIPerformanceStats AIManager::getPerformanceStats() const {
  std::lock_guard<std::mutex> lock(m_statsMutex);
  return m_globalStats;
}

size_t AIManager::getBehaviorCount() const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.size();
}

size_t AIManager::getManagedEntityCount() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Count only active entities
  size_t activeCount = 0;
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.hotData[i].active) {
      activeCount++;
    }
  }

  return activeCount;
}

size_t AIManager::getBehaviorUpdateCount() const {
  return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
  return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

void AIManager::sendMessageToEntity(EntityPtr entity,
                                    const std::string &message,
                                    bool immediate) {
  if (!entity || message.empty())
    return;

  if (immediate) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_entityToIndex.find(entity);
    if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
      if (m_storage.behaviors[it->second]) {
        m_storage.behaviors[it->second]->onMessage(entity, message);
      }
    }
  } else {
    // Use lock-free queue for non-immediate messages
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target = entity;
    std::strncpy(msg.message, message.c_str(), sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.ready.store(true, std::memory_order_release);
  }
}

void AIManager::broadcastMessage(const std::string &message, bool immediate) {
  if (message.empty())
    return;

  if (immediate) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.hotData[i].active && m_storage.behaviors[i]) {
        m_storage.behaviors[i]->onMessage(m_storage.entities[i], message);
      }
    }
  } else {
    // Queue broadcast for processing in next update
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target.reset(); // No specific target for broadcast
    std::strncpy(msg.message, message.c_str(), sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';
    msg.ready.store(true, std::memory_order_release);
  }
}

void AIManager::processMessageQueue() {
  // Process lock-free message queue
  size_t readIndex = m_messageReadIndex.load(std::memory_order_relaxed);
  size_t writeIndex = m_messageWriteIndex.load(std::memory_order_acquire);

  while (readIndex != writeIndex) {
    auto &msg = m_lockFreeMessages[readIndex % MESSAGE_QUEUE_SIZE];

    if (msg.ready.load(std::memory_order_acquire)) {
      if (auto entity = msg.target.lock()) {
        sendMessageToEntity(entity, msg.message, true);
      }

      msg.ready.store(false, std::memory_order_release);
    }

    readIndex++;
  }

  m_messageReadIndex.store(readIndex, std::memory_order_release);
}

BehaviorType
AIManager::inferBehaviorType(const std::string &behaviorName) const {
  // Check cache first for O(1) lookup
  auto cacheIt = m_behaviorTypeCache.find(behaviorName);
  if (cacheIt != m_behaviorTypeCache.end()) {
    return cacheIt->second;
  }

  // Cache miss - lookup and cache result
  auto it = m_behaviorTypeMap.find(behaviorName);
  BehaviorType result =
      (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;

  // Cache the result
  m_behaviorTypeCache[behaviorName] = result;
  return result;
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime,
                             int bufferIndex, const Vector2D &playerPos, bool updateDistances,
                             const PreFetchedBatchData& preFetchedData,
                             std::vector<CollisionManager::KinematicUpdate>& collisionUpdates) {
  // Work on the double buffer for lock-free operation
  auto &workBuffer = m_storage.doubleBuffer[bufferIndex];

  size_t batchExecutions = 0;

  // Reserve space in collision updates accumulator (approximate size)
  size_t batchSize = end - start;
  collisionUpdates.reserve(collisionUpdates.size() + batchSize);

  // Pre-calculate common values once per batch to reduce per-entity overhead
  float maxDist = m_maxUpdateDistance.load(std::memory_order_relaxed);
  float maxDistSquared = maxDist * maxDist;
  bool hasPlayer = (playerPos.getX() != 0 || playerPos.getY() != 0);

  // OPTIMIZATION: Get world bounds ONCE per batch (not per entity)
  // Eliminates 418+ atomic loads per frame → single atomic load per batch
  const auto &pf = PathfinderManager::Instance();
  float worldWidth, worldHeight;
  pf.getCachedWorldBounds(worldWidth, worldHeight);

  // NO LOCK NEEDED - use pre-fetched data!
  // Process entities using pre-copied data (parallel safe)
  for (size_t i = start; i < end && i < preFetchedData.entities.size(); ++i) {
    // Use pre-fetched data instead of locking
    EntityPtr entity = preFetchedData.entities[i];
    auto behavior = preFetchedData.behaviors[i];
    float halfW = preFetchedData.halfWidths[i];
    float halfH = preFetchedData.halfHeights[i];
    auto hotData = preFetchedData.hotDataCopy[i];  // Copy, not reference

    try {
      // Calculate distances inline to avoid separate iteration
      if (updateDistances && hasPlayer) {
        Vector2D entityPos = entity->getPosition();
        Vector2D diff = entityPos - playerPos;
        hotData.distanceSquared = diff.lengthSquared();
        hotData.position = entityPos;
      }

      // Simple distance-based culling - no frame counting needed
      bool shouldUpdate = true;
      if (hasPlayer) {
        // Use pre-calculated values from batch level
        float priorityMultiplier = 1.0f + hotData.priority * 0.1f;
        float effectiveMaxDistSquared =
            maxDistSquared * priorityMultiplier * priorityMultiplier;

        // Pure distance-based culling - entities too far away don't update
        shouldUpdate = (hotData.distanceSquared <= effectiveMaxDistSquared);
      }

      if (shouldUpdate) {
        behavior->executeLogic(entity);

        // Entity updates integrate their own movement AND handle animations
        entity->update(deltaTime);

        // Centralized clamp pass using cached world bounds (NO atomic loads)
        // halfW and halfH already loaded from pre-fetched data above

        Vector2D pos = entity->getPosition();
        Vector2D vel = entity->getVelocity();

        // Inline clamping - no function call, no atomic load
        float minX = halfW;
        float maxX = worldWidth - halfW;
        float minY = halfH;
        float maxY = worldHeight - halfH;
        Vector2D clamped(
            std::clamp(pos.getX(), minX, maxX),
            std::clamp(pos.getY(), minY, maxY)
        );

        if (clamped.getX() != pos.getX() || clamped.getY() != pos.getY()) {
          // Update entity position directly (bypassing individual collision updates)
          entity->Entity::setPosition(clamped); // Use base Entity::setPosition to avoid collision sync

          // Project velocity inward for boundary collisions
          if (clamped.getX() < pos.getX() && vel.getX() < 0) vel.setX(0.0f);
          if (clamped.getX() > pos.getX() && vel.getX() > 0) vel.setX(0.0f);
          if (clamped.getY() < pos.getY() && vel.getY() < 0) vel.setY(0.0f);
          if (clamped.getY() > pos.getY() && vel.getY() > 0) vel.setY(0.0f);
          entity->Entity::setVelocity(vel); // Use base Entity::setVelocity

          pos = clamped; // Update pos for batch accumulation
        }

        // BATCH OPTIMIZATION: Accumulate position/velocity for collision system batch update
        collisionUpdates.emplace_back(entity->getID(), pos, vel);

        batchExecutions++;
      } else {
        // PERFORMANCE OPTIMIZATION: Skip entity updates for culled entities entirely
        // Use cached position from hotData to avoid virtual function calls
        // No need to call setVelocity() - culled entities don't move anyway
        collisionUpdates.emplace_back(entity->getID(), hotData.position, Vector2D(0, 0));
      }
    } catch (const std::exception &e) {
      AI_ERROR("Error in batch processing: " + std::string(e.what()));
      // Decrement active counter if entity was active
      if (hotData.active) {
        hotData.active = false;
        m_activeEntityCount.fetch_sub(1, std::memory_order_relaxed);
      }
    }
  }

  // DEFERRED COLLISION UPDATE: Accumulator pattern
  // Instead of submitting to CollisionManager here (causing lock contention across all batches),
  // we accumulate updates into the passed-in vector. The caller will submit all batches' updates
  // in a single call after all parallel processing completes, reducing lock acquisitions from
  // O(batches) to O(1) and eliminating inter-batch contention.

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions,
                                        std::memory_order_relaxed);
  }
}

void AIManager::updateDistancesScalar(const Vector2D &playerPos) {
  size_t entityCount = m_storage.hotData.size();

  // PERFORMANCE FIX: Use entity positions directly instead of synced positions
  // This avoids the expensive position sync loop in update()
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  size_t updatedCount = 0;
  for (size_t i = 0; i < entityCount; ++i) {
    auto &hotData = m_storage.hotData[i];
    if (hotData.active && m_storage.entities[i]) {
      // Get position directly from entity - this is more accurate than cached position
      Vector2D entityPos = m_storage.entities[i]->getPosition();
      Vector2D diff = entityPos - playerPos;
      hotData.distanceSquared = diff.lengthSquared();
      // Update cached position for next frame
      hotData.position = entityPos;
      updatedCount++;
    }
  }

  // Optional: Early exit if no entities were active
  if (updatedCount == 0) {
    return;
  }
}

void AIManager::cleanupInactiveEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Find all inactive entities
  std::vector<size_t> toRemove;
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (!m_storage.hotData[i].active) {
      toRemove.push_back(i);
    }
  }

  // Remove in reverse order to maintain indices
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it) {
    size_t index = *it;

    // Remove from entity map
    if (index < m_storage.entities.size()) {
      m_entityToIndex.erase(m_storage.entities[index]);
    }

    // Swap with last element and pop
    if (index < m_storage.size() - 1) {
      size_t lastIndex = m_storage.size() - 1;

      // Update hot data
      m_storage.hotData[index] = m_storage.hotData[lastIndex];

      // Update cold data
      m_storage.entities[index] = m_storage.entities[lastIndex];
      m_storage.behaviors[index] = m_storage.behaviors[lastIndex];
      m_storage.lastUpdateTimes[index] = m_storage.lastUpdateTimes[lastIndex];

      // Update index map
      m_entityToIndex[m_storage.entities[index]] = index;
    }

    // Remove last element
    m_storage.hotData.pop_back();
    m_storage.entities.pop_back();
    m_storage.behaviors.pop_back();
    m_storage.lastUpdateTimes.pop_back();
    if (!m_storage.halfWidths.empty()) {
      m_storage.halfWidths.pop_back();
    }
    if (!m_storage.halfHeights.empty()) {
      m_storage.halfHeights.pop_back();
    }
  }

  AI_DEBUG("Cleaned up " + std::to_string(toRemove.size()) +
           " inactive entities");
}

void AIManager::cleanupAllEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Clean all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.entities[i]) {
      try {
        m_storage.behaviors[i]->clean(m_storage.entities[i]);
      } catch (const std::exception &e) {
        AI_ERROR("Exception cleaning behavior: " + std::string(e.what()));
      }
    }
  }

  // Clear all storage
  m_storage.hotData.clear();
  m_storage.entities.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_entityToIndex.clear();

  // Reset active counter
  m_activeEntityCount.store(0, std::memory_order_relaxed);

  AI_DEBUG("Cleaned up all entities for state transition");
}

void AIManager::recordPerformance(BehaviorType type, double timeMs,
                                  uint64_t entities) {
  std::lock_guard<std::mutex> lock(m_statsMutex);

  size_t typeIndex = static_cast<size_t>(type);
  if (typeIndex < m_behaviorStats.size()) {
    m_behaviorStats[typeIndex].addSample(timeMs, entities);
  }
}

uint64_t AIManager::getCurrentTimeNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

void AIManager::updateEntityExtents(EntityPtr entity, float halfW, float halfH) {
  if (!entity) return;
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.halfWidths.size()) {
      m_storage.halfWidths[index] = std::max(1.0f, halfW);
      m_storage.halfHeights[index] = std::max(1.0f, halfH);
    }
  }
}

int AIManager::getEntityPriority(EntityPtr entity) const {
  if (!entity)
    return DEFAULT_PRIORITY;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].priority;
  }
  return DEFAULT_PRIORITY;
}

float AIManager::getUpdateRangeMultiplier(int priority) const {
  // Higher priority = larger update range multiplier
  return 1.0f + (std::max(0, std::min(9, priority)) * 0.1f);
}

void AIManager::registerEntityForUpdates(EntityPtr entity, int priority,
                                         const std::string &behaviorName) {
  // Register for updates
  registerEntityForUpdates(entity, priority);

  // Queue behavior assignment
  queueBehaviorAssignment(entity, behaviorName);
}

// ======================= Legacy Pathfinding Code Removed
// ======================= All pathfinding functionality is now handled by
// PathfinderManager. Use PathfinderManager::Instance() to access pathfinding
// services.

AIManager::~AIManager() {
  if (!m_isShutdown) {
    clean();
  }
}

// processScheduledPathfinding method removed - all pathfinding now handled by
// PathfinderManager

// All pathfinding functionality has been moved to PathfinderManager
// This section previously contained orphaned pathfinding methods that are no
// longer needed

// Centralized async request state management (replaces g_asyncStates static
// map) Legacy pathfinding state management methods removed All pathfinding
// functionality now handled by PathfinderManager

// Direct PathfinderManager access for optimal performance
PathfinderManager &AIManager::getPathfinderManager() const {
  return PathfinderManager::Instance();
}


// All pathfinding components are now managed by PathfinderManager
