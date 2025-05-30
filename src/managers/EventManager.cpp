/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "managers/EventManager.hpp"
#include "events/WeatherEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "core/ThreadSystem.hpp"
#include <algorithm>
#include <utility>
#include <thread>
#include <chrono>
#include <iostream>
#include <memory>

// Uncomment to enable debug logging for EventManager
// #define EVENT_DEBUG_LOGGING

// Constructor
EventManager::EventManager() {
    EVENT_LOG("Event Manager instance created");
}

// Destructor
EventManager::~EventManager() {
    clean();
    EVENT_LOG("Event Manager instance destroyed");
}

bool EventManager::init() {
    // Already initialized?
    if (m_initialized.exchange(true)) {
        EVENT_LOG("Event Manager already initialized, skipping");
        return true;
    }

    EVENT_LOG("Initializing Event Manager");

    // Initialize caches
    m_activeEventCache.clear();
    m_cacheValid.store(false);
    m_batchesValid.store(false);

    // Clear any existing events from previous initializations
    {
        std::unique_lock<std::shared_mutex> lock(m_eventsMutex);
        m_events.clear();
        m_eventTypeIndex.clear();
    }

    // Initialize message queue
    m_messageQueue.clear();
    m_processingMessages.store(false);

    // Initialize ThreadSystem if threading is enabled
    if (m_useThreading.load()) {
        if (!Forge::ThreadSystem::Exists()) {
            EVENT_LOG("Initializing ThreadSystem for event processing");
            if (!Forge::ThreadSystem::Instance().init()) {
                EVENT_LOG("Warning: Failed to initialize ThreadSystem, falling back to single-threaded mode");
                m_useThreading.store(false);
            }
        } else {
            EVENT_LOG("Using existing ThreadSystem instance for event processing");
            // Ensure the ThreadSystem has enough capacity for our tasks
            Forge::ThreadSystem::Instance().reserveQueueCapacity(256);
        }
    }

    // Initialize game time tracking
    m_lastUpdateTime = getCurrentTimeMs();

    // Register system event handlers
    registerSystemEventHandlers();

    EVENT_LOG("Event Manager initialized successfully");
    return true;
}

