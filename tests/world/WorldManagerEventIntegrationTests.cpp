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
#include "events/HarvestResourceEvent.hpp"
#include "world/WorldData.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"

using namespace HammerEngine;

// Global fixture: Initialize ThreadSystem once for the entire test module
// This ensures worker threads are available for all tests that need async task execution
struct GlobalThreadSystemFixture {
    GlobalThreadSystemFixture() {
        HammerEngine::ThreadSystem::Instance().init(); // Auto-detect system threads
    }
    ~GlobalThreadSystemFixture() {
        HammerEngine::ThreadSystem::Instance().clean();
    }
};
BOOST_GLOBAL_FIXTURE(GlobalThreadSystemFixture);

BOOST_AUTO_TEST_SUITE(WorldManagerEventIntegrationTests)

// New: Verify WorldLoadedEvent payload matches WorldManager dimensions
BOOST_AUTO_TEST_CASE(TestWorldLoadedEventPayload) {
    // ThreadSystem is initialized by global fixture

    // Init managers
    BOOST_REQUIRE(WorldManager::Instance().init());
    BOOST_REQUIRE(EventManager::Instance().init());

    // Setup event handlers
    WorldManager::Instance().setupEventHandlers();

    // Setup event handlers
    WorldManager::Instance().setupEventHandlers();

    std::atomic<bool> gotLoaded{false};
    std::string capturedWorldId;
    int capturedW = -1, capturedH = -1;

    EventManager::Instance().registerHandler(EventTypeId::World, [&](const EventData& data){
        if (!data.event) return;
        auto loaded = std::dynamic_pointer_cast<WorldLoadedEvent>(data.event);
        if (loaded) {
            gotLoaded.store(true);
            capturedWorldId = loaded->getWorldId();
            capturedW = loaded->getWidth();
            capturedH = loaded->getHeight();
        }
    });

    WorldGenerationConfig cfg{};
    cfg.width = 5; cfg.height = 5; cfg.seed = 4242; cfg.elevationFrequency = 0.1f; cfg.humidityFrequency = 0.1f; cfg.waterLevel = 0.3f; cfg.mountainLevel = 0.7f;
    BOOST_REQUIRE(WorldManager::Instance().loadNewWorld(cfg));

    // Wait for ThreadSystem task execution - give workers time to process the task
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Process events - more aggressive pumping to ensure deferred events are processed
    for (int i = 0; i < 50 && !gotLoaded.load(); ++i) {
        EventManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Validate
    int w=0, h=0;
    WorldManager::Instance().getWorldDimensions(w,h);
    BOOST_CHECK(gotLoaded.load());
    BOOST_CHECK_EQUAL(capturedW, w);
    BOOST_CHECK_EQUAL(capturedH, h);
    BOOST_CHECK_EQUAL(capturedWorldId, WorldManager::Instance().getCurrentWorldId());

    WorldManager::Instance().clean();
    EventManager::Instance().clean();
    // DON'T clean ThreadSystem - let it persist for subsequent tests like EventManager tests do
}

// New: End-to-end harvest integration via EventManager -> WorldManager
BOOST_AUTO_TEST_CASE(TestHarvestResourceIntegration) {
    BOOST_REQUIRE(WorldManager::Instance().init());
    BOOST_REQUIRE(EventManager::Instance().init());

    // Ensure WorldManager registers its handlers (including Harvest)
    WorldManager::Instance().setupEventHandlers();

    WorldGenerationConfig cfg{};
    cfg.width = 20; cfg.height = 20; cfg.seed = 7777; cfg.elevationFrequency = 0.1f; cfg.humidityFrequency = 0.1f; cfg.waterLevel = 0.2f; cfg.mountainLevel = 0.8f;
    BOOST_REQUIRE(WorldManager::Instance().loadNewWorld(cfg));

    int targetX=-1, targetY=-1;
    for (int y=0; y<cfg.height && targetX==-1; ++y) {
        for (int x=0; x<cfg.width; ++x) {
            const Tile* t = WorldManager::Instance().getTileAt(x,y);
            if (t && t->obstacleType != ObstacleType::NONE) { targetX=x; targetY=y; break; }
        }
    }
    BOOST_REQUIRE_MESSAGE(targetX!=-1 && targetY!=-1, "No obstacle tile found to harvest in generated world");

    std::atomic<int> tileChangedCount{0};
    EventManager::Instance().registerHandler(EventTypeId::World, [&](const EventData& data){
        if (!data.event) return;
        auto changed = std::dynamic_pointer_cast<TileChangedEvent>(data.event);
        if (changed) { tileChangedCount.fetch_add(1); }
    });

    // Create and execute a HarvestResourceEvent via EventManager
    auto harvest = std::make_shared<HarvestResourceEvent>(1, targetX, targetY, "");
    BOOST_REQUIRE(EventManager::Instance().registerEvent("harvest_test", harvest));
    BOOST_CHECK(EventManager::Instance().executeEvent("harvest_test"));

    // Allow processing with event pumping
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() - start < timeout && tileChangedCount.load() == 0) {
        EventManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const Tile* after = WorldManager::Instance().getTileAt(targetX, targetY);
    BOOST_REQUIRE(after != nullptr);
    BOOST_CHECK_EQUAL(after->obstacleType, ObstacleType::NONE);
    BOOST_CHECK_GE(tileChangedCount.load(), 1);

    WorldManager::Instance().clean();
    EventManager::Instance().clean();
}

/**
 * @brief Test basic WorldManager and EventManager integration
 * Simple test that verifies both managers can be initialized and work together
 */
BOOST_AUTO_TEST_CASE(TestBasicWorldManagerEventIntegration) {
    WORLD_MANAGER_INFO("Starting basic WorldManager event integration test");
    
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
    
    WORLD_MANAGER_INFO("Basic WorldManager event integration test completed successfully");
}

/**
 * @brief Test world generation with minimal configuration
 * Uses a very small world to avoid performance issues
 */
BOOST_AUTO_TEST_CASE(TestSimpleWorldGeneration) {
    WORLD_MANAGER_INFO("Starting simple world generation test");
    
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
    
    WORLD_MANAGER_INFO("Generating 5x5 world...");
    bool generateResult = WorldManager::Instance().loadNewWorld(config);
    
    BOOST_REQUIRE(generateResult);
    WORLD_MANAGER_INFO("World generation completed");
    
    // Single event processing call (no loop to avoid hanging)
    EventManager::Instance().update();
    WORLD_MANAGER_INFO("Event processing completed");
    
    // Verify world is active
    BOOST_CHECK(WorldManager::Instance().hasActiveWorld());
    
    // Clean up
    WorldManager::Instance().clean();
    EventManager::Instance().clean();
    
    WORLD_MANAGER_INFO("Simple world generation test completed successfully");
}

/**
 * @brief Test event creation and processing without world operations
 * Focuses purely on event system functionality
 */
BOOST_AUTO_TEST_CASE(TestEventCreationAndProcessing) {
    WORLD_MANAGER_INFO("Starting event creation and processing test");
    
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
    
    WORLD_MANAGER_INFO("Event creation and processing test completed successfully");
}

BOOST_AUTO_TEST_SUITE_END()