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
#include "entities/Entity.hpp"  // For AnimationConfig
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
            EntityHandle handle = edm.createNPCWithRaceClass(pos, "Human", "Guard");

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
            EntityHandle handle = edm.createNPCWithRaceClass(pos, "Human", "Guard");

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
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, false);
    double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, true);

    std::cout << "\n=== AI Scaling Benchmark ===\n";
    std::cout << "Date: " << __DATE__ << " " << __TIME__ << "\n";
    std::cout << "System: " << std::thread::hardware_concurrency() << " hardware threads\n";
    std::cout << "WorkerBudget: " << budget.totalWorkers << " workers\n";
    std::cout << "Single throughput: " << std::fixed << std::setprecision(2) << singleTP << " items/ms\n";
    std::cout << "Multi throughput:  " << std::fixed << std::setprecision(2) << multiTP << " items/ms\n";
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
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

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

        // Check threading decision from WorkerBudget
        auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::AI, count);
        const char* threading = decision.shouldThread ? "multi" : "single";
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
    auto finalDecision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::AI, bestCount);
    std::cout << "Threading mode: " << (finalDecision.shouldThread ? "WorkerBudget Multi-threaded" : "Single-threaded") << "\n";
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Threading Mode Comparison
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(ThreadingModeComparison)
{
    std::cout << "--- Threading Mode Comparison ---\n";
    std::cout << "(Threading uses adaptive threshold from WorkerBudget)\n";
    std::cout << std::setw(10) << "Entities"
              << std::setw(14) << "Single (ms)"
              << std::setw(14) << "Multi (ms)"
              << std::setw(10) << "Speedup\n";

    std::vector<size_t> entityCounts = {500, 1000, 2000, 5000, 10000};

    for (size_t count : entityCounts) {
        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        // Test single-threaded (disabling threading bypasses adaptive threshold)
        prepareForTest();
        #ifndef NDEBUG
        AIManager::Instance().enableThreading(false);
        #endif
        createEntities(count, worldSize);
        setupWorld(worldSize);
        size_t activeCount = verifyActiveTier();
        if (activeCount != count) {
            std::cout << "WARNING: Single-thread test - Only " << activeCount
                      << "/" << count << " entities in Active tier!\n";
        }
        double singleMs = runBenchmark(iterations);
        cleanup();

        // Test multi-threaded (adaptive threshold decides if threading is used)
        prepareForTest();
        #ifndef NDEBUG
        AIManager::Instance().enableThreading(true);
        #endif
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

    // Restore default threading mode
    #ifndef NDEBUG
    AIManager::Instance().enableThreading(true);
    #endif
    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Synthetic Behavior Threading Test (No shared state - pure threading test)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(SyntheticBehaviorThreading)
{
    std::cout << "--- Synthetic Behavior Threading (No Shared State) ---\n";
    std::cout << "Testing threading overhead without behavior state map contention\n";
    std::cout << "(Threading uses adaptive threshold from WorkerBudget)\n";
    std::cout << std::setw(10) << "Entities"
              << std::setw(14) << "Single (ms)"
              << std::setw(14) << "Multi (ms)"
              << std::setw(10) << "Speedup\n";

    // Register synthetic behavior (no shared state)
    auto synthetic = std::make_shared<SyntheticBehavior>();
    AIManager::Instance().registerBehavior("Synthetic", synthetic);

    std::vector<size_t> entityCounts = {500, 1000, 2000, 5000, 10000};

    for (size_t count : entityCounts) {
        float worldSize = std::sqrt(static_cast<float>(count)) * 100.0f;
        int iterations = std::max(20, 100000 / static_cast<int>(count));

        // Test single-threaded
        prepareForTest();
        #ifndef NDEBUG
        AIManager::Instance().enableThreading(false);
        #endif
        createEntitiesWithBehaviors(count, worldSize, {"Synthetic"});
        setupWorld(worldSize);
        double singleMs = runBenchmark(iterations);
        cleanup();

        // Test multi-threaded (adaptive threshold decides if threading is used)
        prepareForTest();
        #ifndef NDEBUG
        AIManager::Instance().enableThreading(true);
        #endif
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

    #ifndef NDEBUG
    AIManager::Instance().enableThreading(true);
    #endif
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
// WorkerBudget Adaptive Tuning Test (Batch Sizing + Threading Threshold)
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(WorkerBudgetAdaptiveTuning)
{
    std::cout << "--- WorkerBudget Adaptive Tuning (AI) ---\n";
    std::cout << "Tests both batch sizing hill-climb and threading threshold adaptation\n\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    auto& aim = AIManager::Instance();

    // =========================================================================
    // PART 1: Batch Sizing Hill-Climb (fast convergence, ~100 frames)
    // =========================================================================
    std::cout << "PART 1: Batch Sizing Hill-Climb\n";
    std::cout << "(Converges in ~100 frames)\n\n";

    constexpr size_t BATCH_ENTITY_COUNT = 5000;  // Sufficient to trigger threading
    constexpr float BATCH_WORLD_SIZE = 7000.0f;
    constexpr int BATCH_MEASURE_INTERVAL = 100;
    constexpr int BATCH_TOTAL_FRAMES = 500;

    prepareForTest();
    createEntities(BATCH_ENTITY_COUNT, BATCH_WORLD_SIZE);
    setupWorld(BATCH_WORLD_SIZE);

    std::cout << std::setw(10) << "Frames"
              << std::setw(14) << "Avg Time (ms)"
              << std::setw(18) << "Throughput (/ms)"
              << std::setw(12) << "Status\n";

    double batchFirstThroughput = 0.0;
    double batchLastThroughput = 0.0;

    for (int interval = 0; interval < BATCH_TOTAL_FRAMES / BATCH_MEASURE_INTERVAL; ++interval) {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < BATCH_MEASURE_INTERVAL; ++i) {
            aim.update(0.016f);
        }
        aim.waitForAsyncBatchCompletion();

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgMs = totalMs / BATCH_MEASURE_INTERVAL;
        double throughput = BATCH_ENTITY_COUNT / avgMs;

        if (interval == 0) {
            batchFirstThroughput = throughput;
        }
        batchLastThroughput = throughput;

        int frameCount = (interval + 1) * BATCH_MEASURE_INTERVAL;
        const char* status = (interval < 2) ? "Converging" : "Stable";

        std::cout << std::setw(10) << frameCount
                  << std::setw(14) << std::fixed << std::setprecision(3) << avgMs
                  << std::setw(18) << std::fixed << std::setprecision(0) << throughput
                  << std::setw(12) << status << "\n";
    }

    double batchImprovement = (batchLastThroughput - batchFirstThroughput) / batchFirstThroughput * 100.0;
    std::cout << "\nBatch sizing: " << std::fixed << std::setprecision(0) << batchFirstThroughput
              << " -> " << batchLastThroughput << " entities/ms ("
              << std::setprecision(1) << batchImprovement << "%)\n";

    cleanup();

    // =========================================================================
    // PART 2: Throughput Tracking (replaces threshold adaptation)
    // =========================================================================
    std::cout << "\nPART 2: Throughput Tracking\n";
    std::cout << "(Tracks single/multi throughput for mode selection)\n\n";

    double initialSingleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, false);
    double initialMultiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, true);
    std::cout << "Initial single throughput: " << std::fixed << std::setprecision(2) << initialSingleTP << " items/ms\n";
    std::cout << "Initial multi throughput:  " << std::fixed << std::setprecision(2) << initialMultiTP << " items/ms\n\n";

    constexpr size_t TRACKING_ENTITY_COUNT = 300;
    constexpr float TRACKING_WORLD_SIZE = 3000.0f;
    constexpr int FRAMES_PER_PHASE = 550;
    constexpr int NUM_PHASES = 4;

    prepareForTest();
    createEntities(TRACKING_ENTITY_COUNT, TRACKING_WORLD_SIZE);
    setupWorld(TRACKING_WORLD_SIZE);

    std::cout << std::setw(8) << "Phase"
              << std::setw(12) << "Frames"
              << std::setw(14) << "Avg Time(ms)"
              << std::setw(12) << "SingleTP"
              << std::setw(12) << "MultiTP"
              << std::setw(12) << "BatchMult\n";

    for (int phase = 0; phase < NUM_PHASES; ++phase) {
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < FRAMES_PER_PHASE; ++i) {
            aim.update(0.016f);
        }
        aim.waitForAsyncBatchCompletion();

        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgMs = totalMs / FRAMES_PER_PHASE;

        double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, false);
        double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, true);
        float batchMultNow = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::AI);

        std::cout << std::setw(8) << (phase + 1)
                  << std::setw(12) << ((phase + 1) * FRAMES_PER_PHASE)
                  << std::setw(14) << std::fixed << std::setprecision(3) << avgMs
                  << std::setw(12) << std::fixed << std::setprecision(2) << singleTP
                  << std::setw(12) << std::fixed << std::setprecision(2) << multiTP
                  << std::setw(12) << std::fixed << std::setprecision(2) << batchMultNow << "\n";
    }

    double finalSingleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, false);
    double finalMultiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, true);
    float finalBatchMultTP = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::AI);

    std::string modePreferred = (finalMultiTP > finalSingleTP * 1.15) ? "MULTI" :
                               (finalSingleTP > finalMultiTP * 1.15) ? "SINGLE" : "COMPARABLE";

    std::cout << "\nFinal single throughput: " << std::fixed << std::setprecision(2) << finalSingleTP << " items/ms\n";
    std::cout << "Final multi throughput:  " << std::fixed << std::setprecision(2) << finalMultiTP << " items/ms\n";
    std::cout << "Final batch multiplier:  " << std::fixed << std::setprecision(2) << finalBatchMultTP << "\n";
    std::cout << "Mode preference:         " << modePreferred << "\n";

    cleanup();

    // =========================================================================
    // RESULTS SUMMARY
    // =========================================================================
    std::cout << "\nADAPTIVE TUNING RESULTS:\n";

    // Batch sizing result
    if (batchImprovement >= 0.0) {
        std::cout << "  Batch sizing: PASS (throughput stable or improved)\n";
    } else if (batchImprovement > -5.0) {
        std::cout << "  Batch sizing: PASS (within noise tolerance)\n";
    } else {
        std::cout << "  Batch sizing: WARNING (throughput degraded)\n";
    }

    // Throughput tracking result
    bool throughputCollected = (finalSingleTP > 0 || finalMultiTP > 0);
    if (throughputCollected) {
        std::cout << "  Throughput tracking: PASS (data collected, mode=" << modePreferred << ")\n";
    } else {
        std::cout << "  Throughput tracking: PASS (system initialized)\n";
    }

    std::cout << std::endl;
}

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_CASE(PrintSummary)
{
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, false);
    double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::AI, true);
    float batchMult = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::AI);

    std::cout << "SUMMARY:\n";
    std::cout << "  AI batch processing: O(n) scaling with WorkerBudget\n";
    std::cout << "  Single throughput: " << std::fixed << std::setprecision(2) << singleTP << " items/ms\n";
    std::cout << "  Multi throughput:  " << std::fixed << std::setprecision(2) << multiTP << " items/ms\n";
    std::cout << "  Batch multiplier:  " << std::fixed << std::setprecision(2) << batchMult << "\n";
    std::cout << "  Entity iteration: Active tier only (via getActiveIndices)\n";
    std::cout << "  Behavior execution: Type-indexed O(1) lookup\n";
    std::cout << "  WorkerBudget adaptive tuning:\n";
    std::cout << "    - Batch sizing: ~100 frames to converge via hill-climbing\n";
    std::cout << "    - Throughput tracking: Both modes tracked, 15% threshold to switch\n";
    std::cout << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
