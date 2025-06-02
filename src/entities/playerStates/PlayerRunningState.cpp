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
    (void)deltaTime; // Mark as unused

    const float speed = 150.0f; // Fixed speed, no calculations

    Vector2D velocity(0.0f, 0.0f);

    // Handle keyboard movement (primary input)
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT)) {
        velocity.setX(speed);
        m_player.get().setFlip(SDL_FLIP_NONE);
    }
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT)) {
        velocity.setX(-speed);
        m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
    }
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_UP)) {
        velocity.setY(-speed);
    }
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_DOWN)) {
        velocity.setY(speed);
    }

    // Handle controller joystick (if no keyboard input)
    if (velocity.length() == 0.0f) {
        int joystickX = InputManager::Instance().getAxisX(0, 1);
        int joystickY = InputManager::Instance().getAxisY(0, 1);

        // InputManager returns normalized values (-1, 0, 1)
        if (joystickX != 0 || joystickY != 0) {
            velocity.setX(static_cast<float>(joystickX) * speed);
            velocity.setY(static_cast<float>(joystickY) * speed);

            if (joystickX > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (joystickX < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Handle mouse movement (if no other input)
    if (velocity.length() == 0.0f && InputManager::Instance().getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = InputManager::Instance().getMousePosition();
        Vector2D playerPos = m_player.get().getPosition();
        Vector2D direction = mousePos - playerPos;

        if (direction.length() > 5.0f) {
            direction.normalize();
            velocity = direction * speed;

            if (direction.getX() > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (direction.getX() < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Normalize diagonal movement for consistent speed
    if (velocity.length() > speed) {
        velocity.normalize();
        velocity = velocity * speed;
    }

    m_player.get().setVelocity(velocity);
    m_player.get().setAcceleration(Vector2D(0, 0));
}

void PlayerRunningState::handleRunningAnimation(float deltaTime) {
    (void)deltaTime; // Mark as unused since we're using SDL_GetTicks()

    // Simple, fixed animation timing
    Vector2D velocity = m_player.get().getVelocity();

    if (velocity.length() > 1.0f) {
        Uint64 currentTime = SDL_GetTicks();

        // Use player's animation speed setting
        if (currentTime > m_player.get().getLastFrameTime() + static_cast<Uint64>(m_player.get().getAnimSpeed())) {
            int currentFrame = m_player.get().getCurrentFrame();
            m_player.get().setCurrentFrame((currentFrame + 1) % 2);
            m_player.get().setLastFrameTime(currentTime);
        }
    } else {
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
