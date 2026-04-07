/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file ItemControllerTests.cpp
 * @brief Tests for ItemController
 *
 * Tests item pickup and inventory UI synchronization.
 */

#define BOOST_TEST_MODULE ItemControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/world/ItemController.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <memory>

// ============================================================================
// Test Fixture
// ============================================================================

class ItemControllerTestFixture {
public:
    ItemControllerTestFixture() {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        // Initialize EntityDataManager
        BOOST_REQUIRE(EntityDataManager::Instance().init());

        // Initialize WorldResourceManager
        WorldResourceManager::Instance().init();
    }

    ~ItemControllerTestFixture() {
        WorldResourceManager::Instance().clean();
        EntityDataManager::Instance().clean();
        EventManager::Instance().clean();
    }

    // Non-copyable
    ItemControllerTestFixture(const ItemControllerTestFixture&) = delete;
    ItemControllerTestFixture& operator=(const ItemControllerTestFixture&) = delete;
};

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ItemControllerStateTests, ItemControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestItemControllerName) {
    // Create with nullptr player
    ItemController controller(nullptr);

    BOOST_CHECK_EQUAL(controller.getName(), "ItemController");
}

BOOST_AUTO_TEST_CASE(TestAttemptPickupWithoutPlayer) {
    ItemController controller(nullptr);

    // Should fail gracefully without player
    bool result = controller.attemptPickup();
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(TestSubscribeWithoutPlayer) {
    ItemController controller(nullptr);

    // Subscribe should not crash
    controller.subscribe();

    // Should be marked as subscribed
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestConstants) {
    // Verify constants are reasonable
    BOOST_CHECK_GT(ItemController::PICKUP_RADIUS, 0.0f);
    BOOST_CHECK_LT(ItemController::PICKUP_RADIUS, 100.0f);

    // Check UI component IDs are not empty
    BOOST_CHECK(ItemController::INVENTORY_STATUS_ID != nullptr);
    BOOST_CHECK(ItemController::INVENTORY_LIST_ID != nullptr);
    BOOST_CHECK(ItemController::EVENT_LOG_ID != nullptr);
}

BOOST_AUTO_TEST_CASE(TestMoveConstructor) {
    ItemController controller(nullptr);
    controller.subscribe();

    // Move construct
    ItemController moved(std::move(controller));

    BOOST_CHECK_EQUAL(moved.getName(), "ItemController");
}

BOOST_AUTO_TEST_CASE(TestMoveAssignment) {
    ItemController controller1(nullptr);
    controller1.subscribe();

    ItemController controller2(nullptr);

    // Move assign
    controller2 = std::move(controller1);

    BOOST_CHECK_EQUAL(controller2.getName(), "ItemController");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Event Subscription Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(ItemControllerEventTests, ItemControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestDoubleSubscribe) {
    ItemController controller(nullptr);

    // First subscribe
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Second subscribe should be a no-op (checkAlreadySubscribed)
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribe) {
    ItemController controller(nullptr);

    // Subscribe first
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_SUITE_END()
