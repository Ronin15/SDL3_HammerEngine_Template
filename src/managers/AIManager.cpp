/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "utils/SIMDMath.hpp"
#include <algorithm>
#include <cstring>
#include <format>
#include <iostream>

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

    // Cache manager references for hot path usage (avoid singleton lookups)
    mp_pathfinderManager = &PathfinderManager::Instance();

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
    m_handleToIndex.reserve(INITIAL_CAPACITY);

    // Reserve capacity for assignment queues to prevent reallocations during
    // batch processing
    m_pendingAssignments.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);
    m_pendingAssignmentIndex.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);

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
    AI_ERROR(std::format("Failed to initialize AIManager: {}", e.what()));
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
      if (m_storage.behaviors[i] && m_storage.handles[i].isValid()) {
        try {
          m_storage.behaviors[i]->clean(m_storage.handles[i]);
        } catch (const std::exception &e) {
          AI_ERROR(std::format("Exception cleaning behavior during shutdown: {}", e.what()));
        } catch (...) {
          AI_ERROR("Unknown exception cleaning behavior during shutdown");
        }
      }
    }

    // Clear all storage
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.behaviors.clear();
    m_storage.lastUpdateTimes.clear();

    m_handleToIndex.clear();
    m_behaviorTemplates.clear();
    m_behaviorCache.clear();
    m_behaviorTypeCache.clear();
    m_pendingAssignments.clear();
    m_pendingAssignmentIndex.clear();
    m_messageQueue.clear();
  }

  // Clear cached manager references
  mp_pathfinderManager = nullptr;

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
      if (m_storage.behaviors[i] && m_storage.handles[i].isValid()) {
        try {
          m_storage.behaviors[i]->clean(m_storage.handles[i]);
          cleanedCount++;
        } catch (const std::exception &e) {
          AI_ERROR(std::format("Exception cleaning behavior: {}", e.what()));
        }
      }
    }

    AI_INFO_IF(cleanedCount > 0, std::format("Cleaned {} AI behaviors", cleanedCount));

    // Clear all storage completely
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.behaviors.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.edmIndices.clear();  // Clear EDM indices to prevent stale data
    m_handleToIndex.clear();

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

  // Clear player reference completely
  {
    std::lock_guard<std::shared_mutex> lock(m_entitiesMutex);
    m_playerHandle = EntityHandle{};
  }

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

  try {
    // Do not carry over AI update futures across frames to avoid races with
    // render. Any previous frame's update work must be completed within its
    // frame.

    // Process pending assignments first so new entities are picked up this frame
    processPendingBehaviorAssignments();

    // Use cached active count - no iteration needed
    size_t activeCount = m_activeEntityCount.load(std::memory_order_acquire);

    // Early exit if no active entities - skip ALL work including timing and cache
    // Messages for non-existent entities are useless, so skip processMessageQueue() too
    if (activeCount == 0) {
      return;
    }

    // Start timing AFTER we know we have work to do
    auto startTime = std::chrono::high_resolution_clock::now();
    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);

    // PERFORMANCE: Invalidate spatial query cache for new frame
    // This ensures thread-local caches are fresh and don't use stale collision data
    AIInternal::InvalidateSpatialCache(currentFrame);

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
    // All entities get distance calculated every frame via SIMD (fast enough)
    // Staggering removed - combat system needs accurate positions for all entities
    bool hasPlayer = m_playerHandle.isValid();
    Vector2D playerPos = hasPlayer ? getPlayerPosition() : Vector2D(0, 0);

    // SINGLE-COPY OPTIMIZATION: Pre-fetch directly from m_storage.hotData
    // No intermediate buffer copy needed - preFetchedData is our only copy
    // This eliminates redundant copying and reduces cache thrashing

    // Determine threading strategy based on threshold and WorkerBudget
    bool useThreading = (entityCount >= m_threadingThreshold.load(std::memory_order_acquire) &&
                         m_useThreading.load(std::memory_order_acquire) &&
                         HammerEngine::ThreadSystem::Exists());

    // Track threading decision for interval logging (local vars, no storage overhead)
    size_t logBatchCount = 1;
    bool logWasThreaded = false;

    if (useThreading) {
      auto &threadSystem = HammerEngine::ThreadSystem::Instance();

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
        AI_DEBUG(std::format("Queue pressure detected ({}/{}), using single-threaded processing",
                             queueSize, queueCapacity));

        // OPTIMIZATION: Direct storage iteration - no copying overhead
        // Hold shared_lock during processing for thread-safe read access
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          processBatch(0, entityCount, deltaTime, playerPos, hasPlayer, m_storage);
        }
      } else {
        // Use centralized WorkerBudgetManager for smart worker allocation
        auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

        // Get optimal workers (WorkerBudget determines everything dynamically)
        size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
            HammerEngine::SystemType::AI, entityCount);

        // Get adaptive batch strategy (maximizes parallelism, fine-tunes based on timing)
        auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
            HammerEngine::SystemType::AI, entityCount, optimalWorkerCount);

        // Track for interval logging at end of function
        logBatchCount = batchCount;
        logWasThreaded = (batchCount > 1);

        // Single batch optimization: avoid thread overhead
        if (batchCount <= 1) {
          // OPTIMIZATION: Direct storage iteration - no copying overhead
          // Hold shared_lock during processing for thread-safe read access
          {
            std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
            processBatch(0, entityCount, deltaTime, playerPos, hasPlayer, m_storage);
          }
        } else {
          size_t entitiesPerBatch = entityCount / batchCount;
          size_t remainingEntities = entityCount % batchCount;

          // OPTIMIZATION: Direct storage iteration with per-batch locking
          // Each batch acquires its own shared_lock - multiple batches can read in parallel

          // Submit batches using futures for parallel processing
          // Reuse m_batchFutures vector (clear keeps capacity, avoids allocations)
          m_batchFutures.clear();
          m_batchFutures.reserve(batchCount);

          for (size_t i = 0; i < batchCount; ++i) {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            // Add remaining entities to last batch
            if (i == batchCount - 1) {
              end += remainingEntities;
            }

            // Submit each batch with future for completion tracking
            // Each batch will acquire its own shared_lock during execution
            m_batchFutures.push_back(threadSystem.enqueueTaskWithResult(
              [this, start, end, deltaTime, playerPos, hasPlayer]() -> void {
                try {
                  // Acquire shared_lock for this batch's execution
                  // Multiple batches can hold shared_locks simultaneously (parallel reads)
                  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
                  processBatch(start, end, deltaTime, playerPos, hasPlayer, m_storage);
                } catch (const std::exception &e) {
                  AI_ERROR(std::format("Exception in AI batch: {}", e.what()));
                } catch (...) {
                  AI_ERROR("Unknown exception in AI batch");
                }
              },
              HammerEngine::TaskPriority::High,
              "AI_Batch"
            ));
          }

          // Batches execute in parallel via ThreadSystem
        }
      }

    } else {
      // Single-threaded processing (threading disabled in config)
      // OPTIMIZATION: Direct storage iteration - no copying overhead
      // Hold shared_lock during processing for thread-safe read access
      {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        processBatch(0, entityCount, deltaTime, playerPos, hasPlayer, m_storage);
      }
    }

    // Process lock-free message queue
    processMessageQueue();

    currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // Wait for async batches to complete (required for accurate timing)
    // Batches execute in parallel - we wait for all to finish before measuring
    for (auto& future : m_batchFutures) {
      if (future.valid()) {
        future.get();
      }
    }

    // Measure completion time for adaptive tuning (after all batches complete)
    auto endTime = std::chrono::high_resolution_clock::now();
    double totalUpdateTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Report batch completion for adaptive tuning (only if threading was used)
    if (logWasThreaded) {
      HammerEngine::WorkerBudgetManager::Instance().reportBatchCompletion(
          HammerEngine::SystemType::AI, entityCount, logBatchCount, totalUpdateTime);
    }

    // Periodic cleanup tracking (balanced frequency)
    if (currentFrame % 300 == 0) {
      // cleanupInactiveEntities() moved to GameEngine background processing
      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);
    }

