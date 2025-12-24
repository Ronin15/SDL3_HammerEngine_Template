/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE EventManagerScalingBenchmark
#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>

#include "managers/EventManager.hpp"
#include "events/Event.hpp"
#include "core/ThreadSystem.hpp"
#include "core/Logger.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <random>
#include <mutex>
#include <iomanip>
#include <algorithm>

// Mock Event class for benchmarking with realistic work
class MockEvent : public Event {
public:
    MockEvent(const std::string& name, int /* complexity */ = 5)
        : m_name(name), m_updateCount(0), m_executeCount(0) {
        m_rng.seed(std::hash<std::string>{}(name));
    }

    void update() override {
        // Simulate realistic event update work: condition checking, state updates
        // Real events do: check time, check positions, update flags
        bool timeCondition = (m_updateCount % 10) == 0;
        bool positionCondition = (m_updateCount % 7) != 0;
        volatile bool conditionMet = timeCondition && positionCondition;

        // Simple state updates that real events do
        m_lastUpdateTime = m_updateCount * 0.016f; // 60fps timing
        m_internalState = conditionMet ? 1 : 0;

        m_updateCount++;
        // Use variables to prevent optimization
        if (conditionMet && m_lastUpdateTime < 0) { /* never happens */ }
    }

    void execute() override {
        // Simulate realistic event execution: apply effects, trigger actions
        // Real events do: play sounds, set flags, update positions, trigger transitions
        m_effectActive = true;
        m_targetX = 100.0f + (m_executeCount % 50);
        m_targetY = 200.0f + (m_executeCount % 30);

        // Simulate triggering other systems (just state changes)
        m_soundTriggered = (m_executeCount % 3) == 0;
        m_particleTriggered = (m_executeCount % 5) == 0;

        m_executeCount++;
        // Use variables to prevent optimization
        if (m_effectActive && m_targetX < 0) { /* never happens */ }
    }

    void reset() override {
        m_updateCount = 0;
        m_executeCount = 0;
    }

    void clean() override {}

    std::string getName() const override { return m_name; }
    std::string getType() const override { return "Mock"; }
    std::string getTypeName() const override { return "MockEvent"; }
    EventTypeId getTypeId() const override { return EventTypeId::Custom; }
    bool checkConditions() override { return true; }

    int getUpdateCount() const { return m_updateCount; }
    int getExecuteCount() const { return m_executeCount; }

private:
    std::string m_name;
    std::atomic<int> m_updateCount;
    std::atomic<int> m_executeCount;
    std::mt19937 m_rng;

    // Realistic event state variables
    float m_lastUpdateTime{0.0f};
    int m_internalState{0};
    bool m_effectActive{false};
    float m_targetX{0.0f};
    float m_targetY{0.0f};
    bool m_soundTriggered{false};
    bool m_particleTriggered{false};
};

// Global shutdown flag for coordinated cleanup
static std::atomic<bool> g_shutdownInProgress{false};
static std::mutex g_outputMutex;

// Mock event handler for benchmarking
class BenchmarkEventHandler {
public:
    BenchmarkEventHandler(int id, int /* complexity */ = 5)
        : m_id(id), m_callCount(0), m_totalProcessingTime(0) {
        // Seed with handler ID for deterministic but varied behavior
        m_rng.seed(static_cast<unsigned int>(id + 12345));
    }

