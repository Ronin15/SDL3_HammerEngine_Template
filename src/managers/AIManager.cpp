/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
#include "ai/AICommandBus.hpp"
#include "ai/BehaviorExecutors.hpp"
#include "ai/internal/Crowd.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/EntityEvents.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "utils/SIMDMath.hpp"
#include <array>
#include <algorithm>
#include <format>
#include <unordered_map>

// Use SIMD abstraction layer
using namespace HammerEngine::SIMD;

namespace {

constexpr size_t MAX_PENDING_BEHAVIOR_MESSAGES = 4;

struct PendingBehaviorMessageCandidate {
  uint8_t messageId{0};
  uint8_t param{0};
  uint64_t sequence{0};
};

constexpr size_t MAX_COMPACTED_BEHAVIOR_MESSAGES = 16;

void sortPendingBehaviorCandidates(
    std::array<PendingBehaviorMessageCandidate,
               MAX_COMPACTED_BEHAVIOR_MESSAGES>& candidates,
    size_t count) {
  for (size_t i = 1; i < count; ++i) {
    PendingBehaviorMessageCandidate value = candidates[i];
    size_t insertIdx = i;
    while (insertIdx > 0 &&
           candidates[insertIdx - 1].sequence > value.sequence) {
      candidates[insertIdx] = candidates[insertIdx - 1];
      --insertIdx;
    }
    candidates[insertIdx] = value;
  }
}

void upsertPendingBehaviorCandidate(
    std::array<PendingBehaviorMessageCandidate,
               MAX_COMPACTED_BEHAVIOR_MESSAGES>& candidates,
    size_t& count,
    const PendingBehaviorMessageCandidate& candidate) {
  for (size_t i = 0; i < count; ++i) {
    if (candidates[i].messageId == candidate.messageId) {
      candidates[i] = candidate;
      return;
    }
  }

  if (count < candidates.size()) {
    candidates[count++] = candidate;
  }
}

} // namespace

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

    // Pre-allocate storage for better performance
    constexpr size_t INITIAL_CAPACITY = 1000;
    m_storage.reserve(INITIAL_CAPACITY);
    m_handleToIndex.reserve(INITIAL_CAPACITY);

    // Reserve EDM-to-storage reverse mapping (grows dynamically based on EDM
    // indices)
    m_edmToStorageIndex.reserve(INITIAL_CAPACITY);

    m_initialized.store(true, std::memory_order_release);
    m_globallyPaused.store(false, std::memory_order_release);
    m_isShutdown = false;

    // Register default behaviors (Idle, Wander, Chase, Guard, Attack, Flee, Follow)
    registerDefaultBehaviors();

    AI_INFO("AIManager initialized successfully");
    return true;

  } catch (const std::exception &e) {
    AI_ERROR(std::format("Failed to initialize AIManager: {}", e.what()));
    return false;
  }
}

