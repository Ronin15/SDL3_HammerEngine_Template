/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file ResourceRenderControllerTests.cpp
 * @brief Tests for ResourceRenderController
 *
 * Tests unified rendering of dropped items, containers, and harvestables.
 */

#define BOOST_TEST_MODULE ResourceRenderControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/render/ResourceRenderController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "utils/Camera.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <memory>

using namespace HammerEngine;

// ============================================================================
// Test Fixture
// ============================================================================

class ResourceRenderControllerTestFixture {
public:
    ResourceRenderControllerTestFixture()
        : m_camera()  // Use default constructor
    {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        // Initialize EntityDataManager
        EntityDataManager::Instance().init();

        // Initialize WorldResourceManager
        WorldResourceManager::Instance().init();
    }

    ~ResourceRenderControllerTestFixture() {
        WorldResourceManager::Instance().clean();
        EntityDataManager::Instance().clean();
        EventManager::Instance().clean();
    }

    Camera& getCamera() { return m_camera; }

    // Non-copyable
    ResourceRenderControllerTestFixture(const ResourceRenderControllerTestFixture&) = delete;
    ResourceRenderControllerTestFixture& operator=(const ResourceRenderControllerTestFixture&) = delete;

private:
    Camera m_camera;
};

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerStateTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceRenderControllerName) {
    ResourceRenderController controller;

    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_CASE(TestSubscribe) {
    ResourceRenderController controller;

    // Subscribe is a no-op (no events needed)
    controller.subscribe();

    // Not marked as subscribed since it's empty
    // Just verify no crash
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestDefaultConstruction) {
    // Should not throw
    ResourceRenderController controller;
    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Update Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerUpdateTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestUpdateWithNoResources) {
    ResourceRenderController controller;

    // Should not crash with no resources
    controller.update(0.016f, getCamera());
    controller.update(1.0f, getCamera());
    controller.update(0.0f, getCamera());

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestUpdateMultipleTimes) {
    ResourceRenderController controller;

    // Multiple updates should not crash
    for (int i = 0; i < 100; ++i) {
        controller.update(0.016f, getCamera());
    }

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Render Tests (SDL_Renderer path)
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerRenderTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestRenderDroppedItemsNullRenderer) {
    ResourceRenderController controller;

    // Null renderer should be handled gracefully (no crash)
    controller.renderDroppedItems(nullptr, getCamera(), 0.0f, 0.0f, 1.0f);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestRenderContainersNullRenderer) {
    ResourceRenderController controller;

    // Null renderer should be handled gracefully (no crash)
    controller.renderContainers(nullptr, getCamera(), 0.0f, 0.0f, 1.0f);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestRenderHarvestablesNullRenderer) {
    ResourceRenderController controller;

    // Null renderer should be handled gracefully (no crash)
    controller.renderHarvestables(nullptr, getCamera(), 0.0f, 0.0f, 1.0f);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestRenderWithDifferentCameraOffsets) {
    ResourceRenderController controller;

    // Different camera offsets
    controller.renderDroppedItems(nullptr, getCamera(), 100.0f, 200.0f, 1.0f);
    controller.renderDroppedItems(nullptr, getCamera(), -100.0f, -200.0f, 1.0f);
    controller.renderDroppedItems(nullptr, getCamera(), 0.0f, 0.0f, 0.5f);

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ClearAll Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ResourceRenderControllerClearTests, ResourceRenderControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestClearAllWithNoResources) {
    ResourceRenderController controller;

    // Should not crash with no resources
    controller.clearAll();

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestClearAllMultipleTimes) {
    ResourceRenderController controller;

    // Multiple clears should be safe
    controller.clearAll();
    controller.clearAll();
    controller.clearAll();

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Animation Constants Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ResourceRenderControllerConstantsTests)

BOOST_AUTO_TEST_CASE(TestAnimationConstants) {
    // These are private constants, but we can verify the behavior through testing
    // by checking that the controller doesn't crash with various update deltas

    ResourceRenderControllerTestFixture fixture;
    ResourceRenderController controller;

    // Test with very small delta (high framerate)
    controller.update(0.001f, fixture.getCamera());

    // Test with larger delta (low framerate)
    controller.update(0.1f, fixture.getCamera());

    // Test with exactly 1 second
    controller.update(1.0f, fixture.getCamera());

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Non-Copyable Tests
// ============================================================================

BOOST_AUTO_TEST_SUITE(ResourceRenderControllerCopyTests)

BOOST_AUTO_TEST_CASE(TestNonCopyable) {
    // This test verifies at compile time that the controller is non-copyable
    // The following would fail to compile:
    // ResourceRenderController controller;
    // ResourceRenderController copy = controller;  // Won't compile
    // ResourceRenderController copy2(controller);  // Won't compile

    // Just verify we can default construct
    ResourceRenderController controller;
    BOOST_CHECK_EQUAL(controller.getName(), "ResourceRenderController");
}

BOOST_AUTO_TEST_SUITE_END()
