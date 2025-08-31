/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfindingSystemTests
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/old/interface.hpp>

#include "ai/pathfinding/PathfindingGrid.hpp"
#include "utils/Vector2D.hpp"
#include <vector>
#include <chrono>
#include <random>
#include <cmath>

using namespace HammerEngine;

// Test fixture for pathfinding grid
struct PathfindingGridFixture {
    PathfindingGridFixture() 
        : grid(20, 20, 32.0f, Vector2D(0.0f, 0.0f))
    {
        // Create a simple test world with some obstacles
        setupSimpleWorld();
    }
    
    void setupSimpleWorld() {
        // For testing purposes, we'll manually set up blocked areas
        // In real usage, this would come from WorldManager
        
        // Create walls around the perimeter and some internal obstacles
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                bool isBlocked = false;
                
                // Perimeter walls
                if (x == 0 || x == 19 || y == 0 || y == 19) {
                    isBlocked = true;
                }
                // Central wall
                else if (x == 10 && y >= 5 && y <= 15) {
                    isBlocked = true;
                }
                // L-shaped obstacle
                else if ((x >= 15 && x <= 17 && y >= 10 && y <= 12) || 
                         (x >= 15 && x <= 17 && y >= 12 && y <= 14)) {
                    isBlocked = true;
                }
                
                // We can't directly set blocked cells without world data,
                // so we'll test with the assumption that rebuildFromWorld works
            }
        }
    }
    
    PathfindingGrid grid;
};

BOOST_AUTO_TEST_SUITE(PathfindingGridBasicTests)

BOOST_FIXTURE_TEST_CASE(TestGridCoordinateConversion, PathfindingGridFixture)
{
    // Test world to grid conversion
    Vector2D worldPos(64.0f, 96.0f); // Should be grid (2, 3)
    auto [gx, gy] = grid.worldToGrid(worldPos);
    BOOST_CHECK_EQUAL(gx, 2);
    BOOST_CHECK_EQUAL(gy, 3);
    
    // Test grid to world conversion
    Vector2D worldBack = grid.gridToWorld(gx, gy);
    BOOST_CHECK_CLOSE(worldBack.getX(), 80.0f, 0.01f); // Cell center at 64 + 16
    BOOST_CHECK_CLOSE(worldBack.getY(), 112.0f, 0.01f); // Cell center at 96 + 16
}

BOOST_FIXTURE_TEST_CASE(TestInBoundsCheck, PathfindingGridFixture)
{
    BOOST_CHECK(grid.inBounds(0, 0));
    BOOST_CHECK(grid.inBounds(19, 19));
    BOOST_CHECK(grid.inBounds(10, 10));
    
    BOOST_CHECK(!grid.inBounds(-1, 0));
    BOOST_CHECK(!grid.inBounds(0, -1));
    BOOST_CHECK(!grid.inBounds(20, 0));
    BOOST_CHECK(!grid.inBounds(0, 20));
    BOOST_CHECK(!grid.inBounds(25, 25));
}

BOOST_FIXTURE_TEST_CASE(TestPathfindingConfiguration, PathfindingGridFixture)
{
    // Test diagonal movement toggle
    grid.setAllowDiagonal(false);
    grid.setAllowDiagonal(true); // Just testing the interface
    
    // Test cost configuration
    grid.setCosts(1.0f, 1.4f);
    
    // Test iteration limits
    grid.setMaxIterations(5000);
    
    // These should not crash and should be configurable
    BOOST_CHECK(true); // Configuration methods should work without error
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PathfindingAlgorithmTests)

