/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE BufferUtilizationTest
#include <boost/test/unit_test.hpp>
#include "core/WorkerBudget.hpp"
#include "core/ThreadSystem.hpp"
#include <iostream>

struct ThreadSystemFixture {
    ThreadSystemFixture() {
        // Initialize ThreadSystem for tests
        HammerEngine::ThreadSystem::Instance().init(256, 8);  // 8 workers for testing
    }
    ~ThreadSystemFixture() {
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

BOOST_FIXTURE_TEST_SUITE(WorkerBudgetManagerTests, ThreadSystemFixture)

BOOST_AUTO_TEST_CASE(TestBudgetAllocation) {
    std::cout << "\n=== Testing WorkerBudgetManager Budget Allocation ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    std::cout << "System: " << budget.totalWorkers << " workers total\n";
    std::cout << "Allocations - AI: " << budget.aiAllocated
              << ", Particle: " << budget.particleAllocated
              << ", Event: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Verify total allocation matches
    size_t totalAllocated = budget.aiAllocated + budget.particleAllocated +
                            budget.eventAllocated + budget.pathfindingAllocated + budget.remaining;
    BOOST_CHECK_EQUAL(totalAllocated, budget.totalWorkers);

    // AI should get the largest allocation (highest weight)
    BOOST_CHECK_GE(budget.aiAllocated, budget.eventAllocated);
    BOOST_CHECK_GE(budget.aiAllocated, budget.pathfindingAllocated);
}

BOOST_AUTO_TEST_CASE(TestOptimalWorkersLowWorkload) {
    std::cout << "\n=== Testing Optimal Workers - Low Workload ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    // Low workload - should use base allocation
    size_t lowWorkload = 500;
    size_t threshold = 1000;
    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, lowWorkload, threshold);

    std::cout << "Low workload (" << lowWorkload << " entities, threshold: "
              << threshold << "): " << optimalWorkers << " workers\n";
    std::cout << "Base AI allocation: " << budget.aiAllocated << "\n";

    // Should use base allocation when below threshold
    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated);
}

BOOST_AUTO_TEST_CASE(TestOptimalWorkersHighWorkload) {
    std::cout << "\n=== Testing Optimal Workers - High Workload ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    // High workload - should use buffer
    size_t highWorkload = 5000;
    size_t threshold = 1000;
    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, highWorkload, threshold);

    std::cout << "High workload (" << highWorkload << " entities, threshold: "
              << threshold << "): " << optimalWorkers << " workers\n";
    std::cout << "Base AI allocation: " << budget.aiAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Should use more than base allocation when above threshold (if buffer available)
    if (budget.remaining > 0) {
        BOOST_CHECK_GT(optimalWorkers, budget.aiAllocated);
    } else {
        BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated);
    }
}

BOOST_AUTO_TEST_CASE(TestBatchStrategy) {
    std::cout << "\n=== Testing Batch Strategy ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    size_t workload = 1000;
    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, workload, 500);

    auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, workload, optimalWorkers);

    std::cout << "Workload: " << workload << ", Workers: " << optimalWorkers << "\n";
    std::cout << "Batch strategy: " << batchCount << " batches of size " << batchSize << "\n";

    // Batches should cover all work
    BOOST_CHECK_GE(batchCount * batchSize, workload);

    // Should have reasonable batch count
    BOOST_CHECK_GE(batchCount, 1);
}

BOOST_AUTO_TEST_CASE(TestBatchCompletionReporting) {
    std::cout << "\n=== Testing Batch Completion Reporting ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Report some batch completions
    budgetMgr.reportBatchCompletion(HammerEngine::SystemType::AI, 4, 0.5);  // 0.125ms per batch
    budgetMgr.reportBatchCompletion(HammerEngine::SystemType::AI, 4, 0.5);

    // Get batch strategy after reporting
    size_t workload = 1000;
    size_t workers = 4;
    auto [batchCount1, batchSize1] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, workload, workers);

    std::cout << "After fast batches (0.125ms/batch): " << batchCount1 << " batches\n";

    // Report slow batches
    budgetMgr.reportBatchCompletion(HammerEngine::SystemType::AI, 2, 10.0);  // 5ms per batch
    budgetMgr.reportBatchCompletion(HammerEngine::SystemType::AI, 2, 10.0);

    auto [batchCount2, batchSize2] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, workload, workers);

    std::cout << "After slow batches (5ms/batch): " << batchCount2 << " batches\n";

    // Both should produce valid batch strategies
    BOOST_CHECK_GE(batchCount1, 1);
    BOOST_CHECK_GE(batchCount2, 1);
}

BOOST_AUTO_TEST_CASE(TestAllSystemTypes) {
    std::cout << "\n=== Testing All System Types ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    size_t workload = 2000;
    size_t threshold = 500;

    // Test each system type
    struct SystemTest {
        HammerEngine::SystemType type;
        const char* name;
        size_t baseAllocation;
    };

    std::vector<SystemTest> systems = {
        {HammerEngine::SystemType::AI, "AI", budget.aiAllocated},
        {HammerEngine::SystemType::Particle, "Particle", budget.particleAllocated},
        {HammerEngine::SystemType::Pathfinding, "Pathfinding", budget.pathfindingAllocated},
        {HammerEngine::SystemType::Event, "Event", budget.eventAllocated}
    };

    for (const auto& sys : systems) {
        size_t optimalWorkers = budgetMgr.getOptimalWorkers(sys.type, workload, threshold);
        auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(sys.type, workload, optimalWorkers);

        std::cout << sys.name << ": base=" << sys.baseAllocation
                  << ", optimal=" << optimalWorkers
                  << ", batches=" << batchCount << "x" << batchSize << "\n";

        // Optimal should be at least base allocation
        BOOST_CHECK_GE(optimalWorkers, std::max(sys.baseAllocation, static_cast<size_t>(1)));

        // Batch strategy should be valid
        BOOST_CHECK_GE(batchCount, 1);
        BOOST_CHECK_GE(batchSize, 1);
    }
}

BOOST_AUTO_TEST_CASE(TestCacheInvalidation) {
    std::cout << "\n=== Testing Cache Invalidation ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Get initial budget
    const auto& budget1 = budgetMgr.getBudget();
    std::cout << "Initial budget - AI: " << budget1.aiAllocated << "\n";

    // Invalidate cache
    budgetMgr.invalidateCache();

    // Get budget again (should recalculate)
    const auto& budget2 = budgetMgr.getBudget();
    std::cout << "After invalidation - AI: " << budget2.aiAllocated << "\n";

    // Should have same values (ThreadSystem hasn't changed)
    BOOST_CHECK_EQUAL(budget1.aiAllocated, budget2.aiAllocated);
    BOOST_CHECK_EQUAL(budget1.totalWorkers, budget2.totalWorkers);
}

BOOST_AUTO_TEST_SUITE_END()
