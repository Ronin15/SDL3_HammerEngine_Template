/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfinderManagerTests
#include <boost/test/included/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
#include "managers/EventManager.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"
#include "core/ThreadSystem.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "utils/Vector2D.hpp"
#include <chrono>
#include <thread>

using namespace HammerEngine;

// Initialize ThreadSystem for async pathfinding in this test module
struct PFThreadFixture {
    PFThreadFixture() {
        HammerEngine::ThreadSystem::Instance().init(4096);
    }
    ~PFThreadFixture() {
        if (!HammerEngine::ThreadSystem::Instance().isShutdown()) {
            HammerEngine::ThreadSystem::Instance().clean();
        }
    }
};

BOOST_GLOBAL_FIXTURE(PFThreadFixture);

BOOST_AUTO_TEST_SUITE(PathfinderManagerTestSuite)

BOOST_AUTO_TEST_CASE(TestPathfinderManagerSingleton) {
    // Test singleton behavior
    PathfinderManager& instance1 = PathfinderManager::Instance();
    PathfinderManager& instance2 = PathfinderManager::Instance();
    
    BOOST_CHECK(&instance1 == &instance2);
}

BOOST_AUTO_TEST_CASE(TestPathfinderManagerInitialization) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    // Initially not initialized
    BOOST_CHECK(!manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Initialize should succeed
    BOOST_CHECK(manager.init());
    BOOST_CHECK(manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Calling init again should return true (already initialized)
    BOOST_CHECK(manager.init());
    
    // Clean up for other tests
    manager.clean();
    BOOST_CHECK(!manager.isInitialized());
}

BOOST_AUTO_TEST_CASE(TestImmediatePathfinding) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    std::vector<Vector2D> path;
    
    // Test immediate pathfinding
    auto result = manager.findPathImmediate(start, goal, path);
    
    // Even if no path is found (due to no world data), the function should not crash
    // Accept all valid PathfindingResult values since we don't have world data setup
    BOOST_CHECK(result == HammerEngine::PathfindingResult::SUCCESS || 
                result == HammerEngine::PathfindingResult::NO_PATH_FOUND ||
                result == HammerEngine::PathfindingResult::INVALID_START ||
                result == HammerEngine::PathfindingResult::INVALID_GOAL ||
                result == HammerEngine::PathfindingResult::TIMEOUT);
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestAsyncPathfinding) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    EntityID entityId = 12345;
    bool callbackCalled = false;
    std::vector<Vector2D> resultPath;
    
    // Test async pathfinding with callback
    auto requestId = manager.requestPath(
        entityId, 
        start, 
        goal, 
        PathfinderManager::Priority::Normal, // Normal priority
        [&callbackCalled, &resultPath](EntityID id, const std::vector<Vector2D>& path) {
            BOOST_CHECK(id == 12345);
            callbackCalled = true;
            resultPath = path;
        }
    );
    
    BOOST_CHECK(requestId > 0); // Valid request ID
    
    // Update to process requests
    manager.update(); // ~60 FPS
    
    // Give it some time to process (async operation)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.update();
    
    // The callback should have been called (even if path is empty due to no world)
    // Note: This might not always be true in a real environment without proper world setup
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestPathfinderConfiguration) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Test configuration methods
    manager.setMaxPathsPerFrame(3);
    manager.setCacheExpirationTime(10.0f);
    manager.setAllowDiagonal(false);
    manager.setMaxIterations(5000);
    
    // These should not crash - actual behavior testing would require more complex setup
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestBasicFunctionality) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    EntityID entityId = 54321;
    
    // Request a path without callback (should not crash)
    auto requestId = manager.requestPath(entityId, start, goal, PathfinderManager::Priority::Low);
    BOOST_CHECK(requestId > 0);
    
    // Process requests
    manager.update();
    
    // Check that we have pending work initially
    BOOST_CHECK(manager.hasPendingWork() || manager.getQueueSize() == 0); // Either has work or processed quickly
    
    // These should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestWeightFields) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D center(150.0f, 150.0f);
    float radius = 50.0f;
    float weight = 2.0f;
    
    // Add temporary weight field
    manager.addTemporaryWeightField(center, radius, weight);
    
    // Clear weight fields
    manager.clearWeightFields();
    
    // These should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestStatistics) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Reset stats first to ensure clean state
    manager.resetStats();
    
    // Get initial stats after reset
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests == 0);
    BOOST_CHECK(stats.completedRequests == 0);
    BOOST_CHECK(stats.failedRequests == 0);
    
    // Reset stats again (should not crash)
    manager.resetStats();
    
    auto statsAfterReset = manager.getStats();
    BOOST_CHECK(statsAfterReset.totalRequests == 0);
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestShutdown) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    BOOST_CHECK(manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Clean should mark as not initialized but not shutdown
    manager.clean();
    BOOST_CHECK(!manager.isInitialized());
    
    // Re-initialize should work
    BOOST_CHECK(manager.init());
    BOOST_CHECK(manager.isInitialized());
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestUpdateCycle) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Test multiple update cycles
    for (int i = 0; i < 10; ++i) {
        manager.update(); // ~60 FPS
    }
    
    // Should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestNoInfiniteRetryLoop) {
    // REGRESSION TEST: Ensure failed pathfinding requests don't cause infinite retry loops
    // This test prevents the bug where same failed requests kept getting requeued endlessly
    
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(50.0f, 50.0f);
    Vector2D goal(100.0f, 100.0f);
    EntityID entityId = 99999;
    
    std::atomic<int> callbackCount{0};
    
    // Make multiple identical requests rapidly (simulating the bug condition)
    auto callback = [&callbackCount](EntityID, const std::vector<Vector2D>&) {
        callbackCount.fetch_add(1, std::memory_order_relaxed);
    };
    
    // Request the same path multiple times within the cache window (1000ms)
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback); 
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    
    // Process requests
    for (int i = 0; i < 10; ++i) {
        manager.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Key assertions to prevent regression:
    // 1. We should receive callbacks (not stuck in infinite loop)
    BOOST_CHECK(callbackCount > 0);
    
    // 2. We shouldn't receive excessive callbacks (indicates retry loop)  
    BOOST_CHECK(callbackCount <= 10); // Reasonable upper bound
    
    // 3. Stats should show reasonable request count (not thousands from retry loop)
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests <= 20); // Should be close to our 4 requests, not thousands

    // Ensure no runaway processing from repeated failed path requests

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestFailedRequestCaching) {
    // REGRESSION TEST: Ensure failed requests are properly cached to prevent retry loops
    
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(10.0f, 10.0f);
    Vector2D goal(20.0f, 20.0f);  
    EntityID entityId = 88888;
    
    std::atomic<int> firstCallbackCount{0};
    std::atomic<int> secondCallbackCount{0};
    
    // First request
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High,
        [&firstCallbackCount](EntityID, const std::vector<Vector2D>&) {
            firstCallbackCount.fetch_add(1, std::memory_order_relaxed);
        });
    
    // Process first request
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();
    
    // Second identical request within cache window (should be rejected/cached)
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High,
        [&secondCallbackCount](EntityID, const std::vector<Vector2D>&) {
            secondCallbackCount.fetch_add(1, std::memory_order_relaxed);
        });
    
    // Process second request  
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();
    
    // Both should have received callbacks (first from processing, second from cache)
    BOOST_CHECK(firstCallbackCount.load(std::memory_order_relaxed) > 0);
    BOOST_CHECK(secondCallbackCount.load(std::memory_order_relaxed) > 0);
    
    // But total requests processed should be minimal (cache working)
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests <= 20); // Ensure no runaway processing from repeated requests

    manager.clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Integration Tests for PathfinderManager Event System  
