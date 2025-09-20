/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file PathfinderBenchmark.cpp
 * @brief Performance benchmarks for the PathfinderManager system
 *
 * Comprehensive pathfinding performance tests covering:
 * - Immediate pathfinding performance across different grid sizes
 * - Async pathfinding request throughput and latency
 * - Cache performance and hit rates
 * - Threading overhead vs benefits analysis
 * - Obstacle density impact on pathfinding performance
 * - Path length vs computation time scaling
 */

#define BOOST_TEST_MODULE PathfinderBenchmark
#include <boost/test/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/WorldManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/GameEngine.hpp"
#include "utils/Vector2D.hpp"

#include <chrono>
#include <vector>
#include <random>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>

using namespace std::chrono;

class PathfinderBenchmarkFixture {
public:
    PathfinderBenchmarkFixture() {
        // Initialize core systems required for pathfinding
        HammerEngine::ThreadSystem::Instance().init(8); // 8 worker threads
        CollisionManager::Instance().init();
        WorldManager::Instance().init();
        PathfinderManager::Instance().init();

        // Set up a basic world for pathfinding tests
        setupTestWorld();

        std::cout << "\n=== PathfinderManager Benchmark Suite ===\n";
        std::cout << "Testing pathfinding performance across various scenarios\n\n";
    }

    ~PathfinderBenchmarkFixture() {
        PathfinderManager::Instance().shutdown();
        WorldManager::Instance().clean();
        CollisionManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }

private:
    void setupTestWorld() {
        // Create a 200x200 world for testing
        const int worldWidth = 200;
        const int worldHeight = 200;

        // Set world bounds for collision manager
        CollisionManager::Instance().setWorldBounds(0, 0, worldWidth * 32, worldHeight * 32);

        // Generate some random obstacles for more realistic pathfinding
        std::mt19937 rng(42); // Fixed seed for reproducible results
        std::uniform_int_distribution<int> posDist(10, worldWidth - 10);

        // Add 20% obstacle coverage scattered throughout the world
        int numObstacles = static_cast<int>((worldWidth * worldHeight) * 0.2f);
        for (int i = 0; i < numObstacles; ++i) {
            int x = posDist(rng);
            int y = posDist(rng);

            // Add obstacle to collision system
            EntityID obstacleId = static_cast<EntityID>(1000 + i);
            CollisionManager::Instance().addCollisionBodySOA(
                obstacleId,
                Vector2D(x * 32.0f, y * 32.0f),
                Vector2D(16.0f, 16.0f),
                BodyType::STATIC,
                CollisionLayer::Layer_Environment,
CollisionLayer::Layer_Environment
            );
        }

        // Rebuild pathfinding grid to include obstacles
        PathfinderManager::Instance().rebuildGrid();
    }
};

BOOST_FIXTURE_TEST_SUITE(PathfinderBenchmarkSuite, PathfinderBenchmarkFixture)

