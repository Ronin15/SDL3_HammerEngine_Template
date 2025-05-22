/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE AIScalingBenchmark
#include <boost/test/included/unit_test.hpp>

#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <memory>
#include <iomanip>
#include <random>
#include <map>

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"

// Simple test entity for benchmarking
class BenchmarkEntity : public Entity {
public:
    BenchmarkEntity(int id, const Vector2D& pos) : m_id(id) {
        setPosition(pos);
        setTextureID("benchmark_texture");
        setWidth(32);
        setHeight(32);
    }

    void update() override { m_updateCount++; }
    void render() override {}
    void clean() override {}

    void updatePosition(float dx, float dy) {
        Vector2D pos = getPosition();
        pos.setX(pos.getX() + dx);
        pos.setY(pos.getY() + dy);
        setPosition(pos);
        m_updateCount++;
    }

    int getId() const { return m_id; }
    int getUpdateCount() const { return m_updateCount; }

private:
    int m_id;
    std::atomic<int> m_updateCount{0};
};

// Simple test behavior for benchmarking
class BenchmarkBehavior : public AIBehavior {
public:
    BenchmarkBehavior(int id, int complexity = 1)
        : m_id(id), m_complexity(complexity) {}

    void update(Entity* entity) override {
        if (!entity) return;

        auto* benchmarkEntity = static_cast<BenchmarkEntity*>(entity);

        // Simulate work based on complexity (1-10)
        float tempResult = 0.0f;
        for (int i = 0; i < m_complexity * 10; ++i) {
            // Perform some computations to simulate AI logic
            float dx = static_cast<float>(m_rng() % 100) / 1000.0f;
            float dy = static_cast<float>(m_rng() % 100) / 1000.0f;
            tempResult += dx * dy; // Accumulate result to avoid unused variable warnings
        }
        // Use the result in a way that doesn't affect behavior but prevents compiler warnings
        if (tempResult < 0.0f) { /* This will never happen, just to use the variable */ }

        // Update entity position
        benchmarkEntity->updatePosition(
            static_cast<float>(m_rng() % 10) / 100.0f,
            static_cast<float>(m_rng() % 10) / 100.0f);

        m_updateCount++;
    }

    void init(Entity* /* entity */) override { m_initialized = true; }
    void clean(Entity* /* entity */) override { m_initialized = false; }

    std::string getName() const override {
        return "BenchmarkBehavior" + std::to_string(m_id);
    }

    void onMessage(Entity* /* entity */, const std::string& /* message */) override {
        m_messageCount++;
    }

    int getUpdateCount() const { return m_updateCount; }
    int getMessageCount() const { return m_messageCount; }

private:
    int m_id;
    int m_complexity;
    bool m_initialized{false};
    std::atomic<int> m_updateCount{0};
    std::atomic<int> m_messageCount{0};
    std::mt19937 m_rng{std::random_device{}()};
};

// Fixture for benchmark setup/teardown
struct AIScalingFixture {
    AIScalingFixture() {
        // Initialize systems
        Forge::ThreadSystem::Instance().init();
        AIManager::Instance().init();
        AIManager::Instance().configureThreading(true);

        std::cout << "=========================================" << std::endl;
        std::cout << "AI SCALING BENCHMARK" << std::endl;
        std::cout << "=========================================" << std::endl;
    }

    ~AIScalingFixture() {
        // Clean up in reverse order
        AIManager::Instance().clean();
        Forge::ThreadSystem::Instance().clean();
    }

    // Run benchmark with specific parameters
    void runBenchmark(int numEntities, int numBehaviors, int numUpdates, bool useThreading) {
        entities.clear();
        behaviors.clear();

        // Configure threading
        AIManager::Instance().configureThreading(useThreading);
        std::string threadingMode = useThreading ? "Threaded" : "Single-Threaded";

        std::cout << "\nBenchmark: " << threadingMode << " mode, "
                  << numEntities << " entities, "
                  << numBehaviors << " behaviors, "
                  << numUpdates << " updates" << std::endl;

        // Create behaviors with varying complexity
        for (int i = 0; i < numBehaviors; ++i) {
            // Complexity ranges from 1-10 based on behavior index
            int complexity = 1 + (i % 10);
            behaviors.push_back(std::make_shared<BenchmarkBehavior>(i, complexity));
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behaviors.back());
        }

        // Create entities
        for (int i = 0; i < numEntities; ++i) {
            entities.push_back(std::make_unique<BenchmarkEntity>(
                i, Vector2D(i % 1000, static_cast<float>(i) / 1000.0f)));

            // Assign behaviors in a round-robin fashion
            std::string behaviorName = "Behavior" + std::to_string(i % numBehaviors);
            AIManager::Instance().assignBehaviorToEntity(entities.back().get(), behaviorName);
        }

        // Organize entities by behavior for batch updates
        std::vector<std::vector<Entity*>> behaviorEntities(numBehaviors);
        for (size_t i = 0; i < entities.size(); ++i) {
            int behaviorIdx = i % numBehaviors;
            behaviorEntities[behaviorIdx].push_back(entities[i].get());
        }

        // Measure performance
        auto startTime = std::chrono::high_resolution_clock::now();

