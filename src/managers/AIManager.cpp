/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/AIManager.hpp"
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

    // Reserve EDM-to-storage reverse mapping (grows dynamically based on EDM
    // indices)
    m_edmToStorageIndex.reserve(INITIAL_CAPACITY);

    // Initialize lock-free message queue
    for (auto &msg : m_lockFreeMessages) {
      msg.ready.store(false, std::memory_order_relaxed);
    }

    m_initialized.store(true, std::memory_order_release);
    m_globallyPaused.store(false, std::memory_order_release);
    m_isShutdown = false;

    // Register default behaviors (Idle, Wander, Chase, Guard, Attack, Flee, Follow)
    registerDefaultBehaviors();

    // Register damage handler for deferred damage application (thread-safe)
    // DamageEvents are queued during parallel batch processing and applied here
    // Responsibilities:
    //   - Apply damage to health (EDM data)
    //   - Apply knockback to velocity (EDM data)
    //   - Update victim's memory for threat detection (EDM data)
    //   - Mark dead + queue destruction if health <= 0 (EDM lifecycle)
    // NOT handled here:
    //   - Combat responses (AI system determines in processBatch)
    EventManager::Instance().registerHandler(EventTypeId::Combat,
        [](const EventData& data) {
          if (!data.isActive() || !data.event) return;

          auto damageEvent = std::dynamic_pointer_cast<DamageEvent>(data.event);
          if (!damageEvent) return;

          auto& edm = EntityDataManager::Instance();
          EntityHandle targetHandle = damageEvent->getTarget();
          EntityHandle attackerHandle = damageEvent->getSource();

          size_t idx = edm.getIndex(targetHandle);
          if (idx == SIZE_MAX) return;

          auto& hotData = edm.getHotDataByIndex(idx);
          auto& charData = edm.getCharacterData(targetHandle);

          // Apply health damage
          charData.health = std::max(0.0f, charData.health - damageEvent->getDamage());

          // Populate event data for downstream consumers
          damageEvent->setRemainingHealth(charData.health);
          damageEvent->setWasLethal(charData.health <= 0.0f);

          // Apply knockback (scaled by inverse mass - heavier entities resist more)
          float knockbackScale = 1.0f / std::max(0.1f, charData.mass);
          hotData.transform.velocity = hotData.transform.velocity + damageEvent->getKnockback() * knockbackScale;

          // Record combat data and apply personality-scaled emotions
          if (attackerHandle.isValid()) {
            Behaviors::processCombatEvent(idx, attackerHandle, targetHandle,
                                          damageEvent->getDamage(), true, 0.0f);
          }

          // Notify nearby NPCs that witnessed this combat (spatial grid: O(K) not O(N))
          Vector2D combatLocation = hotData.transform.position;
          bool wasLethal = (charData.health <= 0.0f);
          uint8_t victimFaction = edm.getCharacterDataByIndex(idx).faction;

          thread_local std::vector<size_t> t_witnessBuffer;
          AIManager::Instance().queryEdmIndicesInRadius(combatLocation, 300.0f, t_witnessBuffer, false);
          for (size_t witnessIdx : t_witnessBuffer) {
            if (witnessIdx == idx) continue;
            Behaviors::processWitnessedCombat(witnessIdx, attackerHandle,
                                               combatLocation, 0.0f, wasLethal);

            // RAISE_ALERT + PANIC for same-faction nearby allies
            const auto& witnessChar = edm.getCharacterDataByIndex(witnessIdx);
            if (witnessChar.faction == victimFaction) {
              Behaviors::queueBehaviorMessage(witnessIdx, BehaviorMessage::RAISE_ALERT);
              if (wasLethal) {
                Behaviors::queueBehaviorMessage(witnessIdx, BehaviorMessage::PANIC);
              }
            }
          }

          // Death handling (EDM lifecycle) - O(1) per damage event
          if (charData.health <= 0.0f && hotData.isAlive()) {
            hotData.flags &= ~EntityHotData::FLAG_ALIVE;
            edm.destroyEntity(targetHandle);
          }
        });

    // BehaviorMessage handler: delivers deferred inter-entity messages on main thread
    EventManager::Instance().registerHandler(EventTypeId::BehaviorMessage,
        [](const EventData& data) {
          if (!data.isActive() || !data.event) return;
          auto alertEvent = std::dynamic_pointer_cast<AlertEvent>(data.event);
          if (!alertEvent) return;
          Behaviors::queueBehaviorMessage(alertEvent->getTargetEdmIndex(),
                                           alertEvent->getMessageId(),
                                           alertEvent->getParam());
        });

    // No NPCSpawn handler in AIManager: state owns creation; AI manages
    // behavior only.

    // NOTE: PathfinderManager and CollisionManager are now initialized by
    // GameEngine AIManager depends on these managers but doesn't manage their
    // lifecycle

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

  {
    std::unique_lock<std::shared_mutex> entitiesLock(m_entitiesMutex);
    std::unique_lock<std::shared_mutex> behaviorsLock(m_behaviorsMutex);
    std::lock_guard<std::mutex> messagesLock(m_messagesMutex);

    // Clear all storage (behaviors are data in EDM, no cleanup needed)
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.edmIndices.clear();

    m_handleToIndex.clear();
    m_edmToStorageIndex.clear(); // Clear EDM-to-storage reverse mapping
    m_messageQueue.clear();
  }

  // Clear cached manager references
  mp_pathfinderManager = nullptr;

  // NOTE: PathfinderManager and CollisionManager are now cleaned up by
  // GameEngine AIManager no longer manages their lifecycle

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

  // Batches always complete within update() — no pending futures to wait for.

  // DETERMINISTIC SYNCHRONIZATION: Wait for all assignment batches to complete
  //
  // Replaces empirical 100ms sleep with futures-based completion tracking.
  // This ensures all async assignment tasks complete before state transition
  // clears entity data, preventing use-after-free and race conditions.
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

  // Clean up all entities safely (behaviors are data in EDM, no cleanup needed)
  {
    std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

    size_t entityCount = m_storage.size();

    // Clear all storage completely
    m_storage.hotData.clear();
    m_storage.handles.clear();
    m_storage.lastUpdateTimes.clear();
    m_storage.edmIndices.clear(); // Clear EDM indices to prevent stale data
    m_handleToIndex.clear();
    m_edmToStorageIndex.clear(); // Clear EDM-to-storage reverse mapping

    AI_INFO_IF(entityCount > 0,
               std::format("Cleaned {} AI entities", entityCount));
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

  // Don't call resetBehaviors() as we've already done comprehensive cleanup
  // above No behavior caches to clear - maps are cleared by resetBehaviors() if
  // needed

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

  // NOTE: We do NOT wait for previous frame's batches here - they can overlap
  // with current frame The critical sync happens in GameEngine before
  // CollisionManager to ensure collision data is ready This allows better frame
  // pipelining on low-core systems

  try {
    // Do not carry over AI update futures across frames to avoid races with
    // render. Any previous frame's update work must be completed within its
    // frame.

    // OPTIMIZATION: Use getActiveIndices() to iterate only Active tier entities
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

    // PERFORMANCE: Invalidate spatial query cache for new frame
    // This ensures thread-local caches are fresh and don't use stale collision
    // data
    AIInternal::InvalidateSpatialCache(currentFrame);
#ifndef NDEBUG
    AIInternal::ResetCrowdStats();
#endif

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

    // Cache world bounds for behaviors that need them during batch processing
    // (e.g., PatrolBehavior waypoint generation). Avoids WorldManager::Instance()
    // calls from worker threads.
    Behaviors::cacheWorldBounds();

    // OPTIMIZATION: Cache player info ONCE per frame (not per behavior call)
    // This eliminates shared_lock contention in
    // getPlayerHandle()/getPlayerPosition()
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

    // OPTIMIZATION: Cache game time ONCE per frame for combat timing comparisons
    float cachedGameTime = GameTimeManager::Instance().getTotalGameTimeSeconds();

    // Cache player edmIndex once per frame (avoids hash lookup per query)
    m_cachedPlayerEdmIdx = m_playerHandle.isValid()
        ? edm.getIndex(m_playerHandle)
        : SIZE_MAX;

    // Emotional contagion removed — O(N²) per-frame scan caused hitches (83ms+).
    // Witness fear (direct combat observation) is handled by processWitnessedCombat()
    // in the damage event handler. Cascading NPC-to-NPC fear propagation is not
    // currently implemented.

    // Centralized engagement pre-pass: push targets to aggressive Idle/Wander NPCs
    // Replaces per-entity shouldEngageEnemy() in behaviors — O(N/60) checks per frame
    {
      constexpr size_t ENGAGE_CHECK_INTERVAL = 60;
      constexpr float ENGAGE_RANGE_SQ = 300.0f * 300.0f;
      constexpr float AGGRESSION_THRESHOLD = 0.8f;

      for (size_t sourceIdx : m_activeIndicesBuffer) {
        if ((currentFrame + sourceIdx) % ENGAGE_CHECK_INTERVAL != 0) continue;

        if (!edm.hasMemoryData(sourceIdx)) continue;
        const auto& memory = edm.getMemoryData(sourceIdx);

        // Quick aggression threshold (same as shouldEngageEnemy)
        if (memory.emotions.aggression + memory.personality.aggression < AGGRESSION_THRESHOLD) continue;

        // Push to passive behaviors (Idle/Wander) and targetless Attack/SEEKING entities
        auto& behaviorData = edm.getBehaviorData(sourceIdx);
        BehaviorType type = behaviorData.behaviorType;
        if (type == BehaviorType::Attack) {
            const auto& atk = behaviorData.state.attack;
            if (atk.hasTarget || atk.hasExplicitTarget) continue;
        } else if (type != BehaviorType::Idle && type != BehaviorType::Wander) {
            continue;
        }

        Vector2D sourcePos = edm.getHotDataByIndex(sourceIdx).transform.position;
        uint8_t sourceFaction = edm.getCharacterDataByIndex(sourceIdx).faction;

        EntityHandle bestTarget{};
        float bestDistSq = ENGAGE_RANGE_SQ;

        for (size_t targetIdx : m_activeIndicesBuffer) {
          if (targetIdx == sourceIdx) continue;
          const auto& hot = edm.getHotDataByIndex(targetIdx);
          if (!hot.isAlive()) continue;
          if (edm.getCharacterDataByIndex(targetIdx).faction == sourceFaction) continue;
          float distSq = Vector2D::distanceSquared(sourcePos, hot.transform.position);
          if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestTarget = edm.getHandle(targetIdx);
          }
        }

        if (bestTarget.isValid()) {
          behaviorData.pendingEngageTarget = bestTarget;
        }
      }
    }

    // Start timing ONLY the batch work (preprocessing is fixed main-thread overhead)
    auto startTime = std::chrono::high_resolution_clock::now();

    // Determine threading strategy using adaptive threshold from WorkerBudget
    // WorkerBudget is the AUTHORITATIVE source - no manager overrides
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    auto decision = budgetMgr.shouldUseThreading(
        HammerEngine::SystemType::AI, entityCount);
    bool useThreading = decision.shouldThread;

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

          // Submit damage events (single-batch path — outside timing)
          if (!damageEvents.empty()) {
            EventManager::Instance().enqueueBatch(std::move(damageEvents));
          }
        } else {
          actualWasThreaded = true;
          actualBatchCount = batchCount;
          size_t entitiesPerBatch = entityCount / batchCount;
          size_t remainingEntities = entityCount % batchCount;

          // Submit batches using futures that return damage events
          m_batchFutures.clear();
          m_batchFutures.reserve(batchCount);

          for (size_t i = 0; i < batchCount; ++i) {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            // Add remaining entities to last batch
            if (i == batchCount - 1) {
              end += remainingEntities;
            }

            // Submit each batch - processBatch returns damage events directly
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

          // Wait for all batches and collect damage events (lock-free collection)
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

          // Submit all accumulated damage events to EventManager (outside timing)
          if (!m_allDamageEvents.empty()) {
            EventManager::Instance().enqueueBatch(std::move(m_allDamageEvents));
          }
        }
    } else {
      // Single-threaded processing (threading disabled in config)
      actualWasThreaded = false;
      actualBatchCount = 1;
      auto damageEvents = processBatch(m_activeIndicesBuffer, 0, entityCount, deltaTime, worldWidth,
                   worldHeight, cachedPlayerHandle, cachedPlayerPosition,
                   cachedPlayerVelocity, cachedPlayerValid, cachedGameTime);
      endTime = std::chrono::high_resolution_clock::now();

      // Submit damage events (single-threaded path — outside timing)
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

    // Process lock-free message queue (AFTER timing — not entity batch work)
    processMessageQueue();

    currentFrame = m_frameCounter.fetch_add(1, std::memory_order_relaxed);

    // Periodic frame tracking (balanced frequency)
    if (currentFrame % 1800 == 0) {  // ~30 seconds at 60fps
      m_lastCleanupFrame.store(currentFrame, std::memory_order_relaxed);
    }

#ifndef NDEBUG
    // Interval stats logging - zero overhead in release (entire block compiles
    // out)
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
  auto config = Behaviors::getDefaultConfig(behaviorType);

  auto& edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex == SIZE_MAX) {
    AI_ERROR("Cannot assign behavior: entity not in EDM");
    return;
  }

  // Set config in EDM and initialize state
  edm.setBehaviorConfig(edmIndex, config);
  Behaviors::init(edmIndex, config);

  // Now acquire write lock for storage modification
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_handleToIndex.find(handle);
  if (indexIt != m_handleToIndex.end()) {
    // Update existing entity
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
      // Update active state
      if (!m_storage.hotData[index].active) {
        m_storage.hotData[index].active = true;
      }

      if (index < m_storage.edmIndices.size()) {
        m_storage.edmIndices[index] = edmIndex;
      }

      // Update EDM-to-storage reverse mapping for O(1) lookup in processBatch
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

    // Add to hot data
    AIEntityData::HotData hotData{};
    hotData.active = true;

    m_storage.hotData.push_back(hotData);
    m_storage.handles.push_back(handle);
    m_storage.lastUpdateTimes.push_back(0.0f);
    m_storage.edmIndices.push_back(edmIndex);

    // Populate EDM-to-storage reverse mapping for O(1) lookup in processBatch
    if (m_edmToStorageIndex.size() <= edmIndex) {
      m_edmToStorageIndex.resize(edmIndex + 1, SIZE_MAX);
    }
    m_edmToStorageIndex[edmIndex] = newIndex;

    // Update index map
    m_handleToIndex[handle] = newIndex;

    AI_INFO(std::format("Added new entity with behavior: {}", behaviorName));
  }

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

  // Set config in EDM and initialize state
  edm.setBehaviorConfig(edmIndex, config);
  Behaviors::init(edmIndex, config);

  // Now acquire write lock for storage modification
  std::unique_lock<std::shared_mutex> lock(m_entitiesMutex);

  auto indexIt = m_handleToIndex.find(handle);
  if (indexIt != m_handleToIndex.end()) {
    // Update existing entity
    size_t index = indexIt->second;
    if (index < m_storage.size()) {
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

      // Clear behavior config in EDM
      size_t edmIndex = m_storage.edmIndices[index];
      if (edmIndex != SIZE_MAX) {
        auto& edm = EntityDataManager::Instance();
        edm.setBehaviorConfig(edmIndex, HammerEngine::BehaviorConfigData{});

        // Clear from EDM-to-storage reverse mapping
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

  // Mark as inactive in main storage
  auto it = m_handleToIndex.find(handle);
  if (it != m_handleToIndex.end() && it->second < m_storage.size()) {
    // Mark as inactive
    m_storage.hotData[it->second].active = false;
  }
}

// Thread safety: m_activeIndicesBuffer and m_cachedPlayerEdmIdx are written on
// the main thread before batch futures are submitted. Futures create a
// happens-before edge, so worker threads read consistent data without a lock.
void AIManager::queryEdmIndicesInRadius(const Vector2D &center, float radius,
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

void AIManager::queryHandlesInRadius(const Vector2D &center, float radius,
                                     std::vector<EntityHandle> &outHandles,
                                     bool excludePlayer) const {
  outHandles.clear();

  // Delegate to spatial grid via edmIndex query
  thread_local std::vector<size_t> t_edmBuffer;
  queryEdmIndicesInRadius(center, radius, t_edmBuffer, excludePlayer);

  auto &edm = EntityDataManager::Instance();
  outHandles.reserve(t_edmBuffer.size());
  for (size_t edmIdx : t_edmBuffer) {
    outHandles.push_back(edm.getHandle(edmIdx));
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

  // Clear all data (behaviors are data in EDM, no cleanup needed)
  m_storage.hotData.clear();
  m_storage.handles.clear();
  m_storage.lastUpdateTimes.clear();
  m_storage.edmIndices.clear();
  m_handleToIndex.clear();
  m_edmToStorageIndex.clear(); // Clear EDM-to-storage reverse mapping

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
  std::shared_lock<std::shared_mutex> lock(m_behaviorsMutex);
  return m_behaviorTypeMap.size();  // Number of registered behavior types
}

size_t AIManager::getBehaviorUpdateCount() const {
  return m_totalBehaviorExecutions.load(std::memory_order_relaxed);
}

size_t AIManager::getTotalAssignmentCount() const {
  return m_totalAssignmentCount.load(std::memory_order_relaxed);
}

void AIManager::sendMessageToEntity(EntityHandle handle,
                                    const std::string &message,
                                    bool /*immediate*/) {
  // Data-oriented: Messages are queued for processing via EDM memory data
  // Behaviors can check memory data during execution
  if (!handle.isValid() || message.empty())
    return;

  // Use lock-free queue for messages
  size_t pending = m_messageWriteIndex.load(std::memory_order_relaxed) -
                   m_messageReadIndex.load(std::memory_order_relaxed);
  if (pending >= MESSAGE_QUEUE_SIZE) {
    return; // Queue full - silently drop
  }

  size_t writeIndex =
      m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
      MESSAGE_QUEUE_SIZE;
  auto &msg = m_lockFreeMessages[writeIndex];

  msg.target = handle;
  size_t copyLen = std::min(message.length(), sizeof(msg.message) - 1);
  message.copy(msg.message, copyLen);
  msg.message[copyLen] = '\0';
  msg.ready.store(true, std::memory_order_release);
}

void AIManager::broadcastMessage(const std::string &message,
                                 bool /*immediate*/) {
  // Data-oriented: Broadcast messages are queued
  if (message.empty())
    return;

  size_t pending = m_messageWriteIndex.load(std::memory_order_relaxed) -
                   m_messageReadIndex.load(std::memory_order_relaxed);
  if (pending >= MESSAGE_QUEUE_SIZE) {
    return; // Queue full - silently drop
  }

  size_t writeIndex =
      m_messageWriteIndex.fetch_add(1, std::memory_order_relaxed) %
      MESSAGE_QUEUE_SIZE;
  auto &msg = m_lockFreeMessages[writeIndex];

  msg.target = EntityHandle{}; // Invalid handle for broadcast
  size_t copyLen = std::min(message.length(), sizeof(msg.message) - 1);
  message.copy(msg.message, copyLen);
  msg.message[copyLen] = '\0';
  msg.ready.store(true, std::memory_order_release);
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
    Vector2D clamped(std::clamp(pos.getX(), halfW, worldWidth - halfW),
                     std::clamp(pos.getY(), halfH, worldHeight - halfH));
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
      minX[lane] = hotData->halfWidth;
      maxX[lane] = worldWidth - hotData->halfWidth;
      minY[lane] = hotData->halfHeight;
      maxY[lane] = worldHeight - hotData->halfHeight;
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
      // contention-free state access Use cached handle from storage to avoid
      // redundant getHandle() call (3 vector accesses) World bounds: minX=0,
      // minY=0, maxX=worldWidth, maxY=worldHeight
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
  Behaviors::collectDeferredMessageEvents(deferredEvents);
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