void EventManager::clean() {
    if (!m_initialized.exchange(false)) {
        EVENT_LOG("Event Manager already cleaned, skipping");
        return;
    }

    EVENT_LOG("Cleaning up Event Manager resources");

    // Disable threading before cleaning up to prevent new tasks from being submitted
    bool wasThreadingEnabled = m_useThreading.exchange(false);

    // Clear message queue first to prevent any pending messages
    m_messageQueue.clear();
    m_processingMessages.store(false);

    // Wait for any ongoing message processing to complete
    // Simple spin lock with timeout
    auto start = std::chrono::steady_clock::now();
    while (m_processingMessages.load() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // If we were using ThreadSystem, handle pending tasks properly
    if (wasThreadingEnabled && Forge::ThreadSystem::Exists()) {
        // Log that we're waiting for tasks to complete
        EVENT_LOG("Waiting for pending event tasks to complete");
        
        // Wait for any remaining tasks with timeout
        int waitTime = 0;
        const int maxWaitTime = 2000; // 2 seconds
        const int checkInterval = 10; // milliseconds
        
        while (Forge::ThreadSystem::Instance().isBusy() && waitTime < maxWaitTime) {
            std::this_thread::sleep_for(std::chrono::milliseconds(checkInterval));
            waitTime += checkInterval;
            
            // Log progress for long waits
            if (waitTime % 500 == 0) {
                EVENT_LOG_DETAIL("Still waiting for ThreadSystem tasks to complete: " << waitTime << "ms elapsed");
            }
        }

        if (waitTime >= maxWaitTime) {
            EVENT_LOG("Warning: Timed out waiting for ThreadSystem tasks to complete after " << waitTime << "ms");
        } else if (waitTime > 0) {
            EVENT_LOG_DETAIL("ThreadSystem tasks completed after " << waitTime << "ms wait");
        }
    }

    // Use a more structured approach to lock acquisition to avoid potential deadlocks
    EVENT_LOG_DETAIL("Acquiring locks for cleanup");
    
    // Try to acquire all locks with timeouts to avoid potential deadlocks
    std::unique_lock<std::shared_mutex> eventLock(m_eventsMutex, std::defer_lock);
    std::unique_lock<std::mutex> cacheLock(m_cacheMutex, std::defer_lock);
    std::unique_lock<std::mutex> batchesLock(m_batchesMutex, std::defer_lock);
    std::unique_lock<std::mutex> perfLock(m_perfStatsMutex, std::defer_lock);
    
    // Try to lock them all at once to avoid deadlocks
    try {
        std::lock(eventLock, cacheLock, batchesLock, perfLock);
    } catch (const std::exception& e) {
        EVENT_LOG("Error acquiring locks during cleanup: " << e.what());
        // Try again with individual locks in a consistent order
        if (!eventLock.owns_lock()) eventLock.lock();
        if (!cacheLock.owns_lock()) cacheLock.lock();
        if (!batchesLock.owns_lock()) batchesLock.lock();
        if (!perfLock.owns_lock()) perfLock.lock();
    }

    // Clean up all events
    for (auto& [name, event] : m_events) {
        if (event) {
            EVENT_LOG("Cleaning event: " << name);
            event->clean();
        }
    }

    // Clear all containers in a controlled manner
    EVENT_LOG_DETAIL("Clearing event containers");
    
    m_events.clear();
    m_eventTypeIndex.clear();
    m_activeEventCache.clear();
    m_eventTypeBatches.clear();
    m_eventTypePerformanceStats.clear();
    
    // Clear EventSystem functionality
    {
        std::lock_guard<std::mutex> handlerLock(m_eventHandlersMutex);
        m_eventHandlers.clear();
    }

    // Update atomic states
    m_cacheValid.store(false, std::memory_order_release);
    m_batchesValid.store(false, std::memory_order_release);
    
    // Reset performance stats
    m_messageQueueStats = PerformanceStats();

    EVENT_LOG("Event Manager cleaned successfully");

    // Notify any waiting threads that cleanup is complete
    m_messageQueue.clear();
    
    // Final check to ensure ThreadSystem is in a good state
    if (Forge::ThreadSystem::Exists() && !Forge::ThreadSystem::Instance().isShutdown()) {
        EVENT_LOG_DETAIL("ThreadSystem remains active for other components");
    }
    
    // Note: We don't clean up the ThreadSystem here as other components might still be using it.
    // ThreadSystem cleanup is handled at application shutdown.
}

void EventManager::configureThreading(bool useThreading, unsigned int maxThreads, Forge::TaskPriority priority) {
    EVENT_LOG("Configuring threading: useThreading=" << useThreading << ", maxThreads=" << maxThreads
              << ", priority=" << static_cast<int>(priority));

    if (!m_initialized.load()) {
        EVENT_LOG("Warning: Configuring threading before initialization");
    }

    // Check if we're changing the threading state
    bool currentThreadingState = m_useThreading.load();
    if (currentThreadingState == useThreading && m_maxThreads == maxThreads && m_eventTaskPriority == priority) {
        EVENT_LOG_DETAIL("Threading configuration unchanged, skipping");
        return;
    }

    // If we're disabling threading, wait for any pending tasks first
    if (currentThreadingState && !useThreading) {
        if (Forge::ThreadSystem::Exists()) {
            EVENT_LOG("Waiting for pending ThreadSystem tasks to complete before disabling threading");

            // Try to cancel pending event tasks
            // Note: Task cancellation is not currently supported by ThreadSystem
            // This would be a good feature to add in the future
            EVENT_LOG_DETAIL("Waiting for pending event tasks to complete");

            // Wait for pending tasks with a reasonable timeout
            int waitTime = 0;
            const int maxWaitTime = 1000; // 1 second
            while (Forge::ThreadSystem::Instance().isBusy() && waitTime < maxWaitTime) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                waitTime += 10;
            }

            if (waitTime >= maxWaitTime) {
                EVENT_LOG("Warning: Timed out waiting for ThreadSystem tasks during reconfiguration");
            } else if (waitTime > 0) {
                EVENT_LOG_DETAIL("Waited " << waitTime << "ms for ThreadSystem tasks to complete");
            }
        }
    }

    m_useThreading.store(useThreading);
    m_maxThreads = maxThreads;
    m_eventTaskPriority = priority;

    if (useThreading) {
        // Check if ThreadSystem is initialized and initialize it if needed
        if (!Forge::ThreadSystem::Exists()) {
            EVENT_LOG("Initializing ThreadSystem for event processing");
            if (!Forge::ThreadSystem::Instance().init()) {
                EVENT_LOG("Error: Failed to initialize ThreadSystem, falling back to single-threaded mode");
                m_useThreading.store(false);
                return;
            }
        } else {
            // Ensure ThreadSystem is properly configured for our needs
            Forge::ThreadSystem::Instance().reserveQueueCapacity(256);

            // Reserve capacity for better performance
            Forge::ThreadSystem::Instance().reserveQueueCapacity(256);
        }

        if (maxThreads == 0) {
            // Auto-detect: ThreadSystem already handles this optimally
            EVENT_LOG("Event processing will use ThreadSystem with default thread count ("
                     << Forge::ThreadSystem::Instance().getThreadCount() << " threads)");
        } else {
            EVENT_LOG("Event processing will use ThreadSystem with up to " << maxThreads << " concurrent tasks");
            // Note: We don't directly control thread count in ThreadSystem,
            // but we'll use this value to limit our concurrent tasks
        }
    } else {
        EVENT_LOG("Event processing will run on the main thread only");
    }

    // Reset caches after changing threading configuration
    invalidateOptimizationCaches();
}

void EventManager::registerEvent(const std::string& eventName, std::shared_ptr<Event> event) {
    if (!event) {
        EVENT_LOG("Error: Attempted to register null event with name: " << eventName);
        return;
    }

    EVENT_LOG("Registering event: " << eventName << " (type: " << event->getType() << ")");

    {
        std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

        // Check if this event name already exists
        if (m_events.find(eventName) != m_events.end()) {
            EVENT_LOG("Warning: Replacing existing event with name: " << eventName);
        }

        // Store the event
        m_events[eventName] = event;

        // Update type index
        std::string eventType = event->getType();
        m_eventTypeIndex[eventType].push_back(eventName);
    }

    // Reset caches after adding a new event
    invalidateOptimizationCaches();
}

bool EventManager::hasEvent(const std::string& eventName) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    return m_events.find(eventName) != m_events.end();
}

EventPtr EventManager::getEvent(const std::string& eventName) const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    auto it = m_events.find(eventName);
    if (it != m_events.end()) {
        return it->second;
    }

    EVENT_LOG("Warning: Event not found: " << eventName);
    return nullptr;
}

std::vector<EventPtr> EventManager::getEventsByType(const std::string& eventType) const {
    std::vector<EventPtr> result;

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    // Check if we have any events of this type
    auto typeIt = m_eventTypeIndex.find(eventType);
    if (typeIt == m_eventTypeIndex.end()) {
        return result; // Empty vector
    }

    // Collect all events of the requested type
    const auto& eventNames = typeIt->second;
    result.reserve(eventNames.size());

    for (const auto& name : eventNames) {
        auto eventIt = m_events.find(name);
        if (eventIt != m_events.end()) {
            result.push_back(eventIt->second);
        }
    }

    return result;
}

