/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * Adaptive Threading Analysis
 *
 * Concrete test to measure single vs multi-threaded throughput at various
 * entity counts to determine optimal threading thresholds per system.
 *
 * Output: Raw data showing crossover points where threading becomes beneficial.
 */

#define BOOST_TEST_MODULE AdaptiveThreadingAnalysis
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <random>

#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "core/Logger.hpp"

// Production AI behaviors (same as AIDemoState)
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"

namespace {

class AnalysisFixture {
public:
    AnalysisFixture() {
        if (!s_initialized) {
            HAMMER_ENABLE_BENCHMARK_MODE();
            HammerEngine::ThreadSystem::Instance().init();
            EntityDataManager::Instance().init();
            PathfinderManager::Instance().init();
            PathfinderManager::Instance().rebuildGrid();
            CollisionManager::Instance().init();
            AIManager::Instance().init();
            BackgroundSimulationManager::Instance().init();
            BackgroundSimulationManager::Instance().setActiveRadius(50000.0f);
            BackgroundSimulationManager::Instance().setBackgroundRadius(100000.0f);

            // Register production behaviors
            registerBehaviors();

            s_initialized = true;
        }
        m_rng.seed(42);
    }

    void reset() {
        AIManager::Instance().prepareForStateTransition();
        EntityDataManager::Instance().prepareForStateTransition();
        CollisionManager::Instance().prepareForStateTransition();
    }

    void registerBehaviors() {
        // Register same behaviors as AIDemoState for realistic workloads
        auto& aim = AIManager::Instance();

        // Wander variants (different areas, speeds)
        aim.registerBehavior("Wander", std::make_shared<WanderBehavior>(
            WanderBehavior::WanderMode::MEDIUM_AREA, 60.0f));
        aim.registerBehavior("SmallWander", std::make_shared<WanderBehavior>(
            WanderBehavior::WanderMode::SMALL_AREA, 45.0f));
        aim.registerBehavior("LargeWander", std::make_shared<WanderBehavior>(
            WanderBehavior::WanderMode::LARGE_AREA, 75.0f));

        // Patrol variants (different modes)
        aim.registerBehavior("Patrol", std::make_shared<PatrolBehavior>(
            PatrolBehavior::PatrolMode::RANDOM_AREA, 75.0f, false));
        aim.registerBehavior("RandomPatrol", std::make_shared<PatrolBehavior>(
            PatrolBehavior::PatrolMode::RANDOM_AREA, 85.0f, false));
        aim.registerBehavior("CirclePatrol", std::make_shared<PatrolBehavior>(
            PatrolBehavior::PatrolMode::CIRCULAR_AREA, 90.0f, false));

        // Guard behavior
        aim.registerBehavior("Guard", std::make_shared<GuardBehavior>(
            Vector2D(5000.0f, 5000.0f), 500.0f));

        // Chase behavior (will chase first entity as target)
        aim.registerBehavior("Chase", std::make_shared<ChaseBehavior>(100.0f, 500.0f, 30.0f));
    }

    void createEntities(size_t count, float worldSize = 10000.0f) {
        auto& edm = EntityDataManager::Instance();
        auto& aim = AIManager::Instance();
        std::uniform_real_distribution<float> posDist(100.0f, worldSize - 100.0f);

        // Realistic mix of behaviors like AIDemoState
        static const std::vector<std::string> behaviors = {
            "Wander", "SmallWander", "LargeWander",
            "Patrol", "RandomPatrol", "CirclePatrol",
            "Guard", "Chase"
        };

        EntityHandle firstHandle;
        for (size_t i = 0; i < count; ++i) {
            Vector2D pos(posDist(m_rng), posDist(m_rng));
            EntityHandle handle = edm.createDataDrivenNPC(pos, "Guard");
            if (handle.isValid()) {
                if (i == 0) firstHandle = handle;
                // Assign random behavior from the mix
                const std::string& behaviorName = behaviors[i % behaviors.size()];
                aim.assignBehavior(handle, behaviorName);
            }
        }

        // Set first entity as player for Chase behaviors
        if (firstHandle.isValid()) {
            aim.setPlayerHandle(firstHandle);
        }
    }

    struct Measurement {
        size_t entityCount;
        double singleThreadedMs;
        double multiThreadedMs;
        double singleThroughput;  // items/ms
        double multiThroughput;   // items/ms
        double speedup;           // multi/single throughput ratio
    };

