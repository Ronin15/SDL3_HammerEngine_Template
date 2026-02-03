/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/EventManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/CameraEvent.hpp"
#include "events/CollisionEvent.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"
#include "events/EntityEvents.hpp"
#include "events/Event.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "events/WorldEvent.hpp"
#include "events/WorldTriggerEvent.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <numeric>

// ==================== Core Singleton & Lifecycle ====================

EventManager &EventManager::Instance() {
  static EventManager instance;
  return instance;
}

EventManager::EventManager() {
  // Pre-allocate handler vectors to avoid reallocation during registration
  for (auto &handlerVec : m_handlersByType) {
    handlerVec.reserve(16);
  }
}

EventManager::~EventManager() {
  if (!m_isShutdown) {
    clean();
  }
}

bool EventManager::isInitialized() const {
  return m_initialized.load(std::memory_order_acquire);
}

bool EventManager::isShutdown() const { return m_isShutdown; }

bool EventManager::init() {
  if (m_initialized.load()) {
    EVENT_WARN("EventManager already initialized");
    return true;
  }

  // Reset shutdown flag to allow re-initialization after clean()
  m_isShutdown = false;

  EVENT_INFO("Initializing EventManager (dispatch-only architecture)");

  // Initialize handler containers
  for (auto &handlerContainer : m_handlersByType) {
    handlerContainer.clear();
    constexpr size_t HANDLER_CONTAINER_CAPACITY = 32;
    handlerContainer.reserve(HANDLER_CONTAINER_CAPACITY);
  }

  // Clear event pools
  clearEventPools();

  // Clear pending dispatch queue
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    m_pendingDispatch.clear();
  }

  // Configure event pools for trigger methods
  m_weatherPool.setCreator([]() {
    return std::make_shared<WeatherEvent>("trigger_weather", WeatherType::Clear);
  });
  m_npcSpawnPool.setCreator([]() {
    return std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", SpawnParameters{});
  });
  m_resourceChangePool.setCreator([]() {
    return std::make_shared<ResourceChangeEvent>(
        EntityHandle{}, HammerEngine::ResourceHandle{}, 0, 0, "");
  });

  // Hot-path event pools
  m_collisionPool.setCreator([]() {
    HammerEngine::CollisionInfo emptyInfo{};
    return std::make_shared<CollisionEvent>(emptyInfo);
  });
  m_particleEffectPool.setCreator([]() {
    return std::make_shared<ParticleEffectEvent>("pool_particle",
                                                 ParticleEffectType::Fire, 0.0f,
                                                 0.0f, 1.0f, -1.0f, "", "");
  });
  m_collisionObstacleChangedPool.setCreator([]() {
    return std::make_shared<CollisionObstacleChangedEvent>(
        CollisionObstacleChangedEvent::ChangeType::MODIFIED, Vector2D(0, 0),
        64.0f, "");
  });
  m_damagePool.setCreator([]() {
    return std::make_shared<DamageEvent>();
  });

  // Reset performance stats
  resetPerformanceStats();

  m_lastUpdateTime.store(getCurrentTimeNanos());
  m_initialized.store(true);

  // Register internal handler for NPCSpawn events
  registerHandler(EventTypeId::NPCSpawn, [](const EventData &data) {
    if (!data.isActive() || !data.event) return;
    auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(data.event);
    if (npcEvent) {
      npcEvent->execute();
    }
  });

  EVENT_INFO("EventManager initialized successfully");
  return true;
}

void EventManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return;
  }

  // Set shutdown flags EARLY to prevent new work
  m_isShutdown = true;
  m_initialized.store(false, std::memory_order_release);

  // Wait for any pending async batches to complete before cleanup
  {
    std::vector<std::future<void>> localFutures;
    {
      std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
      localFutures = std::move(m_batchFutures);
    }

    for (auto &future : localFutures) {
      if (future.valid()) {
        future.wait();
      }
    }
  }

  EVENT_INFO_IF(!m_isShutdown, "Cleaning up EventManager");

  // Clear all handlers
  clearAllHandlers();

  // Clear event pools
  clearEventPools();

  // Clear pending dispatch queue
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    m_pendingDispatch.clear();
  }

  // Reset performance stats
  resetPerformanceStats();
}

