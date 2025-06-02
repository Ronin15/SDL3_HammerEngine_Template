/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

/**
 * @file EventManagerExamples.cpp
 * @brief Comprehensive examples of the optimized EventManager API
 * 
 * This file demonstrates the new high-performance EventManager with:
 * - Convenience methods for one-line event creation
 * - Direct triggering methods for immediate events
 * - Realistic performance patterns for games
 * - Proper threading configuration
 * - Performance monitoring examples
 */

#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>

//=============================================================================
// Example 1: Basic Setup and Initialization
//=============================================================================

void example1_BasicSetup() {
    std::cout << "=== Example 1: Basic Setup ===" << std::endl;
    
    // Initialize ThreadSystem first (required for threading)
    if (!Forge::ThreadSystem::Instance().init()) {
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
    EventManager::Instance().setThreadingThreshold(50); // Use threading for 50+ events
    
    std::cout << "EventManager initialized with threading enabled" << std::endl;
    std::cout << "Threading threshold: 50 events" << std::endl;
}

//=============================================================================
// Example 2: Creating Events with New Convenience Methods
//=============================================================================

void example2_ConvenienceMethodsCreation() {
    std::cout << "\n=== Example 2: New Convenience Methods ===" << std::endl;
    
    // Weather events - create and register in one call
    bool success1 = EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.5f, 3.0f);
    bool success2 = EventManager::Instance().createWeatherEvent("HeavyStorm", "Stormy", 0.9f, 2.0f);
    bool success3 = EventManager::Instance().createWeatherEvent("ClearSkies", "Clear", 1.0f, 1.0f);
    
    // Scene change events - create and register in one call
    bool success4 = EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
    bool success5 = EventManager::Instance().createSceneChangeEvent("ToShop", "ShopScene", "slide", 2.0f);
    bool success6 = EventManager::Instance().createSceneChangeEvent("ToBattle", "BattleScene", "dissolve", 2.5f);
    
    // NPC spawn events - create and register in one call
    bool success7 = EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 25.0f);
    bool success8 = EventManager::Instance().createNPCSpawnEvent("VillagerGroup", "Villager", 5, 40.0f);
    bool success9 = EventManager::Instance().createNPCSpawnEvent("MerchantSpawn", "Merchant", 1, 15.0f);
    
    // Report results
    int successCount = success1 + success2 + success3 + success4 + success5 + success6 + success7 + success8 + success9;
    std::cout << "Created " << successCount << "/9 events using convenience methods" << std::endl;
    
    // Show event counts by type
    size_t weatherCount = EventManager::Instance().getEventCount(EventTypeId::Weather);
    size_t sceneCount = EventManager::Instance().getEventCount(EventTypeId::SceneChange);
    size_t npcCount = EventManager::Instance().getEventCount(EventTypeId::NPCSpawn);
    
    std::cout << "Event counts - Weather: " << weatherCount 
              << ", Scene: " << sceneCount 
              << ", NPC: " << npcCount << std::endl;
}

//=============================================================================
// Example 3: Direct Event Triggering
//=============================================================================

void example3_DirectTriggering() {
    std::cout << "\n=== Example 3: Direct Event Triggering ===" << std::endl;
    
    // Direct weather changes (no pre-registration needed)
    bool weatherSuccess1 = EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
    bool weatherSuccess2 = EventManager::Instance().changeWeather("Stormy", 1.5f); // Alternative method name
    
    // Direct scene transitions
    bool sceneSuccess1 = EventManager::Instance().triggerSceneChange("BattleScene", "fade", 2.0f);
    bool sceneSuccess2 = EventManager::Instance().changeScene("MainMenu", "dissolve", 1.0f); // Alternative method name
    
    // Direct NPC spawning
    bool npcSuccess1 = EventManager::Instance().triggerNPCSpawn("Merchant", 100.0f, 200.0f);
    bool npcSuccess2 = EventManager::Instance().spawnNPC("Guard", 250.0f, 150.0f); // Alternative method name
    
    std::cout << "Direct triggering results:" << std::endl;
    std::cout << "Weather changes: " << (weatherSuccess1 + weatherSuccess2) << "/2" << std::endl;
    std::cout << "Scene changes: " << (sceneSuccess1 + sceneSuccess2) << "/2" << std::endl;
    std::cout << "NPC spawns: " << (npcSuccess1 + npcSuccess2) << "/2" << std::endl;
}