#ifndef NDEBUG
    // Interval stats logging - zero overhead in release (entire block compiles out)
    static thread_local uint64_t logFrameCounter = 0;
    if (++logFrameCounter % 300 == 0 && entityCount > 0) {
      double entitiesPerSecond = totalUpdateTime > 0
          ? (entityCount * 1000.0 / totalUpdateTime)
          : 0.0;
      if (logWasThreaded) {
        AI_DEBUG(std::format("AI Summary - Entities: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
                             "[Threaded: {} batches, {}/batch]",
                             entityCount, totalUpdateTime, entitiesPerSecond,
                             logBatchCount, entityCount / logBatchCount));
      } else {
        AI_DEBUG(std::format("AI Summary - Entities: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
                             "[Single-threaded]",
                             entityCount, totalUpdateTime, entitiesPerSecond));
      }
    }
#endif

  } catch (const std::exception &e) {
    AI_ERROR(std::format("Exception in AIManager::update: {}", e.what()));
  }
}

void AIManager::waitForAsyncBatchCompletion() {
  // Wait for all batch futures to complete
  // Used during state transitions and cleanup to ensure no pending work
  for (auto& future : m_batchFutures) {
    if (future.valid()) {
      future.wait();
    }
  }

  // NOTE: No CollisionManager update needed - behaviors write directly to
  // EntityDataManager transforms (lock-free). CollisionManager reads from EDM.
}

