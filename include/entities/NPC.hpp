/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef NPC_HPP
#define NPC_HPP

#include "entities/Entity.hpp"

#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <string>

class NPC : public Entity {
public:
    NPC(const std::string& textureID, const Vector2D& startPosition, int frameWidth, int frameHeight);
    ~NPC();

    void update() override;
    void render() override;
    void clean() override;

    // No state management - handled by AI Manager
    void setPosition(const Vector2D& position) override;
    
    // Accessor methods for protected members
    Vector2D getPosition() const { return m_position; }
    Vector2D getVelocity() const { return m_velocity; }
    Vector2D getAcceleration() const { return m_acceleration; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    std::string getTextureID() const { return m_textureID; }
    int getCurrentFrame() const { return m_currentFrame; }
    int getCurrentRow() const { return m_currentRow; }
    SDL_FlipMode getFlip() const { return m_flip; }
    
    // Setter methods for state control
    void setVelocity(const Vector2D& velocity) override { m_velocity = velocity; }
    void setAcceleration(const Vector2D& acceleration) override { m_acceleration = acceleration; }
    void setCurrentFrame(int frame) override { m_currentFrame = frame; }
    void setCurrentRow(int row) override { m_currentRow = row; }
    void setFlip(SDL_FlipMode flip) override { m_flip = flip; }
    
    // AI-specific methods
    void setAnimSpeed(int speed) override { m_animSpeed = speed; }
    void setNumFrames(int numFrames) override { m_numFrames = numFrames; }
    void setWanderArea(float minX, float minY, float maxX, float maxY);
    
    // Enable or disable screen bounds checking
    void setBoundsCheckEnabled(bool enabled) { m_boundsCheckEnabled = enabled; }
    bool isBoundsCheckEnabled() const { return m_boundsCheckEnabled; }
    
private:
    void loadDimensionsFromTexture();

    int m_frameWidth{0};      // Width of a single animation frame
    int m_frameHeight{0};     // Height of a single animation frame
    int m_spriteSheetRows{0}; // Number of rows in the sprite sheet
    Uint64 m_lastFrameTime{0}; // Time of last animation frame change
    SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction
    
    // Wander area bounds
    float m_minX{0.0f};
    float m_minY{0.0f};
    float m_maxX{800.0f};
    float m_maxY{600.0f};
    
    // Flag to control bounds checking behavior
    bool m_boundsCheckEnabled{true};
};

#endif // NPC_HPP