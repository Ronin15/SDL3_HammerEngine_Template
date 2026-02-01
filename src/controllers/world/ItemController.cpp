/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/world/ItemController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <format>

void ItemController::subscribe() {
    // No event subscriptions needed - purely on-demand queries
    ITEM_DEBUG("ItemController subscribed");
    setSubscribed(true);
}

bool ItemController::attemptPickup() {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    auto& wrm = WorldResourceManager::Instance();
    auto& edm = EntityDataManager::Instance();

    Vector2D playerPos = player->getPosition();

    // Find closest item on-demand
    size_t itemIdx;
    if (!wrm.findClosestDroppedItem(playerPos, PICKUP_RADIUS, itemIdx)) {
        return false;
    }

    // Get entity handle for item (static pool for resources)
    EntityHandle itemHandle = edm.getStaticHandle(itemIdx);
    if (!itemHandle.isValid()) {
        return false;
    }

    // Validate item still alive (static pool accessor)
    const auto& hot = edm.getStaticHotDataByIndex(itemIdx);
    if (!hot.isAlive() || hot.kind != EntityKind::DroppedItem) {
        return false;
    }

    // Get item data via handle
    const auto& itemData = edm.getItemData(itemHandle);
    if (itemData.quantity <= 0) {
        return false;
    }

    // Try to add to player inventory
    uint32_t playerInvIdx = player->getInventoryIndex();
    if (playerInvIdx == INVALID_INVENTORY_INDEX) {
        ITEM_WARN("Player has no inventory");
        return false;
    }

    if (!edm.addToInventory(playerInvIdx, itemData.resourceHandle, itemData.quantity)) {
        ITEM_DEBUG("Inventory full, cannot pick up item");
        return false;
    }

    // Destroy the item (auto-unregisters from WRM via freeSlot)
    edm.destroyEntity(itemHandle);

    ITEM_INFO(std::format("Picked up {} x{}", itemData.resourceHandle.toString(), itemData.quantity));

    return true;
}
