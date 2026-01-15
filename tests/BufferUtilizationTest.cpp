/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE BufferUtilizationTest
#include <boost/test/unit_test.hpp>
#include "core/WorkerBudget.hpp"
#include "core/ThreadSystem.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <thread>
#include <cmath>

// Global fixture: ThreadSystem initialized once for entire test suite
struct GlobalThreadSystemFixture {
    GlobalThreadSystemFixture() {
        // Initialize ThreadSystem with default worker count (hardware_concurrency - 1)
        HammerEngine::ThreadSystem::Instance().init();
    }
    ~GlobalThreadSystemFixture() {
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

// Apply global fixture to entire test module
BOOST_GLOBAL_FIXTURE(GlobalThreadSystemFixture);

BOOST_AUTO_TEST_SUITE(WorkerBudgetManagerTests)

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

BOOST_AUTO_TEST_CASE(TestExecutionReporting) {
    std::cout << "\n=== Testing Execution Reporting ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Report some executions (workload, wasThreaded, batchCount, timeMs)
    size_t workload = 1000;
    budgetMgr.reportExecution(HammerEngine::SystemType::AI, workload, true, 4, 0.5);  // 0.5ms total
    budgetMgr.reportExecution(HammerEngine::SystemType::AI, workload, true, 4, 0.5);

    // Get batch strategy after reporting
    size_t workers = 4;
    auto [batchCount1, batchSize1] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, workload, workers);

    std::cout << "After fast execution (0.5ms total): " << batchCount1 << " batches\n";

    // Report slow execution
    budgetMgr.reportExecution(HammerEngine::SystemType::AI, workload, true, 2, 10.0);  // 10ms total
    budgetMgr.reportExecution(HammerEngine::SystemType::AI, workload, true, 2, 10.0);

    auto [batchCount2, batchSize2] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, workload, workers);

