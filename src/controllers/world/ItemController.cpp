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
    // No event subscriptions needed - purely on-demand queries
    ITEM_DEBUG("ItemController subscribed");
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

    return true;
}

bool ItemController::attemptHarvest() {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    auto& wrm = WorldResourceManager::Instance();
    auto& edm = EntityDataManager::Instance();

    Vector2D playerPos = player->getPosition();

    // Query harvestables on-demand
    std::vector<size_t> harvestables;
    if (wrm.queryHarvestablesInRadius(playerPos, HARVEST_RADIUS, harvestables) == 0) {
        return false;
    }

    // Find closest non-depleted harvestable
    float closestDistSq = std::numeric_limits<float>::max();
    size_t closestIdx = std::numeric_limits<size_t>::max();

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
            closestIdx = idx;
        }
    }

    if (closestIdx == std::numeric_limits<size_t>::max()) {
        return false;
    }

    // Validate harvestable still valid
    const auto& hot = edm.getHotDataByIndex(closestIdx);
    if (!hot.isAlive() || hot.kind != EntityKind::Harvestable) {
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

        const std::string& worldId = wrm.getActiveWorld();

        edm.createDroppedItem(spawnPos, harvestData.yieldResource, yield, worldId);
        ITEM_INFO(std::format("Harvested {} x{} (dropped)", harvestData.yieldResource.toString(), yield));
    } else {
        ITEM_INFO(std::format("Harvested {} x{}", harvestData.yieldResource.toString(), yield));
    }

    // Mark harvestable as depleted
    harvestData.isDepleted = true;
    harvestData.currentRespawn = harvestData.respawnTime;

    return true;
}
