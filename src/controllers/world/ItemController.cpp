/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/world/ItemController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "entities/Resource.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <format>

void ItemController::subscribe() {
    if (checkAlreadySubscribed()) {
        return;
    }

    auto& eventMgr = EventManager::Instance();

    // Subscribe to ResourceChangeEvent for inventory UI synchronization
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::ResourceChange,
        [this](const EventData& data) { onResourceChange(data); }
    );
    addHandlerToken(token);

    setSubscribed(true);
    ITEM_DEBUG("ItemController subscribed to ResourceChangeEvent");
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

    const int oldQuantity = edm.getInventoryQuantity(playerInvIdx, itemData.resourceHandle);
    if (!edm.addToInventory(playerInvIdx, itemData.resourceHandle, itemData.quantity)) {
        ITEM_DEBUG("Inventory full, cannot pick up item");
        return false;
    }
    const int newQuantity = oldQuantity + itemData.quantity;
    EventManager::Instance().triggerResourceChange(
        player->getHandle(), itemData.resourceHandle, oldQuantity, newQuantity,
        "picked_up");

    // Destroy the item (auto-unregisters from WRM via freeSlot)
    edm.destroyEntity(itemHandle);

    ITEM_INFO(std::format("Picked up {} x{}", itemData.resourceHandle.toString(), itemData.quantity));

    return true;
}

void ItemController::onResourceChange(const EventData& data) {
    // Cast to ResourceChangeEvent to get details
    const auto* event = dynamic_cast<const ResourceChangeEvent*>(data.event.get());
    if (!event) {
        return;
    }

    // Only process if this is for the player's inventory
    auto player = mp_player.lock();
    if (!player || event->getOwnerHandle() != player->getHandle()) {
        return;
    }

    auto& ui = UIManager::Instance();

    // Mark inventory UI bindings as dirty for refresh
    ui.markBindingDirty(INVENTORY_STATUS_ID);
    ui.markBindingDirty(INVENTORY_LIST_ID);

    // Add event log notification for inventory changes
    int delta = event->getQuantityChange();
    if (delta != 0) {
        // Get resource display name (prefer template name, fallback to handle string)
        const auto& rtm = ResourceTemplateManager::Instance();
        auto resourceTemplate = rtm.getResourceTemplate(event->getResourceHandle());

        // Reuse buffer to avoid per-event allocation
        if (resourceTemplate) {
            m_resourceNameBuffer = delta > 0
                ? std::format("+{} {}", delta, resourceTemplate->getName())
                : std::format("{} {}", delta, resourceTemplate->getName());
        } else {
            m_resourceNameBuffer = delta > 0
                ? std::format("+{} {}", delta, event->getResourceHandle().toString())
                : std::format("{} {}", delta, event->getResourceHandle().toString());
        }

        ui.addEventLogEntry(EVENT_LOG_ID, m_resourceNameBuffer);
    }
}
