/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef EVENT_SYSTEM_HPP
#define EVENT_SYSTEM_HPP

/**
 * @file EventSystem.hpp
 * @brief Central system for managing game events and integrating with other systems
 *
 * The EventSystem is a high-level interface that:
 * - Initializes and manages the EventManager
 * - Connects events to other game systems (rendering, audio, AI, etc.)
 * - Provides simplified API for creating and triggering common event types
 * - Handles event callbacks and notifications
 */

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declarations
class Event;
class WeatherEvent;
class SceneChangeEvent;
class NPCSpawnEvent;

// Simple logging macro - can be disabled in release builds
#ifdef _DEBUG
    #define EVENT_SYSTEM_LOG(x) EventSystem::log(x)
#else
    #define EVENT_SYSTEM_LOG(x)
#endif

// Shared pointer types for events
using EventPtr = std::shared_ptr<Event>;
using EventWeakPtr = std::weak_ptr<Event>;

// Event handler function type
using EventHandlerFunc = std::function<void(const std::string&)>;

class EventSystem {
public:
    /**
     * @brief Get the singleton instance of EventSystem
     * @return Pointer to the EventSystem instance
     */
    static EventSystem* Instance();
    
    /**
     * @brief Release the singleton instance
     */
    static void release();
    
    /**
     * @brief Initialize the event system
     * @return True if initialization succeeded, false otherwise
     */
    bool init();
    
    /**
     * @brief Update the event system
     * This should be called once per frame
     */
    void update();
    
    /**
     * @brief Clean up resources used by the event system
     */
    void clean();
    
    /**
     * @brief Register an event handler for a specific event type
     * @param eventType Type of event to handle
     * @param handler Function to call when event occurs
     */
    void registerEventHandler(const std::string& eventType, EventHandlerFunc handler);
    
    // Convenience methods for creating common event types
    /**
     * @brief Register a weather event
     * @param name Unique name for the event
     * @param weatherType Type of weather (Clear, Rainy, Stormy, etc.)
     * @param intensity Intensity of the weather (0.0-1.0)
     */
    void registerWeatherEvent(const std::string& name, const std::string& weatherType, float intensity);
    
    /**
     * @brief Register a scene change event
     * @param name Unique name for the event
     * @param targetScene ID of the target scene
     * @param transitionType Type of transition (fade, dissolve, etc.)
     */
    void registerSceneChangeEvent(const std::string& name, const std::string& targetScene, 
                                 const std::string& transitionType = "fade");
    
    /**
     * @brief Register an NPC spawn event
     * @param name Unique name for the event
     * @param npcType Type of NPC to spawn
     * @param count Number of NPCs to spawn
     * @param spawnRadius Radius around spawn point
     */
    void registerNPCSpawnEvent(const std::string& name, const std::string& npcType, 
                              int count = 1, float spawnRadius = 0.0f);
    
    // Direct trigger methods
    /**
     * @brief Trigger an immediate weather change
     * @param weatherType Type of weather to change to
     * @param transitionTime Time in seconds for the transition
     */
    void triggerWeatherChange(const std::string& weatherType, float transitionTime = 5.0f);
    
    /**
     * @brief Trigger an immediate scene change
     * @param sceneId ID of the scene to change to
     * @param transitionType Type of transition to use
     * @param duration Duration of the transition in seconds
     */
    void triggerSceneChange(const std::string& sceneId, const std::string& transitionType = "fade", 
                           float duration = 1.0f);
    
    /**
     * @brief Trigger an immediate NPC spawn
     * @param npcType Type of NPC to spawn
     * @param x X coordinate for spawn position
     * @param y Y coordinate for spawn position
     */
    void triggerNPCSpawn(const std::string& npcType, float x, float y);
    
    /**
     * @brief Register default events for common game scenarios
     * This creates a set of common events that can be used in most games
     */
    void registerDefaultEvents();
    
    /**
     * @brief Log a message to console/file
     * @param message Message to log
     */
    static void log(const std::string& message);
    
private:
    // Singleton constructor/destructor
    EventSystem();
    ~EventSystem();
    
    // Prevent copying
    EventSystem(const EventSystem&) = delete;
    EventSystem& operator=(const EventSystem&) = delete;
    
    // Register system-level event handlers
    void registerSystemEventHandlers();
    
    // Update event timers with delta time
    void updateEventTimers(float deltaTime);
    
    // Process system-level events
    void processSystemEvents();
    
    // Get current time in milliseconds
    static uint64_t getCurrentTimeMs();
    
    // Static instance
    static EventSystem* s_instance;
    
    // State tracking
    bool m_initialized;
    uint64_t m_lastUpdateTime;
    
    // Event handlers for different event types
    std::unordered_map<std::string, std::vector<EventHandlerFunc>> m_eventHandlers;
};

#endif // EVENT_SYSTEM_HPP