/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/EventManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/Event.hpp"
#include "events/EventFactory.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/ParticleEffectEvent.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/WeatherEvent.hpp"
#include "events/WorldEvent.hpp"
#include "events/CameraEvent.hpp"
#include "events/CollisionEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <numeric>
#include <unordered_set>

// ----------------------
// EventData definitions
// ----------------------
// EventData small accessors are inline in the header per policy.

// ---------------------------
// EventManager core helpers
// ---------------------------
EventManager &EventManager::Instance()
{
  static EventManager instance;
  return instance;
}

// OPTIMIZATION: Pre-allocate handler vectors to avoid reallocation during registration
EventManager::EventManager() {
  // Reserve capacity for each event type's handler vector
  // Typical games have 8-16 handlers per frequently-used event type
  for (auto& handlerVec : m_handlersByType) {
    handlerVec.reserve(16);
  }
}

EventManager::~EventManager()
{
  if (!m_isShutdown) {
    clean();
  }
}

bool EventManager::isInitialized() const
{
  return m_initialized.load(std::memory_order_acquire);
}

bool EventManager::isShutdown() const
{
  return m_isShutdown;
}

void EventManager::enableThreading(bool enable)
{
  m_threadingEnabled.store(enable);
}

bool EventManager::isThreadingEnabled() const
{
  return m_threadingEnabled.load();
}

void EventManager::setThreadingThreshold(size_t threshold)
{
  m_threadingThreshold = threshold;
}

size_t EventManager::getThreadingThreshold() const
{
  return m_threadingThreshold;
}

// --------------------------------------------
// Convenience alias trigger method definitions
// --------------------------------------------
bool EventManager::triggerWeatherChange(const std::string &weatherType,
                                        float transitionTime) const
{
  return changeWeather(weatherType, transitionTime);
}

bool EventManager::triggerSceneChange(const std::string &sceneId,
                                      const std::string &transitionType,
                                      float transitionTime) const
{
  return changeScene(sceneId, transitionType, transitionTime);
}

bool EventManager::triggerNPCSpawn(const std::string &npcType, float x,
                                   float y) const
{
  return spawnNPC(npcType, x, y);
}

bool EventManager::init() {
  if (m_initialized.load()) {
    EVENT_WARN("EventManager already initialized");
    return true;
  }

  // Reset shutdown flag to allow re-initialization after clean()
  m_isShutdown = false;

  EVENT_INFO("Initializing EventManager with performance optimizations");

  // Initialize all event type containers
  for (auto &eventContainer : m_eventsByType) {
    eventContainer.clear();
    constexpr size_t EVENT_CONTAINER_CAPACITY = 256;
    eventContainer.reserve(
        EVENT_CONTAINER_CAPACITY); // Pre-allocate for performance
  }

  // Initialize handler containers
  for (auto &handlerContainer : m_handlersByType) {
    handlerContainer.clear();
    constexpr size_t HANDLER_CONTAINER_CAPACITY = 32;
    handlerContainer.reserve(HANDLER_CONTAINER_CAPACITY);
  }

  // Clear name mappings
  m_nameToIndex.clear();
  m_nameToType.clear();

  // Clear event pools
  clearEventPools();

  // Clear pending dispatch queue
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    m_pendingDispatch.clear();
  }

  // Configure event pools for common ephemeral triggers
  m_weatherPool.setCreator([](){ return std::make_shared<WeatherEvent>("trigger_weather", WeatherType::Clear); });
  m_sceneChangePool.setCreator([](){ return std::make_shared<SceneChangeEvent>("trigger_scene_change", ""); });
  m_npcSpawnPool.setCreator([](){ return std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", SpawnParameters{}); });

  // Reset performance stats
  resetPerformanceStats();

  m_lastUpdateTime.store(getCurrentTimeNanos());
  m_initialized.store(true);

  EVENT_INFO("EventManager initialized successfully with type-indexed storage");
  return true;
}

void EventManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return;
  }

  // Set shutdown flags EARLY to prevent new work (matches AIManager pattern)
  m_isShutdown = true;
  m_initialized.store(false, std::memory_order_release);

  // Wait for any pending async batches to complete before cleanup
  {
    std::vector<std::future<void>> localFutures;
    {
      std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
      localFutures = std::move(m_batchFutures);
    }

    // Wait for all batch futures to complete
    for (auto& future : localFutures) {
      if (future.valid()) {
        future.wait();
      }
    }
  }

  // Only log if not in shutdown to avoid static destruction order issues
  if (!m_isShutdown) {
    EVENT_INFO("Cleaning up EventManager");
  }

  // Clear all events with proper cleanup
  {
    std::unique_lock<std::shared_mutex> lock(m_eventsMutex);
    for (auto &eventContainer : m_eventsByType) {
      // Ensure all events are properly cleaned up
      for (auto &eventData : eventContainer) {
        if (eventData.event) {
          eventData.event.reset(); // Explicit cleanup
        }
      }
      eventContainer.clear();
    }
    m_nameToIndex.clear();
    m_nameToType.clear();
  }

  // Clear all handlers
  clearAllHandlers();

  // Clear event pools with proper cleanup
  clearEventPools();

  // Clear pending dispatch queue
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    m_pendingDispatch.clear();
  }

  // Reset performance stats
  resetPerformanceStats();

  // Shutdown flags already set at beginning of clean()
  // Skip logging during shutdown to avoid static destruction order issues
}

