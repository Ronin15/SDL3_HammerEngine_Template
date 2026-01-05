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

    // Reserve EDM-to-storage reverse mapping (grows dynamically based on EDM indices)
    m_edmToStorageIndex.reserve(INITIAL_CAPACITY);

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

  {
    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
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
    m_storage.edmIndices.clear();

    m_handleToIndex.clear();
    m_behaviorTemplates.clear();
    m_edmToStorageIndex.clear();  // Clear EDM-to-storage reverse mapping
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

  // Clean up all entities safely
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
    m_edmToStorageIndex.clear();  // Clear EDM-to-storage reverse mapping

    AI_DEBUG("Cleaned up all entities for state transition");
  }

  // Reset all counters and stats
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);
  m_lastCleanupFrame.store(0, std::memory_order_relaxed);

  // Clear player reference completely
  {
    std::lock_guard<std::shared_mutex> lock(m_entitiesMutex);
    m_playerHandle = EntityHandle{};
  }

  // Don't call resetBehaviors() as we've already done comprehensive cleanup above
  // No behavior caches to clear - maps are cleared by resetBehaviors() if needed

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

    // OPTIMIZATION: Use getActiveIndices() to iterate only Active tier entities
    // This reduces iteration from 50K to ~468 (entities within active radius)
    auto& edm = EntityDataManager::Instance();
    auto activeSpan = edm.getActiveIndices();

    if (activeSpan.empty()) {
      return;
    }

    // Copy to local buffer (span may be invalidated during processing)
    // Reuse buffer to avoid per-frame allocation
    m_activeIndicesBuffer.clear();
    m_activeIndicesBuffer.insert(m_activeIndicesBuffer.end(),
                                  activeSpan.begin(), activeSpan.end());

    const size_t entityCount = m_activeIndicesBuffer.size();

    // Start timing AFTER we know we have work to do
    auto startTime = std::chrono::high_resolution_clock::now();
    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);

    // PERFORMANCE: Invalidate spatial query cache for new frame
    // This ensures thread-local caches are fresh and don't use stale collision data
    AIInternal::InvalidateSpatialCache(currentFrame);

    // OPTIMIZATION: Query world bounds ONCE per frame (not per batch)
    float worldWidth = 32000.0f;
    float worldHeight = 32000.0f;
    if (mp_pathfinderManager) {
      float w, h;
      if (mp_pathfinderManager->getCachedWorldBounds(w, h) && w > 0 && h > 0) {
        worldWidth = w;
        worldHeight = h;
      }
    }

    // OPTIMIZATION: Cache player info ONCE per frame (not per behavior call)
    // This eliminates shared_lock contention in getPlayerHandle()/getPlayerPosition()
    EntityHandle cachedPlayerHandle;
    Vector2D cachedPlayerPosition;
    Vector2D cachedPlayerVelocity;
    bool cachedPlayerValid = false;
    {
      std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
      cachedPlayerHandle = m_playerHandle;
      cachedPlayerValid = m_playerHandle.isValid();
      if (cachedPlayerValid) {
        size_t playerIdx = edm.getIndex(m_playerHandle);
        if (playerIdx != SIZE_MAX) {
          auto& playerTransform = edm.getTransformByIndex(playerIdx);
          cachedPlayerPosition = playerTransform.position;
          cachedPlayerVelocity = playerTransform.velocity;
        }
      }
    }

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
          static_cast<size_t>(queueCapacity * HammerEngine::QUEUE_PRESSURE_CRITICAL);

      if (queueSize > pressureThreshold) {
        // Graceful degradation: fallback to single-threaded processing
        AI_DEBUG(std::format("Queue pressure detected ({}/{}), using single-threaded processing",
                             queueSize, queueCapacity));

        processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime, worldWidth, worldHeight,
                     cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity, cachedPlayerValid);
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
          processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime, worldWidth, worldHeight,
                       cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity, cachedPlayerValid);
        } else {
          size_t entitiesPerBatch = entityCount / batchCount;
          size_t remainingEntities = entityCount % batchCount;

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
            // processBatch acquires shared_lock internally for thread-safe read access
            m_batchFutures.push_back(threadSystem.enqueueTaskWithResult(
              [this, start, end, deltaTime, worldWidth, worldHeight,
               cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity, cachedPlayerValid]() -> void {
                try {
                  processBatch(m_activeIndicesBuffer, start, end, deltaTime, worldWidth, worldHeight,
                               cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity, cachedPlayerValid);
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
      processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime, worldWidth, worldHeight,
                   cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity, cachedPlayerValid);
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

    // Periodic frame tracking (balanced frequency)
    if (currentFrame % 300 == 0) {
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
        AI_DEBUG(std::format("AI Summary - Active: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
                             "[Threaded: {} batches, {}/batch]",
                             entityCount, totalUpdateTime, entitiesPerSecond,
                             logBatchCount, entityCount / logBatchCount));
      } else {
        AI_DEBUG(std::format("AI Summary - Active: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
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

void AIManager::registerBehavior(const std::string &name,
                                 std::shared_ptr<AIBehavior> behavior) {
  if (!behavior) {
    AI_ERROR(std::format("Attempted to register null behavior with name: {}", name));
    return;
  }

  std::unique_lock<std::shared_mutex> lock(m_behaviorsMutex);
  m_behaviorTemplates[name] = behavior;
  AI_INFO(std::format("Registered behavior: {}", name));
}

bool AIManager::hasBehavior(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.find(name) != m_behaviorTemplates.end();
}

std::shared_ptr<AIBehavior>
AIManager::getBehavior(const std::string &name) const {
  // THREADING: Behaviors are registered during GameState::enter() before any
  // concurrent access. Multiple readers can safely access with shared_lock.
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
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

  const auto& edm = EntityDataManager::Instance();

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

      // Update active state
      if (!m_storage.hotData[index].active) {
        m_storage.hotData[index].active = true;
      }

      // Refresh EDM index if needed
      size_t edmIndex = edm.getIndex(handle);
      if (index < m_storage.edmIndices.size()) {
        m_storage.edmIndices[index] = edmIndex;
      }

      // Update EDM-to-storage reverse mapping for O(1) lookup in processBatch
      if (edmIndex != SIZE_MAX) {
        if (m_edmToStorageIndex.size() <= edmIndex) {
          m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
        }
        m_edmToStorageIndex[edmIndex] = index;
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
    m_storage.lastUpdateTimes.push_back(0.0f);

    // Cache EntityDataManager index for lock-free batch access
    size_t edmIndex = edm.getIndex(handle);
    m_storage.edmIndices.push_back(edmIndex);

    // Populate EDM-to-storage reverse mapping for O(1) lookup in processBatch
    if (edmIndex != SIZE_MAX) {
      if (m_edmToStorageIndex.size() <= edmIndex) {
        m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
      }
      m_edmToStorageIndex[edmIndex] = newIndex;
    }

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
      // Mark as inactive
      m_storage.hotData[index].active = false;

      if (m_storage.behaviors[index]) {
        m_storage.behaviors[index]->clean(handle);
      }

      // Clear from EDM-to-storage reverse mapping
      size_t edmIndex = m_storage.edmIndices[index];
      if (edmIndex < m_edmToStorageIndex.size()) {
        m_edmToStorageIndex[edmIndex] = SIZE_MAX;
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

void AIManager::unregisterEntity(EntityHandle handle) {
  if (!handle.isValid())
    return;

  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Mark as inactive in main storage
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    // Mark as inactive
    m_storage.hotData[it->second].active = false;
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
  m_edmToStorageIndex.clear();  // Clear EDM-to-storage reverse mapping

  // Reset counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
}

#ifndef NDEBUG
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
#endif

size_t AIManager::getBehaviorCount() const {
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTemplates.size();
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
    // Check capacity to prevent overflow (2 relaxed loads, ~1ns)
    size_t pending = m_messageWriteIndex.load(std::memory_order_relaxed) -
                     m_messageReadIndex.load(std::memory_order_relaxed);
    if (pending >= MESSAGE_QUEUE_SIZE) {
      return;  // Queue full - silently drop (pathological case: 60K+ msg/sec)
    }

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
    // Check capacity to prevent overflow (2 relaxed loads, ~1ns)
    size_t pending = m_messageWriteIndex.load(std::memory_order_relaxed) -
                     m_messageReadIndex.load(std::memory_order_relaxed);
    if (pending >= MESSAGE_QUEUE_SIZE) {
      return;  // Queue full - silently drop (pathological case: 60K+ msg/sec)
    }

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
  // m_behaviorTypeMap is populated once in init() and never modified.
  // No lock needed for concurrent reads of an immutable container.
  auto it = m_behaviorTypeMap.find(behaviorName);
  return (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;
}

void AIManager::processBatch(const std::vector<size_t>& activeIndices,
                             size_t start, size_t end,
                             float deltaTime,
                             float worldWidth, float worldHeight,
                             EntityHandle playerHandle, const Vector2D& playerPos,
                             const Vector2D& playerVel, bool playerValid) {
  // Process batch of Active tier entities using EDM indices directly
  // No tier check needed - getActiveIndices() already filters to Active tier
  size_t batchExecutions = 0;
  auto& edm = EntityDataManager::Instance();

  // No lock needed: m_edmToStorageIndex is read-only during batch window
  // - Behavior assignments happen synchronously via assignBehavior() before batch processing
  // - Entity removals only mark inactive (don't modify vector structure)

  for (size_t i = start; i < end && i < activeIndices.size(); ++i) {
    size_t edmIdx = activeIndices[i];

    // Get storage index from reverse mapping - O(1) lookup, no atomic overhead
    if (edmIdx >= m_edmToStorageIndex.size()) {
      continue;  // No behavior registered for this entity (e.g., Player)
    }
    size_t storageIdx = m_edmToStorageIndex[edmIdx];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size()) {
      continue;  // Invalid storage index
    }
    if (!m_storage.hotData[storageIdx].active) {
      continue;  // Entity marked inactive
    }

    AIBehavior* behavior = m_storage.behaviors[storageIdx].get();
    if (!behavior) {
      continue;
    }
    auto& edmHotData = edm.getHotDataByIndex(edmIdx);
    auto& transform = edm.getTransformByIndex(edmIdx);

    // Pre-fetch BehaviorData and PathData once - avoids repeated Instance() calls in behaviors
    BehaviorData* behaviorData = nullptr;
    PathData* pathData = nullptr;
    BehaviorType btype = static_cast<BehaviorType>(m_storage.hotData[storageIdx].behaviorType);
    if (btype != BehaviorType::None && btype != BehaviorType::COUNT) {
      behaviorData = &edm.getBehaviorData(edmIdx);
      if (behaviorData->isValid()) {
        pathData = &edm.getPathData(edmIdx);
      }
    }

    try {
      // Store previous position for interpolation
      transform.previousPosition = transform.position;

      // Execute behavior logic using handle ID and EDM index for contention-free state access
      EntityHandle handle = edm.getHandle(edmIdx);
      BehaviorContext ctx(transform, edmHotData, handle.getId(), edmIdx, deltaTime,
                          playerHandle, playerPos, playerVel, playerValid, behaviorData, pathData);
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
    } catch (const std::exception& e) {
      AI_ERROR(std::format("Error in batch processing entity: {}", e.what()));
    }
  }

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions, std::memory_order_relaxed);
  }
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

AIManager::~AIManager() {
  if (!m_isShutdown) {
    clean();
  }
}

PathfinderManager &AIManager::getPathfinderManager() const {
  return PathfinderManager::Instance();
}