void EventManager::prepareForStateTransition() {
  EVENT_INFO("Preparing EventManager for state transition...");

  // Wait for any pending async batches
  {
    std::vector<std::future<void>> localFutures;
    {
      std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
      localFutures = std::move(m_batchFutures);
    }

    for (auto &future : localFutures) {
      if (future.valid()) {
        future.wait();
      }
    }
  }

  // Clear all handlers
  clearAllHandlers();

  // Clear event pools
  clearEventPools();

  // Clear pending dispatch queue
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    m_pendingDispatch.clear();
  }

  // Reset performance stats
  resetPerformanceStats();

  EVENT_INFO("EventManager prepared for state transition");
}

// ==================== Update (Dispatch Queue Processing) ====================

void EventManager::update() {
  if (!m_initialized.load() || m_isShutdown) {
    return;
  }

  // Skip update when globally paused
  if (m_globallyPaused.load(std::memory_order_acquire)) {
    return;
  }

  // Process the deferred dispatch queue
  drainDispatchQueueWithBudget();
}

void EventManager::drainAllDeferredEvents() {
  if (!m_initialized.load()) {
    return;
  }

  // Process until queue is empty (max 100 iterations for safety)
  constexpr int MAX_ITERATIONS = 100;
  for (int i = 0; i < MAX_ITERATIONS; ++i) {
    bool queueEmpty = false;
    {
      std::lock_guard<std::mutex> lock(m_dispatchMutex);
      queueEmpty = m_pendingDispatch.empty();
    }

    if (queueEmpty) {
      break;
    }

    update();
  }
}

// ==================== Threading & Pause Controls ====================

#ifndef NDEBUG
void EventManager::enableThreading(bool enable) {
  m_threadingEnabled.store(enable);
}

bool EventManager::isThreadingEnabled() const {
  return m_threadingEnabled.load();
}
#endif

void EventManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
}

bool EventManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

// ==================== Handler Registration ====================

void EventManager::registerHandler(EventTypeId typeId, FastEventHandler handler) {
  (void)registerHandlerWithToken(typeId, std::move(handler));
}

EventManager::HandlerToken
EventManager::registerHandlerWithToken(EventTypeId typeId, FastEventHandler handler) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  const size_t idx = static_cast<size_t>(typeId);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);

  m_handlersByType[idx].emplace_back(std::move(handler), id);
  return HandlerToken{typeId, id, false, {}};
}

EventManager::HandlerToken
EventManager::registerHandlerForName(const std::string &name, FastEventHandler handler) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);

  m_nameHandlers[name].emplace_back(std::move(handler), id);
  return HandlerToken{EventTypeId::Custom, id, true, name};
}

bool EventManager::removeHandler(const HandlerToken &token) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  if (token.forName) {
    auto it = m_nameHandlers.find(token.name);
    if (it == m_nameHandlers.end())
      return false;

    auto &entries = it->second;
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].id == token.id) {
        // Swap-and-pop: O(1) removal without leaving holes
        if (i != entries.size() - 1) {
          entries[i] = std::move(entries.back());
        }
        entries.pop_back();
        return true;
      }
    }
    return false;
  } else {
    const size_t idx = static_cast<size_t>(token.typeId);
    if (idx >= m_handlersByType.size())
      return false;

    auto &entries = m_handlersByType[idx];
    auto it = std::find_if(
        entries.begin(), entries.end(),
        [&token](const HandlerEntry &entry) { return entry.id == token.id; });
    if (it != entries.end()) {
      // Swap-and-pop: O(1) removal without leaving holes
      if (it != entries.end() - 1) {
        *it = std::move(entries.back());
      }
      entries.pop_back();
      return true;
    }
    return false;
  }
}

void EventManager::removeHandlers(EventTypeId typeId) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  const size_t idx = static_cast<size_t>(typeId);
  m_handlersByType[idx].clear();
}

void EventManager::clearAllHandlers() {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  for (size_t i = 0; i < m_handlersByType.size(); ++i) {
    m_handlersByType[i].clear();
  }
  m_nameHandlers.clear();
  EVENT_INFO("All event handlers cleared");
}

