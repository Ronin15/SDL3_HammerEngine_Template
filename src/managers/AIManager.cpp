/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "managers/CollisionManager.hpp"
#include "collisions/AABB.hpp"
#include "core/WorkerBudget.hpp"
#include "entities/NPC.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/WorldEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cstring>

bool AIManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    AI_LOG("AIManager already initialized");
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

    // Subscribe to world events to maintain pathfinding grid
    {
      auto &em = EventManager::Instance();
      static std::vector<EventManager::HandlerToken> s_tokens;
      s_tokens.push_back(em.registerHandlerWithToken(
          EventTypeId::World, [this](const EventData &data) {
            auto base = data.event;
            if (!base)
              return;
            if (std::dynamic_pointer_cast<WorldLoadedEvent>(base) ||
                std::dynamic_pointer_cast<WorldGeneratedEvent>(base)) {
              const auto *world = WorldManager::Instance().getWorldData();
              if (world) {
                int h = static_cast<int>(world->grid.size());
                int w = (h > 0) ? static_cast<int>(world->grid[0].size()) : 0;
                m_pathGrid.reset(new HammerEngine::PathfindingGrid(
                    w, h, 32.0f, Vector2D(0, 0)));
                m_pathGrid->rebuildFromWorld();
              }
              return;
            }
            if (std::dynamic_pointer_cast<TileChangedEvent>(base)) {
              if (m_pathGrid)
                m_pathGrid->rebuildFromWorld();
              return;
            }
            if (std::dynamic_pointer_cast<WorldUnloadedEvent>(base)) {
              m_pathGrid.reset();
              return;
            }
          }));
    }

    AI_LOG("AIManager initialized successfully");
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

  AI_LOG("AIManager shutting down...");

  // Mark as shutting down
  m_isShutdown = true;
  m_initialized.store(false, std::memory_order_release);

  // Stop accepting new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // Clean up all entities and behaviors
  // Wait for any pending async assignments to complete before shutdown
  if (!m_assignmentFutures.empty()) {
    AI_LOG("Waiting for " + std::to_string(m_assignmentFutures.size()) +
           " async assignment batches to complete...");

    // Check if ThreadSystem still exists - if not, futures are invalid
    if (HammerEngine::ThreadSystem::Exists() &&
        !HammerEngine::ThreadSystem::Instance().isShutdown()) {
      for (auto &future : m_assignmentFutures) {
        if (future.valid()) {
          try {
            // Use a short timeout instead of blocking indefinitely
            auto status = future.wait_for(std::chrono::milliseconds(50));
            if (status == std::future_status::ready) {
              future.get();
            } else {
              AI_WARN("Async assignment batch did not complete within timeout "
                      "during shutdown");
            }
          } catch (const std::exception &e) {
            AI_ERROR("Exception in async assignment batch during shutdown: " +
                     std::string(e.what()));
          } catch (...) {
            AI_ERROR(
                "Unknown exception in async assignment batch during shutdown");
          }
        }
      }
    } else {
      AI_WARN("ThreadSystem already shutdown - abandoning " +
              std::to_string(m_assignmentFutures.size()) +
              " pending assignment futures");
    }

    m_assignmentFutures.clear();
    m_assignmentInProgress.store(false, std::memory_order_release);
  }

  // Ensure any in-flight update futures are completed before clearing storage
  if (!m_updateFutures.empty()) {
    AI_LOG("Waiting for " + std::to_string(m_updateFutures.size()) +
           " AI update futures to complete...");
    for (auto &future : m_updateFutures) {
      if (future.valid()) {
        try {
          future.wait();
          future.get();
        } catch (const std::exception &e) {
          AI_ERROR("Exception in AI update future during shutdown: " +
                   std::string(e.what()));
        } catch (...) {
          AI_ERROR("Unknown exception in AI update future during shutdown");
        }
      }
    }
    m_updateFutures.clear();
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

  // Reset all counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);

  AI_LOG("AIManager shutdown complete");
}

