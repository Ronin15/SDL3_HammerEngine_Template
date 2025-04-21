#include "PlayerIdleState.hpp"
#include "Player.hpp"
#include <iostream>

PlayerIdleState::PlayerIdleState(Player* player) : m_player(player) {}

void PlayerIdleState::enter() {
    std::cout << "Forge Game Engine - Entering Player Idle State" << std::endl;
    // Set animation for idle
    m_player->setCurrentFrame(0);
    m_player->setVelocity(Vector2D(0, 0));
}

void PlayerIdleState::update() {
    // Check for state transitions based on player input
    // This would be implemented later when you have logic to transition states
}

void PlayerIdleState::exit() {
    std::cout << "Forge Game Engine - Exiting Player Idle State" << std::endl;
}
