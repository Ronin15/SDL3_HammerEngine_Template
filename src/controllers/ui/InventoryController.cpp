/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "controllers/ui/InventoryController.hpp"
#include "core/Logger.hpp"
#include "entities/Player.hpp"
#include "entities/Resource.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/UIConstants.hpp"
#include "managers/UIManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include <algorithm>
#include <format>

namespace {

constexpr int INVENTORY_GRID_COLUMNS = 5;
constexpr int INVENTORY_GRID_ROWS = 4;
constexpr int INVENTORY_SLOT_COUNT = INVENTORY_GRID_COLUMNS * INVENTORY_GRID_ROWS;
constexpr int INVENTORY_SLOT_SIZE = 40;
constexpr int INVENTORY_SLOT_GAP = 8;
constexpr int INVENTORY_ICON_INSET = 4;
constexpr int INVENTORY_ICON_SIZE = INVENTORY_SLOT_SIZE - (INVENTORY_ICON_INSET * 2);
constexpr int INVENTORY_GRID_WIDTH =
    (INVENTORY_GRID_COLUMNS * INVENTORY_SLOT_SIZE) +
    ((INVENTORY_GRID_COLUMNS - 1) * INVENTORY_SLOT_GAP);
constexpr int INVENTORY_GRID_HEIGHT =
    (INVENTORY_GRID_ROWS * INVENTORY_SLOT_SIZE) +
    ((INVENTORY_GRID_ROWS - 1) * INVENTORY_SLOT_GAP);
constexpr int INVENTORY_PANEL_WIDTH = 280;
constexpr int INVENTORY_PANEL_MARGIN_RIGHT = 20;
constexpr int INVENTORY_PANEL_MARGIN_TOP = 170;
constexpr int INVENTORY_CHILD_INSET = 10;
constexpr int INVENTORY_HEADER_HEIGHT = 110;
constexpr int INVENTORY_BOTTOM_PADDING = 15;
constexpr std::string_view INVENTORY_ATLAS_TEXTURE_ID = "atlas";

} // namespace

