/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/Player.hpp"
#include "managers/InputManager.hpp"

PlayerIdleState::PlayerIdleState(Player& player) : m_player(player) {}

void PlayerIdleState::enter() {
    // Set animation for idle
    m_player.get().setCurrentFrame(0);
    // Let velocity naturally decelerate instead of immediate stop
}

void PlayerIdleState::update(float deltaTime) {
    (void)deltaTime; // Mark as unused
    
    // Check for input to transition to running
    if (hasInputDetected()) {
        m_player.get().changeState("running");
        return;
    }
    
    // Set acceleration to zero (no input = no acceleration)
    // Let Player::update() handle friction like NPCs
    m_player.get().setAcceleration(Vector2D(0, 0));
    
    // Keep idle animation frame
    m_player.get().setCurrentFrame(0);
}

bool PlayerIdleState::hasInputDetected() const {
    // Check for any movement input
    return (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_UP) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_DOWN) ||
            InputManager::Instance().getAxisX(0, 1) != 0 ||
            InputManager::Instance().getAxisY(0, 1) != 0 ||
            InputManager::Instance().getMouseButtonState(LEFT));
}

void PlayerIdleState::exit() {
    //std::cout << "Forge Game Engine - Exiting Player Idle State\n";
}
