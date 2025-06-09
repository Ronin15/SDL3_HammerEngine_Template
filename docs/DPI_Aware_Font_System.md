# DPI-Aware Font System Documentation

## Overview

The Forge Game Engine implements a comprehensive DPI-aware font rendering system that automatically detects display pixel density and scales fonts accordingly, ensuring crisp, professional text rendering across all display types - from standard monitors to 4K/Retina displays.

## System Architecture

### Components

1. **GameEngine**: DPI detection and centralized scale management
2. **FontManager**: High-quality font loading and rendering
3. **UIManager**: DPI-aware UI scaling and positioning

### Data Flow

```
Display Detection → DPI Calculation → Font Sizing → Quality Rendering → UI Scaling
     (GameEngine)     (GameEngine)    (FontManager)   (FontManager)    (UIManager)
```

## How It Works

### 1. DPI Detection (GameEngine)

During initialization, the GameEngine automatically detects display characteristics:

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

### 2. Font Size Calculation

Base font sizes are automatically scaled and pixel-aligned:

```cpp
// DPI-aware font sizing
int baseFontSize = static_cast<int>(std::round(24.0f * dpiScale / 2.0f) * 2.0f);
int uiFontSize = static_cast<int>(std::round(18.0f * dpiScale / 2.0f) * 2.0f);

// Ensure minimum readable sizes
baseFontSize = std::max(baseFontSize, 12);
uiFontSize = std::max(uiFontSize, 10);
```

**Key Features:**
- **Pixel Alignment**: Sizes rounded to even numbers for crisp rendering
- **Minimum Constraints**: Ensures text remains readable on all displays
- **Proportional Scaling**: Maintains relative size relationships

### 3. Quality Font Loading (FontManager)

Fonts are loaded with professional quality settings:

```cpp
// Automatic quality configuration for each font
TTF_SetFontHinting(font.get(), TTF_HINTING_NORMAL);  // Better outline quality
TTF_SetFontKerning(font.get(), 1);                   // Proper character spacing
TTF_SetFontStyle(font.get(), TTF_STYLE_NORMAL);      // Consistent rendering
```

### 4. High-Quality Rendering

Text rendering uses optimal settings for clarity:

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

### 5. UI Scale Integration (UIManager)

The UIManager applies DPI scaling to all text positioning:

```cpp
// Automatic scale application in UIManager
float globalScale = GameEngine::Instance().getDPIScale();

// All text coordinates are scaled
int scaledX = static_cast<int>(x * globalScale);
int scaledY = static_cast<int>(y * globalScale);
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

## Integration Examples

### Basic Usage

```cpp
// No code changes needed! System works automatically
FontManager::Instance().drawText(
    "Hello World",
    "fonts_Arial",           // Automatically DPI-scaled font
    400, 300,               // Position (will be DPI-adjusted by UIManager)
    {255, 255, 255, 255},
    renderer
);
```

### Game State Integration

```cpp
class GamePlayState : public GameState {
public:
    void render() override {
        auto& fontManager = FontManager::Instance();
        auto& gameEngine = GameEngine::Instance();
        
        // Text automatically scaled for current display
        SDL_Color textColor = {200, 200, 200, 255};
        fontManager.drawText(
            "Score: 1000",
            "fonts_Arial",                    // DPI-appropriate font
            gameEngine.getWindowWidth() / 2, // Position scaled by UIManager
            50,
            textColor,
            gameEngine.getRenderer()
        );
    }
};
```

### UI Component Integration

```cpp
// UIManager automatically applies DPI scaling
auto& ui = UIManager::Instance();

// Button text automatically crisp on all displays
ui.createButton("play_btn", {300, 200, 200, 50}, "Play Game");

// Labels use DPI-scaled fonts and positioning
ui.createLabel("score", {20, 20, 200, 30}, "Score: 0");
```

## Quality Features

### Font Loading Optimizations

1. **TTF Hinting**: Improves font outline quality at all sizes
2. **Kerning**: Ensures proper character spacing
3. **Style Normalization**: Consistent font rendering properties
4. **Linear Filtering**: Smooth texture scaling for better quality

### Rendering Optimizations

1. **Blended Alpha**: High-quality anti-aliasing
2. **Pixel Alignment**: Eliminates sub-pixel blurriness
3. **Proper Blend Modes**: Preserves alpha channels for crisp edges
4. **Coordinate Rounding**: GPU-friendly texture positioning

### Performance Considerations

1. **One-Time Calculation**: DPI detected once at startup
2. **Cached Font Sizes**: Pre-calculated optimal sizes
3. **Smart Pointer Management**: Efficient memory handling
4. **Thread-Safe Design**: Concurrent access support

## Technical Implementation

### SDL3 Integration

```cpp
// SDL3 window configuration for DPI awareness
int flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;  // Enable high-DPI support
mp_window.reset(SDL_CreateWindow(title, width, height, flags));

