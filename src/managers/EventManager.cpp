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

#include <algorithm>
#include <chrono>
#include <future>

bool EventManager::init() {
  if (m_initialized.load()) {
    EVENT_WARN("EventManager already initialized");
    return true;
  }

  // Only log if not in shutdown to avoid static destruction order issues
  if (!m_isShutdown) {
    EVENT_INFO("Initializing EventManager with performance optimizations");
  }

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

  // Configure event pools for common ephemeral triggers
  m_weatherPool.setCreator([](){ return std::make_shared<WeatherEvent>("trigger_weather", WeatherType::Clear); });
  m_sceneChangePool.setCreator([](){ return std::make_shared<SceneChangeEvent>("trigger_scene_change", ""); });
  m_npcSpawnPool.setCreator([](){ return std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", SpawnParameters{}); });

  // Reset performance stats
  resetPerformanceStats();

  m_lastUpdateTime.store(getCurrentTimeNanos());
  m_initialized.store(true);

  // Only log if not in shutdown to avoid static destruction order issues
  if (!m_isShutdown) {
    EVENT_INFO(
        "EventManager initialized successfully with type-indexed storage");
  }
  return true;
}

void EventManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return;
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

  // Reset performance stats
  resetPerformanceStats();

  m_initialized.store(false);
  m_isShutdown = true;
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

  // Reset performance stats
  resetPerformanceStats();

  EVENT_INFO("EventManager prepared for state transition");
}

void EventManager::update() {
  if (!m_initialized.load()) {
    return;
  }

  auto startTime = getCurrentTimeNanos();

  // Update all event types in optimized batches
  if (m_threadingEnabled.load() && getEventCount() > m_threadingThreshold) {
    // Use threading for medium+ event counts (consistent with buffer threshold)
    updateEventTypeBatchThreaded(EventTypeId::Weather);
    updateEventTypeBatchThreaded(EventTypeId::SceneChange);
    updateEventTypeBatchThreaded(EventTypeId::NPCSpawn);
    updateEventTypeBatchThreaded(EventTypeId::ParticleEffect);
    updateEventTypeBatchThreaded(EventTypeId::ResourceChange);
    updateEventTypeBatchThreaded(EventTypeId::World);
    updateEventTypeBatchThreaded(EventTypeId::Camera);
    updateEventTypeBatchThreaded(EventTypeId::Harvest);
    updateEventTypeBatchThreaded(EventTypeId::Custom);
  } else {
    // Use single-threaded for small event counts (better performance)
    updateEventTypeBatch(EventTypeId::Weather);
    updateEventTypeBatch(EventTypeId::SceneChange);
    updateEventTypeBatch(EventTypeId::NPCSpawn);
    updateEventTypeBatch(EventTypeId::ParticleEffect);
    updateEventTypeBatch(EventTypeId::ResourceChange);
    updateEventTypeBatch(EventTypeId::World);
    updateEventTypeBatch(EventTypeId::Camera);
    updateEventTypeBatch(EventTypeId::Harvest);
    updateEventTypeBatch(EventTypeId::Custom);
  }

  // Simplified performance tracking - reduce lock contention
  // Drain deferred dispatch queue with budget after event updates
  drainDispatchQueueWithBudget();

  auto endTime = getCurrentTimeNanos();
  double totalTimeMs = (endTime - startTime) / 1000000.0;

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
                               EventTypeId::Weather);
}

bool EventManager::registerSceneChangeEvent(
    const std::string &name, std::shared_ptr<SceneChangeEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::SceneChange);
}

bool EventManager::registerNPCSpawnEvent(const std::string &name,
                                         std::shared_ptr<NPCSpawnEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::NPCSpawn);
}

bool EventManager::registerResourceChangeEvent(
    const std::string &name, std::shared_ptr<ResourceChangeEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::ResourceChange);
}

bool EventManager::registerWorldEvent(const std::string &name,
                                      std::shared_ptr<WorldEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::World);
}