size_t EventManager::getHandlerCount(EventTypeId typeId) const {
  std::shared_lock<std::shared_mutex> lock(m_handlersMutex);
  return m_handlersByType[static_cast<size_t>(typeId)].size();
}

void EventManager::removeNameHandlers(const std::string &name) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  m_nameHandlers.erase(name);
}

// ==================== Trigger Methods ====================

bool EventManager::triggerWeatherChange(const std::string &weatherType,
                                        float transitionTime) const {
  return changeWeather(weatherType, transitionTime);
}

bool EventManager::triggerNPCSpawn(const std::string &npcType, float x,
                                   float y, const std::string &npcRace) const {
  return spawnNPC(npcType, x, y, 1, 0.0f, npcRace);
}

bool EventManager::changeWeather(const std::string &weatherType,
                                 float transitionTime, DispatchMode mode) const {
  std::shared_ptr<WeatherEvent> weatherEvent = m_weatherPool.acquire();
  if (!weatherEvent)
    weatherEvent = std::make_shared<WeatherEvent>("trigger_weather", weatherType);
  else
    weatherEvent->setWeatherType(weatherType);

  WeatherParams params = weatherEvent->getWeatherParams();
  params.transitionTime = transitionTime;
  weatherEvent->setWeatherParams(params);

  EventData data;
  data.typeId = EventTypeId::Weather;
  data.setActive(true);
  data.event = weatherEvent;

  return dispatchEvent(EventTypeId::Weather, data, mode, "changeWeather");
}

bool EventManager::spawnNPC(const std::string &npcType, float x, float y,
                            int count, float spawnRadius,
                            const std::string &npcRace,
                            const std::vector<std::string> &aiBehaviors,
                            bool worldWide, DispatchMode mode) const {
  SpawnParameters params(npcType, count, spawnRadius, npcRace);
  params.aiBehaviors = aiBehaviors;
  params.worldWide = worldWide;

  auto npcEvent = m_npcSpawnPool.acquire();
  if (!npcEvent)
    npcEvent = std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", params);
  else
    npcEvent->setSpawnParameters(params);
  npcEvent->addSpawnPoint(x, y);

  EventData data;
  data.typeId = EventTypeId::NPCSpawn;
  data.setActive(true);
  data.event = npcEvent;

  return dispatchEvent(EventTypeId::NPCSpawn, data, mode, "spawnNPC");
}

bool EventManager::triggerParticleEffect(const std::string &effectName, float x,
                                         float y, float intensity,
                                         float duration,
                                         const std::string &groupTag,
                                         DispatchMode mode) const {
  ParticleEffectType effectType = ParticleEffectEvent::stringToEffectType(effectName);

  auto pe = m_particleEffectPool.acquire();
  if (pe) {
    pe->setEffectType(effectType);
    pe->setPosition(x, y);
    pe->setIntensity(intensity);
    pe->setDuration(duration);
    pe->setGroupTag(groupTag);
  } else {
    pe = std::make_shared<ParticleEffectEvent>("trigger_particle_effect",
                                               effectType, x, y, intensity,
                                               duration, groupTag, "");
  }

  EventData data;
  data.typeId = EventTypeId::ParticleEffect;
  data.setActive(true);
  data.event = pe;

  return dispatchEvent(EventTypeId::ParticleEffect, data, mode, "triggerParticleEffect");
}

bool EventManager::triggerParticleEffect(const std::string &effectName,
                                         const Vector2D &position,
                                         float intensity, float duration,
                                         const std::string &groupTag,
                                         DispatchMode mode) const {
  return triggerParticleEffect(effectName, position.getX(), position.getY(),
                               intensity, duration, groupTag, mode);
}

