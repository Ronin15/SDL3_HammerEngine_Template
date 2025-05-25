/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/EventSystem.hpp"
#include "managers/EventManager.hpp"
#include "core/GameTime.hpp"
#include "events/Event.hpp"
#include "events/WeatherEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/NPCSpawnEvent.hpp"
#include <iostream>
#include <memory>
#include <chrono>

// Forward declare required classes
class GameStateManager;
class TextureManager;
class SoundManager;
class EntityFactory;
class Camera;

// Static instance management
EventSystem* EventSystem::s_instance = nullptr;

EventSystem::EventSystem() :
    m_initialized(false),
    m_lastUpdateTime(0) {
    EVENT_SYSTEM_LOG("EventSystem created");
}

EventSystem::~EventSystem() {
    clean();
    EVENT_SYSTEM_LOG("EventSystem destroyed");
}

EventSystem* EventSystem::Instance() {
    if (s_instance == nullptr) {
        s_instance = new EventSystem();
    }
    return s_instance;
}

void EventSystem::release() {
    if (s_instance != nullptr) {
        delete s_instance;
        s_instance = nullptr;
    }
}

bool EventSystem::init() {
    if (m_initialized) {
        EVENT_SYSTEM_LOG("EventSystem already initialized");
        return true;
    }

    EVENT_SYSTEM_LOG("Initializing EventSystem");

    // Initialize the EventManager
    if (!EventManager::Instance().init()) {
        EVENT_SYSTEM_LOG("Failed to initialize EventManager");
        return false;
    }

    // Initialize game time tracking
    m_lastUpdateTime = getCurrentTimeMs();

    // Register system event handlers
    registerSystemEventHandlers();

    m_initialized = true;
    EVENT_SYSTEM_LOG("EventSystem initialized successfully");
    return true;
}

void EventSystem::update() {
    if (!m_initialized) {
        return;
    }

    // Calculate delta time
    uint64_t currentTime = getCurrentTimeMs();
    float deltaTime = (currentTime - m_lastUpdateTime) / 1000.0f; // Convert to seconds
    m_lastUpdateTime = currentTime;

    // Update event cooldowns and timers
    updateEventTimers(deltaTime);

    // Update the EventManager
    EventManager::Instance().update();

    // Process any pending system events
    processSystemEvents();
}

void EventSystem::clean() {
    if (!m_initialized) {
        return;
    }

    EVENT_SYSTEM_LOG("Cleaning up EventSystem");

    // Clean up the EventManager
    EventManager::Instance().clean();

    // Clear all registered event handlers
    m_eventHandlers.clear();

    m_initialized = false;
}

void EventSystem::registerEventHandler(const std::string& eventType, EventHandlerFunc handler) {
    m_eventHandlers[eventType].push_back(handler);
    EVENT_SYSTEM_LOG("Registered event handler for event type: " << eventType);
}

void EventSystem::registerWeatherEvent(const std::string& name, const std::string& weatherType, float intensity) {
    // Create the weather event
    auto weatherEvent = std::make_shared<WeatherEvent>(name, weatherType);

    // Configure the weather parameters
    WeatherParams params;
    params.intensity = intensity;
    params.transitionTime = 3.0f;
    weatherEvent->setWeatherParams(params);

    // Register the event with the EventManager
    EventManager::Instance().registerEvent(name, std::static_pointer_cast<Event>(weatherEvent));

    std::cout << "Registered weather event: " << name << " of type: " << weatherType << std::endl;
}

void EventSystem::registerSceneChangeEvent(const std::string& name, const std::string& targetScene,
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

    // Register the event with the EventManager
    EventManager::Instance().registerEvent(name, std::static_pointer_cast<Event>(sceneEvent));

    std::cout << "Registered scene change event: " << name << " targeting scene: " << targetScene << std::endl;
}

void EventSystem::registerNPCSpawnEvent(const std::string& name, const std::string& npcType,
                                       int count, float spawnRadius) {
    // Create the NPC spawn parameters
    SpawnParameters params(npcType, count, spawnRadius);

    // Create the NPC spawn event
    auto spawnEvent = std::make_shared<NPCSpawnEvent>(name, params);

    // Register the event with the EventManager
    EventManager::Instance().registerEvent(name, std::static_pointer_cast<Event>(spawnEvent));

    std::cout << "Registered NPC spawn event: " << name << " for NPC type: " << npcType << std::endl;
}

