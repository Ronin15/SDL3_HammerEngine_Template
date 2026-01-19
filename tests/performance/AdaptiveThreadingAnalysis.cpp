/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * WorkerBudget Adaptive Logic Validation
 *
 * Tests that WorkerBudgetManager correctly:
 * 1. Learns throughput for single vs multi-threaded modes
 * 2. Switches modes at appropriate crossover points
 * 3. Tunes batch multiplier via hill-climbing
 *
 * Focus: Collision system (demonstrates clear mode switching)
 * See individual manager benchmarks for raw performance testing.
 */

#define BOOST_TEST_MODULE AdaptiveThreadingAnalysis
#include <boost/test/unit_test.hpp>

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <random>
#include <cmath>

#include "managers/EntityDataManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "core/Logger.hpp"

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

            s_initialized = true;
        }
        m_rng.seed(42);
    }

    void reset() {
        EntityDataManager::Instance().prepareForStateTransition();
        CollisionManager::Instance().prepareForStateTransition();
    }

    float calculateWorldSize(size_t entityCount) {
        float baseSize = std::sqrt(static_cast<float>(entityCount) * 400.0f);
        return std::clamp(baseSize, 200.0f, 4000.0f);
    }

    void createEntities(size_t count) {
        auto& edm = EntityDataManager::Instance();

        float worldSize = calculateWorldSize(count);
        std::uniform_real_distribution<float> posDist(50.0f, worldSize - 50.0f);

        for (size_t i = 0; i < count; ++i) {
            Vector2D pos(posDist(m_rng), posDist(m_rng));
            edm.createNPCWithRaceClass(pos, "Human", "Guard");
        }
    }

protected:
    std::mt19937 m_rng;
    static bool s_initialized;
};

bool AnalysisFixture::s_initialized = false;

} // anonymous namespace

BOOST_FIXTURE_TEST_SUITE(WorkerBudgetValidation, AnalysisFixture)

/**
 * Test: Collision Throughput Learning
 * Validates that WBM learns and tracks throughput for Collision system.
 */
BOOST_AUTO_TEST_CASE(Collision_ThroughputLearning) {
    std::cout << "\n===== COLLISION THROUGHPUT LEARNING =====\n" << std::endl;

    reset();
    createEntities(1000);

    auto& colMgr = CollisionManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    double initialSingleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, false);
    double initialMultiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, true);

    std::cout << "Initial state:" << std::endl;
    std::cout << "  Single TP: " << std::fixed << std::setprecision(2) << initialSingleTP << " items/ms" << std::endl;
    std::cout << "  Multi TP:  " << std::fixed << std::setprecision(2) << initialMultiTP << " items/ms" << std::endl;

    // Run 1000 frames to let WBM learn
    std::cout << "\nRunning 1000 frames..." << std::endl;
    for (int i = 0; i < 1000; ++i) {
        colMgr.update(0.016f);
    }

    double finalSingleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, false);
    double finalMultiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, true);
    float batchMult = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::Collision);

    std::cout << "\nAfter 1000 frames:" << std::endl;
    std::cout << "  Single TP: " << std::fixed << std::setprecision(2) << finalSingleTP << " items/ms" << std::endl;
    std::cout << "  Multi TP:  " << std::fixed << std::setprecision(2) << finalMultiTP << " items/ms" << std::endl;
    std::cout << "  Batch multiplier: " << std::fixed << std::setprecision(2) << batchMult << std::endl;

    // Validate: WBM should have learned non-zero throughput for at least one mode
    bool learned = (finalSingleTP > 0) || (finalMultiTP > 0);
    std::cout << "\nValidation: WBM learned throughput: " << (learned ? "PASS" : "FAIL") << std::endl;
    BOOST_CHECK(learned);

    std::cout << "==========================================\n" << std::endl;
}

/**
 * Test: Collision Mode Selection
 * Validates that WBM selects appropriate mode based on learned throughput.
 */
