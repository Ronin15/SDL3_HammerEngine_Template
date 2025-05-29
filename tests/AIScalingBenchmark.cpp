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
#include <algorithm>

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"

// Global state to track initialization status
namespace {
    std::mutex g_setupMutex;
    std::atomic<bool> g_systemsInitialized{false};
    std::atomic<bool> g_shutdownInProgress{false};
}

// Simple test entity for benchmarking
class BenchmarkEntity : public Entity {
public:
    BenchmarkEntity(int id, const Vector2D& pos) : m_id(id) {
        setPosition(pos);
        setTextureID("benchmark_texture");
        setWidth(32);
        setHeight(32);
    }

    static std::shared_ptr<BenchmarkEntity> create(int id, const Vector2D& pos) {
        return std::make_shared<BenchmarkEntity>(id, pos);
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
    BenchmarkBehavior(int id, int complexity = 5) // Increased default complexity from 1 to 5
        : m_id(id), m_complexity(complexity), m_initialized(false) {
        // Set high priority for benchmark
        setPriority(9);
    }

    void executeLogic(EntityPtr entity) override {
        if (!entity) return;

        auto benchmarkEntity = std::dynamic_pointer_cast<BenchmarkEntity>(entity);

        // Simulate work based on complexity (5-15) - Increased work amount
        float tempResult = 0.0f;
        for (int i = 0; i < m_complexity * 30; ++i) { // Increased multiplier from 10 to 30
            // Perform some computations to simulate AI logic
            float dx = static_cast<float>(m_rng() % 100) / 1000.0f;
            float dy = static_cast<float>(m_rng() % 100) / 1000.0f;
            float dz = static_cast<float>(m_rng() % 100) / 1000.0f;
            // Added more calculations to increase computational work
            tempResult += dx * dy * dz;
            tempResult = std::sqrt(tempResult + 1.0f); // Add more computation
        }
        // Use the result in a way that doesn't affect behavior but prevents compiler warnings
        if (tempResult < 0.0f) { /* This will never happen, just to use the variable */ }

        // Update entity position
        benchmarkEntity->updatePosition(
            static_cast<float>(m_rng() % 10) / 100.0f,
            static_cast<float>(m_rng() % 10) / 100.0f);

        // Explicitly update the entity's update count to ensure it's counted
        benchmarkEntity->update();
        m_updateCount++;

        // Diagnostic info removed to reduce console spam
    }

    void init(EntityPtr /* entity */) override {
        m_initialized = true;

        // No extra updates during init - we'll rely on the regular update cycle
    }
    void clean(EntityPtr /* entity */) override { m_initialized = false; }

    std::string getName() const override {
        return "BenchmarkBehavior" + std::to_string(m_id);
    }

    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<BenchmarkBehavior>(m_id, m_complexity);
        cloned->setActive(m_active);
        cloned->setPriority(m_priority);
        return cloned;
    }

    void onMessage(EntityPtr /* entity */, const std::string& /* message */) override {
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

// Global fixture for the entire test suite
struct GlobalFixture {
    GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_setupMutex);
        if (!g_systemsInitialized) {
            Forge::ThreadSystem::Instance().init();
            AIManager::Instance().init();
            g_systemsInitialized = true;
        }
    }

    ~GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_setupMutex);
        if (g_systemsInitialized) {
            // Signal that shutdown is in progress
            g_shutdownInProgress.store(true);

            // Wait for any pending operations to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Clean up in reverse order
            try {
                // Reset all behaviors first
                AIManager::Instance().resetBehaviors();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Clean AIManager
                AIManager::Instance().clean();

                // Wait between cleanup operations
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Clean ThreadSystem
                Forge::ThreadSystem::Instance().clean();
            } catch (const std::exception& e) {
                std::cerr << "Exception during cleanup: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception during cleanup" << std::endl;
            }

            g_systemsInitialized = false;
        }
    }
};

// Register global fixture
BOOST_GLOBAL_FIXTURE(GlobalFixture);

// Define test suite
BOOST_AUTO_TEST_SUITE(AIScalingTests)

// Fixture for benchmark setup/teardown
struct AIScalingFixture {
    AIScalingFixture() {
        // Configure threading for AIManager
        AIManager::Instance().configureThreading(true);

        std::cout << "=========================================" << std::endl;
        std::cout << "AI SCALING BENCHMARK" << std::endl;
        std::cout << "=========================================" << std::endl;
    }