bool EventManager::triggerResourceChange(
    EntityHandle ownerHandle, HammerEngine::ResourceHandle resourceHandle,
    int oldQuantity, int newQuantity, const std::string &changeReason,
    DispatchMode mode) const {
  auto resourceEvent = m_resourceChangePool.acquire();
  if (resourceEvent) {
    resourceEvent->set(ownerHandle, resourceHandle, oldQuantity, newQuantity,
                       changeReason);
  } else {
    resourceEvent = std::make_shared<ResourceChangeEvent>(
        ownerHandle, resourceHandle, oldQuantity, newQuantity, changeReason);
  }

  EventData eventData;
  eventData.typeId = EventTypeId::ResourceChange;
  eventData.setActive(true);
  eventData.event = resourceEvent;

  return dispatchEvent(EventTypeId::ResourceChange, eventData, mode,
                       "triggerResourceChange");
}

bool EventManager::triggerCollision(const HammerEngine::CollisionInfo &info,
                                    DispatchMode mode) const {
  auto collisionEvent = m_collisionPool.acquire();
  if (collisionEvent) {
    collisionEvent->setInfo(info);
    collisionEvent->setConsumed(false);
  } else {
    collisionEvent = std::make_shared<CollisionEvent>(info);
  }

  EventData eventData;
  eventData.typeId = EventTypeId::Collision;
  eventData.setActive(true);
  eventData.priority = EventPriority::CRITICAL;
  eventData.event = collisionEvent;

  return dispatchEvent(EventTypeId::Collision, eventData, mode, "triggerCollision");
}

bool EventManager::triggerWorldTrigger(const WorldTriggerEvent &event,
                                       DispatchMode mode) const {
  EventData eventData;
  eventData.typeId = EventTypeId::WorldTrigger;
  eventData.setActive(true);
  eventData.event = std::make_shared<WorldTriggerEvent>(event);

  return dispatchEvent(EventTypeId::WorldTrigger, eventData, mode, "triggerWorldTrigger");
}

bool EventManager::triggerCollisionObstacleChanged(
    const Vector2D &position, float radius, const std::string &description,
    DispatchMode mode) const {
  auto obstacleEvent = m_collisionObstacleChangedPool.acquire();
  if (obstacleEvent) {
    obstacleEvent->configure(
        CollisionObstacleChangedEvent::ChangeType::MODIFIED, position, radius,
        description);
  } else {
    obstacleEvent = std::make_shared<CollisionObstacleChangedEvent>(
        CollisionObstacleChangedEvent::ChangeType::MODIFIED, position, radius,
        description);
  }

  EventData eventData;
  eventData.typeId = EventTypeId::CollisionObstacleChanged;
  eventData.setActive(true);
  eventData.priority = EventPriority::CRITICAL;
  eventData.event = obstacleEvent;

  return dispatchEvent(EventTypeId::CollisionObstacleChanged, eventData, mode,
                       "triggerCollisionObstacleChanged");
}

// ==================== Combat Triggers ====================

bool EventManager::triggerDamage(DispatchMode mode) const {
  // TODO: Add parameters and configure event when combat is designed
  auto damageEvent = m_damagePool.acquire();
  if (!damageEvent) {
    damageEvent = std::make_shared<DamageEvent>();
  }

  EventData eventData;
  eventData.typeId = EventTypeId::Combat;
  eventData.setActive(true);
  eventData.priority = EventPriority::HIGH;
  eventData.event = damageEvent;

  return dispatchEvent(EventTypeId::Combat, eventData, mode, "triggerDamage");
}

// World triggers
bool EventManager::triggerWorldLoaded(const std::string &worldId, int width,
                                      int height, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldLoadedEvent>(worldId, width, height);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldLoaded");
}

bool EventManager::triggerWorldUnloaded(const std::string &worldId,
                                        DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldUnloadedEvent>(worldId);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldUnloaded");
}

bool EventManager::triggerTileChanged(int x, int y,
                                      const std::string &changeType,
                                      DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<TileChangedEvent>(x, y, changeType);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerTileChanged");
}

bool EventManager::triggerWorldGenerated(const std::string &worldId, int width,
                                         int height, float generationTime,
                                         DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldGeneratedEvent>(worldId, width, height,
                                                     generationTime);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldGenerated");
}

bool EventManager::triggerStaticCollidersReady(size_t solidBodyCount,
                                               size_t triggerCount,
                                               DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<StaticCollidersReadyEvent>(solidBodyCount, triggerCount);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerStaticCollidersReady");
}