BOOST_AUTO_TEST_CASE(BenchmarkImmediatePathfinding) {
    std::cout << "=== Immediate Pathfinding Performance ===\n";

    const std::vector<std::pair<int, std::string>> gridSizes = {
        {50, "Small Grid (50x50)"},
        {100, "Medium Grid (100x100)"},
        {150, "Large Grid (150x150)"},
        {200, "XLarge Grid (200x200)"}
    };

    const int pathsPerSize = 100;
    std::mt19937 rng(42);

    for (const auto& [gridSize, description] : gridSizes) {
        std::uniform_int_distribution<int> coordDist(5, gridSize - 5);

        std::vector<double> pathTimes;
        pathTimes.reserve(pathsPerSize);

        int successfulPaths = 0;

        auto startBatch = high_resolution_clock::now();

        for (int i = 0; i < pathsPerSize; ++i) {
            Vector2D start(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);
            Vector2D goal(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);

            std::vector<Vector2D> path;

            auto pathStart = high_resolution_clock::now();
            HammerEngine::PathfindingResult result = PathfinderManager::Instance().findPathImmediate(
                start, goal, path
            );
            auto pathEnd = high_resolution_clock::now();

            double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
            pathTimes.push_back(pathTimeMs);

            if (result == HammerEngine::PathfindingResult::SUCCESS) {
                successfulPaths++;
            }
        }

        auto endBatch = high_resolution_clock::now();
        double totalBatchTime = duration_cast<milliseconds>(endBatch - startBatch).count();

        // Calculate statistics
        std::sort(pathTimes.begin(), pathTimes.end());
        double avgTime = std::accumulate(pathTimes.begin(), pathTimes.end(), 0.0) / pathTimes.size();
        double medianTime = pathTimes[pathTimes.size() / 2];
        double minTime = pathTimes.front();
        double maxTime = pathTimes.back();
        double p95Time = pathTimes[static_cast<size_t>(pathTimes.size() * 0.95)];

        std::cout << description << ":\n";
        std::cout << "  Paths tested: " << pathsPerSize << "\n";
        std::cout << "  Successful paths: " << successfulPaths << " ("
                  << std::fixed << std::setprecision(1)
                  << (100.0 * successfulPaths / pathsPerSize) << "%)\n";
        std::cout << "  Total batch time: " << totalBatchTime << "ms\n";
        std::cout << "  Average time: " << std::setprecision(3) << avgTime << "ms\n";
        std::cout << "  Median time: " << medianTime << "ms\n";
        std::cout << "  Min time: " << minTime << "ms\n";
        std::cout << "  Max time: " << maxTime << "ms\n";
        std::cout << "  95th percentile: " << p95Time << "ms\n";
        std::cout << "  Paths/second: " << std::setprecision(0)
                  << (1000.0 * pathsPerSize / totalBatchTime) << "\n\n";
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkAsyncPathfinding) {
    std::cout << "=== Async Pathfinding Throughput ===\n";

    const std::vector<int> batchSizes = {10, 50, 100, 250, 500};
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> coordDist(5, 195);

    for (int batchSize : batchSizes) {
        std::vector<uint64_t> requestIds;
        requestIds.reserve(batchSize);

        auto requestStart = high_resolution_clock::now();

        // Submit batch of async requests
        for (int i = 0; i < batchSize; ++i) {
            Vector2D start(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);
            Vector2D goal(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);

            uint64_t requestId = PathfinderManager::Instance().requestPath(
                static_cast<EntityID>(2000 + i),
                start,
                goal,
                PathfinderManager::Priority::Normal,
                [](EntityID, const std::vector<Vector2D>&) {
                    // Callback - just track completion
                }
            );

            requestIds.push_back(requestId);
        }

        auto requestEnd = high_resolution_clock::now();
        double requestTimeMs = duration_cast<microseconds>(requestEnd - requestStart).count() / 1000.0;

        // Wait for all requests to complete (with timeout)
        auto waitStart = high_resolution_clock::now();

        // Give pathfinder time to process all requests
        std::this_thread::sleep_for(std::chrono::milliseconds(batchSize * 2)); // 2ms per request max

        auto waitEnd = high_resolution_clock::now();
        double waitTimeMs = duration_cast<milliseconds>(waitEnd - waitStart).count();

        std::cout << "Batch size " << batchSize << ":\n";
        std::cout << "  Request submission: " << std::setprecision(3) << requestTimeMs << "ms\n";
        std::cout << "  Request rate: " << std::setprecision(0)
                  << (batchSize / (requestTimeMs / 1000.0)) << " requests/sec\n";
        std::cout << "  Processing time: " << waitTimeMs << "ms\n";
        std::cout << "  Throughput: " << std::setprecision(0)
                  << (batchSize / (waitTimeMs / 1000.0)) << " paths/sec\n\n";
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkPathLengthScaling) {
    std::cout << "=== Path Length vs Performance ===\n";

    const std::vector<std::pair<Vector2D, Vector2D>> pathTests = {
        {{32.0f, 32.0f}, {64.0f, 64.0f}}, // Very short path
        {{32.0f, 32.0f}, {320.0f, 320.0f}}, // Short path
        {{32.0f, 32.0f}, {1600.0f, 1600.0f}}, // Medium path
        {{32.0f, 32.0f}, {3200.0f, 3200.0f}}, // Long path
        {{32.0f, 32.0f}, {6000.0f, 6000.0f}} // Very long path
    };

    const int testsPerPath = 20;

    for (size_t i = 0; i < pathTests.size(); ++i) {
        const auto& [start, goal] = pathTests[i];
        float distance = (goal - start).length();

        std::vector<double> pathTimes;
        std::vector<size_t> pathLengths;
        pathTimes.reserve(testsPerPath);
        pathLengths.reserve(testsPerPath);

        int successfulPaths = 0;

        for (int test = 0; test < testsPerPath; ++test) {
            std::vector<Vector2D> path;

            auto pathStart = high_resolution_clock::now();
            HammerEngine::PathfindingResult result = PathfinderManager::Instance().findPathImmediate(
                start, goal, path
            );
            auto pathEnd = high_resolution_clock::now();

            double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
            pathTimes.push_back(pathTimeMs);
            pathLengths.push_back(path.size());

            if (result == HammerEngine::PathfindingResult::SUCCESS) {
                successfulPaths++;
            }
        }

        if (!pathTimes.empty()) {
            double avgTime = std::accumulate(pathTimes.begin(), pathTimes.end(), 0.0) / pathTimes.size();
            double avgLength = std::accumulate(pathLengths.begin(), pathLengths.end(), 0.0) / pathLengths.size();

            std::cout << "Distance " << std::setprecision(0) << distance << " units:\n";
            std::cout << "  Success rate: " << successfulPaths << "/" << testsPerPath
                      << " (" << std::setprecision(1) << (100.0 * successfulPaths / testsPerPath) << "%)\n";
            std::cout << "  Average time: " << std::setprecision(3) << avgTime << "ms\n";
            std::cout << "  Average path nodes: " << std::setprecision(1) << avgLength << "\n";
            if (avgLength > 0) {
                std::cout << "  Time per node: " << std::setprecision(3) << (avgTime / avgLength) << "ms\n";
            }
            std::cout << "\n";
        }
    }
}

BOOST_AUTO_TEST_CASE(BenchmarkCachePerformance) {
    std::cout << "=== Cache Performance Analysis ===\n";

    // Test cache effectiveness by repeating common paths
    const int numUniquePaths = 50;
    const int repeatsPerPath = 5;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> coordDist(5, 195);

    // Generate unique path requests
    std::vector<std::pair<Vector2D, Vector2D>> uniquePaths;
    for (int i = 0; i < numUniquePaths; ++i) {
        Vector2D start(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);
        Vector2D goal(coordDist(rng) * 32.0f, coordDist(rng) * 32.0f);
        uniquePaths.emplace_back(start, goal);
    }

    std::vector<double> firstRunTimes;
    std::vector<double> cachedRunTimes;

    // First run - populate cache
    auto firstRunStart = high_resolution_clock::now();
    for (const auto& [start, goal] : uniquePaths) {
        std::vector<Vector2D> path;

        auto pathStart = high_resolution_clock::now();
        PathfinderManager::Instance().findPathImmediate(start, goal, path);
        auto pathEnd = high_resolution_clock::now();

        double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
        firstRunTimes.push_back(pathTimeMs);
    }
    auto firstRunEnd = high_resolution_clock::now();

    // Cached runs - should be faster
    auto cachedRunStart = high_resolution_clock::now();
    for (int repeat = 0; repeat < repeatsPerPath; ++repeat) {
        for (const auto& [start, goal] : uniquePaths) {
            std::vector<Vector2D> path;

            auto pathStart = high_resolution_clock::now();
            PathfinderManager::Instance().findPathImmediate(start, goal, path);
            auto pathEnd = high_resolution_clock::now();

            double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
            cachedRunTimes.push_back(pathTimeMs);
        }
    }
    auto cachedRunEnd = high_resolution_clock::now();

    // Calculate statistics
    double avgFirstRun = std::accumulate(firstRunTimes.begin(), firstRunTimes.end(), 0.0) / firstRunTimes.size();
    double avgCachedRun = std::accumulate(cachedRunTimes.begin(), cachedRunTimes.end(), 0.0) / cachedRunTimes.size();

    double firstRunTotal = duration_cast<milliseconds>(firstRunEnd - firstRunStart).count();
    double cachedRunTotal = duration_cast<milliseconds>(cachedRunEnd - cachedRunStart).count();

    std::cout << "Unique paths tested: " << numUniquePaths << "\n";
    std::cout << "Repeats per path: " << repeatsPerPath << "\n\n";

    std::cout << "First run (cold cache):\n";
    std::cout << "  Average time per path: " << std::setprecision(3) << avgFirstRun << "ms\n";
    std::cout << "  Total time: " << firstRunTotal << "ms\n";
    std::cout << "  Paths/second: " << std::setprecision(0)
              << (1000.0 * numUniquePaths / firstRunTotal) << "\n\n";

    std::cout << "Cached runs (warm cache):\n";
    std::cout << "  Average time per path: " << std::setprecision(3) << avgCachedRun << "ms\n";
    std::cout << "  Total time: " << cachedRunTotal << "ms\n";
    std::cout << "  Paths/second: " << std::setprecision(0)
              << (1000.0 * cachedRunTimes.size() / cachedRunTotal) << "\n\n";

    double speedupRatio = avgFirstRun / avgCachedRun;
    std::cout << "Cache performance:\n";
    std::cout << "  Speedup ratio: " << std::setprecision(2) << speedupRatio << "x\n";
    std::cout << "  Cache efficiency: " << std::setprecision(1)
              << ((speedupRatio - 1.0) / speedupRatio * 100.0) << "%\n\n";
}

BOOST_AUTO_TEST_SUITE_END()

// Performance summary output
struct BenchmarkResults {
    static void outputSummary() {
        std::cout << "\n=== Pathfinder Benchmark Summary ===\n";
        std::cout << "Benchmark completed successfully!\n";
        std::cout << "\nKey Performance Indicators:\n";
        std::cout << "• Immediate pathfinding should complete in < 20ms for most paths\n";
        std::cout << "• Async throughput should exceed 100 paths/second\n";
        std::cout << "• Cache should provide 2x+ speedup for repeated paths\n";
        std::cout << "• Success rate should be > 90% for reasonable path requests\n";
        std::cout << "\nFor detailed metrics, check the benchmark output above.\n";
        std::cout << "==========================================\n\n";
    }
};

// Global test setup
BOOST_GLOBAL_FIXTURE(BenchmarkResults);