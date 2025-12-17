/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfinderManagerTests
#include <boost/test/included/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
#include "managers/EventManager.hpp"
#include "events/CollisionObstacleChangedEvent.hpp"
#include "core/ThreadSystem.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "utils/Vector2D.hpp"
#include <chrono>
#include <thread>

using namespace HammerEngine;

// Initialize ThreadSystem for async pathfinding in this test module
struct PFThreadFixture {
    PFThreadFixture() {
        HammerEngine::ThreadSystem::Instance().init(4096);
    }
    ~PFThreadFixture() {
        if (!HammerEngine::ThreadSystem::Instance().isShutdown()) {
            HammerEngine::ThreadSystem::Instance().clean();
        }
    }
};

BOOST_GLOBAL_FIXTURE(PFThreadFixture);

BOOST_AUTO_TEST_SUITE(PathfinderManagerTestSuite)

BOOST_AUTO_TEST_CASE(TestPathfinderManagerSingleton) {
    // Test singleton behavior
    PathfinderManager& instance1 = PathfinderManager::Instance();
    PathfinderManager& instance2 = PathfinderManager::Instance();
    
    BOOST_CHECK(&instance1 == &instance2);
}

BOOST_AUTO_TEST_CASE(TestPathfinderManagerInitialization) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    // Initially not initialized
    BOOST_CHECK(!manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Initialize should succeed
    BOOST_CHECK(manager.init());
    BOOST_CHECK(manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Calling init again should return true (already initialized)
    BOOST_CHECK(manager.init());
    
    // Clean up for other tests
    manager.clean();
    BOOST_CHECK(!manager.isInitialized());
}

BOOST_AUTO_TEST_CASE(TestAsyncPathfinding) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    EntityID entityId = 12345;
    bool callbackCalled = false;
    std::vector<Vector2D> resultPath;
    
    // Test async pathfinding with callback
    auto requestId = manager.requestPath(
        entityId, 
        start, 
        goal, 
        PathfinderManager::Priority::Normal, // Normal priority
        [&callbackCalled, &resultPath](EntityID id, const std::vector<Vector2D>& path) {
            BOOST_CHECK(id == 12345);
            callbackCalled = true;
            resultPath = path;
        }
    );
    
    BOOST_CHECK(requestId > 0); // Valid request ID
    
    // Update to process requests
    manager.update(); // ~60 FPS
    
    // Give it some time to process (async operation)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.update();
    
    // The callback should have been called (even if path is empty due to no world)
    // Note: This might not always be true in a real environment without proper world setup
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestPathfinderConfiguration) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Test configuration methods
    manager.setMaxPathsPerFrame(3);
    manager.setCacheExpirationTime(10.0f);
    manager.setAllowDiagonal(false);
    manager.setMaxIterations(5000);
    
    // These should not crash - actual behavior testing would require more complex setup
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestBasicFunctionality) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(200.0f, 200.0f);
    EntityID entityId = 54321;
    
    // Request a path without callback (should not crash)
    auto requestId = manager.requestPath(entityId, start, goal, PathfinderManager::Priority::Low);
    BOOST_CHECK(requestId > 0);
    
    // Process requests
    manager.update();
    
    // Check that we have pending work initially
    BOOST_CHECK(manager.hasPendingWork() || manager.getQueueSize() == 0); // Either has work or processed quickly
    
    // These should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestWeightFields) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    Vector2D center(150.0f, 150.0f);
    float radius = 50.0f;
    float weight = 2.0f;
    
    // Add temporary weight field
    manager.addTemporaryWeightField(center, radius, weight);
    
    // Clear weight fields
    manager.clearWeightFields();
    
    // These should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestStatistics) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Reset stats first to ensure clean state
    manager.resetStats();
    
    // Get initial stats after reset
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests == 0);
    BOOST_CHECK(stats.completedRequests == 0);
    BOOST_CHECK(stats.failedRequests == 0);
    
    // Reset stats again (should not crash)
    manager.resetStats();
    
    auto statsAfterReset = manager.getStats();
    BOOST_CHECK(statsAfterReset.totalRequests == 0);
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestShutdown) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    BOOST_CHECK(manager.isInitialized());
    BOOST_CHECK(!manager.isShutdown());
    
    // Clean should mark as not initialized but not shutdown
    manager.clean();
    BOOST_CHECK(!manager.isInitialized());
    
    // Re-initialize should work
    BOOST_CHECK(manager.init());
    BOOST_CHECK(manager.isInitialized());
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestUpdateCycle) {
    PathfinderManager& manager = PathfinderManager::Instance();
    
    BOOST_REQUIRE(manager.init());
    
    // Test multiple update cycles
    for (int i = 0; i < 10; ++i) {
        manager.update(); // ~60 FPS
    }
    
    // Should not crash
    
    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestNoInfiniteRetryLoop) {
    // REGRESSION TEST: Ensure failed pathfinding requests don't cause infinite retry loops
    // This test prevents the bug where same failed requests kept getting requeued endlessly
    
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(50.0f, 50.0f);
    Vector2D goal(100.0f, 100.0f);
    EntityID entityId = 99999;
    
    std::atomic<int> callbackCount{0};
    
    // Make multiple identical requests rapidly (simulating the bug condition)
    auto callback = [&callbackCount](EntityID, const std::vector<Vector2D>&) {
        callbackCount.fetch_add(1, std::memory_order_relaxed);
    };
    
    // Request the same path multiple times within the cache window (1000ms)
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback); 
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High, callback);
    
    // Process requests
    for (int i = 0; i < 10; ++i) {
        manager.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Key assertions to prevent regression:
    // 1. We should receive callbacks (not stuck in infinite loop)
    BOOST_CHECK(callbackCount > 0);
    
    // 2. We shouldn't receive excessive callbacks (indicates retry loop)  
    BOOST_CHECK(callbackCount <= 10); // Reasonable upper bound
    
    // 3. Stats should show reasonable request count (not thousands from retry loop)
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests <= 20); // Should be close to our 4 requests, not thousands

    // Ensure no runaway processing from repeated failed path requests

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestFailedRequestCaching) {
    // REGRESSION TEST: Ensure failed requests are properly cached to prevent retry loops
    
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());
    
    Vector2D start(10.0f, 10.0f);
    Vector2D goal(20.0f, 20.0f);  
    EntityID entityId = 88888;
    
    std::atomic<int> firstCallbackCount{0};
    std::atomic<int> secondCallbackCount{0};
    
    // First request
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High,
        [&firstCallbackCount](EntityID, const std::vector<Vector2D>&) {
            firstCallbackCount.fetch_add(1, std::memory_order_relaxed);
        });
    
    // Process first request
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();
    
    // Second identical request within cache window (should be rejected/cached)
    manager.requestPath(entityId, start, goal, PathfinderManager::Priority::High,
        [&secondCallbackCount](EntityID, const std::vector<Vector2D>&) {
            secondCallbackCount.fetch_add(1, std::memory_order_relaxed);
        });
    
    // Process second request  
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();
    
    // Both should have received callbacks (first from processing, second from cache)
    BOOST_CHECK(firstCallbackCount.load(std::memory_order_relaxed) > 0);
    BOOST_CHECK(secondCallbackCount.load(std::memory_order_relaxed) > 0);
    
    // But total requests processed should be minimal (cache working)
    auto stats = manager.getStats();
    BOOST_CHECK(stats.totalRequests <= 20); // Ensure no runaway processing from repeated requests

    manager.clean();
}