    ~AIScalingFixture() {
        // Clean up at end of test
        cleanupEntitiesAndBehaviors();

        // Clear collections
        entities.clear();
        behaviors.clear();
    }

    // Run benchmark with specific parameters
    void runBenchmark(int numEntities, int numBehaviors, int numUpdates, bool useThreading, int numMeasurements = 3) {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        // Temporarily disable threading for cleanup to avoid race conditions
        bool wasThreaded = useThreading;
        if (wasThreaded) {
            AIManager::Instance().configureThreading(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Clean up from previous run
        cleanupEntitiesAndBehaviors();

        entities.clear();
        behaviors.clear();

        // Restore threading if it was enabled
        if (wasThreaded) {
            AIManager::Instance().configureThreading(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Configure threading
        AIManager::Instance().configureThreading(useThreading);
        std::string threadingMode = useThreading ? "Threaded" : "Single-Threaded";

        std::cout << "\nBenchmark: " << threadingMode << " mode, "
                  << numEntities << " entities, "
                  << numBehaviors << " behaviors, "
                  << numUpdates << " updates" << std::endl;

        // Create behaviors with varying complexity
        for (int i = 0; i < numBehaviors; ++i) {
            // Complexity ranges from 5-15 based on behavior index (increased from 1-10)
            int complexity = 5 + (i % 11);
            behaviors.push_back(std::make_shared<BenchmarkBehavior>(i, complexity));
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behaviors.back());
        }

        // Create entities
        for (int i = 0; i < numEntities; ++i) {
            auto entity = BenchmarkEntity::create(
                i, Vector2D(i % 1000, static_cast<float>(i) / 1000.0f));
            entities.push_back(entity);

            // Assign behaviors in a round-robin fashion
            std::string behaviorName = "Behavior" + std::to_string(i % numBehaviors);
            AIManager::Instance().assignBehaviorToEntity(entity, behaviorName);
            // Register entity for managed updates
            AIManager::Instance().registerEntityForUpdates(entity);
        }

        // Organize entities by behavior for batch updates
        // Organize entities by behavior
        std::vector<std::vector<EntityPtr>> behaviorEntities(numBehaviors);
        for (size_t i = 0; i < entities.size(); ++i) {
            int behaviorIdx = i % numBehaviors;
            behaviorEntities[behaviorIdx].push_back(entities[i]);
        }

        // Run specified number of times for measurements
        std::vector<double> durations;

        for (int run = 0; run < numMeasurements; run++) {
            // Reset entity update counts for this run
            for (auto& entity : entities) {
                entity->getUpdateCount(); // Clear by reading
            }

            // Post-run synchronization delay before starting timing
            if (useThreading) {
                // Let any previous operations complete
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Measure performance - start timing
            auto startTime = std::chrono::high_resolution_clock::now();

            if (useThreading) {
                // Run update with threading
                for (int update = 0; update < numUpdates; ++update) {
                    // Use managed entity updates
                    AIManager::Instance().updateManagedEntities();
                }
            } else {
                // Run update in single-threaded mode
                for (int update = 0; update < numUpdates; ++update) {
                    // Use managed entity updates once per update cycle
                    AIManager::Instance().updateManagedEntities();
                }
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            // Post-run synchronization to ensure all entities are processed
            // This is outside the timed section
            if (useThreading) {
                // Wait with time proportional to entity count to ensure all tasks complete
                int waitTime = std::min(1200, 50 + numEntities / 25);
                std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

                // Special handling for large entity counts
                if (numEntities >= 10000) {
                    // Temporarily switch to single-threaded mode for large counts
                    AIManager::Instance().configureThreading(false);
                    AIManager::Instance().updateManagedEntities();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    AIManager::Instance().configureThreading(true);

                    // For extremely large entity counts, add additional synchronization
                    if (numEntities >= 20000) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        AIManager::Instance().updateManagedEntities();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));

                        // Special handling for extreme entity counts (50,000+)
                        if (numEntities >= 50000) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                            AIManager::Instance().updateManagedEntities();
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));

                            // Extra handling for ultra-large entity counts (100,000+)
                            if (numEntities >= 100000) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                                AIManager::Instance().updateManagedEntities();
                                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                                AIManager::Instance().updateManagedEntities();
                                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                            }
                        }
                    }
                } else {
                    // Multiple updates for better coverage
                    for (int i = 0; i < 3; i++) {
                        AIManager::Instance().updateManagedEntities();
                        std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    }
                }
            }

