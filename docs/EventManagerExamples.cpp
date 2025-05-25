/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

/**
 * @file EventManagerExamples.cpp
 * @brief Examples of how to use the EventManager system
 * 
 * This file contains code snippets demonstrating common usage patterns
 * for the EventManager system. These are examples only and not meant to
 * be compiled directly.
 */

#include "managers/EventManager.hpp"
#include "events/WeatherEvent.hpp"
#include "events/SceneChangeEvent.hpp"
#include "events/NPCSpawnEvent.hpp"
#include "events/EventFactory.hpp"
#include "events/EventSystem.hpp"
#include "core/ThreadSystem.hpp"

// Example 1: Basic Weather Event
void weatherEventExample() {
    // Create a weather event
    auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);
    
    // Configure weather parameters
    WeatherParams params;
    params.intensity = 0.7f;
    params.visibility = 0.6f;
    params.transitionTime = 5.0f;
    params.particleEffect = "rain";
    params.soundEffect = "rain_ambient";
    rainEvent->setWeatherParams(params);
    
    // Add a time-of-day condition (only rain during morning)
    rainEvent->setTimeOfDay(5.0f, 10.0f); // 5 AM to 10 AM
    
    // Register the event with the EventManager
    EventManager::Instance().registerEvent("MorningRain", rainEvent);
    
    // Later, to force a weather change immediately:
    EventManager::Instance().changeWeather("Rainy", 3.0f);
}

// Example 2: Scene Change Event
void sceneChangeEventExample() {
    // Create a scene change event using EventFactory
    auto sceneEvent = EventFactory::Instance().createSceneChangeEvent(
        "ToMainMenu",        // Event name
        "MainMenu",          // Target scene
        "fade",              // Transition type
        1.5f                 // Duration in seconds
    );
    
    // Configure a trigger zone (rectangular area)
    static_cast<SceneChangeEvent*>(sceneEvent.get())->setTriggerZone(
        100.0f, 100.0f,      // Top-left corner
        200.0f, 200.0f       // Bottom-right corner
    );
    
    // Require player to press a key when in zone
    static_cast<SceneChangeEvent*>(sceneEvent.get())->setRequirePlayerInput(true);
    static_cast<SceneChangeEvent*>(sceneEvent.get())->setInputKey("E");
    
    // Register the event
    EventManager::Instance().registerEvent("ToMainMenu", sceneEvent);
    
    // Later, to trigger a scene change immediately:
    EventManager::Instance().changeScene("MainMenu", "dissolve", 2.0f);
}

// Example 3: NPC Spawn Event
void npcSpawnEventExample() {
    // Create an NPC spawn event
    auto spawnEvent = std::make_shared<NPCSpawnEvent>("GuardSpawn", "Guard");
    
    // Configure spawn parameters
    SpawnParameters params;
    params.npcType = "Guard";
    params.count = 3;
    params.spawnRadius = 10.0f;
    params.facingPlayer = true;
    params.fadeIn = true;
    params.fadeTime = 1.0f;
    params.playSpawnEffect = true;
    params.spawnEffectID = "smoke";
    params.spawnSoundID = "spawn_sound";
    params.aiBehavior = "patrol";
    spawnEvent->setSpawnParameters(params);
    
    // Set spawn area (circular area)
    spawnEvent->setSpawnArea(150.0f, 150.0f, 20.0f);
    
    // Add proximity trigger (spawn when player gets close)
    spawnEvent->setProximityTrigger(50.0f);
    
    // Set respawn time if all NPCs are defeated
    spawnEvent->setRespawnTime(60.0f); // 1 minute cooldown
    
    // Register the event
    EventManager::Instance().registerEvent("GuardSpawn", spawnEvent);
    
    // Later, to spawn an NPC immediately:
    EventManager::Instance().spawnNPC("Guard", 100.0f, 200.0f);
}

// Example 4: Using EventSystem for simplified integration
void eventSystemExample() {
    // Initialize the event system
    EventSystem::Instance()->init();
    
    // Register common event types with simplified API
    EventSystem::Instance()->registerWeatherEvent("Rain", "Rainy", 0.7f);
    EventSystem::Instance()->registerSceneChangeEvent("ToMainMenu", "MainMenu", "fade");
    EventSystem::Instance()->registerNPCSpawnEvent("GuardSpawn", "Guard", 3, 10.0f);
    
    // Register event handlers for system integration
    EventSystem::Instance()->registerEventHandler("WeatherChange", [](const std::string& weatherType) {
        // Update particle systems, lighting, etc.
        std::cout << "Weather changed to: " << weatherType << std::endl;
    });
    
    // Trigger events directly
    EventSystem::Instance()->triggerWeatherChange("Rainy", 3.0f);
    EventSystem::Instance()->triggerSceneChange("MainMenu", "fade", 1.0f);
    EventSystem::Instance()->triggerNPCSpawn("Guard", 100.0f, 200.0f);
    
    // Don't forget to update the event system each frame
    EventSystem::Instance()->update();
}