    Measurement measureAI(size_t entityCount, int iterations = 30) {
        reset();
        createEntities(entityCount);

        auto& aim = AIManager::Instance();
        Measurement m{};
        m.entityCount = entityCount;

        // Extended warmup with threading enabled to warm up thread pool
        #ifndef NDEBUG
        aim.enableThreading(true);
        #endif
        for (int i = 0; i < 50; ++i) aim.update(0.016f);

        // Measure MULTI-THREADED FIRST (thread pool is warm)
        double multiTotal = 0;
        for (int i = 0; i < iterations; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            aim.update(0.016f);
            auto t1 = std::chrono::high_resolution_clock::now();
            multiTotal += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        m.multiThreadedMs = multiTotal / iterations;

        // Switch to single-threaded and warmup
        #ifndef NDEBUG
        aim.enableThreading(false);
        #endif
        for (int i = 0; i < 20; ++i) aim.update(0.016f);

        // Measure single-threaded
        double singleTotal = 0;
        for (int i = 0; i < iterations; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            aim.update(0.016f);
            auto t1 = std::chrono::high_resolution_clock::now();
            singleTotal += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        m.singleThreadedMs = singleTotal / iterations;

        // Restore threading
        #ifndef NDEBUG
        aim.enableThreading(true);
        #endif

        m.singleThroughput = (m.singleThreadedMs > 0) ? entityCount / m.singleThreadedMs : 0;
        m.multiThroughput = (m.multiThreadedMs > 0) ? entityCount / m.multiThreadedMs : 0;
        m.speedup = (m.singleThroughput > 0) ? m.multiThroughput / m.singleThroughput : 0;

        return m;
    }

    Measurement measureCollision(size_t entityCount, int iterations = 30) {
        reset();
        createEntities(entityCount);

        auto& colMgr = CollisionManager::Instance();
        Measurement m{};
        m.entityCount = entityCount;

        // Extended warmup with threading enabled
        #ifndef NDEBUG
        colMgr.enableThreading(true);
        #endif
        for (int i = 0; i < 50; ++i) colMgr.update(0.016f);

        // Measure MULTI-THREADED FIRST (thread pool is warm)
        double multiTotal = 0;
        for (int i = 0; i < iterations; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            colMgr.update(0.016f);
            auto t1 = std::chrono::high_resolution_clock::now();
            multiTotal += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        m.multiThreadedMs = multiTotal / iterations;

        // Switch to single-threaded and warmup
        #ifndef NDEBUG
        colMgr.enableThreading(false);
        #endif
        for (int i = 0; i < 20; ++i) colMgr.update(0.016f);

        // Measure single-threaded
        double singleTotal = 0;
        for (int i = 0; i < iterations; ++i) {
            auto t0 = std::chrono::high_resolution_clock::now();
            colMgr.update(0.016f);
            auto t1 = std::chrono::high_resolution_clock::now();
            singleTotal += std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        m.singleThreadedMs = singleTotal / iterations;

        // Restore threading
        #ifndef NDEBUG
        colMgr.enableThreading(true);
        #endif

        m.singleThroughput = (m.singleThreadedMs > 0) ? entityCount / m.singleThreadedMs : 0;
        m.multiThroughput = (m.multiThreadedMs > 0) ? entityCount / m.multiThreadedMs : 0;
        m.speedup = (m.singleThroughput > 0) ? m.multiThroughput / m.singleThroughput : 0;

        return m;
    }

protected:
    std::mt19937 m_rng;
    static bool s_initialized;
};

bool AnalysisFixture::s_initialized = false;

void printHeader() {
    std::cout << std::setw(10) << "Count"
              << std::setw(14) << "Single(ms)"
              << std::setw(14) << "Multi(ms)"
              << std::setw(14) << "Single/ms"
              << std::setw(14) << "Multi/ms"
              << std::setw(10) << "Speedup"
              << std::setw(12) << "Winner" << std::endl;
    std::cout << std::string(88, '-') << std::endl;
}

void printMeasurement(const AnalysisFixture::Measurement& m) {
    std::string winner = (m.speedup > 1.1) ? "MULTI" :
                         (m.speedup < 0.9) ? "SINGLE" : "~EQUAL";
    std::cout << std::setw(10) << m.entityCount
              << std::setw(14) << std::fixed << std::setprecision(3) << m.singleThreadedMs
              << std::setw(14) << std::fixed << std::setprecision(3) << m.multiThreadedMs
              << std::setw(14) << std::fixed << std::setprecision(0) << m.singleThroughput
              << std::setw(14) << std::fixed << std::setprecision(0) << m.multiThroughput
              << std::setw(9) << std::fixed << std::setprecision(2) << m.speedup << "x"
              << std::setw(12) << winner << std::endl;
}

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(ThreadingAnalysis, AnalysisFixture)

// Find exact crossover point for AI
BOOST_AUTO_TEST_CASE(AI_ThreadingCrossover) {
    std::cout << "\n===== AI THREADING CROSSOVER ANALYSIS =====\n" << std::endl;
    std::cout << "Finding where multi-threading becomes beneficial for AI\n" << std::endl;

    std::vector<size_t> counts = {100, 250, 500, 1000, 2000, 3000, 5000, 7500, 10000};

    printHeader();

    size_t crossoverPoint = 0;
    for (size_t count : counts) {
        auto m = measureAI(count);
        printMeasurement(m);

        if (crossoverPoint == 0 && m.speedup > 1.1) {
            crossoverPoint = count;
        }
    }

    std::cout << "\n=== AI RESULT ===" << std::endl;
    std::cout << "Crossover point (speedup > 1.1x): " << crossoverPoint << " entities" << std::endl;
    std::cout << "Current adaptive threshold: "
              << HammerEngine::WorkerBudgetManager::Instance().getThreadingThreshold(HammerEngine::SystemType::AI)
              << " entities" << std::endl;
    std::cout << "================\n" << std::endl;
}

// Find exact crossover point for Collision
BOOST_AUTO_TEST_CASE(Collision_ThreadingCrossover) {
    std::cout << "\n===== COLLISION THREADING CROSSOVER ANALYSIS =====\n" << std::endl;
    std::cout << "Finding where multi-threading becomes beneficial for Collision\n" << std::endl;

    std::vector<size_t> counts = {100, 250, 500, 1000, 2000, 3000, 5000};

    printHeader();

    size_t crossoverPoint = 0;
    for (size_t count : counts) {
        auto m = measureCollision(count);
        printMeasurement(m);

        if (crossoverPoint == 0 && m.speedup > 1.1) {
            crossoverPoint = count;
        }
    }

    std::cout << "\n=== COLLISION RESULT ===" << std::endl;
    std::cout << "Crossover point (speedup > 1.1x): " << crossoverPoint << " entities" << std::endl;
    std::cout << "Current adaptive threshold: "
              << HammerEngine::WorkerBudgetManager::Instance().getThreadingThreshold(HammerEngine::SystemType::Collision)
              << " entities" << std::endl;
    std::cout << "========================\n" << std::endl;
}

// Test if adaptive system learns correctly over time
BOOST_AUTO_TEST_CASE(AI_AdaptiveLearning) {
    std::cout << "\n===== AI ADAPTIVE LEARNING TEST =====\n" << std::endl;
    std::cout << "Running 3000 frames with 5000 entities to see if threshold adapts\n" << std::endl;

    reset();
    createEntities(5000);

    auto& aim = AIManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    size_t initialThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::AI);
    std::cout << "Initial threshold: " << initialThreshold << std::endl;

    std::cout << "\n   Frame  Threshold  Avg Time(ms)  Throughput" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    double totalTime = 0;
    int sampleCount = 0;

    for (int frame = 0; frame < 3000; ++frame) {
        auto t0 = std::chrono::high_resolution_clock::now();
        aim.update(0.016f);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        totalTime += ms;
        sampleCount++;

        // Report every 500 frames (when probes happen)
        if ((frame + 1) % 500 == 0) {
            size_t currentThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::AI);
            double avgMs = totalTime / sampleCount;
            double throughput = 1000.0 / avgMs;

            std::cout << std::setw(8) << (frame + 1)
                      << std::setw(10) << currentThreshold
                      << std::setw(14) << std::fixed << std::setprecision(3) << avgMs
                      << std::setw(12) << std::fixed << std::setprecision(0) << throughput
                      << std::endl;

            totalTime = 0;
            sampleCount = 0;
        }
    }

    size_t finalThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::AI);
    std::cout << "\nFinal threshold: " << finalThreshold << std::endl;

    if (finalThreshold > initialThreshold) {
        std::cout << "Result: Threshold INCREASED (learned threading hurts)" << std::endl;
    } else if (finalThreshold < initialThreshold) {
        std::cout << "Result: Threshold DECREASED (learned threading helps)" << std::endl;
    } else {
        std::cout << "Result: Threshold UNCHANGED (no learning occurred)" << std::endl;
    }
    std::cout << "=====================================\n" << std::endl;
}

// Test if collision adaptive system learns correctly
BOOST_AUTO_TEST_CASE(Collision_AdaptiveLearning) {
    std::cout << "\n===== COLLISION ADAPTIVE LEARNING TEST =====\n" << std::endl;
    std::cout << "Running 3000 frames with 300 entities (where single-threaded wins)\n" << std::endl;

    reset();
    createEntities(300);

    auto& colMgr = CollisionManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    size_t initialThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Collision);
    std::cout << "Initial threshold: " << initialThreshold << " entities" << std::endl;
    std::cout << "Entity count: 300 (above threshold 150, but single-threaded is faster here)" << std::endl;
    std::cout << "Expected: Threshold should INCREASE since single-threaded wins\n" << std::endl;

