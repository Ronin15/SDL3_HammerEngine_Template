/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE EventManagerScalingBenchmark
#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>

#include "managers/EventManager.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <mutex>
#include <iomanip>
#include <algorithm>

// Global shutdown flag for coordinated cleanup
static std::atomic<bool> g_shutdownInProgress{false};
static std::mutex g_outputMutex;

// Mock event handler for benchmarking
class BenchmarkEventHandler {
public:
    BenchmarkEventHandler(int id, int complexity = 5) 
        : m_id(id), m_complexity(complexity), m_callCount(0), m_totalProcessingTime(0) {
        // Seed with handler ID for deterministic but varied behavior
        m_rng.seed(static_cast<unsigned int>(id + 12345));
    }

    void handleEvent(const std::string& params) {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        m_callCount.fetch_add(1);
        
        // Simulate processing work with varying complexity
        volatile int dummy = 0;
        for (int i = 0; i < m_complexity * 100; ++i) {
            dummy += m_rng() % 1000;
        }
        
        // Store the parameter for verification
        m_lastParams = params;
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
        m_totalProcessingTime.fetch_add(duration.count());
    }

    int getCallCount() const { return m_callCount.load(); }
    int64_t getTotalProcessingTime() const { return m_totalProcessingTime.load(); }
    std::string getLastParams() const { return m_lastParams; }
    int getId() const { return m_id; }
    void resetCounters() { 
        m_callCount.store(0); 
        m_totalProcessingTime.store(0);
    }

private:
    int m_id;
    int m_complexity;
    std::atomic<int> m_callCount;
    std::atomic<int64_t> m_totalProcessingTime;
    std::string m_lastParams;
    std::mt19937 m_rng;
};

// Global fixture for proper initialization/cleanup
struct GlobalFixture {
    GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_outputMutex);
        std::cout << "\n===== EventManager Scaling Benchmark Started =====" << std::endl;
        g_shutdownInProgress.store(false);
    }

    ~GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_outputMutex);
        g_shutdownInProgress.store(true);
        
        // Clean up EventManager
        try {
            EventManager::Instance().clean();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            // Ignore cleanup errors during shutdown
        }
        
        std::cout << "\n===== EventManager Scaling Benchmark Completed =====" << std::endl;
    }
};

BOOST_GLOBAL_FIXTURE(GlobalFixture);