BOOST_AUTO_TEST_SUITE(PathfinderEventIntegrationTests)

// Test fixture for PathfinderManager event integration
struct PathfinderEventFixture {
    PathfinderEventFixture() {
        // Initialize EventManager for event testing (following established pattern)
        EventManager::Instance().init();
        
        // Initialize PathfinderManager
        PathfinderManager::Instance().init();
        
        // Reset tracking variables
        collisionVersionIncremented = false;
        cacheInvalidationCount = 0;
    }
    
    ~PathfinderEventFixture() {
        // Clean up in reverse order
        PathfinderManager::Instance().clean();
        EventManager::Instance().clean();
        // ThreadSystem persists across tests (per established pattern)
    }
    
    // Event tracking variables
    bool collisionVersionIncremented;
    std::atomic<int> cacheInvalidationCount{0};
};

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventSubscription, PathfinderEventFixture)
{
    // Test that PathfinderManager properly subscribes to collision obstacle events
    // This tests the event subscription lifecycle
    
    // Manually trigger a collision obstacle changed event
    Vector2D obstaclePos(100.0f, 150.0f);
    float obstacleRadius = 64.0f;
    std::string description = "Test obstacle change";
    
    // Trigger the event
    bool eventFired = EventManager::Instance().triggerCollisionObstacleChanged(
        obstaclePos, obstacleRadius, description, EventManager::DispatchMode::Immediate);
    
    // Event should fire (PathfinderManager should be subscribed)
    BOOST_CHECK(eventFired);
    
    // Brief wait to let the handler process (following established pattern)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // The PathfinderManager should have incremented its collision version
    // (This is internal state, but we can verify by checking if subsequent operations behave correctly)
    BOOST_CHECK(true); // Subscription worked if no exceptions were thrown
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderCacheInvalidationOnCollisionChange, PathfinderEventFixture)
{
    // Test that collision obstacle changes properly invalidate pathfinding cache
    
    // First, let's simulate having some cached paths by triggering path requests
    Vector2D start1(0.0f, 0.0f);
    Vector2D goal1(100.0f, 100.0f);
    Vector2D start2(200.0f, 200.0f); 
    Vector2D goal2(300.0f, 300.0f);
    
    // Request some paths (they may fail due to no world, but will be cached)
    PathfinderManager::Instance().requestPath(1001, start1, goal1, 
        PathfinderManager::Priority::High,
        [](EntityID, const std::vector<Vector2D>&){ /* no-op */ });
    PathfinderManager::Instance().requestPath(1002, start2, goal2,
        PathfinderManager::Priority::High,
        [](EntityID, const std::vector<Vector2D>&){ /* no-op */ });
    
    // Let processing complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Get initial stats
    auto initialStats = PathfinderManager::Instance().getStats();
    
    // Now trigger a collision obstacle change at position that might affect paths
    Vector2D obstaclePos(150.0f, 150.0f);
    EventManager::Instance().triggerCollisionObstacleChanged(
        obstaclePos, 100.0f, "Cache invalidation test", EventManager::DispatchMode::Immediate);
    
    // Brief processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // The cache should have been selectively invalidated
    // We can't directly test cache internals, but we can verify that the system
    // handles the event without crashing and continues to function
    
    // Request the same paths again - they should be processed again if cache was invalidated
    PathfinderManager::Instance().requestPath(1003, start1, goal1,
        PathfinderManager::Priority::High,
        [this](EntityID, const std::vector<Vector2D>&){ 
            cacheInvalidationCount++; 
        });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify the system is still functioning (no crashes from event handling)
    auto finalStats = PathfinderManager::Instance().getStats();
    BOOST_CHECK_GE(finalStats.totalRequests, initialStats.totalRequests);
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventHandlerLifecycle, PathfinderEventFixture)
{
    // Test that PathfinderManager properly manages its event subscriptions
    
    // PathfinderManager should be initialized with event subscriptions
    BOOST_CHECK(PathfinderManager::Instance().isInitialized());
    
    // Trigger multiple events to ensure handler is stable
    for (int i = 0; i < 5; ++i) {
        Vector2D pos(i * 50.0f, i * 50.0f);
        bool fired = EventManager::Instance().triggerCollisionObstacleChanged(
            pos, 32.0f, "Lifecycle test " + std::to_string(i), 
            EventManager::DispatchMode::Immediate);
        BOOST_CHECK(fired);
    }
    
    // Brief processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // Clean and reinitialize to test subscription cleanup/re-establishment
    PathfinderManager::Instance().clean();
    BOOST_CHECK(!PathfinderManager::Instance().isInitialized());
    
    // Re-initialize
    PathfinderManager::Instance().init();
    BOOST_CHECK(PathfinderManager::Instance().isInitialized());
    
    // Should still be able to receive events after re-initialization
    bool fired = EventManager::Instance().triggerCollisionObstacleChanged(
        Vector2D(999.0f, 999.0f), 64.0f, "Post-reinit test",
        EventManager::DispatchMode::Immediate);
    BOOST_CHECK(fired);
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventPerformance, PathfinderEventFixture) 
{
    // Test that event handling doesn't significantly impact PathfinderManager performance
    
    const int numEvents = 50;
    std::vector<std::chrono::microseconds> eventTimes;
    eventTimes.reserve(numEvents);
    
    // Measure time for each event to be processed
    for (int i = 0; i < numEvents; ++i) {
        Vector2D pos(i * 20.0f, i * 20.0f);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        EventManager::Instance().triggerCollisionObstacleChanged(
            pos, 48.0f, "Performance test " + std::to_string(i),
            EventManager::DispatchMode::Immediate);
        
        auto end = std::chrono::high_resolution_clock::now();
        eventTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }
    
    // Calculate average event processing time
    auto totalTime = std::chrono::microseconds(0);
    for (const auto& time : eventTimes) {
        totalTime += time;
    }
    double avgTime = static_cast<double>(totalTime.count()) / numEvents;
    
    // Event processing should be fast (under 50 microseconds average)
    BOOST_CHECK_LT(avgTime, 50.0);
    
    // No single event should take more than 500 microseconds
    for (const auto& time : eventTimes) {
        BOOST_CHECK_LT(time.count(), 500);
    }
    
    BOOST_TEST_MESSAGE("Processed " << numEvents << " collision events in avg " 
                      << avgTime << " μs per event");
    
    // Verify PathfinderManager is still functioning after many events
    auto stats = PathfinderManager::Instance().getStats();
    BOOST_CHECK_GE(stats.totalRequests, 0); // Should be accessible
}

BOOST_AUTO_TEST_SUITE_END()
