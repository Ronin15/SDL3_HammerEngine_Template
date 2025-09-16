/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE Collisions
#include <boost/test/unit_test.hpp>
#include <boost/test/tools/old/interface.hpp>

#include "collisions/SpatialHash.hpp"
#include "collisions/AABB.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "utils/Vector2D.hpp"
#include <vector>
#include <chrono>
#include <random>
#include <iostream>
#include <fstream>
#include <iomanip>

using namespace HammerEngine;

// Benchmark result structure for data collection
struct BenchmarkResult {
    std::string testName;
    int entityCount;
    int operationCount;
    double averageTimeUs;
    double totalTimeMs;
    double operationsPerSecond;
    std::string additionalInfo;
};

// Helper class for benchmark data collection and reporting
class BenchmarkReporter {
private:
    std::vector<BenchmarkResult> results;
    
public:
    void addResult(const BenchmarkResult& result) {
        results.push_back(result);
    }
    
    void printSummary() {
        BOOST_TEST_MESSAGE("\n=== COLLISION & PATHFINDING BENCHMARK RESULTS ===");
        BOOST_TEST_MESSAGE(std::setw(35) << "Test Name" 
                          << std::setw(12) << "Entities"
                          << std::setw(12) << "Operations"
                          << std::setw(12) << "Avg Time μs"
                          << std::setw(12) << "Total ms"
                          << std::setw(15) << "Ops/Second");
        BOOST_TEST_MESSAGE(std::string(95, '-'));
        
        for (const auto& result : results) {
            BOOST_TEST_MESSAGE(std::setw(35) << result.testName 
                              << std::setw(12) << result.entityCount
                              << std::setw(12) << result.operationCount
                              << std::setw(12) << std::fixed << std::setprecision(2) << result.averageTimeUs
                              << std::setw(12) << std::fixed << std::setprecision(2) << result.totalTimeMs
                              << std::setw(15) << std::fixed << std::setprecision(0) << result.operationsPerSecond);
            if (!result.additionalInfo.empty()) {
                BOOST_TEST_MESSAGE("    " << result.additionalInfo);
            }
        }
        BOOST_TEST_MESSAGE("");
    }
    
    void saveToCsv(const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            BOOST_TEST_MESSAGE("Warning: Could not save benchmark results to " << filename);
            return;
        }
        
        file << "TestName,EntityCount,OperationCount,AverageTimeUs,TotalTimeMs,OperationsPerSecond,AdditionalInfo\n";
        for (const auto& result : results) {
            file << result.testName << ","
                 << result.entityCount << ","
                 << result.operationCount << ","
                 << std::fixed << std::setprecision(2) << result.averageTimeUs << ","
                 << std::fixed << std::setprecision(2) << result.totalTimeMs << ","
                 << std::fixed << std::setprecision(0) << result.operationsPerSecond << ","
                 << result.additionalInfo << "\n";
        }
        
        file.close();
        BOOST_TEST_MESSAGE("Benchmark results saved to: " << filename);
    }
};

static BenchmarkReporter g_reporter;

BOOST_AUTO_TEST_SUITE(CollisionBenchmarks)

