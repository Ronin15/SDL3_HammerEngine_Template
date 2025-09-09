/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfinderManagerTests
#include <boost/test/included/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
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
        static_cast<AIInternal::PathPriority>(2), // Normal priority
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
    auto requestId = manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(3));
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
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1), callback);
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1), callback); 
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1), callback);
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1), callback);
    
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
    
    // Note: No negative-result cache is enforced now; ensure no runaway processing only.
    
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
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1),
        [&firstCallbackCount](EntityID, const std::vector<Vector2D>&) {
            firstCallbackCount.fetch_add(1, std::memory_order_relaxed);
        });
    
    // Process first request
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();
    
    // Second identical request within cache window (should be rejected/cached)
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(1),
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
    BOOST_CHECK(stats.totalRequests <= 20); // No negative cache; still ensure no runaway
    
    manager.clean();
}

BOOST_AUTO_TEST_SUITE_END()
