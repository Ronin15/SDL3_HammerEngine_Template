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

void PlayerRunningState::update(float deltaTime) {
    const float maxSpeed = 120.0f; // Maximum pixels per second
    const float acceleration = 400.0f; // Acceleration in pixels per second squared

    Vector2D inputDirection(0, 0);

    // Handle keyboard movement
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT)) {
        inputDirection.setX(1.0f);
        m_player.get().setFlip(SDL_FLIP_NONE);
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT)) {
        inputDirection.setX(-1.0f);
        m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
    }

    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_UP)) {
        inputDirection.setY(-1.0f);
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_DOWN)) {
        inputDirection.setY(1.0f);
    }

    // Handle controller joystick movement (if a gamepad is connected)
    int joystickX = InputManager::Instance().getAxisX(0, 1);
    int joystickY = InputManager::Instance().getAxisY(0, 1);

    if (joystickX != 0 || joystickY != 0) {
        // Convert joystick values to normalized direction (-1 to 1)
        inputDirection.setX(joystickX / 32767.0f);
        inputDirection.setY(joystickY / 32767.0f);

        // Set proper flip direction based on horizontal movement
        if (joystickX > 0) {
            m_player.get().setFlip(SDL_FLIP_NONE);
        } else if (joystickX < 0) {
            m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
        }
    }

    // Handle mouse movement (when left mouse button is down)
    if (InputManager::Instance().getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = InputManager::Instance().getMousePosition();
        Vector2D playerPos = m_player.get().getPosition();

        Vector2D direction = Vector2D(mousePos.getX() - playerPos.getX(),
                                     mousePos.getY() - playerPos.getY());

        // Only move if the mouse is far enough from the player
        if (direction.length() > 5.0f) {
            direction.normalize();
            inputDirection = direction;

            // Set flip based on horizontal movement direction
            if (direction.getX() > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (direction.getX() < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Apply acceleration based on input
    Vector2D currentVelocity = m_player.get().getVelocity();
    Vector2D targetVelocity = inputDirection * maxSpeed;
    
    // Smooth acceleration towards target velocity
    Vector2D velocityDifference = targetVelocity - currentVelocity;
    Vector2D accelerationVector = velocityDifference;
    
    // Limit acceleration magnitude
    if (accelerationVector.length() > acceleration * deltaTime) {
        accelerationVector.normalize();
        accelerationVector = accelerationVector * acceleration * deltaTime;
    }
    
    Vector2D newVelocity = currentVelocity + accelerationVector;
    m_player.get().setVelocity(newVelocity);

    // Frame-rate independent animation
    if (newVelocity.length() > 10.0f) { // Only animate if moving with reasonable speed
        static float animationTime = 0.0f;
        animationTime += deltaTime;
        
        // Change frame every 200ms
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
