/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "events/EventFactory.hpp"
// #include "events/NPCSpawnEvent.hpp"  // Commented out to avoid linker errors
#include <iostream>
#include <algorithm>
#include <cctype>

EventFactory::EventFactory() {
    // Register built-in event creators
    registerCustomEventCreator("Weather", [this](const EventDefinition& def) {
        std::string weatherType = def.params.count("weatherType") ? def.params.at("weatherType") : "Clear";
        float intensity = def.numParams.count("intensity") ? def.numParams.at("intensity") : 0.5f;
        float transitionTime = def.numParams.count("transitionTime") ? def.numParams.at("transitionTime") : 5.0f;

        return createWeatherEvent(def.name, weatherType, intensity, transitionTime);
    });

    registerCustomEventCreator("SceneChange", [this](const EventDefinition& def) {
        std::string targetScene = def.params.count("targetScene") ? def.params.at("targetScene") : "";
        std::string transitionType = def.params.count("transitionType") ? def.params.at("transitionType") : "fade";
        float duration = def.numParams.count("duration") ? def.numParams.at("duration") : 1.0f;

        return createSceneChangeEvent(def.name, targetScene, transitionType, duration);
    });

    /*
    registerCustomEventCreator("NPCSpawn", [this](const EventDefinition& def) {
        std::string npcType = def.params.count("npcType") ? def.params.at("npcType") : "";
        int count = static_cast<int>(def.numParams.count("count") ? def.numParams.at("count") : 1.0f);
        float spawnRadius = def.numParams.count("spawnRadius") ? def.numParams.at("spawnRadius") : 0.0f;

        return createNPCSpawnEvent(def.name, npcType, count, spawnRadius);
    });
    */
}

bool EventFactory::init() {
    // Register built-in event creators if they're not already registered
    if (m_eventCreators.find("Weather") == m_eventCreators.end()) {
        registerCustomEventCreator("Weather", [this](const EventDefinition& def) {
            std::string weatherType = def.params.count("weatherType") ? def.params.at("weatherType") : "Clear";
            float intensity = def.numParams.count("intensity") ? def.numParams.at("intensity") : 0.5f;
            float transitionTime = def.numParams.count("transitionTime") ? def.numParams.at("transitionTime") : 5.0f;

            return createWeatherEvent(def.name, weatherType, intensity, transitionTime);
        });

        registerCustomEventCreator("SceneChange", [this](const EventDefinition& def) {
            std::string targetScene = def.params.count("targetScene") ? def.params.at("targetScene") : "";
            std::string transitionType = def.params.count("transitionType") ? def.params.at("transitionType") : "fade";
            float duration = def.numParams.count("duration") ? def.numParams.at("duration") : 1.0f;

            return createSceneChangeEvent(def.name, targetScene, transitionType, duration);
        });

        /*
        registerCustomEventCreator("NPCSpawn", [this](const EventDefinition& def) {
            std::string npcType = def.params.count("npcType") ? def.params.at("npcType") : "";
            int count = static_cast<int>(def.numParams.count("count") ? def.numParams.at("count") : 1.0f);
            float spawnRadius = def.numParams.count("spawnRadius") ? def.numParams.at("spawnRadius") : 0.0f;

            return createNPCSpawnEvent(def.name, npcType, count, spawnRadius);
        });
        */
    }

    std::cout << "EventFactory initialized" << std::endl;
    return true;
}

void EventFactory::clean() {
    m_eventCreators.clear();

    // Re-initialize core creators to ensure they're always available
    registerCustomEventCreator("Weather", [this](const EventDefinition& def) {
        std::string weatherType = def.params.count("weatherType") ? def.params.at("weatherType") : "Clear";
        float intensity = def.numParams.count("intensity") ? def.numParams.at("intensity") : 0.5f;
        float transitionTime = def.numParams.count("transitionTime") ? def.numParams.at("transitionTime") : 5.0f;

        return createWeatherEvent(def.name, weatherType, intensity, transitionTime);
    });

    std::cout << "EventFactory cleaned" << std::endl;
}

EventPtr EventFactory::createEvent(const EventDefinition& def) {
    // Check if we have a creator for this event type
    auto it = m_eventCreators.find(def.type);
    if (it != m_eventCreators.end()) {
        // Use the registered creator function
        EventPtr event = it->second(def);

        // Set common event properties if specified in the definition
        if (event) {
            // Set priority if specified
            if (def.numParams.count("priority")) {
                event->setPriority(static_cast<int>(def.numParams.at("priority")));
            }

            // Set update frequency if specified
            if (def.numParams.count("updateFrequency")) {
                event->setUpdateFrequency(static_cast<int>(def.numParams.at("updateFrequency")));
            }

            // Set cooldown if specified
            if (def.numParams.count("cooldown")) {
                event->setCooldown(def.numParams.at("cooldown"));
            }

            // Set one-time flag if specified
            if (def.boolParams.count("oneTime")) {
                event->setOneTime(def.boolParams.at("oneTime"));
            }

            // Set active state if specified
            if (def.boolParams.count("active")) {
                event->setActive(def.boolParams.at("active"));
            }
        }

        return event;
    }

    std::cout << "Error: Unknown event type '" << def.type << "'" << std::endl;
    return nullptr;
}

