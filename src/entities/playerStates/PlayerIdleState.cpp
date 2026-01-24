/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/Player.hpp"
#include "managers/InputManager.hpp"
#include "managers/UIManager.hpp"

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

    // Keyboard or controller input
    if (input.isKeyDown(SDL_SCANCODE_D) ||
        input.isKeyDown(SDL_SCANCODE_A) ||
        input.isKeyDown(SDL_SCANCODE_W) ||
        input.isKeyDown(SDL_SCANCODE_S) ||
        input.getAxisX(0, 1) != 0 ||
        input.getAxisY(0, 1) != 0) {
        return true;
    }

    // Mouse input - only counts if not clicking on UI
    if (input.getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = input.getMousePosition();
        return !UIManager::Instance().isClickOnUI(mousePos);
    }

    return false;
}

void PlayerIdleState::exit() {
    // Nothing special needed on exit
}