void AIManager::waitForAssignmentCompletion() {
  // ASSIGNMENT SYNCHRONIZATION: Wait for all async assignment batches to complete
  // This ensures no dangling references to entity data during state transitions.
  //
  // DETERMINISTIC COMPLETION: Replaces empirical 100ms sleep with future.wait()
  //   - Old approach: sleep_for(100ms) - works empirically but not scalable
  //   - New approach: Block on futures until all assignments complete
  //   - Result: Deterministic completion, scalable to any entity count

  // Reuse member buffer instead of creating local vector (eliminates ~60 alloc/sec)
  m_reusableAssignmentFutures.clear();

  {
    std::lock_guard<std::mutex> lock(m_assignmentFuturesMutex);
    m_reusableAssignmentFutures = std::move(m_assignmentFutures);
  }

  // Wait for all assignment futures to complete
  for (auto& future : m_reusableAssignmentFutures) {
    if (future.valid()) {
      future.wait();  // Block until assignment batch completes
    }
  }
}

void AIManager::registerBehavior(const std::string &name,
                                 std::shared_ptr<AIBehavior> behavior) {
  if (!behavior) {
    AI_ERROR(std::format("Attempted to register null behavior with name: {}", name));
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

  // Pre-populate cache to ensure it's read-only during async operations
  // This eliminates the race condition in getBehavior() cache miss path
  m_behaviorCache[name] = behavior;

  AI_INFO(std::format("Registered behavior: {}", name));
}

bool AIManager::hasBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior>
AIManager::getBehavior(const std::string &name) const {
  // THREADING: Cache is pre-populated by registerBehavior(), making this read-only
  // and safe for concurrent access from worker threads during async assignment.
  // Cache miss means the behavior was never registered.
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);

  auto cacheIt = m_behaviorCache.find(name);
  if (cacheIt != m_behaviorCache.end()) {
    return cacheIt->second;
  }

  // Cache miss - check template map as fallback (shouldn't happen after
  // registerBehavior() now pre-populates cache, but handle gracefully)
  auto it = m_behaviorTemplates.find(name);
  return (it != m_behaviorTemplates.end()) ? it->second : nullptr;
}

void AIManager::assignBehavior(EntityHandle handle,
                               const std::string &behaviorName) {
  if (!handle.isValid()) {
    AI_ERROR("Cannot assign behavior to invalid handle");
    return;
  }

  // Get behavior template (use cache for performance)
  std::shared_ptr<AIBehavior> behaviorTemplate = getBehavior(behaviorName);
  if (!behaviorTemplate) {
    AI_ERROR(std::format("Behavior not found: {}", behaviorName));
    return;
  }

  // Clone behavior for this entity
  auto behavior = behaviorTemplate->clone();
  behavior->init(handle);

  auto& edm = EntityDataManager::Instance();

  // Find or create entity entry
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_handleToIndex.find(handle);
  if (indexIt != m_handleToIndex.end()) {
    // Update existing entity
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      // Clean up old behavior
      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(handle);
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

      // Refresh EDM index if needed
      if (index < m_storage.edmIndices.size()) {
        m_storage.edmIndices[index] = edm.getIndex(handle);
      }

      AI_INFO(std::format("Updated behavior for existing entity to: {}", behaviorName));
    }
  } else {
    // Add new entity
    size_t newIndex = m_storage.size();

    // Add to hot data
    AIEntityData::HotData hotData{};
    hotData.priority = DEFAULT_PRIORITY;
    hotData.behaviorType = static_cast<uint8_t>(inferBehaviorType(behaviorName));
    hotData.active = true;

    m_storage.hotData.push_back(hotData);
    m_storage.handles.push_back(handle);
    m_storage.behaviors.push_back(behavior);

    // Increment active counter for new entity
    m_activeEntityCount.fetch_add(1, std::memory_order_relaxed);
    m_storage.lastUpdateTimes.push_back(0.0f);

    // Cache EntityDataManager index for lock-free batch access
    size_t edmIndex = edm.getIndex(handle);
    m_storage.edmIndices.push_back(edmIndex);

    // Update index map
    m_handleToIndex[handle] = newIndex;

    AI_INFO(std::format("Added new entity with behavior: {}", behaviorName));
  }

  m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
}

