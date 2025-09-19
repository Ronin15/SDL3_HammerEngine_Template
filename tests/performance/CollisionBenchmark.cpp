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
#include "collisions/CollisionBody.hpp"
#include "utils/Vector2D.hpp"

class CollisionBenchmark {
public:
    struct BenchmarkResult {
        size_t bodyCount;
        double legacyTimeMs;
        double soaTimeMs;
        double speedupRatio;
        size_t collisionCount;
        size_t pairCount;
    };

    CollisionBenchmark() {
        CollisionManager::Instance().init();
        setupRandomGenerator();
    }

    ~CollisionBenchmark() {
        CollisionManager::Instance().clean();
    }

    void runBenchmarkSuite() {
        std::cout << "=== Collision System Benchmark Suite ===" << std::endl;
        std::cout << "Testing SOA vs Legacy collision detection performance" << std::endl;
        std::cout << std::endl;

        std::vector<size_t> bodyCounts = {100, 500, 1000, 2000, 5000, 10000};
        std::vector<BenchmarkResult> results;

        for (size_t bodyCount : bodyCounts) {
            std::cout << "Benchmarking with " << bodyCount << " bodies..." << std::endl;

            BenchmarkResult result = benchmarkBodyCount(bodyCount);
            results.push_back(result);

            printResult(result);
            std::cout << std::endl;
        }

        printSummary(results);
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

        // Benchmark legacy system
        std::cout << "  Testing legacy collision system..." << std::flush;
        result.legacyTimeMs = benchmarkLegacySystem(testBodies);
        std::cout << " " << std::fixed << std::setprecision(2) << result.legacyTimeMs << "ms" << std::endl;

        // Benchmark SOA system
        std::cout << "  Testing SOA collision system..." << std::flush;
        auto [soaTime, collisions, pairs] = benchmarkSOASystem(testBodies);
        result.soaTimeMs = soaTime;
        result.collisionCount = collisions;
        result.pairCount = pairs;
        std::cout << " " << std::fixed << std::setprecision(2) << result.soaTimeMs << "ms" << std::endl;

        // Calculate speedup
        result.speedupRatio = result.legacyTimeMs / result.soaTimeMs;

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
        bodies.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            TestBody body{};
            body.position = Vector2D(posDist(rng), posDist(rng));
            body.velocity = Vector2D(velDist(rng), velDist(rng));
            body.halfSize = Vector2D(sizeDist(rng), sizeDist(rng));

            // Mix of body types for realistic scenario
            if (i < count * 0.7f) {
                body.type = BodyType::DYNAMIC;
            } else if (i < count * 0.9f) {
                body.type = BodyType::KINEMATIC;
            } else {
                body.type = BodyType::STATIC;
            }

            body.layer = CollisionLayer::Layer_Default;
            body.collidesWith = 0xFFFFFFFFu;

            bodies.push_back(body);
        }

        return bodies;
    }

    double benchmarkLegacySystem(const std::vector<TestBody>& testBodies) {
        auto& manager = CollisionManager::Instance();

        // Clear any existing bodies
        manager.clean();
        manager.init();

        // Add test bodies to legacy system
        std::vector<EntityID> entityIds;
        for (size_t i = 0; i < testBodies.size(); ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            const auto& body = testBodies[i];

            AABB aabb(body.position.getX(), body.position.getY(),
                     body.halfSize.getX(), body.halfSize.getY());

            manager.addBody(id, aabb, body.type, body.layer, body.collidesWith);
            entityIds.push_back(id);
        }

        // Warm up
        for (int i = 0; i < 3; ++i) {
            manager.update(0.016f); // 60 FPS
        }

        // Benchmark
        constexpr int iterations = 100;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            manager.update(0.016f);
        }

        auto end = std::chrono::high_resolution_clock::now();

        // Clean up
        for (EntityID id : entityIds) {
            manager.removeBody(id);
        }

        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        return totalMs / iterations;
    }

    std::tuple<double, size_t, size_t> benchmarkSOASystem(const std::vector<TestBody>& testBodies) {
        auto& manager = CollisionManager::Instance();

        // Clear any existing bodies
        manager.clean();
        manager.init();

        // Add test bodies to SOA system
        std::vector<EntityID> entityIds;
        for (size_t i = 0; i < testBodies.size(); ++i) {
            EntityID id = static_cast<EntityID>(i + 1000000); // Different ID range
            const auto& body = testBodies[i];

            size_t index = manager.addCollisionBodySOA(id, body.position, body.halfSize,
                                                      body.type, body.layer, body.collidesWith);
            entityIds.push_back(id);
        }

        // Warm up
        for (int i = 0; i < 3; ++i) {
            manager.updateSOA(0.016f); // 60 FPS
        }

        // Benchmark
        constexpr int iterations = 100;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            manager.updateSOA(0.016f);
        }

        auto end = std::chrono::high_resolution_clock::now();

        // Get performance stats
        const auto& perfStats = manager.getPerfStats();
        size_t collisionCount = perfStats.lastCollisions;
        size_t pairCount = perfStats.lastPairs;

        // Clean up
        for (EntityID id : entityIds) {
            manager.removeCollisionBodySOA(id);
        }

        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        return std::make_tuple(totalMs / iterations, collisionCount, pairCount);
    }

    void printResult(const BenchmarkResult& result) {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Results for " << result.bodyCount << " bodies:" << std::endl;
        std::cout << "    Legacy:     " << std::setw(8) << result.legacyTimeMs << " ms" << std::endl;
        std::cout << "    SOA:        " << std::setw(8) << result.soaTimeMs << " ms" << std::endl;
        std::cout << "    Speedup:    " << std::setw(8) << result.speedupRatio << "x" << std::endl;
        std::cout << "    Pairs:      " << std::setw(8) << result.pairCount << std::endl;
        std::cout << "    Collisions: " << std::setw(8) << result.collisionCount << std::endl;
    }

    void printSummary(const std::vector<BenchmarkResult>& results) {
        std::cout << "=== Performance Summary ===" << std::endl;
        std::cout << std::left << std::setw(10) << "Bodies"
                  << std::setw(12) << "Legacy (ms)"
                  << std::setw(12) << "SOA (ms)"
                  << std::setw(10) << "Speedup"
                  << std::setw(10) << "Pairs"
                  << std::setw(12) << "Collisions" << std::endl;
        std::cout << std::string(66, '-') << std::endl;

        for (const auto& result : results) {
            std::cout << std::fixed << std::setprecision(2);
            std::cout << std::left << std::setw(10) << result.bodyCount
                      << std::setw(12) << result.legacyTimeMs
                      << std::setw(12) << result.soaTimeMs
                      << std::setw(10) << result.speedupRatio << "x"
                      << std::setw(10) << result.pairCount
                      << std::setw(12) << result.collisionCount << std::endl;
        }

        // Calculate average speedup
        double totalSpeedup = 0.0;
        for (const auto& result : results) {
            totalSpeedup += result.speedupRatio;
        }
        double avgSpeedup = totalSpeedup / results.size();

        std::cout << std::endl;
        std::cout << "Average speedup: " << std::fixed << std::setprecision(2)
                  << avgSpeedup << "x" << std::endl;

        if (avgSpeedup > 1.5) {
            std::cout << "✓ SOA system shows significant performance improvement!" << std::endl;
        } else if (avgSpeedup > 1.0) {
            std::cout << "~ SOA system shows moderate performance improvement." << std::endl;
        } else {
            std::cout << "⚠ SOA system performance needs investigation." << std::endl;
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