void EventManager::prepareForStateTransition() {
  EVENT_INFO("Preparing EventManager for state transition...");

  // Clear all event handlers first to prevent callbacks during cleanup
  clearAllHandlers();

  // Clear all events
  {
    std::unique_lock<std::shared_mutex> lock(m_eventsMutex);
    for (auto &eventContainer : m_eventsByType) {
      eventContainer.clear();
    }
    m_nameToIndex.clear();
    m_nameToType.clear();
  }

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

void EventManager::update() {
  if (!m_initialized.load() || m_isShutdown) {
    return;
  }

  // NOTE: Early exit optimization removed - some tests dispatch events during update
  // that need to be processed in the same frame. Keep for now.
  // TODO: Re-evaluate this optimization with deferred event dispatch

  // NOTE: We do NOT wait for previous frame's batches here - they can overlap with current frame
  // EventManager batches don't update collision data, so frame overlap is safe
  // This allows better frame pipelining on low-core systems

  auto startTime = getCurrentTimeNanos();

  // OPTIMIZATION: Cache all event counts in a single mutex acquisition
  // This prevents excessive locking when checking per-type thresholds
  size_t totalEventCount = 0;
  std::array<size_t, static_cast<size_t>(EventTypeId::COUNT)> eventCountsByType;
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    for (size_t i = 0; i < static_cast<size_t>(EventTypeId::COUNT); ++i) {
      const auto &container = m_eventsByType[i];
      size_t count = std::count_if(
          container.begin(), container.end(), [](const EventData &eventData) {
            return !(eventData.flags & EventData::FLAG_PENDING_REMOVAL);
          });
      eventCountsByType[i] = count;
      totalEventCount += count;
    }
  }

  // Early exit if no events to process (optimization)
  if (totalEventCount == 0) {
    // Still drain dispatch queue for deferred events
    drainDispatchQueueWithBudget();
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    return;
  }

  // Update all event types in optimized batches with per-type threading decision
  // Global check: Only consider threading if total events > threshold
  bool useThreadingGlobal = m_threadingEnabled.load() && totalEventCount > m_threadingThreshold;

  // Helper lambda to decide threading per type (uses cached counts - no mutex!)
  auto updateEventType = [this, useThreadingGlobal, &eventCountsByType](EventTypeId typeId) {
    if (!useThreadingGlobal) {
      // Global threshold not met - use single-threaded for all types
      updateEventTypeBatch(typeId);
      return;
    }

    // Global threshold met - check per-type threshold using cached count
    size_t typeEventCount = eventCountsByType[static_cast<size_t>(typeId)];
    if (typeEventCount >= PER_TYPE_THREAD_THRESHOLD) {
      // This type has enough events to benefit from threading
      updateEventTypeBatchThreaded(typeId);
    } else {
      // Too few events in this type - threading overhead would hurt performance
      updateEventTypeBatch(typeId);
    }
  };

  // Process each event type with adaptive threading decision
  updateEventType(EventTypeId::Weather);
  updateEventType(EventTypeId::SceneChange);
  updateEventType(EventTypeId::NPCSpawn);
  updateEventType(EventTypeId::ParticleEffect);
  updateEventType(EventTypeId::ResourceChange);
  updateEventType(EventTypeId::World);
  updateEventType(EventTypeId::Camera);
  updateEventType(EventTypeId::Harvest);
  updateEventType(EventTypeId::Custom);
  updateEventType(EventTypeId::Collision);
  updateEventType(EventTypeId::WorldTrigger);

  // Simplified performance tracking - reduce lock contention
  // Drain deferred dispatch queue with budget after event updates
  drainDispatchQueueWithBudget();

  auto endTime = getCurrentTimeNanos();
  double totalTimeMs = (endTime - startTime) / 1000000.0;

  // Store total update time for adaptive batch tuning
  m_adaptiveBatchState.lastUpdateTimeMs.store(totalTimeMs, std::memory_order_release);

  // Update rolling average for DEBUG logging
  m_updateTimeSamples[m_currentSampleIndex] = totalTimeMs;
  m_currentSampleIndex = (m_currentSampleIndex + 1) % PERF_SAMPLE_SIZE;

  // Calculate rolling average
  double sum = std::accumulate(m_updateTimeSamples.begin(), m_updateTimeSamples.end(), 0.0);
  m_avgUpdateTimeMs = sum / PERF_SAMPLE_SIZE;

  m_frameCounter++;

  // Periodic DEBUG logging (every ~5 seconds at 60fps)
  if (m_frameCounter - m_lastDebugLogFrame >= DEBUG_LOG_INTERVAL) {
    m_lastDebugLogFrame = m_frameCounter;

    // Count handlers per type (lock-free read for debug - slightly stale data is acceptable)
    size_t totalHandlers = std::accumulate(m_handlersByType.begin(), m_handlersByType.end(),
                                            size_t{0},
                                            [](size_t sum, const auto& handlers) {
                                              return sum + handlers.size();
                                            });

    // Build event summary string with counts per type
    std::string eventSummary = "";
    for (size_t i = 0; i < eventCountsByType.size(); ++i) {
      if (eventCountsByType[i] > 0) {
        if (!eventSummary.empty()) eventSummary += ", ";
        eventSummary += getEventTypeName(static_cast<EventTypeId>(i)) +
                       "=" + std::to_string(eventCountsByType[i]);
      }
    }
    if (eventSummary.empty()) eventSummary = "none";

    bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);

    if (wasThreaded) {
      size_t optimalWorkers = m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
      size_t availableWorkers = m_lastAvailableWorkers.load(std::memory_order_relaxed);
      size_t eventBudget = m_lastEventBudget.load(std::memory_order_relaxed);

      EVENT_DEBUG("Event Summary - Active: " + std::to_string(totalEventCount) +
                  ", Handlers: " + std::to_string(totalHandlers) +
                  ", Avg Update: " + std::to_string(m_avgUpdateTimeMs) + "ms" +
                  " [Threaded: " + std::to_string(optimalWorkers) + "/" +
                  std::to_string(availableWorkers) + " workers, Budget: " +
                  std::to_string(eventBudget) + "] [Types: " + eventSummary + "]");
    } else {
      EVENT_DEBUG("Event Summary - Active: " + std::to_string(totalEventCount) +
                  ", Handlers: " + std::to_string(totalHandlers) +
                  ", Avg Update: " + std::to_string(m_avgUpdateTimeMs) + "ms" +
                  " [Single-threaded] [Types: " + eventSummary + "]");
    }
  }

  // Only log severe performance issues (>10ms) to reduce noise
  if (totalTimeMs > 10.0) {
    bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);
    if (wasThreaded) {
      size_t optimalWorkers = m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
      size_t availableWorkers = m_lastAvailableWorkers.load(std::memory_order_relaxed);
      size_t eventBudget = m_lastEventBudget.load(std::memory_order_relaxed);

      EVENT_DEBUG("EventManager update took " + std::to_string(totalTimeMs) +
                  "ms (slow frame) [Threaded: " + std::to_string(optimalWorkers) + "/" +
                  std::to_string(availableWorkers) + " workers, Budget: " +
                  std::to_string(eventBudget) + "]");
    } else {
      EVENT_DEBUG("EventManager update took " + std::to_string(totalTimeMs) +
                  "ms (slow frame) [Single-threaded]");
    }
  }
  m_lastUpdateTime.store(endTime);
}

void EventManager::drainAllDeferredEvents() {
  if (!m_initialized.load()) {
    return;
  }

  // Call update() repeatedly until all deferred events are processed
  // Limit iterations to prevent infinite loops (max 100 iterations)
  constexpr int MAX_ITERATIONS = 100;
  for (int i = 0; i < MAX_ITERATIONS; ++i) {
    // Check if dispatch queue is empty
    bool queueEmpty = false;
    {
      std::lock_guard<std::mutex> lock(m_dispatchMutex);
      queueEmpty = m_pendingDispatch.empty();
    }

    if (queueEmpty) {
      break; // All events processed
    }

    // Process another batch
    update();
  }
}

bool EventManager::registerEvent(const std::string &name, EventPtr event) {
  if (!event) {
    EVENT_ERROR("Cannot register null event with name: " + name);
    return false;
  }

  EventTypeId typeId = getEventTypeId(event);
  return registerEventInternal(name, event, typeId);
}

