/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file PathfinderBenchmark.cpp
 * @brief Performance benchmarks for the PathfinderManager system
 *
 * Comprehensive pathfinding performance tests covering:
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
#include "managers/WorldResourceManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"
#include "world/WorldGenerator.hpp"
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
        // Initialize core systems required for pathfinding (order matters!)
        HammerEngine::ThreadSystem::Instance().init(8); // 8 worker threads

        // Initialize resource managers first
        ResourceTemplateManager::Instance().init();
        WorldResourceManager::Instance().init();

        // Initialize EventManager for event-driven architecture
        EventManager::Instance().init();

        // Initialize world and collision managers
        WorldManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();

        // Set up a basic world for pathfinding tests
        setupTestWorld();

        std::cout << "\n=== PathfinderManager Benchmark Suite ===\n";
        std::cout << "Testing pathfinding performance across various scenarios\n\n";
    }

    ~PathfinderBenchmarkFixture() {
        // For benchmarks, we keep the world and managers loaded across all test cases
        // to measure steady-state performance rather than cold-start performance.
        // Clean only happens at the very end via global fixture.
    }

private:
    void setupTestWorld() {
        // Create a 200x200 world for testing using WorldManager
        HammerEngine::WorldGenerationConfig config;
        config.width = 200;
        config.height = 200;
        config.seed = 42; // Fixed seed for reproducible results
        config.elevationFrequency = 0.1f;
        config.humidityFrequency = 0.1f;
        config.waterLevel = 0.3f;
        config.mountainLevel = 0.7f;

        // Load the world through WorldManager - this provides the pathfinding grid context
        bool worldLoaded = WorldManager::Instance().loadNewWorld(config);
        if (!worldLoaded) {
            throw std::runtime_error("Failed to load test world for pathfinding benchmark");
        }

        // EVENT-DRIVEN: Process deferred events (triggers WorldLoaded task on ThreadSystem)
        EventManager::Instance().update();

        // Give ThreadSystem time to execute the WorldLoaded task and enqueue the deferred event
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Process the deferred WorldLoadedEvent (delivers to PathfinderManager)
        EventManager::Instance().update();

        // Wait for async grid rebuild to complete (~100-200ms for test world)
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        std::cout << "Pathfinding grid ready for benchmarks\n";

        // Set world bounds for collision manager (required for collision system)
        CollisionManager::Instance().setWorldBounds(0, 0, config.width * 32, config.height * 32);

        // Add some collision obstacles for more realistic pathfinding testing
        std::mt19937 rng(42); // Fixed seed for reproducible results
        std::uniform_int_distribution<int> posDist(10, config.width - 10);

        // Add 5% collision obstacles (separate from world terrain obstacles)
        int numObstacles = static_cast<int>((config.width * config.height) * 0.05f);
        for (int i = 0; i < numObstacles; ++i) {
            int x = posDist(rng);
            int y = posDist(rng);

            // Add obstacle to collision system
            EntityID obstacleId = static_cast<EntityID>(1000 + i);
            CollisionManager::Instance().addCollisionBodySOA(
                obstacleId,
                Vector2D(x * HammerEngine::TILE_SIZE, y * HammerEngine::TILE_SIZE),
                Vector2D(16.0f, 16.0f),
                BodyType::STATIC,
                CollisionLayer::Layer_Environment,
                CollisionLayer::Layer_Environment
            );
        }

        // The pathfinding grid should now be automatically available through WorldManager
        std::cout << "Test world loaded: " << config.width << "x" << config.height
                  << " with " << numObstacles << " collision obstacles\n";
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
            Vector2D start(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);
            Vector2D goal(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);

            std::vector<Vector2D> path;
            std::atomic<bool> pathReady{false};
            bool pathSuccess = false;

            auto pathStart = high_resolution_clock::now();
            PathfinderManager::Instance().requestPath(
                static_cast<EntityID>(i + 10000), start, goal,
                PathfinderManager::Priority::High,
                [&](EntityID, const std::vector<Vector2D>& resultPath) {
                    path = resultPath;
                    pathSuccess = !resultPath.empty();
                    pathReady.store(true, std::memory_order_release);
                }
            );

            // Wait for async pathfinding to complete
            // IMPORTANT: Call update() to process buffered requests with WorkerBudget integration
            while (!pathReady.load(std::memory_order_acquire)) {
                PathfinderManager::Instance().update(); // Process buffered requests
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            auto pathEnd = high_resolution_clock::now();

            double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
            pathTimes.push_back(pathTimeMs);

            if (pathSuccess) {
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
            Vector2D start(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);
            Vector2D goal(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);

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

        // Process requests with update() calls
        int maxIterations = batchSize * 2; // Max iterations to wait
        for (int i = 0; i < maxIterations; ++i) {
            PathfinderManager::Instance().update(); // Process buffered requests
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

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
        {{HammerEngine::TILE_SIZE, HammerEngine::TILE_SIZE}, {64.0f, 64.0f}}, // Very short path
        {{HammerEngine::TILE_SIZE, HammerEngine::TILE_SIZE}, {320.0f, 320.0f}}, // Short path
        {{HammerEngine::TILE_SIZE, HammerEngine::TILE_SIZE}, {1600.0f, 1600.0f}}, // Medium path
        {{HammerEngine::TILE_SIZE, HammerEngine::TILE_SIZE}, {3200.0f, 3200.0f}}, // Long path
        {{HammerEngine::TILE_SIZE, HammerEngine::TILE_SIZE}, {6000.0f, 6000.0f}} // Very long path
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
            std::atomic<bool> pathReady{false};
            bool pathSuccess = false;

            auto pathStart = high_resolution_clock::now();
            PathfinderManager::Instance().requestPath(
                static_cast<EntityID>(test + 20000), start, goal,
                PathfinderManager::Priority::High,
                [&](EntityID, const std::vector<Vector2D>& resultPath) {
                    path = resultPath;
                    pathSuccess = !resultPath.empty();
                    pathReady.store(true, std::memory_order_release);
                }
            );

            // Wait for async pathfinding to complete
            // IMPORTANT: Call update() to process buffered requests with WorkerBudget integration
            while (!pathReady.load(std::memory_order_acquire)) {
                PathfinderManager::Instance().update(); // Process buffered requests
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            auto pathEnd = high_resolution_clock::now();

            double pathTimeMs = duration_cast<microseconds>(pathEnd - pathStart).count() / 1000.0;
            pathTimes.push_back(pathTimeMs);
            pathLengths.push_back(path.size());

            if (pathSuccess) {
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
        Vector2D start(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);
        Vector2D goal(coordDist(rng) * HammerEngine::TILE_SIZE, coordDist(rng) * HammerEngine::TILE_SIZE);
        uniquePaths.emplace_back(start, goal);
    }

    std::vector<double> firstRunTimes;
    std::vector<double> cachedRunTimes;

    // First run - populate cache
    auto firstRunStart = high_resolution_clock::now();
    int entityIdCounter = 30000;
    for (const auto& [start, goal] : uniquePaths) {
        std::vector<Vector2D> path;
        std::atomic<bool> pathReady{false};

        auto pathStart = high_resolution_clock::now();
        PathfinderManager::Instance().requestPath(
            static_cast<EntityID>(entityIdCounter++), start, goal,
            PathfinderManager::Priority::High,
            [&](EntityID, const std::vector<Vector2D>& resultPath) {
                path = resultPath;
                pathReady.store(true, std::memory_order_release);
            }
        );

        while (!pathReady.load(std::memory_order_acquire)) {
            PathfinderManager::Instance().update(); // Process buffered requests
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
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
            std::atomic<bool> pathReady{false};

            auto pathStart = high_resolution_clock::now();
            PathfinderManager::Instance().requestPath(
                static_cast<EntityID>(entityIdCounter++), start, goal,
                PathfinderManager::Priority::High,
                [&](EntityID, const std::vector<Vector2D>& resultPath) {
                    path = resultPath;
                    pathReady.store(true, std::memory_order_release);
                }
            );

            while (!pathReady.load(std::memory_order_acquire)) {
                PathfinderManager::Instance().update(); // Process buffered requests
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
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

// Global fixture for benchmark suite - handles final cleanup
struct BenchmarkGlobalFixture {
    BenchmarkGlobalFixture() {
        // Global setup (if needed)
    }

    ~BenchmarkGlobalFixture() {
        // Clean up all managers at the end of all benchmark tests
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        WorldManager::Instance().clean();
        EventManager::Instance().clean();
        WorldResourceManager::Instance().clean();
        ResourceTemplateManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();

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
BOOST_GLOBAL_FIXTURE(BenchmarkGlobalFixture);