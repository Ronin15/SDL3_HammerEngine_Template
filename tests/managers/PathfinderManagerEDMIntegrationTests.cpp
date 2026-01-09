/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file PathfinderManagerEDMIntegrationTests.cpp
 * @brief Tests for PathfinderManager's integration with EntityDataManager
 *
 * These tests verify PathfinderManager-specific EDM integration:
 * - requestPathToEDM() EDM path data access
 * - Path data lifecycle with entity creation/destruction
 *
 * NOTE: State transition tests are covered in EntityDataManagerTests.cpp
 * and CollisionManagerEDMIntegrationTests.cpp. This file focuses on
 * PathfinderManager-specific EDM data flow.
 */

#define BOOST_TEST_MODULE PathfinderManagerEDMIntegrationTests
#include <boost/test/unit_test.hpp>

#include "core/ThreadSystem.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace HammerEngine;

// Test helper for data-driven NPCs (NPCs are purely data, no Entity class)
class PathfindingTestNPC {
public:
    explicit PathfindingTestNPC(const Vector2D& pos) {
        auto& edm = EntityDataManager::Instance();
        m_handle = edm.createDataDrivenNPC(pos, "test", AnimationConfig{}, AnimationConfig{});
    }

    static std::shared_ptr<PathfindingTestNPC> create(const Vector2D& pos) {
        return std::make_shared<PathfindingTestNPC>(pos);
    }

    [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

    [[nodiscard]] size_t getEdmIndex() const {
        if (!m_handle.isValid()) return SIZE_MAX;
        return EntityDataManager::Instance().getIndex(m_handle);
    }

private:
    EntityHandle m_handle;
};

// Test fixture
struct PathfinderEDMFixture {
    PathfinderEDMFixture() {
        ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        BackgroundSimulationManager::Instance().init();
    }

    ~PathfinderEDMFixture() {
        BackgroundSimulationManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        ThreadSystem::Instance().clean();
    }

    void waitForPathCompletion(int maxWaitMs = 100) {
        auto& pm = PathfinderManager::Instance();
        int waited = 0;
        while (waited < maxWaitMs) {
            pm.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            waited += 10;
        }
    }
};

// ============================================================================
// PATH DATA EXISTENCE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(PathDataExistenceTests, PathfinderEDMFixture)

BOOST_AUTO_TEST_CASE(TestPathDataExistsForNewEntity) {
    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);

    auto& edm = EntityDataManager::Instance();
    BOOST_CHECK(edm.hasPathData(edmIndex));
}

BOOST_AUTO_TEST_CASE(TestPathDataAccessible) {
    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);

    auto& edm = EntityDataManager::Instance();
    BOOST_REQUIRE(edm.hasPathData(edmIndex));

    // Access path data - should not crash
    auto& pathData = edm.getPathData(edmIndex);
    [[maybe_unused]] bool hasPath = pathData.hasPath;
    [[maybe_unused]] size_t navIndex = pathData.navIndex;
}

BOOST_AUTO_TEST_CASE(TestMultipleEntitiesHavePathData) {
    auto& edm = EntityDataManager::Instance();

    std::vector<std::shared_ptr<PathfindingTestNPC>> entities;
    std::vector<size_t> edmIndices;

    for (int i = 0; i < 20; ++i) {
        auto pos = Vector2D(static_cast<float>(i * 50), 0.0f);
        entities.push_back(PathfindingTestNPC::create(pos));
        edmIndices.push_back(entities.back()->getEdmIndex());
    }

    // All should have path data
    for (size_t idx : edmIndices) {
        BOOST_CHECK(idx != SIZE_MAX);
        BOOST_CHECK(edm.hasPathData(idx));
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// REQUEST PATH TO EDM TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(RequestPathToEDMTests, PathfinderEDMFixture)

BOOST_AUTO_TEST_CASE(TestRequestPathToEDMDoesNotCrash) {
    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);

    auto& pm = PathfinderManager::Instance();

    // Request path (may return 0 without grid, but shouldn't crash)
    [[maybe_unused]] uint64_t requestId = pm.requestPathToEDM(edmIndex,
        Vector2D(100.0f, 100.0f), Vector2D(500.0f, 500.0f),
        PathfinderManager::Priority::Normal);

    waitForPathCompletion();

    // Path data should still be accessible
    auto& edm = EntityDataManager::Instance();
    BOOST_CHECK(edm.hasPathData(edmIndex));
}

BOOST_AUTO_TEST_CASE(TestMultiplePathRequestsDoNotCrash) {
    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);

    auto& pm = PathfinderManager::Instance();

    // Multiple requests shouldn't crash
    for (int i = 0; i < 5; ++i) {
        Vector2D goal(200.0f + i * 100.0f, 200.0f + i * 100.0f);
        pm.requestPathToEDM(edmIndex, Vector2D(100.0f, 100.0f), goal,
            PathfinderManager::Priority::Normal);
    }

    waitForPathCompletion();

    auto& edm = EntityDataManager::Instance();
    BOOST_CHECK(edm.hasPathData(edmIndex));
}

BOOST_AUTO_TEST_CASE(TestRequestPathWithInvalidIndex) {
    auto& pm = PathfinderManager::Instance();

    // Request with invalid index - should handle gracefully
    [[maybe_unused]] uint64_t requestId = pm.requestPathToEDM(SIZE_MAX,
        Vector2D(0, 0), Vector2D(100, 100), PathfinderManager::Priority::Normal);

    waitForPathCompletion();
    BOOST_CHECK(true); // Pass if no crash
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ENTITY DESTRUCTION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EntityDestructionTests, PathfinderEDMFixture)

BOOST_AUTO_TEST_CASE(TestPathDataInvalidAfterEntityDestruction) {
    auto& edm = EntityDataManager::Instance();

    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    EntityHandle handle = entity->getHandle();
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);
    BOOST_CHECK(edm.hasPathData(edmIndex));

    // Destroy entity
    edm.destroyEntity(handle);
    edm.processDestructionQueue();

    // Handle should be invalid
    BOOST_CHECK(!edm.isValidHandle(handle));
}

BOOST_AUTO_TEST_CASE(TestPathRequestAfterStateTransition) {
    auto& edm = EntityDataManager::Instance();
    auto& pm = PathfinderManager::Instance();

    // Phase 1: Create entities
    {
        std::vector<std::shared_ptr<PathfindingTestNPC>> entities;
        for (int i = 0; i < 10; ++i) {
            entities.push_back(PathfindingTestNPC::create(
                Vector2D(static_cast<float>(i * 50), 0.0f)));
        }

        // State transition
        pm.prepareForStateTransition();
        edm.prepareForStateTransition();
        entities.clear();
    }

    // Phase 2: New entities should work
    auto entity = PathfindingTestNPC::create(Vector2D(100.0f, 100.0f));
    size_t edmIndex = entity->getEdmIndex();
    BOOST_REQUIRE(edmIndex != SIZE_MAX);
    BOOST_CHECK(edm.hasPathData(edmIndex));

    // Path request should work
    pm.requestPathToEDM(edmIndex, Vector2D(100.0f, 100.0f),
        Vector2D(500.0f, 500.0f), PathfinderManager::Priority::Normal);

    waitForPathCompletion();

    BOOST_CHECK(edm.hasPathData(edmIndex));
}

BOOST_AUTO_TEST_SUITE_END()