bool EventManager::registerWeatherEvent(const std::string &name,
                                        std::shared_ptr<WeatherEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::Weather, EventPriority::LOW);
}

bool EventManager::registerSceneChangeEvent(
    const std::string &name, std::shared_ptr<SceneChangeEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::SceneChange, EventPriority::LOW);
}

bool EventManager::registerNPCSpawnEvent(const std::string &name,
                                         std::shared_ptr<NPCSpawnEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::NPCSpawn);
}

bool EventManager::registerResourceChangeEvent(
    const std::string &name, std::shared_ptr<ResourceChangeEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::ResourceChange, EventPriority::DEFERRED);
}

bool EventManager::registerWorldEvent(const std::string &name,
                                      std::shared_ptr<WorldEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::World, EventPriority::HIGH);
}

bool EventManager::registerCameraEvent(const std::string &name,
                                       std::shared_ptr<CameraEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::Camera);
}

bool EventManager::registerEventInternal(const std::string &name,
                                         EventPtr event, EventTypeId typeId, uint32_t priority) {
  if (!event) {
    return false;
  }

  std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

  // Check if event already exists
  if (m_nameToIndex.find(name) != m_nameToIndex.end()) {
    EVENT_WARN("Event '" + name + "' already exists, replacing");
  }

  // Create event data
  EventData eventData;
  eventData.event = event;
  eventData.typeId = typeId;
  eventData.setActive(true);
  eventData.priority = priority;

  // Add to type-indexed storage
  auto &container = m_eventsByType[static_cast<size_t>(typeId)];
  size_t index = container.size();
  container.push_back(std::move(eventData));

  // Update name mappings
  m_nameToIndex[name] = index;
  m_nameToType[name] = typeId;

  EVENT_INFO("Registered event '" + name + "' of type " +
             std::string(getEventTypeName(typeId)));
  return true;
}

EventPtr EventManager::getEvent(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

  auto nameIt = m_nameToIndex.find(name);
  if (nameIt == m_nameToIndex.end()) {
    return nullptr;
  }

  auto typeIt = m_nameToType.find(name);
  if (typeIt == m_nameToType.end()) {
    return nullptr;
  }

  const auto &container = m_eventsByType[static_cast<size_t>(typeIt->second)];
  if (nameIt->second >= container.size()) {
    return nullptr;
  }

  return container[nameIt->second].event;
}

std::vector<EventPtr> EventManager::getEventsByType(EventTypeId typeId) const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

  const auto &container = m_eventsByType[static_cast<size_t>(typeId)];
  std::vector<EventPtr> result;
  result.reserve(container.size());

  for (const auto &eventData : container) {
    if (eventData.event) {
      result.push_back(eventData.event);
    }
  }

  return result;
}

std::vector<EventPtr>
EventManager::getEventsByType(const std::string &typeName) const {
  static const std::unordered_map<std::string, EventTypeId> kMap = {
      {"Weather", EventTypeId::Weather},
      {"SceneChange", EventTypeId::SceneChange},
      {"NPCSpawn", EventTypeId::NPCSpawn},
      {"ParticleEffect", EventTypeId::ParticleEffect},
      {"ResourceChange", EventTypeId::ResourceChange},
      {"World", EventTypeId::World},
      {"Camera", EventTypeId::Camera},
      {"Harvest", EventTypeId::Harvest},
      {"Collision", EventTypeId::Collision},
      {"WorldTrigger", EventTypeId::WorldTrigger},
      {"CollisionObstacleChanged", EventTypeId::CollisionObstacleChanged},
      {"Custom", EventTypeId::Custom},
  };
  auto it = kMap.find(typeName);
  return getEventsByType(it == kMap.end() ? EventTypeId::Custom : it->second);
}

bool EventManager::setEventActive(const std::string &name, bool active) {
  std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

  auto nameIt = m_nameToIndex.find(name);
  if (nameIt == m_nameToIndex.end()) {
    return false;
  }

  auto typeIt = m_nameToType.find(name);
  if (typeIt == m_nameToType.end()) {
    return false;
  }

  auto &container = m_eventsByType[static_cast<size_t>(typeIt->second)];
  if (nameIt->second >= container.size()) {
    return false;
  }

  container[nameIt->second].setActive(active);
  return true;
}

bool EventManager::isEventActive(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

  auto nameIt = m_nameToIndex.find(name);
  if (nameIt == m_nameToIndex.end()) {
    return false;
  }

  auto typeIt = m_nameToType.find(name);
  if (typeIt == m_nameToType.end()) {
    return false;
  }

  const auto &container = m_eventsByType[static_cast<size_t>(typeIt->second)];
  if (nameIt->second >= container.size()) {
    return false;
  }

  return container[nameIt->second].isActive();
}

bool EventManager::removeEvent(const std::string &name) {
  std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

  auto nameIt = m_nameToIndex.find(name);
  if (nameIt == m_nameToIndex.end()) {
    return false;
  }

  auto typeIt = m_nameToType.find(name);
  if (typeIt == m_nameToType.end()) {
    return false;
  }

  // Mark for removal (will be cleaned up during compaction)
  auto &container = m_eventsByType[static_cast<size_t>(typeIt->second)];
  if (nameIt->second < container.size()) {
    container[nameIt->second].flags |= EventData::FLAG_PENDING_REMOVAL;
  }

  // Remove from name mappings
  m_nameToIndex.erase(nameIt);
  m_nameToType.erase(typeIt);

  return true;
}

size_t EventManager::removeEventsByType(EventTypeId typeId) {
  std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

  if (typeId >= EventTypeId::COUNT) {
    return 0;
  }

  size_t typeIndex = static_cast<size_t>(typeId);
  auto &container = m_eventsByType[typeIndex];
  size_t removedCount = 0;

  // Mark all events of this type for removal
  for (auto &eventData : container) {
    if (!(eventData.flags & EventData::FLAG_PENDING_REMOVAL)) {
      eventData.flags |= EventData::FLAG_PENDING_REMOVAL;
      removedCount++;
    }
  }

  // Remove from name mappings (iterate through copy to avoid invalidation)
  std::vector<std::string> namesToRemove;
  for (const auto &[name, eventTypeId] : m_nameToType) {
    if (eventTypeId == typeId) {
      namesToRemove.push_back(name);
    }
  }

  for (const auto &name : namesToRemove) {
    m_nameToIndex.erase(name);
    m_nameToType.erase(name);
  }

  return removedCount;
}

size_t EventManager::clearAllEvents() {
  size_t totalRemoved = 0;

  // Remove events for each type
  for (size_t i = 0; i < static_cast<size_t>(EventTypeId::COUNT); ++i) {
    totalRemoved += removeEventsByType(static_cast<EventTypeId>(i));
  }

  return totalRemoved;
}

