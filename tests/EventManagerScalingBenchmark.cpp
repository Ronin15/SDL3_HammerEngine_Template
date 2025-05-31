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
        
        // Initialize ThreadSystem for EventManager threading
        Forge::ThreadSystem::Instance().init();
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

        // Re-enable threading but with debugging
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(1000); // Default threshold

        // Note: New optimized EventManager processes events directly
        (void)useBatching; // Suppress unused warning
        std::string mode = "Optimized";


        std::cout << "\nBenchmark: " << mode << " mode, "
                  << numEventTypes << " event types, "
                  << numHandlersPerType << " handlers per type, "
                  << numEvents << " total events" << std::endl;
        
        // Add debugging for threading behavior
        bool willUseThreading = (numEvents > 1000);
        std::cout << "  Threading mode: " << (willUseThreading ? "THREADED" : "SINGLE") 
                  << " (events: " << numEvents << ", threshold: 1000)" << std::endl;

        // Create handlers and register them
        for (int eventType = 0; eventType < numEventTypes; ++eventType) {            
            for (int handler = 0; handler < numHandlersPerType; ++handler) {
                int handlerId = eventType * numHandlersPerType + handler;
                int complexity = 4 + (handlerId % 6); // Vary complexity from 4-9 (balanced for good simulation)
                
                auto benchmarkHandler = std::make_shared<BenchmarkEventHandler>(handlerId, complexity);
                handlers.push_back(benchmarkHandler);
                
                // Register handlers for all event types we'll be triggering
                EventManager::Instance().registerHandler(EventTypeId::Weather,
                    [benchmarkHandler](const EventData&) {
                        benchmarkHandler->handleEvent("weather_event");
                    });
                
                EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
                    [benchmarkHandler](const EventData&) {
                        benchmarkHandler->handleEvent("npc_event");
                    });
                
                EventManager::Instance().registerHandler(EventTypeId::SceneChange,
                    [benchmarkHandler](const EventData&) {
                        benchmarkHandler->handleEvent("scene_event");
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

            // Test the optimized EventManager's update performance
            // Create mock events and register them with varying complexity
            std::vector<EventPtr> testEvents;
            for (int event = 0; event < numEvents; ++event) {
                int complexity = 4 + (event % 6); // Vary complexity from 4-9 like handlers
                auto mockEvent = std::make_shared<MockEvent>("BenchmarkEvent_" + std::to_string(event), complexity);
                testEvents.push_back(mockEvent);
                EventManager::Instance().registerEvent("BenchmarkEvent_" + std::to_string(event), mockEvent);
            }
            
            // Benchmark the EventManager's update cycle (updates state only)
            for (int cycle = 0; cycle < 10; ++cycle) {
                EventManager::Instance().update();
            }
            
            // Benchmark explicit trigger methods (where handlers are actually called)
            for (int cycle = 0; cycle < 5; ++cycle) {
                // Use the proper trigger methods that call handlers
                EventManager::Instance().changeWeather("Rainy", 1.0f);
                EventManager::Instance().changeWeather("Clear", 1.0f);
                EventManager::Instance().spawnNPC("TestNPC", 100.0f, 100.0f);
                EventManager::Instance().changeScene("TestScene", "fade", 1.0f);
            }




            
            // Add delay for threaded tasks to complete
            bool willUseThreading = (numEvents > 1000);
            if (willUseThreading) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            durations.push_back(duration.count() / 1000.0); // Convert to milliseconds

            // Verify work was actually done
            int totalUpdates = 0;
            int totalExecutes = 0;
            for (const auto& event : testEvents) {
                auto mockEvent = std::dynamic_pointer_cast<MockEvent>(event);
                if (mockEvent) {
                    totalUpdates += mockEvent->getUpdateCount();
                    totalExecutes += mockEvent->getExecuteCount();
                }
            }
            
            // Output verification for first run only to avoid spam
            if (run == 0) {
                std::cout << "  Work verification - Updates: " << totalUpdates 
                         << ", Executes: " << totalExecutes 
                         << " (Updates expected: " << (numEvents * 10) << ")" << std::endl;
                std::cout << "  Event count in manager: " << EventManager::Instance().getEventCount() << std::endl;
                std::cout << "  Architecture: Handlers called only via trigger methods (changeWeather, spawnNPC, etc.)" << std::endl;
            }
            
            // Clean up test events
            for (int event = 0; event < numEvents; ++event) {
                EventManager::Instance().removeEvent("BenchmarkEvent_" + std::to_string(event));
            }
        }

        // Calculate statistics
        double avgDuration = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
        double minDuration = *std::min_element(durations.begin(), durations.end());
        double maxDuration = *std::max_element(durations.begin(), durations.end());

        // Calculate derived metrics based on explicit trigger calls
        int totalTriggerCalls = 5 * 4; // 5 cycles * 4 trigger calls per cycle

        // Each of the totalHandlers is registered for ALL 3 event types (Weather, NPC, Scene)
        // So every trigger calls ALL handlers, not just numHandlersPerType
        // Weather triggers: 2 per cycle * 5 cycles = 10 total weather triggers
        // NPC triggers: 1 per cycle * 5 cycles = 5 total npc triggers  
        // Scene triggers: 1 per cycle * 5 cycles = 5 total scene triggers
        // Each trigger calls ALL handlers (since each handler listens to all types)
        int totalBenchmarkHandlers = handlers.size(); // All created benchmark handlers
        int weatherCalls = 10 * totalBenchmarkHandlers;  // 10 weather triggers * all handlers
        int npcCalls = 5 * totalBenchmarkHandlers;       // 5 npc triggers * all handlers
        int sceneCalls = 5 * totalBenchmarkHandlers;     // 5 scene triggers * all handlers
        int totalHandlerCalls = weatherCalls + npcCalls + sceneCalls;
        double eventsPerSecond = (numEvents / avgDuration) * 1000.0;
        double handlerCallsPerSecond = (totalHandlerCalls / avgDuration) * 1000.0;
        double triggersPerSecond = (totalTriggerCalls / avgDuration) * 1000.0;
        double timePerEvent = avgDuration / numEvents;
        double timePerTrigger = avgDuration / totalTriggerCalls;

        // Report results
        std::cout << "\nPerformance Results (avg of " << numMeasurements << " runs):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << avgDuration << " ms" << std::endl;
        std::cout << "  Min/Max time: " << minDuration << "/" << maxDuration << " ms" << std::endl;
        std::cout << "  Time per event: " << std::setprecision(4) << timePerEvent << " ms" << std::endl;
        std::cout << "  Time per trigger: " << std::setprecision(6) << timePerTrigger << " ms" << std::endl;
        std::cout << "  Events per second: " << std::setprecision(0) << eventsPerSecond << std::endl;
        std::cout << "  Triggers per second: " << triggersPerSecond << std::endl;
        std::cout << "  Handler calls per second: " << handlerCallsPerSecond << std::endl;

        // Verify handlers were called via explicit trigger methods
        // Each trigger type calls the handlers registered for that specific type
        int totalExpectedCalls = totalHandlerCalls; // Use the calculated total
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
        
        // Report new behavior
        std::cout << "  Note: update() only updates event state, handlers called via explicit triggers" << std::endl;

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
        std::cout << "\n===== CONCURRENCY TEST =====" << std::endl;
        std::cout << "Threads: " << numThreads << ", Events per thread: " << eventsPerThread << std::endl;

        cleanup();
        handlers.clear();

        // Create a few event types with multiple handlers
        const int numEventTypes = 5;
        const int numHandlersPerType = 3;
        
        for (int eventType = 0; eventType < numEventTypes; ++eventType) {
            for (int handler = 0; handler < numHandlersPerType; ++handler) {
                int handlerId = eventType * numHandlersPerType + handler;
                auto benchmarkHandler = std::make_shared<BenchmarkEventHandler>(handlerId, 5);
                handlers.push_back(benchmarkHandler);
                
                // Register handler with new optimized API
                EventManager::Instance().registerHandler(EventTypeId::Custom,
                    [benchmarkHandler](const EventData&) {
                        benchmarkHandler->handleEvent("concurrent_event");
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
                
                // Generate and register events
                std::mt19937 rng(t + 42); // Thread-specific seed
                for (int event = 0; event < eventsPerThread; ++event) {
                    std::string eventName = "Thread" + std::to_string(t) + "_Event" + std::to_string(event);
                    auto mockEvent = std::make_shared<MockEvent>(eventName);
                    
                    EventManager::Instance().registerEvent(eventName, mockEvent);
                }
            });
        }

        // Start all threads simultaneously
        startFlag.store(true);
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }

        // Process all events through EventManager update
        auto processingStart = std::chrono::high_resolution_clock::now();
        for (int cycle = 0; cycle < 10; ++cycle) {
            EventManager::Instance().update();
        }
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
        // Large scale test - realistic maximum game event count
        const int numEventTypes = 4;
        const int numHandlersPerType = 10;
        const int numEvents = 500;

        std::cout << "\nExtreme scale parameters:" << std::endl;
        std::cout << "  Event types: " << numEventTypes << std::endl;
        std::cout << "  Handlers per type: " << numHandlersPerType << std::endl;
        std::cout << "  Total events: " << numEvents << std::endl;
        std::cout << "  Total handler calls: " << (numEvents * numEventTypes * numHandlersPerType) << std::endl;

        // Only test batched mode for extreme scale (immediate would be too slow)
        fixture.runHandlerBenchmark(numEventTypes, numHandlersPerType, numEvents, true);

    } catch (const std::exception& e) {
        std::cerr << "Error in extreme scale test: " << e.what() << std::endl;
    }
}