// Camera triggers
bool EventManager::triggerCameraMoved(const Vector2D &newPos,
                                      const Vector2D &oldPos,
                                      DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraMovedEvent>(newPos, oldPos);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraMoved");
}

bool EventManager::triggerCameraModeChanged(int newMode, int oldMode,
                                            DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraModeChangedEvent>(
      static_cast<CameraModeChangedEvent::Mode>(newMode),
      static_cast<CameraModeChangedEvent::Mode>(oldMode));
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraModeChanged");
}

bool EventManager::triggerCameraShakeStarted(float duration, float intensity,
                                             DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraShakeStartedEvent>(duration, intensity);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraShakeStarted");
}

bool EventManager::triggerCameraShakeEnded(DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraShakeEndedEvent>();
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraShakeEnded");
}

bool EventManager::triggerCameraTargetChanged(const std::weak_ptr<Entity>& newTarget,
                                              const std::weak_ptr<Entity>& oldTarget,
                                              DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraTargetChangedEvent>(newTarget, oldTarget);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraTargetChanged");
}

bool EventManager::triggerCameraZoomChanged(float newZoom, float oldZoom,
                                            DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraZoomChangedEvent>(newZoom, oldZoom);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraZoomChanged");
}

// Public dispatch for EventPtr
bool EventManager::dispatchEvent(const EventPtr& event, DispatchMode mode) const {
  if (!event) {
    EVENT_ERROR("dispatchEvent called with null event");
    return false;
  }

  EventTypeId typeId = event->getTypeId();
  EventData data;
  data.typeId = typeId;
  data.setActive(true);
  data.event = event;

  return dispatchEvent(typeId, data, mode, "dispatchEvent(EventPtr)");
}

// ==================== Internal Dispatch ====================

bool EventManager::dispatchEvent(EventTypeId typeId, EventData &eventData,
                                 DispatchMode mode, const char *errorContext) const {
  if (mode == DispatchMode::Immediate) {
    std::shared_lock<std::shared_mutex> lock(m_handlersMutex);

    const auto &typeHandlers = m_handlersByType[static_cast<size_t>(typeId)];

    if (typeHandlers.empty()) {
      return false;
    }

    for (const auto &entry : typeHandlers) {
      if (entry) {
        try {
          entry.callable(eventData);
        } catch (const std::exception &e) {
          EVENT_ERROR(std::format("Handler exception in {}: {}", errorContext, e.what()));
        } catch (...) {
          EVENT_ERROR(std::format("Unknown handler exception in {}", errorContext));
        }
      }
    }

    releaseEventToPool(typeId, eventData.event);
    return true;
  }

  // Deferred dispatch
  enqueueDispatch(typeId, eventData);
  return true;
}

void EventManager::enqueueDispatch(EventTypeId typeId, const EventData &data) const {
  std::lock_guard<std::mutex> lock(m_dispatchMutex);
  if (m_pendingDispatch.size() >= m_maxDispatchQueue) {
    m_pendingDispatch.pop_front();
  }
  m_pendingDispatch.push_back(PendingDispatch{typeId, data});
}

void EventManager::enqueueBatch(std::vector<DeferredEvent>&& events) const {
  if (events.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_dispatchMutex);

  // Drop oldest events if we'd exceed the queue limit
  while (m_pendingDispatch.size() + events.size() > m_maxDispatchQueue &&
         !m_pendingDispatch.empty()) {
    m_pendingDispatch.pop_front();
  }

  // Bulk insert all events
  for (auto& event : events) {
    m_pendingDispatch.push_back(PendingDispatch{event.typeId, std::move(event.data)});
  }
}

void EventManager::processBatchSingleThreaded(size_t start, size_t end) const {
  // No lock needed - handlers only registered during state transitions,
  // not during EventManager's update window

  for (size_t i = start; i < end; ++i) {
    const auto &pd = m_localDispatchBuffer[i];
    const auto &typeHandlers = m_handlersByType[static_cast<size_t>(pd.typeId)];

    for (const auto &entry : typeHandlers) {
      if (entry) {
        try {
          entry.callable(pd.data);
        } catch (const std::exception &e) {
          EVENT_ERROR(std::format("Handler exception in dispatch batch: {}", e.what()));
        } catch (...) {
          EVENT_ERROR("Unknown handler exception in dispatch batch");
        }
      }
    }
  }
}