bool EventManager::hasEvent(const std::string &name) const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
  return m_nameToIndex.find(name) != m_nameToIndex.end();
}

bool EventManager::executeEvent(const std::string &eventName) const {
  auto event = getEvent(eventName);
  if (!event) {
    return false;
  }

  // Get the event's type ID to find the appropriate handlers
  EventTypeId typeId = event->getTypeId();

  // OPTIMIZATION: Shared lock for concurrent handler invocation (READ-ONLY)
  std::shared_lock<std::shared_mutex> lock(m_handlersMutex);
  const auto &typeHandlers = m_handlersByType[static_cast<size_t>(typeId)];
  auto nameIt = m_nameHandlers.find(eventName);

  if (typeHandlers.empty() && (nameIt == m_nameHandlers.end() || nameIt->second.empty())) {
    // No handlers registered: execute directly
    try { event->execute(); }
    catch (const std::exception &e) { EVENT_ERROR(std::string("Exception in executeEvent direct exec: ") + e.what()); }
    catch (...) { EVENT_ERROR("Unknown exception in executeEvent direct exec"); }
    return true;
  }

  // Create EventData once
  EventData eventData;
  eventData.event = event;
  eventData.typeId = typeId;
  eventData.setActive(true);

  // Call type handlers directly (no copy, no allocation)
  for (const auto &entry : typeHandlers) {
    if (entry) {
      try {
        entry.callable(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in executeEvent '" + eventName + "': " +
                    std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in executeEvent '" + eventName + "'");
      }
    }
  }

  // Call name handlers directly (no copy, no allocation)
  if (nameIt != m_nameHandlers.end()) {
    for (const auto &entry : nameIt->second) {
      if (entry) {
        try { entry.callable(eventData); }
        catch (const std::exception &e) { EVENT_ERROR("Name-handler exception in executeEvent: " + std::string(e.what())); }
        catch (...) { EVENT_ERROR("Unknown name-handler exception in executeEvent"); }
      }
    }
  }

  return true;
}

int EventManager::executeEventsByType(EventTypeId typeId) const {
  auto events = getEventsByType(typeId);

  for (auto &event : events) {
    if (event) {
      event->execute();
    }
  }

  return static_cast<int>(events.size());
}

int EventManager::executeEventsByType(const std::string &eventType) const {
  EventTypeId typeId = EventTypeId::Custom;

  if (eventType == "Weather")
    typeId = EventTypeId::Weather;
  else if (eventType == "SceneChange")
    typeId = EventTypeId::SceneChange;
  else if (eventType == "NPCSpawn")
    typeId = EventTypeId::NPCSpawn;
  else if (eventType == "ParticleEffect")
    typeId = EventTypeId::ParticleEffect;
  else if (eventType == "ResourceChange")
    typeId = EventTypeId::ResourceChange;
  else if (eventType == "World")
    typeId = EventTypeId::World;
  else if (eventType == "Camera")
    typeId = EventTypeId::Camera;
  else if (eventType == "Harvest")
    typeId = EventTypeId::Harvest;

  return executeEventsByType(typeId);
}

void EventManager::registerHandler(EventTypeId typeId,
                                   FastEventHandler handler) {
  // Backward-compatible API: register and ignore token
  (void)registerHandlerWithToken(typeId, std::move(handler));
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
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  return m_handlersByType[static_cast<size_t>(typeId)].size();
}

EventManager::HandlerToken
EventManager::registerHandlerWithToken(EventTypeId typeId, FastEventHandler handler) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  const size_t idx = static_cast<size_t>(typeId);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);

  m_handlersByType[idx].emplace_back(std::move(handler), id);
  return HandlerToken{typeId, id, false, {}};
}

bool EventManager::removeHandler(const HandlerToken &token) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  if (token.forName) {
    auto it = m_nameHandlers.find(token.name);
    if (it == m_nameHandlers.end()) return false;

    auto &entries = it->second;
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i].id == token.id) {
        // Mark as invalid (will be filtered during invocation)
        entries[i] = HandlerEntry();
        return true;
      }
    }
    return false;
  } else {
    const size_t idx = static_cast<size_t>(token.typeId);
    if (idx >= m_handlersByType.size()) return false;

    auto &entries = m_handlersByType[idx];
    auto it = std::find_if(entries.begin(), entries.end(),
                           [&token](const HandlerEntry& entry) {
                             return entry.id == token.id;
                           });
    if (it != entries.end()) {
      // Mark as invalid (will be filtered during invocation)
      *it = HandlerEntry();
      return true;
    }
    return false;
  }
}

EventManager::HandlerToken
EventManager::registerHandlerForName(const std::string &name, FastEventHandler handler) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);

  m_nameHandlers[name].emplace_back(std::move(handler), id);
  return HandlerToken{EventTypeId::Custom, id, true, name};
}

void EventManager::updateEventTypeBatch(EventTypeId typeId) const {
  auto startTime = getCurrentTimeNanos();

  // Copy events to local vector to minimize lock time (pointers only)
  thread_local std::vector<EventPtr> localEvents;
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    const auto &container = m_eventsByType[static_cast<size_t>(typeId)];
    localEvents.clear();
    localEvents.reserve(container.size());
    for (const auto &ed : container) {
      if (ed.isActive() && ed.event) localEvents.push_back(ed.event);
    }
  }

  // Process events without holding lock
  for (auto &evt : localEvents) {
    evt->update();

    // Conditional events: auto-execute when conditions are met
    // OPTIMIZATION: Only check conditions if handlers registered (avoid unnecessary work)
    if (!m_handlersByType[static_cast<size_t>(typeId)].empty() && evt->checkConditions()) {
      EventData data;
      data.event = evt;
      data.typeId = typeId;
      data.setActive(true);

      // Dispatch to handlers (same as trigger methods)
      // Use const_cast since dispatchEvent requires non-const EventData&
      dispatchEvent(typeId, data, DispatchMode::Immediate, "conditional_event_auto_execute");
    }
  }

  // Simplified performance recording - reduce overhead
  auto endTime = getCurrentTimeNanos();
  double timeMs = (endTime - startTime) / 1000000.0;

  // Only record performance for operations that took significant time
  if (timeMs > 1.0 || localEvents.size() > 50) {
    recordPerformance(typeId, timeMs);
  }

  // Only log significant performance issues (>5ms) to reduce noise
  if (timeMs > 5.0) {
    EVENT_DEBUG("Updated " + std::to_string(localEvents.size()) +
                " events of type " + std::string(getEventTypeName(typeId)) +
                " in " + std::to_string(timeMs) + "ms (slow)");
  }
}