// SDL3 rendering hints for quality
SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");  // Geometry-based line rendering
```

### Multi-Threading Compatibility

```cpp
// DPI calculation before thread spawning
float dpiScale = calculateDPIScale();
m_dpiScale = dpiScale;  // Store for other managers

// Thread-safe font loading with DPI-scaled sizes
initTasks.push_back(
    Forge::ThreadSystem::Instance().enqueueTaskWithResult([baseFontSize, uiFontSize]() -> bool {
        FontManager& fontMgr = FontManager::Instance();
        fontMgr.loadFont("res/fonts", "fonts", baseFontSize);
        fontMgr.loadFont("res/fonts", "fonts_UI", uiFontSize);
        return true;
    })
);
```

## Best Practices

### Font Selection

```cpp
// Use DPI-appropriate base sizes
const int BASE_FONT_SIZE = 24;  // Will scale to 24/36/48+ based on DPI
const int UI_FONT_SIZE = 18;    // Will scale to 18/27/36+ based on DPI
const int SMALL_FONT_SIZE = 14; // For captions and fine print
```

### Component Design

```cpp
// Design UI components to work across scale factors
class ScalableButton {
    UIRect bounds;  // Will be DPI-scaled automatically
    std::string fontID = "fonts_UI_Arial";  // DPI-appropriate font
    
    void render() {
        // UIManager handles all DPI scaling automatically
        ui.renderButton(this, renderer);
    }
};
```

### Testing Across Displays

```cpp
// Test with different DPI settings
void testDPIScaling() {
    float currentScale = GameEngine::Instance().getDPIScale();
    std::cout << "Current DPI scale: " << currentScale << std::endl;
    
    // Verify text remains crisp and readable
    // Check that UI elements scale proportionally
    // Ensure no clipping or overflow issues
}
```

## Troubleshooting

### Common Issues

**Blurry Text:**
- Ensure SDL3 hints are properly set
- Verify DPI detection is working correctly
- Check that coordinate rounding is enabled

**Incorrect Scaling:**
- Verify window creation uses HIGH_PIXEL_DENSITY flag
- Check that both pixel and logical window sizes are correct
- Ensure DPI scale is calculated before font loading

**Performance Issues:**
- Pre-load fonts at startup, not during rendering
- Use cached textures for frequently changing text
- Verify DPI calculation only happens once

### Debug Information

```cpp
// Check DPI detection results
float dpiScale = GameEngine::Instance().getDPIScale();
int windowWidth = GameEngine::Instance().getWindowWidth();
int windowHeight = GameEngine::Instance().getWindowHeight();

std::cout << "DPI Scale: " << dpiScale << std::endl;
std::cout << "Window Size: " << windowWidth << "x" << windowHeight << std::endl;

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

### FontManager Enhancements

```cpp
// All existing methods automatically use DPI-scaled fonts
bool loadFont(const std::string& fontFile, const std::string& fontID, int fontSize);
void drawText(const std::string& text, const std::string& fontID, int x, int y, 
              SDL_Color color, SDL_Renderer* renderer);
void drawTextAligned(const std::string& text, const std::string& fontID,
                     int x, int y, SDL_Color color, SDL_Renderer* renderer, int alignment);
```

### UIManager Integration

```cpp
float getGlobalScale() const;        // Get current UI scale factor
void setGlobalScale(float scale);    // Manually adjust UI scaling
```

## See Also

- `docs/FontManager.md` - Complete FontManager documentation
- `docs/ui/UIManager_Guide.md` - UI system integration
- `include/core/GameEngine.hpp` - Core DPI detection implementation
- `include/managers/FontManager.hpp` - Font rendering API
- `include/managers/UIManager.hpp` - UI scaling system