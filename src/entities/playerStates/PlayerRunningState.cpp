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
    // Set animation for running using abstracted API
    m_player.get().playAnimation("running");
}

void PlayerRunningState::update(float deltaTime) {
    // Process input and calculate movement velocity
    handleMovementInput(deltaTime);

    // Update animation frames based on movement
    handleRunningAnimation(deltaTime);

    // Transition to idle state when no input is detected
    if (!hasInputDetected()) {
        m_player.get().changeState("idle");
    }
}

void PlayerRunningState::exit() {
    // Nothing needed on exit
}

void PlayerRunningState::handleMovementInput(float deltaTime) {
    (void)deltaTime; // Movement uses direct velocity setting, not acceleration
    
    const float speed = m_player.get().getMovementSpeed();
    const InputManager& input = InputManager::Instance();
    
    Vector2D velocity(0.0f, 0.0f);
    bool hasInput = false;

    // Keyboard input (highest priority - most responsive)
    if (input.isKeyDown(SDL_SCANCODE_RIGHT)) {
        velocity.setX(speed);
        m_player.get().setFlip(SDL_FLIP_NONE);
        hasInput = true;
    }
    if (input.isKeyDown(SDL_SCANCODE_LEFT)) {
        velocity.setX(-speed);
        m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
        hasInput = true;
    }
    if (input.isKeyDown(SDL_SCANCODE_UP)) {
        velocity.setY(-speed);
        hasInput = true;
    }
    if (input.isKeyDown(SDL_SCANCODE_DOWN)) {
        velocity.setY(speed);
        hasInput = true;
    }

    // Controller input (secondary priority - only when no keyboard input)
    if (!hasInput) {
        int joystickX = input.getAxisX(0, 1);
        int joystickY = input.getAxisY(0, 1);

        // InputManager provides pre-normalized directional values: -1 (left/up), 0 (center), 1 (right/down)
        if (joystickX != 0 || joystickY != 0) {
            velocity.setX(static_cast<float>(joystickX) * speed);
            velocity.setY(static_cast<float>(joystickY) * speed);
            hasInput = true;

            if (joystickX > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (joystickX < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Mouse input (lowest priority - only when no keyboard or controller input)
    if (!hasInput && input.getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = input.getMousePosition();
        Vector2D playerPos = m_player.get().getPosition();
        Vector2D direction = mousePos - playerPos;

        if (direction.length() > 5.0f) {
            direction.normalize();
            velocity = direction * speed;
            hasInput = true;

            if (direction.getX() > 0) {
                m_player.get().setFlip(SDL_FLIP_NONE);
            } else if (direction.getX() < 0) {
                m_player.get().setFlip(SDL_FLIP_HORIZONTAL);
            }
        }
    }

    // Normalize diagonal movement for consistent speed (only if we have input)
    if (hasInput && velocity.length() > speed) {
        velocity.normalize();
        velocity = velocity * speed;
    }

    m_player.get().setVelocity(velocity);
    m_player.get().setAcceleration(Vector2D(0, 0));
}

void PlayerRunningState::handleRunningAnimation(float deltaTime) {
    (void)deltaTime; // Animation uses SDL_GetTicks() for consistent timing

    Vector2D velocity = m_player.get().getVelocity();

    // Only animate when player is moving
    if (velocity.length() > 1.0f) {
        Uint64 currentTime = SDL_GetTicks();

        // Advance frame based on player's configurable animation speed
        // Uses m_numFrames set by playAnimation() for proper frame count
        if (currentTime > m_player.get().getLastFrameTime() + static_cast<Uint64>(m_player.get().getAnimSpeed())) {
            int currentFrame = m_player.get().getCurrentFrame();
            int numFrames = m_player.get().getNumFrames();
            m_player.get().setCurrentFrame((currentFrame + 1) % numFrames);
            m_player.get().setLastFrameTime(currentTime);
        }
    } else {
        m_player.get().setCurrentFrame(0);
    }
}

bool PlayerRunningState::hasInputDetected() const {
    // Returns true if any movement input is currently active
    const InputManager& input = InputManager::Instance();
    return (input.isKeyDown(SDL_SCANCODE_RIGHT) ||
            input.isKeyDown(SDL_SCANCODE_LEFT) ||
            input.isKeyDown(SDL_SCANCODE_UP) ||
            input.isKeyDown(SDL_SCANCODE_DOWN) ||
            input.getAxisX(0, 1) != 0 ||
            input.getAxisY(0, 1) != 0 ||
            input.getMouseButtonState(LEFT));
}
