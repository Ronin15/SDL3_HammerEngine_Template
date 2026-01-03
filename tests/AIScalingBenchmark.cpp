/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * AI Scaling Benchmark
 *
 * Tests the AI system's performance characteristics:
 * 1. Entity scaling from 100 to 10,000 entities
 * 2. Threading mode comparison (single vs multi-threaded)
 * 3. Behavior mix impact on performance
 * 4. WorkerBudget integration effectiveness
 *
 * Follows the same structure as CollisionScalingBenchmark for consistency.
 */

#define BOOST_TEST_MODULE AIScalingBenchmark
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <thread>

#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "core/Logger.hpp"

// Production AI behaviors
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"

namespace {

// Test fixture for AI scaling benchmarks
class AIScalingFixture {
public:
    AIScalingFixture() {
        // Initialize systems once per fixture
        if (!s_initialized) {
            HAMMER_ENABLE_BENCHMARK_MODE();
            HammerEngine::ThreadSystem::Instance().init();
            EntityDataManager::Instance().init();
            PathfinderManager::Instance().init();
            PathfinderManager::Instance().rebuildGrid();
            CollisionManager::Instance().init();
            AIManager::Instance().init();
            BackgroundSimulationManager::Instance().init();

            // Set simulation radii for headless testing
            BackgroundSimulationManager::Instance().setActiveRadius(50000.0f);
            BackgroundSimulationManager::Instance().setBackgroundRadius(100000.0f);

            // Register production behaviors once
            registerProductionBehaviors();

            s_initialized = true;
        }
        m_rng.seed(42); // Fixed seed for reproducibility
    }

    ~AIScalingFixture() = default;

    // Prepare fresh state for each test
    void prepareForTest() {
        AIManager::Instance().prepareForStateTransition();
        EntityDataManager::Instance().prepareForStateTransition();
        CollisionManager::Instance().prepareForStateTransition();
    }

    // Create AI entities via EntityDataManager
    void createEntities(size_t count, float worldSize) {
        auto& edm = EntityDataManager::Instance();
        auto& aim = AIManager::Instance();
        std::uniform_real_distribution<float> posDist(100.0f, worldSize - 100.0f);

        static const std::vector<std::string> behaviors =
            {"Wander", "Guard", "Patrol", "Follow", "Chase"};

        for (size_t i = 0; i < count; ++i) {
            Vector2D pos(posDist(m_rng), posDist(m_rng));
            EntityHandle handle = edm.createNPC(pos, 16.0f, 16.0f);

            // Enable collision for the entity
            size_t idx = edm.getIndex(handle);
            if (idx != SIZE_MAX) {
                auto& hot = edm.getHotDataByIndex(idx);
                hot.collisionLayers = CollisionLayer::Layer_Enemy;
                hot.collisionMask = 0xFFFF;
                hot.setCollisionEnabled(true);
            }

            // Assign behavior in round-robin fashion
            aim.assignBehavior(handle, behaviors[i % behaviors.size()]);
            m_handles.push_back(handle);
        }

        // Set first entity as player reference for distance calculations
        if (!m_handles.empty()) {
            aim.setPlayerHandle(m_handles[0]);
        }
    }

    // Create entities with specific behavior distribution
    void createEntitiesWithBehaviors(size_t count, float worldSize,
                                      const std::vector<std::string>& behaviors) {
        auto& edm = EntityDataManager::Instance();
        auto& aim = AIManager::Instance();
        std::uniform_real_distribution<float> posDist(100.0f, worldSize - 100.0f);

        for (size_t i = 0; i < count; ++i) {
            Vector2D pos(posDist(m_rng), posDist(m_rng));
            EntityHandle handle = edm.createNPC(pos, 16.0f, 16.0f);

            aim.assignBehavior(handle, behaviors[i % behaviors.size()]);
            m_handles.push_back(handle);
        }

        if (!m_handles.empty()) {
            aim.setPlayerHandle(m_handles[0]);
        }
    }

    // Set up world bounds and simulation tiers
    void setupWorld(float size) {
        CollisionManager::Instance().setWorldBounds(0.0f, 0.0f, size, size);
        CollisionManager::Instance().prepareCollisionBuffers(m_handles.size());

        BackgroundSimulationManager::Instance().setReferencePoint(Vector2D(size * 0.5f, size * 0.5f));

        // Update simulation tiers to include all entities
        EntityDataManager::Instance().updateSimulationTiers(
            Vector2D(size * 0.5f, size * 0.5f), size, size * 2.0f);
    }

