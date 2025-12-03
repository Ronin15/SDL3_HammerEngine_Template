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
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(12);

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
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(3);

    std::cout << "System: 3 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Events: " << budget.eventAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // After removing CollisionManager allocation, low-end systems (3 workers) now have 1 buffer worker
// Old: engine=1, ai=1, collision=1, buffer=0
// New: engine=1, ai=1, buffer=1 (more efficient!)
BOOST_CHECK_EQUAL(budget.remaining, 1);
BOOST_CHECK(budget.hasBufferCapacity());

    // Has buffer capacity, but with only 1 buffer worker, 75% usage rounds down to 0
    // So optimalWorkerCount still returns base allocation
    size_t highWorkload = 10000;
    size_t optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, highWorkload, 1000);
    // With small buffer (1 worker), integer math means no burst workers: (1 * 75%) = 0
    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated);

    std::cout << "High workload with small buffer: " << optimalWorkers << " workers (base="
              << budget.aiAllocated << ", buffer too small for burst)\n";
}

BOOST_AUTO_TEST_CASE(TestVeryHighEndSystem) {
    std::cout << "\n=== Testing Very High-End System (16 workers) ===\n";

    // Test very high-end system (16 workers)
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(16);

    std::cout << "System: 16 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Events: " << budget.eventAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Should have substantial buffer
    BOOST_CHECK_GT(budget.remaining, 1);
    BOOST_CHECK(budget.hasBufferCapacity());

    // Test aggressive buffer usage (75% of buffer, capped at 2x base allocation)
    size_t highWorkload = 50000;
    size_t optimalWorkers = budget.getOptimalWorkerCount(budget.aiAllocated, highWorkload, 1000);
    size_t bufferToUse = (budget.remaining * 3) / 4;
    size_t expectedBurst = std::min(bufferToUse, budget.aiAllocated * 2);

    std::cout << "Very high workload burst: " << optimalWorkers << " workers\n";
    std::cout << "Expected burst workers: " << expectedBurst << "\n";

    BOOST_CHECK_EQUAL(optimalWorkers, budget.aiAllocated + expectedBurst);
}

BOOST_AUTO_TEST_CASE(TestZeroWorkersEdgeCase) {
    std::cout << "\n=== Testing Zero Workers Edge Case (Defensive) ===\n";

    // Test defensive handling of 0 workers (should never happen in practice)
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(0);

    std::cout << "System: 0 workers total (invalid configuration)\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Events: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Should return all-zero budget
    BOOST_CHECK_EQUAL(budget.totalWorkers, 0);
    BOOST_CHECK_EQUAL(budget.engineReserved, 0);
    BOOST_CHECK_EQUAL(budget.aiAllocated, 0);
    BOOST_CHECK_EQUAL(budget.particleAllocated, 0);
    BOOST_CHECK_EQUAL(budget.eventAllocated, 0);
    BOOST_CHECK_EQUAL(budget.pathfindingAllocated, 0);
    BOOST_CHECK_EQUAL(budget.remaining, 0);
}

BOOST_AUTO_TEST_CASE(TestSingleWorkerSystem) {
    std::cout << "\n=== Testing Single Worker System (Tier 1) ===\n";

    // Test 1-core system: hardware_concurrency=1 → ThreadSystem=1 → GameLoop=1 → Managers=0
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(1);

    std::cout << "System: 1 worker total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Particle: " << budget.particleAllocated
              << ", Events: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // All 1 worker goes to GameLoop, managers use single-threaded fallback
    BOOST_CHECK_EQUAL(budget.totalWorkers, 1);
    BOOST_CHECK_EQUAL(budget.engineReserved, 1);
    BOOST_CHECK_EQUAL(budget.aiAllocated, 0);
    BOOST_CHECK_EQUAL(budget.particleAllocated, 0);
    BOOST_CHECK_EQUAL(budget.eventAllocated, 0);
    BOOST_CHECK_EQUAL(budget.pathfindingAllocated, 0);
    BOOST_CHECK_EQUAL(budget.remaining, 0);

    // No buffer capacity on single-worker system
    BOOST_CHECK(!budget.hasBufferCapacity());
}

