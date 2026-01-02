/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>

#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "collisions/CollisionBody.hpp"
#include "utils/Vector2D.hpp"
#include "utils/Camera.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "world/WorldData.hpp"

class CollisionBenchmark {
public:
    struct BenchmarkResult {
        size_t bodyCount;
        double soaTimeMs;
        size_t collisionCount;
        size_t pairCount;
    };

    CollisionBenchmark() {
        // EntityDataManager must be initialized before CollisionManager
        // (collision bodies now store positions in EDM)
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        setupRandomGenerator();
    }

    ~CollisionBenchmark() {
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
    }

    void runBenchmarkSuite() {
        std::cout << "=== Collision System SOA Benchmark Suite (OPTIMIZED) ===" << std::endl;
        std::cout << "Testing optimized SOA collision detection with spatial hash performance" << std::endl;
        std::cout << "Optimization: O(N) body processing + hierarchical spatial hash + static caching + culling-aware queries" << std::endl;
        std::cout << std::endl;

        // Standard scaling benchmark
        runScalingBenchmark();

        // New: Static collision caching benchmark
        runStaticCachingBenchmark();

        // New: Realistic world scenario benchmark
        runWorldScenarioBenchmark();
    }

    void runScalingBenchmark() {
        std::cout << "=== Body Count Scaling Performance ===" << std::endl;
        std::vector<size_t> bodyCounts = {1000, 2000, 5000, 10000, 20000, 50000};
        std::vector<BenchmarkResult> results;

        for (size_t bodyCount : bodyCounts) {
            std::cout << "Benchmarking with " << bodyCount << " bodies..." << std::endl;

            BenchmarkResult result = benchmarkBodyCount(bodyCount);
            results.push_back(result);

            printResult(result);
            std::cout << std::endl;
        }

        printSummary(results, "Scaling");
    }

    void runStaticCachingBenchmark() {
        std::cout << "=== Static Collision Caching Effectiveness ===" << std::endl;
        std::cout << "Testing cache performance with moving vs stationary bodies" << std::endl;
        std::cout << std::endl;

        // Test scenario: Static bodies with significant moving population (stress test)
        constexpr size_t totalBodies = 15000;
        constexpr size_t movingBodies = 5000;   // 5000 moving bodies for narrowphase stress
        constexpr size_t staticBodies = totalBodies - movingBodies;

        std::cout << "Scenario: " << staticBodies << " static + " << movingBodies << " moving bodies" << std::endl;

        // Generate world-like distribution: many statics, few movables
        auto testBodies = generateWorldScenario(staticBodies, movingBodies);

        // Test cache effectiveness by running multiple frames
        auto result = benchmarkCacheEffectiveness(testBodies);

        std::cout << "Cache benchmark completed - see collision manager debug output for StaticCulled%" << std::endl;
        printResult(result);
        std::cout << std::endl;
    }

    void runWorldScenarioBenchmark() {
        std::cout << "=== Realistic World Scenario Performance ===" << std::endl;
        std::cout << "Testing performance with world-like static body distribution" << std::endl;
        std::cout << std::endl;

        struct WorldTest {
            size_t staticBodies;
            size_t movableBodies;
            std::string description;
        };

        std::vector<WorldTest> worldTests = {
            {2000, 1000, "Small area (2000 static + 1000 NPCs)"},
            {5000, 2000, "Medium area (5000 static + 2000 NPCs)"},
            {10000, 5000, "Large area (10000 static + 5000 NPCs)"},
            {30000, 10000, "Massive area (30000 static + 10000 NPCs)"}
        };

        std::vector<BenchmarkResult> results;

        for (const auto& test : worldTests) {
            std::cout << "Testing " << test.description << "..." << std::endl;

            auto testBodies = generateWorldScenario(test.staticBodies, test.movableBodies);
            auto result = benchmarkSOASystem(testBodies);

            BenchmarkResult benchResult{};
            benchResult.bodyCount = test.staticBodies + test.movableBodies;
            benchResult.soaTimeMs = std::get<0>(result);
            benchResult.collisionCount = std::get<1>(result);
            benchResult.pairCount = std::get<2>(result);

            results.push_back(benchResult);
            printResult(benchResult);
            std::cout << std::endl;
        }

        printSummary(results, "World Scenario");
    }

private:
    std::mt19937 rng;
    std::uniform_real_distribution<float> posDist{-1000.0f, 1000.0f};
    std::uniform_real_distribution<float> sizeDist{5.0f, 50.0f};
    std::uniform_real_distribution<float> velDist{-100.0f, 100.0f};