void EventManager::updateEventTypeBatchThreaded(EventTypeId typeId) {
  if (m_isShutdown || !HammerEngine::ThreadSystem::Exists()) {
    // Fall back to single-threaded if shutting down or ThreadSystem not available
    updateEventTypeBatch(typeId);
    return;
  }

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  auto startTime = getCurrentTimeNanos();

  // Copy events to local vector to minimize lock time
  // Use shared_ptr so async lambdas can safely access this after processEventsByType() returns
  auto localEvents = std::make_shared<std::vector<EventPtr>>();
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    const auto &container = m_eventsByType[static_cast<size_t>(typeId)];

    if (container.empty()) {
      return;
    }

    localEvents->clear();
    localEvents->reserve(container.size());
    for (const auto &ed : container) {
      if (ed.isActive() && ed.event) localEvents->push_back(ed.event);
    }
  }

  if (localEvents->empty()) {
    return;
  }

  // Proper WorkerBudget calculation with architectural respect
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t eventWorkerBudget = budget.eventAllocated;

  // Store thread allocation info for debug output
  m_lastAvailableWorkers.store(availableWorkers, std::memory_order_relaxed);
  m_lastEventBudget.store(eventWorkerBudget, std::memory_order_relaxed);

  // Check queue pressure before submitting tasks
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  size_t pressureThreshold = static_cast<size_t>(queueCapacity * HammerEngine::QUEUE_PRESSURE_CRITICAL); // Use unified threshold

  if (queueSize > pressureThreshold) {
    // Graceful degradation: fallback to single-threaded processing
    EVENT_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                std::to_string(queueCapacity) +
                "), using single-threaded processing");
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    for (auto &evt : *localEvents) { evt->update(); }

    // Record performance and return early
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    if (timeMs > 1.0 || localEvents->size() > 50) {
      recordPerformance(typeId, timeMs);
    }
    return;
  }

  // Use buffer capacity for high workloads
  size_t optimalWorkerCount =
      budget.getOptimalWorkerCount(eventWorkerBudget, localEvents->size(), 100);

  // Store optimal worker count and mark as threaded
  m_lastOptimalWorkerCount.store(optimalWorkerCount, std::memory_order_relaxed);
  m_lastWasThreaded.store(true, std::memory_order_relaxed);

  // Use unified adaptive batch calculation for consistency across all managers
  const size_t threshold = std::max(m_threadingThreshold, static_cast<size_t>(1));
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;

  // Get previous frame's completion time for adaptive feedback
  double lastFrameTime = m_adaptiveBatchState.lastUpdateTimeMs.load(std::memory_order_acquire);

  auto [batchCount, calculatedBatchSize] = HammerEngine::calculateBatchStrategy(
      HammerEngine::EVENT_BATCH_CONFIG,
      localEvents->size(),
      threshold,
      optimalWorkerCount,
      eventWorkerBudget,     // Base allocation for buffer detection
      queuePressure,
      m_adaptiveBatchState,  // Adaptive state for performance tuning
      lastFrameTime          // Previous frame's completion time
  );

  // Debug logging for high queue pressure
  if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
    EVENT_DEBUG("High queue pressure (" +
                std::to_string(static_cast<int>(queuePressure * 100)) +
                "%), using larger batches");
  }

  // Simple batch processing without complex spin-wait
  if (batchCount > 1) {
    // Debug thread allocation info periodically
    static thread_local uint64_t debugFrameCounter = 0;
    if (++debugFrameCounter % 300 == 0 && !localEvents->empty()) {
      EVENT_DEBUG("Event Thread Allocation - Workers: " +
                  std::to_string(optimalWorkerCount) + "/" +
                  std::to_string(availableWorkers) +
                  ", Event Budget: " + std::to_string(eventWorkerBudget) +
                  ", Batches: " + std::to_string(batchCount));
    }

    size_t batchSize = localEvents->size() / batchCount;
    size_t remainingEvents = localEvents->size() % batchCount;

    // Submit batches using futures for native async result tracking
    std::vector<std::future<void>> batchFutures;
    batchFutures.reserve(batchCount);

    for (size_t i = 0; i < batchCount; ++i) {
      size_t start = i * batchSize;
      size_t end = start + batchSize;

      // Add remaining events to last batch
      if (i == batchCount - 1) {
        end += remainingEvents;
      }

      // Submit each batch with future for completion tracking
      // localEvents captured by shared_ptr value - safe for async execution after function returns
      batchFutures.push_back(threadSystem.enqueueTaskWithResult(
        [this, localEvents, start, end, typeId]() -> void {
          try {
            for (size_t j = start; j < end; ++j) {
              (*localEvents)[j]->update();

              // Conditional events: auto-execute when conditions are met
              // OPTIMIZATION: Only check conditions if handlers registered
              if (!m_handlersByType[static_cast<size_t>(typeId)].empty() && (*localEvents)[j]->checkConditions()) {
                EventData data;
                data.event = (*localEvents)[j];
                data.typeId = typeId;
                data.setActive(true);

                // Dispatch to handlers (same as trigger methods)
                dispatchEvent(typeId, data, DispatchMode::Immediate, "conditional_event_auto_execute_threaded");
              }
            }
          } catch (const std::exception &e) {
            EVENT_ERROR(std::string("Exception in event batch: ") + e.what());
          } catch (...) {
            EVENT_ERROR("Unknown exception in event batch");
          }
        },
        HammerEngine::TaskPriority::Normal,
        "Event_Batch"
      ));
    }

    // Store futures for shutdown synchronization (futures-based completion tracking)
    // NO BLOCKING WAIT: Events don't need sync in update(), only during shutdown
    if (!batchFutures.empty()) {
      std::lock_guard<std::mutex> lock(m_batchFuturesMutex);
      m_batchFutures = std::move(batchFutures);
    }
  } else {
    // Process single-threaded for small event counts
    for (auto &evt : *localEvents) {
      evt->update();

      // Conditional events: auto-execute when conditions are met
      // OPTIMIZATION: Only check conditions if handlers registered
      if (!m_handlersByType[static_cast<size_t>(typeId)].empty() && evt->checkConditions()) {
        EventData data;
        data.event = evt;
        data.typeId = typeId;
        data.setActive(true);

        // Dispatch to handlers (same as trigger methods)
        dispatchEvent(typeId, data, DispatchMode::Immediate, "conditional_event_auto_execute");
      }
    }
  }

  // Simplified performance recording
  auto endTime = getCurrentTimeNanos();
  double timeMs = (endTime - startTime) / 1000000.0;

  if (timeMs > 1.0 || localEvents->size() > 50) {
    recordPerformance(typeId, timeMs);
  }
}

void EventManager::processEventDirect(EventData &eventData) {
  if (!eventData.event) {
    return;
  }

  // Only update the event, don't automatically execute or call handlers
  // Events should only execute when explicitly triggered, not every frame
  eventData.event->update();

  // Note: Handlers are only called when events are explicitly triggered
  // via changeWeather(), changeScene(), spawnNPC(), etc.
  // This prevents the continuous handler from being called repeatedly
}