    // Run benchmark iterations and return average time in ms
    double runBenchmark(int iterations) {
        auto& aim = AIManager::Instance();

        // Brief warmup (3 frames)
        for (int i = 0; i < 3; ++i) {
            aim.update(0.016f);
        }

        // Wait for warmup completion
        aim.waitForAsyncBatchCompletion();

        // Benchmark
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; ++i) {
            aim.update(0.016f);
        }

        // Wait for all async work to complete
        aim.waitForAsyncBatchCompletion();

        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    }

    // Get behavior update count for metrics
    size_t getBehaviorUpdateCount() const {
        return AIManager::Instance().getBehaviorUpdateCount();
    }

    void cleanup() {
        auto& aim = AIManager::Instance();
        for (const auto& handle : m_handles) {
            aim.unassignBehavior(handle);
            aim.unregisterEntity(handle);
        }
        m_handles.clear();
    }

private:
    void registerProductionBehaviors() {
        auto& aim = AIManager::Instance();

        // Register all production behaviors
        auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 100.0f);
        aim.registerBehavior("Wander", wander);

        auto guard = std::make_shared<GuardBehavior>(Vector2D(5000.0f, 5000.0f), 500.0f);
        aim.registerBehavior("Guard", guard);

        std::vector<Vector2D> waypoints = {
            Vector2D(4000.0f, 5000.0f),
            Vector2D(6000.0f, 5000.0f)
        };
        auto patrol = std::make_shared<PatrolBehavior>(waypoints, 100.0f, true);
        aim.registerBehavior("Patrol", patrol);

        auto follow = std::make_shared<FollowBehavior>(2.5f, 200.0f, 400.0f);
        aim.registerBehavior("Follow", follow);

        auto chase = std::make_shared<ChaseBehavior>(100.0f, 500.0f, 50.0f);
        aim.registerBehavior("Chase", chase);
    }

    std::mt19937 m_rng;
    std::vector<EntityHandle> m_handles;
    static bool s_initialized;
};

bool AIScalingFixture::s_initialized = false;

} // anonymous namespace

// ===========================================================================
// Benchmark Suite
// ===========================================================================

BOOST_FIXTURE_TEST_SUITE(AIScalingTests, AIScalingFixture)