bool EventManager::registerCameraEvent(const std::string &name,
                                       std::shared_ptr<CameraEvent> event) {
  return registerEventInternal(name, std::static_pointer_cast<Event>(event),
                               EventTypeId::Camera);
}

bool EventManager::registerEventInternal(const std::string &name,
                                         EventPtr event, EventTypeId typeId) {
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
  eventData.name = name;
  eventData.typeId = typeId;
  eventData.setActive(true);
  eventData.priority = 0;

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
  // Also trigger any registered handlers for this event type
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers = m_handlersByType[static_cast<size_t>(typeId)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers),
                 [](const auto& h) { return h != nullptr; });
  }
  if (localHandlers.empty()) {
    // No handlers registered for this type: execute directly
    try { event->execute(); }
    catch (const std::exception &e) { EVENT_ERROR(std::string("Exception in executeEvent direct exec: ") + e.what()); }
    catch (...) { EVENT_ERROR("Unknown exception in executeEvent direct exec"); }
  } else {
    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.event = event;
    eventData.typeId = typeId;
    eventData.setActive(true);

    // Call all registered handlers (type)
    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in executeEvent '" + eventName + "': " +
                    std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in executeEvent '" + eventName + "'");
      }
    }
  }
  // Per-name handlers
  {
    std::vector<FastEventHandler> nh;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(eventName);
      if (it != m_nameHandlers.end()) { 
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh),
                     [](const auto& h) { return h != nullptr; });
      }
    }
    if (!nh.empty()) {
      EventData ed; ed.event = event; ed.typeId = typeId; ed.name = eventName; ed.setActive(true);
      for (const auto &h : nh) {
        try { h(ed); }
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
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  const size_t idx = static_cast<size_t>(typeId);
  m_handlersByType[idx].clear();
  m_handlerIdsByType[idx].clear();
}

void EventManager::clearAllHandlers() {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  for (size_t i = 0; i < m_handlersByType.size(); ++i) {
    m_handlersByType[i].clear();
    m_handlerIdsByType[i].clear();
  }
  m_nameHandlers.clear();
  m_nameHandlerIds.clear();
  EVENT_INFO("All event handlers cleared");
}

size_t EventManager::getHandlerCount(EventTypeId typeId) const {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  return m_handlersByType[static_cast<size_t>(typeId)].size();
}

EventManager::HandlerToken
EventManager::registerHandlerWithToken(EventTypeId typeId, FastEventHandler handler) {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  const size_t idx = static_cast<size_t>(typeId);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);
  m_handlersByType[idx].push_back(std::move(handler));
  m_handlerIdsByType[idx].push_back(id);
  return HandlerToken{typeId, id, false, {}};
}

bool EventManager::removeHandler(const HandlerToken &token) {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  if (token.forName) {
    auto it = m_nameHandlerIds.find(token.name);
    if (it == m_nameHandlerIds.end()) return false;
    auto &ids = it->second;
    auto &handlers = m_nameHandlers[token.name];
    for (size_t i = 0; i < ids.size(); ++i) {
      if (ids[i] == token.id) { handlers[i] = nullptr; ids[i] = 0; return true; }
    }
    return false;
  } else {
    const size_t idx = static_cast<size_t>(token.typeId);
    if (idx >= m_handlersByType.size()) return false;
    auto &ids = m_handlerIdsByType[idx];
    auto &handlers = m_handlersByType[idx];
    auto it = std::find(ids.begin(), ids.end(), token.id);
    if (it != ids.end()) {
      size_t index = std::distance(ids.begin(), it);
      handlers[index] = nullptr;
      *it = 0;
      return true;
    }
    return false;
  }
}

