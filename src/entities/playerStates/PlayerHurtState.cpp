/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/playerStates/PlayerHurtState.hpp"
#include "entities/Player.hpp"
#include "core/Logger.hpp"

PlayerHurtState::PlayerHurtState(Player& player) : m_player(player) {}

void PlayerHurtState::enter() {
    COMBAT_DEBUG("Player entering HURT state");

    m_player.get().playAnimation("hurt");
    m_animationDuration = static_cast<float>(m_player.get().getNumFrames() * m_player.get().getAnimSpeed()) / 1000.0f;
    m_elapsedTime = 0.0f;

    m_player.get().setVelocity(Vector2D(0, 0));
}

void PlayerHurtState::update(float deltaTime) {
    m_elapsedTime += deltaTime;
    if (m_elapsedTime >= m_animationDuration) {
        m_player.get().changeState("idle");
    }
}

void PlayerHurtState::exit() {
    COMBAT_DEBUG("Player exiting HURT state");
}