    std::cout << "After slow execution (10ms total): " << batchCount2 << " batches\n";

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

BOOST_AUTO_TEST_CASE(TestThreadingThresholds) {
    std::cout << "\n=== Testing Threading Thresholds ===\n";
    std::cout << "Finding optimal workload sizes for threading...\n\n";

    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    const size_t workers = threadSystem.getThreadCount();

    std::cout << "Workers: " << workers << "\n\n";

    // Test different workload sizes
    std::vector<size_t> workloads = {10, 25, 50, 100, 200, 500, 1000, 2000, 5000, 10000};

    // Simulated work per item - adjust to match actual manager work
    auto doWork = [](size_t iterations) {
        volatile double result = 0;
        for (size_t i = 0; i < iterations; ++i) {
            result += std::sin(static_cast<double>(i)) * std::cos(static_cast<double>(i));
        }
        return result;
    };

    const size_t workPerItem = 100;  // iterations of work per item
    const size_t testRuns = 5;       // average over multiple runs

    std::cout << std::setw(10) << "Workload"
              << std::setw(15) << "Single (ms)"
              << std::setw(15) << "Threaded (ms)"
              << std::setw(12) << "Speedup"
              << std::setw(15) << "Recommendation" << "\n";
    std::cout << std::string(67, '-') << "\n";

    size_t threadingThreshold = 0;

    for (size_t workload : workloads) {
        double singleTotal = 0;
        double threadedTotal = 0;

        for (size_t run = 0; run < testRuns; ++run) {
            // Single-threaded timing
            auto singleStart = std::chrono::high_resolution_clock::now();
            for (size_t i = 0; i < workload; ++i) {
                doWork(workPerItem);
            }
            auto singleEnd = std::chrono::high_resolution_clock::now();
            singleTotal += std::chrono::duration<double, std::milli>(singleEnd - singleStart).count();

            // Multi-threaded timing using ThreadSystem
            auto threadedStart = std::chrono::high_resolution_clock::now();

            size_t batchCount = std::min(workers, workload);
            size_t batchSize = (workload + batchCount - 1) / batchCount;
            std::atomic<size_t> completed{0};

            for (size_t batch = 0; batch < batchCount; ++batch) {
                size_t start = batch * batchSize;
                size_t end = std::min(start + batchSize, workload);

                threadSystem.enqueueTask([&, start, end]() {
                    for (size_t i = start; i < end; ++i) {
                        doWork(workPerItem);
                    }
                    completed.fetch_add(1, std::memory_order_release);
                });
            }

            // Wait for completion
            while (completed.load(std::memory_order_acquire) < batchCount) {
                std::this_thread::yield();
            }

            auto threadedEnd = std::chrono::high_resolution_clock::now();
            threadedTotal += std::chrono::duration<double, std::milli>(threadedEnd - threadedStart).count();
        }

        double singleAvg = singleTotal / testRuns;
        double threadedAvg = threadedTotal / testRuns;
        double speedup = singleAvg / threadedAvg;

        std::string recommendation;
        if (speedup > 1.5) {
            recommendation = "THREAD";
            if (threadingThreshold == 0) threadingThreshold = workload;
        } else if (speedup > 1.1) {
            recommendation = "marginal";
        } else {
            recommendation = "single";
        }

        std::cout << std::setw(10) << workload
                  << std::setw(15) << std::fixed << std::setprecision(3) << singleAvg
                  << std::setw(15) << std::fixed << std::setprecision(3) << threadedAvg
                  << std::setw(11) << std::fixed << std::setprecision(2) << speedup << "x"
                  << std::setw(15) << recommendation << "\n";
    }

    std::cout << "\n=== Current Manager Thresholds ===\n";
    std::cout << "AIManager:       100 entities\n";
    std::cout << "ParticleManager: 100 particles\n";
    std::cout << "EventManager:    100 events\n";

    std::cout << "\n=== Recommendations ===\n";
    if (threadingThreshold == 0) {
        std::cout << "Threading beneficial at all tested sizes (10+)\n";
        std::cout << "Current threshold of 100 is conservative - could lower to 50\n";
    } else if (threadingThreshold <= 100) {
        std::cout << "Threading beneficial at: " << threadingThreshold << "+ items\n";
        std::cout << "Current threshold of 100 is appropriate\n";
    } else {
        std::cout << "Threading beneficial at: " << threadingThreshold << "+ items\n";
        std::cout << "Consider RAISING thresholds to: " << threadingThreshold << "\n";
    }

    // Basic sanity check - threading should help somewhere
    BOOST_CHECK_LE(threadingThreshold, 1000);
}

BOOST_AUTO_TEST_CASE(TestBatchTuningStability) {
    std::cout << "\n=== Testing Batch Tuning Stability (Simulation) ===\n";

    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    const auto& budget = budgetMgr.getBudget();

    // Use Particle system to avoid interference with AI state from other tests
    const auto systemType = HammerEngine::SystemType::Particle;
    const size_t workload = 14000;  // Simulate 14K entities like real game
    const size_t numFrames = 200;   // Simulate 200 frames (~3.3 seconds at 60fps)

    std::cout << "Workers: " << budget.totalWorkers << ", Workload: " << workload << "\n";
    std::cout << "Simulating " << numFrames << " frames...\n\n";

    // Track batch counts
    std::vector<size_t> batchCounts;
    batchCounts.reserve(numFrames);

    // Simulate realistic timing with noise
    // Base time scales with batch count (fewer batches = less overhead but less parallelism)
    auto simulateFrameTime = [&](size_t batches) -> double {
        // Base work time per item (microseconds)
        double baseTimePerItem = 0.15;  // ~0.15µs per entity

        // Parallel speedup (not perfect - diminishing returns)
        double parallelism = std::min(static_cast<double>(batches),
                                      static_cast<double>(budget.totalWorkers));
        double speedup = 1.0 + (parallelism - 1.0) * 0.85;  // 85% parallel efficiency

        // Work time
        double workTimeMs = (workload * baseTimePerItem / 1000.0) / speedup;

        // Overhead per batch (~10-20µs per batch)
        double overheadMs = batches * 0.015;

        // Add realistic noise (±20%)
        double noise = 0.8 + (static_cast<double>(rand() % 40) / 100.0);

        return (workTimeMs + overheadMs) * noise;
    };

    // Run simulation
    for (size_t frame = 0; frame < numFrames; ++frame) {
        size_t workers = budgetMgr.getOptimalWorkers(systemType, workload);
        auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(systemType, workload, workers);

        batchCounts.push_back(batchCount);

        // Simulate frame execution
        double frameTimeMs = simulateFrameTime(batchCount);

        // Report execution (unified API)
        budgetMgr.reportExecution(systemType, workload, true, batchCount, frameTimeMs);

        // Log every 20 frames
        if (frame < 10 || frame % 20 == 0) {
            std::cout << "Frame " << frame << ": " << batchCount << " batches, "
                      << std::fixed << std::setprecision(2) << frameTimeMs << "ms\n";
        }
    }

    // Analyze stability (last 100 frames after convergence)
    size_t analysisStart = numFrames > 100 ? numFrames - 100 : 0;
    size_t minBatch = batchCounts[analysisStart];
    size_t maxBatch = batchCounts[analysisStart];
    double sumBatch = 0;

    for (size_t i = analysisStart; i < numFrames; ++i) {
        minBatch = std::min(minBatch, batchCounts[i]);
        maxBatch = std::max(maxBatch, batchCounts[i]);
        sumBatch += batchCounts[i];
    }

    double avgBatch = sumBatch / (numFrames - analysisStart);
    size_t range = maxBatch - minBatch;

    std::cout << "\n=== Stability Analysis (last 100 frames) ===\n";
    std::cout << "Batch count: min=" << minBatch << ", max=" << maxBatch
              << ", avg=" << std::fixed << std::setprecision(1) << avgBatch << "\n";
    std::cout << "Range: " << range << " batches (";

    if (range <= 2) {
        std::cout << "EXCELLENT - very stable)\n";
    } else if (range <= 4) {
        std::cout << "GOOD - stable)\n";
    } else if (range <= 6) {
        std::cout << "OK - some variance)\n";
    } else {
        std::cout << "NEEDS TUNING - too much variance)\n";
    }

    // Check convergence - should stabilize within reasonable range
    BOOST_CHECK_LE(range, 6);  // Allow up to 6 batch variance
    BOOST_CHECK_GE(avgBatch, 5);  // Should find reasonable parallelism
}

BOOST_AUTO_TEST_SUITE_END()