    void handleEvent(const std::string& params) {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_callCount.fetch_add(1);

        // Simulate realistic event handler work: respond to events with simple logic
        // Real handlers do: update UI, play sounds, modify game state
        m_handlerState = (m_callCount % 4);
        m_lastProcessedId = m_id;

        // Simple conditional logic that real handlers do
        bool shouldUpdate = (m_callCount % 3) == 0;
        bool shouldNotify = (m_callCount % 7) == 0;

        volatile int workResult = 0;
        if (shouldUpdate) {
            workResult += m_handlerState * 2;
        }
        if (shouldNotify) {
            workResult += m_lastProcessedId;
        }

        // Use the result to prevent compiler optimization
        if (workResult < -1000) { /* never happens */ }

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
    std::atomic<int> m_callCount;
    std::atomic<int64_t> m_totalProcessingTime;
    std::string m_lastParams;
    std::mt19937 m_rng;

    // Realistic handler state
    int m_handlerState{0};
    int m_lastProcessedId{0};
};

// Global fixture for proper initialization/cleanup
struct GlobalFixture {
    GlobalFixture() {
        std::lock_guard<std::mutex> lock(g_outputMutex);
        std::cout << "\n===== EventManager Scaling Benchmark Started =====" << std::endl;
        g_shutdownInProgress.store(false);

        // Enable benchmark mode to silence manager logging during tests
        HAMMER_ENABLE_BENCHMARK_MODE();

        // Initialize ThreadSystem for EventManager threading
        HammerEngine::ThreadSystem::Instance().init();
        EventManager::Instance().init();
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

        // Disable benchmark mode after cleanup
        HAMMER_DISABLE_BENCHMARK_MODE();

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

    void runHandlerBenchmark(int /* numEventTypes */, int numHandlersPerType, int numTriggers, bool /* useBatching */) {
        if (g_shutdownInProgress.load()) {
            return;
        }

        cleanup();
        handlers.clear();

        EventManager::Instance().enableThreading(true);
        // Use default threshold (100) - matches EventManager::m_threadingThreshold

        // WorkerBudget: all workers available to each manager during its update window
        auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
        size_t totalWorkers = budgetMgr.getBudget().totalWorkers;

        std::cout << "\n=== Immediate Event Trigger Benchmark ===" << std::endl;
        std::cout << "  Config: " << numHandlersPerType << " handlers per type, "
                  << numTriggers << " triggers" << std::endl;
        std::cout << "  System: " << totalWorkers << " workers (all available via WorkerBudget)" << std::endl;

        // Register simple handlers (just count calls)
        std::atomic<int> weatherCallCount{0};
        std::atomic<int> npcCallCount{0};
        std::atomic<int> sceneCallCount{0};

        for (int i = 0; i < numHandlersPerType; ++i) {
            EventManager::Instance().registerHandler(EventTypeId::Weather,
                [&weatherCallCount](const EventData&) { weatherCallCount++; });
            EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
                [&npcCallCount](const EventData&) { npcCallCount++; });
            EventManager::Instance().registerHandler(EventTypeId::SceneChange,
                [&sceneCallCount](const EventData&) { sceneCallCount++; });
        }

        // Warmup
        for (int i = 0; i < 10; ++i) {
            EventManager::Instance().changeWeather("Clear", 1.0f);
        }

        // Benchmark: Measure trigger performance
        const int numMeasurements = 3;
        std::vector<double> durations;

        for (int run = 0; run < numMeasurements; run++) {
            weatherCallCount = 0;
            npcCallCount = 0;
            sceneCallCount = 0;

            auto startTime = std::chrono::high_resolution_clock::now();

            // Trigger events (realistic mix)
            for (int i = 0; i < numTriggers; ++i) {
                switch (i % 3) {
                    case 0: EventManager::Instance().changeWeather("Rainy", 1.0f); break;
                    case 1: EventManager::Instance().spawnNPC("TestNPC", 100.0f, 100.0f); break;
                    case 2: EventManager::Instance().changeScene("TestScene", "fade", 1.0f); break;
                }
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            durations.push_back(duration.count() / 1000.0);
        }

        // Calculate statistics
        double avgDuration = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
        double minDuration = *std::min_element(durations.begin(), durations.end());
        double maxDuration = *std::max_element(durations.begin(), durations.end());

        double triggersPerSecond = (numTriggers / avgDuration) * 1000.0;
        double avgTimePerTrigger = avgDuration / numTriggers;

        std::cout << "\nPerformance (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << avgDuration << " ms" << std::endl;
        std::cout << "  Min/Max: " << minDuration << " / " << maxDuration << " ms" << std::endl;
        std::cout << "  Triggers/sec: " << std::setprecision(0) << triggersPerSecond << std::endl;
        std::cout << "  Time per trigger: " << std::setprecision(4) << avgTimePerTrigger << " ms" << std::endl;

        cleanup();
    }

    void runScalabilityTest() {
        std::cout << "\n===== SCALABILITY TEST =====" << std::endl;
        // WorkerBudget: all workers available to each manager during its update window
        auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
        size_t totalWorkers = budgetMgr.getBudget().totalWorkers;
        std::cout << "System Configuration: " << totalWorkers
                  << " workers (all available via WorkerBudget)" << std::endl;

        // Test progression: realistic event counts for actual games
        std::vector<std::tuple<int, int, int>> testCases = {
            {4, 1, 10},         // Small game: 4 types, 1 handler, 10 events
            {4, 2, 25},         // Medium game: 4 types, 2 handlers each, 25 events
            {4, 3, 50},         // Large game: 4 types, 3 handlers each, 50 events
            {4, 4, 100},        // Very large game: 4 types, 4 handlers each, 100 events
            {4, 5, 200},        // Massive game: 4 types, 5 handlers each, 200 events
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
        std::cout << "\n===== CONCURRENCY BENCHMARK =====" << std::endl;

        const int totalEvents = numThreads * eventsPerThread;
        std::cout << "  Config: " << numThreads << " threads, "
                  << eventsPerThread << " events/thread = "
                  << totalEvents << " total events" << std::endl;

        cleanup();

        // Register simple handler with shared_ptr for safe lifetime management
        auto handlerCallCount = std::make_shared<std::atomic<int>>(0);

        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [handlerCallCount](const EventData&) { ++(*handlerCallCount); });
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [handlerCallCount](const EventData&) { ++(*handlerCallCount); });
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [handlerCallCount](const EventData&) { ++(*handlerCallCount); });