void EventManager::drainDispatchQueueWithBudget() {
  auto startTime = std::chrono::high_resolution_clock::now();

  // Extract all pending events - no lock needed, EventManager has sole access
  // during its update window (game loop guarantees sequential manager updates)
  const size_t pendingCount = m_pendingDispatch.size();
  if (pendingCount == 0) {
    return;
  }

  m_localDispatchBuffer.clear();
  m_localDispatchBuffer.reserve(pendingCount);
  for (size_t i = 0; i < pendingCount; ++i) {
    m_localDispatchBuffer.push_back(std::move(m_pendingDispatch.front()));
    m_pendingDispatch.pop_front();
  }

  const size_t eventCount = m_localDispatchBuffer.size();

  // Sort by priority (higher priority first)
  std::sort(m_localDispatchBuffer.begin(), m_localDispatchBuffer.end(),
            [](const PendingDispatch &a, const PendingDispatch &b) {
              return a.data.priority > b.data.priority;
            });

  // Query WorkerBudget for threading decision
  auto &budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
  auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Event, eventCount);

  // Track what actually happened
  bool actualWasThreaded = false;
  size_t actualBatchCount = 1;

  if (decision.shouldThread) {
    auto &threadSystem = HammerEngine::ThreadSystem::Instance();

    // Get optimal worker count and batch strategy
    size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::Event, eventCount);
    auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::Event, eventCount, optimalWorkerCount);

    // Single batch optimization: avoid thread overhead
    if (batchCount <= 1) {
      actualWasThreaded = false;
      actualBatchCount = 1;
      processBatchSingleThreaded(0, eventCount);
    } else {
      // MULTI-THREADED PATH
      actualWasThreaded = true;
      actualBatchCount = batchCount;

      m_batchFutures.clear();
      m_batchFutures.reserve(batchCount);

      size_t eventsPerBatch = eventCount / batchCount;
      size_t remainingEvents = eventCount % batchCount;

      for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * eventsPerBatch;
        size_t end = start + eventsPerBatch;

        // Add remaining events to last batch
        if (i == batchCount - 1) {
          end += remainingEvents;
        }

        m_batchFutures.push_back(threadSystem.enqueueTaskWithResult(
            [this, start, end]() {
              try {
                processBatchSingleThreaded(start, end);
              } catch (const std::exception &e) {
                EVENT_ERROR(std::format("Exception in event batch: {}", e.what()));
              } catch (...) {
                EVENT_ERROR("Unknown exception in event batch");
              }
            },
            HammerEngine::TaskPriority::Normal, "Event_Dispatch_Batch"));
      }
    }
  } else {
    // SINGLE-THREADED PATH (WorkerBudget decision)
    actualWasThreaded = false;
    actualBatchCount = 1;
    processBatchSingleThreaded(0, eventCount);
  }

  // Wait for all batches to complete
  for (auto &future : m_batchFutures) {
    if (future.valid()) {
      future.get();
    }
  }

  // Measure completion time for adaptive tuning
  auto endTime = std::chrono::high_resolution_clock::now();
  double totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

  // Report results for unified adaptive tuning
  if (eventCount > 0) {
    budgetMgr.reportExecution(HammerEngine::SystemType::Event,
                              eventCount, actualWasThreaded, actualBatchCount, totalTimeMs);
  }

#ifndef NDEBUG
  // Periodic debug logging (~35 seconds at 60fps)
  static thread_local uint64_t logFrameCounter = 0;
  if (++logFrameCounter % 2100 == 0 && eventCount > 0) {
    EVENT_DEBUG(std::format("Dispatch: {} events, {} [{} batches, {:.2f}ms]",
                            eventCount,
                            actualWasThreaded ? "multi-threaded" : "single-threaded",
                            actualBatchCount, totalTimeMs));
  }
#endif

  // Release pooled events back to pools (after all processing complete)
  for (const auto &pd : m_localDispatchBuffer) {
    releaseEventToPool(pd.typeId, pd.data.event);
  }
}

