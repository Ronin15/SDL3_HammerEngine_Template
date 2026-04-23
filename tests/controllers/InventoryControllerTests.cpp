/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file InventoryControllerTests.cpp
 * @brief Tests for InventoryController
 *
 * Tests inventory pickup and UI synchronization.
 */

#define BOOST_TEST_MODULE InventoryControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/ui/InventoryController.hpp"
#include "entities/Player.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <atomic>
#include <memory>

// ============================================================================
// Test Fixture
// ============================================================================

namespace {

VoidLight::ResourceHandle getResourceHandleById(const std::string& id) {
    return ResourceTemplateManager::Instance().getHandleById(id);
}

} // namespace

class InventoryControllerTestFixture {
public:
    InventoryControllerTestFixture() {
        // Reset EventManager to clean state
        EventManagerTestAccess::reset();
        EventManager::Instance().init();

        BOOST_REQUIRE(ResourceTemplateManager::Instance().init());
        BOOST_REQUIRE(EntityDataManager::Instance().init());
        UIManager::Instance().init();
        WorldResourceManager::Instance().init();
        if (!WorldResourceManager::Instance().hasWorld("test_world")) {
            BOOST_REQUIRE(WorldResourceManager::Instance().createWorld("test_world"));
        }
        WorldResourceManager::Instance().setActiveWorld("test_world");

        player = std::make_shared<Player>();
        player->initializeInventory();
        BOOST_REQUIRE(player->getHandle().isValid());

        goldHandle = getResourceHandleById("gold_coins");
        BOOST_REQUIRE(goldHandle.isValid());
    }

    ~InventoryControllerTestFixture() {
        WorldResourceManager::Instance().clean();
        UIManager::Instance().clean();
        EntityDataManager::Instance().clean();
        ResourceTemplateManager::Instance().clean();
        EventManager::Instance().clean();
    }

    // Non-copyable
    InventoryControllerTestFixture(const InventoryControllerTestFixture&) = delete;
    InventoryControllerTestFixture& operator=(const InventoryControllerTestFixture&) = delete;

protected:
    std::shared_ptr<Player> player;
    VoidLight::ResourceHandle goldHandle;
};

// ============================================================================
// Basic State Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(InventoryControllerStateTests, InventoryControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestInventoryControllerName) {
    // Create with nullptr player
    InventoryController controller(nullptr);

    BOOST_CHECK_EQUAL(controller.getName(), "InventoryController");
}

BOOST_AUTO_TEST_CASE(TestAttemptPickupWithoutPlayer) {
    InventoryController controller(nullptr);

    // Should fail gracefully without player
    bool result = controller.attemptPickup();
    BOOST_CHECK(!result);
}

BOOST_AUTO_TEST_CASE(TestSubscribeWithoutPlayer) {
    InventoryController controller(nullptr);

    // Subscribe should not crash
    controller.subscribe();

    // Should be marked as subscribed
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestConstants) {
    // Verify constants are reasonable
    BOOST_CHECK_GT(InventoryController::PICKUP_RADIUS, 0.0f);
    BOOST_CHECK_LT(InventoryController::PICKUP_RADIUS, 100.0f);

    // Check UI component IDs are not empty
    BOOST_CHECK(InventoryController::INVENTORY_PANEL_ID != nullptr);
    BOOST_CHECK(InventoryController::INVENTORY_STATUS_ID != nullptr);
    BOOST_CHECK(InventoryController::EVENT_LOG_ID != nullptr);
}

BOOST_AUTO_TEST_CASE(TestInitializeInventoryUICreatesReusableGrid) {
    InventoryController controller(player);

    controller.initializeInventoryUI();

    auto& ui = UIManager::Instance();
    BOOST_CHECK(ui.hasComponent(InventoryController::INVENTORY_PANEL_ID));
    BOOST_CHECK(ui.hasComponent(InventoryController::INVENTORY_STATUS_ID));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_slot_0"));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_icon_0"));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_count_0"));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_slot_19"));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_icon_19"));
    BOOST_CHECK(ui.hasComponent("gameplay_inventory_count_19"));

    controller.setInventoryVisible(true);
    BOOST_CHECK(controller.isInventoryVisible());

    controller.setInventoryVisible(false);
    BOOST_CHECK(!controller.isInventoryVisible());
}

BOOST_AUTO_TEST_CASE(TestMoveConstructor) {
    InventoryController controller(nullptr);
    controller.subscribe();

    // Move construct
    InventoryController moved(std::move(controller));

    BOOST_CHECK_EQUAL(moved.getName(), "InventoryController");
}

BOOST_AUTO_TEST_CASE(TestMoveAssignment) {
    InventoryController controller1(nullptr);
    controller1.subscribe();

    InventoryController controller2(nullptr);

    // Move assign
    controller2 = std::move(controller1);

    BOOST_CHECK_EQUAL(controller2.getName(), "InventoryController");
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// Event Subscription Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(InventoryControllerEventTests, InventoryControllerTestFixture)

BOOST_AUTO_TEST_CASE(TestDoubleSubscribe) {
    InventoryController controller(nullptr);

    // First subscribe
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Second subscribe should be a no-op (checkAlreadySubscribed)
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestUnsubscribe) {
    InventoryController controller(nullptr);

    // Subscribe first
    controller.subscribe();
    BOOST_CHECK(controller.isSubscribed());

    // Unsubscribe
    controller.unsubscribe();
    BOOST_CHECK(!controller.isSubscribed());
}

BOOST_AUTO_TEST_CASE(TestAttemptPickupDispatchesResourceChangeEvent) {
    std::atomic<int> resourceChangeEvents{0};
    std::atomic<int> quantityDelta{0};

    EventManager::Instance().registerHandler(
        EventTypeId::ResourceChange,
        [this, &resourceChangeEvents, &quantityDelta](const EventData& data) {
            const auto* event = dynamic_cast<const ResourceChangeEvent*>(data.event.get());
            if (!event || event->getOwnerHandle() != player->getHandle()) {
                return;
            }

            resourceChangeEvents.fetch_add(1, std::memory_order_relaxed);
            quantityDelta.store(event->getQuantityChange(), std::memory_order_relaxed);
        });

    const int initialQuantity =
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), goldHandle);
    EntityHandle droppedItem = EntityDataManager::Instance().createDroppedItem(
        player->getPosition(), goldHandle, 7, "test_world");
    BOOST_REQUIRE(droppedItem.isValid());

    InventoryController controller(player);
    controller.subscribe();

    BOOST_REQUIRE(controller.attemptPickup());
    EventManager::Instance().drainAllDeferredEvents();
    BOOST_CHECK_EQUAL(resourceChangeEvents.load(std::memory_order_relaxed), 1);
    BOOST_CHECK_EQUAL(quantityDelta.load(std::memory_order_relaxed), 7);
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), goldHandle),
        initialQuantity + 7);
}

BOOST_AUTO_TEST_CASE(TestAttemptPickupFailureDoesNotDispatchResourceChangeEvent) {
    std::atomic<int> resourceChangeEvents{0};

    EventManager::Instance().registerHandler(
        EventTypeId::ResourceChange,
        [&resourceChangeEvents](const EventData&) {
            resourceChangeEvents.fetch_add(1, std::memory_order_relaxed);
        });

    InventoryController controller(player);
    controller.subscribe();

    BOOST_CHECK(!controller.attemptPickup());
    EventManager::Instance().drainAllDeferredEvents();
    BOOST_CHECK_EQUAL(resourceChangeEvents.load(std::memory_order_relaxed), 0);
}

BOOST_AUTO_TEST_SUITE_END()