struct EventManagerScalingFixture {
    EventManagerScalingFixture() {
        // Initialize EventManager
        EventManager::Instance().init();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ~EventManagerScalingFixture() {
        cleanup();
    }

    void cleanup() {
        // Clean up handlers
        handlers.clear();
        
        // Reset EventManager
        EventManager::Instance().clean();
        EventManager::Instance().init();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void runHandlerBenchmark(int numEventTypes, int numHandlersPerType, int numEvents, bool useBatching) {
        // Skip if shutdown is in progress
        if (g_shutdownInProgress.load()) {
            return;
        }

        cleanup();
        handlers.clear();

        // Configure batching
        EventManager::Instance().setBatchProcessingEnabled(useBatching);
        std::string batchingMode = useBatching ? "Batched" : "Immediate";

        std::cout << "\nBenchmark: " << batchingMode << " mode, "
                  << numEventTypes << " event types, "
                  << numHandlersPerType << " handlers per type, "
                  << numEvents << " total events" << std::endl;

        // Create handlers and register them
        for (int eventType = 0; eventType < numEventTypes; ++eventType) {
            std::string eventTypeName = "BenchmarkEvent" + std::to_string(eventType);
            
            for (int handler = 0; handler < numHandlersPerType; ++handler) {
                int handlerId = eventType * numHandlersPerType + handler;
                int complexity = 5 + (handlerId % 10); // Vary complexity from 5-14
                
                auto benchmarkHandler = std::make_shared<BenchmarkEventHandler>(handlerId, complexity);
                handlers.push_back(benchmarkHandler);
                
                // Register handler with EventManager
                EventManager::Instance().registerEventHandler(eventTypeName, 
                    [benchmarkHandler](const std::string& params) {
                        benchmarkHandler->handleEvent(params);
                    });
            }
        }

        // Run multiple measurements for accuracy
        const int numMeasurements = 3;
        std::vector<double> durations;

        for (int run = 0; run < numMeasurements; run++) {
            // Reset handler counters
            for (auto& handler : handlers) {
                handler->resetCounters();
            }

            // Pre-run synchronization
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Measure performance - start timing
            auto startTime = std::chrono::high_resolution_clock::now();

            if (useBatching) {
                // Queue all events first
                for (int event = 0; event < numEvents; ++event) {
                    int eventTypeIdx = event % numEventTypes;
                    std::string eventTypeName = "BenchmarkEvent" + std::to_string(eventTypeIdx);
                    std::string params = "Event_" + std::to_string(event);
                    
                    EventManager::Instance().queueHandlerCall(eventTypeName, params);
                }
                
                // Process all queued handlers at once
                EventManager::Instance().processHandlerQueue();
            } else {
                // Trigger events immediately
                for (int event = 0; event < numEvents; ++event) {
                    int eventTypeIdx = event % numEventTypes;
                    std::string eventTypeName = "BenchmarkEvent" + std::to_string(eventTypeIdx);
                    std::string params = "Event_" + std::to_string(event);
                    
                    EventManager::Instance().queueHandlerCall(eventTypeName, params);
                }
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            durations.push_back(duration.count() / 1000.0); // Convert to milliseconds
        }

        // Calculate statistics
        double avgDuration = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
        double minDuration = *std::min_element(durations.begin(), durations.end());
        double maxDuration = *std::max_element(durations.begin(), durations.end());

        // Calculate derived metrics
        int totalHandlerCalls = numEvents * numHandlersPerType;
        double eventsPerSecond = (numEvents / avgDuration) * 1000.0;
        double handlerCallsPerSecond = (totalHandlerCalls / avgDuration) * 1000.0;
        double timePerEvent = avgDuration / numEvents;
        double timePerHandlerCall = avgDuration / totalHandlerCalls;

        // Report results
        std::cout << "\nPerformance Results (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << avgDuration << " ms" << std::endl;
        std::cout << "  Min/Max time: " << minDuration << "/" << maxDuration << " ms" << std::endl;
        std::cout << "  Time per event: " << std::setprecision(4) << timePerEvent << " ms" << std::endl;
        std::cout << "  Time per handler call: " << std::setprecision(6) << timePerHandlerCall << " ms" << std::endl;
        std::cout << "  Events per second: " << std::setprecision(0) << eventsPerSecond << std::endl;
        std::cout << "  Handler calls per second: " << handlerCallsPerSecond << std::endl;

        // Verify all handlers were called
        int totalExpectedCalls = numEvents * numHandlersPerType;
        int totalActualCalls = 0;
        for (const auto& handler : handlers) {
            totalActualCalls += handler->getCallCount();
        }

        std::cout << "  Handler calls: " << totalActualCalls << "/" << totalExpectedCalls;
        if (totalActualCalls == totalExpectedCalls) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗ (Missing: " << (totalExpectedCalls - totalActualCalls) << ")" << std::endl;
        }

        // Show handler processing time
        int64_t totalProcessingTime = 0;
        for (const auto& handler : handlers) {
            totalProcessingTime += handler->getTotalProcessingTime();
        }
        double avgProcessingTimeMs = (totalProcessingTime / 1000000.0) / handlers.size();
        std::cout << "  Avg handler processing time: " << std::setprecision(3) << avgProcessingTimeMs << " ms" << std::endl;

        cleanup();
    }

    void runScalabilityTest() {
        std::cout << "\n===== SCALABILITY TEST SUITE =====" << std::endl;

        // Test progression: small to large scales
        std::vector<std::tuple<int, int, int>> testCases = {
            {1, 1, 100},        // Minimal: 1 type, 1 handler, 100 events
            {5, 2, 1000},       // Small: 5 types, 2 handlers each, 1K events
            {10, 5, 5000},      // Medium: 10 types, 5 handlers each, 5K events
            {20, 10, 10000},    // Large: 20 types, 10 handlers each, 10K events
            {50, 20, 50000},    // Very Large: 50 types, 20 handlers each, 50K events
        };

        for (const auto& [numTypes, numHandlers, numEvents] : testCases) {
            std::cout << "\n--- Test Case: " << numTypes << " types, " 
                      << numHandlers << " handlers, " << numEvents << " events ---" << std::endl;
            
            // Test both immediate and batched modes
            runHandlerBenchmark(numTypes, numHandlers, numEvents, false); // Immediate
            runHandlerBenchmark(numTypes, numHandlers, numEvents, true);  // Batched
        }
    }

    void runConcurrencyTest(int numThreads, int eventsPerThread) {
        std::cout << "\n===== CONCURRENCY TEST =====" << std::endl;
        std::cout << "Threads: " << numThreads << ", Events per thread: " << eventsPerThread << std::endl;

        cleanup();
        handlers.clear();

        // Enable batching for concurrency test
        EventManager::Instance().setBatchProcessingEnabled(true);

        // Create a few event types with multiple handlers
        const int numEventTypes = 5;
        const int numHandlersPerType = 3;
        
        for (int eventType = 0; eventType < numEventTypes; ++eventType) {
            std::string eventTypeName = "ConcurrentEvent" + std::to_string(eventType);
            
            for (int handler = 0; handler < numHandlersPerType; ++handler) {
                int handlerId = eventType * numHandlersPerType + handler;
                auto benchmarkHandler = std::make_shared<BenchmarkEventHandler>(handlerId, 5);
                handlers.push_back(benchmarkHandler);
                
                EventManager::Instance().registerEventHandler(eventTypeName, 
                    [benchmarkHandler](const std::string& params) {
                        benchmarkHandler->handleEvent(params);
                    });
            }
        }

        // Launch concurrent threads
        std::vector<std::thread> threads;
        std::atomic<bool> startFlag{false};
        auto startTime = std::chrono::high_resolution_clock::now();

        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([t, eventsPerThread, &startFlag]() {
                // Wait for start signal
                while (!startFlag.load()) {
                    std::this_thread::yield();
                }
                
                // Generate events
                std::mt19937 rng(t + 42); // Thread-specific seed
                for (int event = 0; event < eventsPerThread; ++event) {
                    int eventTypeIdx = rng() % numEventTypes;
                    std::string eventTypeName = "ConcurrentEvent" + std::to_string(eventTypeIdx);
                    std::string params = "Thread" + std::to_string(t) + "_Event" + std::to_string(event);
                    
                    EventManager::Instance().queueHandlerCall(eventTypeName, params);
                }
            });
        }

        // Start all threads simultaneously
        startFlag.store(true);
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Process all queued events
        auto processingStart = std::chrono::high_resolution_clock::now();
        EventManager::Instance().processHandlerQueue();
        auto endTime = std::chrono::high_resolution_clock::now();

        // Calculate performance metrics
        auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        auto processingDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - processingStart);
        
        int totalEvents = numThreads * eventsPerThread;
        int totalHandlerCalls = totalEvents * numHandlersPerType;
        
        double totalTimeMs = totalDuration.count() / 1000.0;
        double processingTimeMs = processingDuration.count() / 1000.0;
        double eventsPerSecond = (totalEvents / totalTimeMs) * 1000.0;
        
        // Verify handler calls
        int actualHandlerCalls = 0;
        for (const auto& handler : handlers) {
            actualHandlerCalls += handler->getCallCount();
        }

        std::cout << "\nConcurrency Test Results:" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << totalTimeMs << " ms" << std::endl;
        std::cout << "  Processing time: " << processingTimeMs << " ms" << std::endl;
        std::cout << "  Events queued: " << totalEvents << std::endl;
        std::cout << "  Handler calls: " << actualHandlerCalls << "/" << totalHandlerCalls;
        if (actualHandlerCalls == totalHandlerCalls) {
            std::cout << " ✓" << std::endl;
        } else {
            std::cout << " ✗" << std::endl;
        }
        std::cout << "  Events per second: " << std::setprecision(0) << eventsPerSecond << std::endl;
        std::cout << "  Threads: " << numThreads << std::endl;

        cleanup();
    }