    void setupRandomGenerator() {
        rng.seed(12345); // Fixed seed for reproducible results
    }

    BenchmarkResult benchmarkBodyCount(size_t bodyCount) {
        BenchmarkResult result{};
        result.bodyCount = bodyCount;

        // Generate test bodies
        std::vector<TestBody> testBodies = generateTestBodies(bodyCount);

        // Benchmark SOA system
        std::cout << "  Testing SOA collision system..." << std::flush;
        auto [soaTime, collisions, pairs] = benchmarkSOASystem(testBodies);
        result.soaTimeMs = soaTime;
        result.collisionCount = collisions;
        result.pairCount = pairs;
        std::cout << " " << std::fixed << std::setprecision(2) << result.soaTimeMs << "ms" << std::endl;

        return result;
    }

    struct TestBody {
        Vector2D position;
        Vector2D velocity;
        Vector2D halfSize;
        BodyType type;
        uint32_t layer;
        uint32_t collidesWith;
    };

    std::vector<TestBody> generateTestBodies(size_t count) {
        std::vector<TestBody> bodies;
        bodies.reserve(count + 1);  // +1 for player

        // Create overlapping grid pattern like unit tests to guarantee collisions
        size_t bodiesPerRow = static_cast<size_t>(std::sqrt(count)) + 1;
        float spacing = 60.0f;  // Bodies will be 80x80 (40.0f half-size), so 60.0f spacing = 20 pixels overlap

        // Add player at grid center for proper culling
        float gridCenter = (bodiesPerRow / 2) * spacing + 100.0f;
        TestBody player{};
        player.position = Vector2D(gridCenter, gridCenter);
        player.velocity = Vector2D(0.0f, 0.0f);
        player.halfSize = Vector2D(16.0f, 16.0f);
        player.type = BodyType::DYNAMIC;
        player.layer = CollisionLayer::Layer_Player;
        player.collidesWith = 0xFFFFFFFFu;
        bodies.push_back(player);

        for (size_t i = 0; i < count; ++i) {
            TestBody body{};

            // Grid layout with guaranteed overlaps
            float gridX = (i % bodiesPerRow) * spacing + 100.0f;
            float gridY = (i / bodiesPerRow) * spacing + 100.0f;
            body.position = Vector2D(gridX, gridY);
            body.velocity = Vector2D(velDist(rng) * 0.1f, velDist(rng) * 0.1f); // Reduced velocity
            body.halfSize = Vector2D(40.0f, 40.0f);  // Fixed size for predictable overlaps

            // Mix of body types for realistic scenario
            if (i < count * 0.7f) {
                body.type = BodyType::DYNAMIC;
            } else if (i < count * 0.9f) {
                body.type = BodyType::KINEMATIC;
            } else {
                body.type = BodyType::STATIC;
            }

            // Use same layers as working unit test to guarantee collisions
            body.layer = CollisionLayer::Layer_Enemy;
            body.collidesWith = 0xFFFFFFFFu;

            bodies.push_back(body);
        }

        return bodies;
    }

    // Generate realistic world scenario with mostly static bodies (like world tiles)
    std::vector<TestBody> generateWorldScenario(size_t staticCount, size_t movableCount) {
        std::vector<TestBody> bodies;
        bodies.reserve(staticCount + movableCount);

        // Create grid-like static bodies (world tiles, buildings, etc.)
        constexpr float tileSize = HammerEngine::TILE_SIZE;
        size_t tilesPerRow = static_cast<size_t>(std::sqrt(staticCount)) + 1;

        for (size_t i = 0; i < staticCount; ++i) {
            TestBody body{};
            // Grid layout with some randomness for realistic world
            float gridX = (i % tilesPerRow) * tileSize;
            float gridY = (i / tilesPerRow) * tileSize;
            body.position = Vector2D(gridX + posDist(rng) * 0.1f, gridY + posDist(rng) * 0.1f);
            body.velocity = Vector2D(0.0f, 0.0f); // Static bodies don't move
            body.halfSize = Vector2D(tileSize * 0.5f, tileSize * 0.5f);
            body.type = BodyType::STATIC;
            body.layer = CollisionLayer::Layer_Environment;
            body.collidesWith = 0xFFFFFFFFu;
            bodies.push_back(body);
        }

        // Create movable bodies (NPCs, player, etc.) scattered in the world
        for (size_t i = 0; i < movableCount; ++i) {
            TestBody body{};
            // Position movables within the static world area
            float worldSize = tilesPerRow * tileSize;
            std::uniform_real_distribution<float> worldPosDist{0.0f, worldSize};
            body.position = Vector2D(worldPosDist(rng), worldPosDist(rng));
            body.velocity = Vector2D(velDist(rng), velDist(rng));
            body.halfSize = Vector2D(sizeDist(rng), sizeDist(rng));

            // Mix of dynamic and kinematic movables
            body.type = (i < movableCount * 0.8f) ? BodyType::KINEMATIC : BodyType::DYNAMIC;
            body.layer = CollisionLayer::Layer_Default;
            body.collidesWith = 0xFFFFFFFFu;
            bodies.push_back(body);
        }

        return bodies;
    }