void InventoryController::subscribe() {
    if (checkAlreadySubscribed()) {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    auto token = eventMgr.registerHandlerWithToken(
        EventTypeId::ResourceChange,
        [this](const EventData& data) { onResourceChange(data); });
    addHandlerToken(token);

    setSubscribed(true);
    INVENTORY_CONTROLLER_DEBUG("Subscribed to ResourceChangeEvent");
}

bool InventoryController::attemptPickup() {
    auto player = mp_player.lock();
    if (!player) {
        return false;
    }

    auto& wrm = WorldResourceManager::Instance();
    auto& edm = EntityDataManager::Instance();

    size_t itemIdx;
    if (!wrm.findClosestDroppedItem(player->getPosition(), PICKUP_RADIUS, itemIdx)) {
        return false;
    }

    EntityHandle itemHandle = edm.getStaticHandle(itemIdx);
    if (!itemHandle.isValid()) {
        return false;
    }

    const auto& hot = edm.getStaticHotDataByIndex(itemIdx);
    if (!hot.isAlive() || hot.kind != EntityKind::DroppedItem) {
        return false;
    }

    const auto& itemData = edm.getItemData(itemHandle);
    if (itemData.quantity <= 0) {
        return false;
    }

    const uint32_t playerInvIdx = player->getInventoryIndex();
    if (playerInvIdx == INVALID_INVENTORY_INDEX) {
        INVENTORY_CONTROLLER_WARN("Player has no inventory");
        return false;
    }

    const int oldQuantity = edm.getInventoryQuantity(playerInvIdx, itemData.resourceHandle);
    if (!edm.addToInventory(playerInvIdx, itemData.resourceHandle, itemData.quantity)) {
        INVENTORY_CONTROLLER_DEBUG("Inventory full, cannot pick up item");
        return false;
    }

    const int newQuantity = oldQuantity + itemData.quantity;
    EventManager::Instance().triggerResourceChange(
        player->getHandle(), itemData.resourceHandle, oldQuantity, newQuantity,
        "picked_up");

    edm.destroyEntity(itemHandle);

    INVENTORY_CONTROLLER_INFO(std::format("Picked up {} x{}", itemData.resourceHandle.toString(),
                                          itemData.quantity));
    return true;
}

void InventoryController::initializeInventoryUI() {
    if (m_inventoryUICreated) {
        refreshInventoryUI();
        return;
    }

    auto player = mp_player.lock();
    if (!player) {
        return;
    }

    auto& ui = UIManager::Instance();
    const int windowWidth = ui.getWidthInPixels();
    constexpr int childWidth = INVENTORY_PANEL_WIDTH - (INVENTORY_CHILD_INSET * 2);
    constexpr int inventoryHeight =
        INVENTORY_HEADER_HEIGHT + INVENTORY_GRID_HEIGHT + INVENTORY_BOTTOM_PADDING;
    const int inventoryX = windowWidth - INVENTORY_PANEL_WIDTH - INVENTORY_PANEL_MARGIN_RIGHT;
    constexpr int inventoryY = INVENTORY_PANEL_MARGIN_TOP;
    constexpr int gridOffsetX = INVENTORY_CHILD_INSET +
        ((childWidth - INVENTORY_GRID_WIDTH) / 2);
    constexpr int gridOffsetY = 110;

    ui.createPanel(INVENTORY_PANEL_ID,
                   {inventoryX, inventoryY, INVENTORY_PANEL_WIDTH, inventoryHeight});
    ui.setComponentPositioning(
        INVENTORY_PANEL_ID,
        {UIPositionMode::TOP_RIGHT, INVENTORY_PANEL_MARGIN_RIGHT,
         INVENTORY_PANEL_MARGIN_TOP, INVENTORY_PANEL_WIDTH, inventoryHeight});

    ui.createTitle(INVENTORY_TITLE_ID,
                   {inventoryX + INVENTORY_CHILD_INSET, inventoryY + 25, childWidth, 35},
                   "Player Inventory", INVENTORY_PANEL_ID);
    ui.setTitleAlignment(INVENTORY_TITLE_ID, UIAlignment::CENTER_CENTER);
    ui.setComponentPositioning(
        INVENTORY_TITLE_ID,
        {UIPositionMode::TOP_RIGHT, INVENTORY_PANEL_MARGIN_RIGHT + INVENTORY_CHILD_INSET,
         INVENTORY_PANEL_MARGIN_TOP + 25, childWidth, 35});

    ui.createLabel(INVENTORY_STATUS_ID,
                   {inventoryX + INVENTORY_CHILD_INSET, inventoryY + 75, childWidth, 25},
                   "Capacity: 0/20", INVENTORY_PANEL_ID);
    ui.setComponentPositioning(
        INVENTORY_STATUS_ID,
        {UIPositionMode::TOP_RIGHT, INVENTORY_PANEL_MARGIN_RIGHT + INVENTORY_CHILD_INSET,
         INVENTORY_PANEL_MARGIN_TOP + 75, childWidth, 25});

    UIStyle slotStyle;
    slotStyle.backgroundColor = {.r=20, .g=24, .b=30, .a=190};
    slotStyle.borderColor = {.r=95, .g=115, .b=135, .a=210};
    slotStyle.hoverColor = {.r=42, .g=66, .b=92, .a=230};
    slotStyle.pressedColor = {.r=60, .g=88, .b=118, .a=240};
    slotStyle.borderWidth = 1;
    slotStyle.textAlign = UIAlignment::CENTER_CENTER;

    UIStyle countStyle;
    countStyle.backgroundColor = {.r=0, .g=0, .b=0, .a=0};
    countStyle.textColor = {.r=255, .g=255, .b=255, .a=255};
    countStyle.textAlign = UIAlignment::CENTER_RIGHT;
    countStyle.fontID = UIConstants::FONT_UI;
    countStyle.useTextBackground = true;
    countStyle.textBackgroundColor = {.r=0, .g=0, .b=0, .a=150};
    countStyle.textBackgroundPadding = 2;

    for (int slot = 0; slot < INVENTORY_SLOT_COUNT; ++slot) {
        const int col = slot % INVENTORY_GRID_COLUMNS;
        const int row = slot / INVENTORY_GRID_COLUMNS;
        const int slotX = inventoryX + gridOffsetX +
            col * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP);
        const int slotY = inventoryY + gridOffsetY +
            row * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP);
        const int posRightOffset = INVENTORY_PANEL_MARGIN_RIGHT + gridOffsetX +
            col * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP);
        const int posTopOffset = INVENTORY_PANEL_MARGIN_TOP + gridOffsetY +
            row * (INVENTORY_SLOT_SIZE + INVENTORY_SLOT_GAP);

        const std::string slotComponentId = slotId(static_cast<size_t>(slot));
        ui.createButton(slotComponentId,
                        {slotX, slotY, INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE},
                        "", INVENTORY_PANEL_ID);
        ui.setStyle(slotComponentId, slotStyle);
        ui.setComponentPositioning(
            slotComponentId,
            {UIPositionMode::TOP_RIGHT, posRightOffset, posTopOffset,
             INVENTORY_SLOT_SIZE, INVENTORY_SLOT_SIZE});

        const std::string iconComponentId = iconId(static_cast<size_t>(slot));
        ui.createAtlasImage(
            iconComponentId,
            {slotX + INVENTORY_ICON_INSET, slotY + INVENTORY_ICON_INSET,
             INVENTORY_ICON_SIZE, INVENTORY_ICON_SIZE},
            "", UIRect{}, slotComponentId);
        ui.setComponentPositioning(
            iconComponentId,
            {UIPositionMode::TOP_RIGHT, posRightOffset + INVENTORY_ICON_INSET,
             posTopOffset + INVENTORY_ICON_INSET, INVENTORY_ICON_SIZE,
             INVENTORY_ICON_SIZE});

        const std::string countComponentId = countId(static_cast<size_t>(slot));
        ui.createLabel(countComponentId,
                       {slotX + 2, slotY + INVENTORY_SLOT_SIZE - 16,
                        INVENTORY_SLOT_SIZE - 4, 14},
                       "", slotComponentId);
        ui.setStyle(countComponentId, countStyle);
        ui.setComponentPositioning(
            countComponentId,
            {UIPositionMode::TOP_RIGHT, posRightOffset + 2,
             posTopOffset + INVENTORY_SLOT_SIZE - 16, INVENTORY_SLOT_SIZE - 4, 14});
    }

    m_inventoryUICreated = true;
    refreshInventoryUI();
    ui.setComponentVisible(INVENTORY_PANEL_ID, false);
}

