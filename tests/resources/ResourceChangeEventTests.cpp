/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ResourceChangeEventTests
#include <boost/test/unit_test.hpp>

#include <string>
#include <vector>

#include "entities/EntityHandle.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "utils/ResourceHandle.hpp"

using HammerEngine::ResourceHandle;

// Test handles - ResourceChangeEvent just stores handles, doesn't need real entities
static const EntityHandle TEST_PLAYER_HANDLE{1, EntityKind::Player, 1};
static const EntityHandle TEST_NPC_HANDLE{2, EntityKind::NPC, 1};

struct ResourceChangeEventTestFixture {
    ResourceChangeEventTestFixture() {
        // Simple resource handles for testing
        healthPotionHandle = ResourceHandle{1, 1};
        ironSwordHandle = ResourceHandle{2, 1};
    }

    ResourceHandle healthPotionHandle;
    ResourceHandle ironSwordHandle;
};

BOOST_FIXTURE_TEST_SUITE(ResourceChangeEventTestSuite, ResourceChangeEventTestFixture)

BOOST_AUTO_TEST_CASE(TestResourceChangeEventCreation) {
    ResourceChangeEvent event(TEST_PLAYER_HANDLE, healthPotionHandle, 5, 10, "crafted");

    BOOST_CHECK(event.getOwnerHandle() == TEST_PLAYER_HANDLE);
    BOOST_CHECK(event.getResourceHandle() == healthPotionHandle);
    BOOST_CHECK_EQUAL(event.getOldQuantity(), 5);
    BOOST_CHECK_EQUAL(event.getNewQuantity(), 10);
    BOOST_CHECK_EQUAL(event.getQuantityChange(), 5);
    BOOST_CHECK_EQUAL(event.getChangeReason(), "crafted");
}