void EventManager::registerEventHandler(const std::string& eventType, EventHandlerFunc handler) {
    std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
    m_eventHandlers[eventType].push_back(handler);
    EVENT_LOG("Registered event handler for event type: " << eventType);
}

void EventManager::setBatchProcessingEnabled(bool enabled) {
    m_batchProcessingEnabled.store(enabled);
    EVENT_LOG("Handler batch processing " << (enabled ? "enabled" : "disabled"));
}

void EventManager::queueHandlerCall(const std::string& eventType, const std::string& params) {
    if (m_batchProcessingEnabled.load()) {
        m_handlerQueue.enqueueHandlerCall(eventType, params);
    } else {
        // Process immediately if batching is disabled
        std::vector<EventHandlerFunc> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
            auto it = m_eventHandlers.find(eventType);
            if (it != m_eventHandlers.end()) {
                handlersToCall = it->second;
            }
        }
        for (const auto& handler : handlersToCall) {
            handler(params);
        }
    }
}

void EventManager::processHandlerQueue() {
    auto startTime = getCurrentTimeNanos();
    
    // Swap buffers to get current processing queue
    m_handlerQueue.swapBuffers();
    
    const auto& handlerCalls = m_handlerQueue.getProcessingQueue();
    if (handlerCalls.empty()) {
        return;
    }
    
    // Group handler calls by event type for better cache performance
    boost::container::flat_map<std::string, std::vector<std::string>> batchedCalls;
    for (const auto& call : handlerCalls) {
        batchedCalls[call.eventType].push_back(call.params);
    }
    
    // Process each event type's handlers in batch
    size_t totalHandlersCalled = 0;
    for (const auto& [eventType, paramsList] : batchedCalls) {
        std::vector<EventHandlerFunc> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
            auto it = m_eventHandlers.find(eventType);
            if (it != m_eventHandlers.end()) {
                handlersToCall = it->second;
            }
        }
        
        // Execute all handlers for this event type with all queued params
        for (const auto& handler : handlersToCall) {
            for (const auto& params : paramsList) {
                handler(params);
                totalHandlersCalled++;
            }
        }
    }
    
    // Update performance stats
    auto endTime = getCurrentTimeNanos();
    double processingTimeMs = (endTime - startTime) / 1000000.0;
    m_handlerBatchStats.addSample(processingTimeMs);
    
    EVENT_LOG("Processed " << handlerCalls.size() << " queued handler calls (" 
              << totalHandlersCalled << " total handler executions) in " 
              << processingTimeMs << "ms");
    
    (void)totalHandlersCalled; // Suppress unused variable warning
}

void EventManager::registerWeatherEvent(const std::string& name, const std::string& weatherType, float intensity) {
    // Create the weather event
    auto weatherEvent = std::make_shared<WeatherEvent>(name, weatherType);

    // Configure the weather parameters
    WeatherParams params;
    params.intensity = intensity;
    params.transitionTime = 3.0f;
    weatherEvent->setWeatherParams(params);

    // Register the event
    registerEvent(name, std::static_pointer_cast<Event>(weatherEvent));

    std::cout << "Registered weather event: " << name << " of type: " << weatherType << std::endl;
}

void EventManager::registerSceneChangeEvent(const std::string& name, const std::string& targetScene,
                                           const std::string& transitionType) {
    // Create the scene change event
    auto sceneEvent = std::make_shared<SceneChangeEvent>(name, targetScene);

    // Configure transition type
    TransitionType type = TransitionType::Fade; // Default

    if (transitionType == "dissolve") {
        type = TransitionType::Dissolve;
    } else if (transitionType == "wipe") {
        type = TransitionType::Wipe;
    } else if (transitionType == "slide") {
        type = TransitionType::Slide;
    } else if (transitionType == "instant") {
        type = TransitionType::Instant;
    }

    sceneEvent->setTransitionType(type);

    // Register the event
    registerEvent(name, std::static_pointer_cast<Event>(sceneEvent));

    std::cout << "Registered scene change event: " << name << " targeting scene: " << targetScene << std::endl;
}

void EventManager::registerNPCSpawnEvent(const std::string& name, const std::string& npcType,
                                       int count, float spawnRadius) {
    // Create the NPC spawn parameters
    SpawnParameters params(npcType, count, spawnRadius);

    // Create the NPC spawn event
    auto spawnEvent = std::make_shared<NPCSpawnEvent>(name, params);

    // Register the event
    registerEvent(name, std::static_pointer_cast<Event>(spawnEvent));

    std::cout << "Registered NPC spawn event: " << name << " for NPC type: " << npcType << std::endl;
}

void EventManager::triggerWeatherChange(const std::string& weatherType, float transitionTime) {
    // Use specialized method
    changeWeather(weatherType, transitionTime);

    // Notify handlers (batched or immediate based on settings)
    if (m_batchProcessingEnabled.load()) {
        queueHandlerCall("WeatherChange", weatherType);
    } else {
        std::vector<EventHandlerFunc> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
            auto it = m_eventHandlers.find("WeatherChange");
            if (it != m_eventHandlers.end()) {
                handlersToCall = it->second;  // Copy handlers
            }
        }
        // Execute handlers without holding lock
        for (const auto& handler : handlersToCall) {
            handler(weatherType);
        }
    }
}

