/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

/**
 * @file EventManager_Examples.cpp
 * @brief Comprehensive examples of the EventManager as single source of truth
 *
 * This file demonstrates EventManager as the single source of truth for all event operations:
 * - EventManager as the single source of truth for all event functionality
 * - Simple and advanced event creation methods
 * - Direct triggering methods for immediate events
 * - Type-safe handler registration and management
 * - Event sequences and custom event types
 * - Performance monitoring and threading configuration
 * - Real-world integration patterns and best practices
 */

#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

//=============================================================================
// Example 1: Basic Setup and Initialization
//=============================================================================

void example1_BasicSetup() {
    std::cout << "=== Example 1: Basic Setup ===" << std::endl;

    // Initialize ThreadSystem first (required dependency)
    if (!HammerEngine::ThreadSystem::Instance().init()) {
        std::cerr << "Failed to initialize ThreadSystem!" << std::endl;
        return;
    }

    // Initialize EventManager
    if (!EventManager::Instance().init()) {
        std::cerr << "Failed to initialize EventManager!" << std::endl;
        return;
    }

    // Configure threading for optimal performance
    EventManager::Instance().enableThreading(true);
    EventManager::Instance().setThreadingThreshold(100); // Use threading for 100+ events

    std::cout << "EventManager initialized successfully" << std::endl;
    std::cout << "Threading enabled with threshold: 100 events" << std::endl;
    std::cout << "ThreadSystem available: " << HammerEngine::ThreadSystem::Exists() << std::endl;
}

//=============================================================================
// Example 2: Creating Events with Convenience Methods
//=============================================================================

void example2_ConvenienceMethodsCreation() {
    std::cout << "\n=== Example 2: Convenience Methods ===" << std::endl;

    // Weather events - create and register in one call
    bool success1 = EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.5f, 3.0f);
    bool success2 = EventManager::Instance().createWeatherEvent("HeavyStorm", "Stormy", 0.9f, 2.0f);
    bool success3 = EventManager::Instance().createWeatherEvent("ClearSkies", "Clear", 1.0f, 4.0f);

    // Scene change events - create and register in one call
    bool success4 = EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
    bool success5 = EventManager::Instance().createSceneChangeEvent("ToShop", "ShopScene", "dissolve", 2.0f);
    bool success6 = EventManager::Instance().createSceneChangeEvent("ToBattle", "BattleScene", "fade", 2.5f);

    // NPC spawn events - create and register in one call
    bool success7 = EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 25.0f);
    bool success8 = EventManager::Instance().createNPCSpawnEvent("VillagerGroup", "Villager", 5, 40.0f);
    bool success9 = EventManager::Instance().createNPCSpawnEvent("MerchantSpawn", "Merchant", 1, 15.0f);

    // Report results
    int successCount = success1 + success2 + success3 + success4 + success5 + success6 + success7 + success8 + success9;
    std::cout << "Created " << successCount << "/9 events using convenience methods" << std::endl;

    // Show event counts by type using actual EventTypeId
    size_t weatherCount = EventManager::Instance().getEventCount(EventTypeId::Weather);
    size_t sceneCount = EventManager::Instance().getEventCount(EventTypeId::SceneChange);
    size_t npcCount = EventManager::Instance().getEventCount(EventTypeId::NPCSpawn);
    size_t totalCount = EventManager::Instance().getEventCount();

    std::cout << "Event counts - Weather: " << weatherCount
              << ", Scene: " << sceneCount
              << ", NPC: " << npcCount
              << ", Total: " << totalCount << std::endl;
}

//=============================================================================
// Example 3: Direct Event Triggering
//=============================================================================

