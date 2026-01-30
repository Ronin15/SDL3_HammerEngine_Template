/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ITEM_CONTROLLER_HPP
#define ITEM_CONTROLLER_HPP

/**
 * @file ItemController.hpp
 * @brief Controller for item pickup and harvestable interactions
 *
 * ItemController handles on-demand interaction when player presses E:
 * - Dropped item pickup via WRM spatial query
 * - Harvestable interaction (spawn drops, mark depleted)
 *
 * NO per-frame polling - queries only when interaction is attempted.
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include <memory>

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

    /**
     * @brief Attempt to harvest the closest harvestable
     * @return true if harvest was successful, false otherwise
     *
     * Queries WRM for closest harvestable within harvest radius,
     * validates it's not depleted, spawns yield as dropped items,
     * marks harvestable as depleted.
     */
    bool attemptHarvest();

    // Configuration constants
    static constexpr float PICKUP_RADIUS = 32.0f;
    static constexpr float HARVEST_RADIUS = 48.0f;

private:
    std::weak_ptr<Player> mp_player;
};

#endif // ITEM_CONTROLLER_HPP
