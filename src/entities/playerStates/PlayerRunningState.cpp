/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerRunningState.hpp"
#include "entities/Player.hpp"
#include "managers/InputManager.hpp"
//#include <iostream>

PlayerRunningState::PlayerRunningState(Player& player) : m_player(player) {}

void PlayerRunningState::enter() {
    //std::cout << "Forge Game Engine - Entering Player Running State\n";
}

void PlayerRunningState::update() {

    Vector2D velocity(0, 0);

    // Handle keyboard movement
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT)) {
        velocity.setX(2);
        m_player.get().setFlip(SDL_FLIP_NONE);
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT)) {
        velocity.setX(-2);
        m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
    }

    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_UP)) {
        velocity.setY(-2);
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_DOWN)) {
        velocity.setY(2);
    }

    // Handle controller joystick movement (if a gamepad is connected)
    // We'll use the first connected gamepad (joy = 0) and the left stick (stick = 1)
    int joystickX = InputManager::Instance().getAxisX(0, 1);
    int joystickY = InputManager::Instance().getAxisY(0, 1);

    if (joystickX != 0 || joystickY != 0) {
        // If joystick is being used, override keyboard input
        velocity.setX(joystickX * 2);
        velocity.setY(joystickY * 2);

        // Set proper flip direction based on horizontal movement
        if (joystickX > 0) {
            m_player.get().setFlip(SDL_FLIP_NONE);
        } else if (joystickX < 0) {
            m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
        }
    }

    // Handle mouse movement (when left mouse button is down)
    if (InputManager::Instance().getMouseButtonState(LEFT)) {
        // Get mouse position and player position
        const Vector2D& mousePos = InputManager::Instance().getMousePosition();
        Vector2D playerPos = m_player.get().getPosition();

        // Calculate direction vector from player to mouse cursor
        Vector2D direction = Vector2D(mousePos.getX() - playerPos.getX(),
                                     mousePos.getY() - playerPos.getY());

        // Only move if the mouse is far enough from the player
        if (direction.length() > 5.0f) {
            // Normalize direction and set velocity
            direction.normalize();
            velocity = direction * 2.0f;

            // Set flip based on horizontal movement direction
            if (direction.getX() > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (direction.getX() < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Update player velocity
    m_player.get().setVelocity(velocity);

    // Animate only if moving
    if (velocity.getX() != 0 || velocity.getY() != 0) {
        // Animate running (assumes animation frames are set up)
        Uint64 currentTime = SDL_GetTicks();
        m_player.get().setCurrentFrame((currentTime / 100) % 2); // 2 frames animation
    } else {
        // If not moving, transition back to idle
        // This would be implemented when we add state transitions
    }
}


void PlayerRunningState::exit() {
    //std::cout << "Forge Game Engine - Exiting Player Running State\n";
}
