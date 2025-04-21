#include "Player.hpp"
#include "InputHandler.hpp"
#include "TextureManager.hpp"
#include <SDL3/SDL.h>
#include <iostream>

Player::Player(SDL_Renderer* renderer) : m_pRenderer(renderer) {
    // Initialize player properties
    m_position = Vector2D(400, 300);  // Start position in the middle of a typical screen
    m_velocity = Vector2D(0, 0);
    m_acceleration = Vector2D(0, 0);
    m_textureID = "player";  // Texture ID as loaded by TextureManager from res/img directory

    // Animation properties
    m_currentFrame = 0;      // Start with first frame
    m_currentRow = 1;        // In TextureManager::drawFrame, rows start at 1
    m_numFrames = 2;         // Number of frames in the animation
    m_animSpeed = 100;       // Animation speed in milliseconds
    m_spriteSheetRows = 2;   // Number of rows in the sprite sheet
    m_lastFrameTime = SDL_GetTicks(); // Track when we last changed animation frame

    // Set width and height based on texture dimensions if the texture is loaded
    loadDimensionsFromTexture();

    std::cout << "Forge Game Engine - Player created" << std::endl;
}

// Helper method to get dimensions from the loaded texture
void Player::loadDimensionsFromTexture() {
    // Default dimensions in case texture loading fails
    m_width = 128;
    m_height = 128;  // Set height equal to the sprite sheet row height
    m_frameWidth = 64;  // Default frame width (width/numFrames)

    // Get the texture from TextureManager
    if (TextureManager::Instance()->isTextureInMap(m_textureID)) {
        SDL_Texture* texture = TextureManager::Instance()->getTexture(m_textureID);
        if (texture != nullptr) {
            float width = 0.0f;
            float height = 0.0f;
            // Query the texture to get its width and height
            // SDL3 uses SDL_GetTextureSize which returns float dimensions and returns a bool
            if (SDL_GetTextureSize(texture, &width, &height)) {
                std::cout << "Forge Game Engine - Original texture dimensions: " << width << "x" << height << std::endl;

                // Store original dimensions for full sprite sheet
                m_width = static_cast<int>(width);
                m_height = static_cast<int>(height);

                // Calculate frame dimensions based on sprite sheet layout
                m_frameWidth = m_width / m_numFrames; // Width per frame
                int frameHeight = m_height / m_spriteSheetRows; // Height per row

                // Update height to be the height of a single frame
                m_height = frameHeight;

                std::cout << "Forge Game Engine - Loaded texture dimensions: " << m_width << "x" << height << std::endl;
                std::cout << "Forge Game Engine - Frame dimensions: " << m_frameWidth << "x" << frameHeight << std::endl;
                std::cout << "Forge Game Engine - Sprite layout: " << m_numFrames << " columns x " << m_spriteSheetRows << " rows" << std::endl;
            } else {
                std::cout << "Forge Game Engine - Failed to query texture dimensions: " << SDL_GetError() << std::endl;
            }
        }
    } else {
        std::cout << "Forge Game Engine - Texture '" << m_textureID << "' not found in TextureManager" << std::endl;
    }
}

Player::~Player() {
    clean();
    std::cout << "Forge Game Engine - Player destroyed" << std::endl;
}

void Player::update() {
    // Handle input
    handleInput();

    // Update position based on velocity
    m_velocity += m_acceleration;
    m_position += m_velocity;

    // Reset acceleration
    m_acceleration = Vector2D(0, 0);

    // Get current time for animation updates
    Uint64 currentTime = SDL_GetTicks();

    // Animation handling
    bool isMoving = (m_velocity.getX() != 0 || m_velocity.getY() != 0);

    // Handle animation based on movement state
    if (isMoving) {
        // Time-based animation - only update frame when enough time has passed
        if (currentTime > m_lastFrameTime + m_animSpeed) {
            // Advance to next frame
            m_currentFrame = (m_currentFrame + 1) % m_numFrames;
            m_lastFrameTime = currentTime; // Reset timer

            // Log animation updates for debugging
            std::cout << "Forge Game Engine - Animation frame: " << m_currentFrame << ", Row: " << m_currentRow << std::endl;
        }

        // Determine row based on direction (facing)
        if (m_velocity.getX() > 0) {
            // Moving right
            m_currentRow = 2; // Second row (right-facing)
        } else if (m_velocity.getX() < 0) {
            // Moving left
            m_currentRow = 1; // First row (left-facing)
        }
    } else {
        // If standing still, use first frame but keep current direction (row)
        m_currentFrame = 0;
    }

    // If the texture dimensions haven't been loaded yet, try loading them
    if (m_frameWidth == 0 && TextureManager::Instance()->isTextureInMap(m_textureID)) {
        loadDimensionsFromTexture();
    }
}

void Player::render() {
    // Render player using the TextureManager with animation frame
    TextureManager::Instance()->drawFrame(
        m_textureID,
        static_cast<int>(m_position.getX() - m_frameWidth / 2.0f), // Center based on frame width
        static_cast<int>(m_position.getY() - m_height / 2.0f),
        m_frameWidth,           // Use the calculated frame width
        m_height,               // Height stays the same
        m_currentRow,           // Current animation row
        m_currentFrame,         // Current animation frame
        m_pRenderer
    );

    // Log rendering information (commented out to reduce console spam during animation)
    // std::cout << "Forge Game Engine - Rendering player at position: " << m_position.getX() << ", " << m_position.getY() << std::endl;
}

void Player::clean() {
    // Clean up any resources
    std::cout << "Forge Game Engine - Cleaning player resources" << std::endl;
}

void Player::handleInput() {
    // Get input handler instance
    InputHandler* inputHandler = InputHandler::Instance();

    // Handle keyboard movement
    if (inputHandler->isKeyDown(SDL_SCANCODE_RIGHT)) {
        m_velocity.setX(2);
    } else if (inputHandler->isKeyDown(SDL_SCANCODE_LEFT)) {
        m_velocity.setX(-2);
    } else {
        m_velocity.setX(0);
    }

    if (inputHandler->isKeyDown(SDL_SCANCODE_UP)) {
        m_velocity.setY(-2);
    } else if (inputHandler->isKeyDown(SDL_SCANCODE_DOWN)) {
        m_velocity.setY(2);
    } else {
        m_velocity.setY(0);
    }

    // Handle gamepad input if a gamepad is connected
    if (inputHandler->getAxisX(0, 1) != 0) {
        m_velocity.setX(2.0f * static_cast<float>(inputHandler->getAxisX(0, 1)) / 32767.0f);
    }

    if (inputHandler->getAxisY(0, 1) != 0) {
        m_velocity.setY(2.0f * static_cast<float>(inputHandler->getAxisY(0, 1)) / 32767.0f);
    }

    // Handle mouse input for direction or actions
    if (inputHandler->getMouseButtonState(LEFT)) {
        Vector2D* target = inputHandler->getMousePosition();
        Vector2D direction = (*target - m_position);
        direction.normalize(); // Normalize to get direction vector
        m_velocity = direction * 2; // Move towards clicked position
    }
}