    std::vector<std::shared_ptr<BenchmarkEventHandler>> handlers;
};

// Basic functionality test
BOOST_AUTO_TEST_CASE(BasicHandlerPerformance) {
    EventManagerScalingFixture fixture;
    
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== BASIC HANDLER PERFORMANCE TEST =====" << std::endl;
    
    // Simple test with minimal load
    fixture.runHandlerBenchmark(2, 1, 100, false); // Immediate mode
    fixture.runHandlerBenchmark(2, 1, 100, true);  // Batched mode
}

// Medium scale test
BOOST_AUTO_TEST_CASE(MediumScalePerformance) {
    EventManagerScalingFixture fixture;
    
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== MEDIUM SCALE PERFORMANCE TEST =====" << std::endl;
    
    // Medium load test
    fixture.runHandlerBenchmark(10, 5, 5000, false); // Immediate mode
    fixture.runHandlerBenchmark(10, 5, 5000, true);  // Batched mode
}

// Comprehensive scalability test
BOOST_AUTO_TEST_CASE(ComprehensiveScalabilityTest) {
    EventManagerScalingFixture fixture;
    
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    fixture.runScalabilityTest();
}

// Concurrency test
BOOST_AUTO_TEST_CASE(ConcurrencyTest) {
    EventManagerScalingFixture fixture;
    
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    // Test concurrent event generation
    fixture.runConcurrencyTest(4, 1000);  // 4 threads, 1000 events each
}

// Extreme scale test
BOOST_AUTO_TEST_CASE(ExtremeScaleTest) {
    EventManagerScalingFixture fixture;
    
    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== EXTREME SCALE TEST =====" << std::endl;

    try {
        // Large scale test - this tests the system limits
        const int numEventTypes = 100;
        const int numHandlersPerType = 50;
        const int numEvents = 100000;

        std::cout << "\nExtreme scale parameters:" << std::endl;
        std::cout << "  Event types: " << numEventTypes << std::endl;
        std::cout << "  Handlers per type: " << numHandlersPerType << std::endl;
        std::cout << "  Total events: " << numEvents << std::endl;
        std::cout << "  Total handler calls: " << (numEvents * numHandlersPerType) << std::endl;

        // Only test batched mode for extreme scale (immediate would be too slow)
        fixture.runHandlerBenchmark(numEventTypes, numHandlersPerType, numEvents, true);

    } catch (const std::exception& e) {
        std::cerr << "Error in extreme scale test: " << e.what() << std::endl;
    }
}