//=============================================================================
// Example 4: Event Handlers and Batch Processing
//=============================================================================

void example4_HandlersAndBatching() {
    std::cout << "\n=== Example 4: Event Handlers and Batch Processing ===" << std::endl;
    
    // Register handlers by event type for optimal performance
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData& /*data*/) {
            std::cout << "Weather event processed (batch)" << std::endl;
            // Handle weather changes here
        });
    
    EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
        [](const EventData& /*data*/) {
            std::cout << "NPC spawn event processed (batch)" << std::endl;
            // Handle NPC spawning here
        });
    
    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [](const EventData& /*data*/) {
            std::cout << "Scene change event processed (batch)" << std::endl;
            // Handle scene transitions here
        });
    
    std::cout << "Event handlers registered for batch processing" << std::endl;
    
    // Process all events efficiently
    EventManager::Instance().update();
    std::cout << "Batch update completed" << std::endl;
}

//=============================================================================
// Example 5: Performance Monitoring
//=============================================================================

void example5_PerformanceMonitoring() {
    std::cout << "\n=== Example 5: Performance Monitoring ===" << std::endl;
    
    // Create some events for testing
    for (int i = 0; i < 10; ++i) {
        EventManager::Instance().createWeatherEvent("test_weather_" + std::to_string(i), "Rainy", 0.5f, 1.0f);
        EventManager::Instance().createNPCSpawnEvent("test_npc_" + std::to_string(i), "Guard", 1, 10.0f);
    }
    
    // Process events to generate performance data
    EventManager::Instance().update();
    
    // Get performance statistics by event type
    auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    auto npcStats = EventManager::Instance().getPerformanceStats(EventTypeId::NPCSpawn);
    
    std::cout << "Performance Statistics:" << std::endl;
    std::cout << "Weather Events:" << std::endl;
    std::cout << "  Average time: " << weatherStats.avgTime << "ms" << std::endl;
    std::cout << "  Min time: " << weatherStats.minTime << "ms" << std::endl;
    std::cout << "  Max time: " << weatherStats.maxTime << "ms" << std::endl;
    std::cout << "  Total calls: " << weatherStats.callCount << std::endl;
    
    std::cout << "NPC Events:" << std::endl;
    std::cout << "  Average time: " << npcStats.avgTime << "ms" << std::endl;
    std::cout << "  Min time: " << npcStats.minTime << "ms" << std::endl;
    std::cout << "  Max time: " << npcStats.maxTime << "ms" << std::endl;
    std::cout << "  Total calls: " << npcStats.callCount << std::endl;
    
    // Get event counts
    size_t totalEvents = EventManager::Instance().getEventCount();
    size_t weatherEvents = EventManager::Instance().getEventCount(EventTypeId::Weather);
    size_t npcEvents = EventManager::Instance().getEventCount(EventTypeId::NPCSpawn);
    
    std::cout << "Event Counts:" << std::endl;
    std::cout << "  Total: " << totalEvents << std::endl;
    std::cout << "  Weather: " << weatherEvents << std::endl;
    std::cout << "  NPC: " << npcEvents << std::endl;
    
    // Reset performance statistics for fresh monitoring
    EventManager::Instance().resetPerformanceStats();
    std::cout << "Performance statistics reset" << std::endl;
}

//=============================================================================
// Example 6: Event Management and Control
//=============================================================================

