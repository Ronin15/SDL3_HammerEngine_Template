# FontManager Documentation

## Overview

The FontManager provides a centralized system for loading, managing, and rendering text in the Forge Game Engine. It handles TTF and OTF font formats with support for directory-based batch loading and efficient text rendering to textures or directly to the screen.

## Key Features

- **TTF/OTF Font Support**: Load TrueType and OpenType fonts
- **Directory Loading**: Batch load all fonts from a directory
- **Text Rendering**: Render text to textures or directly to screen
- **DPI-Aware Scaling**: Automatic font sizing based on display pixel density
- **High-Quality Rendering**: TTF hinting, kerning, and linear texture filtering
- **Pixel-Perfect Positioning**: Coordinate rounding for crisp text rendering
- **Memory Management**: Automatic font cleanup with RAII
- **Thread Safety**: Safe to use across multiple threads
- **Efficient Caching**: Fonts are cached for reuse

## Initialization

```cpp
// Initialize the font system with quality improvements
if (!FontManager::Instance().init()) {
    std::cerr << "Failed to initialize FontManager" << std::endl;
    return false;
}

// The FontManager automatically configures:
// - TTF hinting for better font outline quality
// - Kerning for proper character spacing
// - Linear texture scaling for smooth font rendering
// - Pixel-aligned positioning for crisp text
```

## Loading Fonts

### Display-Aware Font Loading

The FontManager provides intelligent font loading that adapts to display characteristics:

```cpp
// Automatic display-aware font loading
FontManager& fontMgr = FontManager::Instance();

// Load fonts with sizes calculated based on display properties
fontMgr.loadFontsForDisplay("res/fonts", windowWidth, windowHeight);

// This automatically creates:
// - fonts_Arial (base font): 22pt for general content
// - fonts_UI_Arial (UI font): 19pt for interface elements  
// - fonts_title_Arial (title font): 33pt for headings
// - fonts_tooltip_Arial (tooltip font): 11pt for compact tooltips

// Sizes are calculated based on:
// - Screen resolution (larger screens get slightly larger fonts)
// - Display characteristics (optimized for readability)
// - SDL3 logical presentation system handles DPI scaling automatically
```

### DPI-Aware Font Loading

### Single Font Loading

```cpp
// Load a single font file
bool success = FontManager::Instance().loadFont(
    "assets/fonts/Arial.ttf",  // Font file path
    "Arial",                   // Font ID for reference
    24                         // Font size in logical points (SDL3 handles DPI scaling)
);

// Font is automatically configured with:
// - TTF_HINTING_NORMAL for better outline quality
// - Kerning enabled for proper character spacing
// - Linear texture scaling for smooth rendering
```

### Directory Loading

```cpp
// Load all TTF/OTF fonts from a directory
bool success = FontManager::Instance().loadFont(
    "assets/fonts/",           // Directory path
    "ui_fonts",               // ID prefix for all fonts
    16                        // Font size for all fonts (logical points)
);

// Creates font IDs like: "ui_fonts_Arial", "ui_fonts_Times", etc.
// All fonts receive quality improvements automatically
```

## Text Measurement Utilities

### Single-Line Text Measurement

```cpp
// Measure text dimensions for layout calculations
FontManager& fontMgr = FontManager::Instance();
int width, height;

bool success = fontMgr.measureText(
    "Button Text",              // Text to measure
    "fonts_UI_Arial",          // Font ID
    &width, &height            // Output dimensions
);

// Use measurements for component sizing, layout, etc.
```

### Multi-Line Text Measurement

```cpp
// Automatic multi-line text measurement (detects newlines)
int width, height;
bool success = fontMgr.measureMultilineText(
    "Line 1\nLine 2\nLine 3",  // Multi-line text
    "fonts_UI_Arial",          // Font ID
    400,                       // Maximum width (0 = no limit)
    &width, &height            // Output dimensions
);

// Returns total dimensions for all lines combined
```

