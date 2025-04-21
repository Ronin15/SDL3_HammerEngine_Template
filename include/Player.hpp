#ifndef PLAYER_HPP
#define PLAYER_HPP
#include "Entity.hpp"
#include "SDL3/SDL_surface.h"
#include "Vector2D.hpp"

class Player : public Entity{
public:
    Player();
    ~Player();

    void update()override;
    void render()override;
    void clean()override;

    // Accessor methods for protected members
    Vector2D getPosition() const { return m_position; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    std::string getTextureID() const { return m_textureID; }

private:
    void handleInput();
    void loadDimensionsFromTexture();
    int m_frameWidth{0}; // Width of a single animation frame
    int m_spriteSheetRows{0}; // Number of rows in the sprite sheet
    Uint64 m_lastFrameTime{0}; // Time of last animation frame change
    SDL_FlipMode m_flip{SDL_FLIP_NONE}; // Default flip direction
};
#endif // PLAYER_HPP