void example6_EventManagement() {
    std::cout << "\n=== Example 6: Event Management and Control ===" << std::endl;
    
    // Create a test event
    bool created = EventManager::Instance().createWeatherEvent("TestEvent", "Rainy", 0.8f, 2.0f);
    std::cout << "Created test event: " << (created ? "Success" : "Failed") << std::endl;
    
    // Query events
    bool hasEvent = EventManager::Instance().hasEvent("TestEvent");
    std::cout << "Has TestEvent: " << (hasEvent ? "Yes" : "No") << std::endl;
    
    // Get specific event
    auto event = EventManager::Instance().getEvent("TestEvent");
    std::cout << "Retrieved TestEvent: " << (event ? "Success" : "Failed") << std::endl;
    
    // Get events by type
    auto weatherEvents = EventManager::Instance().getEventsByType(EventTypeId::Weather);
    std::cout << "Weather events count: " << weatherEvents.size() << std::endl;
    
    // Control event state
    EventManager::Instance().setEventActive("TestEvent", false);
    bool isActive = EventManager::Instance().isEventActive("TestEvent");
    std::cout << "TestEvent active after disable: " << (isActive ? "Yes" : "No") << std::endl;
    
    // Re-enable event
    EventManager::Instance().setEventActive("TestEvent", true);
    isActive = EventManager::Instance().isEventActive("TestEvent");
    std::cout << "TestEvent active after enable: " << (isActive ? "Yes" : "No") << std::endl;
    
    // Remove event
    bool removed = EventManager::Instance().removeEvent("TestEvent");
    std::cout << "Removed TestEvent: " << (removed ? "Success" : "Failed") << std::endl;
    
    // Verify removal
    hasEvent = EventManager::Instance().hasEvent("TestEvent");
    std::cout << "Has TestEvent after removal: " << (hasEvent ? "Yes" : "No") << std::endl;
}

//=============================================================================
// Example 7: Realistic Game Scenario - Weather System
//=============================================================================

class WeatherSystem {
private:
    std::vector<std::string> m_weatherTypes = {"Clear", "Cloudy", "Rainy", "Stormy", "Foggy"};
    size_t m_currentIndex = 0;

public:
    void init() {
        std::cout << "\n=== Example 7: Realistic Weather System ===" << std::endl;
        
        // Create weather events for each type with realistic parameters
        for (size_t i = 0; i < m_weatherTypes.size(); ++i) {
            const auto& weather = m_weatherTypes[i];
            float intensity = (weather == "Clear") ? 0.0f : 0.5f + (i * 0.1f);
            float duration = 2.0f + (i * 0.5f); // Varying transition times
            
            bool success = EventManager::Instance().createWeatherEvent(
                "weather_" + weather, weather, intensity, duration);
            
            std::cout << "Created " << weather << " weather event: " 
                      << (success ? "Success" : "Failed") << std::endl;
        }
        
        // Register weather change handler
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& /*data*/) { 
                onWeatherChanged(); 
            });
        
        std::cout << "Weather system initialized with " << m_weatherTypes.size() << " weather types" << std::endl;
    }
    
    void cycleWeather() {
        std::string nextWeather = m_weatherTypes[m_currentIndex];
        m_currentIndex = (m_currentIndex + 1) % m_weatherTypes.size();
        
        // Use convenience method for immediate weather change
        float transitionTime = 2.0f + (m_currentIndex * 0.3f); // Varying transition times
        bool success = EventManager::Instance().triggerWeatherChange(nextWeather, transitionTime);
        
        std::cout << "Triggered weather change to " << nextWeather 
                  << " (transition: " << transitionTime << "s): " 
                  << (success ? "Success" : "Failed") << std::endl;
    }
    
    void getWeatherStats() {
        auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        size_t weatherCount = EventManager::Instance().getEventCount(EventTypeId::Weather);
        
        std::cout << "Weather System Stats:" << std::endl;
        std::cout << "  Registered weather events: " << weatherCount << std::endl;
        std::cout << "  Average processing time: " << stats.avgTime << "ms" << std::endl;
        std::cout << "  Total weather changes: " << stats.callCount << std::endl;
    }
    
private:
    void onWeatherChanged() {
        std::cout << "Weather system responding to weather change event" << std::endl;
        // Update weather-dependent game systems:
        // - Particle effects
        // - Lighting systems
        // - NPC behavior
        // - Player visibility
        // - Sound effects
    }
};

//=============================================================================
// Example 8: Performance Scaling Demonstration
//=============================================================================