void AIManager::handleCombatEvent(const EventData& data) {
  if (!data.isActive() || !data.event) {
    return;
  }

  auto damageEvent = std::dynamic_pointer_cast<DamageEvent>(data.event);
  if (!damageEvent) {
    return;
  }

  auto& edm = EntityDataManager::Instance();
  EntityHandle targetHandle = damageEvent->getTarget();
  EntityHandle attackerHandle = damageEvent->getSource();
  float gameTime = GameTimeManager::Instance().getTotalGameTimeSeconds();

  size_t idx = edm.getIndex(targetHandle);
  if (idx == SIZE_MAX) {
    return;
  }

  size_t attackerIdx =
      attackerHandle.isValid() ? edm.getIndex(attackerHandle) : SIZE_MAX;

  auto& hotData = edm.getHotDataByIndex(idx);
  const bool targetIsPlayer = targetHandle.isPlayer();
  auto& charData = edm.getCharacterData(targetHandle);

  if (!targetIsPlayer) {
    charData.health = std::max(0.0f, charData.health - damageEvent->getDamage());

    float knockbackScale = 1.0f / std::max(0.1f, charData.mass);
    hotData.transform.velocity =
        hotData.transform.velocity + damageEvent->getKnockback() * knockbackScale;
  }

  damageEvent->setRemainingHealth(charData.health);
  damageEvent->setWasLethal(charData.health <= 0.0f);

  if (attackerHandle.isValid()) {
    Behaviors::processCombatEvent(idx, attackerHandle, targetHandle,
                                  damageEvent->getDamage(), true, gameTime);
    if (attackerIdx != SIZE_MAX && attackerIdx != idx) {
      Behaviors::processCombatEvent(attackerIdx, attackerHandle, targetHandle,
                                    damageEvent->getDamage(), false, gameTime);
    }
  }

  Vector2D combatLocation = hotData.transform.position;
  bool wasLethal = (charData.health <= 0.0f);

  thread_local std::vector<size_t> t_witnessBuffer;
  scanActiveIndicesInRadius(combatLocation, 300.0f, t_witnessBuffer, false);
  for (size_t witnessIdx : t_witnessBuffer) {
    if (witnessIdx == idx || witnessIdx == attackerIdx) {
      continue;
    }

    Behaviors::processWitnessedCombat(witnessIdx, attackerHandle,
                                      combatLocation, gameTime, wasLethal);
  }

  if (!targetIsPlayer && charData.health <= 0.0f && hotData.isAlive()) {
    hotData.flags &= ~EntityHotData::FLAG_ALIVE;
    edm.destroyEntity(targetHandle);
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
  HammerEngine::AICommandBus::Instance().clearAll();
  m_pendingFactionChanges.clear();
  m_pendingBehaviorTransitions.clear();
  m_pendingBehaviorMessages.clear();

  {
    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);

    // Clear all storage (behaviors are data in EDM, no cleanup needed)
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.edmIndices.clear();

    m_handleToIndex.clear();
    m_edmToStorageIndex.clear();
    m_guardEdmIndices.clear();
    for (auto& fv : m_factionEdmIndices) fv.clear();
  }

  // Clear cached manager references
  mp_pathfinderManager = nullptr;

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
  HammerEngine::AICommandBus::Instance().clearAll();
  m_pendingFactionChanges.clear();
  m_pendingBehaviorTransitions.clear();
  m_pendingBehaviorMessages.clear();

  // Batches always complete within update() — no pending futures to wait for.

  // Clean up all entities safely (behaviors are data in EDM, no cleanup needed)
  {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

    size_t entityCount = m_storage.size();

    // Clear all storage completely
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.edmIndices.clear();
    m_handleToIndex.clear();
    m_edmToStorageIndex.clear();
    m_activeIndicesBuffer.clear();
    m_guardEdmIndices.clear();
    for (auto& fv : m_factionEdmIndices) fv.clear();

    AI_INFO_IF(entityCount > 0,
               std::format("Cleaned {} AI entities", entityCount));
    AI_DEBUG("Cleaned up all entities for state transition");
  }

  // Reset all counters and stats
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
  m_totalAssignmentCount.store(0, std::memory_order_relaxed);
  m_frameCounter.store(0, std::memory_order_relaxed);

  // Clear player reference completely
  {
    std::lock_guard<std::shared_mutex> lock(m_entitiesMutex);
    m_playerHandle = EntityHandle{};
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

  // Early exit if no AI-managed entities (e.g., just player with no NPCs)
  // This avoids all setup overhead when there's no behavior work to do
  if (m_storage.hotData.empty()) {
    return;
  }

  try {
    // Apply worker-computed path completions before behavior reads PathData.
    PathfinderManager::Instance().commitCompletedPaths();

    // Commit queued cross-thread commands before reading per-entity behavior state.
    // Order matters: faction changes keep indices coherent before scans;
    // transitions first, then messages, so messages target current behavior.
    commitQueuedFactionChanges();
    commitQueuedBehaviorTransitions();
    commitQueuedBehaviorMessages();

    // Use getActiveIndices() to iterate only Active tier entities
    // This reduces iteration from 50K to ~468 (entities within active radius)
    auto &edm = EntityDataManager::Instance();
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

    uint64_t currentFrame = m_frameCounter.load(std::memory_order_relaxed);

    // Invalidate spatial query cache for new frame
    // This ensures thread-local caches are fresh and don't use stale collision data
    AIInternal::InvalidateSpatialCache(currentFrame);
#ifndef NDEBUG
    AIInternal::ResetCrowdStats();
#endif

    // Query world bounds ONCE per frame (not per batch)
    float worldWidth = 32000.0f;
    float worldHeight = 32000.0f;
    if (mp_pathfinderManager) {
      float w, h;
      if (mp_pathfinderManager->getCachedWorldBounds(w, h) && w > 0 && h > 0) {
        worldWidth = w;
        worldHeight = h;
      }
    }

    // Cache world bounds for behaviors that need them during batch processing
    // (e.g., PatrolBehavior waypoint generation). Avoids WorldManager::Instance()
    // calls from worker threads.
    Behaviors::cacheWorldBounds();

    // Cache player info ONCE per frame (not per behavior call)
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
          const auto &playerTransform = edm.getTransformByIndex(playerIdx);
          cachedPlayerPosition = playerTransform.position;
          cachedPlayerVelocity = playerTransform.velocity;
        }
      }
    }

    // Cache game time ONCE per frame for combat timing comparisons
    float cachedGameTime = GameTimeManager::Instance().getTotalGameTimeSeconds();

    // Cache player edmIndex once per frame (avoids hash lookup per query)
    m_cachedPlayerEdmIdx = m_playerHandle.isValid()
        ? edm.getIndex(m_playerHandle)
        : SIZE_MAX;

    // Start timing ONLY the batch work (preprocessing is fixed main-thread overhead)
    auto startTime = std::chrono::high_resolution_clock::now();

    // Determine threading strategy using adaptive threshold from WorkerBudget
    // WorkerBudget is the AUTHORITATIVE source for production decisions
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    auto decision = budgetMgr.shouldUseThreading(
        HammerEngine::SystemType::AI, entityCount);
    bool useThreading = decision.shouldThread;

#ifndef NDEBUG
    // Debug override: enableThreading(false) forces single-threaded for benchmarks
    if (!m_useThreading.load(std::memory_order_acquire)) {
      useThreading = false;
    }
#endif

    // Track what actually happened (not just what was planned)
    bool actualWasThreaded = false;
    size_t actualBatchCount = 1;

#ifndef NDEBUG
    // Track threading decision for interval logging (local vars, no storage
    // overhead) - only needed for debug logging
    size_t logBatchCount = 1;
    bool logWasThreaded = false;
#endif

    // endTime is set in each code path (single-batch, multi-threaded, single-threaded)
    // right after batch work completes but before enqueueBatch — so only batch work is timed.
    std::chrono::high_resolution_clock::time_point endTime;

    if (useThreading) {
      auto &threadSystem = HammerEngine::ThreadSystem::Instance();

      // Get optimal worker count - WorkerBudget handles queue pressure internally
      // (returns 1 worker under critical pressure, triggering single-batch path)
      size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
          HammerEngine::SystemType::AI, entityCount);

      // Get adaptive batch strategy (maximizes parallelism, fine-tunes based
      // on timing). WorkerBudget determines everything dynamically.
      auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
          HammerEngine::SystemType::AI, entityCount, optimalWorkerCount);

#ifndef NDEBUG
        // Track for interval logging at end of function
        logBatchCount = batchCount;
        logWasThreaded = (batchCount > 1);