EventPtr EventFactory::createWeatherEvent(const std::string& name, const std::string& weatherType,
                                         float intensity, float transitionTime) {
    // Create the weather event
    auto event = std::make_shared<WeatherEvent>(name, weatherType);

    // Configure the weather parameters
    WeatherParams params;
    params.intensity = intensity;
    params.transitionTime = transitionTime;

    // Additional parameter adjustments based on weather type
    if (weatherType == "Rainy" || weatherType == "Stormy") {
        params.visibility = 0.7f - (intensity * 0.4f); // Reduce visibility more with higher intensity
        params.particleEffect = (intensity > 0.7f) ? "heavy_rain" : "rain";
        params.soundEffect = (intensity > 0.7f) ? "thunder_storm" : "rain_ambient";
    } else if (weatherType == "Foggy") {
        params.visibility = 0.8f - (intensity * 0.7f); // Fog drastically reduces visibility
        params.particleEffect = "fog";
    } else if (weatherType == "Snowy") {
        params.visibility = 0.8f - (intensity * 0.3f);
        params.particleEffect = (intensity > 0.7f) ? "heavy_snow" : "snow";
        params.soundEffect = "snow_ambient";
    } else if (weatherType == "Clear") {
        params.visibility = 1.0f;
        params.intensity = 0.0f; // Override intensity for clear weather
    }

    event->setWeatherParams(params);

    return event;
}

EventPtr EventFactory::createSceneChangeEvent(const std::string& name, const std::string& targetScene,
                                            const std::string& transitionType, float duration) {
    // Create the scene change event
    auto event = std::make_shared<SceneChangeEvent>(name, targetScene);

    // Set transition type
    TransitionType type = getTransitionTypeFromString(transitionType);
    event->setTransitionType(type);

    // Configure transition parameters
    TransitionParams params;
    params.duration = duration;

    // Additional parameter adjustments based on transition type
    switch (type) {
        case TransitionType::Fade:
            params.colorR = params.colorG = params.colorB = 0.0f; // Black fade
            params.colorA = 1.0f;
            params.soundEffect = "transition_fade";
            break;
        case TransitionType::Dissolve:
            params.soundEffect = "transition_dissolve";
            break;
        case TransitionType::Wipe:
            params.direction = 0.0f; // Right to left wipe by default
            params.soundEffect = "transition_wipe";
            break;
        case TransitionType::Slide:
            params.direction = 270.0f; // Bottom to top slide by default
            params.soundEffect = "transition_slide";
            break;
        case TransitionType::Instant:
            params.duration = 0.0f;
            params.playSound = false;
            break;
        default:
            break;
    }

    event->setTransitionParams(params);

    return event;
}

EventPtr EventFactory::createNPCSpawnEvent([[maybe_unused]] const std::string& name, [[maybe_unused]] const std::string& npcType,
                                         [[maybe_unused]] int count, [[maybe_unused]] float spawnRadius) {
    // Temporarily disabled to avoid linker errors
    /*
    // Create spawn parameters
    SpawnParameters params;
    params.npcType = npcType;
    params.count = count;
    params.spawnRadius = spawnRadius;

    // Additional default settings
    params.fadeIn = true;
    params.fadeTime = 0.5f;
    params.playSpawnEffect = false;
    params.spawnSoundID = "";

    // Create the event with the parameters
    auto event = std::make_shared<NPCSpawnEvent>(name, params);

    // Default to a circle spawn area around origin
    event->setSpawnArea(0.0f, 0.0f, spawnRadius);

    return std::static_pointer_cast<Event>(event);
    */
    return nullptr;  // Return nullptr until NPCSpawnEvent is properly linked
}

void EventFactory::registerCustomEventCreator(const std::string& eventType,
                                           std::function<EventPtr(const EventDefinition&)> creatorFunc) {
    m_eventCreators[eventType] = creatorFunc;
}

std::vector<EventPtr> EventFactory::createEventSequence(const std::string& name,
                                                     const std::vector<EventDefinition>& events,
                                                     bool sequential) {
    std::vector<EventPtr> createdEvents;
    createdEvents.reserve(events.size());

    // Create all events in the sequence
    for (size_t i = 0; i < events.size(); ++i) {
        // Create a copy of the definition to modify
        EventDefinition def = events[i];

        // If no name is provided in the definition, generate one based on sequence
        if (def.name.empty()) {
            def.name = name + "_" + std::to_string(i + 1);
        }

        // Create the event
        EventPtr event = createEvent(def);
        if (event) {
            // If sequential, set priorities to ensure correct order
            if (sequential) {
                event->setPriority(static_cast<int>(events.size() - i)); // Higher index = lower priority
            }

            createdEvents.push_back(event);
        }
    }

    return createdEvents;
}

WeatherType EventFactory::getWeatherTypeFromString(const std::string& weatherType) {
    // Convert string to lowercase for case-insensitive comparison
    std::string type = weatherType;
    std::transform(type.begin(), type.end(), type.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    if (type == "clear") return WeatherType::Clear;
    if (type == "cloudy") return WeatherType::Cloudy;
    if (type == "rainy") return WeatherType::Rainy;
    if (type == "stormy") return WeatherType::Stormy;
    if (type == "foggy") return WeatherType::Foggy;
    if (type == "snowy") return WeatherType::Snowy;
    if (type == "windy") return WeatherType::Windy;

    // Default to custom type
    return WeatherType::Custom;
}

TransitionType EventFactory::getTransitionTypeFromString(const std::string& transitionType) {
    // Convert string to lowercase for case-insensitive comparison
    std::string type = transitionType;
    std::transform(type.begin(), type.end(), type.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    if (type == "fade") return TransitionType::Fade;
    if (type == "dissolve") return TransitionType::Dissolve;
    if (type == "wipe") return TransitionType::Wipe;
    if (type == "slide") return TransitionType::Slide;
    if (type == "instant") return TransitionType::Instant;

    // Default to custom type
    return TransitionType::Custom;
}