BOOST_AUTO_TEST_CASE(TestResourceChangeEventTypes) {
    // Test addition (increase)
    ResourceChangeEvent addedEvent(TEST_PLAYER_HANDLE, healthPotionHandle, 3, 8, "found");
    BOOST_CHECK(addedEvent.isIncrease());
    BOOST_CHECK(!addedEvent.isDecrease());
    BOOST_CHECK_EQUAL(addedEvent.getQuantityChange(), 5);

    // Test removal (decrease)
    ResourceChangeEvent removedEvent(TEST_NPC_HANDLE, ironSwordHandle, 10, 3, "consumed");
    BOOST_CHECK(!removedEvent.isIncrease());
    BOOST_CHECK(removedEvent.isDecrease());
    BOOST_CHECK_EQUAL(removedEvent.getQuantityChange(), -7);

    // Test new resource (0 to positive)
    ResourceHandle newItemHandle{3, 1};
    ResourceChangeEvent newResourceEvent(TEST_PLAYER_HANDLE, newItemHandle, 0, 5, "acquired");
    BOOST_CHECK(newResourceEvent.isResourceAdded());
    BOOST_CHECK(!newResourceEvent.isResourceRemoved());
    BOOST_CHECK(newResourceEvent.isIncrease());

    // Test resource removal (positive to 0)
    ResourceHandle oldItemHandle{4, 1};
    ResourceChangeEvent resourceRemovedEvent(TEST_NPC_HANDLE, oldItemHandle, 3, 0, "lost");
    BOOST_CHECK(!resourceRemovedEvent.isResourceAdded());
    BOOST_CHECK(resourceRemovedEvent.isResourceRemoved());
    BOOST_CHECK(resourceRemovedEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestQuantityCalculations) {
    ResourceChangeEvent increaseEvent(TEST_PLAYER_HANDLE, healthPotionHandle, 10, 25, "bought");
    BOOST_CHECK_EQUAL(increaseEvent.getQuantityChange(), 15);
    BOOST_CHECK(increaseEvent.isIncrease());

    ResourceChangeEvent decreaseEvent(TEST_NPC_HANDLE, ironSwordHandle, 20, 8, "used");
    BOOST_CHECK_EQUAL(decreaseEvent.getQuantityChange(), -12);
    BOOST_CHECK(decreaseEvent.isDecrease());

    ResourceHandle stableItemHandle{5, 1};
    ResourceChangeEvent noChangeEvent(TEST_PLAYER_HANDLE, stableItemHandle, 5, 5, "checked");
    BOOST_CHECK_EQUAL(noChangeEvent.getQuantityChange(), 0);
    BOOST_CHECK(!noChangeEvent.isIncrease());
    BOOST_CHECK(!noChangeEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestEventInterface) {
    ResourceChangeEvent event(TEST_PLAYER_HANDLE, healthPotionHandle, 0, 5, "initial");

    BOOST_CHECK_EQUAL(event.getName(), "ResourceChange");
    BOOST_CHECK_EQUAL(event.getType(), ResourceChangeEvent::EVENT_TYPE);
    BOOST_CHECK(event.checkConditions());

    // No-op implementations shouldn't throw
    BOOST_CHECK_NO_THROW(event.update());
    BOOST_CHECK_NO_THROW(event.execute());
    BOOST_CHECK_NO_THROW(event.reset());
    BOOST_CHECK_NO_THROW(event.clean());
}

BOOST_AUTO_TEST_CASE(TestEntityOwnership) {
    ResourceChangeEvent playerEvent(TEST_PLAYER_HANDLE, healthPotionHandle, 1, 3, "player_action");
    BOOST_CHECK(playerEvent.getOwnerHandle() == TEST_PLAYER_HANDLE);
    BOOST_CHECK(playerEvent.getOwnerHandle().isPlayer());

    ResourceChangeEvent npcEvent(TEST_NPC_HANDLE, ironSwordHandle, 2, 1, "npc_action");
    BOOST_CHECK(npcEvent.getOwnerHandle() == TEST_NPC_HANDLE);
    BOOST_CHECK(npcEvent.getOwnerHandle().isNPC());

    // Different entities
    BOOST_CHECK(playerEvent.getOwnerHandle() != npcEvent.getOwnerHandle());
}

BOOST_AUTO_TEST_CASE(TestResourceIdentification) {
    ResourceChangeEvent healthEvent(TEST_PLAYER_HANDLE, healthPotionHandle, 0, 3, "healed");
    BOOST_CHECK(healthEvent.getResourceHandle() == healthPotionHandle);

    ResourceChangeEvent swordEvent(TEST_NPC_HANDLE, ironSwordHandle, 1, 0, "broke");
    BOOST_CHECK(swordEvent.getResourceHandle() == ironSwordHandle);

    ResourceHandle customResourceHandle{6, 1};
    ResourceChangeEvent customEvent(TEST_PLAYER_HANDLE, customResourceHandle, 5, 15, "custom");
    BOOST_CHECK(customEvent.getResourceHandle() == customResourceHandle);
}

BOOST_AUTO_TEST_CASE(TestChangeReasons) {
    std::vector<std::string> reasons = {
        "crafted", "bought", "sold", "consumed", "dropped",
        "found", "traded", "gifted", "stolen", "repaired"};

    for (const auto& reason : reasons) {
        ResourceChangeEvent event(TEST_PLAYER_HANDLE, healthPotionHandle, 1, 2, reason);
        BOOST_CHECK_EQUAL(event.getChangeReason(), reason);
    }

    // Test empty reason (default)
    ResourceChangeEvent noReasonEvent(TEST_PLAYER_HANDLE, healthPotionHandle, 1, 2);
    BOOST_CHECK_EQUAL(noReasonEvent.getChangeReason(), "");
}

BOOST_AUTO_TEST_CASE(TestEdgeCases) {
    // Zero quantities
    ResourceHandle emptyResourceHandle{7, 1};
    ResourceChangeEvent zeroToZeroEvent(TEST_PLAYER_HANDLE, emptyResourceHandle, 0, 0, "no_change");
    BOOST_CHECK_EQUAL(zeroToZeroEvent.getQuantityChange(), 0);
    BOOST_CHECK(!zeroToZeroEvent.isIncrease());
    BOOST_CHECK(!zeroToZeroEvent.isDecrease());
    BOOST_CHECK(!zeroToZeroEvent.isResourceAdded());
    BOOST_CHECK(!zeroToZeroEvent.isResourceRemoved());

    // Large quantities
    ResourceHandle bulkItemHandle{8, 1};
    ResourceChangeEvent largeEvent(TEST_NPC_HANDLE, bulkItemHandle, 10000, 50000, "bulk_operation");
    BOOST_CHECK_EQUAL(largeEvent.getQuantityChange(), 40000);
    BOOST_CHECK(largeEvent.isIncrease());

    // Large decrease
    ResourceHandle depletedResourceHandle{9, 1};
    ResourceChangeEvent massiveDecreaseEvent(TEST_PLAYER_HANDLE, depletedResourceHandle, 100000, 1, "massive_use");
    BOOST_CHECK_EQUAL(massiveDecreaseEvent.getQuantityChange(), -99999);
    BOOST_CHECK(massiveDecreaseEvent.isDecrease());
}

BOOST_AUTO_TEST_CASE(TestInvalidHandle) {
    // Test with invalid/empty handle (world-level events)
    ResourceChangeEvent worldEvent(EntityHandle{}, healthPotionHandle, 0, 100, "world_spawn");
    BOOST_CHECK(!worldEvent.getOwnerHandle().isValid());
    BOOST_CHECK_EQUAL(worldEvent.getQuantityChange(), 100);
}

BOOST_AUTO_TEST_CASE(TestEventStaticType) {
    BOOST_CHECK(!ResourceChangeEvent::EVENT_TYPE.empty());

    ResourceChangeEvent event(TEST_PLAYER_HANDLE, healthPotionHandle, 1, 2, "test");
    BOOST_CHECK_EQUAL(event.getType(), ResourceChangeEvent::EVENT_TYPE);
}

BOOST_AUTO_TEST_SUITE_END()