void AIManager::prepareForStateTransition() {
  AI_LOG("Preparing AIManager for state transition...");

  // Pause AI processing to prevent new tasks
  m_globallyPaused.store(true, std::memory_order_release);

  // Wait for any in-flight operations to complete
  if (!m_updateFutures.empty()) {
    AI_DEBUG("Waiting for " + std::to_string(m_updateFutures.size()) + " AI update futures to complete...");
    for (auto &future : m_updateFutures) {
      if (future.valid()) {
        try {
          future.get();
        } catch (const std::exception &e) {
          AI_ERROR("Exception in AI update future during state transition: " + std::string(e.what()));
        }
      }
    }
    m_updateFutures.clear();
  }

  // Wait for async assignments to complete
  if (!m_assignmentFutures.empty()) {
    AI_DEBUG("Waiting for " + std::to_string(m_assignmentFutures.size()) + " async assignments to complete...");
    for (auto &future : m_assignmentFutures) {
      if (future.valid()) {
        try {
          auto status = future.wait_for(std::chrono::milliseconds(100));
          if (status == std::future_status::ready) {
            future.get();
          } else {
            AI_WARN("Async assignment did not complete within timeout during state transition");
          }
        } catch (const std::exception &e) {
          AI_ERROR("Exception in async assignment during state transition: " + std::string(e.what()));
        }
      }
    }
    m_assignmentFutures.clear();
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
    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.behaviors[i] && m_storage.entities[i]) {
        try {
          m_storage.behaviors[i]->clean(m_storage.entities[i]);
          AI_DEBUG("Cleaned " + m_storage.behaviors[i]->getName() + " for entity " + 
                   std::to_string(m_storage.entities[i]->getID()));
        } catch (const std::exception &e) {
          AI_ERROR("Exception cleaning behavior: " + std::string(e.what()));
        }
      }
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

  // Clear pathfinding state completely
  m_entityPaths.clear();
  m_pathCooldownUntil.clear();
  
  // Clear async pathfinding state
  {
    std::lock_guard<std::mutex> lock(m_asyncPathMutex);
    m_asyncEntityPaths.clear();
    m_asyncPathTimestamps.clear();
  }
  {
    std::lock_guard<std::mutex> lock(m_asyncQueueMutex);
    // Clear the queue properly
    std::queue<AsyncPathRequest> empty;
    m_asyncPathQueue.swap(empty);
  }
  
  // Reset async pathfinding counters
  m_asyncPathsRequested.store(0, std::memory_order_relaxed);
  m_asyncPathsProcessed.store(0, std::memory_order_relaxed);

  // Reset all counters and stats
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);
  m_lastCleanupFrame.store(0, std::memory_order_relaxed);
  
  // Reset performance tracking
  {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_globalStats = AIPerformanceStats{};
    for (auto& stat : m_behaviorStats) {
      stat = AIPerformanceStats{};
    }
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

  // Don't call resetBehaviors() as we've already done comprehensive cleanup above
  // Just clear the behavior caches
  {
    std::lock_guard<std::shared_mutex> lock(m_behaviorsMutex);
    m_behaviorCache.clear();
    m_behaviorTypeCache.clear();
  }

  // Reset pause state to false so next state starts unpaused
  m_globallyPaused.store(false, std::memory_order_release);

  AI_LOG("AIManager state transition complete - all state cleared and reset");
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

    // Synchronize positions and count active entities
    size_t entityCount = 0;
    size_t activeCount = 0;
    {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      entityCount = m_storage.size();
      for (size_t i = 0; i < entityCount; ++i) {
        if (m_storage.hotData[i].active && m_storage.entities[i]) {
          m_storage.hotData[i].position = m_storage.entities[i]->getPosition();
          
          // Increment frame counter for each active entity (with overflow protection)
          if (m_storage.hotData[i].frameCounter < UINT16_MAX) {
            m_storage.hotData[i].frameCounter++;
          }
          
          ++activeCount;
        }
      }
    }

    if (entityCount == 0) {
      return;
    }

    // If there are no active entities, skip heavy processing this frame
    if (activeCount == 0) {
      processMessageQueue();
      m_lastWasThreaded.store(false, std::memory_order_relaxed);
      return;
    }

    // Lock-free double buffer swap
    int currentBuffer = m_storage.currentBuffer.load(std::memory_order_acquire);
    int nextBuffer = 1 - currentBuffer;

    // Get player position for distance calculations (only every 4th frame to
    // reduce CPU usage)
    EntityPtr player = m_playerEntity.lock();
    bool distancesUpdated = false;
    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);
    if (player && (currentFrame % 4 == 0)) {
      Vector2D playerPos = player->getPosition();

      // Simple scalar distance updates (more efficient for scattered memory
      // access)
      updateDistancesScalar(playerPos);
      distancesUpdated = true;
    }

    // THREAD-SAFE DOUBLE BUFFERING: Copy data BEFORE starting async tasks
    // This prevents async workers from modifying data during buffer operations
    bool entityCountChanged =
        (m_storage.doubleBuffer[nextBuffer].size() != m_storage.hotData.size());

    if (distancesUpdated || currentFrame % 60 == 0 || entityCountChanged) {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      m_storage.doubleBuffer[nextBuffer] = m_storage.hotData;
    } else {
      // Use current buffer data
      nextBuffer = currentBuffer;
    }

    // Determine threading strategy based on ACTIVE entity count instead of
    // total storage size to avoid unnecessary threading after resets
    bool useThreading = (activeCount >= THREADING_THRESHOLD &&
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
          (queueCapacity * 9) / 10; // 90% capacity threshold

      if (queueSize > pressureThreshold) {
        // Graceful degradation: fallback to single-threaded processing
        AI_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                 std::to_string(queueCapacity) +
                 "), using single-threaded processing");
        m_lastWasThreaded.store(false, std::memory_order_relaxed);
        processBatch(0, entityCount, deltaTime, nextBuffer);

        // Swap buffers atomically
        if (nextBuffer != currentBuffer) {
          m_storage.currentBuffer.store(nextBuffer, std::memory_order_release);
        }
        // Single-threaded path completes within this frame; continue to stats
      }

      HammerEngine::WorkerBudget budget =
          HammerEngine::calculateWorkerBudget(availableWorkers);

      // Use WorkerBudget system properly with threshold-based buffer allocation
      size_t optimalWorkerCount =
          budget.getOptimalWorkerCount(budget.aiAllocated, entityCount, 1000);

      // Store thread allocation info for debug output
      m_lastOptimalWorkerCount.store(optimalWorkerCount,
                                     std::memory_order_relaxed);
      m_lastAvailableWorkers.store(availableWorkers, std::memory_order_relaxed);
      m_lastAIBudget.store(budget.aiAllocated, std::memory_order_relaxed);
      m_lastWasThreaded.store(true, std::memory_order_relaxed);

      // Dynamic batch sizing based on queue pressure for optimal performance
      size_t minEntitiesPerBatch = 1000;
      size_t maxBatches = 4;

      // Adjust batch strategy based on queue pressure
      double queuePressure = static_cast<double>(queueSize) / queueCapacity;
      if (queuePressure > 0.5) {
        // High pressure: use fewer, larger batches to reduce queue overhead
        minEntitiesPerBatch = 1500;
        maxBatches = 2;
        AI_DEBUG("High queue pressure (" +
                 std::to_string(static_cast<int>(queuePressure * 100)) +
                 "%), using larger batches");
      } else if (queuePressure < 0.25) {
        // Low pressure: can use more batches for better parallelization
        minEntitiesPerBatch = 800;
        maxBatches = 4;
      }

      size_t batchCount =
          std::min(optimalWorkerCount, entityCount / minEntitiesPerBatch);
      batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));

      // Debug thread allocation info periodically
      if (currentFrame % 300 == 0 && entityCount > 0) {
        AI_DEBUG("Thread Allocation - Workers: " +
                 std::to_string(optimalWorkerCount) + "/" +
                 std::to_string(availableWorkers) +
                 ", AI Budget: " + std::to_string(budget.aiAllocated) +
                 ", Batches: " + std::to_string(batchCount));
      }

      size_t entitiesPerBatch = entityCount / batchCount;
      size_t remainingEntities = entityCount % batchCount;

      // Batch processing with futures; synchronize before returning from
      // update()
      m_updateFutures.clear();
      m_updateFutures.reserve(batchCount);
      for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * entitiesPerBatch;
        size_t end = start + entitiesPerBatch;

        // Add remaining entities to last batch
        if (i == batchCount - 1) {
          end += remainingEntities;
        }

        // Enqueue with result and retain the future for synchronization
        m_updateFutures.push_back(threadSystem.enqueueTaskWithResult(
            [this, start, end, deltaTime, nextBuffer]() {
              processBatch(start, end, deltaTime, nextBuffer);
            },
            HammerEngine::TaskPriority::High, "AI_OptimalBatch"));
      }
      // Wait for all batches to complete to maintain update->render safety
      for (auto &f : m_updateFutures) {
        if (f.valid()) {
          try {
            f.get();
          } catch (const std::exception &e) {
            AI_ERROR(std::string("Exception in AI batch future: ") + e.what());
          } catch (...) {
            AI_ERROR("Unknown exception in AI batch future");
          }
        }
      }
      m_updateFutures.clear();

    } else {
      // Single-threaded processing
      processBatch(0, entityCount, deltaTime, nextBuffer);
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
      cleanupInactiveEntities();
      
      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);

      std::lock_guard<std::mutex> statsLock(m_statsMutex);
      m_globalStats.addSample(duration, entityCount);

      if (entityCount > 0) {
        double avgDuration =
            m_globalStats.updateCount > 0
                ? (m_globalStats.totalUpdateTime / m_globalStats.updateCount)
                : 0.0;

        bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);
        if (wasThreaded) {
          size_t optimalWorkers =
              m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
          size_t availableWorkers =
              m_lastAvailableWorkers.load(std::memory_order_relaxed);
          size_t aiBudget = m_lastAIBudget.load(std::memory_order_relaxed);

          AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                   ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                   ", Entities/sec: " +
                   std::to_string(m_globalStats.entitiesPerSecond) +
                   " [Threaded: " + std::to_string(optimalWorkers) + "/" +
                   std::to_string(availableWorkers) +
                   " workers, Budget: " + std::to_string(aiBudget) + "]");
        } else {
          AI_DEBUG("AI Summary - Entities: " + std::to_string(entityCount) +
                   ", Avg Update: " + std::to_string(avgDuration) + "ms" +
                   ", Entities/sec: " +
                   std::to_string(m_globalStats.entitiesPerSecond) +
                   " [Single-threaded]");
        }
      }
    }

  } catch (const std::exception &e) {
    AI_ERROR("Exception in AIManager::update: " + std::string(e.what()));
  }
}

