/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "entities/NPC.hpp"
#include "core/GameEngine.hpp"
#include "managers/TextureManager.hpp"
#include "managers/AIManager.hpp"
#include <SDL3/SDL.h>
#include <iostream>

NPC::NPC(const std::string& textureID, const Vector2D& startPosition, int frameWidth, int frameHeight)
    : m_frameWidth(frameWidth), m_frameHeight(frameHeight) {
    // Initialize entity properties
    m_position = startPosition;
    m_velocity = Vector2D(0, 0);
    m_acceleration = Vector2D(0, 0);
    m_textureID = textureID;

    // Animation properties
    m_currentFrame = 1;                 // Start with first frame
    m_currentRow = 1;                   // In TextureManager::drawFrame, rows start at 1
    m_numFrames = 2;                    // Default to 2 frames for simple animation
    m_animSpeed = 100;                  // Default animation speed in milliseconds
    m_spriteSheetRows = 1;              // Default number of rows in the sprite sheet
    m_lastFrameTime = SDL_GetTicks();   // Track when we last changed animation frame
    m_flip = SDL_FLIP_NONE;             // Default flip direction

    // Load dimensions from texture if not provided
    if (m_frameWidth <= 0 || m_frameHeight <= 0) {
        loadDimensionsFromTexture();
    } else {
        m_width = m_frameWidth;
        m_height = m_frameHeight;
    }

    // No state setup needed - handled by AI Manager

    // Set default wander area (can be changed later via setWanderArea)
    m_minX = 0.0f;
    m_minY = 0.0f;
    m_maxX = 800.0f;
    m_maxY = 600.0f;

    //std::cout << "Forge Game Engine - NPC created at position: " << m_position.getX() << ", " << m_position.getY() << "\n";
}

NPC::~NPC() {
    clean();
    std::cout << "Forge Game Engine - NPC resources cleaned!\n";
}

void NPC::loadDimensionsFromTexture() {
    // Default dimensions in case texture loading fails
    m_width = 128;
    m_height = 128;  // Set height equal to the sprite sheet row height
    m_frameWidth = 64;  // Default frame width (width/numFrames)

    // Get the texture from TextureManager
    if (TextureManager::Instance().isTextureInMap(m_textureID)) {
        SDL_Texture* texture = TextureManager::Instance().getTexture(m_textureID);
        if (texture != nullptr) {
            float width = 0.0f;
            float height = 0.0f;
            // Query the texture to get its width and height
            // SDL3 uses SDL_GetTextureSize which returns float dimensions and returns a bool
            if (SDL_GetTextureSize(texture, &width, &height)) {
                std::cout << "Forge Game Engine - NPC texture dimensions: " << width << "x" << height << "\n";

                // Store original dimensions for full sprite sheet
                m_width = static_cast<int>(width);
                m_height = static_cast<int>(height);

                // Calculate frame dimensions based on sprite sheet layout
                m_frameWidth = m_width / m_numFrames; // Width per frame
                int frameHeight = m_height / m_spriteSheetRows; // Height per row

                // Update height to be the height of a single frame
                m_height = frameHeight;

                std::cout << "Forge Game Engine - NPC frame dimensions: " << m_frameWidth << "x" << m_height << "\n";
            } else {
                std::cerr << "Forge Game Engine - Failed to query NPC texture dimensions: " << SDL_GetError() << std::endl;
            }
        }
    } else {
        std::cout << "Forge Game Engine - NPC texture '" << m_textureID << "' not found in TextureManager" << "\n";
    }
}

// State management removed - handled by AI Manager

void NPC::update() {
    // Update position based on velocity and acceleration
    m_velocity += m_acceleration;
    m_position += m_velocity;

    // Apply friction for smoother movement
    if (m_velocity.length() > 0.1f) {
        // Friction coefficient - adjust for desired sliding feel (0.95 means 95% of velocity is retained)
        const float friction = 0.95f;
        m_velocity *= friction;
    } else if (m_velocity.length() < 0.1f) {
        // If velocity is very small, stop completely to avoid tiny sliding
        m_velocity = Vector2D(0, 0);
    }

    // Reset acceleration
    m_acceleration = Vector2D(0, 0);

    // Handle bounds checking with bounce behavior if outside permitted area
    const float bounceBuffer = 20.0f; // Buffer zone before bouncing

    if (m_position.getX() < m_minX - bounceBuffer) {
        m_position.setX(m_minX);
        m_velocity.setX(std::abs(m_velocity.getX())); // Move right
        m_flip = SDL_FLIP_NONE;
    } else if (m_position.getX() + m_width > m_maxX + bounceBuffer) {
        m_position.setX(m_maxX - m_width);
        m_velocity.setX(-std::abs(m_velocity.getX())); // Move left
        m_flip = SDL_FLIP_HORIZONTAL;
    }

    if (m_position.getY() < m_minY - bounceBuffer) {
        m_position.setY(m_minY);
        m_velocity.setY(std::abs(m_velocity.getY())); // Move down
    } else if (m_position.getY() + m_height > m_maxY + bounceBuffer) {
        m_position.setY(m_maxY - m_height);
        m_velocity.setY(-std::abs(m_velocity.getY())); // Move up
    }

    // No need to update animation - TextureManager handles this

    // If the texture dimensions haven't been loaded yet, try loading them
    if (m_frameWidth == 0 && TextureManager::Instance().isTextureInMap(m_textureID)) {
        loadDimensionsFromTexture();
    }
}

void NPC::render() {
    // Calculate centered position for rendering
    // This ensures the NPC is centered at its position coordinates
    int renderX = static_cast<int>(m_position.getX() - (m_frameWidth / 2.0f));
    int renderY = static_cast<int>(m_position.getY() - (m_height / 2.0f));

    // Render the NPC with the current animation frame
    TextureManager::Instance().drawFrame(
        m_textureID,
        renderX,
        renderY,
        m_frameWidth,
        m_height,
        m_currentRow,
        m_currentFrame,
        GameEngine::Instance().getRenderer(),
        m_flip
    );
}

void NPC::clean() {
    // Remove from AI Manager if it has a behavior
    if (AIManager::Instance().entityHasBehavior(this)) {
        AIManager::Instance().unassignBehaviorFromEntity(this);
    }

    std::cout << "Forge Game Engine - Cleaning up NPC resources" << "\n";
}

// Animation handling removed - TextureManager handles this functionality

void NPC::setWanderArea(float minX, float minY, float maxX, float maxY) {
    m_minX = minX;
    m_minY = minY;
    m_maxX = maxX;
    m_maxY = maxY;
}

void NPC::setPosition(const Vector2D& position) {
    m_position = position;
}
