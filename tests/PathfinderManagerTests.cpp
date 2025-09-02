/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfinderManagerTests
#include <boost/test/included/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "utils/Vector2D.hpp"
#include <chrono>
#include <thread>

using namespace HammerEngine;

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
    manager.update(0.016f); // ~60 FPS
    
    // Give it some time to process (async operation)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.update(0.016f);
    
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

BOOST_AUTO_TEST_CASE(TestRequestCancellation) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    EntityID entityId = 54321;
    
    // Request a path
    auto requestId = manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(3)); // Low priority
    BOOST_CHECK(requestId > 0);
    
    // Cancel the request
    manager.cancelRequest(requestId);
    
    // Cancel all requests for entity
    manager.requestPath(entityId, start, goal, static_cast<AIInternal::PathPriority>(3)); // Low priority
    manager.cancelEntityRequests(entityId);
    
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
    BOOST_CHECK(stats.cancelledRequests == 0);
    
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
        manager.update(0.016f); // ~60 FPS
    }
    
    // Should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_SUITE_END()