    // Test cache effectiveness by simulating multiple frames with minimal movement
    BenchmarkResult benchmarkCacheEffectiveness(const std::vector<TestBody>& testBodies) {
        auto& manager = CollisionManager::Instance();

        // Initialize ThreadSystem with auto-detected threads
        static bool threadSystemInitialized = false;
        if (!threadSystemInitialized) {
            HammerEngine::ThreadSystem::Instance().init(); // Auto-detect system threads
            // Log WorkerBudget allocations for production-matching verification
            const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
            std::cout << "System: " << std::thread::hardware_concurrency() << " hardware threads\n";
            std::cout << "WorkerBudget: " << budget.totalWorkers << " workers\n";
            threadSystemInitialized = true;
        }

        // Clear and setup - must clean both EDM and CollisionManager together
        // Always clean EDM since statics accumulate even after collision bodies are removed
        manager.prepareForStateTransition();
        EntityDataManager::Instance().prepareForStateTransition();
        manager.prepareCollisionBuffers(testBodies.size());

        // Add test bodies
        std::vector<EntityID> entityIds;
        entityIds.reserve(testBodies.size());

        for (size_t i = 0; i < testBodies.size(); ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            const auto& body = testBodies[i];
            manager.addCollisionBody(id, body.position, body.halfSize,
                                       body.type, body.layer, body.collidesWith);
            entityIds.push_back(id);
        }

        // Simulate cache effectiveness: most bodies don't move much
        constexpr int cacheTestFrames = 100;
        auto start = std::chrono::high_resolution_clock::now();

        for (int frame = 0; frame < cacheTestFrames; ++frame) {
            // Simulate minimal movement for some bodies (to test cache invalidation)
            if (frame % 10 == 0) { // Every 10 frames, move a few bodies slightly
                for (size_t i = 0; i < std::min<size_t>(10, entityIds.size()); ++i) {
                    Vector2D smallMove(2.0f, 2.0f); // Small movement within cache tolerance
                    Vector2D currentPos = testBodies[i].position;
                    manager.updateCollisionBodyPosition(entityIds[i], currentPos + smallMove);
                }
            }

            manager.update(0.016f); // Pure collision detection - uses production code paths
        }

        auto end = std::chrono::high_resolution_clock::now();

        // Get final performance stats
        const auto& perfStats = manager.getPerfStats();

        // Clean up
        for (EntityID id : entityIds) {
            manager.removeCollisionBody(id);
        }

        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();

        BenchmarkResult result{};
        result.bodyCount = testBodies.size();
        result.soaTimeMs = totalMs / cacheTestFrames;
        result.collisionCount = perfStats.lastCollisions;
        result.pairCount = perfStats.lastPairs;

        return result;
    }

    std::tuple<double, size_t, size_t> benchmarkSOASystem(const std::vector<TestBody>& testBodies) {
        auto& manager = CollisionManager::Instance();

        // Initialize ThreadSystem for threading tests (like other benchmarks)
        static bool threadSystemInitialized = false;
        if (!threadSystemInitialized) {
            HammerEngine::ThreadSystem::Instance().init(); // Auto-detect system threads
            // Log WorkerBudget allocations for production-matching verification
            const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
            std::cout << "System: " << std::thread::hardware_concurrency() << " hardware threads\n";
            std::cout << "WorkerBudget: " << budget.totalWorkers << " workers\n";
            threadSystemInitialized = true;
        }

        // Clear any existing bodies - must clean both EDM and CollisionManager together
        // Always clean EDM since statics accumulate even after collision bodies are removed
        manager.prepareForStateTransition();
        EntityDataManager::Instance().prepareForStateTransition();

        // Set world bounds - use realistic culling (default 1000.0f buffer)
        // Grid spans from (100,100) to roughly (100 + sqrt(count)*60, same for Y)
        float maxExtent = 100.0f + std::sqrt(static_cast<float>(testBodies.size())) * 60.0f + 100.0f;
        manager.setWorldBounds(0.0f, 0.0f, maxExtent, maxExtent);
        // Use default culling buffer for realistic game scenario testing
        // manager.setCullingBuffer(1000.0f); // Default - realistic game culling

        // Pre-allocate containers for better performance
        manager.prepareCollisionBuffers(testBodies.size());

        // Add test bodies to SOA system
        std::vector<EntityID> entityIds;
        entityIds.reserve(testBodies.size());

        for (size_t i = 0; i < testBodies.size(); ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            const auto& body = testBodies[i];

            manager.addCollisionBody(id, body.position, body.halfSize,
                                        body.type, body.layer, body.collidesWith);
            entityIds.push_back(id);
        }

        // Reduced warmup iterations for faster benchmarking
        for (int i = 0; i < 2; ++i) {
            manager.update(0.016f); // 60 FPS simulation
        }

        // Optimized benchmark with fewer iterations for faster completion
        constexpr int iterations = 50; // Reduced from 100 for faster benchmarking
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            manager.update(0.016f); // Pure collision detection
        }

