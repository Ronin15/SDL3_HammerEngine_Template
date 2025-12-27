/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COMBAT_CONTROLLER_HPP
#define COMBAT_CONTROLLER_HPP

/**
 * @file CombatController.hpp
 * @brief Lightweight controller for player combat mechanics
 *
 * CombatController handles:
 * - Attack execution and cooldowns
 * - Stamina consumption and regeneration
 * - Target tracking for UI display
 * - Hit detection against NPCs (via AIManager)
 *
 * Ownership: GameState owns the controller instance (not a singleton).
 * Follows the same pattern as WeatherController, DayNightController.
 */

#include "controllers/ControllerBase.hpp"
#include <vector>
#include <memory>

// Forward declarations
class Player;
class Entity;
class NPC;

class CombatController : public ControllerBase {
public:
    CombatController() = default;
    ~CombatController() override = default;

    // Movable (inherited from base)
    CombatController(CombatController&&) noexcept = default;
    CombatController& operator=(CombatController&&) noexcept = default;

    /**
     * @brief Subscribe to combat-related events
     * @note Called when GamePlayState enters
     */
    void subscribe();

    /**
     * @brief Update combat state (cooldowns, stamina regen, target timer)
     * @param deltaTime Frame delta time in seconds
     * @param player Reference to the player
     */
    void update(float deltaTime, Player& player);

    /**
     * @brief Attempt to perform an attack
     * @param player Reference to the attacking player
     * @return true if attack was performed, false if blocked (cooldown, no stamina)
     * @note Uses AIManager::queryEntitiesInRadius() for hit detection
     */
    bool tryAttack(Player& player);

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
    static constexpr float STAMINA_REGEN_RATE{15.0f};    // per second
    static constexpr float TARGET_DISPLAY_DURATION{3.0f}; // seconds after last hit
    static constexpr float ATTACK_COOLDOWN{0.5f};         // seconds between attacks

private:
    /**
     * @brief Execute the attack and detect hits
     * @param player Reference to the attacking player
     */
    void performAttack(Player& player);

    /**
     * @brief Regenerate player stamina over time
     * @param player Reference to the player
     * @param deltaTime Frame delta time in seconds
     */
    void regenerateStamina(Player& player, float deltaTime);

    /**
     * @brief Update target display timer
     * @param deltaTime Frame delta time in seconds
     */
    void updateTargetTimer(float deltaTime);

    // Target tracking
    std::weak_ptr<NPC> m_targetedNPC;  // Safe weak reference, doesn't extend lifetime
    float m_targetDisplayTimer{0.0f};

    // Attack timing
    float m_attackCooldown{0.0f};
};

#endif // COMBAT_CONTROLLER_HPP