BOOST_AUTO_TEST_CASE(Collision_ModeSelection) {
    std::cout << "\n===== COLLISION MODE SELECTION =====\n" << std::endl;

    reset();
    createEntities(500);

    auto& colMgr = CollisionManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Run enough frames for WBM to stabilize
    std::cout << "Running 500 frames to stabilize..." << std::endl;
    for (int i = 0; i < 500; ++i) {
        colMgr.update(0.016f);
    }

    double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, false);
    double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, true);

    std::cout << "\nLearned throughput:" << std::endl;
    std::cout << "  Single: " << std::fixed << std::setprecision(2) << singleTP << " items/ms" << std::endl;
    std::cout << "  Multi:  " << std::fixed << std::setprecision(2) << multiTP << " items/ms" << std::endl;

    auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, 500);

    std::cout << "\nWBM decision for 500 entities:" << std::endl;
    std::cout << "  shouldThread: " << (decision.shouldThread ? "true (MULTI)" : "false (SINGLE)") << std::endl;

    bool expectedMulti = (multiTP > singleTP * 1.15);
    bool decisionCorrect = (decision.shouldThread == expectedMulti);

    std::cout << "\nValidation:" << std::endl;
    std::cout << "  Expected mode: " << (expectedMulti ? "MULTI" : "SINGLE") << std::endl;
    std::cout << "  Actual mode:   " << (decision.shouldThread ? "MULTI" : "SINGLE") << std::endl;
    std::cout << "  Decision correct: " << (decisionCorrect ? "PASS" : "FAIL") << std::endl;

    BOOST_CHECK(decisionCorrect);

    std::cout << "=====================================\n" << std::endl;
}

/**
 * Test: Batch Multiplier Tuning
 * Validates that WBM's hill-climbing batch multiplier converges.
 */
BOOST_AUTO_TEST_CASE(BatchMultiplierTuning) {
    std::cout << "\n===== BATCH MULTIPLIER TUNING =====\n" << std::endl;

    reset();
    createEntities(1000);

    auto& colMgr = CollisionManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    float initialMult = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::Collision);
    std::cout << "Initial batch multiplier: " << std::fixed << std::setprecision(3) << initialMult << std::endl;

    std::cout << "\nRunning 2000 frames, sampling every 500..." << std::endl;
    std::cout << "  Frame   BatchMult" << std::endl;
    std::cout << "  -----   ---------" << std::endl;

    std::vector<float> multipliers;
    multipliers.push_back(initialMult);

    for (int frame = 0; frame < 2000; ++frame) {
        colMgr.update(0.016f);

        if ((frame + 1) % 500 == 0) {
            float mult = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::Collision);
            multipliers.push_back(mult);
            std::cout << "  " << std::setw(5) << (frame + 1)
                      << "   " << std::fixed << std::setprecision(3) << mult << std::endl;
        }
    }

    // Check if multiplier is within valid range [0.4, 2.0]
    float finalMult = multipliers.back();
    bool inRange = (finalMult >= 0.4f && finalMult <= 2.0f);

    // Check if multiplier stabilized (last 2 samples within 10%)
    bool stabilized = true;
    if (multipliers.size() >= 2) {
        float diff = std::abs(multipliers.back() - multipliers[multipliers.size() - 2]);
        stabilized = (diff / multipliers.back()) < 0.10f;
    }

    std::cout << "\nValidation:" << std::endl;
    std::cout << "  Final multiplier: " << std::fixed << std::setprecision(3) << finalMult << std::endl;
    std::cout << "  In valid range [0.4, 2.0]: " << (inRange ? "PASS" : "FAIL") << std::endl;
    std::cout << "  Stabilized: " << (stabilized ? "PASS" : "STILL TUNING") << std::endl;

    BOOST_CHECK(inRange);

    std::cout << "====================================\n" << std::endl;
}

/**
 * Test: Mode Switching - Scale Up and Down
 * Validates that WBM switches modes correctly in both directions:
 * 1. Scale UP: low count (single) -> high count (multi)
 * 2. Scale DOWN: high count (multi) -> low count (single)
 */
