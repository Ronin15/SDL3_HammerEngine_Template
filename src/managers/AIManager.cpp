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
#include "ai/internal/Crowd.hpp"
#include "utils/SIMDMath.hpp"
#include <algorithm>
#include <cstring>

// Use SIMD abstraction layer
using namespace HammerEngine::SIMD;

bool AIManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    AI_INFO("AIManager already initialized");
    return true;
  }

  try {
    // Validate dependency initialization order
    // AIManager requires these managers to be initialized first to avoid
    // null pointer dereferences and initialization races
    if (!PathfinderManager::Instance().isInitialized()) {
      AI_ERROR("PathfinderManager must be initialized before AIManager");
      return false;
    }
    if (!CollisionManager::Instance().isInitialized()) {
      AI_ERROR("CollisionManager must be initialized before AIManager");
      return false;
    }

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

    // Pre-allocate collision update buffer for single-threaded paths
    // Reserve with 10% headroom for growth (typical: 4000 NPCs → 4400 capacity)
    m_reusableCollisionBuffer.reserve(static_cast<size_t>(INITIAL_CAPACITY * 1.1));

    // Initialize lock-free message queue
    for (auto &msg : m_lockFreeMessages) {
      msg.ready.store(false, std::memory_order_relaxed);
    }

    m_initialized.store(true, std::memory_order_release);
    m_isShutdown = false;

    // No NPCSpawn handler in AIManager: state owns creation; AI manages
    // behavior only.

    // NOTE: PathfinderManager and CollisionManager are now initialized by GameEngine
    // AIManager depends on these managers but doesn't manage their lifecycle

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

  // CRITICAL: Wait for any pending async batches to complete before cleanup
  waitForAsyncBatchCompletion();

  // DETERMINISTIC: Wait for any pending assignments to complete
  AI_INFO("Waiting for async assignments to complete...");
  waitForAssignmentCompletion();

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

    m_entityToIndex.clear();
    m_behaviorTemplates.clear();
    m_behaviorCache.clear();
    m_behaviorTypeCache.clear();
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
    m_messageQueue.clear();
  }

  // NOTE: PathfinderManager and CollisionManager are now cleaned up by GameEngine
  // AIManager no longer manages their lifecycle

  // Reset all counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);
  m_activeEntityCount.store(0, std::memory_order_relaxed);

  AI_INFO("AIManager shutdown complete");
}