void example3_DirectTriggering() {
    std::cout << "\n=== Example 3: Direct Event Triggering ===" << std::endl;

    using DM = EventManager::DispatchMode;

    // Direct weather changes (no pre-registration needed)
    bool weatherSuccess1 = EventManager::Instance().changeWeather("Rainy", 3.0f, DM::Deferred);
    bool weatherSuccess2 = EventManager::Instance().changeWeather("Stormy", 1.5f, DM::Immediate);

    // Direct scene transitions
    bool sceneSuccess1 = EventManager::Instance().changeScene("BattleScene", "fade", 2.0f, DM::Deferred);
    bool sceneSuccess2 = EventManager::Instance().changeScene("MainMenu", "dissolve", 1.0f, DM::Immediate);

    // Direct NPC spawning
    bool npcSuccess1 = EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f, DM::Deferred);
    bool npcSuccess2 = EventManager::Instance().spawnNPC("Guard", 250.0f, 150.0f, DM::Immediate);

    // Particle, World, Camera triggers (no pre-registration)
    EventManager::Instance().triggerParticleEffect("Fire", 250.0f, 150.0f, 2.0f, 3.0f, "combat", DM::Deferred);
    EventManager::Instance().triggerWorldLoaded("overworld", 512, 512, DM::Deferred);
    Vector2D newPos(100, 120), oldPos(80, 120);
    EventManager::Instance().triggerCameraMoved(newPos, oldPos, DM::Immediate);

    int successCount = weatherSuccess1 + weatherSuccess2 + sceneSuccess1 + sceneSuccess2 + npcSuccess1 + npcSuccess2;
    std::cout << "Successfully triggered " << successCount << "/6 direct events (+extras)" << std::endl;

    std::cout << "Direct triggering allows immediate event execution without pre-registration" << std::endl;
}

//=============================================================================
// Example X: EventFactory Basics (definition-driven creation)
//=============================================================================

void exampleX_EventFactoryBasics() {
    std::cout << "\n=== Example X: EventFactory Basics ===" << std::endl;

    // Build a weather event definition
    EventDefinition def{.type="Weather", .name="FactoryStorm",
                        .params={{"weatherType","Stormy"}},
                        .numParams={{"intensity",0.9f},{"transitionTime",2.0f}}};
    auto ev = EventFactory::Instance().createEvent(def);
    if (ev) {
        ev->setPriority(6);
        ev->setOneTime(true);
        EventManager::Instance().registerEvent(def.name, ev);
    }

    // Sequence via definitions
    std::vector<EventDefinition> seq = {
        {.type="Weather", .name="StartRain", .params={{"weatherType","Rainy"}}, .numParams={{"intensity",0.5f}}},
        {.type="Weather", .name="GetStormy", .params={{"weatherType","Stormy"}}, .numParams={{"intensity",0.9f}}},
        {.type="Weather", .name="ClearUp", .params={{"weatherType","Clear"}}},
    };
    auto events = EventFactory::Instance().createEventSequence("StoryWeather", seq, true);
    for (auto &e : events) { EventManager::Instance().registerEvent(e->getName(), e); }
}

//=============================================================================
// Example 4: Type-Safe Handlers and Batch Processing
//=============================================================================

void example4_HandlersAndBatching() {
    std::cout << "\n=== Example 4: Type-Safe Handlers ===" << std::endl;

    // Register type-safe handlers using EventTypeId
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData&) {
            std::cout << "Weather event processed! (Type-safe handler)" << std::endl;
        });

    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [](const EventData&) {
            std::cout << "Scene change event processed! (Type-safe handler)" << std::endl;
        });

    EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
        [](const EventData&) {
            std::cout << "NPC spawn event processed! (Type-safe handler)" << std::endl;
        });

    // Check handler counts
    size_t weatherHandlers = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
    size_t sceneHandlers = EventManager::Instance().getHandlerCount(EventTypeId::SceneChange);
    size_t npcHandlers = EventManager::Instance().getHandlerCount(EventTypeId::
NPCSpawn);

    std::cout << "Registered handlers - Weather: " << weatherHandlers
              << ", Scene: " << sceneHandlers
              << ", NPC: " << npcHandlers << std::endl;

    // Execute events by type to test batch processing
    std::cout << "Testing batch execution..." << std::endl;
    int weatherExecuted = EventManager::Instance().executeEventsByType(EventTypeId::Weather);
    int sceneExecuted = EventManager::Instance().executeEventsByType(EventTypeId::SceneChange);
    int npcExecuted = EventManager::Instance().executeEventsByType(EventTypeId::NPCSpawn);

    std::cout << "Batch execution results - Weather: " << weatherExecuted
              << ", Scene: " << sceneExecuted
              << ", NPC: " << npcExecuted << std::endl;
}