// Print header with system info
BOOST_AUTO_TEST_CASE(PrintHeader)
{
    const auto& budget = HammerEngine::WorkerBudgetManager::Instance().getBudget();
    size_t threshold = AIManager::Instance().getThreadingThreshold();

    std::cout << "\n=== AI Scaling Benchmark ===\n";
    std::cout << "Date: " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "System: " << std::thread::hardware_concurrency() << " hardware threads\n";
    std::cout << "WorkerBudget: " << budget.totalWorkers << " workers\n";
    std::cout << "Threading threshold: " << threshold << " entities\n";
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// AI Entity Scaling (Primary benchmark)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(AIEntityScaling)
{
    std::cout << "--- AI Entity Scaling ---\n";
    std::cout << std::setw(10) << "Entities"
              << std::setw(12) << "Time (ms)"
              << std::setw(14) << "Updates/sec"
              << std::setw(12) << "Threading"
              << std::setw(10) << "Status\n";

    std::vector<size_t> entityCounts = {100, 500, 1000, 2000, 5000, 10000};
    size_t threshold = AIManager::Instance().getThreadingThreshold();

    // Track best performance for summary
    size_t bestCount = 0;
    double bestUpdatesPerSec = 0.0;

    for (size_t count : entityCounts) {
        prepareForTest();

        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        createEntities(count, worldSize);
        setupWorld(worldSize * 2.0f);

        // Dynamic iteration scaling: ensure ~100ms measurement
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        size_t startUpdates = getBehaviorUpdateCount();
        double avgMs = runBenchmark(iterations);
        size_t endUpdates = getBehaviorUpdateCount();

        size_t totalUpdates = endUpdates - startUpdates;
        double updatesPerSec = (totalUpdates > 0)
            ? (static_cast<double>(totalUpdates) / (avgMs * iterations)) * 1000.0
            : 0.0;

        const char* threading = (count >= threshold) ? "multi" : "single";
        const char* status = (totalUpdates > 0) ? "OK" : "FAIL";

        // Track best
        if (updatesPerSec > bestUpdatesPerSec) {
            bestUpdatesPerSec = updatesPerSec;
            bestCount = count;
        }

        std::cout << std::setw(10) << count
                  << std::setw(12) << std::fixed << std::setprecision(2) << avgMs
                  << std::setw(14) << std::fixed << std::setprecision(0) << updatesPerSec
                  << std::setw(12) << threading
                  << std::setw(10) << status << "\n";

        cleanup();
    }

    // Output summary for regression detection
    std::cout << "\nSCALABILITY SUMMARY:\n";
    std::cout << "Entity updates per second: " << std::fixed << std::setprecision(0)
              << bestUpdatesPerSec << " (at " << bestCount << " entities)\n";
    std::cout << "Threading mode: " << (bestCount >= threshold ? "WorkerBudget Multi-threaded" : "Single-threaded") << "\n";
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Threading Mode Comparison
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ThreadingModeComparison)
{
    std::cout << "--- Threading Mode Comparison ---\n";
    std::cout << std::setw(10) << "Entities"
              << std::setw(14) << "Single (ms)"
              << std::setw(14) << "Multi (ms)"
              << std::setw(10) << "Speedup\n";

    std::vector<size_t> entityCounts = {500, 1000, 2000, 5000};

    for (size_t count : entityCounts) {
        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        // Test single-threaded
        prepareForTest();
        AIManager::Instance().enableThreading(false);
        createEntities(count, worldSize);
        setupWorld(worldSize * 2.0f);
        double singleMs = runBenchmark(iterations);
        cleanup();

        // Test multi-threaded
        prepareForTest();
        AIManager::Instance().enableThreading(true);
        createEntities(count, worldSize);
        setupWorld(worldSize * 2.0f);
        double multiMs = runBenchmark(iterations);
        cleanup();

        double speedup = (multiMs > 0) ? singleMs / multiMs : 0.0;

        std::cout << std::setw(10) << count
                  << std::setw(14) << std::fixed << std::setprecision(2) << singleMs
                  << std::setw(14) << std::fixed << std::setprecision(2) << multiMs
                  << std::setw(9) << std::fixed << std::setprecision(2) << speedup << "x\n";
    }

    // Restore default threading mode
    AIManager::Instance().enableThreading(true);
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Behavior Mix Test
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(BehaviorMixTest)
{
    std::cout << "--- Behavior Mix Test (2000 entities) ---\n";
    std::cout << std::setw(15) << "Distribution"
              << std::setw(12) << "Time (ms)"
              << std::setw(14) << "Updates/sec\n";

    constexpr size_t ENTITY_COUNT = 2000;
    constexpr float WORLD_SIZE = 4000.0f;

    struct BehaviorMix {
        const char* name;
        std::vector<std::string> behaviors;
    };

    std::vector<BehaviorMix> mixes = {
        {"All Wander", {"Wander"}},
        {"Wander+Guard", {"Wander", "Guard"}},
        {"Full Mix", {"Wander", "Guard", "Patrol", "Follow", "Chase"}}
    };

    for (const auto& mix : mixes) {
        prepareForTest();
        createEntitiesWithBehaviors(ENTITY_COUNT, WORLD_SIZE, mix.behaviors);
        setupWorld(WORLD_SIZE * 2.0f);

        size_t startUpdates = getBehaviorUpdateCount();
        double avgMs = runBenchmark(50);
        size_t endUpdates = getBehaviorUpdateCount();

        size_t totalUpdates = endUpdates - startUpdates;
        double updatesPerSec = (totalUpdates > 0)
            ? (static_cast<double>(totalUpdates) / (avgMs * 50)) * 1000.0
            : 0.0;

        std::cout << std::setw(15) << mix.name
                  << std::setw(12) << std::fixed << std::setprecision(2) << avgMs
                  << std::setw(14) << std::fixed << std::setprecision(0) << updatesPerSec << "\n";

        cleanup();
    }
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(PrintSummary)
{
    size_t threshold = AIManager::Instance().getThreadingThreshold();

    std::cout << "SUMMARY:\n";
    std::cout << "  AI batch processing: O(n) scaling with WorkerBudget\n";
    std::cout << "  Threading threshold: " << threshold << " entities\n";
    std::cout << "  Entity iteration: Active tier only (via getActiveIndices)\n";
    std::cout << "  Behavior execution: Type-indexed O(1) lookup\n";
    std::cout << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
