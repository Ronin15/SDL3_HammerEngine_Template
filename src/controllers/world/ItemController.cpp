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
#include <random>

void ItemController::subscribe() {
    // No event subscriptions needed currently
    // Could subscribe to input events if we want controller to handle input directly
    ITEM_DEBUG("ItemController subscribed");
}

void ItemController::update(float /*deltaTime*/) {
    auto player = mp_player.lock();
    if (!player) {
        m_closestItemIdx = INVALID_INDEX;
        m_closestHarvestableIdx = INVALID_INDEX;
        return;
    }

    auto& wrm = WorldResourceManager::Instance();
    Vector2D playerPos = player->getPosition();

    // Update closest item
    if (!wrm.findClosestDroppedItem(playerPos, PICKUP_RADIUS, m_closestItemIdx)) {
        m_closestItemIdx = INVALID_INDEX;
    }

    // Update closest harvestable
    std::vector<size_t> harvestables;
    harvestables.reserve(8);
    if (wrm.queryHarvestablesInRadius(playerPos, HARVEST_RADIUS, harvestables) > 0) {
        // Find closest non-depleted harvestable
        auto& edm = EntityDataManager::Instance();
        float closestDistSq = std::numeric_limits<float>::max();
        m_closestHarvestableIdx = INVALID_INDEX;

        for (size_t idx : harvestables) {
            const auto& hot = edm.getHotDataByIndex(idx);
            if (!hot.isAlive() || hot.kind != EntityKind::Harvestable) {
                continue;
            }

            const auto& harvestData = edm.getHarvestableData(hot.typeLocalIndex);
            if (harvestData.isDepleted) {
                continue;
            }

            const auto& pos = hot.transform.position;
            float dx = pos.getX() - playerPos.getX();
            float dy = pos.getY() - playerPos.getY();
            float distSq = dx * dx + dy * dy;

            if (distSq < closestDistSq) {
                closestDistSq = distSq;
                m_closestHarvestableIdx = idx;
            }
        }
    } else {
        m_closestHarvestableIdx = INVALID_INDEX;
    }
}

bool ItemController::attemptPickup() {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    auto& wrm = WorldResourceManager::Instance();
    auto& edm = EntityDataManager::Instance();

    Vector2D playerPos = player->getPosition();

    // Find closest item
    size_t itemIdx;
    if (!wrm.findClosestDroppedItem(playerPos, PICKUP_RADIUS, itemIdx)) {
        return false;
    }

    // Get entity handle for item
    EntityHandle itemHandle = edm.getHandle(itemIdx);
    if (!itemHandle.isValid()) {
        return false;
    }

    // Validate item still alive
    const auto& hot = edm.getHotDataByIndex(itemIdx);
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

    // Clear cached index since item is gone
    m_closestItemIdx = INVALID_INDEX;

    return true;
}

bool ItemController::attemptHarvest() {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    if (m_closestHarvestableIdx == INVALID_INDEX) {
        return false;
    }

    auto& edm = EntityDataManager::Instance();

    // Validate harvestable still valid
    const auto& hot = edm.getHotDataByIndex(m_closestHarvestableIdx);
    if (!hot.isAlive() || hot.kind != EntityKind::Harvestable) {
        m_closestHarvestableIdx = INVALID_INDEX;
        return false;
    }

    // Get harvestable data
    auto& harvestData = edm.getHarvestableData(hot.typeLocalIndex);
    if (harvestData.isDepleted) {
        return false;
    }

    // Calculate yield
    int yield = harvestData.yieldMin;
    if (harvestData.yieldMax > harvestData.yieldMin) {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(harvestData.yieldMin, harvestData.yieldMax);
        yield = dist(rng);
    }

    // Try to add directly to player inventory
    uint32_t playerInvIdx = player->getInventoryIndex();
    bool addedToInventory = false;

    if (playerInvIdx != INVALID_INVENTORY_INDEX) {
        addedToInventory = edm.addToInventory(playerInvIdx, harvestData.yieldResource, yield);
    }

    // If inventory full, spawn as dropped item
    if (!addedToInventory) {
        // Spawn slightly offset from harvestable position
        Vector2D spawnPos = hot.transform.position;
        spawnPos.setX(spawnPos.getX() + 16.0f);

        auto& wrm = WorldResourceManager::Instance();
        const std::string& worldId = wrm.getActiveWorld();

        edm.createDroppedItem(spawnPos, harvestData.yieldResource, yield, worldId);
        ITEM_INFO(std::format("Harvested {} x{} (dropped)", harvestData.yieldResource.toString(), yield));
    } else {
        ITEM_INFO(std::format("Harvested {} x{}", harvestData.yieldResource.toString(), yield));
    }

    // Mark harvestable as depleted
    harvestData.isDepleted = true;
    harvestData.currentRespawn = harvestData.respawnTime;

    m_closestHarvestableIdx = INVALID_INDEX;

    return true;
}

bool ItemController::hasNearbyInteractable() const {
    return hasNearbyItem() || hasNearbyHarvestable();
}