void AIManager::waitForUpdatesToComplete() {
  if (m_isShutdown) {
    return;
  }

  // Wait for all futures from the last update to complete
  for (auto &future : m_updateFutures) {
    if (future.valid()) {
      future.get();
    }
  }
  // Clear the futures vector for the next update cycle
  m_updateFutures.clear();
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

  AI_LOG("Registered behavior: " + name);
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
      m_storage.hotData[index].active = true;

      AI_LOG("Updated behavior for existing entity to: " + behaviorName);
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
    m_storage.lastUpdateTimes.push_back(0.0f);

    // Update index map
    m_entityToIndex[entity] = newIndex;

    AI_LOG("Added new entity with behavior: " + behaviorName);
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
      m_storage.hotData[index].active = false;

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
  // First, check if any previous async assignments have completed
  if (!m_assignmentFutures.empty()) {
    auto it = m_assignmentFutures.begin();
    while (it != m_assignmentFutures.end()) {
      if (it->wait_for(std::chrono::nanoseconds(0)) ==
          std::future_status::ready) {
        try {
          it->get(); // This won't block since we know it's ready
        } catch (const std::exception &e) {
          AI_ERROR("Exception in async assignment batch: " +
                   std::string(e.what()));
        } catch (...) {
          AI_ERROR("Unknown exception in async assignment batch");
        }
        it = m_assignmentFutures.erase(it);
      } else {
        ++it;
      }
    }

    // If all futures completed, reset the flag
    if (m_assignmentFutures.empty()) {
      m_assignmentInProgress.store(false, std::memory_order_release);
    }
  }

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
    m_pendingAssignments.clear();
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

  // Clear any old futures (should be empty at this point)
  m_assignmentFutures.clear();

  // Submit async batches - NO BLOCKING WAITS
  size_t start = 0;
  for (size_t i = 0; i < batchCount; ++i) {
    size_t end =
        start + assignmentsPerBatch + (i == batchCount - 1 ? remaining : 0);

    // Copy the batch data (we need to own this data for async processing)
    std::vector<PendingAssignment> batchData(toProcess.begin() + start,
                                             toProcess.begin() + end);

    m_assignmentFutures.push_back(threadSystem.enqueueTaskWithResult(
        [this, batchData = std::move(batchData)]() {
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

  AI_DEBUG("Started async assignment processing for " +
           std::to_string(assignmentCount) + " entities in " +
           std::to_string(batchCount) + " batches");
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
    m_storage.hotData[it->second].active = false;
  }
}

void AIManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
  AI_LOG((paused ? "AI processing paused" : "AI processing resumed"));
}

bool AIManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

void AIManager::resetBehaviors() {
  AI_LOG("Resetting all AI behaviors");

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

  AI_LOG("Threading configured: " +
         std::string(useThreading ? "enabled" : "disabled") +
         " with max threads: " + std::to_string(m_maxThreads));
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
                             int bufferIndex) {
  // Work on the double buffer for lock-free operation
  auto &workBuffer = m_storage.doubleBuffer[bufferIndex];

  // Get current player position
  EntityPtr player = m_playerEntity.lock();

  size_t batchExecutions = 0;

  // Pre-calculate common values once per batch to reduce per-entity overhead
  float maxDist = m_maxUpdateDistance.load(std::memory_order_relaxed);
  float maxDistSquared = maxDist * maxDist;
  bool hasPlayer = (player != nullptr);

  // Pre-cache entities and behaviors for the entire batch to reduce lock
  // contention
  std::vector<EntityPtr> batchEntities;
  std::vector<std::shared_ptr<AIBehavior>> batchBehaviors;
  batchEntities.reserve(end - start);
  batchBehaviors.reserve(end - start);

  // Single lock acquisition for the entire batch
  {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    for (size_t i = start; i < end && i < m_storage.size(); ++i) {
      batchEntities.push_back(m_storage.entities[i]);
      batchBehaviors.push_back(m_storage.behaviors[i]);
    }
  }

  // Process entities without locks
  for (size_t idx = 0; idx < batchEntities.size(); ++idx) {
    size_t i = start + idx;
    if (i >= workBuffer.size())
      break;

    auto &hotData = workBuffer[i];
    if (!hotData.active)
      continue;

    EntityPtr entity = batchEntities[idx];
    std::shared_ptr<AIBehavior> behavior = batchBehaviors[idx];

    if (!entity || !behavior) {
      hotData.active = false;
      continue;
    }

    try {
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

        // The entity is responsible for its own physics update.
        entity->update(deltaTime);

        batchExecutions++;
      } else {
        // If culled, explicitly stop the entity to prevent ghost movement.
        entity->setVelocity(Vector2D(0, 0));
        // We still call update to apply friction (which will do nothing at zero
        // velocity) and to ensure the animation state is correctly reset to
        // idle.
        entity->update(deltaTime);
      }
    } catch (const std::exception &e) {
      AI_ERROR("Error in batch processing: " + std::string(e.what()));
      hotData.active = false;
    }
  }

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions,
                                        std::memory_order_relaxed);
  }
}