### Word Wrapping Text Measurement

```cpp
// Measure text with automatic word wrapping
int width, height;
bool success = fontMgr.measureTextWithWrapping(
    "This is a long line that will be wrapped to fit within the specified width",
    "fonts_UI_Arial",          // Font ID
    300,                       // Maximum width for wrapping
    &width, &height            // Output dimensions
);

// Returns dimensions of the wrapped text block
```

### Font Metrics

```cpp
// Get font metrics for spacing calculations
int lineHeight, ascent, descent;
bool success = fontMgr.getFontMetrics(
    "fonts_UI_Arial",          // Font ID
    &lineHeight,               // Line height
    &ascent,                   // Font ascent
    &descent                   // Font descent
);

// Use for precise layout calculations, list item heights, etc.
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

### Aligned Text Rendering

```cpp
// Render text with precise alignment control
FontManager::Instance().drawTextAligned(
    "Centered Text",          // Text to render
    "fonts_UI_Arial",        // Font ID
    400, 300,                // Position (x, y)
    {255, 255, 255, 255},    // White color
    renderer,                // SDL Renderer
    0                        // Alignment: 0=center, 1=left, 2=right, 3=top-left, etc.
);

// Alignment options provide precise text positioning control
// Text positioning is automatically rounded to pixel boundaries
// for crisp, blur-free rendering on all display types
```

### Word Wrapped Text Rendering

```cpp
// Render text with automatic word wrapping
FontManager::Instance().drawTextWithWrapping(
    "This is a long text that will be automatically wrapped to fit within the specified width",
    "fonts_UI_Arial",        // Font ID
    50, 100,                // Position (x, y)
    300,                    // Maximum width for wrapping
    {255, 255, 255, 255},   // White color
    renderer                // SDL Renderer
);

// Text automatically wraps at word boundaries and renders multiple lines
```

## Font Management

### Text Wrapping Utilities

```cpp
// Get wrapped lines for layout calculations
std::vector<std::string> wrappedLines = FontManager::Instance().wrapTextToLines(
    "Long text that needs wrapping",
    "fonts_UI_Arial",        // Font ID
    300                      // Maximum width
);

// Use wrapped lines for custom rendering or layout
for (const auto& line : wrappedLines) {
    // Process each wrapped line
}
```

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

### Game UI Text with Auto-Sizing Integration

```cpp
class GameUI {
private:
    SDL_Renderer* m_renderer;
    
public:
    void initializeFonts() {
        // Use display-aware font loading for optimal sizing
        FontManager::Instance().loadFontsForDisplay("assets/fonts/", 1920, 1080);
        
        // Or load specific font sizes
        FontManager::Instance().loadFont("assets/fonts/Impact.ttf", "title", 48);
    }
    
    void renderScore(int score) {
        std::string scoreText = "Score: " + std::to_string(score);
        FontManager::Instance().drawText(
            scoreText, "fonts_UI_Arial", 10, 10, 
            {255, 255, 255, 255}, m_renderer
        );
    }
    
    void renderTitle() {
        FontManager::Instance().drawTextAligned(
            "GAME TITLE", "fonts_title_Arial", 400, 100,
            {255, 215, 0, 255}, m_renderer, 0  // Centered alignment
        );
    }
    
    // Get text dimensions for layout calculations
    int calculateButtonWidth(const std::string& text) {
        int width, height;
        FontManager::Instance().measureText(text, "fonts_UI_Arial", &width, &height);
        return width + 20; // Add padding
    }
};
```

### Dialog System with Multi-Line Support and Word Wrapping

```cpp
class DialogSystem {
public:
    void renderDialog(const std::string& speaker, const std::string& text) {
        // Render speaker name
        FontManager::Instance().drawTextAligned(
            speaker, "fonts_title_Arial", 50, 450,
            {200, 200, 255, 255}, renderer, 1  // Left aligned
        );
        
        // Render dialog text with automatic word wrapping
        FontManager::Instance().drawTextWithWrapping(
            text, "fonts_UI_Arial", 50, 480, 500,  // Wraps at 500px width
            {255, 255, 255, 255}, renderer
        );
    }
    