EventManager::HandlerToken
EventManager::registerHandlerForName(const std::string &name, FastEventHandler handler) {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  uint64_t id = m_nextHandlerId.fetch_add(1, std::memory_order_relaxed);
  auto &vec = m_nameHandlers[name];
  auto &ids = m_nameHandlerIds[name];
  vec.push_back(std::move(handler));
  ids.push_back(id);
  return HandlerToken{EventTypeId::Custom, id, true, name};
}

void EventManager::updateEventTypeBatch(EventTypeId typeId) {
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
  for (auto &evt : localEvents) { evt->update(); }

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
  if (!HammerEngine::ThreadSystem::Exists()) {
    // Fall back to single-threaded if ThreadSystem not available
    updateEventTypeBatch(typeId);
    return;
  }

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  auto startTime = getCurrentTimeNanos();

  // Copy events to local vector to minimize lock time
  // Note: use an automatic storage vector here since work is dispatched to
  // worker threads and capturing a thread_local by reference is unsafe and
  // triggers compiler warnings.
  std::vector<EventPtr> localEvents;
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    const auto &container = m_eventsByType[static_cast<size_t>(typeId)];

    if (container.empty()) {
      return;
    }

    localEvents.clear();
    localEvents.reserve(container.size());
    for (const auto &ed : container) {
      if (ed.isActive() && ed.event) localEvents.push_back(ed.event);
    }
  }

  if (localEvents.empty()) {
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
  size_t pressureThreshold = (queueCapacity * 9) / 10; // 90% capacity threshold

  if (queueSize > pressureThreshold) {
    // Graceful degradation: fallback to single-threaded processing
    EVENT_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                std::to_string(queueCapacity) +
                "), using single-threaded processing");
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    for (auto &evt : localEvents) { evt->update(); }

    // Record performance and return early
    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;
    if (timeMs > 1.0 || localEvents.size() > 50) {
      recordPerformance(typeId, timeMs);
    }
    return;
  }

  // Use buffer capacity for high workloads
  size_t optimalWorkerCount =
      budget.getOptimalWorkerCount(eventWorkerBudget, localEvents.size(), 100);

  // Store optimal worker count and mark as threaded
  m_lastOptimalWorkerCount.store(optimalWorkerCount, std::memory_order_relaxed);
  m_lastWasThreaded.store(true, std::memory_order_relaxed);

  // Dynamic batch sizing based on queue pressure for optimal performance
  size_t minEventsPerBatch = 10;
  size_t maxBatches = 4;

  // Adjust batch strategy based on queue pressure
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;
  if (queuePressure > 0.5) {
    // High pressure: use fewer, larger batches to reduce queue overhead
    minEventsPerBatch = 15;
    maxBatches = 2;
    EVENT_DEBUG("High queue pressure (" +
                std::to_string(static_cast<int>(queuePressure * 100)) +
                "%), using larger batches");
  } else if (queuePressure < 0.25) {
    // Low pressure: can use more batches for better parallelization
    minEventsPerBatch = 8;
    maxBatches = 4;
  }

  // Simple batch processing without complex spin-wait
  if (optimalWorkerCount > 1 && localEvents.size() > 20) {
  size_t batchCount =
      std::min(optimalWorkerCount, localEvents.size() / minEventsPerBatch);
  batchCount = std::max(size_t(1), std::min(batchCount, maxBatches));

  // Debug thread allocation info periodically
  static uint64_t debugFrameCounter = 0;
  if (++debugFrameCounter % 300 == 0 && !localEvents.empty()) {
    EVENT_DEBUG("Event Thread Allocation - Workers: " +
                std::to_string(optimalWorkerCount) + "/" +
                std::to_string(availableWorkers) +
                ", Event Budget: " + std::to_string(eventWorkerBudget) +
                ", Batches: " + std::to_string(batchCount));
  }

    size_t batchSize = localEvents.size() / batchCount;
    size_t remainingEvents = localEvents.size() % batchCount;

    std::vector<std::future<void>> futures;
    futures.reserve(batchCount);

    // Submit optimized batches using futures for simpler synchronization
    for (size_t i = 0; i < batchCount; ++i) {
      size_t start = i * batchSize;
      size_t end = start + batchSize;

      // Add remaining events to last batch
      if (i == batchCount - 1) {
        end += remainingEvents;
      }

      futures.push_back(threadSystem.enqueueTaskWithResult(
          [&localEvents, start, end]() {
            for (size_t j = start; j < end; ++j) {
              localEvents[j]->update();
            }
          },
          HammerEngine::TaskPriority::Normal, "Event_OptimalBatch"));
    }

    // Wait for all batches to complete
    for (auto &future : futures) {
      future.wait();
    }
  } else {
    // Process single-threaded for small event counts
    for (auto &evt : localEvents) { evt->update(); }
  }

  // Simplified performance recording
  auto endTime = getCurrentTimeNanos();
  double timeMs = (endTime - startTime) / 1000000.0;

  if (timeMs > 1.0 || localEvents.size() > 50) {
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
  // Fast check for handler existence
  size_t handlerCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Weather)].size();
  }
  // Build payload (use pool)
  std::shared_ptr<WeatherEvent> weatherEvent = m_weatherPool.acquire();
  if (!weatherEvent) weatherEvent = std::make_shared<WeatherEvent>("trigger_weather", weatherType);
  else weatherEvent->setWeatherType(weatherType);
  WeatherParams params = weatherEvent->getWeatherParams();
  params.transitionTime = transitionTime;
  weatherEvent->setWeatherParams(params);

  if (handlerCount == 0) {
    // Fallback: execute directly
    try { weatherEvent->execute(); } catch (...) {}
    m_weatherPool.release(weatherEvent);
    return true;
  }

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers =
          m_handlersByType[static_cast<size_t>(EventTypeId::Weather)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers),
                   [](const auto& h) { return h != nullptr; });
    }

    EventData eventData;
    eventData.typeId = EventTypeId::Weather;
    eventData.setActive(true);
    eventData.event = weatherEvent;
    eventData.name = "trigger_weather";
    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in changeWeather: " + std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in changeWeather");
      }
    }
     // Name-based handlers
     {
       std::vector<FastEventHandler> nh;
       {
         std::lock_guard<std::mutex> lock(m_handlersMutex);
         auto it = m_nameHandlers.find(eventData.name);
         if (it != m_nameHandlers.end()) { 
           std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
         }
       }
      for (const auto &h: nh) { try { h(eventData);} catch(...){} }
    }
    m_weatherPool.release(weatherEvent);
    (void)weatherType;
    (void)transitionTime;
    return !localHandlers.empty();
  }

  // Deferred: enqueue dispatch and return
  EventData data;
  data.typeId = EventTypeId::Weather;
  data.setActive(true);
  data.event = weatherEvent;
  data.name = "trigger_weather";
  data.onConsumed = [this, weatherEvent]() { m_weatherPool.release(weatherEvent); };
  enqueueDispatch(EventTypeId::Weather, data);
  (void)weatherType;
  (void)transitionTime;
  return true;
}

