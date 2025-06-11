# SDL3 Logical Presentation Modes Documentation

## Overview

SDL3's logical presentation system provides a powerful way to handle different screen resolutions and aspect ratios while maintaining consistent UI layout and coordinate systems. This document details how the UIManager integrates with all SDL3 logical presentation modes for production games.

## Logical Presentation Modes

### SDL_LOGICAL_PRESENTATION_DISABLED
**Default Mode - No Scaling**
- **Behavior**: Direct 1:1 mapping between logical and window coordinates
- **Use Case**: When you want direct pixel control or handling scaling manually
- **Coordinate Conversion**: None needed - window coordinates = logical coordinates
- **UI Impact**: UI elements render at exact pixel positions specified

```cpp
// No coordinate conversion needed
mouseX = static_cast<int>(mousePos.getX());
mouseY = static_cast<int>(mousePos.getY());
```

### SDL_LOGICAL_PRESENTATION_LETTERBOX
**Aspect Ratio Preservation with Black Bars**
- **Behavior**: Maintains logical aspect ratio, adds black bars (pillarbox/letterbox) to fill remaining space
- **Use Case**: Ideal for games requiring consistent aspect ratios across devices
- **Coordinate Conversion**: Required - uses `SDL_RenderCoordinatesFromWindow()`
- **UI Impact**: UI maintains proportions, click detection requires coordinate conversion

**Visual Example:**
```
┌─────────────────────────────────┐
│         BLACK BAR               │
├─────────────────────────────────┤
│                                 │
│      GAME CONTENT AREA          │
│      (maintains aspect)         │
│                                 │
├─────────────────────────────────┤
│         BLACK BAR               │
└─────────────────────────────────┘
```

### SDL_LOGICAL_PRESENTATION_STRETCH
**Full Window Fill with Potential Distortion**
- **Behavior**: Stretches logical coordinates to fill entire window
- **Use Case**: When you want to use full screen real estate and can handle distortion
- **Coordinate Conversion**: Required - non-uniform scaling possible
- **UI Impact**: UI elements may appear stretched on different aspect ratios

### SDL_LOGICAL_PRESENTATION_OVERSCAN
**Aspect Ratio Preservation with Content Cropping**
- **Behavior**: Maintains logical aspect ratio, crops content that doesn't fit
- **Use Case**: When you prefer cropping over black bars and have expendable edge content
- **Coordinate Conversion**: Required - content may extend beyond visible area
- **UI Impact**: Edge UI elements may be cropped, center content always visible

## Implementation in UIManager

### Automatic Coordinate Conversion

The UIManager automatically handles coordinate conversion for all presentation modes:

```cpp
// Check if logical presentation is active
int logicalW, logicalH;
SDL_RendererLogicalPresentation presentation;
if (SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation) && 
    presentation != SDL_LOGICAL_PRESENTATION_DISABLED) {
    
    // Convert coordinates for any logical presentation mode
    float logicalX, logicalY;
    SDL_RenderCoordinatesFromWindow(renderer, mousePos.getX(), mousePos.getY(), 
                                   &logicalX, &logicalY);
    mouseX = static_cast<int>(logicalX);
    mouseY = static_cast<int>(logicalY);
} else {
    // No logical presentation - use window coordinates directly
    mouseX = static_cast<int>(mousePos.getX());
    mouseY = static_cast<int>(mousePos.getY());
}
```

### Universal Compatibility

The UI system is designed to work seamlessly with all presentation modes:

1. **Automatic Detection**: System detects active presentation mode at runtime
2. **Dynamic Conversion**: Mouse coordinates converted appropriately for each mode
3. **Consistent Behavior**: UI interactions work identically regardless of mode
4. **Performance Optimized**: Conversion only performed when necessary

## Choosing the Right Mode

### Recommended Usage by Game Type

| Game Type | Recommended Mode | Reason |
|-----------|------------------|---------|
| Pixel Art Games | DISABLED | Direct pixel control for crisp graphics |
| Cross-Platform Games | LETTERBOX | Consistent experience across all devices |
| Productivity Apps | STRETCH | Maximum screen usage for interfaces |
| Mobile Ports | OVERSCAN | Better adaptation to varied mobile screens |
| Strategy Games | LETTERBOX | Consistent UI layout across platforms |

### UI Design Considerations

#### For LETTERBOX Mode
- Design UI for target logical resolution (e.g., 1920x1080)
- Ensure important elements fit within safe area
- Test with various aspect ratios (16:9, 16:10, 4:3)
- UI auto-sizing works perfectly within logical bounds

#### For STRETCH Mode
- Design flexible UI that handles distortion gracefully
- Use relative positioning where possible
- Test with extreme aspect ratios (ultrawide, mobile)
- Auto-sizing adapts to stretched dimensions

#### For OVERSCAN Mode
- Keep critical UI elements in center area (safe zone)
- Design expendable content for edges that may be cropped
- Provide visual indicators when content extends beyond visible area
- Auto-sizing works but edge content may be hidden

#### For DISABLED Mode
- Handle resolution changes manually in your game logic
- Implement custom scaling logic if needed
- Direct pixel manipulation available
- Full control over rendering pipeline

## Performance Considerations

### Coordinate Conversion Overhead
- **DISABLED**: Zero overhead - no conversion needed
- **LETTERBOX/STRETCH/OVERSCAN**: Minimal overhead - single function call per input event
- **Optimization**: Conversion only performed for UI interactions, not every frame

### Memory and Rendering
- All modes have identical memory footprint
- No additional storage required for coordinate conversion
- Component bounds stored in logical coordinates
- SDL3 handles scaling at GPU level for optimal performance

## Integration Examples