bool EventManager::changeWeather(const std::string &weatherType,
                                 float transitionTime,
                                 DispatchMode mode) const {
  // Build payload (use pool)
  std::shared_ptr<WeatherEvent> weatherEvent = m_weatherPool.acquire();
  if (!weatherEvent) weatherEvent = std::make_shared<WeatherEvent>("trigger_weather", weatherType);
  else weatherEvent->setWeatherType(weatherType);
  WeatherParams params = weatherEvent->getWeatherParams();
  params.transitionTime = transitionTime;
  weatherEvent->setWeatherParams(params);

  EventData data;
  data.typeId = EventTypeId::Weather;
  data.setActive(true);
  data.event = weatherEvent;

  return dispatchEvent(EventTypeId::Weather, data, mode, "changeWeather");
}

bool EventManager::changeScene(const std::string &sceneId,
                               const std::string &transitionType,
                               float transitionTime,
                               DispatchMode mode) const {
  // Build payload
  auto sceneEvent = m_sceneChangePool.acquire();
  if (!sceneEvent) sceneEvent = std::make_shared<SceneChangeEvent>("trigger_scene_change", sceneId);
  else sceneEvent->setTargetSceneID(sceneId);
  // Map transition type string to enum
  TransitionType t = TransitionType::Fade;
  if (transitionType == "slide") t = TransitionType::Slide;
  else if (transitionType == "dissolve") t = TransitionType::Dissolve;
  else if (transitionType == "wipe") t = TransitionType::Wipe;
  sceneEvent->setTransitionType(t);
  TransitionParams params(transitionTime, t);
  sceneEvent->setTransitionParams(params);

  EventData data;
  data.typeId = EventTypeId::SceneChange;
  data.setActive(true);
  data.event = sceneEvent;

  return dispatchEvent(EventTypeId::SceneChange, data, mode, "changeScene");
}

bool EventManager::spawnNPC(const std::string &npcType, float x,
                            float y, DispatchMode mode) const {
  // Build payload
  SpawnParameters params(npcType, 1, 0.0f);
  auto npcEvent = m_npcSpawnPool.acquire();
  if (!npcEvent) npcEvent = std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", params);
  else npcEvent->setSpawnParameters(params);
  npcEvent->addSpawnPoint(x, y);

  EventData data;
  data.typeId = EventTypeId::NPCSpawn;
  data.setActive(true);
  data.event = npcEvent;

  return dispatchEvent(EventTypeId::NPCSpawn, data, mode, "spawnNPC");
}

bool EventManager::triggerParticleEffect(const std::string &effectName, float x, float y,
                                         float intensity, float duration,
                                         const std::string &groupTag,
                                         DispatchMode mode) const {
  ParticleEffectType effectType = ParticleEffectEvent::stringToEffectType(effectName);
  auto pe = std::make_shared<ParticleEffectEvent>("trigger_particle_effect", effectType,
                                                  x, y, intensity, duration, groupTag, "");
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
    EntityPtr owner, HammerEngine::ResourceHandle resourceHandle,
    int oldQuantity, int newQuantity, const std::string &changeReason,
    DispatchMode mode) const {
  EventData eventData;
  eventData.typeId = EventTypeId::ResourceChange;
  eventData.setActive(true);
  eventData.event = std::make_shared<ResourceChangeEvent>(
      owner, resourceHandle, oldQuantity, newQuantity, changeReason);

  return dispatchEvent(EventTypeId::ResourceChange, eventData, mode, "triggerResourceChange");
}

bool EventManager::createWeatherEvent(const std::string &name,
                                      const std::string &weatherType,
                                      float intensity, float transitionTime) {
  // Cache EventFactory reference for better performance
  EventFactory &eventFactory = EventFactory::Instance();
  auto event = eventFactory.createWeatherEvent(name, weatherType, intensity,
                                               transitionTime);
  if (!event) {
    return false;
  }

  auto weatherEvent = std::dynamic_pointer_cast<WeatherEvent>(event);
  if (weatherEvent) {
    return registerWeatherEvent(name, weatherEvent);
  }

  return registerEvent(name, event);
}

bool EventManager::triggerCollision(const HammerEngine::CollisionInfo &info,
                                    DispatchMode mode) const {
  EventData eventData;
  eventData.typeId = EventTypeId::Collision;
  eventData.setActive(true);
  eventData.priority = EventPriority::CRITICAL;
  eventData.event = std::make_shared<CollisionEvent>(info);

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

bool EventManager::triggerCollisionObstacleChanged(const Vector2D& position,
                                                  float radius,
                                                  const std::string& description,
                                                  DispatchMode mode) const {
  EventData eventData;
  eventData.typeId = EventTypeId::CollisionObstacleChanged;
  eventData.setActive(true);
  eventData.priority = EventPriority::CRITICAL;
  eventData.event = std::make_shared<CollisionObstacleChangedEvent>(
    CollisionObstacleChangedEvent::ChangeType::MODIFIED, position, radius, description);

  return dispatchEvent(EventTypeId::CollisionObstacleChanged, eventData, mode, "triggerCollisionObstacleChanged");
}

bool EventManager::createSceneChangeEvent(const std::string &name,
                                          const std::string &targetScene,
                                          const std::string &transitionType,
                                          float transitionTime) {
  // Cache EventFactory reference for better performance
  EventFactory &eventFactory = EventFactory::Instance();
  auto event = eventFactory.createSceneChangeEvent(
      name, targetScene, transitionType, transitionTime);
  if (!event) {
    return false;
  }

  auto sceneEvent = std::dynamic_pointer_cast<SceneChangeEvent>(event);
  if (sceneEvent) {
    return registerSceneChangeEvent(name, sceneEvent);
  }

  return registerEvent(name, event);
}

bool EventManager::createNPCSpawnEvent(const std::string &name,
                                       const std::string &npcType, int count,
                                       float spawnRadius) {
  // Cache EventFactory reference for better performance
  EventFactory &eventFactory = EventFactory::Instance();
  auto event =
      eventFactory.createNPCSpawnEvent(name, npcType, count, spawnRadius);
  if (!event) {
    return false;
  }

  auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(event);
  if (npcEvent) {
    return registerNPCSpawnEvent(name, npcEvent);
  }

  return registerEvent(name, event);
}

bool EventManager::createResourceChangeEvent(
    const std::string &name, EntityPtr owner,
    HammerEngine::ResourceHandle resourceHandle, int oldQuantity,
    int newQuantity, const std::string &changeReason) {
  auto event = std::make_shared<ResourceChangeEvent>(
      owner, resourceHandle, oldQuantity, newQuantity, changeReason);
  return registerResourceChangeEvent(name, event);
}

bool EventManager::createParticleEffectEvent(const std::string &name,
                                             const std::string &effectName,
                                             float x, float y, float intensity,
                                             float duration,
                                             const std::string &groupTag) {
  try {
    // Convert string effect name to ParticleEffectType enum
    ParticleEffectType effectType =
        ParticleEffectEvent::stringToEffectType(effectName);

    // Create ParticleEffectEvent directly (no factory needed for this simple
    // event)
    auto event = std::make_shared<ParticleEffectEvent>(
        name, effectType, x, y, intensity, duration, groupTag, "");
    // Note: std::make_shared never returns nullptr for successful allocation
    // If allocation fails, it throws std::bad_alloc instead

    // Register with EventManager
    return registerEventInternal(name, event, EventTypeId::ParticleEffect);

  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating ParticleEffectEvent '" + name +
                "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating ParticleEffectEvent: " + name);
    return false;
  }
}

