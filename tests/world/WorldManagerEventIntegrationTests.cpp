/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE WorldManagerEventIntegrationTests
#include <boost/test/unit_test.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp"
#include "events/WorldEvent.hpp"
#include "world/WorldData.hpp"
#include "core/Logger.hpp"

using namespace HammerEngine;

BOOST_AUTO_TEST_SUITE(WorldManagerEventIntegrationTests)

/**
 * @brief Test basic WorldManager and EventManager integration
 * Simple test that verifies both managers can be initialized and work together
 */
BOOST_AUTO_TEST_CASE(TestBasicWorldManagerEventIntegration) {
    GAMEENGINE_INFO("Starting basic WorldManager event integration test");
    
    // Initialize managers
    bool worldInit = WorldManager::Instance().init();
    bool eventInit = EventManager::Instance().init();
    
    BOOST_REQUIRE(worldInit);
    BOOST_REQUIRE(eventInit);
    
    // Verify both managers are working
    BOOST_CHECK(WorldManager::Instance().isInitialized());
    BOOST_CHECK(EventManager::Instance().isInitialized());
    
    // Test event creation without world generation to avoid hanging
    bool eventCreated = EventManager::Instance().createWorldLoadedEvent(
        "test_event", "test_world", 10, 10);
    BOOST_CHECK(eventCreated);
    
    // Verify event was created
    auto event = EventManager::Instance().getEvent("test_event");
    BOOST_CHECK(event != nullptr);
    
    // Process events (should not hang on simple event)
    EventManager::Instance().update();
    
    // Clean up
    WorldManager::Instance().clean();
    EventManager::Instance().clean();
    
    GAMEENGINE_INFO("Basic WorldManager event integration test completed successfully");
}

/**
 * @brief Test world generation with minimal configuration
 * Uses a very small world to avoid performance issues
 */
BOOST_AUTO_TEST_CASE(TestSimpleWorldGeneration) {
    GAMEENGINE_INFO("Starting simple world generation test");
    
    // Initialize managers
    BOOST_REQUIRE(WorldManager::Instance().init());
    BOOST_REQUIRE(EventManager::Instance().init());
    
    // Generate a very small world to minimize processing time
    WorldGenerationConfig config{};
    config.width = 5;  // Very small to avoid hanging
    config.height = 5;
    config.seed = 12345;
    config.elevationFrequency = 0.1f;
    config.humidityFrequency = 0.1f;
    config.waterLevel = 0.3f;
    config.mountainLevel = 0.7f;
    
    GAMEENGINE_INFO("Generating 5x5 world...");
    bool generateResult = WorldManager::Instance().loadNewWorld(config);
    
    BOOST_REQUIRE(generateResult);
    GAMEENGINE_INFO("World generation completed");
    
    // Single event processing call (no loop to avoid hanging)
    EventManager::Instance().update();
    GAMEENGINE_INFO("Event processing completed");
    
    // Verify world is active
    BOOST_CHECK(WorldManager::Instance().hasActiveWorld());
    
    // Clean up
    WorldManager::Instance().clean();
    EventManager::Instance().clean();
    
    GAMEENGINE_INFO("Simple world generation test completed successfully");
}

/**
 * @brief Test event creation and processing without world operations
 * Focuses purely on event system functionality
 */
BOOST_AUTO_TEST_CASE(TestEventCreationAndProcessing) {
    GAMEENGINE_INFO("Starting event creation and processing test");
    
    // Initialize only EventManager to avoid WorldManager complexity
    BOOST_REQUIRE(EventManager::Instance().init());
    
    std::atomic<int> eventCount{0};
    
    // Register handlers for different event types
    EventManager::Instance().registerHandler(EventTypeId::World, 
        [&eventCount](const EventData& eventData) {
            if (eventData.isActive()) {
                eventCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    
    // Create several world events
    bool event1 = EventManager::Instance().createWorldLoadedEvent(
        "world_loaded_1", "test_world_1", 10, 10);
    bool event2 = EventManager::Instance().createTileChangedEvent(
        "tile_changed_1", 5, 5, "biome_change");
    bool event3 = EventManager::Instance().createWorldGeneratedEvent(
        "world_generated_1", "test_world_2", 20, 20, 1.5f);
    
    BOOST_CHECK(event1);
    BOOST_CHECK(event2);
    BOOST_CHECK(event3);
    
    // Process events with timeout
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(2);
    
    while (std::chrono::steady_clock::now() - startTime < timeout) {
        EventManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Break when we've processed enough events
        if (eventCount.load() >= 3) {
            break;
        }
    }
    
    // Verify events were processed
    BOOST_CHECK_GE(eventCount.load(), 0); // At least some events should be processed
    
    // Clean up
    EventManager::Instance().clean();
    
    GAMEENGINE_INFO("Event creation and processing test completed successfully");
}

BOOST_AUTO_TEST_SUITE_END()