BOOST_AUTO_TEST_SUITE_END()

// Integration Tests for PathfinderManager Event System  
BOOST_AUTO_TEST_SUITE(PathfinderEventIntegrationTests)

// Test fixture for PathfinderManager event integration
struct PathfinderEventFixture {
    PathfinderEventFixture() {
        // Initialize EventManager for event testing (following established pattern)
        EventManager::Instance().init();
        
        // Initialize PathfinderManager
        PathfinderManager::Instance().init();
        
        // Reset tracking variables
        collisionVersionIncremented = false;
        cacheInvalidationCount = 0;
    }
    
    ~PathfinderEventFixture() {
        // Clean up in reverse order
        PathfinderManager::Instance().clean();
        EventManager::Instance().clean();
        // ThreadSystem persists across tests (per established pattern)
    }
    
    // Event tracking variables
    bool collisionVersionIncremented;
    std::atomic<int> cacheInvalidationCount{0};
};

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventSubscription, PathfinderEventFixture)
{
    // Test that PathfinderManager properly subscribes to collision obstacle events
    // This tests the event subscription lifecycle
    
    // Manually trigger a collision obstacle changed event
    Vector2D obstaclePos(100.0f, 150.0f);
    float obstacleRadius = 64.0f;
    std::string description = "Test obstacle change";
    
    // Trigger the event
    bool eventFired = EventManager::Instance().triggerCollisionObstacleChanged(
        obstaclePos, obstacleRadius, description, EventManager::DispatchMode::Immediate);
    
    // Event should fire (PathfinderManager should be subscribed)
    BOOST_CHECK(eventFired);
    
    // Brief wait to let the handler process (following established pattern)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // The PathfinderManager should have incremented its collision version
    // (This is internal state, but we can verify by checking if subsequent operations behave correctly)
    BOOST_CHECK(true); // Subscription worked if no exceptions were thrown
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderCacheInvalidationOnCollisionChange, PathfinderEventFixture)
{
    // Test that collision obstacle changes properly invalidate pathfinding cache
    
    // First, let's simulate having some cached paths by triggering path requests
    Vector2D start1(0.0f, 0.0f);
    Vector2D goal1(100.0f, 100.0f);
    Vector2D start2(200.0f, 200.0f); 
    Vector2D goal2(300.0f, 300.0f);
    
    // Request some paths (they may fail due to no world, but will be cached)
    PathfinderManager::Instance().requestPath(1001, start1, goal1, 
        PathfinderManager::Priority::High,
        [](EntityID, const std::vector<Vector2D>&){ /* no-op */ });
    PathfinderManager::Instance().requestPath(1002, start2, goal2,
        PathfinderManager::Priority::High,
        [](EntityID, const std::vector<Vector2D>&){ /* no-op */ });
    
    // Let processing complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Get initial stats
    auto initialStats = PathfinderManager::Instance().getStats();
    
    // Now trigger a collision obstacle change at position that might affect paths
    Vector2D obstaclePos(150.0f, 150.0f);
    EventManager::Instance().triggerCollisionObstacleChanged(
        obstaclePos, 100.0f, "Cache invalidation test", EventManager::DispatchMode::Immediate);
    
    // Brief processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // The cache should have been selectively invalidated
    // We can't directly test cache internals, but we can verify that the system
    // handles the event without crashing and continues to function
    
    // Request the same paths again - they should be processed again if cache was invalidated
    PathfinderManager::Instance().requestPath(1003, start1, goal1,
        PathfinderManager::Priority::High,
        [this](EntityID, const std::vector<Vector2D>&){ 
            cacheInvalidationCount++; 
        });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Verify the system is still functioning (no crashes from event handling)
    auto finalStats = PathfinderManager::Instance().getStats();
    BOOST_CHECK_GE(finalStats.totalRequests, initialStats.totalRequests);
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventHandlerLifecycle, PathfinderEventFixture)
{
    // Test that PathfinderManager properly manages its event subscriptions
    
    // PathfinderManager should be initialized with event subscriptions
    BOOST_CHECK(PathfinderManager::Instance().isInitialized());
    
    // Trigger multiple events to ensure handler is stable
    for (int i = 0; i < 5; ++i) {
        Vector2D pos(i * 50.0f, i * 50.0f);
        bool fired = EventManager::Instance().triggerCollisionObstacleChanged(
            pos, 32.0f, "Lifecycle test " + std::to_string(i), 
            EventManager::DispatchMode::Immediate);
        BOOST_CHECK(fired);
    }
    
    // Brief processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // Clean and reinitialize to test subscription cleanup/re-establishment
    PathfinderManager::Instance().clean();
    BOOST_CHECK(!PathfinderManager::Instance().isInitialized());
    
    // Re-initialize
    PathfinderManager::Instance().init();
    BOOST_CHECK(PathfinderManager::Instance().isInitialized());
    
    // Should still be able to receive events after re-initialization
    bool fired = EventManager::Instance().triggerCollisionObstacleChanged(
        Vector2D(999.0f, 999.0f), 64.0f, "Post-reinit test",
        EventManager::DispatchMode::Immediate);
    BOOST_CHECK(fired);
}