#endif

        // Single batch optimization: avoid thread overhead
        if (batchCount <= 1) {
          actualWasThreaded = false;
          actualBatchCount = 1;
          auto damageEvents = processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime,
                       worldWidth, worldHeight, cachedPlayerHandle,
                       cachedPlayerPosition, cachedPlayerVelocity,
                       cachedPlayerValid, cachedGameTime);
          endTime = std::chrono::high_resolution_clock::now();

          // Submit deferred events (single-batch path — outside timing)
          if (!damageEvents.empty()) {
            EventManager::Instance().enqueueBatch(std::move(damageEvents));
          }
        } else {
          actualWasThreaded = true;
          actualBatchCount = batchCount;
          size_t entitiesPerBatch = entityCount / batchCount;
          size_t remainingEntities = entityCount % batchCount;

          // Submit batches using futures that return deferred events
          m_batchFutures.clear();
          m_batchFutures.reserve(batchCount);

          for (size_t i = 0; i < batchCount; ++i) {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            // Add remaining entities to last batch
            if (i == batchCount - 1) {
              end += remainingEntities;
            }

            // Submit each batch - processBatch returns deferred events directly
            m_batchFutures.push_back(threadSystem.enqueueTaskWithResult(
                [this, start, end, deltaTime, worldWidth, worldHeight,
                 cachedPlayerHandle, cachedPlayerPosition, cachedPlayerVelocity,
                 cachedPlayerValid, cachedGameTime]() -> std::vector<EventManager::DeferredEvent> {
                  try {
                    return processBatch(m_activeIndicesBuffer, start, end, deltaTime,
                                 worldWidth, worldHeight, cachedPlayerHandle,
                                 cachedPlayerPosition, cachedPlayerVelocity,
                                 cachedPlayerValid, cachedGameTime);
                  } catch (const std::exception &e) {
                    AI_ERROR(
                        std::format("Exception in AI batch: {}", e.what()));
                    return {};
                  } catch (...) {
                    AI_ERROR("Unknown exception in AI batch");
                    return {};
                  }
                },
                HammerEngine::TaskPriority::High, "AI_Batch"));
          }

          // Wait for all batches and collect deferred events (lock-free collection)
          m_allDamageEvents.clear();
          for (auto& future : m_batchFutures) {
            if (future.valid()) {
              auto batchEvents = future.get();
              if (!batchEvents.empty()) {
                m_allDamageEvents.insert(m_allDamageEvents.end(),
                                       std::make_move_iterator(batchEvents.begin()),
                                       std::make_move_iterator(batchEvents.end()));
              }
            }
          }
          endTime = std::chrono::high_resolution_clock::now();

          // Submit all accumulated deferred events to EventManager (outside timing)
          if (!m_allDamageEvents.empty()) {
            EventManager::Instance().enqueueBatch(std::move(m_allDamageEvents));
          }
        }
    } else {
      // Single-threaded processing
      actualWasThreaded = false;
      actualBatchCount = 1;
      auto damageEvents = processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime, worldWidth,
                   worldHeight, cachedPlayerHandle, cachedPlayerPosition,
                   cachedPlayerVelocity, cachedPlayerValid, cachedGameTime);
      endTime = std::chrono::high_resolution_clock::now();

      // Submit deferred events (single-threaded path — outside timing)
      if (!damageEvents.empty()) {
        EventManager::Instance().enqueueBatch(std::move(damageEvents));
      }
    }
    double totalUpdateTime =
        std::chrono::duration<double, std::milli>(endTime - startTime).count();

    // Report results for unified adaptive tuning - report what actually happened
    if (entityCount > 0) {
      budgetMgr.reportExecution(HammerEngine::SystemType::AI,
                                entityCount, actualWasThreaded, actualBatchCount,
                                totalUpdateTime);
    }

    // Commit commands emitted by worker threads during this frame's batches.
    commitQueuedBehaviorTransitions();
    commitQueuedBehaviorMessages();

    m_frameCounter.fetch_add(1, std::memory_order_relaxed);

#ifndef NDEBUG
    // Interval stats logging - zero overhead in release (entire block compiles out)
    static thread_local uint64_t logFrameCounter = 0;
    if (++logFrameCounter % 1800 == 0 && entityCount > 0) {  // ~30 seconds at 60fps
      // Only calculate expensive stats when actually logging
      double entitiesPerSecond =
          totalUpdateTime > 0 ? (entityCount * 1000.0 / totalUpdateTime) : 0.0;
      const auto crowdStats = AIInternal::GetCrowdStats();
      double crowdHitRate =
          crowdStats.queryCount > 0
              ? (100.0 * static_cast<double>(crowdStats.cacheHits) /
                 static_cast<double>(crowdStats.queryCount))
              : 0.0;
      PathfinderManager::PathfinderStats pathStats{};
      if (mp_pathfinderManager) {
        pathStats = mp_pathfinderManager->getStats();
      }
      double pathHitRate = pathStats.cacheHitRate * 100.0;
      if (logWasThreaded) {
        AI_DEBUG(std::format(
            "AI Summary - Active: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
            "[Threaded: {} batches, {}/batch] Crowd[q:{} hit:{:.0f}% res:{}] "
            "Path[rps:{:.1f} hit:{:.0f}% cache:{}]",
            entityCount, totalUpdateTime, entitiesPerSecond, logBatchCount,
            entityCount / logBatchCount, crowdStats.queryCount, crowdHitRate,
            crowdStats.resultsCount, pathStats.requestsPerSecond, pathHitRate,
            pathStats.cacheSize));
      } else {
        AI_DEBUG(std::format(
            "AI Summary - Active: {}, Update: {:.2f}ms, Throughput: {:.0f}/sec "
            "[Single-threaded] Crowd[q:{} hit:{:.0f}% res:{}] "
            "Path[rps:{:.1f} hit:{:.0f}% cache:{}]",
            entityCount, totalUpdateTime, entitiesPerSecond,
            crowdStats.queryCount, crowdHitRate, crowdStats.resultsCount,
            pathStats.requestsPerSecond, pathHitRate, pathStats.cacheSize));
      }
    }
#endif

  } catch (const std::exception &e) {
    AI_ERROR(std::format("Exception in AIManager::update: {}", e.what()));
  }
}


void AIManager::registerDefaultBehaviors() {
  // Initialize behavior name-to-type map for API compatibility
  m_behaviorTypeMap = {
    {"Idle", BehaviorType::Idle},
    {"Wander", BehaviorType::Wander},
    {"Chase", BehaviorType::Chase},
    {"Patrol", BehaviorType::Patrol},
    {"Guard", BehaviorType::Guard},
    {"Attack", BehaviorType::Attack},
    {"Flee", BehaviorType::Flee},
    {"Follow", BehaviorType::Follow}
  };

  // Register named preset configs (variants of base behaviors)
  m_presetConfigs["SmallWander"] = HammerEngine::BehaviorConfigData::makeWander(
      HammerEngine::WanderBehaviorConfig::createSmallWander());
  m_presetConfigs["LargeWander"] = HammerEngine::BehaviorConfigData::makeWander(
      HammerEngine::WanderBehaviorConfig::createLargeWander());
  m_presetConfigs["EventWander"] = HammerEngine::BehaviorConfigData::makeWander(
      HammerEngine::WanderBehaviorConfig::createEventWander());
  m_presetConfigs["RandomPatrol"] = HammerEngine::BehaviorConfigData::makePatrol(
      HammerEngine::PatrolBehaviorConfig::createRandomPatrol());
  m_presetConfigs["CirclePatrol"] = HammerEngine::BehaviorConfigData::makePatrol(
      HammerEngine::PatrolBehaviorConfig::createCirclePatrol());
  m_presetConfigs["EventTarget"] = HammerEngine::BehaviorConfigData::makeChase(
      HammerEngine::ChaseBehaviorConfig::createEventTarget());

  AI_INFO("Behavior system ready (8 types + 6 presets)");
}