void AIManager::prepareForStateTransition() {
  AI_INFO("Preparing AIManager for state transition...");

  // Pause AI processing to prevent new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // CRITICAL: Wait for any pending async batches to complete before state transition
  waitForAsyncBatchCompletion();

  // DETERMINISTIC SYNCHRONIZATION: Wait for all assignment batches to complete
  //
  // Replaces empirical 100ms sleep with futures-based completion tracking.
  // This ensures all async assignment tasks complete before state transition clears
  // entity data, preventing use-after-free and race conditions.
  //
  // History: Previously used 100ms sleep (empirically tested with 2000 entities).
  // - 10ms: Frequent crashes
  // - 50ms: Occasional crashes
  // - 100ms: Stable but not deterministic
  //
  // Current approach: Blocks on std::future::wait() for each assignment batch.
  // Scalable to any entity count, deterministic completion guarantee.
  AI_DEBUG("Waiting for all AI assignment tasks to complete...");
  waitForAssignmentCompletion();

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
  m_activeEntityCount.store(0, std::memory_order_relaxed);

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

void AIManager::update(float deltaTime) {
  if (!m_initialized.load(std::memory_order_acquire) ||
      m_globallyPaused.load(std::memory_order_acquire)) {
    return;
  }

  // NOTE: We do NOT wait for previous frame's batches here - they can overlap with current frame
  // The critical sync happens in GameEngine before CollisionManager to ensure collision data is ready
  // This allows better frame pipelining on low-core systems

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

    // PERFORMANCE: Invalidate spatial query cache for new frame
    // This ensures thread-local caches are fresh and don't use stale collision data
    AIInternal::InvalidateSpatialCache(currentFrame);

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

    // Get player position for distance calculations
    // Distance calculation moved to processBatch to avoid redundant iteration
    // OPTIMIZATION: Stagger distance updates - update 1/16th of entities per frame
    // instead of all entities every 16 frames for smoother frame-to-frame consistency
    EntityPtr player = m_playerEntity.lock();
    uint64_t distanceUpdateSlice = player ? (currentFrame % 16) : UINT64_MAX;
    Vector2D playerPos = player ? player->getPosition() : Vector2D(0, 0);

    // SINGLE-COPY OPTIMIZATION: Pre-fetch directly from m_storage.hotData
    // No intermediate buffer copy needed - preFetchedData is our only copy
    // This eliminates redundant copying and reduces cache thrashing

    // Determine threading strategy
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

        // OPTIMIZATION: Direct storage iteration - no copying overhead
        // Hold shared_lock during processing for thread-safe read access
        m_reusableCollisionBuffer.clear();
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          processBatch(0, entityCount, deltaTime, playerPos, distanceUpdateSlice, m_storage, m_reusableCollisionBuffer);
        }

        // Submit collision updates directly (single-threaded, no batching needed)
        if (!m_reusableCollisionBuffer.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.applyKinematicUpdates(m_reusableCollisionBuffer);
        }
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

      // Use unified adaptive batch calculation for consistency across all managers
      double queuePressure = static_cast<double>(queueSize) / queueCapacity;

      // Get previous frame's completion time for adaptive feedback
      double lastFrameTime = m_adaptiveBatchState.lastUpdateTimeMs.load(std::memory_order_acquire);

      auto [batchCount, batchSize] = HammerEngine::calculateBatchStrategy(
          HammerEngine::AI_BATCH_CONFIG,
          entityCount,
          threadingThreshold,
          optimalWorkerCount,
          budget.aiAllocated,    // Base allocation for buffer detection
          queuePressure,
          m_adaptiveBatchState,  // Adaptive state for performance tuning
          lastFrameTime          // Previous frame's completion time
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

        // OPTIMIZATION: Direct storage iteration - no copying overhead
        // Hold shared_lock during processing for thread-safe read access
        m_reusableCollisionBuffer.clear();
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          processBatch(0, entityCount, deltaTime, playerPos, distanceUpdateSlice, m_storage, m_reusableCollisionBuffer);
        }

        // Submit collision updates directly (single batch, no contention)
        if (!m_reusableCollisionBuffer.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.applyKinematicUpdates(m_reusableCollisionBuffer);
        }
      } else {
        size_t entitiesPerBatch = entityCount / batchCount;
        size_t remainingEntities = entityCount % batchCount;

        // OPTIMIZATION: Direct storage iteration with per-batch locking
        // Each batch acquires its own shared_lock - multiple batches can read in parallel
        // Eliminates 10K+ push_back operations for pre-fetch copying

        // DEFERRED COLLISION UPDATE OPTIMIZATION:
        // Create per-batch collision update vectors to eliminate CollisionManager lock contention.
        // Each batch accumulates its updates independently, then we merge and submit once.
        // Use shared_ptr so the lambda can safely capture this data for async completion
        // OPTIMIZATION: Reuse member variable to avoid per-frame shared_ptr allocation
        if (!m_batchCollisionUpdates) {
          m_batchCollisionUpdates = std::make_shared<std::vector<std::vector<CollisionManager::KinematicUpdate>>>();
        }
        m_batchCollisionUpdates->resize(batchCount);
        // PERFORMANCE: Avoid unnecessary shared_ptr copy - use member directly
        for (size_t i = 0; i < batchCount; ++i) {
          size_t estimatedSize = entitiesPerBatch + (i == batchCount - 1 ? remainingEntities : 0);
          // Clear existing capacity, only reallocate if needed
          (*m_batchCollisionUpdates)[i].clear();
          if ((*m_batchCollisionUpdates)[i].capacity() < estimatedSize) {
            (*m_batchCollisionUpdates)[i].reserve(estimatedSize);
          }
        }

        // Submit batches using futures for native async result tracking
        // OPTIMIZATION: Reuse m_batchFutures directly to avoid local vector allocation
        {
          std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
          m_batchFutures.clear();  // Clear old futures, keeps capacity
          m_batchFutures.reserve(batchCount);
          // Member variable already holds the buffer, no need for extra copy

          for (size_t i = 0; i < batchCount; ++i) {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            // Add remaining entities to last batch
            if (i == batchCount - 1) {
              end += remainingEntities;
            }

            // Submit each batch with future for completion tracking
            // Each batch will acquire its own shared_lock during execution
            // PERFORMANCE: Capture raw pointer to avoid atomic ref-counting in lambda
            auto* batchCollisionUpdatesPtr = m_batchCollisionUpdates.get();
            m_batchFutures.push_back(threadSystem.enqueueTaskWithResult(
              [this, start, end, deltaTime, playerPos,
               distanceUpdateSlice, batchCollisionUpdatesPtr, i]() -> void {
                try {
                  // Acquire shared_lock for this batch's execution
                  // Multiple batches can hold shared_locks simultaneously (parallel reads)
                  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
                  processBatch(start, end, deltaTime, playerPos,
                              distanceUpdateSlice, m_storage, (*batchCollisionUpdatesPtr)[i]);
                } catch (const std::exception &e) {
                  AI_ERROR(std::string("Exception in AI batch: ") + e.what());
                } catch (...) {
                  AI_ERROR("Unknown exception in AI batch");
                }

                // PER-BATCH BUFFER: Each batch writes to its own buffer (zero contention!)
                // Collision updates will be submitted after all batches complete
              },
              HammerEngine::TaskPriority::High,
              "AI_Batch"
            ));
          }
        }

        // NO WAIT HERE: Async batches complete in background
        // GameEngine waits for completion at NEXT frame's start (consistent timing, no jitter)
        // Batches hold shared_locks during execution, allowing parallel reads
        // This provides deterministic updates with minimal performance impact
      }

      m_lastThreadBatchCount.store(batchCount, std::memory_order_relaxed);

    } else {
      // Single-threaded processing (threading disabled in config)
      m_lastThreadBatchCount.store(1, std::memory_order_relaxed);

      // OPTIMIZATION: Direct storage iteration - no copying overhead
      // Hold shared_lock during processing for thread-safe read access
      m_reusableCollisionBuffer.clear();
      {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        processBatch(0, entityCount, deltaTime, playerPos, distanceUpdateSlice, m_storage, m_reusableCollisionBuffer);
      }

      // Submit collision updates using convenience API (single-vector overload)
      if (!m_reusableCollisionBuffer.empty()) {
        auto &cm = CollisionManager::Instance();
        cm.applyKinematicUpdates(m_reusableCollisionBuffer);
      }
    }

    // Process lock-free message queue
    processMessageQueue();

    currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // CRITICAL: Wait for all async batches to complete before returning
    // This ensures CollisionManager receives complete updates when it runs.
    // The waitForAsyncBatchCompletion() method:
    // 1. Waits for all batch futures to complete
    // 2. Submits aggregated collision updates to CollisionManager
    // 3. Ensures consistent timing and eliminates race conditions
    waitForAsyncBatchCompletion();

    // Performance tracking AFTER async wait (measures true total time)
    auto endTime = std::chrono::high_resolution_clock::now();
    double totalUpdateTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Store for adaptive batch tuning
    m_adaptiveBatchState.lastUpdateTimeMs.store(totalUpdateTime, std::memory_order_release);

    // Periodic cleanup and stats (balanced frequency)
    if (currentFrame % 300 == 0) {
      // cleanupInactiveEntities() moved to GameEngine background processing

      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);

      // Note: Vector capacity trimming is available via AIBehaviorState::trimVectorCapacity()
      // Behaviors can call this in their executeLogic() periodically if needed

      std::lock_guard<std::mutex> statsLock(m_statsMutex);
      m_globalStats.addSample(totalUpdateTime, entityCount);

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