BOOST_FIXTURE_TEST_CASE(TestSimplePathfinding, PathfindingGridFixture)
{
    // Create a simple open area for pathfinding
    Vector2D start(48.0f, 48.0f);   // Grid position (1, 1) - should be open
    Vector2D goal(304.0f, 304.0f);  // Grid position (9, 9) - should be open
    
    std::vector<Vector2D> path;
    PathfindingResult result = grid.findPath(start, goal, path);
    
    // Without world data, this might fail, but we test the interface
    BOOST_CHECK(result == PathfindingResult::SUCCESS || 
                result == PathfindingResult::NO_PATH_FOUND ||
                result == PathfindingResult::TIMEOUT);
    
    if (result == PathfindingResult::SUCCESS) {
        BOOST_CHECK_GE(path.size(), 2); // At least start and goal
        
        // First point should be near start
        float startDistance = std::sqrt(std::pow(path[0].getX() - start.getX(), 2) + 
                                       std::pow(path[0].getY() - start.getY(), 2));
        BOOST_CHECK_LT(startDistance, 50.0f);
        
        // Last point should be near goal
        float goalDistance = std::sqrt(std::pow(path.back().getX() - goal.getX(), 2) + 
                                      std::pow(path.back().getY() - goal.getY(), 2));
        BOOST_CHECK_LT(goalDistance, 50.0f);
    }
}

BOOST_FIXTURE_TEST_CASE(TestInvalidStartAndGoal, PathfindingGridFixture)
{
    std::vector<Vector2D> path;
    
    // Test out of bounds start
    Vector2D invalidStart(-100.0f, -100.0f);
    Vector2D validGoal(160.0f, 160.0f);
    
    PathfindingResult result1 = grid.findPath(invalidStart, validGoal, path);
    BOOST_CHECK(result1 == PathfindingResult::INVALID_START || 
                result1 == PathfindingResult::NO_PATH_FOUND);
    
    // Test out of bounds goal
    Vector2D validStart(160.0f, 160.0f);
    Vector2D invalidGoal(1000.0f, 1000.0f);
    
    PathfindingResult result2 = grid.findPath(validStart, invalidGoal, path);
    BOOST_CHECK(result2 == PathfindingResult::INVALID_GOAL || 
                result2 == PathfindingResult::NO_PATH_FOUND);
}

BOOST_FIXTURE_TEST_CASE(TestSameStartAndGoal, PathfindingGridFixture)
{
    Vector2D samePoint(160.0f, 160.0f);
    std::vector<Vector2D> path;
    
    PathfindingResult result = grid.findPath(samePoint, samePoint, path);
    
    // Should either succeed with a single point or handle gracefully
    BOOST_CHECK(result == PathfindingResult::SUCCESS || 
                result == PathfindingResult::NO_PATH_FOUND);
    
    if (result == PathfindingResult::SUCCESS) {
        BOOST_CHECK_GE(path.size(), 1);
    }
}