bool AIManager::hasBehavior(const std::string &name) const {
  // Check preset configs first, then base behavior types
  if (m_presetConfigs.find(name) != m_presetConfigs.end()) {
    return true;
  }
  auto it = m_behaviorTypeMap.find(name);
  return it != m_behaviorTypeMap.end();
}

void AIManager::assignBehavior(EntityHandle handle,
                               const std::string &behaviorName) {
  if (!handle.isValid()) {
    AI_ERROR("Cannot assign behavior to invalid handle");
    return;
  }

  // Check for preset config first (SmallWander, LargeWander, etc.)
  auto presetIt = m_presetConfigs.find(behaviorName);
  if (presetIt != m_presetConfigs.end()) {
    // Use preset config directly via the config-based overload
    assignBehavior(handle, presetIt->second);
    return;
  }

  // Fall back to default config for base behavior types
  auto typeIt = m_behaviorTypeMap.find(behaviorName);
  if (typeIt == m_behaviorTypeMap.end()) {
    AI_ERROR(std::format("Unknown behavior name: {}", behaviorName));
    return;
  }
  BehaviorType behaviorType = typeIt->second;

  // Get default config for this behavior type
  auto& edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex == SIZE_MAX) {
    AI_ERROR("Cannot assign behavior: entity not in EDM");
    return;
  }

  auto config = Behaviors::getDefaultConfig(behaviorType);

  // Acquire write lock — index updates and EDM config must be atomic
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_handleToIndex.find(handle);
  if (indexIt != m_handleToIndex.end()) {
    // Update existing entity — remove old indices before overwriting config
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      BehaviorType oldType = edm.getBehaviorConfig(edmIndex).type;
      removeFromIndices(edmIndex, oldType);

      if (!m_storage.hotData[index].active) {
        m_storage.hotData[index].active = true;
      }

      if (index < m_storage.edmIndices.size()) {
        m_storage.edmIndices[index] = edmIndex;
      }

      if (m_edmToStorageIndex.size() <= edmIndex) {
        m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
      }
      m_edmToStorageIndex[edmIndex] = index;

      AI_INFO(std::format("Updated behavior for existing entity to: {}",
                          behaviorName));
    }
  } else {
    // Add new entity
    size_t newIndex = m_storage.size();

    AIEntityData::HotData hotData{};
    hotData.active = true;

    m_storage.hotData.push_back(hotData);
    m_storage.handles.push_back(handle);
    m_storage.lastUpdateTimes.push_back(0.0f);
    m_storage.edmIndices.push_back(edmIndex);

    if (m_edmToStorageIndex.size() <= edmIndex) {
      m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
    }
    m_edmToStorageIndex[edmIndex] = newIndex;

    m_handleToIndex[handle] = newIndex;

    AI_INFO(std::format("Added new entity with behavior: {}", behaviorName));
  }

  // Set config in EDM and initialize state (after old indices removed)
  edm.setBehaviorConfig(edmIndex, config);
  Behaviors::init(edmIndex, config);

  // Add to guard/faction indices for the new behavior
  addToIndices(edmIndex, behaviorType);

  m_totalAssignmentCount.fetch_add(1, std::memory_order_relaxed);
}

void AIManager::assignBehavior(EntityHandle handle,
                               const HammerEngine::BehaviorConfigData& config) {
  if (!handle.isValid()) {
    AI_ERROR("Cannot assign behavior to invalid handle");
    return;
  }
  if (config.type == BehaviorType::None) {
    AI_ERROR("Cannot assign behavior with type None");
    return;
  }

  auto& edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex == SIZE_MAX) {
    AI_ERROR("Cannot assign behavior: entity not in EDM");
    return;
  }

  // Acquire write lock — index updates and EDM config must be atomic
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_handleToIndex.find(handle);
  if (indexIt != m_handleToIndex.end()) {
    // Update existing entity — remove old indices before overwriting config
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      BehaviorType oldType = edm.getBehaviorConfig(edmIndex).type;
      removeFromIndices(edmIndex, oldType);

      if (!m_storage.hotData[index].active) {
        m_storage.hotData[index].active = true;
      }

      if (index < m_storage.edmIndices.size()) {
        m_storage.edmIndices[index] = edmIndex;
      }
      if (m_edmToStorageIndex.size() <= edmIndex) {
        m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
      }
      m_edmToStorageIndex[edmIndex] = index;

      AI_DEBUG(std::format("Assigned behavior directly: type={}",
                           static_cast<int>(config.type)));
    }
  } else {
    // Add new entity
    size_t newIndex = m_storage.size();
    AIEntityData::HotData hotData{};
    hotData.active = true;

    m_storage.hotData.push_back(hotData);
    m_storage.handles.push_back(handle);
    m_storage.lastUpdateTimes.push_back(0.0f);
    m_storage.edmIndices.push_back(edmIndex);

    if (m_edmToStorageIndex.size() <= edmIndex) {
      m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
    }
    m_edmToStorageIndex[edmIndex] = newIndex;

    m_handleToIndex[handle] = newIndex;
    AI_INFO(std::format("Added new entity with behavior: type={}",
                        static_cast<int>(config.type)));
  }

  // Set config in EDM and initialize state (after old indices removed)
  edm.setBehaviorConfig(edmIndex, config);
  Behaviors::init(edmIndex, config);

  // Add to guard/faction indices for the new behavior
  addToIndices(edmIndex, config.type);

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

      // Remove from indices then clear behavior config in EDM
      size_t edmIndex = m_storage.edmIndices[index];
      if (edmIndex != SIZE_MAX) {
        auto& edm = EntityDataManager::Instance();
        BehaviorType oldType = edm.getBehaviorConfig(edmIndex).type;
        removeFromIndices(edmIndex, oldType);
        edm.setBehaviorConfig(edmIndex, HammerEngine::BehaviorConfigData{});

        if (edmIndex < m_edmToStorageIndex.size()) {
          m_edmToStorageIndex[edmIndex] = SIZE_MAX;
        }
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
    if (!m_storage.hotData[it->second].active) return false;

    // Check if entity has a valid behavior config in EDM
    size_t edmIndex = m_storage.edmIndices[it->second];
    if (edmIndex != SIZE_MAX) {
      const auto& edm = EntityDataManager::Instance();
      const auto& config = edm.getBehaviorConfig(edmIndex);
      return config.type != BehaviorType::None;
    }
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
    auto &edm = EntityDataManager::Instance();
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

  // Mark as inactive and remove from indices
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    m_storage.hotData[it->second].active = false;

    size_t edmIndex = m_storage.edmIndices[it->second];
    if (edmIndex != SIZE_MAX) {
      auto& edm = EntityDataManager::Instance();
      BehaviorType oldType = edm.getBehaviorConfig(edmIndex).type;
      removeFromIndices(edmIndex, oldType);
      edm.setBehaviorConfig(edmIndex, HammerEngine::BehaviorConfigData{});

      if (edmIndex < m_edmToStorageIndex.size()) {
        m_edmToStorageIndex[edmIndex] = SIZE_MAX;
      }
    }
  }
}

