/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/playerStates/PlayerRunningState.hpp"
#include "entities/Player.hpp"
#include "managers/InputManager.hpp"
#include <SDL3/SDL.h>  // For SDL_GetTicks()

PlayerRunningState::PlayerRunningState(Player& player) : m_player(player) {}

void PlayerRunningState::enter() {
    // Nothing special needed on enter
}

void PlayerRunningState::update(float deltaTime) {
    // Handle all movement input and physics
    handleMovementInput(deltaTime);
    
    // Handle running animation using EXACT same timing as NPCs
    handleRunningAnimation(deltaTime);
    
    // Check for transition to idle (when no input)
    if (!hasInputDetected()) {
        m_player.get().changeState("idle");
    }
}

void PlayerRunningState::exit() {
    // Nothing needed on exit
}

void PlayerRunningState::handleMovementInput(float deltaTime) {
    const float maxSpeed = 120.0f; // Same as NPCs
    const float acceleration = 400.0f; // Same as NPCs

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

    // Handle controller joystick movement
    int joystickX = InputManager::Instance().getAxisX(0, 1);
    int joystickY = InputManager::Instance().getAxisY(0, 1);

    if (joystickX != 0 || joystickY != 0) {
        inputDirection.setX(joystickX / 32767.0f);
        inputDirection.setY(joystickY / 32767.0f);

        if (joystickX > 0) {
            m_player.get().setFlip(SDL_FLIP_NONE);
        } else if (joystickX < 0) {
            m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
        }
    }

    // Handle mouse movement
    if (InputManager::Instance().getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = InputManager::Instance().getMousePosition();
        Vector2D playerPos = m_player.get().getPosition();

        Vector2D direction = Vector2D(mousePos.getX() - playerPos.getX(),
                                     mousePos.getY() - playerPos.getY());

        if (direction.length() > 5.0f) {
            direction.normalize();
            inputDirection = direction;

            if (direction.getX() > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (direction.getX() < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // EXACT same acceleration physics as NPCs
    Vector2D currentVelocity = m_player.get().getVelocity();
    Vector2D targetVelocity = inputDirection * maxSpeed;
    Vector2D velocityDifference = targetVelocity - currentVelocity;
    Vector2D accelerationVector = velocityDifference;
    
    // Limit acceleration magnitude for smooth movement
    if (accelerationVector.length() > acceleration * deltaTime) {
        accelerationVector.normalize();
        accelerationVector = accelerationVector * acceleration * deltaTime;
    }
    
    // Set acceleration (let Player::update() handle velocity integration like NPCs)
    Vector2D finalAcceleration = accelerationVector / deltaTime;
    m_player.get().setAcceleration(finalAcceleration);
}

void PlayerRunningState::handleRunningAnimation(float deltaTime) {
    (void)deltaTime; // Mark as unused since we're using SDL_GetTicks() like NPCs
    
    // Handle running animation based on player velocity
    Vector2D velocity = m_player.get().getVelocity();
    
    // Only animate if player is actually moving
    if (velocity.length() > 10.0f) {
        // Use EXACT same timing method as NPCs
        Uint64 currentTime = SDL_GetTicks();
        
        if (currentTime > m_player.get().getLastFrameTime() + 100) {  // 100ms like NPCs
            int currentFrame = m_player.get().getCurrentFrame();
            m_player.get().setCurrentFrame((currentFrame + 1) % 2);
            m_player.get().setLastFrameTime(currentTime);
        }
    } else {
        // When not moving, reset to first frame (like NPCs do)
        m_player.get().setCurrentFrame(0);
    }
}

bool PlayerRunningState::hasInputDetected() const {
    // Check for any movement input
    return (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_UP) ||
            InputManager::Instance().isKeyDown(SDL_SCANCODE_DOWN) ||
            InputManager::Instance().getAxisX(0, 1) != 0 ||
            InputManager::Instance().getAxisY(0, 1) != 0 ||
            InputManager::Instance().getMouseButtonState(LEFT));
}
