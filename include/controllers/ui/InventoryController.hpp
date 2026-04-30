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
class HudController;
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
    void handleHotbarAssignmentInput(HudController& hudController);
    void toggleInventoryDisplay();
    void setInventoryVisible(bool visible);
    [[nodiscard]] bool isInventoryVisible() const { return m_inventoryVisible; }

    static constexpr float PICKUP_RADIUS = 32.0f;

    static constexpr const char* INVENTORY_PANEL_ID = "inventory_panel";
    static constexpr const char* INVENTORY_TITLE_ID = "inventory_title";
    static constexpr const char* INVENTORY_STATUS_ID = "inventory_status";
    static constexpr const char* EVENT_LOG_ID = "event_log";

private:
    struct InventoryGridEntry {
        std::string name{};
        VoidLight::ResourceHandle handle{};
        int quantity{0};
    };

    void onResourceChange(const EventData& data);
    void addInventoryEventLogEntry(const VoidLight::ResourceHandle& handle, int delta);
    void refreshSlot(size_t slotIndex, const InventoryGridEntry* entry);
    void refreshGearUI();
    void refreshGearSlot(size_t slotIndex);
    void handleInventorySlotClicked(size_t slotIndex);
    void handleGearSlotClicked(size_t slotIndex);
    bool startHotbarAssignment(size_t slotIndex);
    void cancelHotbarAssignment();
    void updateDragGhost(const VoidLight::ResourceHandle& handle, bool visible);
    [[nodiscard]] int findInventorySlotAtMouse() const;
    [[nodiscard]] int findHotbarSlotAtMouse() const;

    [[nodiscard]] static bool isEquipment(const VoidLight::ResourceHandle& handle);
    [[nodiscard]] static bool isWeapon(const VoidLight::ResourceHandle& handle);
    [[nodiscard]] static bool isHotbarAssignable(const VoidLight::ResourceHandle& handle);
    [[nodiscard]] static std::string displayNameFor(const VoidLight::ResourceHandle& handle);

    [[nodiscard]] static std::string slotId(size_t slotIndex);
    [[nodiscard]] static std::string iconId(size_t slotIndex);
    [[nodiscard]] static std::string countId(size_t slotIndex);
    [[nodiscard]] static std::string gearSlotId(size_t slotIndex);
    [[nodiscard]] static std::string gearIconId(size_t slotIndex);
    [[nodiscard]] static std::string gearLabelId(size_t slotIndex);

    std::weak_ptr<Player> mp_player;
    bool m_inventoryVisible{false};
    bool m_inventoryUICreated{false};
    bool m_leftMouseWasDown{false};
    bool m_draggingHotbarAssignment{false};
    bool m_dragGhostCreated{false};
    int m_draggedHotbarSourceSlot{-1};
    VoidLight::ResourceHandle m_pendingHotbarAssignment{};
    VoidLight::ResourceHandle m_draggedHotbarAssignment{};
    std::vector<InventoryGridEntry> m_gridEntries;
    std::string m_resourceNameBuffer;
};

#endif // INVENTORY_CONTROLLER_HPP
