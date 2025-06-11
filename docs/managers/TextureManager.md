# TextureManager Documentation

## Overview

The TextureManager provides a centralized system for loading, managing, and rendering textures in the Forge Game Engine. It handles PNG image files with support for directory-based batch loading, sprite animations, parallax scrolling, and efficient texture management with automatic memory cleanup.

## Key Features

- **PNG Image Support**: Load PNG texture files
- **Directory Loading**: Batch load all PNG files from a directory
- **Sprite Animation**: Frame-based animation system
- **Parallax Scrolling**: Background scrolling effects
- **Memory Management**: Automatic texture cleanup with RAII
- **Thread Safety**: Safe texture access across threads
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
    0,                             // Current row
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
    cameraX,                       // Camera X position
    0.5f,                         // Scroll speed factor (0.0-1.0)
    renderer                       // SDL Renderer
);
```

## Advanced Features

### Texture Queries

```cpp
// Check if texture exists
if (TextureManager::Instance().hasTexture("player")) {
    // Texture is loaded and ready
}

// Get texture dimensions
int width, height;
if (TextureManager::Instance().getTextureDimensions("player", width, height)) {
    std::cout << "Texture size: " << width << "x" << height << std::endl;
}
```

### Memory Management

```cpp
// Remove specific texture
TextureManager::Instance().clearTexture("old_texture");

// Clear all textures
TextureManager::Instance().clearAllTextures();
```

## Usage Examples

### Player Animation System

```cpp
class Player {
private:
    int m_currentFrame = 0;
    int m_animationTimer = 0;
    const int m_frameDelay = 100; // milliseconds
    bool m_facingRight = true;
    
public:
    void loadTextures(SDL_Renderer* renderer) {
        // Load player sprite sheets
        TextureManager::Instance().load("assets/player/idle.png", "player_idle", renderer);
        TextureManager::Instance().load("assets/player/walk.png", "player_walk", renderer);
        TextureManager::Instance().load("assets/player/jump.png", "player_jump", renderer);
    }
    
    void update(uint32_t deltaTime) {
        m_animationTimer += deltaTime;
        if (m_animationTimer >= m_frameDelay) {
            m_currentFrame = (m_currentFrame + 1) % 8; // 8-frame animation
            m_animationTimer = 0;
        }
    }
    
    void render(SDL_Renderer* renderer) {
        std::string textureID = "player_idle";
        if (m_isMoving) textureID = "player_walk";
        if (m_isJumping) textureID = "player_jump";
        
        SDL_FlipMode flip = m_facingRight ? SDL_FLIP_NONE : SDL_FLIP_HORIZONTAL;
        
        TextureManager::Instance().drawFrame(
            textureID, m_x, m_y, 64, 64, 0, m_currentFrame, renderer, flip
        );
    }
};
```

### Background System with Parallax

```cpp
class BackgroundRenderer {
private:
    float m_cameraX = 0.0f;
    
public:
    void loadBackgrounds(SDL_Renderer* renderer) {
        // Load background layers
        TextureManager::Instance().load("assets/bg/", "bg", renderer);
    }
    
    void render(SDL_Renderer* renderer, float cameraX) {
        m_cameraX = cameraX;
        
        // Multiple parallax layers for depth
        TextureManager::Instance().drawParallax("bg_sky", m_cameraX, 0.1f, renderer);
        TextureManager::Instance().drawParallax("bg_mountains", m_cameraX, 0.3f, renderer);
        TextureManager::Instance().drawParallax("bg_trees", m_cameraX, 0.6f, renderer);
        TextureManager::Instance().drawParallax("bg_ground", m_cameraX, 0.9f, renderer);
    }
};
```

### UI Texture Management

```cpp
class UIManager {
public:
    void initializeUI(SDL_Renderer* renderer) {
        // Load UI textures from directory
        TextureManager::Instance().load("assets/ui/", "ui", renderer);
        
        // Load specific UI elements
        TextureManager::Instance().load("assets/ui/buttons/", "btn", renderer);
        TextureManager::Instance().load("assets/ui/icons/", "icon", renderer);
    }
    
    void renderHealthBar(int health, int maxHealth, SDL_Renderer* renderer) {
        // Background
        TextureManager::Instance().draw("ui_health_bg", 10, 10, 200, 20, renderer);
        
        // Health fill
        int healthWidth = (health * 200) / maxHealth;
        TextureManager::Instance().draw("ui_health_fill", 10, 10, healthWidth, 20, renderer);
        
        // Health icon
        TextureManager::Instance().draw("icon_heart", 220, 10, 20, 20, renderer);
    }
};
```

### Tile-Based Level System

```cpp
class TileMap {
private:
    std::vector<std::vector<int>> m_tileData;
    const int m_tileSize = 32;
    
public:
    void loadTilesets(SDL_Renderer* renderer) {
        // Load tileset textures
        TextureManager::Instance().load("assets/tiles/grass.png", "tile_grass", renderer);
        TextureManager::Instance().load("assets/tiles/stone.png", "tile_stone", renderer);
        TextureManager::Instance().load("assets/tiles/water.png", "tile_water", renderer);
    }
    
