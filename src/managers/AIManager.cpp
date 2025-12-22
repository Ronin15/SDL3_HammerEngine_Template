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
    m_entityToIndex.reserve(INITIAL_CAPACITY);

    // Reserve capacity for assignment queues to prevent reallocations during
    // batch processing
    m_pendingAssignments.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);
    m_pendingAssignmentIndex.reserve(AIConfig::ASSIGNMENT_QUEUE_RESERVE);

    // Pre-allocate collision update buffer for single-threaded paths
    // Reserve with 10% headroom for growth (typical: 4000 NPCs → 4400 capacity)
    m_reusableCollisionBuffer.reserve(static_cast<size_t>(INITIAL_CAPACITY * 1.1));

    // Pre-allocate distance/position buffers (Issue #2 fix)
    // WorkerBudget.hpp: AI_BATCH_CONFIG allows up to 16 batches (maxBatchCount * 2)
    // Batch size = (workloadSize + batchCount - 1) / batchCount
    // Worst case: 10K entities / 4 batches (high queue pressure) = 2,500 entities/batch
    // Conservative sizing: 16 batches * 3,000 entities/batch = handles up to 48K entities
    constexpr size_t MAX_UPDATE_BATCHES = 16;     // AI_BATCH_CONFIG.maxBatchCount * 2
    constexpr size_t MAX_BATCH_SIZE = 3000;       // Worst case: 10K entities / 4 batches with headroom
    m_distanceBuffers.resize(MAX_UPDATE_BATCHES);
    m_positionBuffers.resize(MAX_UPDATE_BATCHES);
    for (size_t i = 0; i < MAX_UPDATE_BATCHES; ++i) {
      m_distanceBuffers[i].reserve(MAX_BATCH_SIZE);
      m_positionBuffers[i].reserve(MAX_BATCH_SIZE);
    }

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
      if (m_storage.behaviors[i] && m_storage.entities[i]) {
        try {
          m_storage.behaviors[i]->clean(m_storage.entities[i]);
        } catch (const std::exception &e) {
          AI_ERROR(std::format("Exception cleaning behavior during shutdown: {}", e.what()));
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
      if (m_storage.behaviors[i] && m_storage.entities[i]) {
        try {
          m_storage.behaviors[i]->clean(m_storage.entities[i]);
          cleanedCount++;
        } catch (const std::exception &e) {
          AI_ERROR(std::format("Exception cleaning behavior: {}", e.what()));
        }
      }
    }

    if (cleanedCount > 0) {
      AI_INFO(std::format("Cleaned {} AI behaviors", cleanedCount));
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

  // Clear player reference completely
  {
    std::lock_guard<std::shared_mutex> lock(m_entitiesMutex);
    m_playerEntity.reset();
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

    // Use atomic active count - no iteration needed
    size_t activeCount = m_activeEntityCount.load(std::memory_order_relaxed);

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
    EntityPtr player = m_playerEntity.lock();
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

    // Track threading decision for interval logging (local vars, no storage overhead)
    size_t logWorkerCount = 0;
    size_t logAvailableWorkers = 0;
    size_t logBudget = 0;
    size_t logBatchCount = 1;
    bool logWasThreaded = false;

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
        AI_DEBUG(std::format("Queue pressure detected ({}/{}), using single-threaded processing",
                             queueSize, queueCapacity));

        // OPTIMIZATION: Direct storage iteration - no copying overhead
        // Hold shared_lock during processing for thread-safe read access
        m_reusableCollisionBuffer.clear();
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          processBatch(0, entityCount, deltaTime, playerPos, m_storage, m_reusableCollisionBuffer);
        }

        // Submit collision updates directly (single-threaded, no batching needed)
        if (!m_reusableCollisionBuffer.empty()) {
          auto &cm = CollisionManager::Instance();
          cm.applyKinematicUpdates(m_reusableCollisionBuffer);
        }
      }

      // Use centralized WorkerBudgetManager for smart worker allocation
      auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
      const auto& budget = budgetMgr.getBudget();

      // Get optimal workers (considers queue pressure internally)
      size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
          HammerEngine::SystemType::AI, entityCount, threadingThreshold);

      // Get adaptive batch strategy (maximizes parallelism, fine-tunes based on timing)
      auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
          HammerEngine::SystemType::AI, entityCount, optimalWorkerCount);

      // Track for interval logging at end of function
      logWorkerCount = optimalWorkerCount;
      logAvailableWorkers = availableWorkers;
      logBudget = budget.aiAllocated;
      logBatchCount = batchCount;
      logWasThreaded = (batchCount > 1);

      // Single batch optimization: avoid thread overhead
      if (batchCount <= 1) {
        // OPTIMIZATION: Direct storage iteration - no copying overhead
        // Hold shared_lock during processing for thread-safe read access
        m_reusableCollisionBuffer.clear();
        {
          std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
          processBatch(0, entityCount, deltaTime, playerPos, m_storage, m_reusableCollisionBuffer);
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
               batchCollisionUpdatesPtr, i]() -> void {
                try {
                  // Acquire shared_lock for this batch's execution
                  // Multiple batches can hold shared_locks simultaneously (parallel reads)
                  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
                  processBatch(start, end, deltaTime, playerPos,
                              m_storage, (*batchCollisionUpdatesPtr)[i], i);
                } catch (const std::exception &e) {
                  AI_ERROR(std::format("Exception in AI batch: {}", e.what()));
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

    } else {
      // Single-threaded processing (threading disabled in config)
      // OPTIMIZATION: Direct storage iteration - no copying overhead
      // Hold shared_lock during processing for thread-safe read access
      m_reusableCollisionBuffer.clear();
      {
        std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
        processBatch(0, entityCount, deltaTime, playerPos, m_storage, m_reusableCollisionBuffer);
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

    // Report batch completion for adaptive tuning (only if threading was used)
    if (logWasThreaded) {
      HammerEngine::WorkerBudgetManager::Instance().reportBatchCompletion(
          HammerEngine::SystemType::AI, logBatchCount, totalUpdateTime);
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
                             "[Threaded: {}/{} workers, Budget: {}, Batches: {}]",
                             entityCount, totalUpdateTime, entitiesPerSecond,
                             logWorkerCount, logAvailableWorkers, logBudget, logBatchCount));
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
  // BATCH SYNCHRONIZATION: Wait for all async batches to complete
  // This ensures collision updates are ready before CollisionManager processes them.
  //
  // JITTER ELIMINATION: Per-batch buffers eliminate mutex contention
  //   - Old approach: Each batch serializes on submitPendingKinematicUpdates mutex
  //   - New approach: Each batch writes to its own buffer (zero contention!)
  //   - Result: Consistent batch completion times → smooth frames

  // Reuse member buffer instead of creating local vector (eliminates ~120 alloc/sec)
  // Use swap() to preserve capacity on both vectors (avoids reallocation)
  std::shared_ptr<std::vector<std::vector<CollisionManager::KinematicUpdate>>> collisionBuffers;

  {
    std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
    m_reusableBatchFutures.clear();  // Clear old content, keep capacity
    std::swap(m_reusableBatchFutures, m_batchFutures);  // Swap preserves both capacities
    collisionBuffers = m_batchCollisionUpdates;
    m_batchCollisionUpdates.reset();  // Clear for next frame
  }

  // Wait for all batch futures to complete
  for (auto& future : m_reusableBatchFutures) {
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

  AI_INFO(std::format("Registered behavior: {}", name));
}

bool AIManager::hasBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior>
AIManager::getBehavior(const std::string &name) const {
  // Fast path: Check cache first for O(1) lookup (unprotected read is safe after initial population)
  auto cacheIt = m_behaviorCache.find(name);
  if (cacheIt != m_behaviorCache.end()) {
    return cacheIt->second;
  }

  // Cache miss - use EXCLUSIVE lock for template read + cache write
  // THREADING FIX: Changed from shared_lock to unique_lock to prevent double-free
  // when multiple threads write to m_behaviorCache simultaneously
  std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);

  // Double-checked locking: Re-check cache after acquiring exclusive lock
  // Another thread may have populated it while we were waiting for the lock
  cacheIt = m_behaviorCache.find(name);
  if (cacheIt != m_behaviorCache.end()) {
    return cacheIt->second;
  }

  // Lookup template and cache result (now thread-safe with exclusive lock)
  auto it = m_behaviorTemplates.find(name);
  std::shared_ptr<AIBehavior> result =
      (it != m_behaviorTemplates.end()) ? it->second : nullptr;

  // Cache the result (even if nullptr) - safe with exclusive lock
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
    AI_ERROR(std::format("Behavior not found: {}", behaviorName));
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

      AI_INFO(std::format("Updated behavior for existing entity to: {}", behaviorName));
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

    AI_INFO(std::format("Added new entity with behavior: {}", behaviorName));
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
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
      }
    }
    return assignmentCount;
  }

  // Async processing for large batches
  auto &threadSystem = HammerEngine::ThreadSystem::Instance();

  // Calculate optimal batching using centralized WorkerBudgetManager
  auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

  size_t assignmentThreadingThreshold =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));

  // Get optimal workers (considers queue pressure internally)
  size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
      HammerEngine::SystemType::AI, assignmentCount, assignmentThreadingThreshold);

  // Get adaptive batch strategy
  auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
      HammerEngine::SystemType::AI, assignmentCount, optimalWorkerCount);

  // If WorkerBudget recommends single-threaded execution, process synchronously
  if (batchCount <= 1) {
    AI_DEBUG(std::format("WorkerBudget recommends single-threaded processing for {} assignments",
             assignmentCount));
    for (const auto &assignment : toProcess) {
      if (assignment.entity) {
        assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
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
              if (assignment.entity) {
                try {
                  assignBehaviorToEntity(assignment.entity, assignment.behaviorName);
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

void AIManager::queryEntitiesInRadius(const Vector2D& center, float radius,
                                      std::vector<EntityPtr>& outEntities,
                                      bool excludePlayer) const {
  outEntities.clear();

  // Get player reference for exclusion check
  EntityPtr playerRef = excludePlayer ? m_playerEntity.lock() : nullptr;
  const float radiusSq = radius * radius;

  // Thread-safe read access to entity storage
  std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Iterate through all active entities
  for (size_t i = 0; i < m_storage.size(); ++i) {
    // Skip inactive entities
    if (!m_storage.hotData[i].active) {
      continue;
    }

    EntityPtr entity = m_storage.entities[i];
    if (!entity) {
      continue;
    }

    // Skip player if requested
    if (excludePlayer && entity == playerRef) {
      continue;
    }

    // Check distance (squared to avoid sqrt)
    const Vector2D& pos = m_storage.hotData[i].position;
    const float dx = pos.getX() - center.getX();
    const float dy = pos.getY() - center.getY();
    const float distSq = dx * dx + dy * dy;

    if (distSq <= radiusSq) {
      outEntities.push_back(entity);
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
  m_activeEntityCount.store(0, std::memory_order_relaxed);  // BUGFIX: Reset active count when clearing storage
}

void AIManager::configureThreading(bool useThreading, unsigned int maxThreads) {
  m_useThreading.store(useThreading, std::memory_order_release);

  if (maxThreads > 0) {
    m_maxThreads = maxThreads;
  }

  AI_INFO(std::format("Threading configured: {} with max threads: {}",
         useThreading ? "enabled" : "disabled", m_maxThreads));
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

void AIManager::configurePriorityMultiplier(float multiplier) {
  m_priorityMultiplier.store(multiplier, std::memory_order_release);
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

/**
 * @brief SIMD-optimized batch distance calculation
 * Processes 4 entities at once using platform-specific SIMD instructions
 * All entities are processed every frame (no staggering) for accurate combat
 *
 * @param start Starting index in storage
 * @param end Ending index in storage
 * @param playerPos Player position for distance calculations
 * @param storage Entity storage (read-only)
 * @param outDistances Output array for calculated squared distances (must be sized >= end - start)
 * @param outPositions Output array for entity positions (must be sized >= end - start)
 */
void AIManager::calculateDistancesSIMD(
    size_t start, size_t end,
    const Vector2D& playerPos,
    const EntityStorage& storage,
    std::vector<float>& outDistances,
    std::vector<Vector2D>& outPositions
) {
    const bool hasPlayer = (playerPos.getX() != 0 || playerPos.getY() != 0);
    if (!hasPlayer) {
        return; // No distance updates needed without player
    }

    const Float4 playerPosX = broadcast(playerPos.getX());
    const Float4 playerPosY = broadcast(playerPos.getY());

#if defined(HAMMER_SIMD_SSE2) || defined(HAMMER_SIMD_NEON)
    // SIMD path: Process 4 entities at once
    // All entities processed every frame for accurate combat system
    for (size_t i = start; i + 3 < end && i + 3 < storage.entities.size(); i += 4) {
        // Get entity positions
        Vector2D pos0 = storage.entities[i]->getPosition();
        Vector2D pos1 = storage.entities[i + 1]->getPosition();
        Vector2D pos2 = storage.entities[i + 2]->getPosition();
        Vector2D pos3 = storage.entities[i + 3]->getPosition();

        // Load positions into SIMD registers
        const Float4 entityPosX = set(pos0.getX(), pos1.getX(), pos2.getX(), pos3.getX());
        const Float4 entityPosY = set(pos0.getY(), pos1.getY(), pos2.getY(), pos3.getY());

        // Calculate differences
        const Float4 diffX = sub(entityPosX, playerPosX);
        const Float4 diffY = sub(entityPosY, playerPosY);

        // Calculate squared distances: diffX * diffX + diffY * diffY
        const Float4 distSq = add(mul(diffX, diffX), mul(diffY, diffY));

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

    // Scalar tail loop for remaining entities (not divisible by 4)
    // Calculate where SIMD actually stopped based on actual storage size
    // SIMD loop condition is: i + 3 < end && i + 3 < storage.entities.size()
    // So SIMD processed: start, start+4, start+8, ... up to where i+3 >= min(end, storage.size())
    size_t effectiveEnd = std::min(end, storage.entities.size());
    size_t simdProcessedCount = (effectiveEnd > start) ? ((effectiveEnd - start) / 4) * 4 : 0;
    for (size_t i = start + simdProcessedCount; i < end && i < storage.entities.size(); ++i) {
        const Vector2D entityPos = storage.entities[i]->getPosition();
        const Vector2D diff = entityPos - playerPos;
        size_t idx = i - start;
        outDistances[idx] = diff.lengthSquared();
        outPositions[idx] = entityPos;
    }
#else
    // Scalar fallback for non-SIMD platforms
    for (size_t i = start; i < end && i < storage.entities.size(); ++i) {
        const Vector2D entityPos = storage.entities[i]->getPosition();
        const Vector2D diff = entityPos - playerPos;
        size_t idx = i - start;
        outDistances[idx] = diff.lengthSquared();
        outPositions[idx] = entityPos;
    }
#endif
}

void AIManager::processBatch(size_t start, size_t end, float deltaTime,
                             const Vector2D &playerPos,
                             const EntityStorage& storage,
                             std::vector<CollisionManager::KinematicUpdate>& collisionUpdates,
                             size_t batchIndex) {
  size_t batchExecutions = 0;

  // Reserve space in collision updates accumulator (approximate size)
  size_t batchSize = end - start;
  collisionUpdates.reserve(collisionUpdates.size() + batchSize);

  // Pre-calculate common values once per batch to reduce per-entity overhead
  const float maxDist = m_maxUpdateDistance.load(std::memory_order_relaxed);
  const float maxDistSquared = maxDist * maxDist;
  const bool hasPlayer = (playerPos.getX() != 0 || playerPos.getY() != 0);

  // OPTIMIZATION: Get world bounds ONCE per batch (not per entity)
  // Eliminates 418+ atomic loads per frame → single atomic load per batch
  // Uses cached pointer (mp_pathfinderManager) to avoid singleton lookup
  const auto &pf = *mp_pathfinderManager;
  float worldWidth, worldHeight;
  if (!pf.getCachedWorldBounds(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
    // Fallback: Use large default if PathfinderManager grid isn't ready yet
    // This ensures entities can move even during world loading/grid rebuild
    worldWidth = 32000.0f;  // Default world size
    worldHeight = 32000.0f;
  }

  // SIMD OPTIMIZATION: Pre-compute distances for this batch using SIMD
  // Process 4 entities at once for 2-4x speedup
  // All entities processed every frame (no staggering) for accurate combat
  // Use pre-allocated buffers to avoid per-batch allocation (Issue #2 fix)
  size_t bufferIndex = batchIndex % m_distanceBuffers.size();
  std::vector<float>& precomputedDistances = m_distanceBuffers[bufferIndex];
  std::vector<Vector2D>& precomputedPositions = m_positionBuffers[bufferIndex];

  // Resize buffers for this batch (clear() + resize keeps capacity, minimal allocation)
  precomputedDistances.clear();
  precomputedDistances.resize(batchSize, 0.0f);
  precomputedPositions.clear();
  precomputedPositions.resize(batchSize);

  calculateDistancesSIMD(start, end, playerPos, storage,
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
      // Use SIMD-precomputed distances for all entities (no staggering)
      // All entities get fresh distance/position every frame for accurate combat
      size_t localIdx = i - start;
      if (hasPlayer) {
        // Use precomputed SIMD results (2-4x faster than scalar)
        hotData.distanceSquared = precomputedDistances[localIdx];
        hotData.position = precomputedPositions[localIdx];
      }

      // Priority-based distance culling - far entities are frozen
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
        // INTERPOLATION: Store current position before any movement updates
        // This enables smooth rendering between fixed timestep updates
        entity->storePositionForInterpolation();

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
        const float minX = halfW;
        const float maxX = worldWidth - halfW;
        const float minY = halfH;
        const float maxY = worldHeight - halfH;
        Vector2D clamped(
            std::clamp(pos.getX(), minX, maxX),
            std::clamp(pos.getY(), minY, maxY)
        );

        // Update entity position (preserves previous position for interpolation)
        entity->updatePositionFromMovement(clamped);

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

        // SYNC: Update cached position so queries always have current data
        // Safe: each batch processes non-overlapping indices (no race condition)
        m_storage.hotData[i].position = pos;
        m_storage.hotData[i].distanceSquared = hotData.distanceSquared;

        // BATCH OPTIMIZATION: Accumulate position/velocity for collision system batch update
        collisionUpdates.emplace_back(entity->getID(), pos, vel);

        batchExecutions++;
      } else {
        // Frozen entities are too far for combat queries (beyond culling distance)
        // No sync needed - they're not moving and outside any combat range
        // When they become active again, they'll be synced on that first active frame
        collisionUpdates.emplace_back(entity->getID(), hotData.position, Vector2D(0, 0));
      }
    } catch (const std::exception &e) {
      AI_ERROR(std::format("Error in batch processing entity: {}", e.what()));
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

  AI_DEBUG(std::format("Cleaned up {} inactive entities", toRemove.size()));
}

void AIManager::cleanupAllEntities() {
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Clean all behaviors
  for (size_t i = 0; i < m_storage.size(); ++i) {
    if (m_storage.behaviors[i] && m_storage.entities[i]) {
      try {
        m_storage.behaviors[i]->clean(m_storage.entities[i]);
      } catch (const std::exception &e) {
        AI_ERROR(std::format("Exception cleaning behavior: {}", e.what()));
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
