/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/Player.hpp"
#include "core/GameEngine.hpp"
#include "managers/InputManager.hpp"
#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/playerStates/PlayerRunningState.hpp"
#include "SDL3/SDL_surface.h"
#include "managers/TextureManager.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <cmath>

Player::Player() {
    // Initialize player properties
    m_position = Vector2D(400, 300);  // Start position in the middle of a typical screen
    m_velocity = Vector2D(0, 0);
    m_acceleration = Vector2D(0, 0);
    m_textureID = "player";  // Texture ID as loaded by TextureManager from res/img directory

    // Animation properties
    m_currentFrame = 1;                 // Start with first frame
    m_currentRow = 1;                   // In TextureManager::drawFrame, rows start at 1
    m_numFrames = 2;                    // Number of frames in the animation
    m_animSpeed = 100;                  // Animation speed in milliseconds
    m_spriteSheetRows = 1;              // Number of rows in the sprite sheet
    m_lastFrameTime = SDL_GetTicks();   // Track when we last changed animation frame
    m_flip = SDL_FLIP_NONE;             // Default flip direction

    // Set width and height based on texture dimensions if the texture is loaded
    loadDimensionsFromTexture();

    // Setup state manager and add states
    setupStates();

    // Set default state
    changeState("idle");

    //std::cout << "Forge Game Engine - Player created" << "\n";
}

// Helper method to get dimensions from the loaded texture
void Player::loadDimensionsFromTexture() {
    // Default dimensions in case texture loading fails
    m_width = 128;
    m_height = 128;  // Set height equal to the sprite sheet row height
    m_frameWidth = 64;  // Default frame width (width/numFrames)

    // Get the texture from TextureManager
    if (TextureManager::Instance().isTextureInMap(m_textureID)) {
        auto texture = TextureManager::Instance().getTexture(m_textureID);
        if (texture != nullptr) {
            float width = 0.0f;
            float height = 0.0f;
            // Query the texture to get its width and height
            // SDL3 uses SDL_GetTextureSize which returns float dimensions and returns a bool
            if (SDL_GetTextureSize(texture.get(), &width, &height)) {
                std::cout << "Forge Game Engine - Original texture dimensions: " << width << "x" << height << "\n";

                // Store original dimensions for full sprite sheet
                m_width = static_cast<int>(width);
                m_height = static_cast<int>(height);

                // Calculate frame dimensions based on sprite sheet layout
                m_frameWidth = m_width / m_numFrames; // Width per frame
                int frameHeight = m_height / m_spriteSheetRows; // Height per row

                // Update height to be the height of a single frame
                m_height = frameHeight;

                std::cout << "Forge Game Engine - Loaded texture dimensions: " << m_width << "x" << height << "\n";
                std::cout << "Forge Game Engine - Frame dimensions: " << m_frameWidth << "x" << frameHeight << "\n";
                std::cout << "Forge Game Engine - Sprite layout: " << m_numFrames << " columns x " << m_spriteSheetRows << " rows" << "\n";
            } else {
                std::cerr << "Forge Game Engine - Failed to query texture dimensions: " << SDL_GetError() << std::endl;
            }
        }
    } else {
        std::cout << "Forge Game Engine - Texture '" << m_textureID << "' not found in TextureManager" << "\n";
    }
}

void Player::setupStates() {
    // Create and add states
    m_stateManager.addState("idle", std::make_unique<PlayerIdleState>(*this));
    m_stateManager.addState("running", std::make_unique<PlayerRunningState>(*this));
}

Player::~Player() {
    // Don't call virtual functions from destructors
    // Instead of calling clean(), directly handle cleanup here

    std::cout << "Forge Game Engine - Cleaning up player resources" << "\n";
    std::cout << "Forge Game Engine - Player resources cleaned!\n";
}

void Player::changeState(const std::string& stateName) {
    if (m_stateManager.hasState(stateName)) {
        m_stateManager.setState(stateName);
    } else {
        std::cerr << "Player state not found: " << stateName << std::endl;
    }
}

std::string Player::getCurrentStateName() const {
    return m_stateManager.getCurrentStateName();
}