BOOST_AUTO_TEST_CASE(TestDualWorkerSystem) {
    std::cout << "\n=== Testing Dual Worker System (Tier 2, 1 Manager Worker) ===\n";

    // Test 3-core system: hardware_concurrency=3 → ThreadSystem=2 → GameLoop=1 → Managers=1
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(2);

    std::cout << "System: 2 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Particle: " << budget.particleAllocated
              << ", Events: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Engine gets 1, AI gets 1 (actualManagerWorkers=1)
    BOOST_CHECK_EQUAL(budget.totalWorkers, 2);
    BOOST_CHECK_EQUAL(budget.engineReserved, 1);
    BOOST_CHECK_EQUAL(budget.aiAllocated, 1);  // actualManagerWorkers >= 1
    BOOST_CHECK_EQUAL(budget.particleAllocated, 0); // Needs actualManagerWorkers >= 3
    BOOST_CHECK_EQUAL(budget.eventAllocated, 0);
    BOOST_CHECK_EQUAL(budget.pathfindingAllocated, 0);
    BOOST_CHECK_EQUAL(budget.remaining, 0);

    // Verify total allocation matches
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated +
                            budget.particleAllocated + budget.eventAllocated +
                            budget.pathfindingAllocated + budget.remaining;
    BOOST_CHECK_EQUAL(totalAllocated, budget.totalWorkers);
}

BOOST_AUTO_TEST_CASE(TestFourWorkerSystem) {
    std::cout << "\n=== Testing Four Worker System (Tier 2, 3 Manager Workers) ===\n";

    // Test 5-core system: hardware_concurrency=5 → ThreadSystem=4 → GameLoop=1 → Managers=3
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(4);

    std::cout << "System: 4 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Particle: " << budget.particleAllocated
              << ", Events: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Tier 2 allocation with 3 manager workers
    BOOST_CHECK_EQUAL(budget.totalWorkers, 4);
    BOOST_CHECK_EQUAL(budget.engineReserved, 1);
    BOOST_CHECK_EQUAL(budget.aiAllocated, 1);      // actualManagerWorkers >= 1
    BOOST_CHECK_EQUAL(budget.particleAllocated, 1); // actualManagerWorkers >= 3
    BOOST_CHECK_EQUAL(budget.eventAllocated, 0);    // Tier 2 keeps events single-threaded
    BOOST_CHECK_EQUAL(budget.pathfindingAllocated, 0); // Tier 2 keeps pathfinding single-threaded
    BOOST_CHECK_EQUAL(budget.remaining, 1);         // 3 - 2 = 1 buffer

    // Should have small buffer
    BOOST_CHECK(budget.hasBufferCapacity());

    // Verify total allocation matches
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated +
                            budget.particleAllocated + budget.eventAllocated +
                            budget.pathfindingAllocated + budget.remaining;
    BOOST_CHECK_EQUAL(totalAllocated, budget.totalWorkers);
}

BOOST_AUTO_TEST_CASE(TestFiveWorkerSystem) {
    std::cout << "\n=== Testing Five Worker System (Tier 3 Boundary) ===\n";

    // Test 6-core system: hardware_concurrency=6 → ThreadSystem=5 → GameLoop=1 → Managers=4
    // This is the FIRST Tier 3 allocation (actualManagerWorkers=4)
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(5);

    std::cout << "System: 5 workers total\n";
    std::cout << "Allocations - GameLoop: " << budget.engineReserved
              << ", AI: " << budget.aiAllocated
              << ", Particle: " << budget.particleAllocated
              << ", Events: " << budget.eventAllocated
              << ", Pathfinding: " << budget.pathfindingAllocated
              << ", Buffer: " << budget.remaining << "\n";

    // Tier 3 allocation: weighted distribution + 30% buffer
    BOOST_CHECK_EQUAL(budget.totalWorkers, 5);
    BOOST_CHECK_EQUAL(budget.engineReserved, 1);

    // With 4 manager workers, buffer = max(1, 4*0.3) = 1
    // Allocate remaining 3 workers via weights
    BOOST_CHECK_GT(budget.aiAllocated, 0);        // Should get allocation
    BOOST_CHECK_GT(budget.remaining, 0);          // Should have buffer

    // All subsystems should get something or buffer should compensate
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated +
                            budget.particleAllocated + budget.eventAllocated +
                            budget.pathfindingAllocated + budget.remaining;
    BOOST_CHECK_EQUAL(totalAllocated, budget.totalWorkers);

    std::cout << "First Tier 3 allocation validated\n";
}

BOOST_AUTO_TEST_SUITE_END()