void EventManager::releaseEventToPool(EventTypeId typeId, const EventPtr& event) const {
  if (!event) return;

  switch (typeId) {
    case EventTypeId::Weather:
      if (auto we = std::dynamic_pointer_cast<WeatherEvent>(event)) {
        m_weatherPool.release(we);
      }
      break;
    case EventTypeId::NPCSpawn:
      if (auto nse = std::dynamic_pointer_cast<NPCSpawnEvent>(event)) {
        m_npcSpawnPool.release(nse);
      }
      break;
    case EventTypeId::ResourceChange:
      if (auto rce = std::dynamic_pointer_cast<ResourceChangeEvent>(event)) {
        m_resourceChangePool.release(rce);
      }
      break;
    case EventTypeId::World:
      if (auto we = std::dynamic_pointer_cast<WorldEvent>(event)) {
        m_worldPool.release(we);
      }
      break;
    case EventTypeId::Camera:
      if (auto ce = std::dynamic_pointer_cast<CameraEvent>(event)) {
        m_cameraPool.release(ce);
      }
      break;
    case EventTypeId::Collision:
      if (auto ce = std::dynamic_pointer_cast<CollisionEvent>(event)) {
        m_collisionPool.release(ce);
      }
      break;
    case EventTypeId::ParticleEffect:
      if (auto pe = std::dynamic_pointer_cast<ParticleEffectEvent>(event)) {
        m_particleEffectPool.release(pe);
      }
      break;
    case EventTypeId::Combat:
      if (auto de = std::dynamic_pointer_cast<DamageEvent>(event)) {
        m_damagePool.release(de);
      }
      break;
    case EventTypeId::CollisionObstacleChanged:
      if (auto coce = std::dynamic_pointer_cast<CollisionObstacleChangedEvent>(event)) {
        m_collisionObstacleChangedPool.release(coce);
      }
      break;
    default:
      // Non-pooled event types
      break;
  }
}

// ==================== Performance & Diagnostics ====================

PerformanceStats EventManager::getPerformanceStats(EventTypeId typeId) const {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  return m_performanceStats[static_cast<size_t>(typeId)];
}

void EventManager::resetPerformanceStats() const {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  for (auto &stats : m_performanceStats) {
    stats.reset();
  }
}

size_t EventManager::getPendingEventCount() const {
  std::lock_guard<std::mutex> lock(m_dispatchMutex);
  return m_pendingDispatch.size();
}

void EventManager::clearEventPools() {
  m_weatherPool.clear();
  m_npcSpawnPool.clear();
  m_resourceChangePool.clear();
  m_worldPool.clear();
  m_cameraPool.clear();
  m_collisionPool.clear();
  m_particleEffectPool.clear();
  m_collisionObstacleChangedPool.clear();
  m_damagePool.clear();
}

// ==================== Helper Methods ====================

std::string EventManager::getEventTypeName(EventTypeId typeId) const {
  switch (typeId) {
  case EventTypeId::Weather: return "Weather";
  case EventTypeId::NPCSpawn: return "NPCSpawn";
  case EventTypeId::ParticleEffect: return "ParticleEffect";
  case EventTypeId::ResourceChange: return "ResourceChange";
  case EventTypeId::World: return "World";
  case EventTypeId::Camera: return "Camera";
  case EventTypeId::Harvest: return "Harvest";
  case EventTypeId::Collision: return "Collision";
  case EventTypeId::WorldTrigger: return "WorldTrigger";
  case EventTypeId::CollisionObstacleChanged: return "CollisionObstacleChanged";
  case EventTypeId::Custom: return "Custom";
  case EventTypeId::Time: return "Time";
  case EventTypeId::Combat: return "Combat";
  case EventTypeId::Entity: return "Entity";
  default: return "Unknown";
  }
}

void EventManager::recordPerformance(EventTypeId typeId, double timeMs) const {
  static thread_local uint64_t updateCounter = 0;
  updateCounter++;

  if (updateCounter % 10 == 0 || timeMs > 5.0) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    m_performanceStats[static_cast<size_t>(typeId)].addSample(timeMs);
  }
}

uint64_t EventManager::getCurrentTimeNanos() const {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}