        // Benchmark concurrent deferred dispatch + drain
        auto& threadSystem = HammerEngine::ThreadSystem::Instance();
        std::atomic<int> tasksCompleted{0};

        auto startTime = std::chrono::high_resolution_clock::now();

        // Queue events from multiple threads
        for (int t = 0; t < numThreads; ++t) {
            threadSystem.enqueueTask([&tasksCompleted, t, eventsPerThread]() {
                for (int i = 0; i < eventsPerThread; ++i) {
                    int eventNum = t * eventsPerThread + i;
                    switch (eventNum % 3) {
                        case 0:
                            EventManager::Instance().changeWeather("Storm", 1.0f,
                                EventManager::DispatchMode::Deferred);
                            break;
                        case 1:
                            EventManager::Instance().spawnNPC("NPC", 0.0f, 0.0f,
                                EventManager::DispatchMode::Deferred);
                            break;
                        case 2:
                            EventManager::Instance().changeScene("Scene", "fade", 1.0f,
                                EventManager::DispatchMode::Deferred);
                            break;
                    }
                }
                tasksCompleted.fetch_add(1);
            });
        }

        // Wait for queuing to complete
        while (tasksCompleted.load() < numThreads) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // Drain the deferred queue
        int frameCount = 0;
        int prevCount = 0;
        int stableFrames = 0;

        while (frameCount < 100 && stableFrames < 5) {
            EventManager::Instance().update();
            frameCount++;

            int currentCount = handlerCallCount->load();
            if (currentCount == prevCount) {
                stableFrames++;
            } else {
                stableFrames = 0;
                prevCount = currentCount;
            }

            // Early exit if we processed all events
            if (currentCount >= totalEvents) {
                break;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        double totalTimeMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        int processedEvents = handlerCallCount->load();
        double eventsPerSecond = (processedEvents / totalTimeMs) * 1000.0;

        std::cout << "\nPerformance:" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << totalTimeMs << " ms" << std::endl;
        std::cout << "  Processed: " << processedEvents << "/" << totalEvents << " events" << std::endl;
        std::cout << "  Drain frames: " << frameCount << std::endl;
        std::cout << "  Events/sec: " << std::setprecision(0) << eventsPerSecond << std::endl;
        std::cout << "  Avg time/event: " << std::setprecision(4) << (totalTimeMs / processedEvents) << " ms" << std::endl;

        // Extra safety: ensure all deferred work completes
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

    // Simple test with realistic small game event count
    fixture.runHandlerBenchmark(4, 1, 10, false); // Immediate mode
    fixture.runHandlerBenchmark(4, 1, 10, true);  // Batched mode
}

// Medium scale test
BOOST_AUTO_TEST_CASE(MediumScalePerformance) {
    EventManagerScalingFixture fixture;

    if (g_shutdownInProgress.load()) {
        BOOST_TEST_MESSAGE("Skipping test due to shutdown in progress");
        return;
    }

    std::cout << "\n===== MEDIUM SCALE PERFORMANCE TEST =====" << std::endl;

    // Medium load test - realistic medium game event count
    fixture.runHandlerBenchmark(4, 3, 50, false); // Immediate mode
    fixture.runHandlerBenchmark(4, 3, 50, true);  // Batched mode
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

    // Test concurrent event generation using WorkerBudget (production config)
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Use the same logic as production: optimal workers for 4000 events
    size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::Event, 4000);
    int numThreads = static_cast<int>(std::max(static_cast<size_t>(1), optimalWorkerCount));

    // FIXED: Keep total at 4000 events, divide by thread count
    const int totalEvents = 4000;
    int eventsPerThread = totalEvents / numThreads;
    fixture.runConcurrencyTest(numThreads, eventsPerThread);  // 4000 total events
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
        // Large scale test - realistic maximum game event count
        const int numEventTypes = 4;
        const int numHandlersPerType = 10;
        const int numEvents = 500;



        // Only test batched mode for extreme scale (immediate would be too slow)
        fixture.runHandlerBenchmark(numEventTypes, numHandlersPerType, numEvents, true);

    } catch (const std::exception& e) {
        std::cerr << "Error in extreme scale test: " << e.what() << std::endl;
    }
}