void EventManager::triggerSceneChange(const std::string& sceneId, const std::string& transitionType, float duration) {
    // Use specialized method
    changeScene(sceneId, transitionType, duration);

    // Notify handlers (batched or immediate based on settings)
    if (m_batchProcessingEnabled.load()) {
        queueHandlerCall("SceneChange", sceneId);
    } else {
        std::vector<EventHandlerFunc> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
            auto it = m_eventHandlers.find("SceneChange");
            if (it != m_eventHandlers.end()) {
                handlersToCall = it->second;  // Copy handlers
            }
        }
        // Execute handlers without holding lock
        for (const auto& handler : handlersToCall) {
            handler(sceneId);
        }
    }
}

void EventManager::triggerNPCSpawn(const std::string& npcType, float x, float y) {
    EVENT_LOG("Triggering NPC spawn: " << npcType << " at position (" << x << ", " << y << ")");
    
    // Use NPCSpawnEvent system for robust spawning
    spawnNPC(npcType, x, y);
    
    // Also notify any registered handlers for compatibility
    // Notify handlers (batched or immediate based on settings)
    if (m_batchProcessingEnabled.load()) {
        queueHandlerCall("NPCSpawn", npcType);
    } else {
        std::vector<EventHandlerFunc> handlersToCall;
        {
            std::lock_guard<std::mutex> lock(m_eventHandlersMutex);
            auto it = m_eventHandlers.find("NPCSpawn");
            if (it != m_eventHandlers.end()) {
                handlersToCall = it->second;  // Copy handlers
            }
        }
        // Execute handlers without holding lock
        for (const auto& handler : handlersToCall) {
            handler(npcType);
        }
    }
}

bool EventManager::spawnNPC(const std::string& npcType, float x, float y) {
    EVENT_LOG("Spawning NPC of type: " << npcType << " at position (" << x << ", " << y << ")");

    // Send spawn request to NPCSpawn events for handling
    auto spawnEvents = getEventsByType("NPCSpawn");
    if (spawnEvents.empty()) {
        EVENT_LOG("Warning: No NPC spawn events registered");
        return false;
    }

    // Send a special formatted message to all NPC spawn events
    std::string message = "SPAWN_REQUEST:" + npcType + ":" + std::to_string(x) + ":" + std::to_string(y);
    for (size_t i = 0; i < spawnEvents.size(); ++i) {
        try {
            auto event = spawnEvents[i];
            if (event && event.use_count() > 0) {
                event->onMessage(message);
            }
        } catch (const std::exception& e) {
            EVENT_LOG("Error processing NPC spawn event " << i << ": " << e.what());
        } catch (...) {
            EVENT_LOG("Unknown error processing NPC spawn event " << i);
        }
    }

    return true;
}



void EventManager::registerDefaultEvents() {
    // Register some common weather events
    registerWeatherEvent("SunnyDay", "Clear", 0.0f);
    registerWeatherEvent("LightRain", "Rainy", 0.4f);
    registerWeatherEvent("HeavyRain", "Rainy", 0.8f);
    registerWeatherEvent("ThunderStorm", "Stormy", 1.0f);
    registerWeatherEvent("LightFog", "Foggy", 0.3f);
    registerWeatherEvent("DenseFog", "Foggy", 0.8f);
    registerWeatherEvent("LightSnow", "Snowy", 0.3f);
    registerWeatherEvent("Blizzard", "Snowy", 0.9f);

    // Set up some random weather transitions based on time
    auto sunnyWeather = getEvent("SunnyDay");
    if (sunnyWeather) {
        std::dynamic_pointer_cast<WeatherEvent>(sunnyWeather)->setTimeOfDay(6.0f, 18.0f); // Daytime
    }

    auto foggyMorning = getEvent("LightFog");
    if (foggyMorning) {
        std::dynamic_pointer_cast<WeatherEvent>(foggyMorning)->setTimeOfDay(5.0f, 9.0f); // Early morning
    }
}

void EventManager::registerSystemEventHandlers() {
    // Register handlers for system events

    // Weather change handler
    registerEventHandler("WeatherChange", [](const std::string& params) {
        std::cout << "System handling weather change: " << params << std::endl;

        // Here we would update graphics settings, particle systems, etc.
        // For example, adjust ambient lighting, fog settings, etc.

        // Play appropriate ambient sounds
        if (params == "Rainy" || params == "Stormy") {
            // Would play rain sound if SoundManager was implemented
            std::cout << "Playing rain ambient sound" << std::endl;
        } else if (params == "Snowy") {
            // Would play wind sound if SoundManager was implemented
            std::cout << "Playing wind ambient sound" << std::endl;
        }
    });

    // Scene change handler
    registerEventHandler("SceneChange", [](const std::string& params) {
        std::cout << "System handling scene change: " << params << std::endl;

        // Here we would notify the GameStateManager to change the scene
        std::cout << "Changing game state to: " << params << std::endl;
    });

    // NPC spawn handler removed - game states handle NPC creation directly
    // This prevents duplicate NPC creation and maintains clean architecture
}

void EventManager::updateEventTimers(float deltaTime) {
    // Get all events
    std::vector<EventPtr> allEvents;

    // Get events of each major type
    auto weatherEvents = getEventsByType("Weather");
    auto sceneEvents = getEventsByType("SceneChange");
    auto spawnEvents = getEventsByType("NPCSpawn");

    // Combine all events
    allEvents.reserve(allEvents.size() + weatherEvents.size() + sceneEvents.size() + spawnEvents.size());
    std::copy(weatherEvents.begin(), weatherEvents.end(), std::back_inserter(allEvents));
    std::copy(sceneEvents.begin(), sceneEvents.end(), std::back_inserter(allEvents));
    std::copy(spawnEvents.begin(), spawnEvents.end(), std::back_inserter(allEvents));

    // Update cooldown timers for all events
    for (size_t i = 0; i < allEvents.size(); ++i) {
        try {
            auto event = allEvents[i];
            if (event && event.use_count() > 0) {
                event->updateCooldown(deltaTime);
            }
        } catch (const std::exception& e) {
            EVENT_LOG("Error updating cooldown for event " << i << ": " << e.what());
        } catch (...) {
            EVENT_LOG("Unknown error updating cooldown for event " << i);
        }
    }
}

