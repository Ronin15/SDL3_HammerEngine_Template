# FontManager Documentation

## Overview

The FontManager provides a centralized system for loading, managing, and rendering text in the Forge Game Engine. It integrates seamlessly with the DPI-aware font system and UI components to provide crisp, professional text rendering across all display types.

## Key Features

- **TTF/OTF Font Support**: Load TrueType and OpenType fonts with high quality
- **DPI-Aware Scaling**: Automatic font sizing based on display pixel density
- **Text Measurement**: Precise text dimension calculation for auto-sizing systems
- **High-Quality Rendering**: TTF hinting, kerning, and anti-aliasing
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

// The FontManager automatically configures quality settings:
// - TTF hinting for better font outline quality
// - Kerning for proper character spacing
// - Linear texture scaling for smooth rendering
```

### Display-Aware Font Loading

```cpp
// Automatic display-aware font loading (recommended)
FontManager& fontMgr = FontManager::Instance();

// Load fonts with sizes calculated based on display properties
fontMgr.loadFontsForDisplay("res/fonts", windowWidth, windowHeight);

// This automatically creates DPI-scaled fonts:
// - fonts_Arial: Base font for general content
// - fonts_UI_Arial: UI font for interface elements
// - fonts_Title_Arial: Title font for headers
// - fonts_Tooltip_Arial: Smaller font for tooltips
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
// Render text directly to screen
fontMgr.drawText(
    "Hello World",               // Text to render
    "fonts_Arial",               // Font ID
    400, 300,                    // Position (x, y)
    {255, 255, 255, 255},        // Color (white)
    renderer                     // SDL renderer
);

// Centered text rendering
fontMgr.drawTextCentered(
    "Centered Text",
    "fonts_Title_Arial",
    400, 300,                    // Center position
    {255, 215, 0, 255},          // Gold color
    renderer
);
```

### Text to Texture Rendering

```cpp
// Create text texture for caching or complex layouts
SDL_Texture* textTexture = fontMgr.renderTextToTexture(
    "Cached Text",
    "fonts_UI_Arial",
    {200, 200, 200, 255},        // Gray color
    renderer
);

// Use the texture in your rendering
SDL_Rect destRect = {x, y, width, height};
SDL_RenderTexture(renderer, textTexture, nullptr, &destRect);
```

## Text Measurement

### Single-Line Text

```cpp
// Measure text dimensions for UI auto-sizing
int width, height;
bool success = fontMgr.measureText(
    "Sample Text",
    "fonts_UI_Arial",
    &width,
    &height
);

if (success) {
    std::cout << "Text size: " << width << "x" << height << " pixels" << std::endl;
}
```

### Multi-Line Text

```cpp
// Measure multi-line text (automatically detects newlines)
int width, height;
bool success = fontMgr.measureMultilineText(
    "Line 1\nLine 2\nLine 3",
    "fonts_Arial",
    400,                         // Maximum width (0 for no limit)
    &width,
    &height
);

// Returns total dimensions including all lines
```

### Font Metrics

```cpp
// Get font metrics for layout calculations
int lineHeight, ascent, descent;
bool success = fontMgr.getFontMetrics(
    "fonts_Arial",
    &lineHeight,
    &ascent,
    &descent
);

// Use metrics for precise text positioning
int baselineY = y + ascent;
int nextLineY = y + lineHeight;
```

## Integration with UI Systems

### Auto-Sizing Integration

The FontManager integrates seamlessly with the UI auto-sizing system:

```cpp
// UI components automatically use FontManager for text measurement
auto& ui = UIManager::Instance();

// Label automatically sizes to fit text using FontManager
ui.createLabel("dynamic", {x, y, 0, 0}, "Dynamic Content");

// Button automatically sizes to fit text plus padding
ui.createButton("action", {x, y, 0, 0}, "Click Me");

// Multi-line text automatically measured and sized
ui.createLabel("multiline", {x, y, 0, 0}, "Line 1\nLine 2\nLine 3");
```

### DPI Integration

```cpp
// FontManager automatically adapts to display DPI
float dpiScale = GameEngine::Instance().getDPIScale();

// Font sizes are automatically calculated based on DPI:
// Standard DPI (1.0x): 24px base font
// High DPI (2.0x): 48px base font  
// 4K/Retina (3.0x): 72px base font

// All text rendering uses DPI-appropriate fonts automatically
fontMgr.drawText("Text", "fonts_Arial", x, y, color, renderer);
```

## Directory Loading

### Batch Font Loading

```cpp
// Load all fonts from a directory
bool success = fontMgr.loadFontsFromDirectory(
    "res/fonts/",               // Directory path
    "game_fonts",               // Font ID prefix
    18                          // Base font size
);

// Creates font IDs like: "game_fonts_arial", "game_fonts_times", etc.
```

### Font Family Loading

```cpp
// Load multiple sizes of the same font
std::vector<int> sizes = {12, 18, 24, 36};
for (int size : sizes) {
    std::string fontID = "arial_" + std::to_string(size);
    fontMgr.loadFont("res/fonts/arial.ttf", fontID, size);
}
```

## Quality Settings

### Automatic Quality Configuration

The FontManager automatically configures optimal quality settings:

```cpp
// Applied to all loaded fonts automatically:
TTF_SetFontHinting(font, TTF_HINTING_NORMAL);    // Better outline quality
TTF_SetFontKerning(font, 1);                     // Proper character spacing
TTF_SetFontStyle(font, TTF_STYLE_NORMAL);        // Consistent rendering