void AIManager::unassignBehavior(EntityHandle handle) {
  if (!handle.isValid())
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.size()) {
      // Decrement active counter if entity was active
      if (m_storage.hotData[index].active) {
        m_storage.hotData[index].active = false;
        m_activeEntityCount.fetch_sub(1, std::memory_order_relaxed);
      }

      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(handle);
      }
    }
  }
}

bool AIManager::hasBehavior(EntityHandle handle) const {
  if (!handle.isValid())
    return false;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].active &&
           m_storage.behaviors[it->second] != nullptr;
  }

  return false;
}

void AIManager::queueBehaviorAssignment(EntityHandle handle,
                                        const std::string &behaviorName) {
  if (!handle.isValid() || behaviorName.empty()) {
    AI_ERROR("Invalid behavior assignment request");
    return;
  }

  // Direct assignment - the queue system was removed as it caused hangs
  // when AIManager::update() wasn't running (during state transitions)
  assignBehavior(handle, behaviorName);
}

size_t AIManager::processPendingBehaviorAssignments() {
  // FUTURES-BASED: Deterministic completion tracking with m_assignmentFutures
  // Assignments use std::future<void> for safe state transition synchronization

  // OPTIMIZATION: Reuse buffer to avoid per-frame allocation during spawning
  m_reusableToProcessBuffer.clear();
  {
    std::lock_guard<std::mutex> lock(m_assignmentsMutex);
    if (m_pendingAssignments.empty()) {
      return 0;
    }
    // Swap to reusable buffer - moves content, preserves capacity
    std::swap(m_reusableToProcessBuffer, m_pendingAssignments);
    m_pendingAssignmentIndex.clear();
  }
  auto& toProcess = m_reusableToProcessBuffer;

  size_t assignmentCount = toProcess.size();
  if (assignmentCount == 0) {
    return 0;
  }

  // Check if threading is available
  bool useThreading = m_useThreading.load(std::memory_order_acquire) &&
                      HammerEngine::ThreadSystem::Exists();

  if (!useThreading) {
    // Fall back to synchronous processing
    for (const auto &assignment : toProcess) {
      if (assignment.handle.isValid()) {
        assignBehavior(assignment.handle, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Async processing for large batches
  auto &threadSystem = HammerEngine::ThreadSystem::Instance();

  // Calculate optimal batching using centralized WorkerBudgetManager
  auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

  // Get optimal workers (WorkerBudget determines everything dynamically)
  size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
      HammerEngine::SystemType::AI, assignmentCount);

  // Get adaptive batch strategy
  auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
      HammerEngine::SystemType::AI, assignmentCount, optimalWorkerCount);

  // If WorkerBudget recommends single-threaded execution, process synchronously
  if (batchCount <= 1) {
    AI_DEBUG(std::format("WorkerBudget recommends single-threaded processing for {} assignments",
             assignmentCount));
    for (const auto &assignment : toProcess) {
      if (assignment.handle.isValid()) {
        assignBehavior(assignment.handle, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Submit batches using futures for deterministic completion tracking
  {
    std::lock_guard<std::mutex> lock(m_assignmentFuturesMutex);
    m_assignmentFutures.clear();  // Clear old futures, keeps capacity

    // Calculate actual needed batches (avoid submitting empty tasks)
    size_t actualBatchesNeeded = (assignmentCount + batchSize - 1) / batchSize;
    size_t tasksToSubmit = std::min(batchCount, actualBatchesNeeded);

    m_assignmentFutures.reserve(tasksToSubmit);

    // OPTIMIZATION: Reuse shared_ptr to avoid allocation per batch
    if (!m_reusableAssignmentBatch) {
      m_reusableAssignmentBatch = std::make_shared<std::vector<PendingAssignment>>();
    }
    m_reusableAssignmentBatch->clear();
    std::swap(*m_reusableAssignmentBatch, toProcess);
    auto toProcessShared = m_reusableAssignmentBatch;

    // Submit tasks with index ranges (deterministic task count, no buffer pool needed)
    for (size_t i = 0; i < tasksToSubmit; ++i) {
      size_t start = i * batchSize;
      size_t end = std::min(start + batchSize, assignmentCount);

      m_assignmentFutures.push_back(threadSystem.enqueueTaskWithResult(
          [this, toProcessShared, start, end]() -> void {
            // Check if AIManager is shutting down
            if (m_isShutdown) {
              return; // Don't process assignments during shutdown
            }

            // Process index range directly (no intermediate buffer)
            for (size_t idx = start; idx < end; ++idx) {
              const auto& assignment = (*toProcessShared)[idx];
              if (assignment.handle.isValid()) {
                try {
                  assignBehavior(assignment.handle, assignment.behaviorName);
                } catch (const std::exception &e) {
                  AI_ERROR(std::format("Exception during async behavior assignment: {}", e.what()));
                } catch (...) {
                  AI_ERROR("Unknown exception during async behavior assignment");
                }
              }
            }
          },
          HammerEngine::TaskPriority::High, "AI_AssignmentBatch"));
    }
  }

  // Async assignment processing statistics are now tracked in periodic summary
  return assignmentCount; // Return immediately, don't wait
}

void AIManager::setPlayerHandle(EntityHandle player) {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);
  m_playerHandle = player;
}

EntityHandle AIManager::getPlayerHandle() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return m_playerHandle;
}

Vector2D AIManager::getPlayerPosition() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  if (m_playerHandle.isValid()) {
    auto& edm = EntityDataManager::Instance();
    size_t edmIndex = edm.getIndex(m_playerHandle);
    if (edmIndex != SIZE_MAX) {
      return edm.getTransformByIndex(edmIndex).position;
    }
  }
  return Vector2D{0.0f, 0.0f};
}

bool AIManager::isPlayerValid() const {
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  return m_playerHandle.isValid();
}

void AIManager::registerEntity(EntityHandle handle) {
  if (!handle.isValid()) {
    AI_ERROR("Cannot register invalid handle for updates");
    return;
  }

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Check if entity already exists
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end()) {
    // Entity already registered, nothing to do
    return;
  }

  // Add managed entity info
  EntityUpdateInfo info(handle, DEFAULT_PRIORITY);
  info.lastUpdateTime = getCurrentTimeNanos();
  m_managedEntities.push_back(info);
}

void AIManager::unregisterEntity(EntityHandle handle) {
  if (!handle.isValid())
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Remove from managed entities
  m_managedEntities.erase(
      std::remove_if(m_managedEntities.begin(), m_managedEntities.end(),
                     [&handle](const EntityUpdateInfo &info) {
                       return !info.handle.isValid() || info.handle == handle;
                     }),
      m_managedEntities.end());

  // Mark as inactive in main storage
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    // Decrement active counter if entity was active
    if (m_storage.hotData[it->second].active) {
      m_storage.hotData[it->second].active = false;
      m_activeEntityCount.fetch_sub(1, std::memory_order_relaxed);
    }
  }
}

void AIManager::queryHandlesInRadius(const Vector2D& center, float radius,
                                     std::vector<EntityHandle>& outHandles,
                                     bool excludePlayer) const {
  outHandles.clear();

  const float radiusSq = radius * radius;
  auto& edm = EntityDataManager::Instance();

  // Thread-safe read access to entity storage
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Iterate through all active entities
  for (size_t i = 0; i < m_storage.size(); ++i) {
    // Skip inactive entities
    if (!m_storage.hotData[i].active) {
      continue;
    }

    EntityHandle handle = m_storage.handles[i];
    if (!handle.isValid()) {
      continue;
    }

    // Skip player if requested
    if (excludePlayer && handle == m_playerHandle) {
      continue;
    }

    // Get position from EDM
    size_t edmIndex = m_storage.edmIndices[i];
    if (edmIndex == SIZE_MAX) {
      continue;
    }

    const Vector2D& pos = edm.getTransformByIndex(edmIndex).position;
    const float dx = pos.getX() - center.getX();
    const float dy = pos.getY() - center.getY();
    const float distSq = dx * dx + dy * dy;

    if (distSq <= radiusSq) {
      outHandles.push_back(handle);
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
    if (m_storage.behaviors[i] && m_storage.handles[i].isValid()) {
      m_storage.behaviors[i]->clean(m_storage.handles[i]);
    }
  }

  // Clear all data
  m_storage.hotData.clear();
  m_storage.handles.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_storage.edmIndices.clear();  // BUGFIX: Must clear edmIndices to prevent stale index pollution
  m_handleToIndex.clear();
  m_managedEntities.clear();

  // Reset counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_activeEntityCount.store(0, std::memory_order_relaxed);
}

void AIManager::enableThreading(bool enable) {
  m_useThreading.store(enable, std::memory_order_release);
  AI_INFO(std::format("Threading {}", enable ? "enabled" : "disabled"));
}

void AIManager::setThreadingThreshold(size_t threshold) {
  threshold = std::max(static_cast<size_t>(1), threshold);
  m_threadingThreshold.store(threshold, std::memory_order_release);
  AI_INFO(std::format("AI threading threshold set to {} entities", threshold));
}

size_t AIManager::getThreadingThreshold() const {
  return m_threadingThreshold.load(std::memory_order_acquire);
}

void AIManager::setWaitForBatchCompletion(bool wait) {
  m_waitForBatchCompletion.store(wait, std::memory_order_release);
  AI_INFO(std::format("AI batch completion wait {} (smooth frames: {})",
                      wait ? "enabled" : "disabled", wait ? "no" : "yes"));
}

bool AIManager::getWaitForBatchCompletion() const {
  return m_waitForBatchCompletion.load(std::memory_order_acquire);
}


size_t AIManager::getBehaviorCount() const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.size();
}

size_t AIManager::getManagedEntityCount() const {
  // PERFORMANCE: Use cached counter instead of O(n) iteration
  return m_activeEntityCount.load(std::memory_order_relaxed);
}

size_t AIManager::getBehaviorUpdateCount() const {
  return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
  return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

void AIManager::sendMessageToEntity(EntityHandle handle,
                                    const std::string &message,
                                    bool immediate) {
  if (!handle.isValid() || message.empty())
    return;

  if (immediate) {
    std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
    auto it = m_handleToIndex.find(handle);
    if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
      if (m_storage.behaviors[it->second]) {
        m_storage.behaviors[it->second]->onMessage(handle, message);
      }
    }
  } else {
    // Use lock-free queue for non-immediate messages
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target = handle;
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
        m_storage.behaviors[i]->onMessage(m_storage.handles[i], message);
      }
    }
  } else {
    // Queue broadcast for processing in next update
    size_t writeIndex =
        m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
        MESSAGE_QUEUE_SIZE;
    auto &msg = m_lockFreeMessages[writeIndex];

    msg.target = EntityHandle{}; // Invalid handle for broadcast
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
      if (msg.target.isValid()) {
        sendMessageToEntity(msg.target, msg.message, true);
      }

      msg.ready.store(false, std::memory_order_release);
    }

    readIndex++;
  }

  m_messageReadIndex.store(readIndex, std::memory_order_release);
}

