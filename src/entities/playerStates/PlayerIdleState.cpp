/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/Player.hpp"
#include "managers/InputManager.hpp"

PlayerIdleState::PlayerIdleState(Player& player) : m_player(player) {}

void PlayerIdleState::enter() {
    // Set animation for idle using abstracted API
    m_player.get().playAnimation("idle");
    // Stop movement immediately upon entering idle state
    m_player.get().setVelocity(Vector2D(0, 0));
    m_player.get().setAcceleration(Vector2D(0, 0));
}

void PlayerIdleState::update(float deltaTime) {
    (void)deltaTime; // Mark as unused

    // Check for input to transition to running
    if (hasInputDetected()) {
        m_player.get().changeState("running");
        return;
    }

    // Animation is handled by playAnimation() in enter() - no manual frame setting needed
}

bool PlayerIdleState::hasInputDetected() const {
    // Check for any movement input
    const InputManager& input = InputManager::Instance();
    return (input.isKeyDown(SDL_SCANCODE_RIGHT) ||
            input.isKeyDown(SDL_SCANCODE_LEFT) ||
            input.isKeyDown(SDL_SCANCODE_UP) ||
            input.isKeyDown(SDL_SCANCODE_DOWN) ||
            input.getAxisX(0, 1) != 0 ||
            input.getAxisY(0, 1) != 0 ||
            input.getMouseButtonState(LEFT));
}

void PlayerIdleState::exit() {
    // Nothing special needed on exit
}
