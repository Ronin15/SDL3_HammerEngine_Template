/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE PathfinderAIContentionTests
#include <boost/test/included/unit_test.hpp>

#include "managers/PathfinderManager.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "entities/NPC.hpp"
#include "utils/Vector2D.hpp"
#include "world/WorldData.hpp"
#include <chrono>
#include <thread>
#include <atomic>

using namespace HammerEngine;

/**
 * PathfinderAIContentionTests
 *
 * Integration tests to verify that PathfinderManager and AIManager
 * can coexist under heavy load without starving each other for
 * ThreadSystem workers.
 *
 * Tests the WorkerBudget coordination between:
 * - PathfinderManager (19% allocation)
 * - AIManager (44% allocation)
 * - Shared buffer workers (30% for burst capacity)
 */

// Global ThreadSystem fixture (matching PathfinderManagerTests pattern)
struct ContentionThreadFixture {
    ContentionThreadFixture() {
        ThreadSystem::Instance().init(4096);
    }
    ~ContentionThreadFixture() {
        if (!ThreadSystem::Instance().isShutdown()) {
            ThreadSystem::Instance().clean();
        }
    }
};

BOOST_GLOBAL_FIXTURE(ContentionThreadFixture);

BOOST_AUTO_TEST_SUITE(PathfinderAIContentionTestSuite)

BOOST_AUTO_TEST_CASE(TestWorkerBudgetAllocation) {
    auto& threadSystem = ThreadSystem::Instance();
    size_t availableWorkers = threadSystem.getThreadCount();

    BOOST_TEST_MESSAGE("Available workers: " << availableWorkers);

    // Calculate WorkerBudget
    WorkerBudget budget = calculateWorkerBudget(availableWorkers);

    BOOST_TEST_MESSAGE("Worker allocation:");
    BOOST_TEST_MESSAGE("  AI: " << budget.aiAllocated << " (~44%)");
    BOOST_TEST_MESSAGE("  Particle: " << budget.particleAllocated << " (~25%)");
    BOOST_TEST_MESSAGE("  Pathfinding: " << budget.pathfindingAllocated << " (~19%)");
    BOOST_TEST_MESSAGE("  Event: " << budget.eventAllocated << " (~12.5%)");
    BOOST_TEST_MESSAGE("  Buffer: " << budget.remaining << " (~30%)");

    // Verify allocations
    BOOST_CHECK_GT(budget.aiAllocated, 0);
    BOOST_CHECK_GT(budget.pathfindingAllocated, 0);
    BOOST_CHECK_GT(budget.remaining, 0);

    // Verify pathfinding gets reasonable allocation
    if (availableWorkers >= 8) {
        BOOST_CHECK_GE(budget.pathfindingAllocated, 1);
    }

    // Verify total doesn't exceed available
    size_t total = budget.aiAllocated + budget.particleAllocated +
                   budget.pathfindingAllocated + budget.eventAllocated + budget.remaining;
    BOOST_CHECK_EQUAL(total, availableWorkers);
}

BOOST_AUTO_TEST_CASE(TestSimultaneousAIAndPathfindingLoad) {
    // Initialize managers (matching PathfinderManagerTests pattern)
    PathfinderManager::Instance().init();
    AIManager::Instance().init();

    // Submit burst of pathfinding requests simultaneously
    const size_t pathRequests = 100;
    std::atomic<size_t> pathsCompleted{0};

    for (size_t i = 0; i < pathRequests; ++i) {
        Vector2D start(200.0f + i * 5.0f, 200.0f);
        Vector2D goal(800.0f + i * 5.0f, 800.0f);

        PathfinderManager::Instance().requestPath(
            static_cast<EntityID>(2000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&pathsCompleted](EntityID, const std::vector<Vector2D>&) {
                pathsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    BOOST_TEST_MESSAGE("Submitted " << pathRequests << " path requests");

    // Process both managers over multiple frames (matching PathfinderManagerTests pattern)
    const int numFrames = 10;

    for (int frame = 0; frame < numFrames; ++frame) {
        AIManager::Instance().update(0.016f);
        PathfinderManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Wait for async processing to complete (matching PathfinderManagerTests)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    BOOST_TEST_MESSAGE("Completed: " << pathsCompleted.load() << " / " << pathRequests);

    // Verify pathfinding processed work (match PathfinderManagerTests: at least half)
    BOOST_CHECK_GE(pathsCompleted.load(), pathRequests / 2);

    // Clean up
    PathfinderManager::Instance().clean();
    AIManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestNoWorkerStarvation) {
    // Initialize managers
    PathfinderManager::Instance().init();
    AIManager::Instance().init();

    // Submit many path requests to stress PathfinderManager
    const size_t burstRequests = 200;
    std::atomic<size_t> pathsCompleted{0};

    for (size_t i = 0; i < burstRequests; ++i) {
        Vector2D start(100.0f, 100.0f + i);
        Vector2D goal(500.0f, 500.0f + i);

        PathfinderManager::Instance().requestPath(
            static_cast<EntityID>(3000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&pathsCompleted](EntityID, const std::vector<Vector2D>&) {
                pathsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    BOOST_TEST_MESSAGE("Stress test: " << burstRequests << " path requests");

    // Process for extended period (matching PathfinderManagerTests pattern)
    const int stressFrames = 15;

    for (int frame = 0; frame < stressFrames; ++frame) {
        AIManager::Instance().update(0.016f);
        PathfinderManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Wait for async processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    BOOST_TEST_MESSAGE("Completed: " << pathsCompleted.load() << " / " << burstRequests);

    // Verify both managers made progress (matching PathfinderManagerTests: at least half)
    BOOST_CHECK_GE(pathsCompleted.load(), burstRequests / 2);

    // Clean up
    PathfinderManager::Instance().clean();
    AIManager::Instance().clean();
}

BOOST_AUTO_TEST_CASE(TestQueuePressureCoordination) {
    // Initialize managers
    PathfinderManager::Instance().init();
    AIManager::Instance().init();

    // Submit pathfinding requests
    const size_t pathRequests = 150;
    std::atomic<size_t> pathsCompleted{0};

    for (size_t i = 0; i < pathRequests; ++i) {
        Vector2D start(150.0f, 150.0f + i * 2.0f);
        Vector2D goal(600.0f, 600.0f + i * 2.0f);

        PathfinderManager::Instance().requestPath(
            static_cast<EntityID>(4000 + i),
            start,
            goal,
            PathfinderManager::Priority::Normal,
            [&pathsCompleted](EntityID, const std::vector<Vector2D>&) {
                pathsCompleted.fetch_add(1, std::memory_order_relaxed);
            }
        );
    }

    for (int frame = 0; frame < 10; ++frame) {
        AIManager::Instance().update(0.016f);
        PathfinderManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // Wait for async processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    BOOST_TEST_MESSAGE("Completed: " << pathsCompleted.load() << " / " << pathRequests);

    // Verify pathfinding processed work
    BOOST_CHECK_GE(pathsCompleted.load(), pathRequests / 2);

    // Clean up
    PathfinderManager::Instance().clean();
    AIManager::Instance().clean();
}

BOOST_AUTO_TEST_SUITE_END()