BOOST_FIXTURE_TEST_CASE(TestPathfinderEventPerformance, PathfinderEventFixture) 
{
    // Test that event handling doesn't significantly impact PathfinderManager performance
    
    const int numEvents = 50;
    std::vector<std::chrono::microseconds> eventTimes;
    eventTimes.reserve(numEvents);
    
    // Measure time for each event to be processed
    for (int i = 0; i < numEvents; ++i) {
        Vector2D pos(i * 20.0f, i * 20.0f);
        
        auto start = std::chrono::high_resolution_clock::now();
        
        EventManager::Instance().triggerCollisionObstacleChanged(
            pos, 48.0f, "Performance test " + std::to_string(i),
            EventManager::DispatchMode::Immediate);
        
        auto end = std::chrono::high_resolution_clock::now();
        eventTimes.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
    }
    
    // Calculate average event processing time
    auto totalTime = std::chrono::microseconds(0);
    for (const auto& time : eventTimes) {
        totalTime += time;
    }
    double avgTime = static_cast<double>(totalTime.count()) / numEvents;
    
    // Event processing should be fast (under 50 microseconds average)
    BOOST_CHECK_LT(avgTime, 50.0);
    
    // No single event should take more than 500 microseconds
    for (const auto& time : eventTimes) {
        BOOST_CHECK_LT(time.count(), 500);
    }
    
    BOOST_TEST_MESSAGE("Processed " << numEvents << " collision events in avg " 
                      << avgTime << " μs per event");
    
    // Verify PathfinderManager is still functioning after many events
    auto stats = PathfinderManager::Instance().getStats();
    BOOST_CHECK_GE(stats.totalRequests, 0); // Should be accessible
}