void AIManager::onEntityFactionChanged(size_t edmIndex, uint8_t oldFaction, uint8_t newFaction) {
  if (oldFaction >= MAX_FACTIONS || newFaction >= MAX_FACTIONS || oldFaction == newFaction) {
    return;
  }
  auto& edm = EntityDataManager::Instance();
  HammerEngine::AICommandBus::Instance().enqueueFactionChange(
      edm.getHandle(edmIndex), edmIndex, oldFaction, newFaction);
}

// Thread safety: m_activeIndicesBuffer and m_cachedPlayerEdmIdx are written on
// the main thread before batch futures are submitted. Futures create a
// happens-before edge, so worker threads read consistent data without a lock.
void AIManager::scanActiveIndicesInRadius(const Vector2D &center, float radius,
                                        std::vector<size_t> &outEdmIndices,
                                        bool excludePlayer) const {
  outEdmIndices.clear();
  const float radiusSq = radius * radius;
  auto &edm = EntityDataManager::Instance();
  for (size_t idx : m_activeIndicesBuffer) {
    float distSq = Vector2D::distanceSquared(
        center, edm.getHotDataByIndex(idx).transform.position);
    if (distSq <= radiusSq) {
      outEdmIndices.push_back(idx);
    }
  }

  // Filter player using cached edmIndex (no hash lookup)
  if (excludePlayer && m_cachedPlayerEdmIdx != SIZE_MAX) {
    std::erase(outEdmIndices, m_cachedPlayerEdmIdx);
  }
}

void AIManager::scanActiveHandlesInRadius(const Vector2D &center, float radius,
                                     std::vector<EntityHandle> &outHandles,
                                     bool excludePlayer) const {
  outHandles.clear();

  thread_local std::vector<size_t> t_edmBuffer;
  scanActiveIndicesInRadius(center, radius, t_edmBuffer, excludePlayer);

  auto &edm = EntityDataManager::Instance();
  outHandles.reserve(t_edmBuffer.size());
  std::transform(t_edmBuffer.begin(), t_edmBuffer.end(), std::back_inserter(outHandles),
                 [&edm](size_t edmIdx) { return edm.getHandle(edmIdx); });
}

void AIManager::scanGuardsInRadius(const Vector2D &center, float radius,
                                    std::vector<size_t> &outEdmIndices,
                                    bool excludePlayer) const {
  outEdmIndices.clear();
  const float radiusSq = radius * radius;
  auto &edm = EntityDataManager::Instance();

  for (size_t edmIdx : m_guardEdmIndices) {
    if (edmIdx >= m_edmToStorageIndex.size()) {
      continue;
    }
    const size_t storageIdx = m_edmToStorageIndex[edmIdx];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size() ||
        !m_storage.hotData[storageIdx].active) {
      continue;
    }
    const auto& hotData = edm.getHotDataByIndex(edmIdx);
    if (!hotData.isAlive()) {
      continue;
    }
    float distSq = Vector2D::distanceSquared(center, hotData.transform.position);
    if (distSq <= radiusSq) {
      outEdmIndices.push_back(edmIdx);
    }
  }

  if (excludePlayer && m_cachedPlayerEdmIdx != SIZE_MAX) {
    std::erase(outEdmIndices, m_cachedPlayerEdmIdx);
  }
}

void AIManager::scanFactionInRadius(uint8_t faction, const Vector2D &center,
                                     float radius,
                                     std::vector<size_t> &outEdmIndices,
                                     bool excludePlayer) const {
  outEdmIndices.clear();
  if (faction >= MAX_FACTIONS) return;
  const float radiusSq = radius * radius;
  auto &edm = EntityDataManager::Instance();
  for (size_t edmIdx : m_factionEdmIndices[faction]) {
    if (edmIdx >= m_edmToStorageIndex.size()) {
      continue;
    }
    const size_t storageIdx = m_edmToStorageIndex[edmIdx];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size() ||
        !m_storage.hotData[storageIdx].active) {
      continue;
    }
    const auto& hotData = edm.getHotDataByIndex(edmIdx);
    if (!hotData.isAlive()) {
      continue;
    }
    float distSq = Vector2D::distanceSquared(
        center, hotData.transform.position);
    if (distSq <= radiusSq) {
      outEdmIndices.push_back(edmIdx);
    }
  }
  if (excludePlayer && m_cachedPlayerEdmIdx != SIZE_MAX) {
    std::erase(outEdmIndices, m_cachedPlayerEdmIdx);
  }
}

void AIManager::addToIndices(size_t edmIndex, BehaviorType behaviorType) {
  // Guard index
  if (behaviorType == BehaviorType::Guard) {
    if (std::find(m_guardEdmIndices.begin(), m_guardEdmIndices.end(), edmIndex)
        == m_guardEdmIndices.end()) {
      m_guardEdmIndices.push_back(edmIndex);
    }
  }
  // Faction index
  auto &edm = EntityDataManager::Instance();
  uint8_t faction = edm.getCharacterDataByIndex(edmIndex).faction;
  if (faction < MAX_FACTIONS) {
    auto &factionVec = m_factionEdmIndices[faction];
    if (std::find(factionVec.begin(), factionVec.end(), edmIndex)
        == factionVec.end()) {
      factionVec.push_back(edmIndex);
    }
  }
}