void InventoryController::refreshInventoryUI() {
    if (!m_inventoryUICreated) {
        return;
    }

    auto player = mp_player.lock();
    if (!player) {
        return;
    }

    auto& ui = UIManager::Instance();
    const uint32_t invIdx = player->getInventoryIndex();
    if (invIdx == INVALID_INVENTORY_INDEX) {
        ui.setText(INVENTORY_STATUS_ID, "Capacity: 0/0");
        for (size_t i = 0; i < INVENTORY_SLOT_COUNT; ++i) {
            refreshSlot(i, nullptr);
        }
        return;
    }

    const auto& inv = EntityDataManager::Instance().getInventoryData(invIdx);
    ui.setText(INVENTORY_STATUS_ID,
               std::format("Capacity: {}/{}", inv.usedSlots, inv.maxSlots));

    auto allResources = EntityDataManager::Instance().getInventoryResources(invIdx);
    m_gridEntries.clear();
    m_gridEntries.reserve(allResources.size());

    for (const auto& [resourceHandle, quantity] : allResources) {
        if (quantity <= 0) {
            continue;
        }

        auto resourceTemplate =
            ResourceTemplateManager::Instance().getResourceTemplate(resourceHandle);
        m_gridEntries.push_back({
            .name = resourceTemplate ? resourceTemplate->getName() : resourceHandle.toString(),
            .handle = resourceHandle,
            .quantity = quantity});
    }

    std::sort(m_gridEntries.begin(), m_gridEntries.end(),
              [](const InventoryGridEntry& lhs, const InventoryGridEntry& rhs) {
                  return lhs.name < rhs.name;
              });

    for (size_t i = 0; i < INVENTORY_SLOT_COUNT; ++i) {
        refreshSlot(i, i < m_gridEntries.size() ? &m_gridEntries[i] : nullptr);
    }
}