void AIManager::waitForAsyncBatchCompletion() {
  // BATCH SYNCHRONIZATION: Wait for all async batches to complete
  // This ensures collision updates are ready before CollisionManager processes them.
  //
  // JITTER ELIMINATION: Per-batch buffers eliminate mutex contention
  //   - Old approach: Each batch serializes on submitPendingKinematicUpdates mutex
  //   - New approach: Each batch writes to its own buffer (zero contention!)
  //   - Result: Consistent batch completion times → smooth frames

  std::vector<std::future<void>> localFutures;
  std::shared_ptr<std::vector<std::vector<CollisionManager::KinematicUpdate>>> collisionBuffers;

  {
    std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
    localFutures = std::move(m_batchFutures);
    collisionBuffers = m_batchCollisionUpdates;
    m_batchCollisionUpdates.reset();  // Clear for next frame
  }

  // Wait for all batch futures to complete
  for (auto& future : localFutures) {
    if (future.valid()) {
      future.wait();  // Block until batch completes
    }
  }

  // Submit ALL collision updates at once (zero mutex contention during batch execution!)
  if (collisionBuffers && !collisionBuffers->empty()) {
    auto &cm = CollisionManager::Instance();
    cm.applyBatchedKinematicUpdates(*collisionBuffers);
  }
}

