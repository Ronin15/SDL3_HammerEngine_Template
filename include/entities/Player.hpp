/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PLAYER_HPP
#define PLAYER_HPP

#include "entities/Entity.hpp"
#include "managers/EntityStateManager.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>

class Player : public Entity{
public:
    Player();
    ~Player();

    void update()override;
    void render()override;
    void clean()override;

    // State management
    void changeState(const std::string& stateName);
    std::string getCurrentStateName() const;
    void setPosition(const Vector2D& m_position) override;
    //void setVelocity(const Vector2D& m_velocity); for later in save manager
    //void setFlip(SDL_FlipMode m_flip);

    // Player-specific accessor methods
    SDL_FlipMode getFlip() const override { return m_flip; }
    
    // Player-specific setter methods
    void setFlip(SDL_FlipMode flip) override { m_flip = flip; }

private:
    void handleInput();
    void loadDimensionsFromTexture();
    void setupStates();

    EntityStateManager m_stateManager;
    int m_frameWidth{0}; // Width of a single animation frame
    int m_spriteSheetRows{0}; // Number of rows in the sprite sheet
    Uint64 m_lastFrameTime{0}; // Time of last animation frame change
    SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction
};
#endif // PLAYER_HPP
