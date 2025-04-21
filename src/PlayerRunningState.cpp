#include "PlayerRunningState.hpp"
#include "Player.hpp"
#include "InputHandler.hpp"
#include <iostream>

PlayerRunningState::PlayerRunningState(Player* player) : m_player(player) {}

void PlayerRunningState::enter() {
    std::cout << "Entering Player Running State" << std::endl;
}

void PlayerRunningState::update() {

    Vector2D velocity(0, 0);

    // Handle keyboard movement
    if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_RIGHT)) {
        velocity.setX(2);
        m_player->setFlip(SDL_FLIP_NONE);
    } else if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_LEFT)) {
        velocity.setX(-2);
        m_player->setFlip(SDL_FLIP_HORIZONTAL);
    }

    if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_UP)) {
        velocity.setY(-2);
    } else if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_DOWN)) {
        velocity.setY(2);
    }

    // Update player velocity
    m_player->setVelocity(velocity);

    // Animate only if moving
    if (velocity.getX() != 0 || velocity.getY() != 0) {
        // Animate running (assumes animation frames are set up)
        Uint64 currentTime = SDL_GetTicks();
        m_player->setCurrentFrame((currentTime / 100) % 2); // 2 frames animation
    } else {
        // If not moving, transition back to idle
        // This would be implemented when we add state transitions
    }
}

void PlayerRunningState::render() {
    // Actual rendering is handled by the player
}

void PlayerRunningState::exit() {
    std::cout << "Forge Game Engine - Exiting Player Running State" << std::endl;
}