void InventoryController::toggleInventoryDisplay() {
    setInventoryVisible(!m_inventoryVisible);
}

void InventoryController::setInventoryVisible(bool visible) {
    m_inventoryVisible = visible;
    UIManager::Instance().setComponentVisible(INVENTORY_PANEL_ID, visible);
    if (visible) {
        refreshInventoryUI();
    }
}

void InventoryController::onResourceChange(const EventData& data) {
    const auto* event = dynamic_cast<const ResourceChangeEvent*>(data.event.get());
    if (!event) {
        return;
    }

    auto player = mp_player.lock();
    if (!player || event->getOwnerHandle() != player->getHandle()) {
        return;
    }

    refreshInventoryUI();
    addInventoryEventLogEntry(event->getResourceHandle(), event->getQuantityChange());
}

void InventoryController::addInventoryEventLogEntry(const VoidLight::ResourceHandle& handle,
                                                    int delta) {
    if (delta == 0) {
        return;
    }

    const auto& rtm = ResourceTemplateManager::Instance();
    auto resourceTemplate = rtm.getResourceTemplate(handle);
    const std::string displayName =
        resourceTemplate ? resourceTemplate->getName() : handle.toString();

    m_resourceNameBuffer = delta > 0
        ? std::format("+{} {}", delta, displayName)
        : std::format("{} {}", delta, displayName);
    UIManager::Instance().addEventLogEntry(EVENT_LOG_ID, m_resourceNameBuffer);
}

void InventoryController::refreshSlot(size_t slotIndex,
                                      const InventoryGridEntry* entry) {
    auto& ui = UIManager::Instance();
    const std::string iconComponentId = iconId(slotIndex);
    const std::string countComponentId = countId(slotIndex);

    if (!entry) {
        ui.setTexture(iconComponentId, "");
        ui.setImageSourceRect(iconComponentId, UIRect{});
        ui.setText(countComponentId, "");
        return;
    }

    auto resourceTemplate =
        ResourceTemplateManager::Instance().getResourceTemplate(entry->handle);
    if (!resourceTemplate || resourceTemplate->getAtlasW() <= 0 ||
        resourceTemplate->getAtlasH() <= 0) {
        ui.setTexture(iconComponentId, "");
        ui.setImageSourceRect(iconComponentId, UIRect{});
    } else {
        ui.setTexture(iconComponentId, std::string(INVENTORY_ATLAS_TEXTURE_ID));
        ui.setImageSourceRect(
            iconComponentId,
            UIRect{resourceTemplate->getAtlasX(), resourceTemplate->getAtlasY(),
                   resourceTemplate->getAtlasW(), resourceTemplate->getAtlasH()});
    }

    ui.setText(countComponentId, std::format("{}", entry->quantity));
}

std::string InventoryController::slotId(size_t slotIndex) {
    return std::format("gameplay_inventory_slot_{}", slotIndex);
}

std::string InventoryController::iconId(size_t slotIndex) {
    return std::format("gameplay_inventory_icon_{}", slotIndex);
}

std::string InventoryController::countId(size_t slotIndex) {
    return std::format("gameplay_inventory_count_{}", slotIndex);
}
