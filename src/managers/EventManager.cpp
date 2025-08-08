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
#include "events/HarvestResourceEvent.hpp"
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
  if (m_threadingEnabled.load() && getEventCount() > 50) {
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
  auto endTime = getCurrentTimeNanos();
  double totalTimeMs = (endTime - startTime) / 1000000.0;

  // Only log severe performance issues (>10ms) to reduce noise
  if (totalTimeMs > 10.0) {
    EVENT_DEBUG("EventManager update took " + std::to_string(totalTimeMs) +
                "ms (slow frame)");
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
  eventData.typeId = typeId;
  eventData.setActive(true);
  eventData.lastUpdateTime = 0.0f;
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
  EventTypeId typeId = EventTypeId::Custom;

  if (typeName == "Weather")
    typeId = EventTypeId::Weather;
  else if (typeName == "SceneChange")
    typeId = EventTypeId::SceneChange;
  else if (typeName == "NPCSpawn")
    typeId = EventTypeId::NPCSpawn;
  else if (typeName == "ParticleEffect")
    typeId = EventTypeId::ParticleEffect;
  else if (typeName == "ResourceChange")
    typeId = EventTypeId::ResourceChange;
  else if (typeName == "World")
    typeId = EventTypeId::World;
  else if (typeName == "Camera")
    typeId = EventTypeId::Camera;
  else if (typeName == "Harvest")
    typeId = EventTypeId::Harvest;

  return getEventsByType(typeId);
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
  EventTypeId typeId = getEventTypeId(event);
  
  // Execute the event itself
  event->execute();
  
  // Also trigger any registered handlers for this event type
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers = m_handlersByType[static_cast<size_t>(typeId)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(),
                 std::back_inserter(localHandlers),
                 [](const auto &handler) { return handler != nullptr; });
  }
  
  if (!localHandlers.empty()) {
    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.event = event;
    eventData.typeId = typeId;
    eventData.setActive(true);
    
    // Call all registered handlers
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
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  auto &handlers = m_handlersByType[static_cast<size_t>(typeId)];

  handlers.push_back(std::move(handler));
}

void EventManager::removeHandlers(EventTypeId typeId) {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  m_handlersByType[static_cast<size_t>(typeId)].clear();
}

void EventManager::clearAllHandlers() {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  for (auto &handlerContainer : m_handlersByType) {
    handlerContainer.clear();
  }
  EVENT_INFO("All event handlers cleared");
}

size_t EventManager::getHandlerCount(EventTypeId typeId) const {
  std::lock_guard<std::mutex> lock(m_handlersMutex);
  return m_handlersByType[static_cast<size_t>(typeId)].size();
}

void EventManager::updateEventTypeBatch(EventTypeId typeId) {
  auto startTime = getCurrentTimeNanos();

  // Copy events to local vector to minimize lock time
  std::vector<EventData> localEvents;
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    auto &container = m_eventsByType[static_cast<size_t>(typeId)];
    localEvents.reserve(container.size());

    // Quick copy of active events using STL algorithm
    std::copy_if(container.begin(), container.end(),
                 std::back_inserter(localEvents), [](const auto &eventData) {
                   return eventData.isActive() && eventData.event;
                 });
  }

  // Process events without holding lock
  for (auto &eventData : localEvents) {
    if (eventData.event) {
      eventData.event->update();
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
  if (!HammerEngine::ThreadSystem::Exists()) {
    // Fall back to single-threaded if ThreadSystem not available
    updateEventTypeBatch(typeId);
    return;
  }

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  auto startTime = getCurrentTimeNanos();

  // Copy events to local vector to minimize lock time
  std::vector<EventData> localEvents;
  {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    auto &container = m_eventsByType[static_cast<size_t>(typeId)];

    if (container.empty()) {
      return;
    }

    localEvents.reserve(container.size());
    std::copy_if(container.begin(), container.end(),
                 std::back_inserter(localEvents), [](const auto &eventData) {
                   return eventData.isActive() && eventData.event;
                 });
  }

  if (localEvents.empty()) {
    return;
  }

  // Proper WorkerBudget calculation with architectural respect
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t eventWorkerBudget = budget.eventAllocated;

  // Check queue pressure before submitting tasks
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  size_t pressureThreshold = (queueCapacity * 9) / 10; // 90% capacity threshold

  if (queueSize > pressureThreshold) {
    // Graceful degradation: fallback to single-threaded processing
    EVENT_DEBUG("Queue pressure detected (" + std::to_string(queueSize) + "/" +
                std::to_string(queueCapacity) +
                "), using single-threaded processing");
    for (auto &eventData : localEvents) {
      if (eventData.event) {
        eventData.event->update();
      }
    }

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
              if (localEvents[j].event) {
                localEvents[j].event->update();
              }
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
    for (auto &eventData : localEvents) {
      if (eventData.event) {
        eventData.event->update();
      }
    }
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
                                 float transitionTime) const {
  // Copy handlers to avoid holding lock during execution
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers =
        m_handlersByType[static_cast<size_t>(EventTypeId::Weather)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(),
                 std::back_inserter(localHandlers),
                 [](const auto &handler) { return handler != nullptr; });
  }

  if (!localHandlers.empty()) {
    EVENT_INFO("Triggering weather change to: " + weatherType +
               " (transition: " + std::to_string(transitionTime) + "s)");

    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.typeId = EventTypeId::Weather;
    eventData.setActive(true);

    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in changeWeather: " +
                    std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in changeWeather");
      }
    }
  }

  (void)weatherType;
  (void)transitionTime; // Suppress unused warnings
  return !localHandlers.empty();
}

