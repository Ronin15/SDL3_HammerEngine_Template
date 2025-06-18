# DPI-Aware Font System Documentation

## Overview

The Hammer Game Engine implements a comprehensive DPI-aware font rendering system that automatically detects display pixel density and scales fonts accordingly, ensuring crisp, professional text rendering across all display types - from standard monitors to 4K/Retina displays.

## System Architecture

### Components
1. **GameEngine**: DPI detection and centralized scale management
2. **FontManager**: High-quality font loading and rendering with automatic sizing
3. **UIManager**: DPI-aware UI scaling and text positioning

### Process Flow
```
Display Detection → DPI Calculation → Font Sizing → Quality Rendering → UI Integration
     (GameEngine)     (GameEngine)    (FontManager)   (FontManager)    (UIManager)
```

## How It Works

### 1. Automatic DPI Detection

During initialization, GameEngine automatically detects display characteristics:

```cpp
// Automatic DPI detection in GameEngine::init()
int pixelWidth, pixelHeight;
int logicalWidth, logicalHeight;
SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight);
SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight);

float scaleX = static_cast<float>(pixelWidth) / static_cast<float>(logicalWidth);
float scaleY = static_cast<float>(pixelHeight) / static_cast<float>(logicalHeight);
float dpiScale = std::max(scaleX, scaleY);
```

### 2. Smart Font Size Calculation

Base font sizes are automatically scaled and pixel-aligned for crisp rendering:

```cpp
// DPI-aware font sizing with pixel alignment
int baseFontSize = static_cast<int>(std::round(24.0f * dpiScale / 2.0f) * 2.0f);
int uiFontSize = static_cast<int>(std::round(18.0f * dpiScale / 2.0f) * 2.0f);

// Ensure minimum readable sizes
baseFontSize = std::max(baseFontSize, 12);
uiFontSize = std::max(uiFontSize, 10);
```

**Key Features:**
- **Pixel Alignment**: Sizes rounded to even numbers for crisp rendering
- **Minimum Constraints**: Ensures text remains readable on all displays
- **Proportional Scaling**: Maintains relative size relationships across display types

### 3. Quality Font Loading

FontManager loads fonts with professional quality settings:

```cpp
// Automatic quality configuration
TTF_SetFontHinting(font.get(), TTF_HINTING_NORMAL);  // Better outline quality
TTF_SetFontKerning(font.get(), 1);                   // Proper character spacing
TTF_SetFontStyle(font.get(), TTF_STYLE_NORMAL);      // Consistent rendering
```

### 4. High-Quality Text Rendering

```cpp
// Blended rendering for high-quality anti-aliasing
auto surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);

// Linear texture scaling for smooth rendering
SDL_SetTextureScaleMode(texture.get(), SDL_SCALEMODE_LINEAR);

// Pixel-perfect positioning
SDL_FRect dstRect = {
    std::roundf(x - width/2.0f),   // Rounded to pixel boundaries
    std::roundf(y - height/2.0f),
    static_cast<float>(width),
    static_cast<float>(height)
};
```

## Display Type Support

### Standard DPI (96-120 DPI)
- **Scale Factor**: 1.0x
- **Base Font**: 24px → 24px
- **UI Font**: 18px → 18px
- **Optimization**: Performance-focused with crisp rendering

### High DPI (150-200 DPI)
- **Scale Factor**: 1.5-2.0x
- **Base Font**: 24px → 36-48px
- **UI Font**: 18px → 27-36px
- **Optimization**: Balanced quality and performance

### 4K/Retina (250+ DPI)
- **Scale Factor**: 2.0-3.0x+
- **Base Font**: 24px → 48-72px+
- **UI Font**: 18px → 36-54px+
- **Optimization**: Maximum quality for premium displays

## Integration with UI System

### Automatic Operation

The DPI system works seamlessly with the UI system without requiring code changes:

```cpp
// All UIManager components automatically use DPI-scaled fonts
auto& ui = UIManager::Instance();

// Button text automatically crisp on all displays
ui.createButton("play_btn", {300, 200, 0, 0}, "Play Game");  // Auto-sizes with DPI scaling

// Labels use DPI-scaled fonts and positioning
ui.createLabel("score", {20, 20, 0, 0}, "Score: 0");  // Text size scales with display
```

### FontManager Integration

```cpp
// FontManager automatically uses DPI-scaled fonts
FontManager::Instance().drawText(
    "Hello World",
    "fonts_Arial",           // Automatically DPI-scaled font
    400, 300,               // Position adjusted by UIManager
    {255, 255, 255, 255},
    renderer
);
```

### Auto-Sizing System Compatibility

The DPI system integrates seamlessly with the auto-sizing system:

```cpp
// Auto-sizing works correctly across all DPI levels
ui.createLabel("dynamic", {x, y, 0, 0}, "Dynamic Content");
// - Text measured using DPI-appropriate font
// - Component sized using accurate measurements
// - Rendering crisp on all display types
```

## SDL3 Configuration

### Window Setup

```cpp
// SDL3 window configuration for DPI awareness
int flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;  // Enable high-DPI support
mp_window.reset(SDL_CreateWindow(title, width, height, flags));

// SDL3 rendering hints for quality
SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");  // Geometry-based line rendering
```

### Quality Settings

