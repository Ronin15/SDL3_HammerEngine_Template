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

#include "controllers/ui/HudController.hpp"
#include "controllers/ui/InventoryController.hpp"
#include "entities/Player.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "../events/EventManagerTestAccess.hpp"
#include <atomic>
#include <cstdlib>
#include <memory>

// ============================================================================
// Test Fixture
// ============================================================================

namespace {

VoidLight::ResourceHandle getResourceHandleById(const std::string& id) {
    return ResourceTemplateManager::Instance().getHandleById(id);
}

void moveMouseTo(const UIRect& bounds) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = static_cast<float>(bounds.x + (bounds.width / 2));
    event.motion.y = static_cast<float>(bounds.y + (bounds.height / 2));
    InputManager::Instance().onMouseMove(event);
}

void setLeftMouseDownAt(const UIRect& bounds, bool down) {
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = static_cast<float>(bounds.x + (bounds.width / 2));
    event.button.y = static_cast<float>(bounds.y + (bounds.height / 2));
    if (down) {
        InputManager::Instance().onMouseButtonDown(event);
    } else {
        InputManager::Instance().onMouseButtonUp(event);
    }
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
        UIManager::Instance().cleanupForStateTransition();
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
    BOOST_CHECK(ui.hasComponent(InventoryController::INVENTORY_TITLE_ID));
    BOOST_CHECK(ui.hasComponent(InventoryController::INVENTORY_STATUS_ID));
    BOOST_CHECK(ui.hasComponent("inventory_slot_0"));
    BOOST_CHECK(ui.hasComponent("inventory_icon_0"));
    BOOST_CHECK(ui.hasComponent("inventory_count_0"));
    BOOST_CHECK(ui.hasComponent("inventory_slot_19"));
    BOOST_CHECK(ui.hasComponent("inventory_icon_19"));
    BOOST_CHECK(ui.hasComponent("inventory_count_19"));
    BOOST_CHECK(ui.hasComponent("inventory_tab_items"));
    BOOST_CHECK(ui.hasComponent("inventory_tab_gear"));
    BOOST_CHECK(ui.hasComponent("gear_slot_0"));
    BOOST_CHECK(ui.hasComponent("gear_icon_0"));
    BOOST_CHECK(ui.hasComponent("gear_label_0"));
    BOOST_CHECK(ui.hasComponent("gear_slot_7"));
    BOOST_CHECK(ui.hasComponent("gear_icon_7"));
    BOOST_CHECK(ui.hasComponent("gear_label_7"));
    BOOST_CHECK_EQUAL(ui.getText(InventoryController::INVENTORY_TITLE_ID), "Inventory");
    BOOST_CHECK_EQUAL(ui.getText("inventory_tab_items"), "Items");
    BOOST_CHECK_EQUAL(ui.getText("inventory_tab_gear"), "Gear");

    controller.setInventoryVisible(true);
    BOOST_CHECK(controller.isInventoryVisible());

    controller.setInventoryVisible(false);
    BOOST_CHECK(!controller.isInventoryVisible());
}

BOOST_AUTO_TEST_CASE(TestInventoryGearLayoutHasReadableSpacingAt1280x720) {
    auto& ui = UIManager::Instance();
    ui.onWindowResize(1280, 720);

    InventoryController controller(player);
    controller.initializeInventoryUI();

    const UIRect panelBounds = ui.getBounds(InventoryController::INVENTORY_PANEL_ID);
    const UIRect titleBounds = ui.getBounds(InventoryController::INVENTORY_TITLE_ID);
    const UIRect inventoryHeaderBounds = ui.getBounds("inventory_tab_items");
    const UIRect firstInventoryBounds = ui.getBounds("inventory_slot_0");
    const UIRect gearHeaderBounds = ui.getBounds("inventory_tab_gear");
    const UIRect firstGearBounds = ui.getBounds("gear_slot_0");
    const UIRect secondGearBounds = ui.getBounds("gear_slot_1");
    const UIRect firstGearLabelBounds = ui.getBounds("gear_label_0");
    const int panelCenterX = panelBounds.x + (panelBounds.width / 2);

    BOOST_CHECK_GE(panelBounds.width, 480);
    BOOST_CHECK_LT(titleBounds.width, panelBounds.width / 2);
    BOOST_CHECK_GE(firstGearBounds.width, 240);
    BOOST_CHECK_GE(firstGearLabelBounds.width, 200);
    BOOST_CHECK_GE(panelCenterX, 639);
    BOOST_CHECK_LE(panelCenterX, 641);
    BOOST_CHECK_LE(std::abs((titleBounds.x + (titleBounds.width / 2)) - panelCenterX), 1);
    BOOST_CHECK_EQUAL(inventoryHeaderBounds.x, firstInventoryBounds.x);
    BOOST_CHECK_EQUAL(gearHeaderBounds.x, firstGearBounds.x);
    BOOST_CHECK_GT(secondGearBounds.y, firstGearBounds.y + firstGearBounds.height);
    BOOST_CHECK_LE(panelBounds.x + panelBounds.width, 1280);
}