// Texture quality settings:
SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);  // Smooth scaling
```

### Custom Quality Settings

```cpp
// Advanced font configuration (rarely needed)
fontMgr.setFontHinting("fonts_Arial", TTF_HINTING_LIGHT);
fontMgr.setFontKerning("fonts_Arial", false);
```

## Performance Optimization

### Font Caching

```cpp
// Fonts are automatically cached for reuse
bool isLoaded = fontMgr.isFontLoaded("fonts_Arial");

// Unload unused fonts to free memory
fontMgr.unloadFont("unused_font");

// Clear all fonts
fontMgr.clearAllFonts();
```

### Text Texture Caching

```cpp
// For frequently changing text, consider texture caching
class TextCache {
    std::unordered_map<std::string, SDL_Texture*> m_cache;
    
public:
    SDL_Texture* getOrCreateText(const std::string& text, const std::string& fontID) {
        auto it = m_cache.find(text);
        if (it != m_cache.end()) {
            return it->second;  // Return cached texture
        }
        
        // Create new texture and cache it
        auto texture = FontManager::Instance().renderTextToTexture(text, fontID, color, renderer);
        m_cache[text] = texture;
        return texture;
    }
};
```

## Error Handling

### Font Loading Errors

```cpp
// Check font loading success
if (!fontMgr.loadFont("res/fonts/arial.ttf", "arial", 24)) {
    std::cerr << "Failed to load font: arial.ttf" << std::endl;
    // Fall back to default font or handle error
}

// Verify font availability before use
if (!fontMgr.isFontLoaded("fonts_Arial")) {
    std::cerr << "Font not available: fonts_Arial" << std::endl;
    return;
}
```

### Rendering Errors

```cpp
// Text measurement can fail with invalid fonts or empty text
int width, height;
if (!fontMgr.measureText("", "invalid_font", &width, &height)) {
    // Handle measurement failure
    width = 0;
    height = 0;
}
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
bool loadFontsFromDirectory(const std::string& directory, const std::string& prefix, int fontSize);

// Font management
bool isFontLoaded(const std::string& fontID) const;
void unloadFont(const std::string& fontID);
void clearAllFonts();

// Text rendering
void drawText(const std::string& text, const std::string& fontID, int x, int y, 
              SDL_Color color, SDL_Renderer* renderer);
void drawTextCentered(const std::string& text, const std::string& fontID, int x, int y,
                      SDL_Color color, SDL_Renderer* renderer);
SDL_Texture* renderTextToTexture(const std::string& text, const std::string& fontID,
                                 SDL_Color color, SDL_Renderer* renderer);

// Text measurement
bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);
bool measureMultilineText(const std::string& text, const std::string& fontID, 
                         int maxWidth, int* width, int* height);
bool getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent);
```

### Integration Methods

```cpp
// DPI integration
float getDPIScale() const;  // Get current DPI scale factor

// Quality configuration
void setFontHinting(const std::string& fontID, int hinting);
void setFontKerning(const std::string& fontID, bool enable);
```

## Best Practices

### Font Loading

```cpp
// ✅ GOOD: Use display-aware loading for UI fonts
fontMgr.loadFontsForDisplay("res/fonts", windowWidth, windowHeight);

// ✅ GOOD: Load fonts once during initialization
void GameState::enter() {
    fontMgr.loadFont("res/fonts/special.ttf", "special_font", 32);
}

// ❌ BAD: Don't load fonts every frame
void GameState::render() {
    fontMgr.loadFont("res/fonts/arial.ttf", "arial", 24);  // Inefficient
    fontMgr.drawText("Text", "arial", x, y, color, renderer);
}
```

### Text Rendering

```cpp
// ✅ GOOD: Cache textures for static text
SDL_Texture* cachedTexture = fontMgr.renderTextToTexture("Static Text", "fonts_Arial", color, renderer);

// ✅ GOOD: Use appropriate font sizes for content
fontMgr.drawText("Title", "fonts_Title_Arial", x, y, color, renderer);      // Large for titles
fontMgr.drawText("Body text", "fonts_Arial", x, y, color, renderer);        // Medium for content
fontMgr.drawText("Tooltip", "fonts_Tooltip_Arial", x, y, color, renderer);  // Small for tooltips

// ✅ GOOD: Measure text before rendering for layout
int width, height;
fontMgr.measureText("Dynamic Text", "fonts_UI_Arial", &width, &height);
// Use width/height for positioning or UI layout
```

### Memory Management

```cpp
// ✅ GOOD: Unload unused fonts
void GameState::exit() {
    fontMgr.unloadFont("state_specific_font");
}

// ✅ GOOD: Check font availability
if (fontMgr.isFontLoaded("fonts_Arial")) {
    fontMgr.drawText("Text", "fonts_Arial", x, y, color, renderer);
} else {
    // Use fallback font or skip rendering
}
```

## Integration with Other Systems

The FontManager works seamlessly with:
- **[UIManager](../ui/UIManager_Guide.md)**: Automatic text measurement for component auto-sizing
- **[DPI-Aware Font System](../ui/DPI_Aware_Font_System.md)**: Automatic font scaling for all display types
- **[Auto-Sizing System](../ui/Auto_Sizing_System.md)**: Precise text measurement for content-aware sizing

The FontManager serves as the foundation for all text rendering in the engine, providing the measurement and rendering capabilities that enable the UI system's advanced features like auto-sizing and DPI awareness.