    // Calculate dialog box height based on wrapped content
    int calculateDialogHeight(const std::string& text) {
        int width, height;
        FontManager::Instance().measureTextWithWrapping(
            text, "fonts_UI_Arial", 500, &width, &height
        );
        return height + 40; // Add padding
    }
    
    // Get individual wrapped lines for custom layout
    std::vector<std::string> getWrappedDialogLines(const std::string& text) {
        return FontManager::Instance().wrapTextToLines(text, "fonts_UI_Arial", 500);
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
    
    // Recommended: Use display-aware loading for optimal sizing
    auto& gameEngine = GameEngine::Instance();
    fontMgr.loadFontsForDisplay("assets/fonts/", 
                               gameEngine.getWindowWidth(), 
                               gameEngine.getWindowHeight());
    
    // Alternative: Load specific font sizes
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

## Auto-Sizing Integration

### UIManager Integration

The FontManager integrates seamlessly with UIManager's auto-sizing system:

```cpp
// UIManager automatically uses FontManager for text measurement
ui.createLabel("info", bounds, "Dynamic content");  // Auto-sizes using FontManager
ui.createButton("action", bounds, "Click Me");      // Measures text for sizing

// Multi-line labels are automatically detected and measured correctly
ui.createLabel("multi", bounds, "Line 1\nLine 2\nLine 3");

// Event logs automatically use word wrapping for long entries
ui.createEventLog("events", bounds, 10);
ui.addEventLogEntry("events", "This is a long event message that will automatically wrap");

// Font-based list item heights with proper padding
ui.createList("items", bounds);  // Uses FontManager metrics for item sizing
```

### Text Measurement Pipeline

1. **Content Analysis**: Automatic detection of single vs multi-line text
2. **Word Wrapping**: Intelligent text wrapping at word boundaries for long content
3. **Font Measurement**: Precise text dimension calculation using TTF_GetStringSize
4. **Multi-line Processing**: Line-by-line measurement for accurate height calculation
5. **Font Metrics**: Line height, ascent, and descent for spacing calculations

## Performance Considerations

- **Pre-load fonts**: Load all fonts at startup rather than during gameplay
- **Cache textures**: For frequently changing text, cache textures when possible
- **Batch directory loading**: Use directory loading for better startup performance
- **Display-Aware Sizing**: Use loadFontsForDisplay() for optimal font sizes
- **Text Measurement Caching**: Measurement results can be cached for repeated calculations
- **Quality vs Performance**: High-quality rendering uses TTF_RenderText_Blended for best results
- **Pixel Alignment**: Coordinate rounding ensures optimal GPU texture caching

## Display Quality Features

### DPI Scaling Integration

The FontManager integrates with GameEngine's DPI detection system:

```cpp
// Automatic DPI-aware font loading in GameEngine:
float dpiScale = GameEngine::Instance().getDPIScale();

// Font sizes are calculated as:
// actualSize = baseSize * dpiScale (rounded to even numbers)
// This ensures crisp rendering on high-DPI displays
```

### Quality Rendering Pipeline

1. **Font Loading**: TTF hinting, kerning, and style normalization
2. **Text Measurement**: Accurate dimension calculation for layout systems
3. **Texture Creation**: Linear filtering for smooth scaling
4. **Coordinate Rounding**: Pixel-perfect positioning for crisp edges
5. **Alpha Blending**: Proper blend modes for clear text rendering

### Multi-DPI Support

- **Standard DPI (96-120)**: Uses base font sizes for optimal performance
- **High DPI (150-300)**: Automatically scales fonts for clarity
- **4K/Retina Displays**: Maintains crisp text at high pixel densities
- **Dynamic Scaling**: UIManager applies additional scaling as needed

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