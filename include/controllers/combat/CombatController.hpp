/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COMBAT_CONTROLLER_HPP
#define COMBAT_CONTROLLER_HPP

/**
 * @file CombatController.hpp
 * @brief Frame-updatable controller for player combat mechanics
 *
 * CombatController handles:
 * - Attack execution and cooldowns
 * - Stamina consumption and regeneration
 * - Target tracking for UI display
 * - Hit detection against NPCs (via AIManager)
 *
 * This is a frame-updatable controller (implements IUpdatable) because it
 * manages per-frame state: attack cooldowns, stamina regen, target timers.
 *
 * Ownership: ControllerRegistry owns the controller instance.
 */

#include "controllers/ControllerBase.hpp"
#include "controllers/IUpdatable.hpp"
#include <memory>
#include <vector>

// Forward declarations
class Player;
class Entity;
class NPC;

class CombatController : public ControllerBase, public IUpdatable
{
public:
    CombatController() = default;
    ~CombatController() override = default;

    // Movable (inherited from base)
    CombatController(CombatController&&) noexcept = default;
    CombatController& operator=(CombatController&&) noexcept = default;

    // --- ControllerBase interface ---

    /**
     * @brief Subscribe to combat-related events
     * @note Called by ControllerRegistry::subscribeAll()
     */
    void subscribe() override;

    /**
     * @brief Get controller name for debugging
     * @return "CombatController"
     */
    [[nodiscard]] std::string_view getName() const override { return "CombatController"; }

    // --- IUpdatable interface ---

    /**
     * @brief Update combat state (cooldowns, stamina regen, target timer)
     * @param deltaTime Frame delta time in seconds
     * @note Called by ControllerRegistry::updateAll()
     */
    void update(float deltaTime) override;

    // --- Configuration ---

    /**
     * @brief Set the player reference for combat operations
     * @param player Shared pointer to the player
     * @note Must be called after add() and before subscribeAll()
     */
    void setPlayer(std::shared_ptr<Player> player) { mp_player = player; }

    // --- Combat operations ---

    /**
     * @brief Attempt to perform an attack
     * @return true if attack was performed, false if blocked (cooldown, no stamina)
     * @note Uses AIManager::queryEntitiesInRadius() for hit detection
     */
    bool tryAttack();

    /**
     * @brief Get the currently targeted NPC (for UI display)
     * @return Shared pointer to targeted NPC, or nullptr if no target/expired
     */
    [[nodiscard]] std::shared_ptr<NPC> getTargetedNPC() const;

    /**
     * @brief Get remaining time for target display
     * @return Seconds remaining before target frame hides
     */
    [[nodiscard]] float getTargetDisplayTimer() const { return m_targetDisplayTimer; }

    /**
     * @brief Check if there's an active target to display
     * @return true if target frame should be visible
     */
    [[nodiscard]] bool hasActiveTarget() const;

    // Configuration constants
    static constexpr float ATTACK_STAMINA_COST{10.0f};
    static constexpr float STAMINA_REGEN_RATE{15.0f};     // per second
    static constexpr float TARGET_DISPLAY_DURATION{3.0f}; // seconds after last hit
    static constexpr float ATTACK_COOLDOWN{0.5f};         // seconds between attacks

private:
    /**
     * @brief Execute the attack and detect hits
     * @param player Raw pointer to player (from locked weak_ptr)
     */
    void performAttack(Player* player);

    /**
     * @brief Regenerate player stamina over time
     * @param player Raw pointer to player (from locked weak_ptr)
     * @param deltaTime Frame delta time in seconds
     */
    void regenerateStamina(Player* player, float deltaTime);

    /**
     * @brief Update target display timer
     * @param deltaTime Frame delta time in seconds
     */
    void updateTargetTimer(float deltaTime);

    // Player reference (set via setPlayer())
    std::weak_ptr<Player> mp_player;

    // Target tracking
    std::weak_ptr<NPC> m_targetedNPC; // Safe weak reference, doesn't extend lifetime
    float m_targetDisplayTimer{0.0f};

    // Attack timing
    float m_attackCooldown{0.0f};
};

#endif // COMBAT_CONTROLLER_HPP
