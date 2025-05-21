/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/Player.hpp"
//#include <iostream>

PlayerIdleState::PlayerIdleState(Player* player) : mp_player(player) {}

void PlayerIdleState::enter() {
    //std::cout << "Forge Game Engine - Entering Player Idle State\n";
    // Set animation for idle
    mp_player->setCurrentFrame(0);
    mp_player->setVelocity(Vector2D(0, 0));
}

void PlayerIdleState::update() {
    // Check for state transitions based on player input
    // This would be implemented later when you have logic to transition states
}

void PlayerIdleState::exit() {
    //std::cout << "Forge Game Engine - Exiting Player Idle State\n";
}