void EventSystem::triggerWeatherChange(const std::string& weatherType, float transitionTime) {
    // Forward to EventManager's specialized method
    EventManager::Instance().changeWeather(weatherType, transitionTime);

    // Also notify any registered handlers for weather events
    if (m_eventHandlers.find("WeatherChange") != m_eventHandlers.end()) {
        for (const auto& handler : m_eventHandlers["WeatherChange"]) {
            handler(weatherType);
        }
    }
}

void EventSystem::triggerSceneChange(const std::string& sceneId, const std::string& transitionType, float duration) {
    // Forward to EventManager's specialized method
    EventManager::Instance().changeScene(sceneId, transitionType, duration);

    // Notify any registered handlers for scene change events
    if (m_eventHandlers.find("SceneChange") != m_eventHandlers.end()) {
        for (const auto& handler : m_eventHandlers["SceneChange"]) {
            handler(sceneId);
        }
    }
}

void EventSystem::triggerNPCSpawn(const std::string& npcType, float x, float y) {
    // Forward to EventManager's specialized method
    EventManager::Instance().spawnNPC(npcType, x, y);

    // Also notify any registered handlers for NPC spawn events
    if (m_eventHandlers.find("NPCSpawn") != m_eventHandlers.end()) {
        for (const auto& handler : m_eventHandlers["NPCSpawn"]) {
            handler(npcType);
        }
    }
}

void EventSystem::registerDefaultEvents() {
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
    auto sunnyWeather = EventManager::Instance().getEvent("SunnyDay");
    if (sunnyWeather) {
        dynamic_cast<WeatherEvent*>(sunnyWeather)->setTimeOfDay(6.0f, 18.0f); // Daytime
    }

    auto foggyMorning = EventManager::Instance().getEvent("LightFog");
    if (foggyMorning) {
        dynamic_cast<WeatherEvent*>(foggyMorning)->setTimeOfDay(5.0f, 9.0f); // Early morning
    }
}

void EventSystem::registerSystemEventHandlers() {
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

    // NPC spawn handler
    registerEventHandler("NPCSpawn", [](const std::string& params) {
        std::cout << "System handling NPC spawn: " << params << std::endl;

        // Here we would request entity creation
        std::cout << "Creating NPC of type: " << params << std::endl;
    });
}

void EventSystem::updateEventTimers(float deltaTime) {
    // Get all events from EventManager
    std::vector<Event*> allEvents;

    // Get events of each major type
    auto weatherEvents = EventManager::Instance().getEventsByType("Weather");
    auto sceneEvents = EventManager::Instance().getEventsByType("SceneChange");
    auto spawnEvents = EventManager::Instance().getEventsByType("NPCSpawn");

    // Combine all events
    allEvents.insert(allEvents.end(), weatherEvents.begin(), weatherEvents.end());
    allEvents.insert(allEvents.end(), sceneEvents.begin(), sceneEvents.end());
    allEvents.insert(allEvents.end(), spawnEvents.begin(), spawnEvents.end());

    // Update cooldown timers for all events
    for (auto event : allEvents) {
        if (event) {
            // Cast to base Event class which has the updateCooldown method
            static_cast<Event*>(event)->updateCooldown(deltaTime);
        }
    }
}

void EventSystem::processSystemEvents() {
    // This would process any system-level events like SDL events, window events, etc.
    // that might trigger game events

    // Example: Check for day/night cycle changes
    // Use a placeholder value since GameTime might not be fully implemented
    float gameTime = 12.0f; // Noon
    if (GameTime::Instance().init()) {
        gameTime = GameTime::Instance().getGameHour();
    }
    static float lastCheckedHour = -1.0f;

    // Check if hour has changed
    if (static_cast<int>(gameTime) != static_cast<int>(lastCheckedHour)) {
        lastCheckedHour = gameTime;

        // Dawn
        if (gameTime >= 6.0f && gameTime < 7.0f) {
            EventManager::Instance().broadcastMessage("TIME_DAWN");
        }
        // Day
        else if (gameTime >= 7.0f && gameTime < 19.0f) {
            EventManager::Instance().broadcastMessage("TIME_DAY");
        }
        // Dusk
        else if (gameTime >= 19.0f && gameTime < 20.0f) {
            EventManager::Instance().broadcastMessage("TIME_DUSK");
        }
        // Night
        else {
            EventManager::Instance().broadcastMessage("TIME_NIGHT");
        }
    }
}

uint64_t EventSystem::getCurrentTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Static helper to log to both console and potentially a file
void EventSystem::log(const std::string& message) {
    std::cout << "EventSystem: " << message << std::endl;
    // Could also log to a file here
}

// Define EVENT_SYSTEM_LOG if not already defined
#ifndef EVENT_SYSTEM_LOG
#define EVENT_SYSTEM_LOG(x) EventSystem::log(x)
#endif