void example8_PerformanceScaling() {
    std::cout << "\n=== Example 8: Performance Scaling Demonstration ===" << std::endl;
    
    // Test different event counts to show scaling
    std::vector<size_t> testSizes = {10, 50, 100, 200}; // Realistic game scales
    
    for (size_t testSize : testSizes) {
        std::cout << "\nTesting with " << testSize << " events:" << std::endl;
        
        // Reset performance stats for clean measurement
        EventManager::Instance().resetPerformanceStats();
        
        // Create test events (realistic mix)
        size_t weatherEvents = testSize / 3;      // ~33% weather events
        size_t npcEvents = testSize / 2;          // ~50% NPC events  
        size_t sceneEvents = testSize - weatherEvents - npcEvents; // Remaining scene events
        
        // Create weather events
        for (size_t i = 0; i < weatherEvents; ++i) {
            std::string weatherType = (i % 2 == 0) ? "Rainy" : "Clear";
            EventManager::Instance().createWeatherEvent(
                "scale_weather_" + std::to_string(i), weatherType, 0.5f, 1.0f);
        }
        
        // Create NPC events
        for (size_t i = 0; i < npcEvents; ++i) {
            std::string npcType = (i % 2 == 0) ? "Guard" : "Villager";
            EventManager::Instance().createNPCSpawnEvent(
                "scale_npc_" + std::to_string(i), npcType, 1, 20.0f);
        }
        
        // Create scene events
        for (size_t i = 0; i < sceneEvents; ++i) {
            EventManager::Instance().createSceneChangeEvent(
                "scale_scene_" + std::to_string(i), "TestScene", "fade", 1.0f);
        }
        
        // Measure update performance
        auto startTime = std::chrono::high_resolution_clock::now();
        EventManager::Instance().update();
        auto endTime = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        double updateTimeMs = duration.count() / 1000.0;
        
        // Get final counts
        size_t finalTotal = EventManager::Instance().getEventCount();
        
        std::cout << "  Created events: " << finalTotal << std::endl;
        std::cout << "  Update time: " << updateTimeMs << "ms" << std::endl;
        std::cout << "  Events/second: " << (finalTotal / (updateTimeMs / 1000.0)) << std::endl;
        std::cout << "  Threading enabled: " << (EventManager::Instance().isThreadingEnabled() ? "Yes" : "No") << std::endl;
        
        // Clean up for next test
        EventManager::Instance().compactEventStorage();
    }
}

//=============================================================================
// Example 9: Memory Management
//=============================================================================

void example9_MemoryManagement() {
    std::cout << "\n=== Example 9: Memory Management ===" << std::endl;
    
    // Create many events to demonstrate memory management
    std::cout << "Creating 100 temporary events..." << std::endl;
    for (int i = 0; i < 100; ++i) {
        EventManager::Instance().createWeatherEvent("temp_" + std::to_string(i), "Rainy", 0.5f, 1.0f);
    }
    
    size_t beforeCount = EventManager::Instance().getEventCount();
    std::cout << "Events before cleanup: " << beforeCount << std::endl;
    
    // Remove temporary events
    std::cout << "Removing temporary events..." << std::endl;
    for (int i = 0; i < 100; ++i) {
        EventManager::Instance().removeEvent("temp_" + std::to_string(i));
    }
    
    size_t afterRemoval = EventManager::Instance().getEventCount();
    std::cout << "Events after removal: " << afterRemoval << std::endl;
    
    // Compact storage to reclaim memory
    std::cout << "Compacting event storage..." << std::endl;
    EventManager::Instance().compactEventStorage();
    
    size_t afterCompact = EventManager::Instance().getEventCount();
    std::cout << "Events after compaction: " << afterCompact << std::endl;
    
    std::cout << "Memory management complete" << std::endl;
}

//=============================================================================
// Example 10: Complete Game Integration Example
//=============================================================================

class GameState {
private:
    WeatherSystem m_weatherSystem;
    bool m_initialized = false;

public:
    bool init() {
        std::cout << "\n=== Example 10: Complete Game Integration ===" << std::endl;
        
        // Initialize core systems
        if (!Forge::ThreadSystem::Instance().init()) {
            std::cerr << "Failed to initialize ThreadSystem!" << std::endl;
            return false;
        }
        
        if (!EventManager::Instance().init()) {
            std::cerr << "Failed to initialize EventManager!" << std::endl;
            return false;
        }
        
        // Configure for game-scale performance
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(50);
        
        // Initialize weather system
        m_weatherSystem.init();
        
        // Create initial game events
        createGameEvents();
        
        // Register global game handlers
        registerGameHandlers();
        
        m_initialized = true;
        std::cout << "Game state initialized successfully" << std::endl;
        return true;
    }
    