        if (useThreading) {
            // Run update with threading
            for (int update = 0; update < numUpdates; ++update) {
                // Use batch update all behaviors
                AIManager::Instance().batchUpdateAllBehaviors();
            }
        } else {
            // Run update in single-threaded mode
            for (int update = 0; update < numUpdates; ++update) {
                // Process each behavior sequentially
                for (int i = 0; i < numBehaviors; ++i) {
                    std::string behaviorName = "Behavior" + std::to_string(i);
                    AIManager::Instance().batchProcessEntities(behaviorName, behaviorEntities[i]);
                }
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Calculate statistics
        double totalTimeMs = duration.count() / 1000.0;
        double timePerUpdateMs = totalTimeMs / numUpdates;
        double timePerEntityMs = totalTimeMs / (static_cast<double>(numEntities) * numUpdates);
        double updatesPerSecond = (static_cast<double>(numEntities) * numUpdates) / (totalTimeMs / 1000.0);

        // Print results
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  Total time: " << totalTimeMs << " ms" << std::endl;
        std::cout << "  Time per update: " << timePerUpdateMs << " ms" << std::endl;
        std::cout << "  Time per entity: " << timePerEntityMs << " ms" << std::endl;
        std::cout << "  Updates per second: " << updatesPerSecond << std::endl;

        // Verify all entities were updated
        int notUpdatedCount = 0;
        for (const auto& entity : entities) {
            if (entity->getUpdateCount() == 0) {
                notUpdatedCount++;
            }
        }

        if (notUpdatedCount > 0) {
            std::cout << "  WARNING: " << notUpdatedCount << " entities not updated!" << std::endl;
        }

        // Clean up
        AIManager::Instance().resetBehaviors();
    }

    // Run scalability test with increasing entity counts
    void runScalabilityTest(bool useThreading) {
        std::string threadingMode = useThreading ? "Threaded" : "Single-Threaded";
        std::cout << "\n=========================================" << std::endl;
        std::cout << "SCALABILITY TEST: " << threadingMode << std::endl;
        std::cout << "=========================================" << std::endl;

        const int numBehaviors = 10;
        const int numUpdates = 10;
        std::vector<int> entityCounts = {100, 500, 1000, 5000, 10000, 25000, 50000};

        std::map<int, double> updateRates;

        for (int numEntities : entityCounts) {
            if (numEntities > 10000 && !useThreading) {
                std::cout << "Skipping " << numEntities << " entities in single-threaded mode (too slow)" << std::endl;
                continue;
            }

            runBenchmark(numEntities, numBehaviors, numUpdates, useThreading);

            // Calculate updates per second
            auto startTime = std::chrono::high_resolution_clock::now();

            if (useThreading) {
                AIManager::Instance().batchUpdateAllBehaviors();
            } else {
                for (int i = 0; i < numBehaviors; ++i) {
                    std::string behaviorName = "Behavior" + std::to_string(i);
                    std::vector<Entity*> behaviorEntities;
                    for (size_t j = i; j < entities.size(); j += numBehaviors) {
                        behaviorEntities.push_back(entities[j].get());
                    }
                    AIManager::Instance().batchProcessEntities(behaviorName, behaviorEntities);
                }
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            double updatesPerSecond = static_cast<double>(numEntities) / (duration.count() / 1000000.0);
            updateRates[numEntities] = updatesPerSecond;

            // Clean up after each test
            AIManager::Instance().resetBehaviors();
        }

        // Print summary
        std::cout << "\nSCALABILITY SUMMARY (" << threadingMode << "):" << std::endl;
        std::cout << "Entity Count | Updates Per Second" << std::endl;
        std::cout << "-------------|-----------------" << std::endl;

        for (const auto& [count, rate] : updateRates) {
            std::cout << std::setw(12) << count << " | "
                      << std::setw(15) << std::fixed << std::setprecision(0) << rate << std::endl;
        }
    }

    std::vector<std::unique_ptr<BenchmarkEntity>> entities;
    std::vector<std::shared_ptr<BenchmarkBehavior>> behaviors;
};

BOOST_FIXTURE_TEST_SUITE(AIScalingTests, AIScalingFixture)

// Test the difference between threaded and non-threaded performance
BOOST_AUTO_TEST_CASE(TestThreadingPerformance) {
    const int numEntities = 1000;
    const int numBehaviors = 5;
    const int numUpdates = 20;

    // Run in single-threaded mode
    runBenchmark(numEntities, numBehaviors, numUpdates, false);

    // Run in multi-threaded mode
    runBenchmark(numEntities, numBehaviors, numUpdates, true);
}

// Test scalability with increasing entity counts
BOOST_AUTO_TEST_CASE(TestScalabilitySingleThreaded) {
    runScalabilityTest(false);
}

// Test scalability with threading
BOOST_AUTO_TEST_CASE(TestScalabilityThreaded) {
    runScalabilityTest(true);
}

// Test extreme case
BOOST_AUTO_TEST_CASE(TestLargeEntityCount) {
    // Only run if we have a lot of memory
    #ifdef ENABLE_EXTREME_TESTS
    // 100,000 entities with 10 behaviors
    runBenchmark(100000, 10, 5, true);
    #endif
}

BOOST_AUTO_TEST_SUITE_END()
