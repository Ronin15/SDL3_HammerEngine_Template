/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/EventManager.hpp"
#include "core/Logger.hpp"
#include "events/Event.hpp"
#include "events/WeatherEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/EventFactory.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
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
    for (auto& eventContainer : m_eventsByType) {
        eventContainer.clear();
        eventContainer.reserve(256); // Pre-allocate for performance
    }

    // Initialize handler containers
    for (auto& handlerContainer : m_handlersByType) {
        handlerContainer.clear();
        handlerContainer.reserve(32);
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
        EVENT_INFO("EventManager initialized successfully with type-indexed storage");
    }
    return true;
}

void EventManager::clean() {
    if (!m_initialized.load()) {
        return;
    }

    // Only log if not in shutdown to avoid static destruction order issues
    if (!m_isShutdown) {
        EVENT_INFO("Cleaning up EventManager");
    }

    // Clear all events with proper cleanup
    {
        std::unique_lock<std::shared_mutex> lock(m_eventsMutex);
        for (auto& eventContainer : m_eventsByType) {
            // Ensure all events are properly cleaned up
            for (auto& eventData : eventContainer) {
                if (eventData.event) {
                    eventData.event.reset();  // Explicit cleanup
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
        for (auto& eventContainer : m_eventsByType) {
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
        updateEventTypeBatchThreaded(EventTypeId::Custom);
    } else {
        // Use single-threaded for small event counts (better performance)
        updateEventTypeBatch(EventTypeId::Weather);
        updateEventTypeBatch(EventTypeId::SceneChange);
        updateEventTypeBatch(EventTypeId::NPCSpawn);
        updateEventTypeBatch(EventTypeId::Custom);
    }

    // Simplified performance tracking - reduce lock contention
    auto endTime = getCurrentTimeNanos();
    double totalTimeMs = (endTime - startTime) / 1000000.0;

    // Only log severe performance issues (>10ms) to reduce noise
    if (totalTimeMs > 10.0) {
        EVENT_DEBUG("EventManager update took " + std::to_string(totalTimeMs) + "ms (slow frame)");
    }
    m_lastUpdateTime.store(endTime);
}

bool EventManager::registerEvent(const std::string& name, EventPtr event) {
    if (!event) {
        EVENT_ERROR("Cannot register null event with name: " + name);
        return false;
    }

    EventTypeId typeId = getEventTypeId(event);
    return registerEventInternal(name, event, typeId);
}

bool EventManager::registerWeatherEvent(const std::string& name, std::shared_ptr<WeatherEvent> event) {
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::Weather);
}

bool EventManager::registerSceneChangeEvent(const std::string& name, std::shared_ptr<SceneChangeEvent> event) {
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::SceneChange);
}

bool EventManager::registerNPCSpawnEvent(const std::string& name, std::shared_ptr<NPCSpawnEvent> event) {
    return registerEventInternal(name, std::static_pointer_cast<Event>(event), EventTypeId::NPCSpawn);
}

bool EventManager::registerEventInternal(const std::string& name, EventPtr event, EventTypeId typeId) {
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
    auto& container = m_eventsByType[static_cast<size_t>(typeId)];
    size_t index = container.size();
    container.push_back(std::move(eventData));

    // Update name mappings
    m_nameToIndex[name] = index;
    m_nameToType[name] = typeId;

    EVENT_INFO("Registered event '" + name + "' of type " + std::string(getEventTypeName(typeId)));
    return true;
}

EventPtr EventManager::getEvent(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    auto nameIt = m_nameToIndex.find(name);
    if (nameIt == m_nameToIndex.end()) {
        return nullptr;
    }

    auto typeIt = m_nameToType.find(name);
    if (typeIt == m_nameToType.end()) {
        return nullptr;
    }

    const auto& container = m_eventsByType[static_cast<size_t>(typeIt->second)];
    if (nameIt->second >= container.size()) {
        return nullptr;
    }

    return container[nameIt->second].event;
}

std::vector<EventPtr> EventManager::getEventsByType(EventTypeId typeId) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    const auto& container = m_eventsByType[static_cast<size_t>(typeId)];
    std::vector<EventPtr> result;
    result.reserve(container.size());

    for (const auto& eventData : container) {
        if (eventData.event) {
            result.push_back(eventData.event);
        }
    }

    return result;
}

std::vector<EventPtr> EventManager::getEventsByType(const std::string& typeName) const {
    EventTypeId typeId = EventTypeId::Custom;

    if (typeName == "Weather") typeId = EventTypeId::Weather;
    else if (typeName == "SceneChange") typeId = EventTypeId::SceneChange;
    else if (typeName == "NPCSpawn") typeId = EventTypeId::NPCSpawn;

    return getEventsByType(typeId);
}

bool EventManager::setEventActive(const std::string& name, bool active) {
    std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

    auto nameIt = m_nameToIndex.find(name);
    if (nameIt == m_nameToIndex.end()) {
        return false;
    }

    auto typeIt = m_nameToType.find(name);
    if (typeIt == m_nameToType.end()) {
        return false;
    }

    auto& container = m_eventsByType[static_cast<size_t>(typeIt->second)];
    if (nameIt->second >= container.size()) {
        return false;
    }

    container[nameIt->second].setActive(active);
    return true;
}

bool EventManager::isEventActive(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    auto nameIt = m_nameToIndex.find(name);
    if (nameIt == m_nameToIndex.end()) {
        return false;
    }

    auto typeIt = m_nameToType.find(name);
    if (typeIt == m_nameToType.end()) {
        return false;
    }

    const auto& container = m_eventsByType[static_cast<size_t>(typeIt->second)];
    if (nameIt->second >= container.size()) {
        return false;
    }

    return container[nameIt->second].isActive();
}

bool EventManager::removeEvent(const std::string& name) {
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
    auto& container = m_eventsByType[static_cast<size_t>(typeIt->second)];
    if (nameIt->second < container.size()) {
        container[nameIt->second].flags |= EventData::FLAG_PENDING_REMOVAL;
    }

    // Remove from name mappings
    m_nameToIndex.erase(nameIt);
    m_nameToType.erase(typeIt);

    return true;
}

bool EventManager::hasEvent(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    return m_nameToIndex.find(name) != m_nameToIndex.end();
}

bool EventManager::executeEvent(const std::string& eventName) const {
    auto event = getEvent(eventName);
    if (!event) {
        return false;
    }

    event->execute();
    return true;
}

int EventManager::executeEventsByType(EventTypeId typeId) const {
    auto events = getEventsByType(typeId);

    for (auto& event : events) {
        if (event) {
            event->execute();
        }
    }

    return static_cast<int>(events.size());
}

int EventManager::executeEventsByType(const std::string& eventType) const {
    EventTypeId typeId = EventTypeId::Custom;

    if (eventType == "Weather") typeId = EventTypeId::Weather;
    else if (eventType == "SceneChange") typeId = EventTypeId::SceneChange;
    else if (eventType == "NPCSpawn") typeId = EventTypeId::NPCSpawn;

    return executeEventsByType(typeId);
}

void EventManager::registerHandler(EventTypeId typeId, FastEventHandler handler) {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    auto& handlers = m_handlersByType[static_cast<size_t>(typeId)];

    handlers.push_back(std::move(handler));
}

void EventManager::removeHandlers(EventTypeId typeId) {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    m_handlersByType[static_cast<size_t>(typeId)].clear();
}

void EventManager::clearAllHandlers() {
    std::lock_guard<std::mutex> lock(m_handlersMutex);
    for (auto& handlerContainer : m_handlersByType) {
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
        auto& container = m_eventsByType[static_cast<size_t>(typeId)];
        localEvents.reserve(container.size());
        
        // Quick copy of active events
        for (const auto& eventData : container) {
            if (eventData.isActive() && eventData.event) {
                localEvents.push_back(eventData);
            }
        }
    }
    
    // Process events without holding lock
    for (auto& eventData : localEvents) {
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
        EVENT_DEBUG("Updated " + std::to_string(localEvents.size()) + " events of type " +
                    std::string(getEventTypeName(typeId)) + " in " + std::to_string(timeMs) + "ms (slow)");
    }
}

void EventManager::updateEventTypeBatchThreaded(EventTypeId typeId) {
    if (!Forge::ThreadSystem::Exists()) {
        // Fall back to single-threaded if ThreadSystem not available
        updateEventTypeBatch(typeId);
        return;
    }

    auto& threadSystem = Forge::ThreadSystem::Instance();
    auto startTime = getCurrentTimeNanos();

    // Copy events to local vector to minimize lock time
    std::vector<EventData> localEvents;
    {
        std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
        auto& container = m_eventsByType[static_cast<size_t>(typeId)];
        
        if (container.empty()) {
            return;
        }
        
        localEvents.reserve(container.size());
        for (const auto& eventData : container) {
            if (eventData.isActive() && eventData.event) {
                localEvents.push_back(eventData);
            }
        }
    }

    if (localEvents.empty()) {
        return;
    }

    // Proper WorkerBudget calculation with architectural respect
    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
    size_t eventWorkerBudget = budget.eventAllocated;

    // Use buffer capacity for high workloads
    size_t optimalWorkerCount = budget.getOptimalWorkerCount(eventWorkerBudget, localEvents.size(), 100);

    // Simple batch processing without complex spin-wait
    if (optimalWorkerCount > 1 && localEvents.size() > 20) {
        size_t batchSize = std::max(size_t(10), localEvents.size() / optimalWorkerCount);
        
        std::vector<std::future<void>> futures;
        futures.reserve(optimalWorkerCount);

        // Submit batches using futures for simpler synchronization
        for (size_t i = 0; i < localEvents.size(); i += batchSize) {
            size_t batchEnd = std::min(i + batchSize, localEvents.size());

            futures.push_back(threadSystem.enqueueTaskWithResult([&localEvents, i, batchEnd]() {
                for (size_t j = i; j < batchEnd; ++j) {
                    if (localEvents[j].event) {
                        localEvents[j].event->update();
                    }
                }
            }, Forge::TaskPriority::Normal, "Event_Batch"));
        }

        // Wait for all batches to complete
        for (auto& future : futures) {
            future.wait();
        }
    } else {
        // Process single-threaded for small event counts
        for (auto& eventData : localEvents) {
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

void EventManager::processEventDirect(EventData& eventData) {
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

bool EventManager::changeWeather(const std::string& weatherType, float transitionTime) const {
    // Copy handlers to avoid holding lock during execution
    std::vector<FastEventHandler> localHandlers;
    {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        const auto& handlers = m_handlersByType[static_cast<size_t>(EventTypeId::Weather)];
        localHandlers.reserve(handlers.size());
        for (const auto& handler : handlers) {
            if (handler) {
                localHandlers.push_back(handler);
            }
        }
    }

    if (!localHandlers.empty()) {
        EVENT_INFO("Triggering weather change to: " + weatherType + " (transition: " + std::to_string(transitionTime) + "s)");
        
        // Create a temporary EventData for handler execution
        EventData eventData;
        eventData.typeId = EventTypeId::Weather;
        eventData.setActive(true);

        for (const auto& handler : localHandlers) {
            try {
                handler(eventData);
            } catch (const std::exception& e) {
                EVENT_ERROR("Handler exception in changeWeather: " + std::string(e.what()));
            } catch (...) {
                EVENT_ERROR("Unknown handler exception in changeWeather");
            }
        }
    }

    (void)weatherType; (void)transitionTime; // Suppress unused warnings
    return !localHandlers.empty();
}

bool EventManager::changeScene(const std::string& sceneId, const std::string& transitionType, float transitionTime) const {
    // Copy handlers to avoid holding lock during execution
    std::vector<FastEventHandler> localHandlers;
    {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        const auto& handlers = m_handlersByType[static_cast<size_t>(EventTypeId::SceneChange)];
        localHandlers.reserve(handlers.size());
        for (const auto& handler : handlers) {
            if (handler) {
                localHandlers.push_back(handler);
            }
        }
    }

    if (!localHandlers.empty()) {
        EVENT_INFO("Triggering scene change to: " + sceneId + " (transition: " + transitionType + ", duration: " + std::to_string(transitionTime) + "s)");
        
        // Create a temporary EventData for handler execution
        EventData eventData;
        eventData.typeId = EventTypeId::SceneChange;
        eventData.setActive(true);

        for (const auto& handler : localHandlers) {
            try {
                handler(eventData);
            } catch (const std::exception& e) {
                EVENT_ERROR("Handler exception in changeScene: " + std::string(e.what()));
            } catch (...) {
                EVENT_ERROR("Unknown handler exception in changeScene");
            }
        }
    }

    (void)sceneId; (void)transitionType; (void)transitionTime; // Suppress unused warnings
    return !localHandlers.empty();
}

bool EventManager::spawnNPC(const std::string& npcType, float x, float y) const {
    // Copy handlers to avoid holding lock during execution
    std::vector<FastEventHandler> localHandlers;
    {
        std::lock_guard<std::mutex> lock(m_handlersMutex);
        const auto& handlers = m_handlersByType[static_cast<size_t>(EventTypeId::NPCSpawn)];
        localHandlers.reserve(handlers.size());
        for (const auto& handler : handlers) {
            if (handler) {
                localHandlers.push_back(handler);
            }
        }
    }

    if (!localHandlers.empty()) {
        EVENT_INFO("Triggering NPC spawn: " + npcType + " at (" + std::to_string(x) + ", " + std::to_string(y) + ")");
        
        // Create a temporary EventData for handler execution
        EventData eventData;
        eventData.typeId = EventTypeId::NPCSpawn;
        eventData.setActive(true);

        for (const auto& handler : localHandlers) {
            try {
                handler(eventData);
            } catch (const std::exception& e) {
                EVENT_ERROR("Handler exception in spawnNPC: " + std::string(e.what()));
            } catch (...) {
                EVENT_ERROR("Unknown handler exception in spawnNPC");
            }
        }
    }

    (void)npcType; (void)x; (void)y; // Suppress unused warnings
    return !localHandlers.empty();
}

bool EventManager::createWeatherEvent(const std::string& name, const std::string& weatherType, float intensity, float transitionTime) {
    // Cache EventFactory reference for better performance
    EventFactory& eventFactory = EventFactory::Instance();
    auto event = eventFactory.createWeatherEvent(name, weatherType, intensity, transitionTime);
    if (!event) {
        return false;
    }

    auto weatherEvent = std::dynamic_pointer_cast<WeatherEvent>(event);
    if (weatherEvent) {
        return registerWeatherEvent(name, weatherEvent);
    }

    return registerEvent(name, event);
}

bool EventManager::createSceneChangeEvent(const std::string& name, const std::string& targetScene, const std::string& transitionType, float transitionTime) {
    // Cache EventFactory reference for better performance
    EventFactory& eventFactory = EventFactory::Instance();
    auto event = eventFactory.createSceneChangeEvent(name, targetScene, transitionType, transitionTime);
    if (!event) {
        return false;
    }

    auto sceneEvent = std::dynamic_pointer_cast<SceneChangeEvent>(event);
    if (sceneEvent) {
        return registerSceneChangeEvent(name, sceneEvent);
    }

    return registerEvent(name, event);
}

bool EventManager::createNPCSpawnEvent(const std::string& name, const std::string& npcType, int count, float spawnRadius) {
    // Cache EventFactory reference for better performance
    EventFactory& eventFactory = EventFactory::Instance();
    auto event = eventFactory.createNPCSpawnEvent(name, npcType, count, spawnRadius);
    if (!event) {
        return false;
    }

    auto npcEvent = std::dynamic_pointer_cast<NPCSpawnEvent>(event);
    if (npcEvent) {
        return registerNPCSpawnEvent(name, npcEvent);
    }

    return registerEvent(name, event);
}

PerformanceStats EventManager::getPerformanceStats(EventTypeId typeId) const {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    return m_performanceStats[static_cast<size_t>(typeId)];
}

void EventManager::resetPerformanceStats() {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    for (auto& stats : m_performanceStats) {
        stats.reset();
    }
}

size_t EventManager::getEventCount() const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    size_t total = 0;
    for (const auto& container : m_eventsByType) {
        total += std::count_if(container.begin(), container.end(),
            [](const EventData& eventData) {
                return !(eventData.flags & EventData::FLAG_PENDING_REMOVAL);
            });
    }
    return total;
}

size_t EventManager::getEventCount(EventTypeId typeId) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    const auto& container = m_eventsByType[static_cast<size_t>(typeId)];
    return std::count_if(container.begin(), container.end(),
        [](const EventData& eventData) {
            return !(eventData.flags & EventData::FLAG_PENDING_REMOVAL);
        });
}

void EventManager::compactEventStorage() {
    std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

    for (auto& container : m_eventsByType) {
        // Remove events marked for deletion
        container.erase(
            std::remove_if(container.begin(), container.end(),
                [](const EventData& data) {
                    return data.flags & EventData::FLAG_PENDING_REMOVAL;
                }),
            container.end()
        );
    }

    // Rebuild name mappings after compaction
    m_nameToIndex.clear();
    m_nameToType.clear();

    for (size_t typeIdx = 0; typeIdx < m_eventsByType.size(); ++typeIdx) {
        const auto& container = m_eventsByType[typeIdx];

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
}

EventTypeId EventManager::getEventTypeId(const EventPtr& event) const {
    if (std::dynamic_pointer_cast<WeatherEvent>(event)) {
        return EventTypeId::Weather;
    }
    if (std::dynamic_pointer_cast<SceneChangeEvent>(event)) {
        return EventTypeId::SceneChange;
    }
    if (std::dynamic_pointer_cast<NPCSpawnEvent>(event)) {
        return EventTypeId::NPCSpawn;
    }
    return EventTypeId::Custom;
}

std::string EventManager::getEventTypeName(EventTypeId typeId) const {
    switch (typeId) {
        case EventTypeId::Weather: return "Weather";
        case EventTypeId::SceneChange: return "SceneChange";
        case EventTypeId::NPCSpawn: return "NPCSpawn";
        case EventTypeId::Custom: return "Custom";
        default: return "Unknown";
    }
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
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}
