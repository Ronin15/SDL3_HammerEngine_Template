/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "NPC.hpp"
#include "TextureManager.hpp"
#include "GameEngine.hpp"
#include "AIManager.hpp"
#include <SDL3/SDL.h>

NPC::NPC(const std::string& textureID, const Vector2D& startPosition, int frameWidth, int frameHeight)
    : m_frameWidth(frameWidth), m_frameHeight(frameHeight) {
    // Initialize entity properties
    m_position = startPosition;
    m_velocity = Vector2D(0, 0);
    m_textureID = textureID;
    m_currentFrame = 0;
    m_currentRow = 0;
    m_numFrames = 2; // Default to 2 frames for simple animation
    m_animSpeed = 100; // Default animation speed in milliseconds
    
    // Load dimensions from texture if not provided
    if (m_frameWidth <= 0 || m_frameHeight <= 0) {
        loadDimensionsFromTexture();
    } else {
        m_width = m_frameWidth;
        m_height = m_frameHeight;
    }
    
    // Setup entity states
    setupStates();
}

NPC::~NPC() {
    clean();
}

void NPC::update() {
    // Update position based on velocity - AI behaviors set the velocity
    m_position = m_position + m_velocity;
    
    // Update bounds check with softer constraints
    // Only apply bounce logic if we're far outside the bounds
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
    
    // Update animation
    updateAnimation();
    
    // Update entity state
    m_stateManager.update();
}

void NPC::render() {
    TextureManager::Instance().drawFrame(
        m_textureID,
        static_cast<int>(m_position.getX()),
        static_cast<int>(m_position.getY()),
        m_width,
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
}

void NPC::changeState(const std::string& stateName) {
    m_stateManager.setState(stateName);
}

std::string NPC::getCurrentStateName() const {
    return m_stateManager.getCurrentStateName();
}

void NPC::loadDimensionsFromTexture() {
    // Try to get dimensions from the texture
    int fullWidth = 0, fullHeight = 0;
    SDL_Texture* texture = TextureManager::Instance().getTexture(m_textureID);
    if (texture) {
        // Use the texture dimensions from the player sprite as a fallback
        // SDL3 doesn't have SDL_QueryTexture anymore, but we can set reasonable defaults
        fullWidth = 64;  // Default width assumption
        fullHeight = 256; // Default height assumption for a 4-row sprite sheet
        
        // Assume simple division for sprite sheet
        m_spriteSheetRows = 1; // Default to 1 row
        
        // Try to detect sprite sheet configuration
        if (fullHeight > fullWidth) {
            m_spriteSheetRows = 4; // Common for character spritesheets
        }
        
        // Default to full texture dimensions if frame dimensions are unknown
        if (m_frameWidth <= 0) {
            m_frameWidth = fullWidth;
        }
        
        if (m_frameHeight <= 0) {
            m_frameHeight = fullHeight / m_spriteSheetRows;
        }
        
        m_width = m_frameWidth;
        m_height = m_frameHeight;
    }
}

void NPC::setupStates() {
    // NPC states could be set up here, similar to Player states
    // This is a placeholder for future state implementation
}

void NPC::updateAnimation() {
    // Simple animation based on time
    Uint64 currentTime = SDL_GetTicks();
    if (currentTime > m_lastFrameTime + m_animSpeed) {
        m_lastFrameTime = currentTime;
        
        // Ensure m_numFrames is at least 1 to avoid division by zero
        if (m_numFrames < 1) {
            m_numFrames = 1;
        }
        
        // Always animate, with different animations for moving vs idle
        if (m_velocity.length() > 0.1f) {
            // Moving animation
            m_currentFrame = (m_currentFrame + 1) % m_numFrames;
            
            // Determine animation row based on dominant direction
            if (std::abs(m_velocity.getX()) > std::abs(m_velocity.getY())) {
                // Horizontal movement - use row 1 (assuming row 0 is idle)
                m_currentRow = 1;
            } else {
                // Vertical movement - use row 2 or 3 (down or up)
                m_currentRow = (m_velocity.getY() > 0) ? 2 : 3;
            }
        } else {
            // Idle animation - typically row 0
            m_currentRow = 0;
            m_currentFrame = (m_currentFrame + 1) % m_numFrames;
        }
    }
}

void NPC::setWanderArea(float minX, float minY, float maxX, float maxY) {
    m_minX = minX;
    m_minY = minY;
    m_maxX = maxX;
    m_maxY = maxY;
}