//=============================================================================
// Example 5: Performance Monitoring
//=============================================================================

void example5_PerformanceMonitoring() {
    std::cout << "\n=== Example 5: Performance Monitoring ===" << std::endl;

    // Create multiple events for performance testing
    for (int i = 0; i < 10; ++i) {
        EventManager::Instance().createWeatherEvent("PerfTest_Weather_" + std::to_string(i), "Rainy", 0.5f, 3.0f);
        EventManager::Instance().createSceneChangeEvent("PerfTest_Scene_" + std::to_string(i), "TestScene", "fade", 1.0f);
        EventManager::Instance().createNPCSpawnEvent("PerfTest_NPC_" + std::to_string(i), "TestNPC", 1, 10.0f);
    }

    // Reset performance stats for clean measurement
    EventManager::Instance().resetPerformanceStats();

    // Run multiple update cycles to gather performance data
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        EventManager::Instance().update();
    }
    auto end = std::chrono::high_resolution_clock::now();

    auto totalTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;

    // Get performance statistics by event type
    auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    auto sceneStats = EventManager::Instance().getPerformanceStats(EventTypeId::SceneChange);
    auto npcStats = EventManager::Instance().getPerformanceStats(EventTypeId::NPCSpawn);

    std::cout << "Performance Results:" << std::endl;
    std::cout << "Total update time: " << totalTime << "ms for 5 cycles" << std::endl;

    if (weatherStats.callCount > 0) {
        std::cout << "Weather events: " << weatherStats.avgTime << "ms avg, "
                  << weatherStats.callCount << " calls, "
                  << weatherStats.minTime << "-" << weatherStats.maxTime << "ms range" << std::endl;
    }

    if (sceneStats.callCount > 0) {
        std::cout << "Scene events: " << sceneStats.avgTime << "ms avg, "
                  << sceneStats.callCount << " calls, "
                  << sceneStats.minTime << "-" << sceneStats.maxTime << "ms range" << std::endl;
    }

    if (npcStats.callCount > 0) {
        std::cout << "NPC events: " << npcStats.avgTime << "ms avg, "
                  << npcStats.callCount << " calls, "
                  << npcStats.minTime << "-" << npcStats.maxTime << "ms range" << std::endl;
    }

    // Check threading effectiveness
    bool isThreaded = EventManager::Instance().isThreadingEnabled();
    size_t totalEvents = EventManager::Instance().getEventCount();
    std::cout << "Threading enabled: " << (isThreaded ? "yes" : "no")
              << " for " << totalEvents << " total events" << std::endl;
}

//=============================================================================
// Example 6: Event Management Operations
//=============================================================================

void example6_EventManagement() {
    std::cout << "\n=== Example 6: Event Management ===" << std::endl;

    // Create a test event
    bool created = EventManager::Instance().createWeatherEvent("TestEvent", "Rainy", 0.7f, 3.0f);
    std::cout << "Created test event: " << (created ? "success" : "failed") << std::endl;

    // Check if event exists
    bool exists = EventManager::Instance().hasEvent("TestEvent");
    std::cout << "Event exists: " << (exists ? "yes" : "no") << std::endl;

    // Check if event is active
    bool active = EventManager::Instance().isEventActive("TestEvent");
    std::cout << "Event is active: " << (active ? "yes" : "no") << std::endl;

    // Deactivate event
    bool deactivated = EventManager::Instance().setEventActive("TestEvent", false);
    std::cout << "Deactivated event: " << (deactivated ? "success" : "failed") << std::endl;

    // Check active status again
    active = EventManager::Instance().isEventActive("TestEvent");
    std::cout << "Event is now active: " << (active ? "yes" : "no") << std::endl;

    // Reactivate event
    bool reactivated = EventManager::Instance().setEventActive("TestEvent", true);
    std::cout << "Reactivated event: " << (reactivated ? "success" : "failed") << std::endl;

    // Execute specific event
    bool executed = EventManager::Instance().executeEvent("TestEvent");
    std::cout << "Executed event: " << (executed ? "success" : "failed") << std::endl;

    // Get event details
    auto event = EventManager::Instance().getEvent("TestEvent");
    std::cout << "Retrieved event: " << (event ? "success" : "failed") << std::endl;

    // Get events by type
    auto weatherEvents = EventManager::Instance().getEventsByType(EventTypeId::Weather);
    std::cout << "Weather events count: " << weatherEvents.size() << std::endl;

    // Remove event
    bool removed = EventManager::Instance().removeEvent("TestEvent");
    std::cout << "Removed event: " << (removed ? "success" : "failed") << std::endl;

    // Verify removal
    exists = EventManager::Instance().hasEvent("TestEvent");
    std::cout << "Event exists after removal: " << (exists ? "yes" : "no") << std::endl;
}