BOOST_AUTO_TEST_CASE(TestPlayerEquipUsesEquipmentSlotMetadata) {
    auto chestHandle = getResourceHandleById("dragon_scale_armor");
    BOOST_REQUIRE(chestHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(chestHandle, 1));

    BOOST_REQUIRE(player->equipItem(chestHandle));

    BOOST_CHECK(player->getEquippedItem("chest") == chestHandle);
    BOOST_CHECK(!player->getEquippedItem("weapon").isValid());
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), chestHandle),
        0);
}

BOOST_AUTO_TEST_CASE(TestUnknownEquipmentSlotDoesNotEquip) {
    auto shieldHandle = getResourceHandleById("iron_shield");
    BOOST_REQUIRE(shieldHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(shieldHandle, 1));

    BOOST_CHECK(!player->equipItem(shieldHandle));
    BOOST_CHECK(!player->getEquippedItem("weapon").isValid());
    BOOST_CHECK(!player->getEquippedItem("unknown").isValid());
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), shieldHandle),
        1);
}

BOOST_AUTO_TEST_CASE(TestInventorySlotClickEquipsAndGearSlotClickUnequips) {
    auto chestHandle = getResourceHandleById("dragon_scale_armor");
    BOOST_REQUIRE(chestHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(chestHandle, 1));

    InventoryController controller(player);
    controller.initializeInventoryUI();
    controller.setInventoryVisible(true);

    auto& ui = UIManager::Instance();
    ui.simulateClick("inventory_slot_0");
    ui.update(0.016f);

    BOOST_CHECK(player->getEquippedItem("chest") == chestHandle);
    BOOST_CHECK_EQUAL(ui.getText("gear_label_2").find("Dragon Scale Armor"), 7);
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), chestHandle),
        0);

    ui.simulateClick("gear_slot_2");
    ui.update(0.016f);

    BOOST_CHECK(!player->getEquippedItem("chest").isValid());
    BOOST_CHECK(ui.getText("gear_label_2").find("Empty") != std::string::npos);
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), chestHandle),
        1);
}

BOOST_AUTO_TEST_CASE(TestEquippingReplacementReturnsPreviousItemToInventory) {
    auto arcaneStaffHandle = getResourceHandleById("arcane_staff");
    auto daggerHandle = getResourceHandleById("dagger");
    BOOST_REQUIRE(arcaneStaffHandle.isValid());
    BOOST_REQUIRE(daggerHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(arcaneStaffHandle, 1));
    BOOST_REQUIRE(player->addToInventory(daggerHandle, 1));

    BOOST_REQUIRE(player->equipItem(arcaneStaffHandle));
    BOOST_REQUIRE(player->getEquippedItem("weapon") == arcaneStaffHandle);

    BOOST_REQUIRE(player->equipItem(daggerHandle));

    BOOST_CHECK(player->getEquippedItem("weapon") == daggerHandle);
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), arcaneStaffHandle),
        1);
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), daggerHandle),
        0);
}

BOOST_AUTO_TEST_CASE(TestWeaponInventoryClickStartsHotbarAssignmentInsteadOfEquip) {
    auto daggerHandle = getResourceHandleById("dagger");
    BOOST_REQUIRE(daggerHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(daggerHandle, 1));

    InventoryController controller(player);
    controller.initializeInventoryUI();
    controller.setInventoryVisible(true);

    auto& ui = UIManager::Instance();
    ui.simulateClick("inventory_slot_0");
    ui.update(0.016f);

    BOOST_CHECK(!player->getEquippedItem("weapon").isValid());
    BOOST_CHECK_EQUAL(
        EntityDataManager::Instance().getInventoryQuantity(player->getInventoryIndex(), daggerHandle),
        1);
}

BOOST_AUTO_TEST_CASE(TestHotbarItemCanBeDraggedToAnotherSlot) {
    auto potionHandle = getResourceHandleById("health_potion");
    BOOST_REQUIRE(potionHandle.isValid());
    BOOST_REQUIRE(player->addToInventory(potionHandle, 3));

    auto& ui = UIManager::Instance();
    ui.onWindowResize(1280, 720);

    HudController hudController(player);
    hudController.initializeHotbarUI();
    BOOST_REQUIRE(hudController.assignHotbarItem(0, potionHandle));

    InventoryController inventoryController(player);
    InputManager::Instance().reset();

    const UIRect sourceBounds = ui.getBounds(HudController::hotbarSlotId(0));
    const UIRect targetBounds = ui.getBounds(HudController::hotbarSlotId(4));

    setLeftMouseDownAt(sourceBounds, true);
    inventoryController.handleHotbarAssignmentInput(hudController);

    moveMouseTo(targetBounds);
    inventoryController.handleHotbarAssignmentInput(hudController);

    setLeftMouseDownAt(targetBounds, false);
    inventoryController.handleHotbarAssignmentInput(hudController);

    BOOST_CHECK(!hudController.getHotbarItem(0).isValid());
    BOOST_CHECK(hudController.getHotbarItem(4) == potionHandle);
    BOOST_CHECK_EQUAL(ui.getTexture("hotbar_icon_0"), "");
    BOOST_CHECK_EQUAL(ui.getText("hotbar_count_0"), "");
    BOOST_CHECK_EQUAL(ui.getTexture("hotbar_icon_4"), "atlas");
    BOOST_CHECK_EQUAL(ui.getText("hotbar_count_4"), "3");
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