void AIManager::removeFromIndices(size_t edmIndex, BehaviorType oldBehaviorType) {
  if (oldBehaviorType == BehaviorType::Guard) {
    std::erase(m_guardEdmIndices, edmIndex);
  }
  auto &edm = EntityDataManager::Instance();
  uint8_t faction = edm.getCharacterDataByIndex(edmIndex).faction;
  if (faction < MAX_FACTIONS) {
    std::erase(m_factionEdmIndices[faction], edmIndex);
  }
}

void AIManager::commitQueuedFactionChanges() {
  HammerEngine::AICommandBus::Instance().drainFactionChanges(m_pendingFactionChanges);
  if (m_pendingFactionChanges.empty()) {
    return;
  }

  auto& edm = EntityDataManager::Instance();
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  for (const auto& cmd : m_pendingFactionChanges) {
    if (!cmd.targetHandle.isValid()) {
      continue;
    }

    const size_t edmIndex = edm.getIndex(cmd.targetHandle);
    if (edmIndex == SIZE_MAX || edmIndex != cmd.targetEdmIndex ||
        edmIndex >= m_edmToStorageIndex.size()) {
      continue;
    }

    const size_t storageIdx = m_edmToStorageIndex[edmIndex];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size() ||
        !m_storage.hotData[storageIdx].active) {
      continue;
    }

    const uint8_t currentFaction = edm.getCharacterDataByIndex(edmIndex).faction;
    if (currentFaction >= MAX_FACTIONS) {
      continue;
    }

    for (auto& factionVec : m_factionEdmIndices) {
      std::erase(factionVec, edmIndex);
    }

    auto& targetFaction = m_factionEdmIndices[currentFaction];
    if (std::find(targetFaction.begin(), targetFaction.end(), edmIndex) == targetFaction.end()) {
      targetFaction.push_back(edmIndex);
    }
  }
}

void AIManager::commitQueuedBehaviorMessages() {
  HammerEngine::AICommandBus::Instance().drainBehaviorMessages(m_pendingBehaviorMessages);
  if (m_pendingBehaviorMessages.empty()) {
    return;
  }

  auto& edm = EntityDataManager::Instance();
  size_t droppedCount = 0;
  std::sort(m_pendingBehaviorMessages.begin(), m_pendingBehaviorMessages.end(),
            [](const auto& lhs, const auto& rhs) {
              if (lhs.targetEdmIndex != rhs.targetEdmIndex) {
                return lhs.targetEdmIndex < rhs.targetEdmIndex;
              }
              return lhs.sequence < rhs.sequence;
            });

  size_t i = 0;
  while (i < m_pendingBehaviorMessages.size()) {
    const size_t edmIndex = m_pendingBehaviorMessages[i].targetEdmIndex;
    size_t runEnd = i + 1;
    while (runEnd < m_pendingBehaviorMessages.size() &&
           m_pendingBehaviorMessages[runEnd].targetEdmIndex == edmIndex) {
      ++runEnd;
    }

    if (edmIndex == SIZE_MAX || !edm.hasBehaviorData(edmIndex)) {
      i = runEnd;
      continue;
    }

    auto& data = edm.getBehaviorData(edmIndex);
    std::array<PendingBehaviorMessageCandidate,
               MAX_COMPACTED_BEHAVIOR_MESSAGES> candidates{};
    size_t candidateCount = 0;

    // Existing inbox entries are always older than commands drained this frame.
    for (uint8_t msgIdx = 0; msgIdx < data.pendingMessageCount; ++msgIdx) {
      upsertPendingBehaviorCandidate(
          candidates, candidateCount,
          {data.pendingMessages[msgIdx].messageId,
           data.pendingMessages[msgIdx].param,
           static_cast<uint64_t>(msgIdx)});
    }

    const uint64_t sequenceBias = static_cast<uint64_t>(data.pendingMessageCount);
    for (size_t runIdx = i; runIdx < runEnd; ++runIdx) {
      const auto& cmd = m_pendingBehaviorMessages[runIdx];
      if (!cmd.targetHandle.isValid()) {
        continue;
      }

      const size_t resolvedEdmIndex = edm.getIndex(cmd.targetHandle);
      if (resolvedEdmIndex == SIZE_MAX || resolvedEdmIndex != edmIndex) {
        continue;
      }

      upsertPendingBehaviorCandidate(
          candidates, candidateCount,
          {cmd.messageId, cmd.param, cmd.sequence + sequenceBias});
    }

    if (candidateCount == 0) {
      i = runEnd;
      continue;
    }

    sortPendingBehaviorCandidates(candidates, candidateCount);

    size_t firstKept = 0;
    if (candidateCount > MAX_PENDING_BEHAVIOR_MESSAGES) {
      droppedCount += candidateCount - MAX_PENDING_BEHAVIOR_MESSAGES;
      firstKept = candidateCount - MAX_PENDING_BEHAVIOR_MESSAGES;
    }

    data.pendingMessageCount = 0;
    for (size_t candidateIdx = firstKept; candidateIdx < candidateCount;
         ++candidateIdx) {
      data.pendingMessages[data.pendingMessageCount].messageId =
          candidates[candidateIdx].messageId;
      data.pendingMessages[data.pendingMessageCount].param =
          candidates[candidateIdx].param;
      data.pendingMessageCount++;
    }

    i = runEnd;
  }

  if (droppedCount > 0) {
    AI_WARN(std::format("Behavior message queue overflow: dropped {} messages", droppedCount));
  }
}