// Example 5: Creating an event sequence
void eventSequenceExample() {
    // Define a sequence of events
    std::vector<EventDefinition> dungeonEntranceSequence = {
        // First: Weather changes to foggy
        {"Weather", "DungeonFog", {{"weatherType", "Foggy"}}, {{"intensity", 0.8f}, {"transitionTime", 3.0f}}, {}},
        
        // Second: Spawn guardian NPC
        {"NPCSpawn", "DungeonGuardian", {{"npcType", "DungeonGuard"}}, {{"count", 1.0f}, {"spawnRadius", 0.0f}}, {}},
        
        // Third: Scene change to dungeon interior
        {"SceneChange", "EnterDungeon", {{"targetScene", "DungeonInterior"}, {"transitionType", "fade"}}, 
         {{"duration", 2.0f}}, {}}
    };
    
    // Create the sequence (true = sequential execution)
    auto sequenceEvents = EventFactory::Instance().createEventSequence(
        "DungeonEntrance", dungeonEntranceSequence, true);
    
    // Register all events in the sequence
    for (auto& event : sequenceEvents) {
        EventManager::Instance().registerEvent(event->getName(), event);
    }
    
    // Later, to start the sequence, execute the first event
    EventManager::Instance().executeEvent("DungeonFog");
}

// Example 6: Custom event conditions
void customEventConditionsExample() {
    // Create a weather event with custom conditions
    auto stormEvent = EventFactory::Instance().createWeatherEvent("ThunderStorm", "Stormy", 1.0f);
    
    // Add a random chance condition (30% chance of storm when conditions are checked)
    static_cast<WeatherEvent*>(stormEvent.get())->addRandomChanceCondition(0.3f);
    
    // Add a custom condition based on player health
    int playerHealth = 50; // Placeholder value
    static_cast<WeatherEvent*>(stormEvent.get())->addTimeCondition([playerHealth]() {
        // This would typically check the player's health from the game state
        return playerHealth < 30; // Storm only happens when player health is low
    });
    
    // Register the event
    EventManager::Instance().registerEvent("ThunderStorm", stormEvent);
}

// Example 7: Message-based event communication
void eventMessagingExample() {
    // Create events that respond to messages
    auto weatherSystem = EventFactory::Instance().createWeatherEvent("WeatherSystem", "Clear");
    
    // Register the event
    EventManager::Instance().registerEvent("WeatherSystem", weatherSystem);
    
    // Later, send messages to control the weather
    EventManager::Instance().sendMessageToEvent("WeatherSystem", "SET_RAINY");
    EventManager::Instance().sendMessageToEvent("WeatherSystem", "SET_INTENSITY:0.8");
    
    // Broadcast a message to all events
    EventManager::Instance().broadcastMessage("GAME_PAUSED");
    
    // Send a message to all events of a certain type
    EventManager::Instance().broadcastMessageToType("Weather", "RESET");
}

// Example 8: Thread-safe event processing with ThreadSystem
void threadSafeEventProcessingExample() {
    // First, ensure ThreadSystem is initialized
    if (!Forge::ThreadSystem::Exists()) {
        Forge::ThreadSystem::Instance().init();
    }
    
    // Configure EventManager to use ThreadSystem for multi-threaded processing
    EventManager::Instance().configureThreading(true, 4); // Use up to 4 concurrent tasks
    
    // Register events as usual - EventManager handles thread safety internally
    auto event1 = EventFactory::Instance().createWeatherEvent("Rain", "Rainy");
    auto event2 = EventFactory::Instance().createWeatherEvent("Fog", "Foggy");
    auto event3 = EventFactory::Instance().createWeatherEvent("Snow", "Snowy");
    auto event4 = EventFactory::Instance().createWeatherEvent("Storm", "Stormy");
    
    EventManager::Instance().registerEvent("Rain", event1);
    EventManager::Instance().registerEvent("Fog", event2);
    EventManager::Instance().registerEvent("Snow", event3);
    EventManager::Instance().registerEvent("Storm", event4);
    
    // Events will be processed in parallel through ThreadSystem during update
    EventManager::Instance().update();
    
    // For cleanup during shutdown
    EventManager::Instance().configureThreading(false);
    
    // ThreadSystem cleanup should be handled by the application's main shutdown sequence
    // Forge::ThreadSystem::Instance().clean();
}

// Example 9: Advanced ThreadSystem integration with EventManager
void advancedThreadingExample() {
    // Initialize both systems early in your application startup
    Forge::ThreadSystem::Instance().init(512); // Initialize with custom queue capacity
    EventManager::Instance().init();
    
    // Configure threading options
    EventManager::Instance().configureThreading(true);
    
    // Create many events for batch processing
    for (int i = 0; i < 100; i++) {
        auto event = EventFactory::Instance().createWeatherEvent(
            "Weather" + std::to_string(i), 
            i % 4 == 0 ? "Rainy" : i % 4 == 1 ? "Foggy" : i % 4 == 2 ? "Snowy" : "Stormy"
        );
        EventManager::Instance().registerEvent("Weather" + std::to_string(i), event);
    }
    
    // EventManager automatically organizes events into type-based batches
    // for optimal thread utilization
    EventManager::Instance().update();
    
    // For testing and debugging scenarios, you can disable threading
    EventManager::Instance().configureThreading(false);
    
    // Always clean up properly during application shutdown
    EventManager::Instance().clean();
    // Forge::ThreadSystem::Instance().clean(); // Done by the application
}

// Note: These examples are meant to be instructional, not functional code.
// In a real application, you would integrate these patterns into your game architecture.