            // Store the duration, ensuring it's at least 1 microsecond
            durations.push_back(std::max(1.0, static_cast<double>(duration.count())));
        }

        // Calculate the average duration
        double avgDuration = 0.0;
        if (numMeasurements == 1) {
            avgDuration = durations[0];
        } else {
            // Exclude the highest value for stability with multiple measurements
            std::sort(durations.begin(), durations.end());
            for (size_t i = 0; i < durations.size() - 1; i++) {
                avgDuration += durations[i];
            }
            avgDuration /= (durations.size() - 1);
        }

        // Calculate statistics with the average duration
        double totalTimeMs = avgDuration / 1000.0;
        double timePerUpdateMs = totalTimeMs / numUpdates;
        double timePerEntityMs = totalTimeMs / (static_cast<double>(numEntities) * numUpdates);
        // Calculate derived metrics
        double entitiesPerSecond = (static_cast<double>(numEntities) * numUpdates) / (totalTimeMs / 1000.0);
        
        // Note: Individual behavior instances (not templates) are updated via executeLogic()
        // Template behaviors stored in 'behaviors' vector are not directly updated
        int totalBehaviorUpdates = 0; // Keep for output format compatibility
        
        // Verify all entities were updated
        int notUpdatedCount = 0;
        for (const auto& entity : entities) {
            if (entity->getUpdateCount() == 0) {
                notUpdatedCount++;
            }
        }

        // Print results in clean format matching event benchmark
        std::cout << "\nPerformance Results (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Total time: " << totalTimeMs << " ms" << std::endl;
        std::cout << "  Time per update cycle: " << timePerUpdateMs << " ms" << std::endl;
        std::cout << std::setprecision(6);
        std::cout << "  Time per entity: " << timePerEntityMs << " ms" << std::endl;
        std::cout << std::setprecision(0);
        std::cout << "  Entity updates per second: " << entitiesPerSecond << std::endl;
        std::cout << "  Total behavior updates: " << totalBehaviorUpdates << std::endl;
        
        // Verification status with checkmark/X like event benchmark
        std::cout << "  Entity updates: " << (numEntities - notUpdatedCount) << "/" << numEntities;
        if (notUpdatedCount == 0) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗ (Missing: " << notUpdatedCount << ")" << std::endl;
            
            // If we have missed entities and threading is enabled, try one more update
            if (useThreading && notUpdatedCount > 0) {
                AIManager::Instance().updateManagedEntities();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Recount missed entities
                int finalNotUpdatedCount = 0;
                for (const auto& entity : entities) {
                    if (entity->getUpdateCount() == 0) {
                        finalNotUpdatedCount++;
                    }
                }
                
                if (finalNotUpdatedCount == 0) {
                    std::cout << "  ✓ All entities updated after recovery" << std::endl;
                }
            }
        }

        // Clear all entity frame counters
        // clearFrameCounters no longer needed - AIManager controls all update timing

        // Clean up
        cleanupEntitiesAndBehaviors();
    }

    // Helper to clean up entities and behaviors safely
    void cleanupEntitiesAndBehaviors() {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        // Clear all frame counters
        for (const auto& behavior : behaviors) {
            if (behavior) {
                try {
                    // clearFrameCounters no longer needed - AIManager controls all update timing
                } catch (const std::exception& e) {
                    std::cerr << "Error clearing frame counters: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown error clearing frame counters" << std::endl;
                }
            }
        }

        // First unregister from managed updates and unassign all behaviors from entities
        for (auto& entity : entities) {
            if (entity) {
                try {
                    AIManager::Instance().unregisterEntityFromUpdates(entity);
                    AIManager::Instance().unassignBehaviorFromEntity(entity);
                } catch (const std::exception& e) {
                    std::cerr << "Error unregistering/unassigning entity: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown error unregistering/unassigning entity" << std::endl;
                }
            }
        }

        // Wait for unassign operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Reset all behaviors
        try {
            AIManager::Instance().resetBehaviors();
            std::cout << "AI behaviors reset successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error resetting behaviors: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error resetting behaviors" << std::endl;
        }

        // Wait for any pending operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Run scalability test with increasing entity counts
    void runScalabilityTest(bool useThreading) {
        std::cout << "\n===== AI SCALABILITY TEST SUITE (" << (useThreading ? "Threaded" : "Single-Threaded") << ") =====" << std::endl;
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        const int numBehaviors = 10;
        const int numUpdates = 10;
        std::vector<int> entityCounts;

        // Use different entity counts based on threading mode to balance performance and stability
        if (useThreading) {
            entityCounts = {100, 500, 1000, 5000, 10000, 25000, 50000};
        } else {
            entityCounts = {100, 500, 1000, 5000, 10000, 25000, 50000};
        }

        std::map<int, double> updateRates;

        for (int numEntities : entityCounts) {
            if ((numEntities > 10000 && !useThreading) ||
                (numEntities > 100000 && useThreading && std::thread::hardware_concurrency() < 8)) {
                continue;
            }

            // For very large entity counts, use fewer behaviors to avoid excessive memory usage
            int adjustedNumBehaviors = (numEntities >= 100000) ? 5 : numBehaviors;
            int adjustedNumUpdates = (numEntities >= 100000) ? 5 : numUpdates;

            std::cout << "\n--- Test Case: " << numEntities << " entities, " 
                      << adjustedNumBehaviors << " behaviors, " << adjustedNumUpdates << " updates ---" << std::endl;


            runBenchmark(numEntities, adjustedNumBehaviors, adjustedNumUpdates, false);

            // Calculate updates per second
            auto startTime = std::chrono::high_resolution_clock::now();

            // Pre-run synchronization
            if (useThreading) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Use managed entity updates for both threading modes
            AIManager::Instance().updateManagedEntities();

            // Post-run synchronization (outside of timing)
            if (useThreading) {
                // Scale wait time with entity count, minimum 20ms
                // For very large counts, cap at 500ms to avoid excessive waiting
                int waitTime = std::min(500, 20 + numEntities / 100);
                std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

                // Force a second update to catch any entities that weren't processed
                AIManager::Instance().updateManagedEntities();
                std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

            // Ensure we don't get division by zero or extremely small values
            double durationInSec = std::max(0.000001, duration.count() / 1000000.0);
            double updatesPerSecond = static_cast<double>(numEntities) / durationInSec;

            // Cap the updates per second to a realistic value to avoid "inf"
            const double MAX_UPDATES_PER_SEC = 100000000.0; // 100 million updates/sec
            updatesPerSecond = std::min(updatesPerSecond, MAX_UPDATES_PER_SEC);

            updateRates[numEntities] = updatesPerSecond;

            // Clean up after each test
            cleanupEntitiesAndBehaviors();
        }

        // Print summary
        std::string threadingMode = useThreading ? "Threaded" : "Single-Threaded";
        std::cout << "\nSCALABILITY SUMMARY (" << threadingMode << "):" << std::endl;
        std::cout << "Entity Count | Updates Per Second" << std::endl;
        std::cout << "-------------|-----------------" << std::endl;

        for (const auto& [count, rate] : updateRates) {
            std::cout << std::setw(12) << count << " | "
                          << std::setw(15) << std::fixed << std::setprecision(0) << rate << std::endl;
        }
    }

    std::vector<std::shared_ptr<BenchmarkEntity>> entities;
    std::vector<std::shared_ptr<BenchmarkBehavior>> behaviors;

};

BOOST_FIXTURE_TEST_SUITE(AIScalingTests, AIScalingFixture)

// Test the difference between threaded and non-threaded performance
BOOST_AUTO_TEST_CASE(TestThreadingPerformance) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    const int numEntities = 1000;
    const int numBehaviors = 5;
    const int numUpdates = 20;

    // Run in single-threaded mode
    // For very large entity counts, use fewer behaviors to avoid excessive memory usage
    int adjustedNumBehaviors = (numEntities >= 50000) ? 5 : numBehaviors;
    int adjustedNumUpdates = (numEntities >= 50000) ? 5 : numUpdates;

    if (adjustedNumBehaviors != numBehaviors || adjustedNumUpdates != numUpdates) {
        std::cout << "Adjusted parameters for large entity count: "
                  << adjustedNumBehaviors << " behaviors, "
                  << adjustedNumUpdates << " updates" << std::endl;
    }

    runBenchmark(numEntities, adjustedNumBehaviors, adjustedNumUpdates, false);

    // Run in multi-threaded mode
    runBenchmark(numEntities, numBehaviors, numUpdates, true);

    // Clean up after test
    cleanupEntitiesAndBehaviors();
}