bool EventManager::changeScene(const std::string &sceneId,
                               const std::string &transitionType,
                               float transitionTime,
                               DispatchMode mode) const {
  size_t handlerCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::SceneChange)].size();
  }

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

  if (handlerCount == 0) {
    try { sceneEvent->execute(); } catch (...) {}
    m_sceneChangePool.release(sceneEvent);
    return true;
  }

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers =
          m_handlersByType[static_cast<size_t>(EventTypeId::SceneChange)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    EventData eventData;
    eventData.typeId = EventTypeId::SceneChange;
    eventData.setActive(true);
    eventData.event = sceneEvent;
    eventData.name = "trigger_scene_change";
    for (const auto &handler : localHandlers) {
      try { handler(eventData); }
      catch (const std::exception &e) { EVENT_ERROR("Handler exception in changeScene: " + std::string(e.what())); }
       catch (...) { EVENT_ERROR("Unknown handler exception in changeScene"); }
     }
     {
       std::vector<FastEventHandler> nh;
       {
         std::lock_guard<std::mutex> lock(m_handlersMutex);
         auto it = m_nameHandlers.find(eventData.name);
         if (it != m_nameHandlers.end()) { 
           std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
         }
       }
      for (const auto &h: nh) { try { h(eventData);} catch(...){} }
    }
    m_sceneChangePool.release(sceneEvent);
    (void)sceneId; (void)transitionType; (void)transitionTime; return !localHandlers.empty();
  }

  EventData data;
  data.typeId = EventTypeId::SceneChange;
  data.setActive(true);
  data.event = sceneEvent;
  data.name = "trigger_scene_change";
  data.onConsumed = [this, sceneEvent]() { m_sceneChangePool.release(sceneEvent); };
  enqueueDispatch(EventTypeId::SceneChange, data);
  (void)sceneId; (void)transitionType; (void)transitionTime; return true;
}

