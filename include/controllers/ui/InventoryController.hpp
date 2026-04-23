/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef INVENTORY_CONTROLLER_HPP
#define INVENTORY_CONTROLLER_HPP

/**
 * @file InventoryController.hpp
 * @brief Player inventory interaction and UI controller
 *
 * InventoryController owns the reusable player-inventory behavior that game
 * states need: pickup interaction, inventory panel creation, slot-grid refresh,
 * ResourceChangeEvent synchronization, and event-log notifications.
 */

#include "controllers/ControllerBase.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>
#include <vector>

class Player;
struct UIRect;

class InventoryController : public ControllerBase {
public:
    explicit InventoryController(std::shared_ptr<Player> player)
        : mp_player(std::move(player)) {}

    ~InventoryController() override = default;

    InventoryController(InventoryController&&) noexcept = default;
    InventoryController& operator=(InventoryController&&) noexcept = default;

    void subscribe() override;

    [[nodiscard]] std::string_view getName() const override { return "InventoryController"; }

    bool attemptPickup();

    void initializeInventoryUI();
    void refreshInventoryUI();
    void toggleInventoryDisplay();
    void setInventoryVisible(bool visible);
    [[nodiscard]] bool isInventoryVisible() const { return m_inventoryVisible; }

    static constexpr float PICKUP_RADIUS = 32.0f;

    static constexpr const char* INVENTORY_PANEL_ID = "gameplay_inventory_panel";
    static constexpr const char* INVENTORY_TITLE_ID = "gameplay_inventory_title";
    static constexpr const char* INVENTORY_STATUS_ID = "gameplay_inventory_status";
    static constexpr const char* EVENT_LOG_ID = "gameplay_event_log";

private:
    struct InventoryGridEntry {
        std::string name{};
        VoidLight::ResourceHandle handle{};
        int quantity{0};
    };

    void onResourceChange(const EventData& data);
    void addInventoryEventLogEntry(const VoidLight::ResourceHandle& handle, int delta);
    void refreshSlot(size_t slotIndex, const InventoryGridEntry* entry);

    [[nodiscard]] static std::string slotId(size_t slotIndex);
    [[nodiscard]] static std::string iconId(size_t slotIndex);
    [[nodiscard]] static std::string countId(size_t slotIndex);

    std::weak_ptr<Player> mp_player;
    bool m_inventoryVisible{false};
    bool m_inventoryUICreated{false};
    std::vector<InventoryGridEntry> m_gridEntries;
    std::string m_resourceNameBuffer;
};

#endif // INVENTORY_CONTROLLER_HPP