void AIManager::updateDistancesScalar(const Vector2D &playerPos) {
  size_t entityCount = m_storage.hotData.size();

  // Simple scalar implementation - only update active entities
  // Skip inactive entities to reduce CPU usage significantly
  size_t updatedCount = 0;
  for (size_t i = 0; i < entityCount; ++i) {
    auto &hotData = m_storage.hotData[i];
    if (hotData.active) {
      Vector2D diff = hotData.position - playerPos;
      hotData.distanceSquared = diff.lengthSquared();
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

// ======================= Pathfinding API (synchronous) =======================
uint32_t AIManager::requestPath(EntityPtr entity, const Vector2D &start,
                                const Vector2D &goal) {
  if (!entity)
    return 0;
  if (!m_pathGrid) {
    return 0;
  }
  uint64_t id = entity->getID();
  Uint64 now = SDL_GetTicks();
  auto itCool = m_pathCooldownUntil.find(id);
  if (itCool != m_pathCooldownUntil.end() && now < itCool->second) {
    return 0; // backoff active
  }
  // Faction-aware avoidance weights
  m_pathGrid->resetWeights(1.0f);
  // Determine requester's faction (Player = Friendly by default)
  enum class Faction { Friendly, Enemy, Neutral };
  auto getFaction = [](EntityPtr e) {
    if (!e)
      return Faction::Neutral;
    if (auto npc = std::dynamic_pointer_cast<class NPC>(e)) {
      // Map NPC::Faction to local enum
      switch (npc->getFaction()) {
      case NPC::Faction::Friendly:
        return Faction::Friendly;
      case NPC::Faction::Enemy:
        return Faction::Enemy;
      case NPC::Faction::Neutral:
      default:
        return Faction::Neutral;
      }
    }
    // Player or unknown: treat as Friendly
    return Faction::Friendly;
  };

  Faction requester = getFaction(entity);

  // Add avoidance around hostiles and a mild cost around allies to reduce
  // clumping
  {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    for (size_t i = 0; i < m_storage.size(); ++i) {
      EntityPtr other = m_storage.entities[i];
      if (!other || other.get() == entity.get())
        continue;
      Faction otherF = getFaction(other);
      Vector2D pos = other->getPosition();
      if ((requester == Faction::Friendly && otherF == Faction::Enemy) ||
          (requester == Faction::Enemy && otherF == Faction::Friendly)) {
        m_pathGrid->addWeightCircle(pos, 96.0f, 2.5f);
      } else {
        m_pathGrid->addWeightCircle(pos, 64.0f, 1.2f);
      }
    }
  }

  std::vector<Vector2D> path;
  auto result = m_pathGrid->findPath(start, goal, path);
  if (result == HammerEngine::PathfindingResult::SUCCESS && !path.empty()) {
    m_entityPaths[id] = std::move(path);
    return 1;
  }
  // store empty to signal no path
  m_entityPaths[id] = {};
  // apply cooldown on failure to avoid spamming
  m_pathCooldownUntil[id] = now + 1000; // 1 second backoff
  return 0;
}

bool AIManager::hasPath(EntityPtr entity) const {
  if (!entity)
    return false;
  auto it = m_entityPaths.find(entity->getID());
  return it != m_entityPaths.end() && !it->second.empty();
}

std::vector<Vector2D> AIManager::getPath(EntityPtr entity) const {
  if (!entity)
    return {};
  auto it = m_entityPaths.find(entity->getID());
  if (it != m_entityPaths.end())
    return it->second;
  return {};
}

void AIManager::clearPath(EntityPtr entity) {
  if (!entity)
    return;
  m_entityPaths.erase(entity->getID());
}

// ======================= Async Pathfinding API =======================

void AIManager::requestPathAsync(EntityPtr entity, const Vector2D &start, const Vector2D &goal,
                                  PathPriority priority, 
                                  std::function<void(EntityID, const std::vector<Vector2D>&)> callback) {
  if (!entity) return;
  requestPathAsync(entity->getID(), start, goal, priority, callback);
}

void AIManager::requestPathAsync(EntityID entityId, const Vector2D &start, const Vector2D &goal,
                                 PathPriority priority,
                                 std::function<void(EntityID, const std::vector<Vector2D>&)> callback) {
  if (!m_asyncPathfindingEnabled.load()) {
    // Fall back to synchronous pathfinding if async is disabled
    // Find entity by scanning storage (temporary until we add proper lookup)
    EntityPtr entity = nullptr;
    {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      for (size_t i = 0; i < m_storage.size(); ++i) {
        if (m_storage.entities[i] && m_storage.entities[i]->getID() == entityId) {
          entity = m_storage.entities[i];
          break;
        }
      }
    }
    if (entity) {
      requestPath(entity, start, goal);
    }
    return;
  }

  // Check cooldown
  Uint64 now = SDL_GetTicks();
  auto cooldownIt = m_pathCooldownUntil.find(entityId);
  if (cooldownIt != m_pathCooldownUntil.end() && now < cooldownIt->second) {
    return; // Still in cooldown
  }

  {
    std::lock_guard<std::mutex> lock(m_asyncQueueMutex);
    m_asyncPathQueue.emplace(entityId, start, goal, priority, callback);
    m_asyncPathsRequested.fetch_add(1, std::memory_order_relaxed);
  }

  // Submit pathfinding task to ThreadSystem with appropriate priority
  HammerEngine::TaskPriority taskPriority;
  switch (priority) {
    case PathPriority::Critical: taskPriority = HammerEngine::TaskPriority::Critical; break;
    case PathPriority::High: taskPriority = HammerEngine::TaskPriority::High; break;
    case PathPriority::Normal: taskPriority = HammerEngine::TaskPriority::Normal; break;
    case PathPriority::Low: taskPriority = HammerEngine::TaskPriority::Low; break;
  }

  // Capture entityId for the task description
  std::string taskDesc = "AsyncPathfinding_" + std::to_string(entityId);
  
  HammerEngine::ThreadSystem::Instance().enqueueTask(
    [this, entityId, start, goal]() { 
      this->processAsyncPathRequest(entityId, start, goal); 
    },
    taskPriority,
    taskDesc
  );
}

bool AIManager::hasAsyncPath(EntityPtr entity) const {
  return entity ? hasAsyncPath(entity->getID()) : false;
}

bool AIManager::hasAsyncPath(EntityID entityId) const {
  std::lock_guard<std::mutex> lock(m_asyncPathMutex);
  auto it = m_asyncEntityPaths.find(entityId);
  return it != m_asyncEntityPaths.end() && !it->second.empty();
}

std::vector<Vector2D> AIManager::getAsyncPath(EntityPtr entity) const {
  return entity ? getAsyncPath(entity->getID()) : std::vector<Vector2D>{};
}

std::vector<Vector2D> AIManager::getAsyncPath(EntityID entityId) const {
  std::lock_guard<std::mutex> lock(m_asyncPathMutex);
  auto it = m_asyncEntityPaths.find(entityId);
  return it != m_asyncEntityPaths.end() ? it->second : std::vector<Vector2D>{};
}

void AIManager::clearAsyncPath(EntityPtr entity) {
  if (entity) clearAsyncPath(entity->getID());
}

void AIManager::clearAsyncPath(EntityID entityId) {
  std::lock_guard<std::mutex> lock(m_asyncPathMutex);
  m_asyncEntityPaths.erase(entityId);
  m_asyncPathTimestamps.erase(entityId);
}

size_t AIManager::getAsyncPathQueueSize() const {
  std::lock_guard<std::mutex> lock(m_asyncQueueMutex);
  return m_asyncPathQueue.size();
}

void AIManager::processAsyncPathResults() {
  // This method is called from the main thread to handle any callbacks
  // The actual pathfinding work is done in processAsyncPathBatch()
}

void AIManager::forceUnstickEntity(EntityPtr entity) {
  if (!entity) {
    AI_ERROR("Cannot unstick null entity");
    return;
  }

  EntityID entityId = entity->getID();
  AI_WARN("Force unsticking entity " + std::to_string(entityId));

  // Simple approach: clear pathfinding state and give velocity nudge
  clearPath(entity);
  m_pathCooldownUntil.erase(entityId);

  // Apply simple random velocity nudge
  float angle = ((entityId * 17) % 360) * M_PI / 180.0f;
  Vector2D nudgeVel(cosf(angle) * 50.0f, sinf(angle) * 50.0f);
  entity->setVelocity(nudgeVel);
  
  AI_INFO("Simple unstick applied to entity " + std::to_string(entityId));
}

bool AIManager::isEntityStalled(EntityPtr entity) const {
  if (!entity) return false;
  
  // Simplified stall detection - just check velocity
  Vector2D velocity = entity->getVelocity();
  return velocity.length() < 0.5f;
}

void AIManager::logEntityDiagnostics(EntityPtr entity) const {
  if (!entity) return;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  auto it = m_entityToIndex.find(entity);
  if (it != m_entityToIndex.end() && it->second < m_storage.size()) {
    size_t index = it->second;
    const auto& hotData = m_storage.hotData[index];
    Vector2D velocity = entity->getVelocity();
    
    std::string behaviorName = "Unknown";
    if (m_storage.behaviors[index]) {
      behaviorName = m_storage.behaviors[index]->getName();
    }
    
    AI_DEBUG("Entity " + std::to_string(entity->getID()) + " Diagnostics:");
    AI_DEBUG("  Behavior: " + behaviorName);
    AI_DEBUG("  Position: (" + std::to_string(hotData.position.getX()) + ", " + 
             std::to_string(hotData.position.getY()) + ")");
    AI_DEBUG("  Velocity: (" + std::to_string(velocity.getX()) + ", " + 
             std::to_string(velocity.getY()) + ") Speed: " + std::to_string(velocity.length()));
    AI_DEBUG("  Active: " + std::string(hotData.active ? "true" : "false"));
    AI_DEBUG("  Priority: " + std::to_string(hotData.priority));
    AI_DEBUG("  Distance to Player: " + std::to_string(std::sqrt(hotData.distanceSquared)));
    
    // Check if entity has a path
    bool hasActivePath = hasPath(entity);
    AI_DEBUG("  Has Path: " + std::string(hasActivePath ? "true" : "false"));
    
    if (hasActivePath) {
      auto path = getPath(entity);
      AI_DEBUG("  Path Length: " + std::to_string(path.size()));
    }
  }
}

size_t AIManager::checkAndDisperseClusters(EntityPtr entity) {
  if (!entity) return 0;

  auto& cm = CollisionManager::Instance();
  Vector2D entityPos = entity->getPosition();
  
  // Query for nearby entities
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();
  
  const float CLUSTER_RADIUS = 60.0f; // Reasonable detection radius
  HammerEngine::AABB area(entityPos.getX() - CLUSTER_RADIUS, entityPos.getY() - CLUSTER_RADIUS,
                          CLUSTER_RADIUS * 2.0f, CLUSTER_RADIUS * 2.0f);
  cm.queryArea(area, queryResults);
  
  // Count entities in close proximity
  size_t closeNeighbors = 0;
  const float CLOSE_DISTANCE = 40.0f;
  for (EntityID id : queryResults) {
    if (id == entity->getID()) continue;
    if (!cm.isDynamic(id) || cm.isTrigger(id)) continue;
    Vector2D other;
    if (!cm.getBodyCenter(id, other)) continue;
    float dist = (entityPos - other).length();
    if (dist < CLOSE_DISTANCE) {
      closeNeighbors++;
    }
  }
  
  // If clustered, signal that this entity needs target adjustment
  // Don't force movement - let behaviors handle target selection
  if (closeNeighbors >= 5) {
    // Mark this entity as needing target diversification
    // This will be handled by individual behaviors through PathFollow system
    AI_DEBUG("Entity " + std::to_string(entity->getID()) + " is in cluster of " + std::to_string(closeNeighbors) + " - needs target adjustment");
    return 1; // Detected cluster that needs addressing
  }
  
  return 0;
}

// ======================= Async Pathfinding Processing =======================

void AIManager::processAsyncPathRequest(EntityID entityId, const Vector2D &start, const Vector2D &goal) {
  if (!m_pathGrid) return;
  
  // Faction-aware pathfinding (same logic as sync version but with EntityID)
  m_pathGrid->resetWeights(1.0f);
  
  // Since we only have EntityID, we need to find the entity for faction checking
  EntityPtr requestingEntity = nullptr;
  NPC::Faction requesterFaction = NPC::Faction::Neutral;
  
  {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    for (size_t i = 0; i < m_storage.size(); ++i) {
      if (m_storage.entities[i] && m_storage.entities[i]->getID() == entityId) {
        requestingEntity = m_storage.entities[i];
        if (auto npc = std::dynamic_pointer_cast<NPC>(requestingEntity)) {
          requesterFaction = npc->getFaction();
        }
        break;
      }
    }
  }

  // Add avoidance weights for hostile entities
  if (requestingEntity) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    for (size_t i = 0; i < m_storage.size(); ++i) {
      EntityPtr other = m_storage.entities[i];
      if (!other || other.get() == requestingEntity.get()) continue;
      
      Vector2D pos = other->getPosition();
      
      if (auto otherNPC = std::dynamic_pointer_cast<NPC>(other)) {
        NPC::Faction otherFaction = otherNPC->getFaction();
        if ((requesterFaction == NPC::Faction::Friendly && otherFaction == NPC::Faction::Enemy) ||
            (requesterFaction == NPC::Faction::Enemy && otherFaction == NPC::Faction::Friendly)) {
          m_pathGrid->addWeightCircle(pos, 96.0f, 2.5f); // Strong avoidance for hostiles
        } else {
          m_pathGrid->addWeightCircle(pos, 64.0f, 1.2f); // Mild avoidance for allies
        }
      } else {
        // Player or unknown entity - treat as friendly  
        if (requesterFaction == NPC::Faction::Enemy) {
          m_pathGrid->addWeightCircle(pos, 96.0f, 2.5f);
        } else {
          m_pathGrid->addWeightCircle(pos, 64.0f, 1.2f);
        }
      }
    }
  }

  // Perform pathfinding
  std::vector<Vector2D> path;
  auto result = m_pathGrid->findPath(start, goal, path);
  
  // Store result
  {
    std::lock_guard<std::mutex> lock(m_asyncPathMutex);
    if (result == HammerEngine::PathfindingResult::SUCCESS && !path.empty()) {
      m_asyncEntityPaths[entityId] = std::move(path);
      m_asyncPathTimestamps[entityId] = SDL_GetTicks();
    } else {
      // Store empty path to indicate failure
      m_asyncEntityPaths[entityId] = {};
      // Apply cooldown for failed requests
      Uint64 now = SDL_GetTicks();
      m_pathCooldownUntil[entityId] = now + 1000; // 1 second backoff
    }
    m_asyncPathsProcessed.fetch_add(1, std::memory_order_relaxed);
  }
}

AIManager::~AIManager() {
  if (!m_isShutdown) {
    clean();
  }
}
// Define deleter for forward-declared PathfindingGrid
void AIManager::PathGridDeleter::operator()(
    HammerEngine::PathfindingGrid *p) const {
  delete p;
}