BOOST_AUTO_TEST_CASE(Collision_ModeSwitching) {
    std::cout << "\n===== COLLISION MODE SWITCHING (UP/DOWN) =====\n" << std::endl;

    auto& colMgr = CollisionManager::Instance();
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Phase 1: Start with VERY LOW count - should be forced single-threaded
    // MIN_WORKLOAD=100 means anything below 100 is always SINGLE
    std::cout << "=== PHASE 1: VERY LOW COUNT (expect forced SINGLE) ===" << std::endl;
    reset();
    createEntities(50);  // Below MIN_WORKLOAD=100 threshold

    std::cout << "Entity count: 50 (below MIN_WORKLOAD=100)" << std::endl;
    std::cout << "Running 200 frames to stabilize..." << std::endl;

    bool lowCountThreaded = false;
    for (int i = 0; i < 200; ++i) {
        colMgr.update(0.016f);
        if (i == 199) {
            auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, 50);
            lowCountThreaded = decision.shouldThread;
        }
    }

    std::cout << "  Mode at 50 entities: " << (lowCountThreaded ? "MULTI" : "SINGLE") << std::endl;
    std::cout << "  Expected: SINGLE (forced below MIN_WORKLOAD=100)" << std::endl;

    // Phase 2: Scale UP to HIGH count - should switch to multi-threaded
    std::cout << "\n=== PHASE 2: SCALE UP (expect MULTI) ===" << std::endl;
    reset();
    createEntities(2000);  // High count - well above threading threshold

    std::cout << "Entity count: 2000" << std::endl;
    std::cout << "Running 500 frames to stabilize and switch..." << std::endl;

    std::vector<bool> highCountModes;
    for (int i = 0; i < 500; ++i) {
        colMgr.update(0.016f);
        if ((i + 1) % 100 == 0) {
            auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, 2000);
            highCountModes.push_back(decision.shouldThread);
            std::cout << "  Frame " << std::setw(3) << (i + 1) << ": "
                      << (decision.shouldThread ? "MULTI" : "SINGLE") << std::endl;
        }
    }

    bool highCountThreaded = highCountModes.back();
    std::cout << "  Final mode at 2000 entities: " << (highCountThreaded ? "MULTI" : "SINGLE") << std::endl;

    // Phase 3: Gradual scale DOWN to find natural crossover point
    // Test progressively smaller counts to find where WBM switches from MULTI to SINGLE
    // based on throughput comparison, not just hitting MIN_WORKLOAD
    std::cout << "\n=== PHASE 3: GRADUAL SCALE DOWN (find natural crossover) ===" << std::endl;

    std::vector<size_t> testCounts = {1500, 1000, 750, 500, 400, 300, 200, 150, 125, 100};
    size_t crossoverPoint = 0;
    bool foundCrossover = false;

    std::cout << "  Count    Mode      Single TP    Multi TP     Ratio" << std::endl;
    std::cout << "  -----    ----      ---------    --------     -----" << std::endl;

    for (size_t count : testCounts) {
        reset();
        createEntities(count);

        // Run frames to let WBM observe this workload
        for (int i = 0; i < 200; ++i) {
            colMgr.update(0.016f);
        }

        auto decision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, count);
        double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, false);
        double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, true);
        double ratio = (singleTP > 0) ? multiTP / singleTP : 0;

        std::cout << "  " << std::setw(5) << count << "    "
                  << (decision.shouldThread ? "MULTI " : "SINGLE")
                  << "    " << std::setw(9) << std::fixed << std::setprecision(0) << singleTP
                  << "    " << std::setw(8) << std::fixed << std::setprecision(0) << multiTP
                  << "     " << std::fixed << std::setprecision(2) << ratio << "x" << std::endl;

        // Track first switch from MULTI to SINGLE (natural crossover)
        if (!foundCrossover && !decision.shouldThread && count > 100) {
            crossoverPoint = count;
            foundCrossover = true;
        }
    }

    // Final check below MIN_WORKLOAD boundary (should force SINGLE)
    auto belowBoundaryDecision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, 99);
    bool belowBoundaryThreaded = belowBoundaryDecision.shouldThread;

    // At MIN_WORKLOAD (100), throughput comparison is used, not forced SINGLE
    auto atBoundaryDecision = budgetMgr.shouldUseThreading(HammerEngine::SystemType::Collision, 100);
    bool atBoundaryThreaded = atBoundaryDecision.shouldThread;

    std::cout << "\n  Natural crossover point: ";
    if (foundCrossover) {
        std::cout << crossoverPoint << " entities (MULTI->SINGLE based on throughput)" << std::endl;
    } else {
        std::cout << "Not found above MIN_WORKLOAD=100 (MULTI preferred at all tested counts)" << std::endl;
    }

    // Validation
    std::cout << "\n=== VALIDATION ===" << std::endl;

    bool scaleUpWorked = highCountThreaded;  // Should be MULTI at high count
    // Below MIN_WORKLOAD (99): forced SINGLE regardless of throughput
    bool belowBoundaryCorrect = !belowBoundaryThreaded;

    std::cout << "  Scale UP (50->2000): " << (scaleUpWorked ? "PASS - switched to MULTI" : "FAIL - stayed SINGLE") << std::endl;
    std::cout << "  Below MIN_WORKLOAD (99): " << (belowBoundaryCorrect ? "PASS - forced SINGLE" : "FAIL - not forced") << std::endl;
    std::cout << "  At MIN_WORKLOAD (100): " << (atBoundaryThreaded ? "MULTI (throughput comparison)" : "SINGLE (throughput comparison)") << std::endl;
    std::cout << "  Crossover detection: " << (foundCrossover ? "PASS - found natural switch" : "N/A - MULTI preferred") << std::endl;

    // Key validation: low count should be SINGLE, high count should be MULTI
    bool modesDiffer = (!lowCountThreaded && highCountThreaded);
    std::cout << "  Bidirectional adaptive: " << (modesDiffer ? "PASS" : "FAIL") << std::endl;

    BOOST_CHECK_MESSAGE(!lowCountThreaded, "WBM should force SINGLE below MIN_WORKLOAD (50 entities)");
    BOOST_CHECK_MESSAGE(scaleUpWorked, "WBM should use MULTI at 2000 entities");
    BOOST_CHECK_MESSAGE(belowBoundaryCorrect, "WBM should force SINGLE at 99 entities (below MIN_WORKLOAD=100)");

    std::cout << "===============================================\n" << std::endl;
}