BehaviorType
AIManager::inferBehaviorType(const std::string &behaviorName) const {
  // Check cache first with shared lock (multiple readers allowed)
  {
    std::shared_lock lock(m_behaviorCacheMutex);
    auto cacheIt = m_behaviorTypeCache.find(behaviorName);
    if (cacheIt != m_behaviorTypeCache.end()) {
      return cacheIt->second;
    }
  }

  // Cache miss - lookup result (m_behaviorTypeMap is read-only after init)
  auto it = m_behaviorTypeMap.find(behaviorName);
  BehaviorType result =
      (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;

  // Cache the result with exclusive lock
  {
    std::unique_lock lock(m_behaviorCacheMutex);
    m_behaviorTypeCache[behaviorName] = result;
  }
  return result;
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime,
                             const Vector2D& /*playerPos*/,
                             bool /*hasPlayer*/,
                             const EntityStorage& storage) {
  size_t batchExecutions = 0;

  // OPTIMIZATION: Get world bounds ONCE per batch (not per entity)
  const auto &pf = *mp_pathfinderManager;
  float worldWidth, worldHeight;
  if (!pf.getCachedWorldBounds(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
    worldWidth = 32000.0f;
    worldHeight = 32000.0f;
  }

  auto& edm = EntityDataManager::Instance();

  for (size_t i = start; i < end && i < storage.handles.size(); ++i) {
    EntityHandle handle = storage.handles[i];
    AIBehavior* behavior = storage.behaviors[i].get();
    size_t edmIndex = storage.edmIndices[i];

    if (!handle.isValid() || edmIndex == SIZE_MAX || !behavior) {
      continue;
    }

    auto& edmHotData = edm.getHotDataByIndex(edmIndex);
    auto& transform = edm.getTransformByIndex(edmIndex);

    // Tier-based culling - EDM handles distance calculation via updateSimulationTiers()
    if (edmHotData.tier != SimulationTier::Active) {
      continue;
    }

    try {
      // Store previous position for interpolation
      transform.previousPosition = transform.position;

      // Execute behavior logic
      BehaviorContext ctx(transform, edmHotData, handle.getId(), deltaTime);
      behavior->executeLogic(ctx);

      // Movement integration
      Vector2D pos = transform.position + (transform.velocity * deltaTime);

      // World bounds clamping
      float halfW = edmHotData.halfWidth;
      float halfH = edmHotData.halfHeight;
      Vector2D clamped(
          std::clamp(pos.getX(), halfW, worldWidth - halfW),
          std::clamp(pos.getY(), halfH, worldHeight - halfH)
      );
      transform.position = clamped;

      // Stop velocity at world edges
      if (clamped.getX() != pos.getX()) {
        transform.velocity.setX(0.0f);
      }
      if (clamped.getY() != pos.getY()) {
        transform.velocity.setY(0.0f);
      }

      ++batchExecutions;
    } catch (const std::exception &e) {
      AI_ERROR(std::format("Error in batch processing entity: {}", e.what()));
    }
  }

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions, std::memory_order_relaxed);
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

    // Remove from handle map
    if (index < m_storage.handles.size()) {
      m_handleToIndex.erase(m_storage.handles[index]);
    }

    // Swap with last element and pop
    if (index < m_storage.size() - 1) {
      size_t lastIndex = m_storage.size() - 1;

      // Update hot data
      m_storage.hotData[index] = m_storage.hotData[lastIndex];

      // Update cold data
      m_storage.handles[index] = m_storage.handles[lastIndex];
      m_storage.behaviors[index] = m_storage.behaviors[lastIndex];
      m_storage.lastUpdateTimes[index] = m_storage.lastUpdateTimes[lastIndex];
      m_storage.edmIndices[index] = m_storage.edmIndices[lastIndex];

      // Update index map
      m_handleToIndex[m_storage.handles[index]] = index;
    }

    // Remove last element
    m_storage.hotData.pop_back();
    m_storage.handles.pop_back();
    m_storage.behaviors.pop_back();
    m_storage.lastUpdateTimes.pop_back();
    m_storage.edmIndices.pop_back();
  }

  AI_DEBUG(std::format("Cleaned up {} inactive entities", toRemove.size()));
}

