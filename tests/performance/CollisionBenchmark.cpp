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
#include "utils/Camera.hpp"

class CollisionBenchmark {
public:
    struct BenchmarkResult {
        size_t bodyCount;
        double soaTimeMs;
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
        std::cout << "=== Collision System SOA Benchmark Suite (WITH CAMERA CULLING) ===" << std::endl;
        std::cout << "Testing camera-culled SOA collision detection performance" << std::endl;
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


    std::tuple<double, size_t, size_t> benchmarkSOASystem(const std::vector<TestBody>& testBodies) {
        auto& manager = CollisionManager::Instance();

        // Clear any existing bodies
        manager.clean();
        manager.init();

        // Create camera positioned at center of entity distribution (0,0)
        HammerEngine::Camera camera(0.0f, 0.0f, 1920.0f, 1080.0f); // Standard 1080p viewport

        // Add test bodies to SOA system
        std::vector<EntityID> entityIds;
        for (size_t i = 0; i < testBodies.size(); ++i) {
            EntityID id = static_cast<EntityID>(i + 1);
            const auto& body = testBodies[i];

            manager.addCollisionBodySOA(id, body.position, body.halfSize,
                                        body.type, body.layer, body.collidesWith);
            entityIds.push_back(id);
        }

        // Warm up with camera culling
        for (int i = 0; i < 3; ++i) {
            manager.updateSOA(0.016f); // 60 FPS with configurable culling
        }

        // Benchmark with camera culling
        constexpr int iterations = 100;
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            manager.updateSOA(0.016f); // Configurable culling collision detection
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
        std::cout << "    SOA Time:   " << std::setw(8) << result.soaTimeMs << " ms" << std::endl;
        std::cout << "    Pairs:      " << std::setw(8) << result.pairCount << std::endl;
        std::cout << "    Collisions: " << std::setw(8) << result.collisionCount << std::endl;
        std::cout << "    Efficiency: " << std::setw(8) << std::fixed << std::setprecision(1)
                  << (result.pairCount > 0 ? (100.0 * result.collisionCount / result.pairCount) : 0.0) << "%" << std::endl;
    }

    void printSummary(const std::vector<BenchmarkResult>& results) {
        std::cout << "=== SOA Performance Summary ===" << std::endl;
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