bool EventManager::changeScene(const std::string &sceneId,
                               const std::string &transitionType,
                               float transitionTime) const {
  // Copy handlers to avoid holding lock during execution
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers =
        m_handlersByType[static_cast<size_t>(EventTypeId::SceneChange)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(),
                 std::back_inserter(localHandlers),
                 [](const auto &handler) { return handler != nullptr; });
  }

  if (!localHandlers.empty()) {
    EVENT_INFO("Triggering scene change to: " + sceneId +
               " (transition: " + transitionType +
               ", duration: " + std::to_string(transitionTime) + "s)");

    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.typeId = EventTypeId::SceneChange;
    eventData.setActive(true);

    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in changeScene: " +
                    std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in changeScene");
      }
    }
  }

  (void)sceneId;
  (void)transitionType;
  (void)transitionTime; // Suppress unused warnings
  return !localHandlers.empty();
}

bool EventManager::spawnNPC(const std::string &npcType, float x,
                            float y) const {
  // Copy handlers to avoid holding lock during execution
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers =
        m_handlersByType[static_cast<size_t>(EventTypeId::NPCSpawn)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(),
                 std::back_inserter(localHandlers),
                 [](const auto &handler) { return handler != nullptr; });
  }

  if (!localHandlers.empty()) {
    EVENT_INFO("Triggering NPC spawn: " + npcType + " at (" +
               std::to_string(x) + ", " + std::to_string(y) + ")");

    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.typeId = EventTypeId::NPCSpawn;
    eventData.setActive(true);

    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in spawnNPC: " + std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in spawnNPC");
      }
    }
  }

  (void)npcType;
  (void)x;
  (void)y; // Suppress unused warnings
  return !localHandlers.empty();
}

bool EventManager::triggerResourceChange(
    EntityPtr owner, HammerEngine::ResourceHandle resourceHandle,
    int oldQuantity, int newQuantity, const std::string &changeReason) const {
  // Copy handlers to avoid holding lock during execution
  std::vector<FastEventHandler> localHandlers;
  {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    const auto &handlers =
        m_handlersByType[static_cast<size_t>(EventTypeId::ResourceChange)];
    localHandlers.reserve(handlers.size());
    std::copy_if(handlers.begin(), handlers.end(),
                 std::back_inserter(localHandlers),
                 [](const auto &handler) { return handler != nullptr; });
  }

  if (!localHandlers.empty()) {
    EVENT_INFO(
        "Triggering resource change for handle: " + resourceHandle.toString() +
        " from " + std::to_string(oldQuantity) + " to " +
        std::to_string(newQuantity) + " (reason: " + changeReason + ")");

    // Create a temporary EventData for handler execution
    EventData eventData;
    eventData.typeId = EventTypeId::ResourceChange;
    eventData.setActive(true);
    // Create a temporary ResourceChangeEvent for the handler
    eventData.event = std::make_shared<ResourceChangeEvent>(
        owner, resourceHandle, oldQuantity, newQuantity, changeReason);

    for (const auto &handler : localHandlers) {
      try {
        handler(eventData);
      } catch (const std::exception &e) {
        EVENT_ERROR("Handler exception in triggerResourceChange: " +
                    std::string(e.what()));
      } catch (...) {
        EVENT_ERROR("Unknown handler exception in triggerResourceChange");
      }
    }
  }

  return !localHandlers.empty();
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
      // This is a simplified rebuild - in practice, you'd need to store names
      // For now, just update the type mapping
      // m_nameToIndex[eventName] = i;
      // m_nameToType[eventName] = typeId;
    }
  }
}

void EventManager::clearEventPools() {
  m_weatherPool.clear();
  m_sceneChangePool.clear();
  m_npcSpawnPool.clear();
  m_resourceChangePool.clear();
}

void EventManager::clearAllEvents() {
    std::lock_guard<std::shared_mutex> lock(m_eventsMutex);
    for (auto& event_list : m_eventsByType) {
        event_list.clear();
    }
    m_nameToIndex.clear();
    m_nameToType.clear();
}

EventTypeId EventManager::getEventTypeId(const EventPtr &event) const {
  if (std::dynamic_pointer_cast<WeatherEvent>(event)) {
    return EventTypeId::Weather;
  }
  if (std::dynamic_pointer_cast<SceneChangeEvent>(event)) {
    return EventTypeId::SceneChange;
  }
  if (std::dynamic_pointer_cast<NPCSpawnEvent>(event)) {
    return EventTypeId::NPCSpawn;
  }
  if (std::dynamic_pointer_cast<ParticleEffectEvent>(event)) {
    return EventTypeId::ParticleEffect;
  }
  if (std::dynamic_pointer_cast<ResourceChangeEvent>(event)) {
    return EventTypeId::ResourceChange;
  }
  if (std::dynamic_pointer_cast<WorldEvent>(event)) {
    return EventTypeId::World;
  }
  if (std::dynamic_pointer_cast<CameraEvent>(event)) {
    return EventTypeId::Camera;
  }
  if (std::dynamic_pointer_cast<HarvestResourceEvent>(event)) {
    return EventTypeId::Harvest;
  }
  return EventTypeId::Custom;
}

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