bool EventManager::spawnNPC(const std::string &npcType, float x,
                            float y, DispatchMode mode) const {
  size_t handlerCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::NPCSpawn)].size();
  }
  if (handlerCount == 0) {
    auto ev = m_npcSpawnPool.acquire();
    if (!ev) ev = std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", npcType);
    else {
      SpawnParameters p = ev->getSpawnParameters();
      p.npcType = npcType;
      ev->setSpawnParameters(p);
    }
    ev->addSpawnPoint(x, y);
    try { ev->execute(); } catch (...) {}
    m_npcSpawnPool.release(ev);
    return true;
  }

  // Build payload
  SpawnParameters params(npcType, 1, 0.0f);
  auto npcEvent = m_npcSpawnPool.acquire();
  if (!npcEvent) npcEvent = std::make_shared<NPCSpawnEvent>("trigger_npc_spawn", params);
  else npcEvent->setSpawnParameters(params);
  npcEvent->addSpawnPoint(x, y);

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers =
          m_handlersByType[static_cast<size_t>(EventTypeId::NPCSpawn)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    EventData eventData;
    eventData.typeId = EventTypeId::NPCSpawn;
    eventData.setActive(true);
    eventData.event = npcEvent;
    eventData.name = "trigger_npc_spawn";
    for (const auto &handler : localHandlers) {
      try { handler(eventData); }
      catch (const std::exception &e) { EVENT_ERROR("Handler exception in spawnNPC: " + std::string(e.what())); }
       catch (...) { EVENT_ERROR("Unknown handler exception in spawnNPC"); }
     }
     {
       std::vector<FastEventHandler> nh;
       {
         std::lock_guard<std::mutex> lock(m_handlersMutex);
         auto it = m_nameHandlers.find(eventData.name);
         if (it != m_nameHandlers.end()) { 
           std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
         }
       }
      for (const auto &h: nh) { try { h(eventData);} catch(...){} }
    }
    m_npcSpawnPool.release(npcEvent);
    (void)npcType; (void)x; (void)y; return !localHandlers.empty();
  }

  EventData data;
  data.typeId = EventTypeId::NPCSpawn;
  data.setActive(true);
  data.event = npcEvent;
  data.name = "trigger_npc_spawn";
  data.onConsumed = [this, npcEvent]() { m_npcSpawnPool.release(npcEvent); };
  enqueueDispatch(EventTypeId::NPCSpawn, data);
  (void)npcType; (void)x; (void)y; return true;
}

