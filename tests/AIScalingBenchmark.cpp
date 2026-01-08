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

/**
 * Synthetic benchmark behavior - NO shared state for contention-free threading.
 * Each entity's state is stored in the BehaviorContext/transform, not in a shared map.
 * This isolates the threading overhead from behavior state contention.
 */
class SyntheticBehavior : public AIBehavior {
public:
    SyntheticBehavior() = default;

    void init(EntityHandle) override {}
    void clean(EntityHandle) override {}

    std::string getName() const override { return "Synthetic"; }

    std::shared_ptr<AIBehavior> clone() const override {
        return std::make_shared<SyntheticBehavior>();
    }

    void onMessage(EntityHandle, const std::string&) override {}

    // NO shared state access - pure computation on context data
    void executeLogic(BehaviorContext& ctx) override {
        // Simulate realistic AI work without shared state:
        // 1. Direction calculation (local computation only)
        float angle = static_cast<float>(ctx.entityId % 628) * 0.01f;  // Deterministic per entity
        float dx = std::cos(angle);
        float dy = std::sin(angle);

        // 2. Normalization
        float len = std::sqrt(dx * dx + dy * dy);
        if (len > 0.001f) {
            dx /= len;
            dy /= len;
        }

        // 3. Boundary avoidance (local calculation)
        float px = ctx.transform.position.getX();
        float py = ctx.transform.position.getY();
        if (px < 500.0f) dx += 0.5f;
        if (px > 9500.0f) dx -= 0.5f;
        if (py < 500.0f) dy += 0.5f;
        if (py > 9500.0f) dy -= 0.5f;

        // 4. Apply velocity directly to context (no shared state write)
        float speed = 100.0f;
        ctx.transform.velocity = Vector2D(dx * speed, dy * speed);
    }
};

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
    // CRITICAL: All spawned entities MUST be in Active tier for accurate benchmarking
    // spawnWorldSize = the worldSize passed to createEntities (entities spawn in [100, spawnWorldSize-100])
    void setupWorld(float spawnWorldSize) {
        // World bounds can be larger than spawn area
        float worldBoundsSize = spawnWorldSize * 2.0f;
        CollisionManager::Instance().setWorldBounds(0.0f, 0.0f, worldBoundsSize, worldBoundsSize);
        CollisionManager::Instance().prepareCollisionBuffers(m_handles.size());

        // Entities spawn in [100, spawnWorldSize - 100]
        // Center of spawn area is at (spawnWorldSize/2, spawnWorldSize/2)
        float spawnCenter = spawnWorldSize / 2.0f;

        // Reference point at center of entity spawn area
        BackgroundSimulationManager::Instance().setReferencePoint(Vector2D(spawnCenter, spawnCenter));

        // Match old benchmark: use very large radius (100000) to ensure ALL entities are Active
        // The old test used 100000.0f radius which worked reliably
        float activeRadius = 100000.0f;
        EntityDataManager::Instance().updateSimulationTiers(
            Vector2D(spawnCenter, spawnCenter), activeRadius, activeRadius * 2.0f);
    }

    // Verify all entities are in Active tier - returns active count
    size_t verifyActiveTier() const {
        return EntityDataManager::Instance().getActiveIndices().size();
    }

    // Run benchmark iterations and return average time in ms
    double runBenchmark(int iterations) {
        auto& aim = AIManager::Instance();

        // Extended warmup for WorkerBudget hill-climb convergence
        // Hill-climb uses ADJUST_RATE=0.02f and THROUGHPUT_SMOOTHING=0.12
        // Need ~50 frames for throughput smoothing to stabilize
        // Need ~100 frames for multiplier hill-climb to converge
        constexpr int WARMUP_FRAMES = 100;
        for (int i = 0; i < WARMUP_FRAMES; ++i) {
            aim.update(0.016f);
        }

        // Wait for warmup completion
        aim.waitForAsyncBatchCompletion();

        // Benchmark (steady-state after hill-climb convergence)
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
        setupWorld(worldSize);  // Pass spawn worldSize directly

        // Verify ALL entities are in Active tier
        size_t activeCount = verifyActiveTier();
        if (activeCount != count) {
            std::cout << "WARNING: Only " << activeCount << "/" << count
                      << " entities in Active tier!\n";
        }

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
        const char* status = (activeCount == count && totalUpdates > 0) ? "OK" : "FAIL";

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

    // Save original threshold and set to 1 to force threading at all entity counts
    size_t originalThreshold = AIManager::Instance().getThreadingThreshold();
    AIManager::Instance().setThreadingThreshold(1);

    std::vector<size_t> entityCounts = {500, 1000, 2000, 5000, 10000};

    for (size_t count : entityCounts) {
        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        // Test single-threaded (disabling threading bypasses threshold check)
        prepareForTest();
        AIManager::Instance().enableThreading(false);
        createEntities(count, worldSize);
        setupWorld(worldSize);
        size_t activeCount = verifyActiveTier();
        if (activeCount != count) {
            std::cout << "WARNING: Single-thread test - Only " << activeCount
                      << "/" << count << " entities in Active tier!\n";
        }
        double singleMs = runBenchmark(iterations);
        cleanup();

        // Test multi-threaded (threshold=1 ensures threading is used)
        prepareForTest();
        AIManager::Instance().enableThreading(true);
        createEntities(count, worldSize);
        setupWorld(worldSize);
        activeCount = verifyActiveTier();
        if (activeCount != count) {
            std::cout << "WARNING: Multi-thread test - Only " << activeCount
                      << "/" << count << " entities in Active tier!\n";
        }
        double multiMs = runBenchmark(iterations);
        cleanup();

        double speedup = (multiMs > 0) ? singleMs / multiMs : 0.0;

        std::cout << std::setw(10) << count
                  << std::setw(14) << std::fixed << std::setprecision(2) << singleMs
                  << std::setw(14) << std::fixed << std::setprecision(2) << multiMs
                  << std::setw(9) << std::fixed << std::setprecision(2) << speedup << "x\n";
    }

    // Restore original threshold and default threading mode
    AIManager::Instance().setThreadingThreshold(originalThreshold);
    AIManager::Instance().enableThreading(true);
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Synthetic Behavior Threading Test (No shared state - pure threading test)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(SyntheticBehaviorThreading)
{
    std::cout << "--- Synthetic Behavior Threading (No Shared State) ---\n";
    std::cout << "Testing threading overhead without behavior state map contention\n";
    std::cout << std::setw(10) << "Entities"
              << std::setw(14) << "Single (ms)"
              << std::setw(14) << "Multi (ms)"
              << std::setw(10) << "Speedup\n";

    // Register synthetic behavior (no shared state)
    auto synthetic = std::make_shared<SyntheticBehavior>();
    AIManager::Instance().registerBehavior("Synthetic", synthetic);

    // Force threading at all entity counts
    size_t originalThreshold = AIManager::Instance().getThreadingThreshold();
    AIManager::Instance().setThreadingThreshold(1);

    std::vector<size_t> entityCounts = {500, 1000, 2000, 5000, 10000};

    for (size_t count : entityCounts) {
        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        // Test single-threaded
        prepareForTest();
        AIManager::Instance().enableThreading(false);
        createEntitiesWithBehaviors(count, worldSize, {"Synthetic"});
        setupWorld(worldSize);
        double singleMs = runBenchmark(iterations);
        cleanup();

        // Test multi-threaded
        prepareForTest();
        AIManager::Instance().enableThreading(true);
        createEntitiesWithBehaviors(count, worldSize, {"Synthetic"});
        setupWorld(worldSize);
        double multiMs = runBenchmark(iterations);
        cleanup();

        double speedup = (multiMs > 0) ? singleMs / multiMs : 0.0;

        std::cout << std::setw(10) << count
                  << std::setw(14) << std::fixed << std::setprecision(2) << singleMs
                  << std::setw(14) << std::fixed << std::setprecision(2) << multiMs
                  << std::setw(9) << std::fixed << std::setprecision(2) << speedup << "x\n";
    }

    AIManager::Instance().setThreadingThreshold(originalThreshold);
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
        setupWorld(WORLD_SIZE);  // Pass spawn worldSize directly

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
// Hill-Climb Convergence Test
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(HillClimbConvergence)
{
    std::cout << "--- WorkerBudget Hill-Climb Convergence (AI) ---\n";
    std::cout << "Testing that throughput improves as hill-climb converges\n\n";

    constexpr size_t ENTITY_COUNT = 5000;  // Above threading threshold
    constexpr float WORLD_SIZE = 7000.0f;
    constexpr int MEASURE_INTERVAL = 100;  // Measure every N frames
    constexpr int TOTAL_FRAMES = 500;      // Total frames for full convergence

    prepareForTest();
    createEntities(ENTITY_COUNT, WORLD_SIZE);
    setupWorld(WORLD_SIZE);  // Pass spawn worldSize directly

    // Verify ALL entities are in Active tier
    size_t activeCount = verifyActiveTier();
    if (activeCount != ENTITY_COUNT) {
        std::cout << "WARNING: Only " << activeCount << "/" << ENTITY_COUNT
                  << " entities in Active tier!\n";
    }

    auto& aim = AIManager::Instance();

    std::cout << std::setw(10) << "Frames"
              << std::setw(14) << "Avg Time (ms)"
              << std::setw(18) << "Throughput (/ms)"
              << std::setw(12) << "Status\n";

    double firstThroughput = 0.0;
    double lastThroughput = 0.0;

    for (int interval = 0; interval < TOTAL_FRAMES / MEASURE_INTERVAL; ++interval) {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < MEASURE_INTERVAL; ++i) {
            aim.update(0.016f);
        }
        aim.waitForAsyncBatchCompletion();

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgMs = totalMs / MEASURE_INTERVAL;
        double throughput = ENTITY_COUNT / avgMs;  // entities per ms

        if (interval == 0) {
            firstThroughput = throughput;
        }
        lastThroughput = throughput;

        int frameCount = (interval + 1) * MEASURE_INTERVAL;
        const char* status = (interval < 2) ? "Converging" : "Stable";

        std::cout << std::setw(10) << frameCount
                  << std::setw(14) << std::fixed << std::setprecision(3) << avgMs
                  << std::setw(18) << std::fixed << std::setprecision(0) << throughput
                  << std::setw(12) << status << "\n";
    }

    // Verify improvement
    double improvement = (lastThroughput - firstThroughput) / firstThroughput * 100.0;
    std::cout << "\nHILL-CLIMB RESULT:\n";
    std::cout << "  Initial throughput: " << std::fixed << std::setprecision(0) << firstThroughput << " entities/ms\n";
    std::cout << "  Final throughput:   " << std::fixed << std::setprecision(0) << lastThroughput << " entities/ms\n";
    std::cout << "  Improvement: " << std::fixed << std::setprecision(1) << improvement << "%\n";

    if (improvement >= 0.0) {
        std::cout << "  Status: PASS (throughput stable or improved)\n";
    } else if (improvement > -5.0) {
        std::cout << "  Status: PASS (within noise tolerance)\n";
    } else {
        std::cout << "  Status: WARNING (throughput degraded significantly)\n";
    }

    cleanup();
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
    std::cout << "  Hill-climb convergence: ~100 frames for optimal batch sizing\n";
    std::cout << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