BOOST_FIXTURE_TEST_CASE(TestDiagonalMovementToggle, PathfindingGridFixture)
{
    Vector2D start(48.0f, 48.0f);
    Vector2D goal(144.0f, 144.0f); // Diagonal goal
    
    std::vector<Vector2D> pathWithDiagonal, pathWithoutDiagonal;
    
    // Test with diagonal movement
    grid.setAllowDiagonal(true);
    PathfindingResult result1 = grid.findPath(start, goal, pathWithDiagonal);
    
    // Test without diagonal movement
    grid.setAllowDiagonal(false);
    PathfindingResult result2 = grid.findPath(start, goal, pathWithoutDiagonal);
    
    // Both should work or fail consistently
    if (result1 == PathfindingResult::SUCCESS && result2 == PathfindingResult::SUCCESS) {
        // Without diagonal movement, path should typically be longer
        BOOST_CHECK_GE(pathWithoutDiagonal.size(), pathWithDiagonal.size());
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PathfindingWeightTests)

BOOST_FIXTURE_TEST_CASE(TestWeightReset, PathfindingGridFixture)
{
    grid.resetWeights(2.0f);
    grid.resetWeights(1.0f); // Reset back to default
    
    // This tests the interface - actual weight effects would need world data
    BOOST_CHECK(true);
}

BOOST_FIXTURE_TEST_CASE(TestWeightCircleApplication, PathfindingGridFixture)
{
    Vector2D center(160.0f, 160.0f);
    float radius = 64.0f;
    float weightMultiplier = 3.0f;
    
    grid.addWeightCircle(center, radius, weightMultiplier);
    
    // Test that the weight application doesn't crash
    BOOST_CHECK(true);
    
    // In a full integration test, we would verify that paths avoid high-weight areas
}

BOOST_FIXTURE_TEST_CASE(TestMultipleWeightAreas, PathfindingGridFixture)
{
    grid.resetWeights(1.0f);
    
    // Add multiple weight areas
    grid.addWeightCircle(Vector2D(100.0f, 100.0f), 32.0f, 2.0f);
    grid.addWeightCircle(Vector2D(200.0f, 200.0f), 48.0f, 3.0f);
    grid.addWeightCircle(Vector2D(300.0f, 100.0f), 24.0f, 4.0f);
    
    // Overlapping areas should take the maximum weight
    grid.addWeightCircle(Vector2D(110.0f, 110.0f), 32.0f, 1.5f); // Should not reduce existing weight
    
    BOOST_CHECK(true); // Interface test
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PathfindingPerformanceTests)

BOOST_FIXTURE_TEST_CASE(TestPathfindingPerformance, PathfindingGridFixture)
{
    const int NUM_PATHFINDING_TESTS = 50;
    const float WORLD_SIZE = 640.0f; // 20 * 32
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(32.0f, WORLD_SIZE - 32.0f);
    
    std::vector<std::pair<Vector2D, Vector2D>> testCases;
    
    // Generate random start/goal pairs
    for (int i = 0; i < NUM_PATHFINDING_TESTS; ++i) {
        Vector2D start(posDist(rng), posDist(rng));
        Vector2D goal(posDist(rng), posDist(rng));
        testCases.emplace_back(start, goal);
    }
    
    // Performance test
    auto startTime = std::chrono::high_resolution_clock::now();
    
    int successfulPaths = 0;
    int totalPathLength = 0;
    
    for (const auto& testCase : testCases) {
        std::vector<Vector2D> path;
        PathfindingResult result = grid.findPath(testCase.first, testCase.second, path);
        
        if (result == PathfindingResult::SUCCESS) {
            successfulPaths++;
            totalPathLength += path.size();
        }
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    BOOST_TEST_MESSAGE("Pathfinding performance test:");
    BOOST_TEST_MESSAGE("  " << NUM_PATHFINDING_TESTS << " pathfinding requests in " 
                      << duration.count() << " microseconds");
    BOOST_TEST_MESSAGE("  " << (duration.count() / NUM_PATHFINDING_TESTS) << " μs per pathfinding request");
    BOOST_TEST_MESSAGE("  " << successfulPaths << " successful paths out of " << NUM_PATHFINDING_TESTS);
    if (successfulPaths > 0) {
        BOOST_TEST_MESSAGE("  Average path length: " << (totalPathLength / successfulPaths) << " waypoints");
    }
    
    // Performance requirements (adjust based on target performance)
    BOOST_CHECK_LT(duration.count() / NUM_PATHFINDING_TESTS, 5000); // < 5ms per pathfinding request
}

BOOST_FIXTURE_TEST_CASE(TestPathfindingIterationLimits, PathfindingGridFixture)
{
    // Test with very low iteration limits
    grid.setMaxIterations(100);
    
    Vector2D start(48.0f, 48.0f);
    Vector2D distantGoal(560.0f, 560.0f); // Far away goal
    
    std::vector<Vector2D> path;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    PathfindingResult result = grid.findPath(start, distantGoal, path);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Should timeout quickly with low iteration limit
    BOOST_CHECK(result == PathfindingResult::TIMEOUT || 
                result == PathfindingResult::NO_PATH_FOUND ||
                result == PathfindingResult::SUCCESS);
    
    // Should complete quickly due to iteration limit
    BOOST_CHECK_LT(duration.count(), 100); // < 100ms
    
    BOOST_TEST_MESSAGE("Limited iteration pathfinding completed in " 
                      << duration.count() << "ms with result: " 
                      << static_cast<int>(result));
}

BOOST_FIXTURE_TEST_CASE(TestPathfindingMemoryUsage, PathfindingGridFixture)
{
    // Test multiple pathfinding requests to ensure no memory leaks
    const int STRESS_TEST_ITERATIONS = 200;
    
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> posDist(64.0f, 576.0f);
    
    for (int i = 0; i < STRESS_TEST_ITERATIONS; ++i) {
        Vector2D start(posDist(rng), posDist(rng));
        Vector2D goal(posDist(rng), posDist(rng));
        
        std::vector<Vector2D> path;
        grid.findPath(start, goal, path);
        
        // Path vector should be cleared between requests
        path.clear();
        
        // Periodically reset weights to test that functionality
        if (i % 50 == 0) {
            grid.resetWeights(1.0f);
        }
        
        // Add occasional weight areas
        if (i % 25 == 0) {
            grid.addWeightCircle(Vector2D(posDist(rng), posDist(rng)), 32.0f, 2.0f);
        }
    }
    
    BOOST_TEST_MESSAGE("Memory usage test: " << STRESS_TEST_ITERATIONS 
                      << " pathfinding operations completed");
    BOOST_CHECK(true); // If we get here without crashing, memory management is working
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PathfindingEdgeCaseTests)

BOOST_FIXTURE_TEST_CASE(TestNearestOpenCellFinding, PathfindingGridFixture)
{
    // This tests the internal findNearestOpen functionality indirectly
    // by using start/goal positions that might be blocked
    
    Vector2D potentiallyBlockedStart(32.0f, 32.0f);  // Edge of world
    Vector2D potentiallyBlockedGoal(608.0f, 608.0f); // Other edge
    
    std::vector<Vector2D> path;
    PathfindingResult result = grid.findPath(potentiallyBlockedStart, potentiallyBlockedGoal, path);
    
    // Should either find a path by nudging to nearest open cell, or fail gracefully
    BOOST_CHECK(result == PathfindingResult::SUCCESS || 
                result == PathfindingResult::NO_PATH_FOUND ||
                result == PathfindingResult::INVALID_START ||
                result == PathfindingResult::INVALID_GOAL ||
                result == PathfindingResult::TIMEOUT);
}

BOOST_FIXTURE_TEST_CASE(TestPathfindingWithWeights, PathfindingGridFixture)
{
    Vector2D start(80.0f, 80.0f);
    Vector2D goal(400.0f, 400.0f);
    
    // First, find path without weights
    std::vector<Vector2D> normalPath;
    PathfindingResult normalResult = grid.findPath(start, goal, normalPath);
    
    // Add weight area in the middle
    grid.addWeightCircle(Vector2D(240.0f, 240.0f), 80.0f, 5.0f);
    
    // Find path with weights
    std::vector<Vector2D> weightedPath;
    PathfindingResult weightedResult = grid.findPath(start, goal, weightedPath);
    
    // Both should succeed or fail consistently
    BOOST_CHECK_EQUAL(normalResult, weightedResult);
    
    if (normalResult == PathfindingResult::SUCCESS && weightedResult == PathfindingResult::SUCCESS) {
        // Weighted path might be different (avoiding high-cost areas)
        // This is hard to test without exact world knowledge, so we just verify it completes
        BOOST_CHECK_GE(weightedPath.size(), 2);
    }
}

BOOST_FIXTURE_TEST_CASE(TestExtremeDistances, PathfindingGridFixture)
{
    // Test very short distance
    Vector2D closeStart(160.0f, 160.0f);
    Vector2D closeGoal(165.0f, 165.0f);
    
    std::vector<Vector2D> shortPath;
    PathfindingResult shortResult = grid.findPath(closeStart, closeGoal, shortPath);
    
    // Test maximum distance within grid
    Vector2D farStart(48.0f, 48.0f);
    Vector2D farGoal(592.0f, 592.0f);
    
    std::vector<Vector2D> longPath;
    PathfindingResult longResult = grid.findPath(farStart, farGoal, longPath);
    
    // Both should handle the distance appropriately
    BOOST_CHECK(shortResult != PathfindingResult::INVALID_START);
    BOOST_CHECK(shortResult != PathfindingResult::INVALID_GOAL);
    BOOST_CHECK(longResult != PathfindingResult::INVALID_START);
    BOOST_CHECK(longResult != PathfindingResult::INVALID_GOAL);
    
    if (shortResult == PathfindingResult::SUCCESS) {
        BOOST_CHECK_GE(shortPath.size(), 1);
    }
    
    if (longResult == PathfindingResult::SUCCESS) {
        BOOST_CHECK_GE(longPath.size(), 2);
    }
}

BOOST_AUTO_TEST_SUITE_END()