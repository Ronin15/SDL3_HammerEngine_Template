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
 * ItemController handles:
 * - Dropped item proximity detection via WRM spatial queries
 * - Pickup execution (transfer to player inventory via EDM)
 * - Harvestable interaction (check yield, spawn drops)
 * - UI hint tracking (closest interactable for prompt display)
 *
 * This is a frame-updatable controller (implements IUpdatable) because it
 * tracks closest interactables each frame for UI hints.
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include <cstddef>
#include <limits>
#include <memory>

// Forward declarations
class Player;

class ItemController : public ControllerBase, public IUpdatable {
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

    /**
     * @brief Subscribe to item-related events (if any)
     * @note Called by ControllerRegistry::subscribeAll()
     */
    void subscribe() override;

    /**
     * @brief Get controller name for debugging
     * @return "ItemController"
     */
    [[nodiscard]] std::string_view getName() const override { return "ItemController"; }

    // --- IUpdatable interface ---

    /**
     * @brief Update proximity tracking for UI hints
     * @param deltaTime Frame delta time in seconds
     * @note Called by ControllerRegistry::updateAll()
     */
    void update(float deltaTime) override;

    // --- Interaction API ---

    /**
     * @brief Attempt to pick up the closest item
     * @return true if item was picked up, false otherwise
     *
     * Checks for closest dropped item within pickup radius,
     * validates it's still alive, transfers to player inventory,
     * then destroys the item entity.
     */
    bool attemptPickup();

    /**
     * @brief Attempt to harvest the closest harvestable
     * @return true if harvest was successful, false otherwise
     *
     * Checks for closest harvestable within harvest radius,
     * validates it's not depleted, spawns yield as dropped items,
     * marks harvestable as depleted.
     */
    bool attemptHarvest();

    /**
     * @brief Check if there's a nearby interactable (item or harvestable)
     * @return true if player is near something interactable
     */
    [[nodiscard]] bool hasNearbyInteractable() const;

    /**
     * @brief Check if there's a nearby dropped item
     * @return true if within pickup radius of an item
     */
    [[nodiscard]] bool hasNearbyItem() const {
        return m_closestItemIdx != INVALID_INDEX;
    }

    /**
     * @brief Check if there's a nearby harvestable
     * @return true if within harvest radius of a harvestable
     */
    [[nodiscard]] bool hasNearbyHarvestable() const {
        return m_closestHarvestableIdx != INVALID_INDEX;
    }

    // Configuration constants
    static constexpr float PICKUP_RADIUS = 32.0f;
    static constexpr float HARVEST_RADIUS = 48.0f;
    static constexpr size_t INVALID_INDEX = std::numeric_limits<size_t>::max();

private:
    std::weak_ptr<Player> mp_player;

    // Closest interactables (updated each frame)
    size_t m_closestItemIdx{INVALID_INDEX};
    size_t m_closestHarvestableIdx{INVALID_INDEX};
};

#endif // ITEM_CONTROLLER_HPP