void AIManager::commitQueuedBehaviorTransitions() {
  HammerEngine::AICommandBus::Instance().drainBehaviorTransitions(m_pendingBehaviorTransitions);
  if (m_pendingBehaviorTransitions.empty()) {
    return;
  }

  auto& edm = EntityDataManager::Instance();
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  // Coalesce to one transition per target for this commit pass.
  // Multiple worker threads can enqueue conflicting transitions for the same
  // entity in one frame. Resolve by latest logical enqueue sequence.
  std::vector<HammerEngine::AICommandBus::BehaviorTransitionCommand> selected;
  selected.reserve(m_pendingBehaviorTransitions.size());
  std::unordered_map<size_t, size_t> selectedByEdmIndex;
  selectedByEdmIndex.reserve(m_pendingBehaviorTransitions.size());

  for (const auto& cmd : m_pendingBehaviorTransitions) {
    if (!cmd.targetHandle.isValid()) {
      continue;
    }

    const size_t resolvedEdmIndex = edm.getIndex(cmd.targetHandle);
    if (resolvedEdmIndex == SIZE_MAX || resolvedEdmIndex != cmd.targetEdmIndex ||
        resolvedEdmIndex >= m_edmToStorageIndex.size()) {
      continue;
    }

    auto it = selectedByEdmIndex.find(resolvedEdmIndex);
    if (it == selectedByEdmIndex.end()) {
      selectedByEdmIndex.emplace(resolvedEdmIndex, selected.size());
      selected.push_back(cmd);
      continue;
    }

    auto& existing = selected[it->second];
    if (cmd.sequence > existing.sequence) {
      existing = cmd;
    }
  }

  std::sort(selected.begin(), selected.end(),
            [](const auto& a, const auto& b) {
              return a.targetEdmIndex < b.targetEdmIndex;
            });

  for (const auto& cmd : selected) {
    if (!cmd.targetHandle.isValid()) {
      continue;
    }

    const size_t edmIndex = edm.getIndex(cmd.targetHandle);
    if (edmIndex == SIZE_MAX || edmIndex != cmd.targetEdmIndex ||
        edmIndex >= m_edmToStorageIndex.size()) {
      continue;
    }

    size_t storageIdx = m_edmToStorageIndex[edmIndex];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size() ||
        !m_storage.hotData[storageIdx].active) {
      continue;
    }

    const auto oldConfig = edm.getBehaviorConfig(edmIndex);
    removeFromIndices(edmIndex, oldConfig.type);

    edm.clearBehaviorData(edmIndex);
    edm.setBehaviorConfig(edmIndex, cmd.config);
    Behaviors::init(edmIndex, cmd.config);

    addToIndices(edmIndex, cmd.config.type);
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

  // Clear all data (behaviors are data in EDM, no cleanup needed)
  m_storage.hotData.clear();
  m_storage.handles.clear();
  m_storage.lastUpdateTimes.clear();
  m_storage.edmIndices.clear();
  m_handleToIndex.clear();
  m_edmToStorageIndex.clear();
  m_guardEdmIndices.clear();
  for (auto& fv : m_factionEdmIndices) fv.clear();

  // Reset counters
  m_totalBehaviorExecutions.store(0, std::memory_order_relaxed);
}

#ifndef NDEBUG
void AIManager::enableThreading(bool enable) {
  m_useThreading.store(enable, std::memory_order_release);
  AI_INFO(std::format("Threading {}", enable ? "enabled" : "disabled"));
}
#endif

size_t AIManager::getBehaviorCount() const {
  // m_behaviorTypeMap is immutable after init() — no lock needed
  return m_behaviorTypeMap.size();
}

