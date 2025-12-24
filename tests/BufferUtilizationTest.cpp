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
        // Initialize ThreadSystem with default worker count (hardware_concurrency - 1)
        HammerEngine::ThreadSystem::Instance().init();
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
    std::cout << "Sequential execution model: each manager gets all "
              << budget.totalWorkers << " workers during its window\n";

    // Verify we have workers
    BOOST_CHECK_GT(budget.totalWorkers, 0);
}

BOOST_AUTO_TEST_CASE(TestOptimalWorkersAllWorkloads) {
    std::cout << "\n=== Testing Optimal Workers - Sequential Execution Model ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    // Any workload should get all workers (sequential execution model)
    size_t lowWorkload = 500;
    size_t optimalLow = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, lowWorkload);

    size_t highWorkload = 5000;
    size_t optimalHigh = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, highWorkload);

    std::cout << "Low workload (" << lowWorkload << " entities): "
              << optimalLow << " workers\n";
    std::cout << "High workload (" << highWorkload << " entities): "
              << optimalHigh << " workers\n";
    std::cout << "Total workers: " << budget.totalWorkers << "\n";

    // Both should get all workers (sequential execution = no contention)
    BOOST_CHECK_EQUAL(optimalLow, budget.totalWorkers);
    BOOST_CHECK_EQUAL(optimalHigh, budget.totalWorkers);
}

BOOST_AUTO_TEST_CASE(TestZeroWorkloadReturnsZero) {
    std::cout << "\n=== Testing Zero Workload Returns Zero Workers ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, 0);

    std::cout << "Zero workload: " << optimalWorkers << " workers\n";

    // Zero work = zero workers needed
    BOOST_CHECK_EQUAL(optimalWorkers, 0);
}

BOOST_AUTO_TEST_CASE(TestBatchStrategy) {
    std::cout << "\n=== Testing Batch Strategy ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    size_t workload = 1000;
    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, workload);

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

    // Test each system type - all should get same allocation (sequential execution)
    std::vector<std::pair<HammerEngine::SystemType, const char*>> systems = {
        {HammerEngine::SystemType::AI, "AI"},
        {HammerEngine::SystemType::Particle, "Particle"},
        {HammerEngine::SystemType::Pathfinding, "Pathfinding"},
        {HammerEngine::SystemType::Event, "Event"}
    };

    for (const auto& [type, name] : systems) {
        size_t optimalWorkers = budgetMgr.getOptimalWorkers(type, workload);
        auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(type, workload, optimalWorkers);

        std::cout << name << ": optimal=" << optimalWorkers
                  << ", batches=" << batchCount << "x" << batchSize << "\n";

        // All systems get all workers (sequential execution model)
        BOOST_CHECK_EQUAL(optimalWorkers, budget.totalWorkers);

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
    std::cout << "Initial budget - totalWorkers: " << budget1.totalWorkers << "\n";

    // Invalidate cache
    budgetMgr.invalidateCache();

    // Get budget again (should recalculate)
    const auto& budget2 = budgetMgr.getBudget();
    std::cout << "After invalidation - totalWorkers: " << budget2.totalWorkers << "\n";

    // Should have same values (ThreadSystem hasn't changed)
    BOOST_CHECK_EQUAL(budget1.totalWorkers, budget2.totalWorkers);
}

BOOST_AUTO_TEST_SUITE_END()
