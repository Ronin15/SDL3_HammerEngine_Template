#ifndef PLAYER_HPP
#define PLAYER_HPP
#include "Entity.hpp"
#include "Vector2D.hpp"

class Player : public Entity{
public:
    Player(SDL_Renderer* renderer);
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
    SDL_Renderer* m_pRenderer{nullptr}; // Renderer pointer
    int m_frameWidth{0}; // Width of a single animation frame
    int m_spriteSheetRows{2}; // Number of rows in the sprite sheet
    Uint64 m_lastFrameTime{0}; // Time of last animation frame change

};

#endif // PLAYER_HPP