    std::cout << "   Frame  Threshold  Avg Time(ms)  Throughput  Learning" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    double totalTime = 0;
    int sampleCount = 0;
    size_t lastThreshold = initialThreshold;

    for (int frame = 0; frame < 3000; ++frame) {
        auto t0 = std::chrono::high_resolution_clock::now();
        colMgr.update(0.016f);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        totalTime += ms;
        sampleCount++;

        if ((frame + 1) % 500 == 0) {
            size_t currentThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Collision);
            double avgMs = totalTime / sampleCount;
            double throughput = 300.0 / avgMs;

            std::string learning = "---";
            if (currentThreshold > lastThreshold) {
                learning = "UP (single better)";
            } else if (currentThreshold < lastThreshold) {
                learning = "DOWN (multi better)";
            }
            lastThreshold = currentThreshold;

            std::cout << std::setw(8) << (frame + 1)
                      << std::setw(10) << currentThreshold
                      << std::setw(14) << std::fixed << std::setprecision(3) << avgMs
                      << std::setw(12) << std::fixed << std::setprecision(0) << throughput
                      << "  " << learning << std::endl;

            totalTime = 0;
            sampleCount = 0;
        }
    }

    size_t finalThreshold = budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Collision);
    std::cout << "\nFinal threshold: " << finalThreshold << std::endl;

    if (finalThreshold > initialThreshold) {
        std::cout << "Result: Threshold INCREASED (learned single-threaded is faster)" << std::endl;
    } else if (finalThreshold < initialThreshold) {
        std::cout << "Result: Threshold DECREASED (learned multi-threaded is faster)" << std::endl;
        std::cout << "Status: ALGORITHM WORKING - correctly identified threading benefit" << std::endl;
    } else {
        std::cout << "Result: Threshold UNCHANGED (no clear 30%+ winner)" << std::endl;
        std::cout << "Status: STABLE - system held steady without clear evidence" << std::endl;
    }
    std::cout << "============================================\n" << std::endl;
}

// Summary comparison
BOOST_AUTO_TEST_CASE(Summary) {
    std::cout << "\n===== ADAPTIVE THRESHOLD SUMMARY =====\n" << std::endl;

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    std::cout << "Current Adaptive Thresholds:" << std::endl;
    std::cout << "  AI:        " << budgetMgr.getThreadingThreshold(HammerEngine::SystemType::AI) << std::endl;
    std::cout << "  Collision: " << budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Collision) << std::endl;
    std::cout << "  Particle:  " << budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Particle) << std::endl;
    std::cout << "  Event:     " << budgetMgr.getThreadingThreshold(HammerEngine::SystemType::Event) << std::endl;

    std::cout << "\nCompare crossover points above to these thresholds." << std::endl;
    std::cout << "If crossover > threshold: threshold too low (threading used when it shouldn't be)" << std::endl;
    std::cout << "If crossover < threshold: threshold too high (missing threading benefit)" << std::endl;
    std::cout << "======================================\n" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