size_t AIManager::getBehaviorUpdateCount() const {
  return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
  return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

BehaviorType
AIManager::inferBehaviorType(const std::string &behaviorName) const {
  // m_behaviorTypeMap is populated once in registerDefaultBehaviors() and never modified.
  // No lock needed for concurrent reads of an immutable container.
  auto it = m_behaviorTypeMap.find(behaviorName);
  return (it != m_behaviorTypeMap.end()) ? it->second : BehaviorType::Custom;
}

std::vector<EventManager::DeferredEvent> AIManager::processBatch(
                             const std::vector<size_t> &activeIndices,
                             size_t start, size_t end, float deltaTime,
                             float worldWidth, float worldHeight,
                             EntityHandle playerHandle,
                             const Vector2D &playerPos,
                             const Vector2D &playerVel, bool playerValid,
                             float gameTime) {
  // Process batch of Active tier entities using EDM indices directly
  // No tier check needed - getActiveIndices() already filters to Active tier
  size_t batchExecutions = 0;
  auto &edm = EntityDataManager::Instance();

  // No lock needed: m_edmToStorageIndex is read-only during batch window
  // - Behavior assignments happen synchronously via assignBehavior() before
  // batch processing
  // - Entity removals only mark inactive (don't modify vector structure)

  auto updateMovementScalar = [&](TransformData &transform,
                                  const EntityHotData &edmHotData) {
    Vector2D pos = transform.position + (transform.velocity * deltaTime);

    float halfW = edmHotData.halfWidth;
    float halfH = edmHotData.halfHeight;
    float minX = halfW;
    float maxX = worldWidth - halfW;
    float minY = halfH;
    float maxY = worldHeight - halfH;
    if (maxX < minX) {
      minX = worldWidth * 0.5f;
      maxX = minX;
    }
    if (maxY < minY) {
      minY = worldHeight * 0.5f;
      maxY = minY;
    }
    Vector2D clamped(std::clamp(pos.getX(), minX, maxX),
                     std::clamp(pos.getY(), minY, maxY));
    transform.position = clamped;

    if (clamped.getX() != pos.getX()) {
      transform.velocity.setX(0.0f);
    }
    if (clamped.getY() != pos.getY()) {
      transform.velocity.setY(0.0f);
    }
  };

  std::array<TransformData *, 4> batchTransforms{};
  std::array<const EntityHotData *, 4> batchHotData{};
  size_t batchCount = 0;

  auto flushMovementBatch = [&]() {
    if (batchCount == 0) {
      return;
    }
    if (batchCount < 4) {
      for (size_t lane = 0; lane < batchCount; ++lane) {
        updateMovementScalar(*batchTransforms[lane], *batchHotData[lane]);
      }
      batchCount = 0;
      return;
    }

    alignas(16) float posX[4];
    alignas(16) float posY[4];
    alignas(16) float velX[4];
    alignas(16) float velY[4];
    alignas(16) float minX[4];
    alignas(16) float maxX[4];
    alignas(16) float minY[4];
    alignas(16) float maxY[4];

    for (size_t lane = 0; lane < 4; ++lane) {
      TransformData *transform = batchTransforms[lane];
      const EntityHotData *hotData = batchHotData[lane];
      posX[lane] = transform->position.getX();
      posY[lane] = transform->position.getY();
      velX[lane] = transform->velocity.getX();
      velY[lane] = transform->velocity.getY();
      float laneMinX = hotData->halfWidth;
      float laneMaxX = worldWidth - hotData->halfWidth;
      float laneMinY = hotData->halfHeight;
      float laneMaxY = worldHeight - hotData->halfHeight;
      if (laneMaxX < laneMinX) {
        laneMinX = worldWidth * 0.5f;
        laneMaxX = laneMinX;
      }
      if (laneMaxY < laneMinY) {
        laneMinY = worldHeight * 0.5f;
        laneMaxY = laneMinY;
      }
      minX[lane] = laneMinX;
      maxX[lane] = laneMaxX;
      minY[lane] = laneMinY;
      maxY[lane] = laneMaxY;
    }

    const Float4 deltaTimeVec = broadcast(deltaTime);
    Float4 posXv = load4_aligned(posX);
    Float4 posYv = load4_aligned(posY);
    const Float4 velXv = load4_aligned(velX);
    const Float4 velYv = load4_aligned(velY);

    posXv = madd(velXv, deltaTimeVec, posXv);
    posYv = madd(velYv, deltaTimeVec, posYv);

    const Float4 minXv = load4_aligned(minX);
    const Float4 maxXv = load4_aligned(maxX);
    const Float4 minYv = load4_aligned(minY);
    const Float4 maxYv = load4_aligned(maxY);
    const Float4 clampedXv = clamp(posXv, minXv, maxXv);
    const Float4 clampedYv = clamp(posYv, minYv, maxYv);

    const Float4 xDiff =
        bitwise_or(cmplt(clampedXv, posXv), cmplt(posXv, clampedXv));
    const Float4 yDiff =
        bitwise_or(cmplt(clampedYv, posYv), cmplt(posYv, clampedYv));
    const int clampXMask = movemask(xDiff);
    const int clampYMask = movemask(yDiff);

    store4_aligned(posX, clampedXv);
    store4_aligned(posY, clampedYv);

    for (size_t lane = 0; lane < 4; ++lane) {
      TransformData *transform = batchTransforms[lane];
      transform->position.setX(posX[lane]);
      transform->position.setY(posY[lane]);

      if ((clampXMask >> lane) & 0x1) {
        transform->velocity.setX(0.0f);
      }
      if ((clampYMask >> lane) & 0x1) {
        transform->velocity.setY(0.0f);
      }
    }

    batchCount = 0;
  };

  for (size_t i = start; i < end && i < activeIndices.size(); ++i) {
    size_t edmIdx = activeIndices[i];

    // Get storage index from reverse mapping - O(1) lookup, no atomic overhead
    if (edmIdx >= m_edmToStorageIndex.size()) {
      continue; // No behavior registered for this entity (e.g., Player)
    }
    size_t storageIdx = m_edmToStorageIndex[edmIdx];
    if (storageIdx == SIZE_MAX || storageIdx >= m_storage.size()) {
      continue; // Invalid storage index
    }
    if (!m_storage.hotData[storageIdx].active) {
      continue; // Entity marked inactive
    }

    auto &edmHotData = edm.getHotDataByIndex(edmIdx);
    auto &transform =
        edmHotData
            .transform; // Direct access, avoid redundant getTransformByIndex()

    // Pre-fetch BehaviorData, PathData, and MemoryData once - avoids repeated Instance()
    // calls in behaviors BehaviorType is read from EDM BehaviorData (single
    // source of truth)
    BehaviorData *behaviorData = nullptr;
    PathData *pathData = nullptr;
    NPCMemoryData *memoryData = nullptr;
    if (edm.hasBehaviorData(edmIdx)) {
      behaviorData = &edm.getBehaviorData(edmIdx);
      if (behaviorData->isValid() &&
          behaviorData->behaviorType != BehaviorType::None &&
          behaviorData->behaviorType != BehaviorType::COUNT) {
        pathData = &edm.getPathData(edmIdx);
      }
    }

    // Fetch memory data (independent of behavior data)
    if (edm.hasMemoryData(edmIdx)) {
      memoryData = &edm.getMemoryData(edmIdx);
      // Update emotional decay each frame
      if (memoryData->isValid()) {
        edm.updateEmotionalDecay(edmIdx, deltaTime);
      }
    }

    // Pre-fetch CharacterData to avoid repeated getCharacterDataByIndex() calls in behaviors
    const CharacterData* characterData = &edm.getCharacterDataByIndex(edmIdx);

    // Get behavior config from EDM
    const auto& config = edm.getBehaviorConfig(edmIdx);
    if (config.type == BehaviorType::None) {
      continue;  // No behavior configured for this entity
    }

    try {
      // Store previous position for interpolation
      transform.previousPosition = transform.position;

      // Execute behavior logic using cached handle ID and EDM index for
      // contention-free state access
      BehaviorContext ctx(
          transform, edmHotData, m_storage.handles[storageIdx].getId(), edmIdx,
          deltaTime, playerHandle, playerPos, playerVel, playerValid,
          behaviorData, pathData, memoryData, characterData,
          0.0f, 0.0f, worldWidth, worldHeight, true, gameTime);
      Behaviors::execute(ctx, config);

      batchTransforms[batchCount] = &transform;
      batchHotData[batchCount] = &edmHotData;
      ++batchCount;
      if (batchCount == 4) {
        flushMovementBatch();
      }

      ++batchExecutions;
    } catch (const std::exception &e) {
      AI_ERROR(std::format("Error in batch processing entity: {}", e.what()));
    }
  }

  flushMovementBatch();

  if (batchExecutions > 0) {
    m_totalBehaviorExecutions.fetch_add(batchExecutions,
                                        std::memory_order_relaxed);
  }

  // Collect all deferred events from this batch's thread-local buffers (lock-free)
  // Uses ref-based API to preserve thread_local vector capacity across frames
  std::vector<EventManager::DeferredEvent> deferredEvents;
  Behaviors::collectDeferredDamageEvents(deferredEvents);
  return deferredEvents;
}

uint64_t AIManager::getCurrentTimeNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

int AIManager::getEntityPriority(EntityHandle handle) const {
  if (!handle.isValid())
    return DEFAULT_PRIORITY;

  // Read priority from EDM CharacterData (single source of truth)
  auto &edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex != SIZE_MAX) {
    const auto &charData = edm.getCharacterDataByIndex(edmIndex);
    return charData.priority;
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
