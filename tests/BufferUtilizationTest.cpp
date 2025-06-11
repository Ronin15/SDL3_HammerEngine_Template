/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE BufferUtilizationTest
#include <boost/test/unit_test.hpp>
#include "core/WorkerBudget.hpp"
#include <iostream>

BOOST_AUTO_TEST_SUITE(WorkerBudgetBufferTests)

BOOST_AUTO_TEST_CASE(TestBufferUtilizationLogic) {
    std::cout << "\n=== Testing Buffer Thread Utilization ===\n";
    
    // Test high-end system (12 workers)
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(12);
    
    std::cout << "System: 12 workers total\n";
    std::cout << "Base allocations - GameLoop: " << budget.engineReserved 
              << ", AI: " << budget.aiAllocated 
              << ", Events: " << budget.eventAllocated 
              << ", Buffer: " << budget.remaining << "\n";
    
    // Test AI workload scenarios
    std::cout << "\nAI Workload Tests:\n";
    
    // Low workload - should use base allocation
    size_t lowWorkload = 500;
    size_t optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, lowWorkload, 1000);
    std::cout << "Low workload (" << lowWorkload << " entities): " << optimalWorkers << " workers\n";
    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated);
    
    // High workload - should use buffer
    size_t highWorkload = 5000;
    optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, highWorkload, 1000);
    std::cout << "High workload (" << highWorkload << " entities): " << optimalWorkers << " workers\n";
    BOOST_CHECK_GT(optimalWorkers, budget.aiAllocated);
    
    // Test Event workload scenarios
    std::cout << "\nEvent Workload Tests:\n";
    
    // Low workload - should use base allocation
    size_t lowEvents = 50;
    optimalWorkers = budget.getOptimalWorkerCount(budget.eventAllocated, lowEvents, 100);
    std::cout << "Low workload (" << lowEvents << " events): " << optimalWorkers << " workers\n";
    BOOST_CHECK_EQUAL(optimalWorkers, budget.eventAllocated);
    
    // High workload - should use buffer
    size_t highEvents = 500;
    optimalWorkers = budget.getOptimalWorkerCount(budget.eventAllocated, highEvents, 100);
    std::cout << "High workload (" << highEvents << " events): " << optimalWorkers << " workers\n";
    BOOST_CHECK_GT(optimalWorkers, budget.eventAllocated);
    
    // Test buffer capacity checks
    BOOST_CHECK(budget.hasBufferCapacity());
    
    // Test max worker count
    size_t maxWorkers = budget.getMaxWorkerCount(budget.aiAllocated);
    BOOST_CHECK_EQUAL(maxWorkers, budget.aiAllocated + budget.remaining);
    
    std::cout << "Max possible AI workers: " << maxWorkers << "\n";
}

BOOST_AUTO_TEST_CASE(TestLowEndSystemBuffer) {
    std::cout << "\n=== Testing Low-End System (No Buffer) ===\n";
    
    // Test low-end system (3 workers)
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(3);
    
    std::cout << "System: 3 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved 
              << ", AI: " << budget.aiAllocated 
              << ", Events: " << budget.eventAllocated 
              << ", Buffer: " << budget.remaining << "\n";
    
    // No buffer available
    BOOST_CHECK_EQUAL(budget.remaining, 0);
    BOOST_CHECK(!budget.hasBufferCapacity());
    
    // Should always return base allocation regardless of workload
    size_t highWorkload = 10000;
    size_t optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, highWorkload, 1000);
    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated);
    
    std::cout << "High workload with no buffer: " << optimalWorkers << " workers (same as base)\n";
}

BOOST_AUTO_TEST_CASE(TestVeryHighEndSystem) {
    std::cout << "\n=== Testing Very High-End System (16 workers) ===\n";
    
    // Test very high-end system (16 workers)
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(16);
    
    std::cout << "System: 16 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved 
              << ", AI: " << budget.aiAllocated 
              << ", Events: " << budget.eventAllocated 
              << ", Buffer: " << budget.remaining << "\n";
    
    // Should have substantial buffer
    BOOST_CHECK_GT(budget.remaining, 1);
    BOOST_CHECK(budget.hasBufferCapacity());
    
    // Test conservative buffer usage (max half of base allocation)
    size_t highWorkload = 50000;
    size_t optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, highWorkload, 1000);
    size_t expectedBurst = std::min(budget.remaining, budget.aiAllocated);
    
    std::cout << "Very high workload burst: " << optimalWorkers << " workers\n";
    std::cout << "Expected burst workers: " << expectedBurst << "\n";
    
    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated + expectedBurst);
}

BOOST_AUTO_TEST_SUITE_END()