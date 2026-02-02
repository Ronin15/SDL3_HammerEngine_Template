/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ITEM_CONTROLLER_HPP
#define ITEM_CONTROLLER_HPP

/**
 * @file ItemController.hpp
 * @brief Controller for item pickup and inventory UI synchronization
 *
 * ItemController handles:
 * - On-demand item pickup when player presses E (via WRM spatial query)
 * - Inventory UI synchronization via ResourceChangeEvent subscription
 * - Event log notifications for inventory changes
 *
 * NO per-frame polling - queries only when interaction is attempted.
 * UI updates are event-driven via ResourceChangeEvent.
 *
 * NOTE: Harvesting has been moved to HarvestController for progress-based
 * harvesting with type-specific durations.
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include "managers/EventManager.hpp"
#include <memory>
#include <string>

// Forward declarations
class Player;

class ItemController : public ControllerBase {
public:
    /**
     * @brief Construct ItemController with required player reference
     * @param player Shared pointer to the player (required)
     */
    explicit ItemController(std::shared_ptr<Player> player)
        : mp_player(std::move(player)) {}

    ~ItemController() override = default;

    // Movable (inherited from base)
    ItemController(ItemController&&) noexcept = default;
    ItemController& operator=(ItemController&&) noexcept = default;

    // --- ControllerBase interface ---

    void subscribe() override;

    [[nodiscard]] std::string_view getName() const override { return "ItemController"; }

    // --- Interaction API (on-demand, no per-frame cost) ---

    /**
     * @brief Attempt to pick up the closest item
     * @return true if item was picked up, false otherwise
     *
     * Queries WRM for closest dropped item within pickup radius,
     * validates it's still alive, transfers to player inventory,
     * then destroys the item entity.
     */
    bool attemptPickup();

    // Configuration constants
    static constexpr float PICKUP_RADIUS = 32.0f;

    // UI component IDs for inventory binding refresh
    static constexpr const char* INVENTORY_STATUS_ID = "gameplay_inventory_status";
    static constexpr const char* INVENTORY_LIST_ID = "gameplay_inventory_list";
    static constexpr const char* EVENT_LOG_ID = "gameplay_event_log";

private:
    /**
     * @brief Handle ResourceChangeEvent for inventory UI synchronization
     * @param data Event data containing resource change info
     *
     * Marks inventory UI bindings as dirty and adds event log notification.
     */
    void onResourceChange(const EventData& data);

    std::weak_ptr<Player> mp_player;

    // Cached resource name for event log (avoids per-event allocation)
    std::string m_resourceNameBuffer;
};

#endif // ITEM_CONTROLLER_HPP
