/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/Player.hpp"
//#include <iostream>

PlayerIdleState::PlayerIdleState(Player& player) : m_player(player) {}

void PlayerIdleState::enter() {
    //std::cout << "Forge Game Engine - Entering Player Idle State\n";
    // Set animation for idle
    m_player.get().setCurrentFrame(0);
    m_player.get().setVelocity(Vector2D(0, 0));
}

void PlayerIdleState::update([[maybe_unused]] float deltaTime) {
    // Check for state transitions based on player input
    // This would be implemented later when you have logic to transition states
}

void PlayerIdleState::exit() {
    //std::cout << "Forge Game Engine - Exiting Player Idle State\n";
}