    void renderTiles(SDL_Renderer* renderer, float cameraX, float cameraY) {
        for (int y = 0; y < m_tileData.size(); ++y) {
            for (int x = 0; x < m_tileData[y].size(); ++x) {
                int tileType = m_tileData[y][x];
                if (tileType == 0) continue; // Empty tile
                
                int screenX = (x * m_tileSize) - cameraX;
                int screenY = (y * m_tileSize) - cameraY;
                
                std::string textureID = getTileTexture(tileType);
                TextureManager::Instance().draw(
                    textureID, screenX, screenY, m_tileSize, m_tileSize, renderer
                );
            }
        }
    }
    
private:
    std::string getTileTexture(int tileType) {
        switch (tileType) {
            case 1: return "tile_grass";
            case 2: return "tile_stone";
            case 3: return "tile_water";
            default: return "tile_grass";
        }
    }
};
```

## Best Practices

### Texture Loading Strategy

```cpp
void loadGameTextures(SDL_Renderer* renderer) {
    TextureManager& texMgr = TextureManager::Instance();
    
    // Load textures at game startup for best performance
    texMgr.load("assets/players/", "player", renderer);
    texMgr.load("assets/enemies/", "enemy", renderer);
    texMgr.load("assets/items/", "item", renderer);
    texMgr.load("assets/tiles/", "tile", renderer);
    texMgr.load("assets/ui/", "ui", renderer);
    texMgr.load("assets/effects/", "fx", renderer);
}
```

### Memory Optimization

```cpp
class StateManager {
public:
    void changeState(GameState newState) {
        // Clear previous state textures
        clearStateTextures(m_currentState);
        
        // Load new state textures
        loadStateTextures(newState);
        
        m_currentState = newState;
    }
    
private:
    void clearStateTextures(GameState state) {
        switch (state) {
            case GameState::MENU:
                TextureManager::Instance().clearTexture("menu_bg");
                break;
            case GameState::GAMEPLAY:
                TextureManager::Instance().clearTexture("level_bg");
                break;
        }
    }
};
```

### Error Handling

```cpp
bool initializeGraphics(SDL_Renderer* renderer) {
    if (!TextureManager::Exists()) {
        std::cerr << "TextureManager not available" << std::endl;
        return false;
    }
    
    if (!TextureManager::Instance().load("assets/player.png", "player", renderer)) {
        std::cerr << "Failed to load player texture" << std::endl;
        return false;
    }
    
    return true;
}
```

## Performance Considerations

- **Batch Loading**: Use directory loading for better I/O performance
- **Texture Size**: Use appropriate texture sizes for your target resolution
- **Memory Usage**: Monitor texture memory usage, especially on mobile platforms
- **Atlas Textures**: Consider using texture atlases for small sprites
- **Pre-loading**: Load textures during loading screens, not during gameplay

## File Format Support

- **PNG**: Portable Network Graphics (primary supported format)
- **Alpha Channel**: Full alpha transparency support
- **Color Depth**: 8-bit, 16-bit, and 32-bit color support

## Thread Safety

TextureManager provides thread-safe access for reading operations:

```cpp
// Safe to call from multiple threads
std::thread renderThread([&]() {
    TextureManager::Instance().draw("sprite", 100, 100, 32, 32, renderer);
});
```

## Integration with Game Systems

### Resource Management

```cpp
class ResourceManager {
public:
    bool loadLevel(const std::string& levelName, SDL_Renderer* renderer) {
        std::string texturePath = "assets/levels/" + levelName + "/textures/";
        return TextureManager::Instance().load(texturePath, levelName, renderer);
    }
    
    void unloadLevel(const std::string& levelName) {
        // Clear level-specific textures
        TextureManager::Instance().clearTexture(levelName + "_*");
    }
};
```

### Animation System Integration

```cpp
class AnimationSystem {
private:
    struct Animation {
        std::string textureID;
        int frameCount;
        int frameDelay;
        bool looping;
    };
    
public:
    void playAnimation(const std::string& animID, SDL_Renderer* renderer) {
        Animation& anim = m_animations[animID];
        
        TextureManager::Instance().drawFrame(
            anim.textureID, m_x, m_y, m_frameWidth, m_frameHeight,
            0, m_currentFrame, renderer
        );
    }
};
```

## See Also

- `include/managers/TextureManager.hpp` - Complete API reference
- `docs/FontManager.md` - Font management documentation
- `docs/SoundManager.md` - Audio management documentation
- `docs/README.md` - General manager system overview