### Setting Up Logical Presentation
```cpp
// Initialize with target logical resolution
SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, 
                                SDL_LOGICAL_PRESENTATION_LETTERBOX);

// UIManager automatically adapts to this configuration
auto& ui = UIManager::Instance();
ui.createButton("my_button", {100, 100, 0, 0}, "Click Me");  // Works in any mode
```

### Checking Current Mode
```cpp
int logicalW, logicalH;
SDL_RendererLogicalPresentation presentation;
SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);

switch(presentation) {
    case SDL_LOGICAL_PRESENTATION_DISABLED:
        // Handle direct pixel mode
        break;
    case SDL_LOGICAL_PRESENTATION_LETTERBOX:
        // Handle letterbox mode - UI maintains proportions
        break;
    case SDL_LOGICAL_PRESENTATION_STRETCH:
        // Handle stretch mode - UI may appear distorted
        break;
    case SDL_LOGICAL_PRESENTATION_OVERSCAN:
        // Handle overscan mode - edge UI may be cropped
        break;
}
```

### Runtime Mode Switching
```cpp
void GameSettings::setPresentationMode(SDL_RendererLogicalPresentation mode) {
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, mode);
    
    // UIManager automatically adapts - no additional code needed
    // All existing UI components continue to work correctly
}
```

## Common Issues and Solutions

### Issue: Clicks Not Registering
**Cause**: Coordinate conversion not applied in logical presentation modes
**Solution**: UIManager handles this automatically - ensure you're using UIManager for input

### Issue: UI Elements Appear Distorted
**Cause**: Using STRETCH mode with non-matching aspect ratio
**Solution**: Switch to LETTERBOX or design UI to handle stretching gracefully

### Issue: UI Elements Cut Off
**Cause**: Using OVERSCAN mode with edge-positioned elements
**Solution**: Move critical UI to center area or switch to LETTERBOX mode

### Issue: Unwanted Black Bars
**Cause**: Using LETTERBOX mode when you want full screen usage
**Solution**: Switch to STRETCH or OVERSCAN based on your distortion/cropping preference

### Issue: Inconsistent UI Sizing
**Cause**: Not setting logical presentation consistently
**Solution**: Set logical presentation early in initialization before creating UI

## Best Practices

### Development Workflow
1. **Choose your target logical resolution** (commonly 1920x1080)
2. **Select appropriate presentation mode** based on game requirements
3. **Design UI using logical coordinates** - UIManager handles the rest
4. **Test on multiple aspect ratios** to verify behavior
5. **Use auto-sizing features** to adapt to different scaling scenarios

### Testing Strategy
```cpp
void testPresentationModes() {
    std::vector<SDL_RendererLogicalPresentation> modes = {
        SDL_LOGICAL_PRESENTATION_DISABLED,
        SDL_LOGICAL_PRESENTATION_LETTERBOX,
        SDL_LOGICAL_PRESENTATION_STRETCH,
        SDL_LOGICAL_PRESENTATION_OVERSCAN
    };
    
    for (auto mode : modes) {
        SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, mode);
        
        // Test UI interactions in each mode
        testUIInteractions();
    }
}
```

### Resolution Testing
Test your UI across common resolutions:
- **1920x1080** (16:9) - Most common desktop
- **1280x720** (16:9) - Lower resolution displays  
- **1024x768** (4:3) - Legacy displays
- **3440x1440** (21:9) - Ultrawide monitors
- **Various mobile** - Different aspect ratios

## Advanced Features

### Dynamic Safe Area Calculation
```cpp
SDL_FRect getSafeArea() {
    int logicalW, logicalH;
    SDL_RendererLogicalPresentation presentation;
    SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);
    
    if (presentation == SDL_LOGICAL_PRESENTATION_OVERSCAN) {
        // Calculate safe area for overscan mode
        float margin = 0.1f;  // 10% margin
        return {
            logicalW * margin,
            logicalH * margin,
            logicalW * (1.0f - 2 * margin),
            logicalH * (1.0f - 2 * margin)
        };
    }
    
    return {0, 0, static_cast<float>(logicalW), static_cast<float>(logicalH)};
}
```

### Aspect Ratio Detection
```cpp
float getDisplayAspectRatio() {
    int windowW, windowH;
    SDL_GetWindowSize(window, &windowW, &windowH);
    return static_cast<float>(windowW) / static_cast<float>(windowH);
}

bool isUltrawide() {
    return getDisplayAspectRatio() > 2.0f;  // 21:9 or wider
}
```

## Integration with Auto-Sizing

The UIManager's auto-sizing system works seamlessly with all presentation modes:

```cpp
// Auto-sizing works in any presentation mode
ui.createLabel("status", {x, y, 0, 0}, "Dynamic Text");  // Sizes to fit content

// Title centering adapts to logical presentation bounds
ui.createTitle("header", {0, y, logicalWidth, 0}, "Game Title");
ui.setTitleAlignment("header", UIAlignment::CENTER_CENTER);  // Centers within logical area

// Multi-line text auto-sizing works correctly in all modes
ui.createLabel("info", {x, y, 0, 0}, "Line 1\nLine 2\nLine 3");
```

The auto-sizing system uses logical coordinates, so components maintain proper proportions regardless of the presentation mode in use.

## Conclusion

SDL3's logical presentation system provides flexible scaling options while the UIManager ensures consistent behavior across all modes. By understanding each mode's characteristics and trade-offs, developers can choose the best option for their specific game while maintaining excellent user experience across all target platforms and display configurations.

The UIManager's automatic coordinate conversion and auto-sizing features eliminate the complexity of handling different presentation modes manually, allowing developers to focus on game content rather than display compatibility issues.