void EventManager::processSystemEvents() {
    // This would process any system-level events like SDL events, window events, etc.
    // that might trigger game events

    // Example: Check for day/night cycle changes
    // Use a placeholder value since GameTime might not be fully implemented
    float gameTime = 12.0f; // Noon
    
    // TODO: Add GameTime integration when available
    // Note: Commented out to avoid linking issues in tests
    // if (GameTime::Instance().init()) {
    //     gameTime = GameTime::Instance().getGameHour();
    // }
    
    static float lastCheckedHour = -1.0f;

    // Check if hour has changed
    if (static_cast<int>(gameTime) != static_cast<int>(lastCheckedHour)) {
        lastCheckedHour = gameTime;

        // Dawn
        if (gameTime >= 6.0f && gameTime < 7.0f) {
            broadcastMessage("TIME_DAWN");
        }
        // Day
        else if (gameTime >= 7.0f && gameTime < 19.0f) {
            broadcastMessage("TIME_DAY");
        }
        // Dusk
        else if (gameTime >= 19.0f && gameTime < 20.0f) {
            broadcastMessage("TIME_DUSK");
        }
        // Night
        else {
            broadcastMessage("TIME_NIGHT");
        }
    }
}

uint64_t EventManager::getCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

bool EventManager::removeEvent(const std::string& eventName) {
    EVENT_LOG("Removing event: " << eventName);

    std::unique_lock<std::shared_mutex> lock(m_eventsMutex);

    auto it = m_events.find(eventName);
    if (it == m_events.end()) {
        EVENT_LOG("Warning: Cannot remove non-existent event: " << eventName);
        return false;
    }

    // Get event type for updating type index
    std::string eventType = it->second->getType();

    // Clean the event before removing it
    it->second->clean();

    // Remove from type index
    auto typeIt = m_eventTypeIndex.find(eventType);
    if (typeIt != m_eventTypeIndex.end()) {
        auto& eventNames = typeIt->second;
        eventNames.erase(std::remove(eventNames.begin(), eventNames.end(), eventName), eventNames.end());

        // If no more events of this type, remove the type entry
        if (eventNames.empty()) {
            m_eventTypeIndex.erase(typeIt);
        }
    }

    // Remove from main events map
    m_events.erase(it);

    // Reset caches after removing an event
    invalidateOptimizationCaches();

    return true;
}

bool EventManager::setEventActive(const std::string& eventName, bool active) {
    EVENT_LOG("Setting event " << eventName << " active state to: " << (active ? "active" : "inactive"));

    auto event = getEvent(eventName);
    if (!event) {
        EVENT_LOG("Warning: Cannot set active state for non-existent event: " << eventName);
        return false;
    }

    event->setActive(active);

    // Reset active event cache when changing active state
    invalidateOptimizationCaches();

    return true;
}

bool EventManager::isEventActive(const std::string& eventName) const {
    auto event = getEvent(eventName);
    if (!event) {
        return false;
    }

    return event->isActive();
}

bool EventManager::executeEvent(const std::string& eventName) {
    EVENT_LOG("Executing event: " << eventName);

    auto event = getEvent(eventName);
    if (!event) {
        EVENT_LOG("Warning: Cannot execute non-existent event: " << eventName);
        return false;
    }

    // Execute even if inactive (forced execution)
    event->execute();
    return true;
}

int EventManager::executeEventsByType(const std::string& eventType) {
    EVENT_LOG("Executing all events of type: " << eventType);

    auto events = getEventsByType(eventType);
    int count = 0;

    for (size_t i = 0; i < events.size(); ++i) {
        try {
            auto event = events[i];
            if (event && event.use_count() > 0) {
                event->execute();
                count++;
            }
        } catch (const std::exception& e) {
            EVENT_LOG("Error executing event " << i << " of type " << eventType << ": " << e.what());
        } catch (...) {
            EVENT_LOG("Unknown error executing event " << i << " of type " << eventType);
        }
    }

    EVENT_LOG("Executed " << count << " events of type: " << eventType);
    return count;
}

