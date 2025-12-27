/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/playerStates/PlayerAttackingState.hpp"
#include "entities/Player.hpp"
#include "core/Logger.hpp"

PlayerAttackingState::PlayerAttackingState(Player& player) : m_player(player) {}

void PlayerAttackingState::enter() {
    COMBAT_DEBUG("Player entering ATTACKING state");

    // Reset attack duration timer
    m_attackDuration = 0.0f;

    // Play attacking animation
    m_player.get().playAnimation("attacking");

    // Stop movement during attack
    m_player.get().setVelocity(Vector2D(0, 0));
    m_player.get().setAcceleration(Vector2D(0, 0));
}

void PlayerAttackingState::update(float deltaTime) {
    // Track attack animation duration
    m_attackDuration += deltaTime;

    // Return to idle after attack animation completes
    if (m_attackDuration >= ATTACK_ANIMATION_TIME) {
        COMBAT_DEBUG("Player attack animation complete, returning to idle");
        m_player.get().changeState("idle");
    }
}

void PlayerAttackingState::exit() {
    COMBAT_DEBUG("Player exiting ATTACKING state");
}