bool EventManager::createParticleEffectEvent(const std::string &name,
                                             const std::string &effectName,
                                             const Vector2D &position,
                                             float intensity, float duration,
                                             const std::string &groupTag) {
  return createParticleEffectEvent(name, effectName, position.getX(),
                                   position.getY(), intensity, duration,
                                   groupTag);
}

// World event convenience methods
bool EventManager::createWorldLoadedEvent(const std::string &name, const std::string &worldId,
                             int width, int height) {
  try {
    auto event = std::make_shared<WorldLoadedEvent>(worldId, width, height);
    return registerWorldEvent(name, event);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating WorldLoadedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating WorldLoadedEvent: " + name);
    return false;
  }
}

bool EventManager::createWorldUnloadedEvent(const std::string &name, const std::string &worldId) {
  try {
    auto event = std::make_shared<WorldUnloadedEvent>(worldId);
    return registerWorldEvent(name, event);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating WorldUnloadedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating WorldUnloadedEvent: " + name);
    return false;
  }
}

bool EventManager::createTileChangedEvent(const std::string &name, int x, int y,
                             const std::string &changeType) {
  try {
    auto event = std::make_shared<TileChangedEvent>(x, y, changeType);
    return registerWorldEvent(name, event);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating TileChangedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating TileChangedEvent: " + name);
    return false;
  }
}

bool EventManager::createWorldGeneratedEvent(const std::string &name, const std::string &worldId,
                                int width, int height, float generationTime) {
  try {
    auto event = std::make_shared<WorldGeneratedEvent>(worldId, width, height, generationTime);
    return registerWorldEvent(name, event);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating WorldGeneratedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating WorldGeneratedEvent: " + name);
    return false;
  }
}

// World triggers (no registration)
bool EventManager::triggerWorldLoaded(const std::string &worldId, int width, int height,
                                      DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldLoadedEvent>(worldId, width, height);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldLoaded");
}

bool EventManager::triggerWorldUnloaded(const std::string &worldId, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldUnloadedEvent>(worldId);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldUnloaded");
}

bool EventManager::triggerTileChanged(int x, int y, const std::string &changeType, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<TileChangedEvent>(x, y, changeType);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerTileChanged");
}

bool EventManager::triggerWorldGenerated(const std::string &worldId, int width, int height,
                                         float generationTime, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::World;
  data.setActive(true);
  data.event = std::make_shared<WorldGeneratedEvent>(worldId, width, height, generationTime);
  return dispatchEvent(EventTypeId::World, data, mode, "triggerWorldGenerated");
}

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

size_t EventManager::getEventCount() const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
  size_t total = 0;
  for (const auto &container : m_eventsByType) {
    total += std::count_if(
        container.begin(), container.end(), [](const EventData &eventData) {
          return !(eventData.flags & EventData::FLAG_PENDING_REMOVAL);
        });
  }
  return total;
}

size_t EventManager::getEventCount(EventTypeId typeId) const {
  std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
  const auto &container = m_eventsByType[static_cast<size_t>(typeId)];
  return std::count_if(
      container.begin(), container.end(), [](const EventData &eventData) {
        return !(eventData.flags & EventData::FLAG_PENDING_REMOVAL);
      });
}

void EventManager::compactEventStorage() {
  std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

  for (auto &container : m_eventsByType) {
    // Remove events marked for deletion
    container.erase(std::remove_if(container.begin(), container.end(),
                                   [](const EventData &data) {
                                     return data.flags &
                                            EventData::FLAG_PENDING_REMOVAL;
                                   }),
                    container.end());
  }

  // Rebuild name mappings after compaction
  m_nameToIndex.clear();
  m_nameToType.clear();
  // Name mapping removed - using typeId for all lookups
}

void EventManager::clearEventPools() {
  m_weatherPool.clear();
  m_sceneChangePool.clear();
  m_npcSpawnPool.clear();
  m_resourceChangePool.clear();
  m_worldPool.clear();
  m_cameraPool.clear();
}

// clearAllEvents removed from public API; tests call clean()+init() via EventManagerTestAccess

EventTypeId EventManager::getEventTypeId(const EventPtr &event) const { return event ? event->getTypeId() : EventTypeId::Custom; }

std::string EventManager::getEventTypeName(EventTypeId typeId) const {
  switch (typeId) {
  case EventTypeId::Weather:
    return "Weather";
  case EventTypeId::SceneChange:
    return "SceneChange";
  case EventTypeId::NPCSpawn:
    return "NPCSpawn";
  case EventTypeId::ParticleEffect:
    return "ParticleEffect";
  case EventTypeId::ResourceChange:
    return "ResourceChange";
  case EventTypeId::World:
    return "World";
  case EventTypeId::Camera:
    return "Camera";
  case EventTypeId::Harvest:
    return "Harvest";
  case EventTypeId::Collision:
    return "Collision";
  case EventTypeId::WorldTrigger:
    return "WorldTrigger";
  case EventTypeId::CollisionObstacleChanged:
    return "CollisionObstacleChanged";
  case EventTypeId::Custom:
    return "Custom";
  default:
    return "Unknown";
  }
}

// Individual update methods for specific event types
void EventManager::updateWeatherEvents() {
  updateEventTypeBatch(EventTypeId::Weather);
}

void EventManager::updateSceneChangeEvents() {
  updateEventTypeBatch(EventTypeId::SceneChange);
}

void EventManager::updateNPCSpawnEvents() {
  updateEventTypeBatch(EventTypeId::NPCSpawn);
}

void EventManager::updateResourceChangeEvents() {
  updateEventTypeBatch(EventTypeId::ResourceChange);
}

void EventManager::updateCustomEvents() {
  updateEventTypeBatch(EventTypeId::Custom);
}

void EventManager::updateWorldEvents() {
  updateEventTypeBatch(EventTypeId::World);
}

void EventManager::updateCameraEvents() {
  updateEventTypeBatch(EventTypeId::Camera);
}

void EventManager::updateHarvestEvents() {
  updateEventTypeBatch(EventTypeId::Harvest);
}

