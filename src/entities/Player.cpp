/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/Player.hpp"
#include "core/GameEngine.hpp"
#include "entities/playerStates/PlayerIdleState.hpp"
#include "entities/playerStates/PlayerRunningState.hpp"
#include "SDL3/SDL_surface.h"
#include "managers/TextureManager.hpp"
#include <SDL3/SDL.h>
#include "core/Logger.hpp"

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

    // Cache TextureManager reference for better performance
    const TextureManager& texMgr = TextureManager::Instance();

    // Get the texture from TextureManager
    if (texMgr.isTextureInMap(m_textureID)) {
        auto texture = texMgr.getTexture(m_textureID);
        if (texture != nullptr) {
            float width = 0.0f;
            float height = 0.0f;
            // Query the texture to get its width and height
            // SDL3 uses SDL_GetTextureSize which returns float dimensions and returns a bool
            if (SDL_GetTextureSize(texture.get(), &width, &height)) {
                PLAYER_DEBUG("Original texture dimensions: " + std::to_string(width) + "x" + std::to_string(height));

                // Store original dimensions for full sprite sheet
                m_width = static_cast<int>(width);
                m_height = static_cast<int>(height);

                // Calculate frame dimensions based on sprite sheet layout
                m_frameWidth = m_width / m_numFrames; // Width per frame
                int frameHeight = m_height / m_spriteSheetRows; // Height per row

                // Update height to be the height of a single frame
                m_height = frameHeight;

                PLAYER_DEBUG("Loaded texture dimensions: " + std::to_string(m_width) + "x" + std::to_string(height));
                PLAYER_DEBUG("Frame dimensions: " + std::to_string(m_frameWidth) + "x" + std::to_string(frameHeight));
                PLAYER_DEBUG("Sprite layout: " + std::to_string(m_numFrames) + " columns x " + std::to_string(m_spriteSheetRows) + " rows");
            } else {
                PLAYER_ERROR("Failed to query texture dimensions: " + std::string(SDL_GetError()));
            }
        }
    } else {
        PLAYER_ERROR("Texture '" + m_textureID + "' not found in TextureManager");
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

    PLAYER_DEBUG("Cleaning up player resources");
    PLAYER_DEBUG("Player resources cleaned!");
}

void Player::changeState(const std::string& stateName) {
    if (m_stateManager.hasState(stateName)) {
        m_stateManager.setState(stateName);
    } else {
        PLAYER_ERROR("Player state not found: " + stateName);
    }
}

std::string Player::getCurrentStateName() const {
    return m_stateManager.getCurrentStateName();
}

void Player::update(float deltaTime) {
    // Let the state machine handle ALL movement and input logic
    m_stateManager.update(deltaTime);

    // Apply velocity to position
    m_position += m_velocity * deltaTime;

    // If the texture dimensions haven't been loaded yet, try loading them
    if (m_frameWidth == 0 && TextureManager::Instance().isTextureInMap(m_textureID)) {
        loadDimensionsFromTexture();
    }
}

void Player::render() {
    // Cache manager references for better performance
    TextureManager& texMgr = TextureManager::Instance();
    SDL_Renderer* renderer = GameEngine::Instance().getRenderer();

    // Calculate centered position for rendering (IDENTICAL to NPCs)
    int renderX = static_cast<int>(m_position.getX() - (m_frameWidth / 2.0f));
    int renderY = static_cast<int>(m_position.getY() - (m_height / 2.0f));

    // Render the Player with the current animation frame (IDENTICAL to NPCs)
    texMgr.drawFrame(
        m_textureID,
        renderX,                // Center horizontally
        renderY,                // Center vertically
        m_frameWidth,           // Use the calculated frame width
        m_height,               // Height stays the same
        m_currentRow,           // Current animation row
        m_currentFrame,         // Current animation frame
        renderer,
        m_flip
    );
}

void Player::clean() {
    // Clean up any resources
    PLAYER_DEBUG("Cleaning up player resources");
}