BOOST_AUTO_TEST_CASE(BenchmarkSpatialHashInsertion)
{
    const std::vector<int> entityCounts = {100, 500, 1000, 5000, 10000};
    const float WORLD_SIZE = 2000.0f;
    const float CELL_SIZE = 64.0f;
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(8.0f, 32.0f);
    
    for (int numEntities : entityCounts) {
        SpatialHash spatialHash(CELL_SIZE);
        
        // Generate test entities
        std::vector<std::pair<EntityID, AABB>> entities;
        entities.reserve(numEntities);
        
        for (int i = 0; i < numEntities; ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            float x = posDist(rng);
            float y = posDist(rng);
            float halfW = sizeDist(rng);
            float halfH = sizeDist(rng);
            entities.emplace_back(id, AABB(x, y, halfW, halfH));
        }
        
        // Benchmark insertion
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& entity : entities) {
            spatialHash.insert(entity.first, entity.second);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double totalMs = duration.count() / 1000.0;
        double avgUs = static_cast<double>(duration.count()) / numEntities;
        double opsPerSec = (numEntities * 1000000.0) / duration.count();
        
        BenchmarkResult result;
        result.testName = "SpatialHash_Insert";
        result.entityCount = numEntities;
        result.operationCount = numEntities;
        result.averageTimeUs = avgUs;
        result.totalTimeMs = totalMs;
        result.operationsPerSecond = opsPerSec;
        {
            std::ostringstream oss;
            oss << "Cell size: " << std::fixed << std::setprecision(0) << CELL_SIZE;
            result.additionalInfo = oss.str();
        }
        
        g_reporter.addResult(result);
        
        // Performance assertions
        BOOST_CHECK_LT(avgUs, 100.0); // < 100μs per insertion
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkSpatialHashQuery)
{
    const std::vector<int> entityCounts = {500, 1000, 2000, 5000};
    const int NUM_QUERIES = 1000;
    const float WORLD_SIZE = 2000.0f;
    const float CELL_SIZE = 64.0f;
    const float QUERY_SIZE = 100.0f;
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(8.0f, 32.0f);
    
    for (int numEntities : entityCounts) {
        SpatialHash spatialHash(CELL_SIZE);
        
        // Insert entities
        for (int i = 0; i < numEntities; ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            float x = posDist(rng);
            float y = posDist(rng);
            float halfW = sizeDist(rng);
            float halfH = sizeDist(rng);
            spatialHash.insert(id, AABB(x, y, halfW, halfH));
        }
        
        // Benchmark queries
        std::vector<EntityID> results;
        int totalFound = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int q = 0; q < NUM_QUERIES; ++q) {
            float queryX = posDist(rng);
            float queryY = posDist(rng);
            AABB queryArea(queryX, queryY, QUERY_SIZE, QUERY_SIZE);
            
            results.clear();
            spatialHash.query(queryArea, results);
            totalFound += results.size();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double totalMs = duration.count() / 1000.0;
        double avgUs = static_cast<double>(duration.count()) / NUM_QUERIES;
        double opsPerSec = (NUM_QUERIES * 1000000.0) / duration.count();
        
        BenchmarkResult result;
        result.testName = "SpatialHash_Query";
        result.entityCount = numEntities;
        result.operationCount = NUM_QUERIES;
        result.averageTimeUs = avgUs;
        result.totalTimeMs = totalMs;
        result.operationsPerSecond = opsPerSec;
        {
            std::ostringstream oss;
            oss << "Avg found: " << (totalFound / NUM_QUERIES)
                << ", Query size: " << std::fixed << std::setprecision(0) << QUERY_SIZE;
            result.additionalInfo = oss.str();
        }
        
        g_reporter.addResult(result);
        
        // Performance assertions
        BOOST_CHECK_LT(avgUs, 150.0); // < 150μs per query
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkSpatialHashUpdate)
{
    const std::vector<int> entityCounts = {500, 1000, 2000, 5000};
    const int NUM_UPDATES = 2000;
    const float WORLD_SIZE = 1000.0f;
    const float CELL_SIZE = 50.0f;
    const float MOVEMENT_RANGE = 100.0f;
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_SIZE);
    std::uniform_real_distribution<float> sizeDist(5.0f, 20.0f);
    std::uniform_real_distribution<float> moveDist(-MOVEMENT_RANGE, MOVEMENT_RANGE);
    
    for (int numEntities : entityCounts) {
        SpatialHash spatialHash(CELL_SIZE);
        
        // Insert entities and track their positions
        std::vector<std::pair<EntityID, AABB>> entities;
        entities.reserve(numEntities);
        
        for (int i = 0; i < numEntities; ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            float x = posDist(rng);
            float y = posDist(rng);
            float halfW = sizeDist(rng);
            float halfH = sizeDist(rng);
            AABB aabb(x, y, halfW, halfH);
            
            entities.emplace_back(id, aabb);
            spatialHash.insert(id, aabb);
        }
        
        // Benchmark updates
        std::uniform_int_distribution<size_t> entityDist(0, entities.size() - 1);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int u = 0; u < NUM_UPDATES; ++u) {
            // Pick random entity to update
            size_t entityIndex = entityDist(rng);
            EntityID id = entities[entityIndex].first;
            AABB& currentAABB = entities[entityIndex].second;
            
            // Move entity
            float newX = currentAABB.center.getX() + moveDist(rng);
            float newY = currentAABB.center.getY() + moveDist(rng);
            newX = std::max(0.0f, std::min(WORLD_SIZE, newX));
            newY = std::max(0.0f, std::min(WORLD_SIZE, newY));
            
            AABB newAABB(newX, newY, currentAABB.halfSize.getX(), currentAABB.halfSize.getY());
            spatialHash.update(id, newAABB);
            currentAABB = newAABB;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double totalMs = duration.count() / 1000.0;
        double avgUs = static_cast<double>(duration.count()) / NUM_UPDATES;
        double opsPerSec = (NUM_UPDATES * 1000000.0) / duration.count();
        
        BenchmarkResult result;
        result.testName = "SpatialHash_Update";
        result.entityCount = numEntities;
        result.operationCount = NUM_UPDATES;
        result.averageTimeUs = avgUs;
        result.totalTimeMs = totalMs;
        result.operationsPerSecond = opsPerSec;
        {
            std::ostringstream oss;
            oss << "Movement range: ±" << std::fixed << std::setprecision(0) << MOVEMENT_RANGE;
            result.additionalInfo = oss.str();
        }
        
        g_reporter.addResult(result);
        
        // Performance assertions
        BOOST_CHECK_LT(avgUs, 200.0); // < 200μs per update
    }
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(PathfindingBenchmarks)

BOOST_AUTO_TEST_CASE(BenchmarkPathfindingGrid)
{
    const std::vector<std::pair<int, int>> gridSizes = {
        {50, 50}, {100, 100}, {150, 150}, {200, 200}
    };
    const int NUM_PATHFIND_REQUESTS = 100;
    const float CELL_SIZE = 20.0f;
    
    std::mt19937 rng(42);
    
    for (const auto& [width, height] : gridSizes) {
        PathfindingGrid grid(width, height, CELL_SIZE, Vector2D(0.0f, 0.0f));
        
        // Generate pathfinding test cases
        float worldWidth = width * CELL_SIZE;
        float worldHeight = height * CELL_SIZE;
        std::uniform_real_distribution<float> xDist(CELL_SIZE, worldWidth - CELL_SIZE);
        std::uniform_real_distribution<float> yDist(CELL_SIZE, worldHeight - CELL_SIZE);
        
        std::vector<std::pair<Vector2D, Vector2D>> testCases;
        testCases.reserve(NUM_PATHFIND_REQUESTS);
        
        for (int i = 0; i < NUM_PATHFIND_REQUESTS; ++i) {
            Vector2D start(xDist(rng), yDist(rng));
            Vector2D goal(xDist(rng), yDist(rng));
            testCases.emplace_back(start, goal);
        }
        
        // Benchmark pathfinding
        int successfulPaths = 0;
        int totalPathNodes = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& testCase : testCases) {
            std::vector<Vector2D> path;
            PathfindingResult result = grid.findPath(testCase.first, testCase.second, path);
            
            if (result == PathfindingResult::SUCCESS) {
                successfulPaths++;
                totalPathNodes += path.size();
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double totalMs = duration.count() / 1000.0;
        double avgUs = static_cast<double>(duration.count()) / NUM_PATHFIND_REQUESTS;
        double opsPerSec = (NUM_PATHFIND_REQUESTS * 1000000.0) / duration.count();
        
        BenchmarkResult result;
        result.testName = "Pathfinding_Grid";
        result.entityCount = width * height;
        result.operationCount = NUM_PATHFIND_REQUESTS;
        result.averageTimeUs = avgUs;
        result.totalTimeMs = totalMs;
        result.operationsPerSecond = opsPerSec;
        result.additionalInfo = "Grid: " + std::to_string(width) + "x" + std::to_string(height) + 
                                ", Success: " + std::to_string(successfulPaths) + "/" + std::to_string(NUM_PATHFIND_REQUESTS);
        if (successfulPaths > 0) {
            result.additionalInfo += ", Avg nodes: " + std::to_string(totalPathNodes / successfulPaths);
        }
        
        g_reporter.addResult(result);
        
        // Performance assertions - pathfinding should be reasonable for grid sizes
        if (width <= 100 && height <= 100) {
            BOOST_CHECK_LT(avgUs, 10000.0); // < 10ms for small grids
        } else {
            BOOST_CHECK_LT(avgUs, 50000.0); // < 50ms for large grids
        }
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkPathfindingWithWeights)
{
    const int GRID_SIZE = 100;
    const float CELL_SIZE = 16.0f;
    const int NUM_REQUESTS = 50;
    const int NUM_WEIGHT_AREAS = 20;
    
    PathfindingGrid grid(GRID_SIZE, GRID_SIZE, CELL_SIZE, Vector2D(0.0f, 0.0f));
    
    std::mt19937 rng(123);
    float worldSize = GRID_SIZE * CELL_SIZE;
    std::uniform_real_distribution<float> posDist(CELL_SIZE, worldSize - CELL_SIZE);
    std::uniform_real_distribution<float> radiusDist(20.0f, 60.0f);
    std::uniform_real_distribution<float> weightDist(2.0f, 5.0f);
    
    // Add weight areas
    for (int i = 0; i < NUM_WEIGHT_AREAS; ++i) {
        Vector2D center(posDist(rng), posDist(rng));
        float radius = radiusDist(rng);
        float weight = weightDist(rng);
        grid.addWeightCircle(center, radius, weight);
    }
    
    // Generate pathfinding requests
    std::vector<std::pair<Vector2D, Vector2D>> testCases;
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        Vector2D start(posDist(rng), posDist(rng));
        Vector2D goal(posDist(rng), posDist(rng));
        testCases.emplace_back(start, goal);
    }
    
    // Benchmark weighted pathfinding
    int successfulPaths = 0;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (const auto& testCase : testCases) {
        std::vector<Vector2D> path;
        PathfindingResult result = grid.findPath(testCase.first, testCase.second, path);
        
        if (result == PathfindingResult::SUCCESS) {
            successfulPaths++;
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    double totalMs = duration.count() / 1000.0;
    double avgUs = static_cast<double>(duration.count()) / NUM_REQUESTS;
    double opsPerSec = (NUM_REQUESTS * 1000000.0) / duration.count();
    
    BenchmarkResult result;
    result.testName = "Pathfinding_Weighted";
    result.entityCount = GRID_SIZE * GRID_SIZE;
    result.operationCount = NUM_REQUESTS;
    result.averageTimeUs = avgUs;
    result.totalTimeMs = totalMs;
    result.operationsPerSecond = opsPerSec;
    result.additionalInfo = std::to_string(NUM_WEIGHT_AREAS) + " weight areas, Success: " + 
                            std::to_string(successfulPaths) + "/" + std::to_string(NUM_REQUESTS);
    
    g_reporter.addResult(result);
    
    // Performance assertions
    BOOST_CHECK_LT(avgUs, 20000.0); // < 20ms for weighted pathfinding
}

BOOST_AUTO_TEST_CASE(BenchmarkPathfindingIterationLimits)
{
    const std::vector<int> iterationLimits = {500, 1000, 2000, 5000, 10000};
    const int GRID_SIZE = 150;
    const float CELL_SIZE = 16.0f;
    const int NUM_REQUESTS = 30;
    
    std::mt19937 rng(456);
    float worldSize = GRID_SIZE * CELL_SIZE;
    std::uniform_real_distribution<float> posDist(CELL_SIZE, worldSize - CELL_SIZE);
    
    // Generate challenging pathfinding cases (far apart)
    std::vector<std::pair<Vector2D, Vector2D>> testCases;
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        // Create longer distance paths to test iteration limits
        Vector2D start(posDist(rng), posDist(rng));
        Vector2D goal(posDist(rng), posDist(rng));
        
        // Ensure some distance between start and goal
        float distance = std::sqrt(std::pow(goal.getX() - start.getX(), 2) + 
                                  std::pow(goal.getY() - start.getY(), 2));
        if (distance < worldSize * 0.3f) {
            // Adjust goal to be farther
            goal = Vector2D(worldSize - start.getX() * 0.8f, worldSize - start.getY() * 0.8f);
        }
        
        testCases.emplace_back(start, goal);
    }
    
    for (int iterLimit : iterationLimits) {
        PathfindingGrid grid(GRID_SIZE, GRID_SIZE, CELL_SIZE, Vector2D(0.0f, 0.0f));
        grid.setMaxIterations(iterLimit);
        
        int successfulPaths = 0;
        int timeoutPaths = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (const auto& testCase : testCases) {
            std::vector<Vector2D> path;
            PathfindingResult result = grid.findPath(testCase.first, testCase.second, path);
            
            if (result == PathfindingResult::SUCCESS) {
                successfulPaths++;
            } else if (result == PathfindingResult::TIMEOUT) {
                timeoutPaths++;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        double totalMs = duration.count() / 1000.0;
        double avgUs = static_cast<double>(duration.count()) / NUM_REQUESTS;
        double opsPerSec = (NUM_REQUESTS * 1000000.0) / duration.count();
        
        BenchmarkResult result;
        result.testName = "Pathfinding_Limited";
        result.entityCount = iterLimit;
        result.operationCount = NUM_REQUESTS;
        result.averageTimeUs = avgUs;
        result.totalTimeMs = totalMs;
        result.operationsPerSecond = opsPerSec;
        result.additionalInfo = "Iter limit: " + std::to_string(iterLimit) + 
                                ", Success: " + std::to_string(successfulPaths) + 
                                ", Timeout: " + std::to_string(timeoutPaths);
        
        g_reporter.addResult(result);
        
        // With lower iteration limits, should complete faster
        if (iterLimit <= 1000) {
            BOOST_CHECK_LT(avgUs, 5000.0); // < 5ms with low iteration limit
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

// Global test suite cleanup - print summary
BOOST_AUTO_TEST_CASE(BenchmarkSummary)
{
    g_reporter.printSummary();
    g_reporter.saveToCsv("test_results/collisions.csv");
}