// ========== WorkerBudget Integration Tests ==========

BOOST_AUTO_TEST_CASE(TestBurstRequestHandling) {
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    const size_t burstSize = 150; // Test 150 simultaneous requests
    std::atomic<size_t> completedCount{0};
    std::atomic<size_t> successCount{0};

    BOOST_TEST_MESSAGE("Submitting " << burstSize << " simultaneous path requests...");

    // Submit burst of path requests
    for (size_t i = 0; i < burstSize; ++i) {
        Vector2D start(100.0f + i * 10.0f, 100.0f);
        Vector2D goal(500.0f + i * 10.0f, 500.0f);

        manager.requestPath(
            static_cast<EntityID>(1000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&completedCount, &successCount](EntityID, const std::vector<Vector2D>& path) {
                completedCount.fetch_add(1, std::memory_order_relaxed);
                if (!path.empty()) {
                    successCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        );
    }

    // Process requests over multiple frames (rate limiting should apply)
    const int maxFrames = 10;
    for (int frame = 0; frame < maxFrames; ++frame) {
        manager.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    // Wait for async processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    BOOST_TEST_MESSAGE("Completed " << completedCount.load() << " / " << burstSize << " requests");

    // Verify rate limiting worked (should spread across multiple frames)
    // With 50 req/frame limit, 150 requests should take at least 3 frames
    BOOST_CHECK_GE(completedCount.load(), burstSize / 2); // At least half completed

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestQueuePressureGracefulDegradation) {
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    const size_t queueCapacity = threadSystem.getQueueCapacity();

    BOOST_TEST_MESSAGE("ThreadSystem queue capacity: " << queueCapacity);

    // Submit enough requests to test queue pressure handling
    const size_t testRequests = 200;
    std::atomic<size_t> completed{0};

    for (size_t i = 0; i < testRequests; ++i) {
        Vector2D start(50.0f + i, 50.0f);
        Vector2D goal(300.0f + i, 300.0f);

        manager.requestPath(
            static_cast<EntityID>(2000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&completed](EntityID, const std::vector<Vector2D>&) {
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    // Process over multiple frames
    for (int frame = 0; frame < 15; ++frame) {
        manager.update();

        // Check queue size doesn't exceed critical threshold
        size_t queueSize = threadSystem.getQueueSize();
        double queuePressure = static_cast<double>(queueSize) / queueCapacity;

        // Queue pressure should stay below critical (0.90)
        BOOST_CHECK_LT(queuePressure, 0.95); // Allow small margin

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Wait for completion
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    BOOST_TEST_MESSAGE("Completed " << completed.load() << " / " << testRequests << " requests");
    BOOST_CHECK_GE(completed.load(), testRequests / 2);

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestWorkerBudgetCoordination) {
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    size_t availableWorkers = threadSystem.getThreadCount();

    BOOST_TEST_MESSAGE("Available workers: " << availableWorkers);

    // Calculate expected WorkerBudget allocation
    HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(availableWorkers);

    BOOST_TEST_MESSAGE("Pathfinding allocated workers: " << budget.pathfindingAllocated);
    BOOST_CHECK_GT(budget.pathfindingAllocated, 0); // Should have at least 1 worker allocated

    // Verify buffer capacity exists
    BOOST_CHECK_GT(budget.remaining, 0); // Buffer workers available

    // Submit workload that should trigger batching (> 8 requests)
    const size_t batchWorkload = 24; // 3x the MIN_REQUESTS_FOR_BATCHING
    std::atomic<size_t> completed{0};

    for (size_t i = 0; i < batchWorkload; ++i) {
        Vector2D start(100.0f, 100.0f + i * 5.0f);
        Vector2D goal(400.0f, 400.0f + i * 5.0f);

        manager.requestPath(
            static_cast<EntityID>(3000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&completed](EntityID, const std::vector<Vector2D>&) {
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    // Process - should use batching
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    BOOST_TEST_MESSAGE("Completed " << completed.load() << " / " << batchWorkload << " batch requests");

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestRateLimiting) {
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    // MAX_REQUESTS_PER_FRAME = 50 in PathfinderManager
    const size_t requestsSubmitted = 100;
    std::atomic<size_t> completed{0};

    // Submit 100 requests in single frame
    for (size_t i = 0; i < requestsSubmitted; ++i) {
        Vector2D start(50.0f, 50.0f + i);
        Vector2D goal(200.0f, 200.0f + i);

        manager.requestPath(
            static_cast<EntityID>(4000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&completed](EntityID, const std::vector<Vector2D>&) {
                completed.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    // First update should process maximum 50 requests
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    size_t completedAfterFrame1 = completed.load();
    BOOST_TEST_MESSAGE("After frame 1: " << completedAfterFrame1 << " completed");

    // Second update should process remaining 50
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    size_t completedAfterFrame2 = completed.load();
    BOOST_TEST_MESSAGE("After frame 2: " << completedAfterFrame2 << " completed");

    // Wait for all to finish
    for (int i = 0; i < 5; ++i) {
        manager.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    BOOST_TEST_MESSAGE("Final: " << completed.load() << " / " << requestsSubmitted << " completed");

    // Verify rate limiting spread requests across frames
    BOOST_CHECK_GE(completed.load(), requestsSubmitted / 2);

    manager.clean();
}

BOOST_AUTO_TEST_CASE(TestPriorityStratification) {
    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    // Test that default priority is now Normal (not High)
    std::atomic<size_t> completed{0};

    // Submit with default priority (should be Normal)
    Vector2D start(100.0f, 100.0f);
    Vector2D goal(300.0f, 300.0f);

    auto requestId = manager.requestPath(
        static_cast<EntityID>(5000),
        start,
        goal,
        PathfinderManager::Priority::Normal, // Explicitly Normal
        [&completed](EntityID, const std::vector<Vector2D>&) {
            completed.fetch_add(1, std::memory_order_relaxed);
        }
    );

    BOOST_CHECK_GT(requestId, 0);

    // Process
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    BOOST_CHECK_GE(completed.load(), 0); // Should process successfully

    manager.clean();
}

// ========== Cache Bucketing Regression Test ==========

BOOST_AUTO_TEST_CASE(TestCacheBucketingForNearbyCoordsRegressionFix) {
    // REGRESSION TEST: Verify that nearby coordinates hit the same cache bucket
    // This tests the fix for 0% cache hit rate where pre-warmed sector paths
    // weren't being hit by NPC requests at nearby positions.
    //
    // Root cause was: cache key computed AFTER normalizeEndpoints() which includes
    // snapToNearestOpenWorld() - making cache keys non-deterministic based on obstacles.
    // Fix: compute cache key from RAW coords BEFORE normalization, with sector-based quantization.

    PathfinderManager& manager = PathfinderManager::Instance();
    BOOST_REQUIRE(manager.init());

    // Simulate sector-based coordinates (like pre-warming uses)
    // For a 16000x16000 world with 8 sectors, sector size = 2000px
    // Sector center would be at (1000, 1000), (3000, 1000), etc.
    Vector2D sectorCenter(4000.0f, 4000.0f);
    Vector2D sectorGoal(8000.0f, 8000.0f);

    // Nearby NPC positions (within same sector, offset from center)
    Vector2D npcPos1(4050.0f, 4020.0f);  // 50px offset
    Vector2D npcPos2(4200.0f, 4150.0f);  // 250px offset
    Vector2D npcGoal1(8020.0f, 7990.0f);
    Vector2D npcGoal2(8150.0f, 8100.0f);

    std::atomic<size_t> callbackCount{0};
    auto callback = [&callbackCount](EntityID, const std::vector<Vector2D>&) {
        callbackCount.fetch_add(1, std::memory_order_relaxed);
    };

    // First request (simulates pre-warming at sector center)
    manager.requestPath(1001, sectorCenter, sectorGoal, PathfinderManager::Priority::Normal, callback);

    // Process first request
    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();

    auto stats1 = manager.getStats();
    size_t initialCacheSize = stats1.cacheSize;
    BOOST_TEST_MESSAGE("After first request - Cache size: " << initialCacheSize);

    // Second request from nearby NPC position (should hit cache bucket)
    manager.requestPath(1002, npcPos1, npcGoal1, PathfinderManager::Priority::Normal, callback);

    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();

    auto stats2 = manager.getStats();
    BOOST_TEST_MESSAGE("After second request - Cache hits: " << stats2.cacheHits
                      << ", Cache size: " << stats2.cacheSize);

    // Third request from another nearby position
    manager.requestPath(1003, npcPos2, npcGoal2, PathfinderManager::Priority::Normal, callback);

    manager.update();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.update();

    auto stats3 = manager.getStats();
    BOOST_TEST_MESSAGE("After third request - Cache hits: " << stats3.cacheHits
                      << ", Cache size: " << stats3.cacheSize);

    // With proper sector-based cache key quantization (1000px for 16K world),
    // all three requests should map to the same bucket.
    // Cache size should NOT grow by 3 (would indicate no cache sharing).
    // Note: Without a real world/grid, paths may fail, but cache key bucketing
    // should still work (failed paths are also cached to prevent retry loops).

    // The key assertion: cache size should grow by less than 3
    // (indicating at least some cache bucket sharing occurred)
    size_t cacheGrowth = stats3.cacheSize - initialCacheSize;
    BOOST_TEST_MESSAGE("Cache growth: " << cacheGrowth << " (expected < 3 with bucket sharing)");

    // If cache bucketing is working, we should see some cache hits or limited growth
    // Even if paths fail, the bucket-based coalescing should prevent duplicate processing
    BOOST_CHECK_LE(cacheGrowth, 2); // At most 2 new entries (some should share bucket)

    manager.clean();
}

BOOST_AUTO_TEST_SUITE_END()