void Player::update(float deltaTime) {
    // Handle all movement physics directly in Player class
    handleMovementInput(deltaTime);

    // Handle state transitions for visuals only
    handleStateTransitions();

    // Let the current state handle visual/animation behavior only
    m_stateManager.update(deltaTime);

    // Update position based on velocity (physics)
    m_position += m_velocity * deltaTime;

    // Apply smooth friction for deceleration
    if (m_velocity.length() > 0.1f) {
        // Frame-rate independent friction using exponential decay
        const float frictionRate = 0.08f;  // Gentle friction for smooth stops
        float frictionFactor = std::pow(frictionRate, deltaTime);
        m_velocity *= frictionFactor;
    } else if (m_velocity.length() < 0.1f) {
        // If velocity is very small, stop completely to avoid tiny sliding
        m_velocity = Vector2D(0, 0);
    }

    // If the texture dimensions haven't been loaded yet, try loading them
    if (m_frameWidth == 0 && TextureManager::Instance().isTextureInMap(m_textureID)) {
        loadDimensionsFromTexture();
    }
}

void Player::render() {
    // The render method in EntityStateManager calls the render method of the current state
    // Don't call update here to avoid double updates

    // Calculate centered position for rendering
    // This ensures the player is centered at its position coordinates
    int renderX = std::lround(m_position.getX() - (m_frameWidth / 2.0f));
    int renderY = std::lround(m_position.getY() - (m_height / 2.0f));

    // Do the common rendering for all states
    TextureManager::Instance().drawFrame(
        m_textureID,
        renderX,                // Center horizontally
        renderY,                // Center vertically
        m_frameWidth,           // Use the calculated frame width
        m_height,               // Height stays the same
        m_currentRow,           // Current animation row
        m_currentFrame,         // Current animation frame
        GameEngine::Instance().getRenderer(),
        m_flip
    );

    // Log rendering information (commented out to reduce console spam during animation)
    // std::cout << "Forge Game Engine - Rendering player at position: " << m_position.getX() << ", " << m_position.getY()
    //          << " (render at: " << renderX << ", " << renderY << ")" << "\n";
}

void Player::clean() {
    // Clean up any resources
    std::cout << "Forge Game Engine - Cleaning up player resources" << "\n";
}

void Player::setPosition(const Vector2D& position) {
    m_position = position;
}

void Player::handleMovementInput(float deltaTime) {
    const float maxSpeed = 120.0f; // Maximum pixels per second
    const float acceleration = 300.0f; // Acceleration in pixels per second squared

    Vector2D inputDirection(0, 0);

    // Handle keyboard movement
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_RIGHT)) {
        inputDirection.setX(1.0f);
        m_flip = SDL_FLIP_NONE;
    } else if (InputManager::Instance().isKeyDown(SDL_SCANCODE_LEFT)) {
        inputDirection.setX(-1.0f);
        m_flip = SDL_FLIP_HORIZONTAL;
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
        // Convert joystick values to normalized direction (-1 to 1)
        inputDirection.setX(joystickX / 32767.0f);
        inputDirection.setY(joystickY / 32767.0f);

        // Set proper flip direction based on horizontal movement
        if (joystickX > 0) {
            m_flip = SDL_FLIP_NONE;
        } else if (joystickX < 0) {
            m_flip = SDL_FLIP_HORIZONTAL;
        }
    }

    // Handle mouse movement (when left mouse button is down)
    if (InputManager::Instance().getMouseButtonState(LEFT)) {
        const Vector2D& mousePos = InputManager::Instance().getMousePosition();
        Vector2D playerPos = getPosition();

        Vector2D direction = Vector2D(mousePos.getX() - playerPos.getX(),
                                     mousePos.getY() - playerPos.getY());

        // Only move if the mouse is far enough from the player
        if (direction.length() > 5.0f) {
            direction.normalize();
            inputDirection = direction;

            // Set flip based on horizontal movement direction
            if (direction.getX() > 0) {
                m_flip = SDL_FLIP_NONE;
            } else if (direction.getX() < 0) {
                m_flip = SDL_FLIP_HORIZONTAL;
            }
        }
    }

    // Apply smooth acceleration towards target velocity
    Vector2D targetVelocity = inputDirection * maxSpeed;
    Vector2D velocityDifference = targetVelocity - m_velocity;

    // Calculate acceleration vector
    Vector2D accelerationVector = velocityDifference;

    // Limit acceleration magnitude for smooth movement
    if (accelerationVector.length() > acceleration * deltaTime) {
        accelerationVector.normalize();
        accelerationVector = accelerationVector * acceleration * deltaTime;
    }

    // Apply acceleration to velocity
    m_velocity += accelerationVector;
}

void Player::handleStateTransitions() {
    // Simple state transitions based on movement for visuals only
    bool hasVelocity = m_velocity.length() > 10.0f; // Moving with reasonable speed

    if (hasVelocity && getCurrentStateName() != "running") {
        changeState("running");
    } else if (!hasVelocity && getCurrentStateName() != "idle") {
        changeState("idle");
    }
}