void AIManager::cleanupAllEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Clean all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.handles[i].isValid()) {
      try {
        m_storage.behaviors[i]->clean(m_storage.handles[i]);
      } catch (const std::exception &e) {
        AI_ERROR(std::format("Exception cleaning behavior: {}", e.what()));
      }
    }
  }

  // Clear all storage
  m_storage.hotData.clear();
  m_storage.handles.clear();
  m_storage.behaviors.clear();
  m_storage.lastUpdateTimes.clear();
  m_storage.edmIndices.clear();
  m_handleToIndex.clear();

  // Reset active counter
  m_activeEntityCount.store(0, std::memory_order_relaxed);

  AI_DEBUG("Cleaned up all entities for state transition");
}

uint64_t AIManager::getCurrentTimeNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

int AIManager::getEntityPriority(EntityHandle handle) const {
  if (!handle.isValid())
    return DEFAULT_PRIORITY;

  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    return m_storage.hotData[it->second].priority;
  }
  return DEFAULT_PRIORITY;
}

float AIManager::getUpdateRangeMultiplier(int priority) const {
  // Higher priority = larger update range multiplier
  return 1.0f + (std::max(0, std::min(9, priority)) * 0.1f);
}

void AIManager::registerEntity(EntityHandle handle,
                               const std::string &behaviorName) {
  // Assign behavior directly - no queue delay
  // The queue system was causing hangs when update() wasn't running yet
  assignBehavior(handle, behaviorName);
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