void EventManager::update() {
    if (!m_initialized.load()) {
        EVENT_LOG("Warning: Update called before initialization");
        return;
    }

    // Calculate delta time for EventSystem functionality
    uint64_t currentTime = getCurrentTimeMs();
    float deltaTime = (currentTime - m_lastUpdateTime) / 1000.0f; // Convert to seconds
    m_lastUpdateTime = currentTime;

    // Update event cooldowns and timers
    updateEventTimers(deltaTime);

    // Process any pending messages first
    processMessageQueue();

    // Process any queued handler calls in batches
    processHandlerQueue();

    // Process system-level events
    processSystemEvents();

    // Check if the ThreadSystem is available
    if (!Forge::ThreadSystem::Exists()) {
        // Fall back to single-threaded mode if ThreadSystem is not available
        EVENT_LOG_DETAIL("ThreadSystem is not available, falling back to single-threaded mode");
        m_useThreading.store(false);
    }

    // Ensure optimization caches are valid
    ensureOptimizationCachesValid();

    // Update all events by type (in batches for better cache locality)
    if (m_useThreading.load() && m_eventTypeBatches.size() > 1 && Forge::ThreadSystem::Exists()) {
        // Make a local copy of batches to avoid data races
        decltype(m_eventTypeBatches) localBatches;
        {
            // Lock while copying batches
            std::lock_guard<std::mutex> lock(m_batchesMutex);
            localBatches = m_eventTypeBatches;
        }

        // Create a vector of futures to track task completion
        std::vector<std::future<void>> futures;
        futures.reserve(localBatches.size());

        // Track the time we started submitting tasks
        // Reserve capacity in the ThreadSystem task queue for better performance
        Forge::ThreadSystem::Instance().reserveQueueCapacity(
            std::min(static_cast<size_t>(localBatches.size() * 2),
                    static_cast<size_t>(1024)));

        #ifdef EVENT_DEBUG_LOGGING
        auto startTime = std::chrono::steady_clock::now();
        #endif

        // Process each batch through the ThreadSystem
        for (const auto& [eventType, batch] : localBatches) {
            // Skip empty batches
            if (batch.empty()) {
                continue;
            }

            // Make a local copy of the batch and type to ensure thread safety
            std::string typeStr = std::string(eventType);
            EventBatch batchCopy = batch;

            // Determine priority for specific event types
            Forge::TaskPriority batchPriority = m_eventTaskPriority;

            // Prioritize critical event types (this could be made configurable)
            if (typeStr == "Input" || typeStr == "Physics" || typeStr == "Rendering") {
                batchPriority = Forge::TaskPriority::Critical;
            }
            // Handle special event types that should run at high priority
            else if (typeStr == "Animation" || typeStr == "Audio") {
                batchPriority = Forge::TaskPriority::High;
            }
            // Background tasks should run at lower priority
            else if (typeStr == "Background" || typeStr == "Analytics" || typeStr == "AutoSave") {
                batchPriority = Forge::TaskPriority::Low;
            }

            // Create a meaningful task description for debugging
            std::string taskDescription = "EventManager: Update " + typeStr + " events (" +
                                         std::to_string(batchCopy.size()) + " events)";

            // Submit task to ThreadSystem with appropriate priority
            try {
                // Create a task that handles proper shutdown gracefully
                auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                    [this, typeStr, batchCopy]() {
                        // Check if we're still initialized before processing
                        if (m_initialized.load(std::memory_order_acquire)) {
                            updateEventTypeBatch(typeStr, batchCopy);
                        } else {
                            // Skip processing if we're shutting down
                            EVENT_LOG_DETAIL("Skipping " << typeStr << " event batch - system shutting down");
                        }
                    },
                    batchPriority,
                    taskDescription
                );
                futures.push_back(std::move(future));
            } catch (const std::exception& e) {
                EVENT_LOG("Error enqueueing event batch task: " << e.what());
                // If ThreadSystem fails, process this batch on the main thread
                updateEventTypeBatch(eventType, batch);
            }
        }

        // Log the time it took to submit all tasks
        if (futures.size() > 0) {
            #ifdef EVENT_DEBUG_LOGGING
            EVENT_LOG_DETAIL("Submitted " << futures.size() << " event batch tasks in "
                             << std::chrono::duration_cast<std::chrono::microseconds>(
                                    std::chrono::steady_clock::now() - startTime).count() << "μs");
            #endif
        }

        // Wait for all futures to complete with progress tracking
        #ifdef EVENT_DEBUG_LOGGING
        size_t completedCount = 0; // Track tasks that complete successfully for logging
        #endif
        size_t timedOutTasks = 0;
        #ifdef EVENT_DEBUG_LOGGING
        auto waitStartTime = std::chrono::steady_clock::now();
        #endif

        // Use a more reliable wait strategy with better timeout handling
        const int DEFAULT_TIMEOUT_MS = 500;  // Default timeout per task in milliseconds
        const int EXTENDED_TIMEOUT_MS = 2000;  // Extended timeout for slow tasks

        for (auto& future : futures) {
            try {
                // Use a reasonable timeout to prevent hanging
                auto status = future.wait_for(std::chrono::milliseconds(DEFAULT_TIMEOUT_MS));
                if (status != std::future_status::ready) {
                    // Try again with a longer timeout
                    EVENT_LOG_DETAIL("Event task taking longer than expected, extending timeout");
                    status = future.wait_for(std::chrono::milliseconds(EXTENDED_TIMEOUT_MS));
                    if (status != std::future_status::ready) {
                        EVENT_LOG("Warning: Event batch processing timed out after extended wait");
                        timedOutTasks++;
                        
                        // We can't cancel tasks, but log the issue
                        if (Forge::ThreadSystem::Exists()) {
                            EVENT_LOG_DETAIL("Task appears to be hung, but cancellation is not supported");
                        }
                        
                        // Continue to next task instead of potentially hanging
                        continue;
                    } else {
                        future.get(); // Get the result to propagate any exceptions
                        #ifdef EVENT_DEBUG_LOGGING
                        completedCount++;
                        #endif
                    }
                } else {
                    future.get(); // Get the result to propagate any exceptions
                    #ifdef EVENT_DEBUG_LOGGING
                    completedCount++;
                    #endif
                }
            } catch (const std::exception& e) {
                EVENT_LOG("Error in event batch processing: " << e.what());
            } catch (...) {
                EVENT_LOG("Unknown error in event batch processing");
            }
        }

        // Log task completion statistics
        if (timedOutTasks > 0) {
            EVENT_LOG("Warning: " << timedOutTasks << " of " << futures.size()
                      << " event batch tasks timed out");
        }

        #ifdef EVENT_DEBUG_LOGGING
        if (completedCount > 0) {
            EVENT_LOG_DETAIL("Processed " << completedCount << " event batch tasks in "
                             << std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - waitStartTime).count() << "ms");
        }
        #endif
    } else {
        // With threading disabled or ThreadSystem unavailable, process events sequentially
        // Make a local copy to avoid data races
        std::vector<EventCache> localCache;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            localCache = m_activeEventCache;
        }

        for (auto& cacheEntry : localCache) {
            // Lock the weak_ptr to get a shared_ptr
            if (auto eventPtr = cacheEntry.event.lock()) {
                updateEvent(eventPtr, cacheEntry.eventName);
            }
        }
    }
}

