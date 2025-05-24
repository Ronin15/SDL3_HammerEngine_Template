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
#include <memory>

class NPC : public Entity {
public:
    NPC(const std::string& textureID, const Vector2D& startPosition, int frameWidth, int frameHeight);
    ~NPC() override;

    // Factory method to ensure proper shared_ptr creation
    // Factory method to ensure NPCs are always created with shared_ptr
    static std::shared_ptr<NPC> create(const std::string& textureID, const Vector2D& startPosition, int frameWidth = 0, int frameHeight = 0) {
        return std::make_shared<NPC>(textureID, startPosition, frameWidth, frameHeight);
    }

    void update() override;
    void render() override;
    void clean() override;

    // No state management - handled by AI Manager
    void setPosition(const Vector2D& position) override;
    
    // NPC-specific accessor methods
    SDL_FlipMode getFlip() const override { return m_flip; }
    
    // NPC-specific setter methods
    void setFlip(SDL_FlipMode flip) override { m_flip = flip; }
    
    // AI-specific methods
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
    bool m_boundsCheckEnabled{false};
};

#endif // NPC_HPP