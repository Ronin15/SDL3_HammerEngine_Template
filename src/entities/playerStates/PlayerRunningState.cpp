/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerRunningState.hpp"
#include "entities/Player.hpp"
//#include <iostream>

PlayerRunningState::PlayerRunningState(Player& player) : m_player(player) {}

void PlayerRunningState::enter() {
    //std::cout << "Forge Game Engine - Entering Player Running State\n";
}

void PlayerRunningState::update(float deltaTime) {
    // Handle running animation based on player velocity
    Vector2D velocity = m_player.get().getVelocity();

    // Only animate if player is actually moving
    if (velocity.length() > 10.0f) {
        static float animationTime = 0.0f;
        animationTime += deltaTime;

        // Change frame every 200ms for running animation
        if (animationTime >= 0.2f) {
            int currentFrame = m_player.get().getCurrentFrame();
            m_player.get().setCurrentFrame((currentFrame + 1) % 2);
            animationTime = 0.0f;
        }
    }
}


void PlayerRunningState::exit() {
    //std::cout << "Forge Game Engine - Exiting Player Running State\n";
}