//=============================================================================
// Example 7: Complete Weather System Integration
//=============================================================================

class WeatherSystem {
private:
    std::vector<std::string> m_weatherTypes{"Clear", "Cloudy", "Rainy", "Stormy", "Foggy"};
    size_t m_currentIndex{0};
    bool m_initialized{false};

public:
    void init() {
        if (m_initialized) return;

        std::cout << "\n=== Example 7: Weather System Integration ===" << std::endl;

        // Create weather events for each type
        for (size_t i = 0; i < m_weatherTypes.size(); ++i) {
            std::string eventName = "weather_" + m_weatherTypes[i];
            bool created = EventManager::Instance().createWeatherEvent(
                eventName, m_weatherTypes[i], 0.7f, 2.0f);

            if (created) {
                std::cout << "Created weather event: " << eventName << std::endl;
            }
        }

        // Register handler for weather changes
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) { onWeatherChanged(data); });

        m_initialized = true;
        std::cout << "Weather system initialized with " << m_weatherTypes.size() << " weather types" << std::endl;
    }

    void cycleWeather() {
        if (!m_initialized) return;

        std::string currentWeather = m_weatherTypes[m_currentIndex];
        m_currentIndex = (m_currentIndex + 1) % m_weatherTypes.size();

        // Use direct trigger for immediate weather change
        bool success = EventManager::Instance().changeWeather(currentWeather, 3.0f);

        if (success) {
            std::cout << "Weather changed to: " << currentWeather << std::endl;
        }
    }

    void getWeatherStats() {
        auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        if (stats.callCount > 0) {
            std::cout << "Weather system stats - Average time: " << stats.avgTime
                      << "ms, Total calls: " << stats.callCount << std::endl;
        } else {
            std::cout << "No weather performance data available" << std::endl;
        }
    }

private:
    void onWeatherChanged(const EventData&) {
        std::cout << "Weather system responding to weather change event" << std::endl;
        // Update weather-dependent game systems here
        updateLighting();
        updateParticleEffects();
        updateSoundscape();
    }

    void updateLighting() { /* Weather lighting updates */ }
    void updateParticleEffects() { /* Weather particle updates */ }
    void updateSoundscape() { /* Weather audio updates */ }
};

//=============================================================================
// Example 8: Threading Performance Comparison
//=============================================================================

