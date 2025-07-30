# TextureManager Documentation

**Where to find the code:**
- Implementation: `src/managers/TextureManager.cpp`
- Header: `include/managers/TextureManager.hpp`

**Singleton Access:** Use `TextureManager::Instance()` to access the manager.

## Overview

The TextureManager provides a centralized system for loading, managing, and rendering textures in the Hammer Game Engine. It handles PNG image files with support for directory-based batch loading, sprite animations, parallax scrolling, and efficient texture management with automatic memory cleanup.

## Key Features

- **PNG Image Support**: Load PNG texture files
- **Directory Loading**: Batch load all PNG files from a directory
- **Sprite Animation**: Frame-based animation system
- **Parallax Scrolling**: Background scrolling effects
- **Memory Management**: Automatic texture cleanup with RAII
- **Efficient Caching**: Textures are cached for reuse

## Basic Usage

### Initialization

The TextureManager is automatically initialized when first accessed:

```cpp
// TextureManager is ready to use immediately
TextureManager& texMgr = TextureManager::Instance();
```

### Loading Textures

#### Single Texture Loading

```cpp
// Load a single PNG file
bool success = TextureManager::Instance().load(
    "assets/textures/player.png",  // Texture file path
    "player",                      // Texture ID for reference
    renderer                       // SDL Renderer
);
```

#### Directory Loading

```cpp
// Load all PNG files from a directory
bool success = TextureManager::Instance().load(
    "assets/textures/enemies/",    // Directory path
    "enemy",                       // ID prefix for all textures
    renderer                       // SDL Renderer
);
// Creates texture IDs like: "enemy_goblin", "enemy_orc", etc.
```

## Texture Rendering

### Basic Drawing

```cpp
// Draw a texture at position with size
TextureManager::Instance().draw(
    "player",                      // Texture ID
    100, 200,                      // Position (x, y)
    64, 64,                        // Size (width, height)
    renderer,                      // SDL Renderer
    SDL_FLIP_NONE                  // Flip mode (optional)
);
```

### Sprite Animation

```cpp
// Draw a specific frame from a sprite sheet
TextureManager::Instance().drawFrame(
    "player_walk",                 // Texture ID
    playerX, playerY,              // Position
    64, 64,                        // Frame size
    1,                             // Current row (1-based)
    currentFrame,                  // Current frame number
    renderer,                      // SDL Renderer
    SDL_FLIP_HORIZONTAL           // Flip mode (optional)
);
```

### Parallax Scrolling

```cpp
// Create scrolling background effect
TextureManager::Instance().drawParallax(
    "background",                  // Texture ID
    100,                           // X position
    50,                            // Y position
    150,                           // Scroll offset
    renderer                       // SDL Renderer
);
```

## Advanced Features

### Texture Queries

```cpp
// Check if texture exists
if (TextureManager::Instance().isTextureInMap("player")) {
    // Texture is loaded and ready
}

// Get texture directly for custom operations
if (auto texture = TextureManager::Instance().getTexture("player")) {
    // Texture is available for use
    float width, height;
    SDL_GetTextureSize(texture.get(), &width, &height);
    std::cout << "Texture size: " << width << "x" << height << std::endl;
}
```

### Memory Management

```cpp
// Remove specific texture
TextureManager::Instance().clearFromTexMap("old_texture");

// Clear all textures
TextureManager::Instance().clean();
```

## API Reference

### Core Methods

```cpp
// Loading
bool load(const std::string& fileName, const std::string& textureID, SDL_Renderer* p_renderer);

// Drawing
void draw(const std::string& textureID, int x, int y, int width, int height, SDL_Renderer* p_renderer, SDL_FlipMode flip = SDL_FLIP_NONE);
void drawFrame(const std::string& textureID, int x, int y, int width, int height, int currentRow, int currentFrame, SDL_Renderer* p_renderer, SDL_FlipMode flip = SDL_FLIP_NONE);
void drawParallax(const std::string& textureID, int x, int y, int scroll, SDL_Renderer* p_renderer);

// Texture management
void clearFromTexMap(const std::string& textureID);
bool isTextureInMap(const std::string& textureID) const;
std::shared_ptr<SDL_Texture> getTexture(const std::string& textureID) const;
void clean();
bool isShutdown() const;
```

## Best Practices

- Load all required textures at game or state startup for best performance.
- Use descriptive texture IDs for easy management.
- Use directory loading for batch import of textures.
- Always check if a texture is loaded before drawing.
- Call `clean()` on shutdown to free resources.

## Integration with Other Systems

The TextureManager works seamlessly with:
- **[UIManager](../ui/UIManager_Guide.md)**: UI textures and icons
- **[GameEngine](../GameEngine.md)**: Integrated into main game loop
- **GameStates**: State-specific texture loading and management

The TextureManager provides essential graphics capabilities that enhance the game experience while maintaining efficient resource usage and easy integration with game logic.
