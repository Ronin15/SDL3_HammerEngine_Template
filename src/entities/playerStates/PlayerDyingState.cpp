/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/playerStates/PlayerDyingState.hpp"
#include "entities/Player.hpp"
#include "core/Logger.hpp"

PlayerDyingState::PlayerDyingState(Player& player) : m_player(player) {}

void PlayerDyingState::enter() {
    COMBAT_DEBUG("Player entering DYING state");

    m_player.get().playAnimation("dying");
    m_player.get().setVelocity(Vector2D(0, 0));
    m_player.get().setAcceleration(Vector2D(0, 0));
}

void PlayerDyingState::update(float deltaTime) {
    (void)deltaTime;
    // Player stays dead - no transition
}

void PlayerDyingState::exit() {
    COMBAT_DEBUG("Player exiting DYING state");
}