    void update() {
        if (!m_initialized) return;
        
        // Single efficient update call processes all events
        EventManager::Instance().update();
        
        // Example: Cycle weather every few seconds in demo
        static int updateCount = 0;
        if (++updateCount % 300 == 0) { // Every ~5 seconds at 60 FPS
            m_weatherSystem.cycleWeather();
        }
    }
    
    void showStats() {
        if (!m_initialized) return;
        
        std::cout << "\n=== Game State Statistics ===" << std::endl;
        
        // Show event counts by type
        size_t total = EventManager::Instance().getEventCount();
        size_t weather = EventManager::Instance().getEventCount(EventTypeId::Weather);
        size_t npcs = EventManager::Instance().getEventCount(EventTypeId::NPCSpawn);
        size_t scenes = EventManager::Instance().getEventCount(EventTypeId::SceneChange);
        
        std::cout << "Event Counts:" << std::endl;
        std::cout << "  Total: " << total << std::endl;
        std::cout << "  Weather: " << weather << std::endl;
        std::cout << "  NPC Spawn: " << npcs << std::endl;
        std::cout << "  Scene Change: " << scenes << std::endl;
        
        // Show performance stats
        m_weatherSystem.getWeatherStats();
        
        // Show threading status
        std::cout << "Threading: " << (EventManager::Instance().isThreadingEnabled() ? "Enabled" : "Disabled") << std::endl;
    }
    
    void cleanup() {
        std::cout << "Cleaning up game state..." << std::endl;
        EventManager::Instance().clean();
        m_initialized = false;
    }

private:
    void createGameEvents() {
        // Create level-specific events
        EventManager::Instance().createSceneChangeEvent("level_complete", "NextLevel", "fade", 2.0f);
        EventManager::Instance().createSceneChangeEvent("game_over", "MainMenu", "dissolve", 3.0f);
        EventManager::Instance().createSceneChangeEvent("pause_game", "PauseMenu", "instant", 0.0f);
        
        // Create gameplay events
        EventManager::Instance().createNPCSpawnEvent("enemy_wave", "Enemy", 5, 50.0f);
        EventManager::Instance().createNPCSpawnEvent("friendly_npcs", "Villager", 3, 30.0f);
        
        std::cout << "Created initial game events" << std::endl;
    }
    
    void registerGameHandlers() {
        // Register scene change handler
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [](const EventData& /*data*/) {
                std::cout << "Game: Scene change detected, updating game state" << std::endl;
                // Handle scene transitions:
                // - Save player progress
                // - Update UI
                // - Load new assets
            });
        
        // Register NPC spawn handler
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [](const EventData& /*data*/) {
                std::cout << "Game: NPC spawn detected, updating entity systems" << std::endl;
                // Handle NPC spawning:
                // - Create entity
                // - Assign AI behavior
                // - Update spatial systems
            });
        
        std::cout << "Registered game event handlers" << std::endl;
    }
};

//=============================================================================
// Main Example Runner
//=============================================================================

int main() {
    std::cout << "EventManager Optimized API Examples" << std::endl;
    std::cout << "====================================" << std::endl;
    
    // Run all examples
    example1_BasicSetup();
    example2_ConvenienceMethodsCreation();
    example3_DirectTriggering();
    example4_HandlersAndBatching();
    example5_PerformanceMonitoring();
    example6_EventManagement();
    
    // Run weather system example
    WeatherSystem weatherSystem;
    weatherSystem.init();
    weatherSystem.cycleWeather();
    weatherSystem.cycleWeather();
    weatherSystem.getWeatherStats();
    
    example8_PerformanceScaling();
    example9_MemoryManagement();
    
    // Run complete game integration example
    GameState gameState;
    if (gameState.init()) {
        // Simulate a few update cycles
        for (int i = 0; i < 5; ++i) {
            gameState.update();
        }
        gameState.showStats();
        gameState.cleanup();
    }
    
    std::cout << "\n====================================" << std::endl;
    std::cout << "All examples completed successfully!" << std::endl;
    
    return 0;
}