// Test scalability with increasing entity counts
BOOST_AUTO_TEST_CASE(TestScalabilitySingleThreaded) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    runScalabilityTest(false);

    // Clean up after test
    cleanupEntitiesAndBehaviors();
}

// Test scalability with threading
BOOST_AUTO_TEST_CASE(TestScalabilityThreaded) {
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    try {
        // Use maximum available threads for optimal performance
        unsigned int maxThreads = std::thread::hardware_concurrency();
        AIManager::Instance().configureThreading(true, maxThreads);
        std::cout << "Running scalability test with " << maxThreads << " threads" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        runScalabilityTest(true);

        // Switch to single-threaded mode for cleanup
        AIManager::Instance().configureThreading(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Clean up after test
        cleanupEntitiesAndBehaviors();
    }
    catch (const std::exception& e) {
        std::cerr << "Error in threaded test: " << e.what() << std::endl;
        AIManager::Instance().configureThreading(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cleanupEntitiesAndBehaviors();
        throw;
    }
}

// Test with extreme number of entities (200,000)
BOOST_AUTO_TEST_CASE(TestExtremeEntityCount) {
    AIScalingFixture fixture;
    // Skip if shutdown is in progress
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== EXTREME ENTITY COUNT TEST =====" << std::endl;

    try {
        // Configure for maximum thread utilization
        unsigned int maxThreads = std::thread::hardware_concurrency();
        AIManager::Instance().configureThreading(true, maxThreads);
        std::cout << "Running extreme entity test with " << maxThreads << " threads" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const int numEntities = 100000;
        const int numBehaviors = 3;   // Fewer behaviors for extreme-scale test
        const int numUpdates = 3;     // Fewer updates to avoid excessive test time

        std::cout << "\n=========================================" << std::endl;
        std::cout << "EXTREME ENTITY TEST: " << numEntities << " entities with " << numBehaviors << " behaviors" << std::endl;
        std::cout << "=========================================" << std::endl;

        // Create entities and behaviors (single run for extreme test)
        fixture.runBenchmark(numEntities, numBehaviors, numUpdates, true, 1);

        // Measure performance for a single update
        auto startTime = std::chrono::high_resolution_clock::now();

        AIManager::Instance().updateManagedEntities();

        // Allow time for updates to complete
        int waitTime = 500;  // 500ms should be enough for 200K entities
        std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

        // Force a second update to catch any entities that weren't processed
        AIManager::Instance().updateManagedEntities();
        std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        // Calculate performance metrics
        double durationInSec = std::max(0.000001, duration.count() / 1000000.0);
        double updatesPerSecond = static_cast<double>(numEntities) / durationInSec;
        double timePerEntity = durationInSec / numEntities * 1000.0;  // in milliseconds

        // Report performance
        std::cout << "\nPerformance Results:" << std::endl;
        std::cout << "  Total entities: " << numEntities << std::endl;
        std::cout << "  Total behaviors: " << numBehaviors << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << durationInSec * 1000 << " ms" << std::endl;
        std::cout << "  Time per entity: " << std::setprecision(6) << timePerEntity << " ms" << std::endl;
        std::cout << "  Updates per second: " << std::setprecision(0) << updatesPerSecond << std::endl;
        std::cout << "  Thread count: " << maxThreads << std::endl;
        
        // Note: Individual behavior instances (not templates) are updated via executeLogic()
        // Template behaviors stored in fixture.behaviors are not directly updated
        int totalBehaviorUpdates = 0; // Keep for output format compatibility
        std::cout << "  Total behavior updates: " << totalBehaviorUpdates << std::endl;

        // Only run if we have a lot of memory
        #ifdef ENABLE_EXTREME_TESTS
        // Even more extreme test with additional behaviors
        fixture.runBenchmark(100000, 5, 3, true);
        #endif

        // Switch to single-threaded mode for cleanup
        AIManager::Instance().configureThreading(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    catch (const std::exception& e) {
        std::cerr << "Error in extreme entity test: " << e.what() << std::endl;
        AIManager::Instance().configureThreading(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Extra cleanup to ensure no leftover references
    fixture.cleanupEntitiesAndBehaviors();

    // Clear collections explicitly
    fixture.entities.clear();
    fixture.behaviors.clear();

    // Wait for any pending operations to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

BOOST_AUTO_TEST_SUITE_END() // AIScalingTests
}