bool EventManager::triggerParticleEffect(const std::string &effectName, float x, float y,
                                         float intensity, float duration,
                                         const std::string &groupTag,
                                         DispatchMode mode) const {
  // Fast check
  size_t handlerCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::ParticleEffect)].size();
  }
  if (handlerCount == 0) {
    // Fallback: execute effect directly
    try {
      auto pe = std::make_shared<ParticleEffectEvent>(
          "trigger_particle_effect",
          ParticleEffectEvent::stringToEffectType(effectName),
          x, y, intensity, duration, groupTag, "");
      pe->execute();
    } catch (...) {}
    return true;
  }

  ParticleEffectType effectType = ParticleEffectEvent::stringToEffectType(effectName);
  auto pe = std::make_shared<ParticleEffectEvent>("trigger_particle_effect", effectType,
                                                  x, y, intensity, duration, groupTag, "");
  EventData data;
  data.typeId = EventTypeId::ParticleEffect;
  data.setActive(true);
  data.event = pe;
  data.name = "trigger_particle_effect";

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::ParticleEffect)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) {
      try { h(data); } catch (const std::exception &e) { EVENT_ERROR("Handler exception in triggerParticleEffect: " + std::string(e.what())); }
      catch (...) { EVENT_ERROR("Unknown handler exception in triggerParticleEffect"); }
    }
    // Name-based
    {
      std::vector<FastEventHandler> nh;
      {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        auto it = m_nameHandlers.find(data.name);
        if (it != m_nameHandlers.end()) { 
          std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
        }
      }
      for (const auto &h : nh) { try { h(data);} catch(...){} }
    }
    return !localHandlers.empty();
  }

  enqueueDispatch(EventTypeId::ParticleEffect, data);
  return true;
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
  size_t handlerCount = 0;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::ResourceChange)].size();
  }
  if (handlerCount == 0) { return false; }

  // Build EventData with concrete ResourceChangeEvent payload
  EventData eventData;
  eventData.typeId = EventTypeId::ResourceChange;
  eventData.setActive(true);
  eventData.event = std::make_shared<ResourceChangeEvent>(
      owner, resourceHandle, oldQuantity, newQuantity, changeReason);
  eventData.name = "trigger_resource_change";

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers =
          m_handlersByType[static_cast<size_t>(EventTypeId::ResourceChange)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &handler : localHandlers) {
      try { handler(eventData); }
      catch (const std::exception &e) { EVENT_ERROR("Handler exception in triggerResourceChange: " + std::string(e.what())); }
       catch (...) { EVENT_ERROR("Unknown handler exception in triggerResourceChange"); }
     }
     {
       std::vector<FastEventHandler> nh;
       {
         std::lock_guard<std::mutex> lock(m_handlersMutex);
         auto it = m_nameHandlers.find(eventData.name);
         if (it != m_nameHandlers.end()) { 
           std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
         }
       }
      for (const auto &h : nh) { try { h(eventData);} catch(...){} }
    }
    return !localHandlers.empty();
  }

  enqueueDispatch(EventTypeId::ResourceChange, eventData);
  return true;
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
  if (!event) {
    return false;
  }

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
  auto ev = std::make_shared<WorldLoadedEvent>(worldId, width, height);
  EventData data; data.typeId = EventTypeId::World; data.setActive(true); data.event = ev; data.name = "trigger_world_loaded";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::World)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::World)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerWorldLoaded"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::World, data); return true;
}

bool EventManager::triggerWorldUnloaded(const std::string &worldId, DispatchMode mode) const {
  auto ev = std::make_shared<WorldUnloadedEvent>(worldId);
  EventData data; data.typeId = EventTypeId::World; data.setActive(true); data.event = ev; data.name = "trigger_world_unloaded";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::World)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::World)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerWorldUnloaded"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::World, data); return true;
}

bool EventManager::triggerTileChanged(int x, int y, const std::string &changeType, DispatchMode mode) const {
  auto ev = std::make_shared<TileChangedEvent>(x, y, changeType);
  EventData data; data.typeId = EventTypeId::World; data.setActive(true); data.event = ev; data.name = "trigger_tile_changed";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::World)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::World)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerTileChanged"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::World, data); return true;
}