void AIManager::waitForAssignmentCompletion() {
  // ASSIGNMENT SYNCHRONIZATION: Wait for all async assignment batches to complete
  // This ensures no dangling references to entity data during state transitions.
  //
  // DETERMINISTIC COMPLETION: Replaces empirical 100ms sleep with future.wait()
  //   - Old approach: sleep_for(100ms) - works empirically but not scalable
  //   - New approach: Block on futures until all assignments complete
  //   - Result: Deterministic completion, scalable to any entity count

  std::vector<std::future<void>> localFutures;

  {
    std::lock_guard<std::mutex> lock(m_assignmentFuturesMutex);
    localFutures = std::move(m_assignmentFutures);
  }

  // Wait for all assignment futures to complete
  for (auto& future : localFutures) {
    if (future.valid()) {
      future.wait();  // Block until assignment batch completes
    }
  }
}

void AIManager::registerBehavior(const std::string &name,
                                 std::shared_ptr<AIBehavior> behavior) {
  if (!behavior) {
    AI_ERROR("Attempted to register null behavior with name: " + name);
    return;
  }

  std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
  
  // OPTIMIZATION: Only invalidate cache entry for this specific behavior (not entire cache)
  // If behavior already exists, we're replacing it - clear its cache entry
  // If it's new, no cache entry exists anyway
  auto existingIt = m_behaviorTemplates.find(name);
  if (existingIt != m_behaviorTemplates.end()) {
    // Replacing existing behavior - invalidate its cache entries
    m_behaviorCache.erase(name);
    m_behaviorTypeCache.erase(name);
  }
  
  m_behaviorTemplates[name] = behavior;

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
  // FUTURES-BASED: Deterministic completion tracking with m_assignmentFutures
  // Assignments use std::future<void> for safe state transition synchronization

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
  if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
    AI_DEBUG("ThreadSystem queue pressure high (" +
             std::to_string(static_cast<int>(queuePressure * 100)) +
             "%) - processing assignments synchronously");
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Calculate optimal batching using unified API
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t optimalWorkerCount =
      budget.getOptimalWorkerCount(budget.aiAllocated, assignmentCount, 1000);

  size_t assignmentThreadingThreshold =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));

  // Get previous frame's completion time for adaptive feedback
  double lastUpdateTimeMs = m_adaptiveBatchState.lastUpdateTimeMs.load(std::memory_order_acquire);

  auto [batchCount, batchSize] = HammerEngine::calculateBatchStrategy(
      HammerEngine::AI_BATCH_CONFIG,
      assignmentCount,
      assignmentThreadingThreshold,
      optimalWorkerCount,
      budget.aiAllocated,    // Base allocation for buffer detection
      queuePressure,
      m_adaptiveBatchState,
      lastUpdateTimeMs
  );

  // Submit batches using futures for deterministic completion tracking
  {
    std::lock_guard<std::mutex> lock(m_assignmentFuturesMutex);
    m_assignmentFutures.clear();  // Clear old futures, keeps capacity
    m_assignmentFutures.reserve(batchCount);

    size_t start = 0;
    for (size_t i = 0; i < batchCount; ++i) {
      // Early exit if all items have been processed
      if (start >= assignmentCount) break;

      // Ensure end never exceeds assignmentCount (batchSize uses ceiling division)
      size_t end = std::min(start + batchSize, assignmentCount);

      // Copy the batch data (we need to own this data for async processing)
      std::vector<PendingAssignment> batchData(toProcess.begin() + start,
                                               toProcess.begin() + end);

      // Submit each batch with future for completion tracking
      m_assignmentFutures.push_back(threadSystem.enqueueTaskWithResult(
          [this, batchData = std::move(batchData)]() -> void {
            // Check if AIManager is shutting down
            if (m_isShutdown) {
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
          },
          HammerEngine::TaskPriority::High, "AI_AssignmentBatch"));
      start = end;
    }
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

void AIManager::setWaitForBatchCompletion(bool wait) {
  m_waitForBatchCompletion.store(wait, std::memory_order_release);
  AI_INFO("AI batch completion wait " + std::string(wait ? "enabled" : "disabled") +
          " (smooth frames: " + std::string(wait ? "no" : "yes") + ")");
}

bool AIManager::getWaitForBatchCompletion() const {
  return m_waitForBatchCompletion.load(std::memory_order_acquire);
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
  // PERFORMANCE: Use atomic counter instead of O(n) iteration
  // m_activeEntityCount is maintained in sync with entity activation/deactivation
  return m_activeEntityCount.load(std::memory_order_relaxed);
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
    // SAFETY: Use safer string copy with explicit bounds checking
    size_t copyLen = std::min(message.length(), sizeof(msg.message) - 1);
    message.copy(msg.message, copyLen);
    msg.message[copyLen] = '\0';
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
    // SAFETY: Use safer string copy with explicit bounds checking
    size_t copyLen = std::min(message.length(), sizeof(msg.message) - 1);
    message.copy(msg.message, copyLen);
    msg.message[copyLen] = '\0';
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

/**
 * @brief SIMD-optimized batch distance calculation
 * Processes 4 entities at once using platform-specific SIMD instructions
 *
 * @param start Starting index in storage
 * @param end Ending index in storage
 * @param playerPos Player position for distance calculations
 * @param distanceUpdateSlice Stagger pattern index (only entities matching i % 16 == slice are updated)
 * @param storage Entity storage (read-only)
 * @param outDistances Output array for calculated squared distances (must be sized >= end - start)
 * @param outPositions Output array for entity positions (must be sized >= end - start)
 */
void AIManager::calculateDistancesSIMD(
    size_t start, size_t end,
    const Vector2D& playerPos,
    uint64_t distanceUpdateSlice,
    const EntityStorage& storage,
    std::vector<float>& outDistances,
    std::vector<Vector2D>& outPositions
) {
    const bool hasPlayer = (playerPos.getX() != 0 || playerPos.getY() != 0);
    if (!hasPlayer || distanceUpdateSlice == UINT64_MAX) {
        return; // No distance updates needed
    }

    const Float4 playerPosX = broadcast(playerPos.getX());
    const Float4 playerPosY = broadcast(playerPos.getY());

#if defined(HAMMER_SIMD_SSE2) || defined(HAMMER_SIMD_NEON)
    // SIMD path: Process 4 entities at once
    // Only process entities that match the stagger pattern
    for (size_t i = start; i + 3 < end && i + 3 < storage.entities.size(); i += 4) {
        // Check if all 4 entities match the stagger pattern (same slice)
        bool allMatch = ((i % 16) == distanceUpdateSlice) &&
                       (((i + 1) % 16) == distanceUpdateSlice) &&
                       (((i + 2) % 16) == distanceUpdateSlice) &&
                       (((i + 3) % 16) == distanceUpdateSlice);

        if (allMatch && i < storage.entities.size() &&
            (i + 1) < storage.entities.size() &&
            (i + 2) < storage.entities.size() &&
            (i + 3) < storage.entities.size()) {

            // Get entity positions
            Vector2D pos0 = storage.entities[i]->getPosition();
            Vector2D pos1 = storage.entities[i + 1]->getPosition();
            Vector2D pos2 = storage.entities[i + 2]->getPosition();
            Vector2D pos3 = storage.entities[i + 3]->getPosition();

            // Load positions into SIMD registers
            Float4 entityPosX = set(pos0.getX(), pos1.getX(), pos2.getX(), pos3.getX());
            Float4 entityPosY = set(pos0.getY(), pos1.getY(), pos2.getY(), pos3.getY());

            // Calculate differences
            Float4 diffX = sub(entityPosX, playerPosX);
            Float4 diffY = sub(entityPosY, playerPosY);

            // Calculate squared distances: diffX * diffX + diffY * diffY
            Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

            // Store results
            alignas(16) float distSquaredArray[4];
            store4(distSquaredArray, distSq);

            // Update output arrays
            size_t idx = i - start;
            outDistances[idx] = distSquaredArray[0];
            outDistances[idx + 1] = distSquaredArray[1];
            outDistances[idx + 2] = distSquaredArray[2];
            outDistances[idx + 3] = distSquaredArray[3];

            outPositions[idx] = pos0;
            outPositions[idx + 1] = pos1;
            outPositions[idx + 2] = pos2;
            outPositions[idx + 3] = pos3;
        }
    }
#endif

    // Scalar fallback/tail loop for remaining entities
    for (size_t i = start; i < end && i < storage.entities.size(); ++i) {
        if ((i % 16) == distanceUpdateSlice) {
            Vector2D entityPos = storage.entities[i]->getPosition();
            Vector2D diff = entityPos - playerPos;
            size_t idx = i - start;
            outDistances[idx] = diff.lengthSquared();
            outPositions[idx] = entityPos;
        }
    }
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime,
                             const Vector2D &playerPos, uint64_t distanceUpdateSlice,
                             const EntityStorage& storage,
                             std::vector<CollisionManager::KinematicUpdate>& collisionUpdates) {
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
  if (!pf.getCachedWorldBounds(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
    // Fallback: Use large default if PathfinderManager grid isn't ready yet
    // This ensures entities can move even during world loading/grid rebuild
    worldWidth = 32000.0f;  // Default world size
    worldHeight = 32000.0f;
  }

  // SIMD OPTIMIZATION: Pre-compute distances for this batch using SIMD
  // Process 4 entities at once for 2-4x speedup
  std::vector<float> precomputedDistances(batchSize, -1.0f); // -1 = not computed
  std::vector<Vector2D> precomputedPositions(batchSize);
  calculateDistancesSIMD(start, end, playerPos, distanceUpdateSlice, storage,
                        precomputedDistances, precomputedPositions);

  // OPTIMIZATION: Direct storage iteration - no copying overhead
  // Caller holds shared_lock, safe for parallel read access
  for (size_t i = start; i < end && i < storage.entities.size(); ++i) {
    // Access storage directly (read-only, no allocation)
    // PERFORMANCE: Use raw pointers to avoid atomic ref-counting overhead
    // Safe: shared_lock ensures storage stability, parent shared_ptrs keep objects alive
    Entity* entity = storage.entities[i].get();
    AIBehavior* behavior = storage.behaviors[i].get();
    float halfW = storage.halfWidths[i];
    float halfH = storage.halfHeights[i];
    auto hotData = storage.hotData[i];  // Copy for local modification

    try {
      // Use SIMD-precomputed distances for matched entities
      // Staggered update: only update this entity if its index matches the current slice
      size_t localIdx = i - start;
      if (distanceUpdateSlice != UINT64_MAX && hasPlayer && (i % 16 == distanceUpdateSlice) &&
          precomputedDistances[localIdx] >= 0.0f) {
        // Use precomputed SIMD results (2-4x faster than scalar)
        hotData.distanceSquared = precomputedDistances[localIdx];
        hotData.position = precomputedPositions[localIdx];
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
        // PERFORMANCE: Use shared_ptr only for executeLogic (required by interface)
        // This is the only place we need shared ownership semantics
        behavior->executeLogic(storage.entities[i], deltaTime);

        // OPTIMIZATION: Only update animations/sprites for entities near the player
        // Off-screen entities still think (behavior logic runs) but don't animate
        // This saves 75% of entity->update() calls for typical 4000 NPC scenarios
        // Buffer distance: 1500 pixels (typical screen width @ 1080p)
        constexpr float VISUAL_UPDATE_DISTANCE_SQ = 1500.0f * 1500.0f; // ~1.5 screens
        bool isNearPlayer = (hotData.distanceSquared <= VISUAL_UPDATE_DISTANCE_SQ);

        if (isNearPlayer) {
          // Entity updates integrate their own movement AND handle animations
          entity->update(deltaTime);
        }

        // Centralized clamp pass using cached world bounds (NO atomic loads)
        // halfW and halfH already loaded from pre-fetched data above

        Vector2D pos = entity->getPosition();
        Vector2D vel = entity->getVelocity();

        // MOVEMENT INTEGRATION: Apply velocity to position (core physics step)
        // AIManager is responsible for entity movement - CollisionManager only modifies
        // positions when collision RESOLUTION occurs (pushing bodies apart)
        pos = pos + (vel * deltaTime);

        // Inline clamping - no function call, no atomic load
        float minX = halfW;
        float maxX = worldWidth - halfW;
        float minY = halfH;
        float maxY = worldHeight - halfH;
        Vector2D clamped(
            std::clamp(pos.getX(), minX, maxX),
            std::clamp(pos.getY(), minY, maxY)
        );

        // Update entity position directly - AIManager owns entity movement
        entity->Entity::setPosition(clamped); // Use base Entity::setPosition to avoid collision sync

        // Handle boundary collisions: stop velocity at world edges
        if (clamped.getX() != pos.getX() || clamped.getY() != pos.getY()) {
          // Project velocity inward for boundary collisions
          if (clamped.getX() < pos.getX() && vel.getX() < 0) vel.setX(0.0f);
          if (clamped.getX() > pos.getX() && vel.getX() > 0) vel.setX(0.0f);
          if (clamped.getY() < pos.getY() && vel.getY() < 0) vel.setY(0.0f);
          if (clamped.getY() > pos.getY() && vel.getY() > 0) vel.setY(0.0f);
          entity->Entity::setVelocity(vel); // Use base Entity::setVelocity
        }

        pos = clamped; // Update pos for batch accumulation

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
      AI_ERROR("Error in batch processing entity: " + std::string(e.what()));
      // NOTE: Don't decrement active counter here - entity is still active, just had an error
      // The entity will be properly cleaned up in the next cleanup cycle
      // Decrementing here could cause count drift and make the counter unreliable
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

#if defined(HAMMER_SIMD_SSE2)
  // SSE2: Process 4 entities at once for distance calculations using SIMDMath
  const float playerX = playerPos.getX();
  const float playerY = playerPos.getY();
  const Float4 playerPosX = broadcast(playerX);
  const Float4 playerPosY = broadcast(playerY);

  size_t i = 0;
  const size_t simdEnd = (entityCount / 4) * 4;

  for (; i < simdEnd; i += 4) {
    // Check if all 4 entities in this batch are active
    bool allActive = m_storage.hotData[i].active && m_storage.entities[i] &&
                     m_storage.hotData[i+1].active && m_storage.entities[i+1] &&
                     m_storage.hotData[i+2].active && m_storage.entities[i+2] &&
                     m_storage.hotData[i+3].active && m_storage.entities[i+3];

    if (!allActive) {
      // Fall back to scalar for this batch
      for (size_t j = i; j < i + 4 && j < entityCount; ++j) {
        auto &hotData = m_storage.hotData[j];
        if (hotData.active && m_storage.entities[j]) {
          Vector2D entityPos = m_storage.entities[j]->getPosition();
          Vector2D diff = entityPos - playerPos;
          hotData.distanceSquared = diff.lengthSquared();
          hotData.position = entityPos;
          updatedCount++;
        }
      }
      continue;
    }

    // Load 4 entity X positions
    Float4 entityX = set(
      m_storage.entities[i]->getPosition().getX(),
      m_storage.entities[i+1]->getPosition().getX(),
      m_storage.entities[i+2]->getPosition().getX(),
      m_storage.entities[i+3]->getPosition().getX()
    );

    // Load 4 entity Y positions
    Float4 entityY = set(
      m_storage.entities[i]->getPosition().getY(),
      m_storage.entities[i+1]->getPosition().getY(),
      m_storage.entities[i+2]->getPosition().getY(),
      m_storage.entities[i+3]->getPosition().getY()
    );

    // Calculate differences
    Float4 diffX = sub(entityX, playerPosX);
    Float4 diffY = sub(entityY, playerPosY);

    // Calculate squared distances: diffX² + diffY²
    Float4 distSqX = mul(diffX, diffX);
    Float4 distSqY = mul(diffY, diffY);
    Float4 distSq = add(distSqX, distSqY);

    // Store results
    alignas(16) float distSquaredArray[4];
    store4(distSquaredArray, distSq);

    // Update hot data with results (no reverse order needed with set())
    for (size_t j = 0; j < 4; ++j) {
      auto &hotData = m_storage.hotData[i + j];
      hotData.distanceSquared = distSquaredArray[j];
      hotData.position = m_storage.entities[i + j]->getPosition();
      updatedCount++;
    }
  }

  // Scalar tail for remaining entities
  for (; i < entityCount; ++i) {
    auto &hotData = m_storage.hotData[i];
    if (hotData.active && m_storage.entities[i]) {
      Vector2D entityPos = m_storage.entities[i]->getPosition();
      Vector2D diff = entityPos - playerPos;
      hotData.distanceSquared = diff.lengthSquared();
      hotData.position = entityPos;
      updatedCount++;
    }
  }

#elif defined(HAMMER_SIMD_NEON)
  // ARM NEON: Process 4 entities at once for distance calculations using SIMDMath
  const float playerX = playerPos.getX();
  const float playerY = playerPos.getY();
  const Float4 playerPosX = broadcast(playerX);
  const Float4 playerPosY = broadcast(playerY);

  size_t i = 0;
  const size_t simdEnd = (entityCount / 4) * 4;

  for (; i < simdEnd; i += 4) {
    // Check if all 4 entities in this batch are active
    bool allActive = m_storage.hotData[i].active && m_storage.entities[i] &&
                     m_storage.hotData[i+1].active && m_storage.entities[i+1] &&
                     m_storage.hotData[i+2].active && m_storage.entities[i+2] &&
                     m_storage.hotData[i+3].active && m_storage.entities[i+3];

    if (!allActive) {
      // Fall back to scalar for this batch
      for (size_t j = i; j < i + 4 && j < entityCount; ++j) {
        auto &hotData = m_storage.hotData[j];
        if (hotData.active && m_storage.entities[j]) {
          Vector2D entityPos = m_storage.entities[j]->getPosition();
          Vector2D diff = entityPos - playerPos;
          hotData.distanceSquared = diff.lengthSquared();
          hotData.position = entityPos;
          updatedCount++;
        }
      }
      continue;
    }

    // Load 4 entity positions
    Float4 entityX = set(
      m_storage.entities[i]->getPosition().getX(),
      m_storage.entities[i+1]->getPosition().getX(),
      m_storage.entities[i+2]->getPosition().getX(),
      m_storage.entities[i+3]->getPosition().getX()
    );
    Float4 entityY = set(
      m_storage.entities[i]->getPosition().getY(),
      m_storage.entities[i+1]->getPosition().getY(),
      m_storage.entities[i+2]->getPosition().getY(),
      m_storage.entities[i+3]->getPosition().getY()
    );

    // Calculate differences
    Float4 diffX = sub(entityX, playerPosX);
    Float4 diffY = sub(entityY, playerPosY);

    // Calculate squared distances: diffX² + diffY² using fused multiply-add
    Float4 distSq = madd(diffY, diffY, mul(diffX, diffX));

    // Store results
    alignas(16) float distSquaredArray[4];
    store4(distSquaredArray, distSq);

    // Update hot data with results
    for (size_t j = 0; j < 4; ++j) {
      auto &hotData = m_storage.hotData[i + j];
      hotData.distanceSquared = distSquaredArray[j];
      hotData.position = m_storage.entities[i + j]->getPosition();
      updatedCount++;
    }
  }

  // Scalar tail for remaining entities
  for (; i < entityCount; ++i) {
    auto &hotData = m_storage.hotData[i];
    if (hotData.active && m_storage.entities[i]) {
      Vector2D entityPos = m_storage.entities[i]->getPosition();
      Vector2D diff = entityPos - playerPos;
      hotData.distanceSquared = diff.lengthSquared();
      hotData.position = entityPos;
      updatedCount++;
    }
  }

#else
  // Scalar fallback
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
#endif

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