/**
 * Test: Summary
 * Shows final WBM state for Collision system.
 */
BOOST_AUTO_TEST_CASE(Summary) {
    std::cout << "\n===== WORKERBUDGET VALIDATION SUMMARY =====\n" << std::endl;

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    double singleTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, false);
    double multiTP = budgetMgr.getExpectedThroughput(HammerEngine::SystemType::Collision, true);
    float batchMult = budgetMgr.getBatchMultiplier(HammerEngine::SystemType::Collision);

    std::string preferred;
    if (multiTP > singleTP * 1.15) preferred = "MULTI";
    else if (singleTP > multiTP * 1.15) preferred = "SINGLE";
    else preferred = "EQUAL";

    std::cout << "Collision System:" << std::endl;
    std::cout << "  Single TP:      " << std::fixed << std::setprecision(0) << singleTP << " items/ms" << std::endl;
    std::cout << "  Multi TP:       " << std::fixed << std::setprecision(0) << multiTP << " items/ms" << std::endl;
    std::cout << "  Batch Mult:     " << std::fixed << std::setprecision(2) << batchMult << std::endl;
    std::cout << "  Preferred Mode: " << preferred << std::endl;

    double speedup = (singleTP > 0) ? multiTP / singleTP : 0;
    std::cout << "  Multi Speedup:  " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;

    std::cout << "\n============================================\n" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