void example8_ThreadingPerformance() {
    std::cout << "\n=== Example 8: Threading Performance ===" << std::endl;

    // Create many events for threading test
    const int eventCount = 150;
    for (int i = 0; i < eventCount; ++i) {
        EventManager::Instance().createWeatherEvent("thread_test_" + std::to_string(i), "Rainy", 0.5f, 3.0f);
    }

    std::cout << "Created " << eventCount << " events for threading test" << std::endl;

    // Test single-threaded performance
    EventManager::Instance().enableThreading(false);
    EventManager::Instance().resetPerformanceStats();

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        EventManager::Instance().update();
    }
    auto singleThreadTime = std::chrono::high_resolution_clock::now() - start;

    auto singleMs = std::chrono::duration_cast<std::chrono::milliseconds>(singleThreadTime).count();
    std::cout << "Single-threaded: " << singleMs << "ms for 10 updates" << std::endl;

    // Test multi-threaded performance
    EventManager::Instance().enableThreading(true);
    EventManager::Instance().setThreadingThreshold(50); // Low threshold to force threading
    EventManager::Instance().resetPerformanceStats();

    start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 10; ++i) {
        EventManager::Instance().update();
    }
    auto multiThreadTime = std::chrono::high_resolution_clock::now() - start;

    auto multiMs = std::chrono::duration_cast<std::chrono::milliseconds>(multiThreadTime).count();
    std::cout << "Multi-threaded: " << multiMs << "ms for 10 updates" << std::endl;

    if (multiMs < singleMs) {
        double speedup = double(singleMs) / double(multiMs);
        std::cout << "Threading speedup: " << speedup << "x faster" << std::endl;
    } else if (singleMs < multiMs) {
        double slowdown = double(multiMs) / double(singleMs);
        std::cout << "Threading overhead: " << slowdown << "x slower" << std::endl;
    } else {
        std::cout << "Threading performance: no significant difference" << std::endl;
    }

    // Clean up test events
    for (int i = 0; i < eventCount; ++i) {
        EventManager::Instance().removeEvent("thread_test_" + std::to_string(i));
    }

    std::cout << "Cleaned up threading test events" << std::endl;
}

//=============================================================================
// Example 9: Memory Management and Optimization
//=============================================================================

void example9_MemoryManagement() {
    std::cout << "\n=== Example 9: Memory Management ===" << std::endl;

    // Create and remove many events to test memory management
    const int testEvents = 100;

    // Create events
    for (int i = 0; i < testEvents; ++i) {
        EventManager::Instance().createWeatherEvent("memory_test_" + std::to_string(i), "Rainy", 0.5f, 3.0f);
    }

    size_t initialCount = EventManager::Instance().getEventCount();
    std::cout << "Created " << initialCount << " test events" << std::endl;

    // Remove half of the events
    for (int i = 0; i < testEvents / 2; ++i) {
        EventManager::Instance().removeEvent("memory_test_" + std::to_string(i));
    }

    size_t afterRemovalCount = EventManager::Instance().getEventCount();
    std::cout << "After removing half: " << afterRemovalCount << " events remaining" << std::endl;

    // Compact storage to reclaim memory
    EventManager::Instance().compactEventStorage();
    std::cout << "Compacted event storage to optimize memory usage" << std::endl;

    size_t afterCompactionCount = EventManager::Instance().getEventCount();
    std::cout << "After compaction: " << afterCompactionCount << " events" << std::endl;

    // Clean up remaining test events
    for (int i = testEvents / 2; i < testEvents; ++i) {
        EventManager::Instance().removeEvent("memory_test_" + std::to_string(i));
    }

    std::cout << "Cleaned up all memory test events" << std::endl;
    std::cout << "Final event count: " << EventManager::Instance().getEventCount() << std::endl;
}

//=============================================================================
// Example 10: Complete Game State Integration
//=============================================================================

class ExampleGameState {
private:
    WeatherSystem m_weatherSystem;
    bool m_initialized{false};
    float m_stateTimer{0.0f};

public:
    bool init() {
        if (m_initialized) return true;

        std::cout << "\n=== Example 10: Game State Integration ===" << std::endl;

        // Initialize required systems
        if (!HammerEngine::ThreadSystem::Instance().init()) {
            std::cerr << "Failed to initialize ThreadSystem!" << std::endl;
            return false;
        }

        if (!EventManager::Instance().init()) {
            std::cerr << "Failed to initialize EventManager!" << std::endl;
            return false;
        }

        // Configure for medium-scale game
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(75);

        // Initialize weather system
        m_weatherSystem.init();

        // Create game-specific events
        createGameEvents();

        // Register game-specific handlers
        registerGameHandlers();

        m_initialized = true;
        std::cout << "Game state initialized successfully" << std::endl;
        return true;
    }

