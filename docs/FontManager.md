# FontManager Documentation

## Overview

The FontManager provides a centralized system for loading, managing, and rendering text in the Forge Game Engine. It handles TTF and OTF font formats with support for directory-based batch loading and efficient text rendering to textures or directly to the screen.

## Key Features

- **TTF/OTF Font Support**: Load TrueType and OpenType fonts
- **Directory Loading**: Batch load all fonts from a directory
- **Text Rendering**: Render text to textures or directly to screen
- **Memory Management**: Automatic font cleanup with RAII
- **Thread Safety**: Safe to use across multiple threads
- **Efficient Caching**: Fonts are cached for reuse

## Initialization

```cpp
// Initialize the font system
if (!FontManager::Instance().init()) {
    std::cerr << "Failed to initialize FontManager" << std::endl;
    return false;
}
```

## Loading Fonts

### Single Font Loading

```cpp
// Load a single font file
bool success = FontManager::Instance().loadFont(
    "assets/fonts/Arial.ttf",  // Font file path
    "Arial",                   // Font ID for reference
    24                         // Font size in pixels
);
```

### Directory Loading

```cpp
// Load all TTF/OTF fonts from a directory
bool success = FontManager::Instance().loadFont(
    "assets/fonts/",           // Directory path
    "ui_fonts",               // ID prefix for all fonts
    16                        // Font size for all fonts
);

// Creates font IDs like: "ui_fonts_Arial", "ui_fonts_Times", etc.
```

## Text Rendering

### Render to Texture

```cpp
// Create a text texture for later use
auto textTexture = FontManager::Instance().renderText(
    "Hello World!",           // Text to render
    "Arial",                  // Font ID
    {255, 255, 255, 255},    // Color (RGBA)
    renderer                  // SDL Renderer
);

if (textTexture) {
    // Use the texture for rendering
    // Texture is automatically managed by shared_ptr
}
```

### Direct Screen Rendering

```cpp
// Render text directly to the screen
FontManager::Instance().drawText(
    "Score: 1000",            // Text to render
    "Arial",                  // Font ID
    400, 50,                  // Center position (x, y)
    {255, 255, 0, 255},      // Yellow color
    renderer                  // SDL Renderer
);
```

## Font Management

### Check Font Availability

```cpp
if (FontManager::Instance().isFontLoaded("Arial")) {
    // Font is available for use
}
```

### Remove Specific Font

```cpp
FontManager::Instance().clearFont("Arial");
```

### Clear All Fonts

```cpp
FontManager::Instance().clearAllFonts();
```

## Usage Examples

### Game UI Text

```cpp
class GameUI {
private:
    SDL_Renderer* m_renderer;
    
public:
    void initializeFonts() {
        // Load UI fonts
        FontManager::Instance().loadFont("assets/fonts/", "ui", 16);
        FontManager::Instance().loadFont("assets/fonts/Impact.ttf", "title", 48);
    }
    
    void renderScore(int score) {
        std::string scoreText = "Score: " + std::to_string(score);
        FontManager::Instance().drawText(
            scoreText, "ui_Arial", 10, 10, 
            {255, 255, 255, 255}, m_renderer
        );
    }
    
    void renderTitle() {
        FontManager::Instance().drawText(
            "GAME TITLE", "title", 400, 100,
            {255, 215, 0, 255}, m_renderer  // Gold color
        );
    }
};
```

### Dialog System

```cpp
class DialogSystem {
public:
    void renderDialog(const std::string& speaker, const std::string& text) {
        // Render speaker name
        FontManager::Instance().drawText(
            speaker, "ui_bold", 50, 450,
            {200, 200, 255, 255}, renderer
        );
        
        // Render dialog text
        FontManager::Instance().drawText(
            text, "ui_regular", 50, 480,
            {255, 255, 255, 255}, renderer
        );
    }
};
```

### Performance-Optimized Text

```cpp
class PerformantUI {
private:
    std::shared_ptr<SDL_Texture> m_scoreTexture;
    int m_lastScore = -1;
    
public:
    void updateScore(int newScore) {
        // Only re-render if score changed
        if (newScore != m_lastScore) {
            std::string scoreText = "Score: " + std::to_string(newScore);
            m_scoreTexture = FontManager::Instance().renderText(
                scoreText, "ui_Arial", {255, 255, 255, 255}, renderer
            );
            m_lastScore = newScore;
        }
    }
    
    void renderScore() {
        if (m_scoreTexture) {
            // Fast texture rendering
            SDL_FRect destRect = {10, 10, 200, 30};
            SDL_RenderTexture(renderer, m_scoreTexture.get(), nullptr, &destRect);
        }
    }
};
```

## Best Practices

### Font Loading Strategy

```cpp
void loadGameFonts() {
    // Load fonts at startup for best performance
    FontManager& fontMgr = FontManager::Instance();
    
    // UI fonts - smaller sizes
    fontMgr.loadFont("assets/fonts/UI/", "ui", 16);
    
    // Title fonts - larger sizes
    fontMgr.loadFont("assets/fonts/Impact.ttf", "title_large", 48);
    fontMgr.loadFont("assets/fonts/Impact.ttf", "title_medium", 32);
    
    // Dialog fonts
    fontMgr.loadFont("assets/fonts/Dialog.ttf", "dialog", 18);
}
```

### Memory Management

```cpp
// Fonts are automatically cleaned up when FontManager is destroyed
// Manual cleanup if needed:
void cleanupFonts() {
    FontManager::Instance().clearAllFonts();
}
```

### Error Handling

```cpp
bool initializeTextSystem() {
    if (!FontManager::Instance().init()) {
        std::cerr << "FontManager initialization failed" << std::endl;
        return false;
    }
    
    if (!FontManager::Instance().loadFont("assets/fonts/main.ttf", "main", 16)) {
        std::cerr << "Failed to load main font" << std::endl;
        return false;
    }
    
    return true;
}
```

## Thread Safety

FontManager is thread-safe for concurrent access:

```cpp
// Safe to call from multiple threads
std::thread renderThread([&]() {
    FontManager::Instance().drawText("Thread Text", "Arial", 100, 100, 
                                   {255, 255, 255, 255}, renderer);
});
```

## Performance Considerations

- **Pre-load fonts**: Load all fonts at startup rather than during gameplay
- **Cache textures**: For frequently changing text, cache textures when possible
- **Batch directory loading**: Use directory loading for better startup performance
- **Appropriate sizes**: Load fonts at the sizes you'll actually use

## File Format Support

- **TTF**: TrueType Font files
- **OTF**: OpenType Font files

## Integration with Game States

```cpp
class GameState {
public:
    virtual bool enter() {
        // Load state-specific fonts
        return FontManager::Instance().loadFont("assets/fonts/state.ttf", "state", 20);
    }
    
    virtual bool exit() {
        // Clean up state-specific fonts if needed
        FontManager::Instance().clearFont("state");
        return true;
    }
};
```

## See Also

- `include/managers/FontManager.hpp` - Complete API reference
- `docs/TextureManager.md` - Texture management documentation
- `docs/README.md` - General manager system overview