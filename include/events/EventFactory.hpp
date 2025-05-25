/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef EVENT_FACTORY_HPP
#define EVENT_FACTORY_HPP

/**
 * @file EventFactory.hpp
 * @brief Factory for creating different types of game events
 *
 * The EventFactory provides a simplified interface for creating and configuring
 * different types of events, abstracting away the complexity of the underlying
 * event implementations.
 */

#include <string>
#include <unordered_map>
#include <functional>
#include "Event.hpp"
#include "WeatherEvent.hpp"
#include "SceneChangeEvent.hpp"


// Simplify creation of event JSON definition
struct EventDefinition {
    std::string type;           // Event type (Weather, SceneChange, NPCSpawn, etc.)
    std::string name;           // Unique name for the event
    std::unordered_map<std::string, std::string> params;  // String parameters
    std::unordered_map<std::string, float> numParams;     // Numeric parameters
    std::unordered_map<std::string, bool> boolParams;     // Boolean parameters
};

class EventFactory {
public:
    /**
     * @brief Get the singleton instance of EventFactory
     * @return Reference to the EventFactory instance
     */
    static EventFactory& Instance() {
        static EventFactory instance;
        return instance;
    }

    /**
     * @brief Initialize the factory
     * @return True if initialization succeeded, false otherwise
     */
    bool init();

    /**
     * @brief Create an event from a definition
     * @param def Event definition with parameters
     * @return Shared pointer to the created event, or nullptr if creation failed
     */
    EventPtr createEvent(const EventDefinition& def);

    /**
     * @brief Create a weather event
     * @param name Unique name for the event
     * @param weatherType Type of weather (Clear, Rainy, Stormy, etc.)
     * @param intensity Intensity of the weather (0.0-1.0)
     * @param transitionTime Time in seconds for weather transitions
     * @return Shared pointer to the created weather event
     */
    EventPtr createWeatherEvent(const std::string& name, const std::string& weatherType,
                               float intensity = 0.5f, float transitionTime = 5.0f);

    /**
     * @brief Create a scene change event
     * @param name Unique name for the event
     * @param targetScene ID of the target scene
     * @param transitionType Type of transition (fade, dissolve, etc.)
     * @param duration Duration of the transition in seconds
     * @return Shared pointer to the created scene change event
     */
    EventPtr createSceneChangeEvent(const std::string& name, const std::string& targetScene,
                                  const std::string& transitionType = "fade", float duration = 1.0f);

    /**
     * @brief Create an NPC spawn event
     * @param name Unique name for the event
     * @param npcType Type of NPC to spawn
     * @param count Number of NPCs to spawn
     * @param spawnRadius Radius around spawn point
     * @return Shared pointer to the created NPC spawn event
     */
    EventPtr createNPCSpawnEvent(const std::string& name, const std::string& npcType,
                               int count = 1, float spawnRadius = 0.0f);

    /**
     * @brief Register a custom event creator function
     * @param eventType Type name for the custom event
     * @param creatorFunc Function that creates the event from a definition
     */
    void registerCustomEventCreator(const std::string& eventType,
                                  std::function<EventPtr(const EventDefinition&)> creatorFunc);

    /**
     * @brief Create a sequence of events that trigger in order
     * @param name Base name for the sequence
     * @param events Vector of event definitions to create
     * @param sequential If true, events trigger one after another; if false, they trigger simultaneously
     * @return Vector of created event pointers
     */
    std::vector<EventPtr> createEventSequence(const std::string& name,
                                            const std::vector<EventDefinition>& events,
                                            bool sequential = true);

    /**
     * @brief Clean up resources used by the factory
     */
    void clean();

private:
    // Singleton constructor/destructor
    EventFactory();
    ~EventFactory() = default;

    // Prevent copying
    EventFactory(const EventFactory&) = delete;
    EventFactory& operator=(const EventFactory&) = delete;

    // Custom event creator functions
    std::unordered_map<std::string, std::function<EventPtr(const EventDefinition&)>> m_eventCreators;

    // Helper methods for event creation
    WeatherType getWeatherTypeFromString(const std::string& weatherType);
    TransitionType getTransitionTypeFromString(const std::string& transitionType);
};

#endif // EVENT_FACTORY_HPP