    void update(float deltaTime) {
        if (!m_initialized) return;

        m_stateTimer += deltaTime;

        // EventManager is automatically updated by GameEngine
        // But for demonstration, we'll call it here
        EventManager::Instance().update();

        // Cycle weather every 5 seconds
        static int updateCount = 0;
        if (++updateCount % 300 == 0) { // Every 5 seconds at 60 FPS
            m_weatherSystem.cycleWeather();
            showStats();
        }

        // Trigger random events based on game state
        if (static_cast<int>(m_stateTimer) % 10 == 0 && static_cast<int>(m_stateTimer) != static_cast<int>(m_stateTimer - deltaTime)) {
            // Every 10 seconds, spawn some NPCs
            EventManager::Instance().spawnNPC("RandomNPC", 100.0f + (rand() % 200), 100.0f + (rand() % 200));
        }
    }

    void showStats() {
        std::cout << "Game State Stats:" << std::endl;
        std::cout << "State timer: " << m_stateTimer << " seconds" << std::endl;
        std::cout << "Total events: " << EventManager::Instance().getEventCount() << std::endl;

        m_weatherSystem.getWeatherStats();

        // Show performance stats
        auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        auto npcStats = EventManager::Instance().getPerformanceStats(EventTypeId::NPCSpawn);

        if (weatherStats.callCount > 0) {
            std::cout << "Weather performance: " << weatherStats.avgTime << "ms avg" << std::endl;
        }
        if (npcStats.callCount > 0) {
            std::cout << "NPC spawn performance: " << npcStats.avgTime << "ms avg" << std::endl;
        }
    }

    void cleanup() {
        std::cout << "Cleaning up game state..." << std::endl;

        // Clear all handlers
        EventManager::Instance().clearAllHandlers();

        // Clean up EventManager
        EventManager::Instance().clean();

        m_initialized = false;
    }

private:
    void createGameEvents() {
        // Create state-specific events
        EventManager::Instance().createSceneChangeEvent("ExitGame", "MainMenu", "fade", 2.0f);
        EventManager::Instance().createNPCSpawnEvent("InitialGuards", "Guard", 3, 50.0f);

        // Create some environmental events
        EventManager::Instance().createWeatherEvent("GameStateWeather", "Clear", 1.0f, 4.0f);

        std::cout << "Created game state specific events" << std::endl;
    }

    void registerGameHandlers() {
        // Register handlers for game state
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [](const EventData&) {
                std::cout << "Game state handling scene change" << std::endl;
                // Handle scene transitions, save state, etc.
            });

        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [](const EventData&) {
                std::cout << "Game state handling NPC spawn" << std::endl;
                // Add NPC to game world, update AI manager, etc.
            });

        std::cout << "Registered game state event handlers" << std::endl;
    }
};

//=============================================================================
// Main Function - Runs All Examples
//=============================================================================

int main() {
    std::cout << "EventManager Examples - Comprehensive API Demonstration" << std::endl;
    std::cout << "==========================================================" << std::endl;

    try {
        // Run all examples in sequence
        example1_BasicSetup();
        example2_ConvenienceMethodsCreation();
        example3_DirectTriggering();
        example4_HandlersAndBatching();
        example5_PerformanceMonitoring();
        example6_EventManagement();

        // Weather system example
        WeatherSystem weatherSystem;
        weatherSystem.init();
        weatherSystem.cycleWeather();
        weatherSystem.cycleWeather();
        weatherSystem.getWeatherStats();

        example8_ThreadingPerformance();
        example9_MemoryManagement();

        // Game state integration example
        ExampleGameState gameState;
        if (gameState.init()) {
            // Simulate a few update cycles
            for (int i = 0; i < 5; ++i) {
                gameState.update(0.016f); // 60 FPS delta time
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            gameState.cleanup();
        }

        std::cout << "\n==========================================================" << std::endl;
        std::cout << "All examples completed successfully!" << std::endl;
        std::cout << "Final system stats:" << std::endl;
        std::cout << "Total events: " << EventManager::Instance().getEventCount() << std::endl;
        std::cout << "Threading enabled: " << (EventManager::Instance().isThreadingEnabled() ? "yes" : "no") << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Example failed with exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
