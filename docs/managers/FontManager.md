# FontManager Documentation

**Where to find the code:**
- Implementation: `src/managers/FontManager.cpp`
- Header: `include/managers/FontManager.hpp`

**Singleton Access:** Use `FontManager::Instance()` to access the manager.

## Overview

The FontManager provides a centralized system for loading, managing, and rendering text in the Hammer Game Engine. It integrates seamlessly with the DPI-aware font system and UI components to provide crisp, professional text rendering across all display types.

## Key Features

- **TTF/OTF Font Support**: Load TrueType and OpenType fonts with high quality
- **Cross-Platform Font Sizing**: Automatic font sizing based on screen resolution and platform
- **Native Resolution Rendering**: Crisp text rendering without scaling blur
- **Dynamic Auto-Sizing**: Fonts automatically adapt to current display characteristics
- **Text Measurement**: Precise text dimension calculation for auto-sizing systems
- **High-Quality Rendering**: TTF hinting, kerning, and pixel-perfect scaling
- **Directory Loading**: Batch load fonts from directories
- **Thread Safety**: Safe to use across multiple threads
- **Memory Management**: Automatic font cleanup with RAII

## Quick Start

### Initialization

```cpp
// Initialize the font system
if (!FontManager::Instance().init()) {
    std::cerr << "Failed to initialize FontManager" << std::endl;
    return false;
}
```

### Cross-Platform Font Loading

```cpp
// Load fonts with sizes calculated based on display properties
FontManager& fontMgr = FontManager::Instance();
fontMgr.loadFontsForDisplay("res/fonts", windowWidth, windowHeight);
// Font IDs created:
// - fonts: Base font for general content
// - fonts_UI: UI font for interface elements
// - fonts_title: Title font for headers
// - fonts_tooltip: Smaller font for tooltips
```

### Manual Font Loading

```cpp
// Load specific font with custom size
bool success = fontMgr.loadFont(
    "res/fonts/arial.ttf",     // Font file path
    "custom_font",             // Font ID for reference
    24                         // Font size (will be DPI-scaled automatically)
);
```

## Text Rendering

### Basic Text Rendering

```cpp
// Render text directly to screen (centered by default)
fontMgr.drawText(
    "Hello World",               // Text to render
    "fonts",                     // Font ID
    400, 300,                    // Position (x, y)
    {255, 255, 255, 255},        // Color (white)
    renderer                     // SDL renderer
);

// Render text with alignment (0=center, 1=left, 2=right, 3=top-left, 4=top-center, 5=top-right)
fontMgr.drawTextAligned(
    "Aligned Text",
    "fonts_title",
    400, 300,                    // Position
    {255, 215, 0, 255},          // Gold color
    renderer,
    1                            // Left alignment
);
```

### Text to Texture Rendering

```cpp
// Create text texture for caching or complex layouts
std::shared_ptr<SDL_Texture> textTexture = fontMgr.renderText(
    "Cached Text",
    "fonts_UI",
    {200, 200, 200, 255},        // Gray color
    renderer
);

// Use the texture in your rendering
SDL_FRect destRect = {x, y, width, height};
SDL_RenderTexture(renderer, textTexture.get(), nullptr, &destRect);
```

### Multi-Line and Wrapped Text

```cpp
// Render multi-line text (handles newlines automatically)
fontMgr.drawText(
    "Line 1\nLine 2\nLine 3",
    "fonts",
    400, 300,
    {255, 255, 255, 255},
    renderer
);

// Render text with word wrapping
fontMgr.drawTextWithWrapping(
    "This is a long string that will wrap to fit within 300px.",
    "fonts_UI",
    100, 100,                    // Top-left position
    300,                         // Max width
    {255, 255, 255, 255},
    renderer
);
```

## Text Measurement

### Single-Line Text

```cpp
// Measure text dimensions for UI auto-sizing
int width, height;
bool success = fontMgr.measureText(
    "Sample Text",
    "fonts_UI",
    &width,
    &height
);
if (success) {
    std::cout << "Text size: " << width << "x" << height << " pixels" << std::endl;
}
```

### Multi-Line and Wrapped Text

```cpp
// Measure multi-line text (newlines)
int width, height;
bool success = fontMgr.measureMultilineText(
    "Line 1\nLine 2\nLine 3",
    "fonts",
    0,      // No max width
    &width,
    &height
);

// Measure text with word wrapping
success = fontMgr.measureTextWithWrapping(
    "This is a long string that will wrap.",
    "fonts_UI",
    300,    // Max width
    &width,
    &height
);
```

### Font Metrics

```cpp
// Get font metrics for layout calculations
int lineHeight, ascent, descent;
bool success = fontMgr.getFontMetrics(
    "fonts",
    &lineHeight,
    &ascent,
    &descent
);
```

## Font Management

```cpp
// Check if a font is loaded
if (fontMgr.isFontLoaded("fonts_UI")) {
    // Use the font
}

// Remove a specific font
fontMgr.clearFont("custom_font");

// Clean up all fonts and shut down
fontMgr.clean();
```

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
bool init();
void clean();

// Font loading
bool loadFont(const std::string& fontFile, const std::string& fontID, int fontSize);
bool loadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight);
bool refreshFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight);

// Font management
bool isFontLoaded(const std::string& fontID) const;
void clearFont(const std::string& fontID);

// Text rendering
void drawText(const std::string& text, const std::string& fontID, int x, int y, SDL_Color color, SDL_Renderer* renderer);
void drawTextAligned(const std::string& text, const std::string& fontID, int x, int y, SDL_Color color, SDL_Renderer* renderer, int alignment = 0);
void drawTextWithWrapping(const std::string& text, const std::string& fontID, int x, int y, int maxWidth, SDL_Color color, SDL_Renderer* renderer);
std::shared_ptr<SDL_Texture> renderText(const std::string& text, const std::string& fontID, SDL_Color color, SDL_Renderer* renderer);
std::shared_ptr<SDL_Texture> renderMultiLineText(const std::string& text, TTF_Font* font, SDL_Color color, SDL_Renderer* renderer);

// Text measurement
bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);
bool measureMultilineText(const std::string& text, const std::string& fontID, int maxWidth, int* width, int* height);
bool measureTextWithWrapping(const std::string& text, const std::string& fontID, int maxWidth, int* width, int* height);
bool getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent);
std::vector<std::string> wrapTextToLines(const std::string& text, const std::string& fontID, int maxWidth);
```

## Integration with Other Systems

The FontManager works seamlessly with:
- **[UIManager](../ui/UIManager_Guide.md)**: Dynamic text measurement for component auto-sizing and grow-only lists
- **[GameEngine](../GameEngine.md)**: Cross-platform coordinate system and native resolution rendering
- **[InputManager](../InputManager.md)**: Automatic font refresh during window resize events
- **[Auto-Sizing System](../ui/Auto_Sizing_System.md)**: Real-time text measurement for content-aware sizing

The FontManager serves as the foundation for all text rendering in the engine, providing cross-platform font sizing, crisp rendering, and dynamic measurement capabilities that enable the UI system's advanced features like auto-sizing and resolution adaptation.