bool EventManager::triggerWorldGenerated(const std::string &worldId, int width, int height,
                                         float generationTime, DispatchMode mode) const {
  auto ev = std::make_shared<WorldGeneratedEvent>(worldId, width, height, generationTime);
  EventData data; data.typeId = EventTypeId::World; data.setActive(true); data.event = ev; data.name = "trigger_world_generated";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::World)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::World)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerWorldGenerated"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::World, data); return true;
}

PerformanceStats EventManager::getPerformanceStats(EventTypeId typeId) const {
  std::lock_guard<std::mutex> lock(m_perfMutex);
  return m_performanceStats[static_cast<size_t>(typeId)];
}

void EventManager::resetPerformanceStats() {
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

  for (size_t typeIdx = 0; typeIdx < m_eventsByType.size(); ++typeIdx) {
    const auto &container = m_eventsByType[typeIdx];
    for (size_t i = 0; i < container.size(); ++i) {
      const auto &ed = container[i];
      if (!ed.name.empty()) {
        m_nameToIndex[ed.name] = i;
        m_nameToType[ed.name] = static_cast<EventTypeId>(typeIdx);
      }
    }
  }
}

void EventManager::clearEventPools() {
  m_weatherPool.clear();
  m_sceneChangePool.clear();
  m_npcSpawnPool.clear();
  m_resourceChangePool.clear();
  m_worldPool.clear();
  m_cameraPool.clear();
}

void EventManager::clearAllEvents() {
    std::lock_guard<std::shared_mutex> lock(m_eventsMutex);
    for (auto& event_list : m_eventsByType) {
        event_list.clear();
    }
    m_nameToIndex.clear();
    m_nameToType.clear();
}

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
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  m_nameHandlers.erase(name);
  m_nameHandlerIds.erase(name);
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

  const uint64_t start = getCurrentTimeNanos();
  uint64_t timeBudgetNs = 2'000'000; // ~2ms

  std::vector<PendingDispatch> local;
  local.reserve(maxToProcess);
  {
    std::lock_guard<std::mutex> lock(m_dispatchMutex);
    const size_t backlog = m_pendingDispatch.size();
    if (backlog > m_maxDispatchQueue / 2) {
      maxToProcess += std::min(backlog / 64, static_cast<size_t>(256));
      timeBudgetNs += 500'000; // +0.5ms under heavy backlog
    }
    size_t toTake = std::min(maxToProcess, m_pendingDispatch.size());
    for (size_t i = 0; i < toTake; ++i) {
      local.push_back(std::move(m_pendingDispatch.front()));
      m_pendingDispatch.pop_front();
    }
  }

  for (const auto &pd : local) {
    // Copy handlers locally per type
    std::vector<FastEventHandler> handlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &src = m_handlersByType[static_cast<size_t>(pd.typeId)];
      handlers.reserve(src.size());
      std::copy_if(src.begin(), src.end(), std::back_inserter(handlers), [](const auto& h) { return static_cast<bool>(h); });
    }

    for (const auto &handler : handlers) {
      try { handler(pd.data); }
      catch (const std::exception &e) { EVENT_ERROR("Handler exception in deferred dispatch: " + std::string(e.what())); }
      catch (...) { EVENT_ERROR("Unknown handler exception in deferred dispatch"); }
    }

    // Per-name handlers
    if (!pd.data.name.empty()) {
      std::vector<FastEventHandler> nameHandlers;
      {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        auto it = m_nameHandlers.find(pd.data.name);
        if (it != m_nameHandlers.end()) {
          nameHandlers.reserve(it->second.size());
          std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nameHandlers), [](const auto& h) { return static_cast<bool>(h); });
        }
      }
      for (const auto &h : nameHandlers) {
        try { h(pd.data); }
        catch (const std::exception &e) { EVENT_ERROR("Name-handler exception in deferred dispatch: " + std::string(e.what())); }
        catch (...) { EVENT_ERROR("Unknown name-handler exception in deferred dispatch"); }
      }
    }

    if (pd.data.onConsumed) { pd.data.onConsumed(); }

    if ((getCurrentTimeNanos() - start) > timeBudgetNs) {
      break; // Defer remaining to next frame
    }
  }
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
  auto ev = std::make_shared<CameraMovedEvent>(newPos, oldPos);
  EventData data; data.typeId = EventTypeId::Camera; data.setActive(true); data.event = ev; data.name = "trigger_camera_moved";

  size_t handlerCount = 0; {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)].size();
  }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }

  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)];
      localHandlers.reserve(handlers.size());
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data); } catch (...) { EVENT_ERROR("Handler exception in triggerCameraMoved"); } }
    std::vector<FastEventHandler> nh;
    {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name);
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::Camera, data); return true;
}