void EventManager::recordPerformance(EventTypeId typeId, double timeMs) const {
  // Reduced lock contention - only update stats periodically
  static thread_local uint64_t updateCounter = 0;
  updateCounter++;

  // Only acquire lock every 10th call to reduce contention
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

void EventManager::enqueueDispatch(EventTypeId typeId, const EventData &data) const {
  std::lock_guard<std::mutex> lock(m_dispatchMutex);
  if (m_pendingDispatch.size() >= m_maxDispatchQueue) {
    m_pendingDispatch.pop_front();
  }
  m_pendingDispatch.push_back(PendingDispatch{typeId, data});
}

void EventManager::removeNameHandlers(const std::string &name) {
  std::unique_lock<std::shared_mutex> lock(m_handlersMutex);
  m_nameHandlers.erase(name);
}

void EventManager::drainDispatchQueueWithBudget() {
  // Compute per-frame cap from worker budget
  size_t maxToProcess = 64; // base
  if (HammerEngine::ThreadSystem::Exists()) {
    const auto &threadSystem = HammerEngine::ThreadSystem::Instance();
    size_t workers = static_cast<size_t>(threadSystem.getThreadCount());
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(workers);
    maxToProcess = 32 + (budget.eventAllocated * 32);
  }

  // Removed timing budget - process all events for reliability

  std::vector<PendingDispatch> local;
  local.reserve(maxToProcess);
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    const size_t backlog = m_pendingDispatch.size();
    if (backlog > m_maxDispatchQueue / 2) {
      maxToProcess += std::min(backlog / 64, static_cast<size_t>(256));
    }
    size_t toTake = std::min(maxToProcess, m_pendingDispatch.size());
    for (size_t i = 0; i < toTake; ++i) {
      local.push_back(std::move(m_pendingDispatch.front()));
      m_pendingDispatch.pop_front();
    }
  }

  // OPTIMIZATION: Sort events by priority (higher priority = processed first)
  std::sort(local.begin(), local.end(), [](const PendingDispatch& a, const PendingDispatch& b) {
    return a.data.priority > b.data.priority; // Higher priority first
  });

  // OPTIMIZATION: Shared lock for concurrent handler invocation (READ-ONLY)
  std::shared_lock<std::shared_mutex> lock(m_handlersMutex);

  // Process each event with direct handler access (shared lock allows concurrent reads)
  for (const auto &pd : local) {
    const EventData &eventData = pd.data;

    // Invoke type handlers directly
    const auto &typeHandlers = m_handlersByType[static_cast<size_t>(pd.typeId)];
    for (const auto &entry : typeHandlers) {
      if (entry) {  // Skip invalidated handlers
        try {
          entry.callable(eventData);
        } catch (const std::exception &e) {
          EVENT_ERROR("Handler exception in deferred dispatch: " + std::string(e.what()));
        } catch (...) {
          EVENT_ERROR("Unknown handler exception in deferred dispatch");
        }
      }
    }

    // Name handlers removed - all handlers registered by typeId
  }
}

// OPTIMIZATION: Consolidated dispatch helper - eliminates 500+ lines of duplicate code
bool EventManager::dispatchEvent(EventTypeId typeId, EventData& eventData, DispatchMode mode,
                                  const char* errorContext) const {
  if (mode == DispatchMode::Immediate) {
    // Shared lock for concurrent handler invocation (READ-ONLY)
    std::shared_lock<std::shared_mutex> lock(m_handlersMutex);

    const auto &typeHandlers = m_handlersByType[static_cast<size_t>(typeId)];

    // Early exit if no handlers
    if (typeHandlers.empty()) {
      // No handlers registered - nothing to dispatch
      return false;
    }

    // Invoke type handlers directly (no copy, no allocation)
    for (const auto &entry : typeHandlers) {
      if (entry) {
        try {
          entry.callable(eventData);
        } catch (const std::exception &e) {
          EVENT_ERROR(std::string("Handler exception in ") + errorContext + ": " + e.what());
        } catch (...) {
          EVENT_ERROR(std::string("Unknown handler exception in ") + errorContext);
        }
      }
    }

    // Name handlers removed - all dispatch uses typeId only
    return true;
  }

  // Deferred dispatch
  enqueueDispatch(typeId, eventData);
  return true;
}

// Camera event convenience methods
bool EventManager::createCameraMovedEvent(const std::string &name, const Vector2D &newPos,
                             const Vector2D &oldPos) {
  try {
    auto event = std::make_shared<CameraMovedEvent>(newPos, oldPos);
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::Camera);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating CameraMovedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating CameraMovedEvent: " + name);
    return false;
  }
}

bool EventManager::createCameraModeChangedEvent(const std::string &name, int newMode, int oldMode) {
  try {
    auto event = std::make_shared<CameraModeChangedEvent>(
        static_cast<CameraModeChangedEvent::Mode>(newMode),
        static_cast<CameraModeChangedEvent::Mode>(oldMode));
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::Camera);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating CameraModeChangedEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating CameraModeChangedEvent: " + name);
    return false;
  }
}

bool EventManager::createCameraShakeEvent(const std::string &name, float duration, float intensity) {
  try {
    auto event = std::make_shared<CameraShakeStartedEvent>(duration, intensity);
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::Camera);
  } catch (const std::exception &e) {
    EVENT_ERROR("Exception creating CameraShakeEvent '" + name + "': " + e.what());
    return false;
  } catch (...) {
    EVENT_ERROR("Unknown exception creating CameraShakeEvent: " + name);
    return false;
  }
}

// Camera triggers (no registration)
bool EventManager::triggerCameraMoved(const Vector2D &newPos, const Vector2D &oldPos,
                                      DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraMovedEvent>(newPos, oldPos);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraMoved");
}

bool EventManager::triggerCameraModeChanged(int newMode, int oldMode, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraModeChangedEvent>(
      static_cast<CameraModeChangedEvent::Mode>(newMode),
      static_cast<CameraModeChangedEvent::Mode>(oldMode));
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraModeChanged");
}

bool EventManager::triggerCameraShakeStarted(float duration, float intensity, DispatchMode mode) const {
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

bool EventManager::triggerCameraTargetChanged(std::weak_ptr<Entity> newTarget,
                                              std::weak_ptr<Entity> oldTarget,
                                              DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraTargetChangedEvent>(newTarget, oldTarget);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraTargetChanged");
}

bool EventManager::triggerCameraZoomChanged(float newZoom, float oldZoom, DispatchMode mode) const {
  EventData data;
  data.typeId = EventTypeId::Camera;
  data.setActive(true);
  data.event = std::make_shared<CameraZoomChangedEvent>(newZoom, oldZoom);
  return dispatchEvent(EventTypeId::Camera, data, mode, "triggerCameraZoomChanged");
}