void EventManager::invalidateOptimizationCaches() {
    m_cacheValid.store(false);
    m_batchesValid.store(false);
}

void EventManager::ensureOptimizationCachesValid() {
    // Use double-checked locking to rebuild caches efficiently

    // Check and rebuild active event cache if needed
    if (!m_cacheValid.load()) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);

        // Double-check after acquiring lock
        if (!m_cacheValid.load()) {
            rebuildActiveEventCache();
            m_cacheValid.store(true);
        }
    }

    // Check and rebuild event type batches if needed
    if (!m_batchesValid.load()) {
        std::lock_guard<std::mutex> lock(m_batchesMutex);

        // Double-check after acquiring lock
        if (!m_batchesValid.load()) {
            rebuildEventTypeBatches();
            m_batchesValid.store(true);
        }
    }
}

void EventManager::rebuildActiveEventCache() {
    EVENT_LOG("Rebuilding active event cache");

    m_activeEventCache.clear();

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    // Reserve space to avoid reallocations
    m_activeEventCache.reserve(m_events.size());

    // Add all active events to the cache
    for (const auto& [name, eventPtr] : m_events) {
        if (eventPtr && eventPtr->isActive()) {
            EventCache cacheEntry;
            cacheEntry.event = eventPtr;
            cacheEntry.eventName = name;
            cacheEntry.eventType = eventPtr->getType();
            cacheEntry.lastUpdateTime = getCurrentTimeNanos();

            m_activeEventCache.push_back(cacheEntry);
        }
    }

    EVENT_LOG("Active event cache rebuilt with " << m_activeEventCache.size() << " events");
}

void EventManager::rebuildEventTypeBatches() {
    EVENT_LOG("Rebuilding event type batches");

    m_eventTypeBatches.clear();

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    // Group active events by type
    for (const auto& [type, eventNames] : m_eventTypeIndex) {
        EventBatch batch;
        batch.reserve(eventNames.size());

        for (const auto& name : eventNames) {
            auto it = m_events.find(name);
            if (it != m_events.end() && it->second && it->second->isActive()) {
                batch.push_back(it->second);
            }
        }

        if (!batch.empty()) {
            m_eventTypeBatches[type] = std::move(batch);
        }
    }

    EVENT_LOG("Event type batches rebuilt with " << m_eventTypeBatches.size() << " types");
}

void EventManager::updateEventTypeBatch(const std::string_view& eventType, const EventBatch& batch) {
    if (batch.empty()) {
        return;
    }

    EVENT_LOG_DETAIL("Updating batch of " << batch.size() << " events of type: " << eventType);

    auto batchStartTime = getCurrentTimeNanos();

    for (auto event : batch) {
        if (event && event->isActive()) {
            // Check event conditions and execute if met
            updateEvent(event, event->getName());
        }
    }

    auto batchEndTime = getCurrentTimeNanos();
    double batchTimeMs = (batchEndTime - batchStartTime) / 1000000.0;

    // Record performance stats for this event type
    recordEventTypePerformance(eventType, batchTimeMs);
}

void EventManager::updateEvent(const EventPtr& event, const std::string_view& /* eventName */) {
    if (!event) {
        return;
    }

    auto startTime = getCurrentTimeNanos();

    // Update the event (runs condition checks internally)
    event->update();

    // Check if conditions are met and execute if they are
    executeEventIfConditionsMet(event);

    auto endTime = getCurrentTimeNanos();

    // Record performance metrics in a thread-safe way
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = std::find_if(m_activeEventCache.begin(), m_activeEventCache.end(),
                          [&event](const auto& cacheEntry) {
                              if (auto cachedEvent = cacheEntry.event.lock()) {
                                  return cachedEvent == event;
                              }
                              return false;
                          });
    if (it != m_activeEventCache.end()) {
        it->lastUpdateTime = endTime;
        double timeMs = (endTime - startTime) / 1000000.0;
        it->perfStats.addSample(timeMs);
    }
}

bool EventManager::checkEventConditions(const EventPtr& event) {
    if (!event || !event->isActive()) {
        return false;
    }

    return event->checkConditions();
}

void EventManager::executeEventIfConditionsMet(const EventPtr& event) {
    if (!event || !event->isActive()) {
        return;
    }

    if (event->checkConditions()) {
        event->execute();
    }
}

void EventManager::sendMessageToEvent(const std::string& eventName, const std::string& message, bool immediate) {
    EVENT_LOG("Sending message to event " << eventName << ": " << message);

    if (immediate) {
        deliverMessageToEvent(eventName, message);
    } else {
        m_messageQueue.enqueueMessage(eventName, message);
    }
}

void EventManager::broadcastMessageToType(const std::string& eventType, const std::string& message, bool immediate) {
    EVENT_LOG("Broadcasting message to all events of type " << eventType << ": " << message);

    if (immediate) {
        deliverMessageToEventType(eventType, message);
    } else {
        m_messageQueue.enqueueTypeMessage(eventType, message);
    }
}

void EventManager::broadcastMessage(const std::string& message, bool immediate) {
    EVENT_LOG("Broadcasting message to all events: " << message);

    if (immediate) {
        deliverBroadcastMessage(message);
    } else {
        m_messageQueue.enqueueBroadcastMessage(message);
    }
}