bool EventManager::triggerCameraModeChanged(int newMode, int oldMode, DispatchMode mode) const {
  auto ev = std::make_shared<CameraModeChangedEvent>(
      static_cast<CameraModeChangedEvent::Mode>(newMode),
      static_cast<CameraModeChangedEvent::Mode>(oldMode));
  EventData data; data.typeId = EventTypeId::Camera; data.setActive(true); data.event = ev; data.name = "trigger_camera_mode_changed";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerCameraModeChanged"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::Camera, data); return true;
}

bool EventManager::triggerCameraShakeStarted(float duration, float intensity, DispatchMode mode) const {
  auto ev = std::make_shared<CameraShakeStartedEvent>(duration, intensity);
  EventData data; data.typeId = EventTypeId::Camera; data.setActive(true); data.event = ev; data.name = "trigger_camera_shake_started";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerCameraShakeStarted"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::Camera, data); return true;
}

bool EventManager::triggerCameraShakeEnded(DispatchMode mode) const {
  auto ev = std::make_shared<CameraShakeEndedEvent>();
  EventData data; data.typeId = EventTypeId::Camera; data.setActive(true); data.event = ev; data.name = "trigger_camera_shake_ended";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerCameraShakeEnded"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::Camera, data); return true;
}

bool EventManager::triggerCameraTargetChanged(std::weak_ptr<Entity> newTarget,
                                              std::weak_ptr<Entity> oldTarget,
                                              DispatchMode mode) const {
  auto ev = std::make_shared<CameraTargetChangedEvent>(newTarget, oldTarget);
  EventData data; data.typeId = EventTypeId::Camera; data.setActive(true); data.event = ev; data.name = "trigger_camera_target_changed";
  size_t handlerCount = 0; { std::lock_guard<std::mutex> lock(m_handlersMutex);
    handlerCount = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)].size(); }
  if (handlerCount == 0) { try { ev->execute(); } catch (...) {} return true; }
  if (mode == DispatchMode::Immediate) {
    std::vector<FastEventHandler> localHandlers; {
      std::lock_guard<std::mutex> lock(m_handlersMutex);
      const auto &handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Camera)];
      localHandlers.reserve(handlers.size()); 
      std::copy_if(handlers.begin(), handlers.end(), std::back_inserter(localHandlers), [](const auto& h) { return static_cast<bool>(h); });
    }
    for (const auto &h : localHandlers) { try { h(data);} catch (...) { EVENT_ERROR("Handler exception in triggerCameraTargetChanged"); } }
    std::vector<FastEventHandler> nh; { std::lock_guard<std::mutex> lock(m_handlersMutex);
      auto it = m_nameHandlers.find(data.name); 
      if (it != m_nameHandlers.end()) {
        std::copy_if(it->second.begin(), it->second.end(), std::back_inserter(nh), [](const auto& h) { return static_cast<bool>(h); });
      }
    }
    for (const auto &h: nh) { try { h(data);} catch(...){} }
    return !localHandlers.empty();
  }
  enqueueDispatch(EventTypeId::Camera, data); return true;
}