        auto end = std::chrono::high_resolution_clock::now();

        // Get performance stats
        const auto& perfStats = manager.getPerfStats();
        size_t collisionCount = perfStats.lastCollisions;
        size_t pairCount = perfStats.lastPairs;

        // Clean up
        for (EntityID id : entityIds) {
            manager.removeCollisionBody(id);
        }

        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        return std::make_tuple(totalMs / iterations, collisionCount, pairCount);
    }

    void printResult(const BenchmarkResult& result) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Results for " << result.bodyCount << " bodies:" << std::endl;
        std::cout << "    SOA Time:   " << std::setw(8) << result.soaTimeMs << " ms" << std::endl;
        std::cout << "    Pairs:      " << std::setw(8) << result.pairCount << std::endl;
        std::cout << "    Collisions: " << std::setw(8) << result.collisionCount << std::endl;
        std::cout << "    Efficiency: " << std::setw(8) << std::fixed << std::setprecision(1)
                  << (result.pairCount > 0 ? (100.0 * result.collisionCount / result.pairCount) : 0.0) << "%" << std::endl;
    }

    void printSummary(const std::vector<BenchmarkResult>& results, const std::string& benchmarkType = "SOA") {
        std::cout << "=== " << benchmarkType << " Performance Summary ===" << std::endl;
        std::cout << std::left << std::setw(10) << "Bodies"
                  << std::setw(12) << "SOA (ms)"
                  << std::setw(10) << "Pairs"
                  << std::setw(12) << "Collisions"
                  << std::setw(12) << "Efficiency" << std::endl;
        std::cout << std::string(56, '-') << std::endl;

        for (const auto& result : results) {
            std::cout << std::fixed << std::setprecision(2);
            double efficiency = result.pairCount > 0 ? (100.0 * result.collisionCount / result.pairCount) : 0.0;
            std::cout << std::left << std::setw(10) << result.bodyCount
                      << std::setw(12) << result.soaTimeMs
                      << std::setw(10) << result.pairCount
                      << std::setw(12) << result.collisionCount
                      << std::setw(12) << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
        }

        // Calculate performance metrics
        double totalTime = 0.0;
        size_t totalPairs = 0;
        size_t totalCollisions = 0;
        for (const auto& result : results) {
            totalTime += result.soaTimeMs;
            totalPairs += result.pairCount;
            totalCollisions += result.collisionCount;
        }
        double avgTime = totalTime / results.size();
        double avgEfficiency = totalPairs > 0 ? (100.0 * totalCollisions / totalPairs) : 0.0;

        std::cout << std::endl;
        std::cout << "Average timing: " << std::fixed << std::setprecision(2)
                  << avgTime << "ms per frame" << std::endl;
        std::cout << "Overall efficiency: " << std::fixed << std::setprecision(1)
                  << avgEfficiency << "%" << std::endl;

        if (avgTime < 1.0) {
            std::cout << "✓ SOA system shows excellent performance (< 1ms per frame)!" << std::endl;
        } else if (avgTime < 5.0) {
            std::cout << "~ SOA system shows good performance (< 5ms per frame)." << std::endl;
        } else {
            std::cout << "⚠ SOA system performance may need optimization (> 5ms per frame)." << std::endl;
        }
    }
};

int main() {
    try {
        CollisionBenchmark benchmark;
        benchmark.runBenchmarkSuite();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Benchmark failed: " << e.what() << std::endl;
        return 1;
    }
}