The system automatically configures optimal rendering settings:
- **Blended Alpha**: High-quality anti-aliasing
- **Linear Filtering**: Smooth texture scaling
- **Pixel Alignment**: Eliminates sub-pixel blurriness
- **Proper Blend Modes**: Preserves alpha channels for crisp edges

## Performance Considerations

### Optimizations
- **One-Time Calculation**: DPI detected once at startup
- **Pre-Calculated Sizes**: Font sizes determined during initialization
- **Smart Pointer Management**: Efficient memory handling
- **Thread-Safe Design**: Concurrent access support

### Multi-Threading Compatibility

```cpp
// DPI calculation before thread spawning
float dpiScale = calculateDPIScale();

// Thread-safe font loading with DPI-scaled sizes
FontManager& fontMgr = FontManager::Instance();
fontMgr.loadFont("res/fonts", "fonts_Arial", scaledFontSize);
```

## Usage Examples

### Game State Integration

```cpp
class GamePlayState : public GameState {
public:
    void render() override {
        auto& fontManager = FontManager::Instance();
        auto& gameEngine = GameEngine::Instance();

        // Text automatically scaled for current display
        fontManager.drawText(
            "Score: 1000",
            "fonts_Arial",                    // DPI-appropriate font
            gameEngine.getLogicalWidth() / 2, // Position scaled by UIManager
            50,
            {255, 255, 255, 255},
            gameEngine.getRenderer()
        );
    }
};
```

### Dynamic Text with DPI Scaling

```cpp
void updatePlayerStats() {
    auto& ui = UIManager::Instance();

    // Text automatically uses appropriate font size for display
    std::string healthText = "Health: " + std::to_string(player.getHealth());
    ui.setText("hud_health", healthText);  // DPI-scaled font used automatically

    // Auto-sizing works correctly with DPI scaling
    ui.calculateOptimalSize("hud_health");  // Measures with DPI-appropriate font
}
```

## Best Practices

### Font Selection
```cpp
// Use consistent base sizes that scale well
const int BASE_FONT_SIZE = 24;  // Scales to 24/36/48+ based on DPI
const int UI_FONT_SIZE = 18;    // Scales to 18/27/36+ based on DPI
const int SMALL_FONT_SIZE = 14; // For captions and details
```

### Component Design
```cpp
// Design components to work across scale factors
ui.createButton("action", {x, y, 0, 0}, "Click Me");  // Auto-sizes with DPI scaling
ui.createTitle("header", {0, y, windowWidth, 0}, "Game Title");  // Centers with DPI scaling
```

### Testing Across Displays
- Test on standard DPI monitors (96-120 DPI)
- Test on high DPI displays (150-200 DPI)
- Test on 4K/Retina displays (250+ DPI)
- Verify text remains crisp and readable at all scales
- Check that UI elements scale proportionally

## Troubleshooting

### Common Issues

**Blurry Text:**
- Ensure SDL3 window uses `SDL_WINDOW_HIGH_PIXEL_DENSITY` flag
- Verify DPI detection is working correctly
- Check that coordinate rounding is enabled in rendering

**Incorrect Scaling:**
- Verify both pixel and logical window sizes are detected correctly
- Ensure DPI scale is calculated before font loading
- Check that FontManager is using DPI-scaled font sizes

**Performance Issues:**
- Pre-load fonts at startup, not during rendering
- Use cached textures for frequently changing text
- Verify DPI calculation only happens once at initialization

### Debug Information

```cpp
// Check DPI detection results
float dpiScale = GameEngine::Instance().getDPIScale();
int windowWidth = GameEngine::Instance().getLogicalWidth();
int windowHeight = GameEngine::Instance().getLogicalHeight();

std::cout << "DPI Scale: " << dpiScale << std::endl;
std::cout << "Logical Size: " << windowWidth << "x" << windowHeight << std::endl;

// Verify font loading
bool fontLoaded = FontManager::Instance().isFontLoaded("fonts_Arial");
std::cout << "Font loaded: " << (fontLoaded ? "Yes" : "No") << std::endl;
```

## API Reference

### GameEngine Methods
```cpp
float getDPIScale() const;           // Get calculated DPI scale factor
SDL_Window* getWindow() const;       // Access window for DPI calculations
SDL_Renderer* getRenderer() const;   // Access renderer for text rendering
```

### FontManager Integration
```cpp
// All existing methods automatically use DPI-scaled fonts
bool loadFont(const std::string& fontFile, const std::string& fontID, int fontSize);
void drawText(const std::string& text, const std::string& fontID, int x, int y,
              SDL_Color color, SDL_Renderer* renderer);
bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);
bool measureMultilineText(const std::string& text, const std::string& fontID,
                         int maxWidth, int* width, int* height);
```

### UIManager Integration
```cpp
float getGlobalScale() const;        // Get current UI scale factor
void setGlobalScale(float scale);    // Manually adjust UI scaling (rarely needed)
```

## Integration with Other Systems

This DPI system works seamlessly with:
- **[Auto-Sizing System](Auto_Sizing_System.md)**: Content measurement uses DPI-scaled fonts
- **[UIManager](UIManager_Guide.md)**: All UI components automatically use DPI scaling
- **[SDL3 Logical Presentation](SDL3_Logical_Presentation_Modes.md)**: Compatible with all presentation modes

The DPI-aware font system ensures that text remains crisp and readable across all display types while maintaining consistent visual proportions and professional appearance.