void EventManager::processMessageQueue() {
    // Avoid recursive calls to processMessageQueue
    if (m_processingMessages.exchange(true)) {
        return;
    }

    auto startTime = getCurrentTimeNanos();

    // Swap the message queues to process current messages while allowing new ones to be added
    m_messageQueue.swapBuffers();

    // Process all messages in the processing queue
    const auto& messages = m_messageQueue.getProcessingQueue();
    size_t processedCount = 0;

    for (const auto& msg : messages) {
        if (!msg.targetEvent.empty()) {
            // Message targeted at a specific event
            deliverMessageToEvent(msg.targetEvent, msg.message);
            processedCount++;
        } else if (!msg.targetType.empty()) {
            // Message targeted at all events of a specific type
            processedCount += deliverMessageToEventType(msg.targetType, msg.message);
        } else {
            // Broadcast message to all events
            processedCount += deliverBroadcastMessage(msg.message);
        }
    }

    auto endTime = getCurrentTimeNanos();
    double timeMs = (endTime - startTime) / 1000000.0;

    // Record performance stats for message processing
    {
        std::lock_guard<std::mutex> lock(m_perfStatsMutex);
        m_messageQueueStats.addSample(timeMs);
    }

    if (processedCount > 0) {
        EVENT_LOG_DETAIL("Processed " << processedCount << " messages in " << timeMs << "ms");
    }

    // Always reset the processing flag, even if an exception occurred
    m_processingMessages.store(false);
}

void EventManager::deliverMessageToEvent(const std::string& eventName, const std::string& message) {
    auto event = getEvent(eventName);
    if (!event) {
        EVENT_LOG("Warning: Cannot deliver message to non-existent event: " << eventName);
        return;
    }

    event->onMessage(message);
}

size_t EventManager::deliverMessageToEventType(const std::string& eventType, const std::string& message) {
    size_t count = 0;

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    auto typeIt = m_eventTypeIndex.find(eventType);
    if (typeIt == m_eventTypeIndex.end()) {
        return 0;
    }

    const auto& eventNames = typeIt->second;
    for (const auto& name : eventNames) {
        auto it = m_events.find(name);
        if (it != m_events.end() && it->second) {
            it->second->onMessage(message);
            count++;
        }
    }

    return count;
}

size_t EventManager::deliverBroadcastMessage(const std::string& message) {
    size_t count = 0;

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);

    for (const auto& [name, event] : m_events) {
        if (event) {
            event->onMessage(message);
            count++;
        }
    }

    return count;
}

void EventManager::recordEventTypePerformance(const std::string_view& eventType, double timeMs) {
    std::lock_guard<std::mutex> lock(m_perfStatsMutex);
    m_eventTypePerformanceStats[std::string(eventType)].addSample(timeMs);
}

void EventManager::resetEvents() {
    EVENT_LOG("Resetting all events");

    // Stop message processing
    m_messageQueue.clear();

    // Wait for any ongoing message processing to complete
    auto start = std::chrono::steady_clock::now();
    while (m_processingMessages.load() &&
           std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < 1000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Reset all events by type
    {
        std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
        for (const auto& [name, event] : m_events) {
            if (event) {
                EVENT_LOG_DETAIL("Resetting event: " << name);
                event->reset();
            }
        }
    }

    // Clear and rebuild caches
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_activeEventCache.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_batchesMutex);
        m_eventTypeBatches.clear();
    }

    invalidateOptimizationCaches();

    EVENT_LOG("All events reset successfully");
}

size_t EventManager::getEventCount() const {
    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    return m_events.size();
}

size_t EventManager::getActiveEventCount() const {
    size_t count = 0;

    std::shared_lock<std::shared_mutex> lock(m_eventsMutex);
    for (const auto& [name, event] : m_events) {
        if (event && event->isActive()) {
            count++;
        }
    }

    return count;
}

// Weather-specific methods
bool EventManager::changeWeather(const std::string& weatherType, float transitionTime) {
    EVENT_LOG("Forcing weather change to: " << weatherType << " (transition: " << transitionTime << "s)");

    // Find weather events
    auto weatherEvents = getEventsByType("Weather");
    if (weatherEvents.empty()) {
        EVENT_LOG("Warning: No weather events registered");
        return false;
    }

    // Send a special formatted message to all weather events
    std::string message = "CHANGE:" + weatherType + ":" + std::to_string(transitionTime);
    for (size_t i = 0; i < weatherEvents.size(); ++i) {
        try {
            auto event = weatherEvents[i];
            if (event && event.use_count() > 0) {
                event->onMessage(message);
            }
        } catch (const std::exception& e) {
            EVENT_LOG("Error processing weather event " << i << ": " << e.what());
        } catch (...) {
            EVENT_LOG("Unknown error processing weather event " << i);
        }
    }

    return true;
}

// Scene-specific methods
bool EventManager::changeScene(const std::string& sceneId, const std::string& transitionType, float transitionTime) {
    EVENT_LOG("Forcing scene change to: " << sceneId << " (transition: " << transitionType
              << ", duration: " << transitionTime << "s)");

    // Find scene change events
    auto sceneEvents = getEventsByType("SceneChange");
    if (sceneEvents.empty()) {
        EVENT_LOG("Warning: No scene change events registered");
        return false;
    }

    // Send a special formatted message to all scene change events
    std::string message = "CHANGE:" + sceneId + ":" + transitionType + ":" + std::to_string(transitionTime);
    for (size_t i = 0; i < sceneEvents.size(); ++i) {
        try {
            auto event = sceneEvents[i];
            if (event && event.use_count() > 0) {
                event->onMessage(message);
            }
        } catch (const std::exception& e) {
            EVENT_LOG("Error processing scene change event " << i << ": " << e.what());
        } catch (...) {
            EVENT_LOG("Unknown error processing scene change event " << i);
        }
    }

    return true;
}


// NPC spawn methods - uses NPCSpawnEvent system for robust functionality
