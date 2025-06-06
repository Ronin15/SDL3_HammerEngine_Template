# SDL3 Logical Presentation Modes Documentation

## Overview

SDL3's logical presentation system provides a powerful way to handle different screen resolutions and aspect ratios while maintaining consistent UI layout and coordinate systems. This document details how the Forge Game Engine's UI system handles all SDL3 logical presentation modes.

**Template Context**: This documentation is part of the SDL3 Game Engine Template. The UI stress testing system described here is included as a **validation and demonstration tool** for template users. When using this template for your own project, you may choose to:
- Keep the stress testing system for ongoing UI performance validation
- Remove it entirely after validating your UI implementation
- Adapt it for your specific performance requirements

The stress testing integration demonstrates real-world UI performance characteristics and validates the template's design decisions.
</overview>

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

**Visual Example:**
```
┌─────────────────────────────────┐
│                                 │
│      GAME CONTENT AREA          │
│      (may be distorted)         │
│                                 │
└─────────────────────────────────┘
```

### SDL_LOGICAL_PRESENTATION_OVERSCAN
**Aspect Ratio Preservation with Content Cropping**
- **Behavior**: Maintains logical aspect ratio, crops content that doesn't fit
- **Use Case**: When you prefer cropping over black bars and have expendable edge content
- **Coordinate Conversion**: Required - content may extend beyond visible area
- **UI Impact**: Edge UI elements may be cropped, center content always visible

**Visual Example:**
```
    ┌─────────────────────────────────┐
    │ CROPPED │ VISIBLE AREA │ CROPPED │
    │  AREA   │              │  AREA   │
    │         │              │         │
    └─────────────────────────────────┘
```

## Implementation in UIManager

### Coordinate Conversion System

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
    mouseY = static_cast<int>(logicalY);
}
```

### Universal Compatibility

The UI system is designed to work seamlessly with all presentation modes:

1. **Automatic Detection**: System detects active presentation mode at runtime
2. **Dynamic Conversion**: Mouse coordinates converted appropriately for each mode
3. **Consistent Behavior**: UI interactions work identically regardless of mode
4. **Performance Optimized**: Conversion only performed when necessary

## Best Practices

### Choosing the Right Mode

| Scenario | Recommended Mode | Reason |
|----------|-----------------|---------|
| Pixel-perfect games | DISABLED | Direct control over every pixel |
| Cross-platform games | LETTERBOX | Consistent experience across devices |
| Full-screen utilities | STRETCH | Maximum screen usage |
| Mobile games | OVERSCAN | Better use of varied mobile screens |

### UI Design Considerations

#### For LETTERBOX Mode
- Design UI for target logical resolution
- Ensure important elements fit within safe area
- Test with various aspect ratios

#### For STRETCH Mode
- Design flexible UI that handles distortion gracefully
- Use relative positioning where possible
- Test with extreme aspect ratios (ultrawide, mobile)

#### For OVERSCAN Mode
- Keep critical UI elements in center area
- Design expendable content for edges
- Provide visual indicators for cropped areas

#### For DISABLED Mode
- Handle resolution changes manually
- Implement custom scaling logic if needed
- Direct pixel manipulation available

## Performance Considerations

### Coordinate Conversion Overhead
- **DISABLED**: Zero overhead - no conversion
- **LETTERBOX/STRETCH/OVERSCAN**: Minimal overhead - single function call per mouse event
- **Optimization**: Conversion only performed for interactive components

### Memory Usage
- All modes have identical memory footprint
- No additional storage required for coordinate conversion
- Component bounds stored in logical coordinates

### Rendering Performance
- All modes use same rendering pipeline
- SDL3 handles scaling at GPU level
- No performance difference between modes

## Testing Scenarios

### Resolution Testing
Test UI behavior across common resolutions:
- 1920x1080 (16:9)
- 1280x720 (16:9)
- 1024x768 (4:3)
- 3440x1440 (21:9 ultrawide)
- 812x375 (mobile)

### Aspect Ratio Testing
Verify UI behavior with different aspect ratios:
- Standard (16:9, 16:10)
- Legacy (4:3, 5:4) 
- Ultrawide (21:9, 32:9)
- Mobile (various ratios)

### Dynamic Mode Switching
Test runtime mode changes:
- Window resizing
- Fullscreen transitions
- Mode switching during gameplay

## Common Issues and Solutions

### Issue: Clicks Not Registering
**Cause**: Coordinate conversion not applied
**Solution**: Ensure `SDL_RenderCoordinatesFromWindow()` is called

### Issue: UI Elements Appear Distorted
**Cause**: Using STRETCH mode with non-matching aspect ratio
**Solution**: Switch to LETTERBOX or design flexible UI

### Issue: UI Elements Cropped
**Cause**: Using OVERSCAN mode with edge-positioned elements
**Solution**: Move critical UI to center or switch to LETTERBOX

### Issue: Black Bars Unwanted
**Cause**: Using LETTERBOX mode
**Solution**: Switch to STRETCH or OVERSCAN based on preference

## Code Examples

### Setting Up Logical Presentation
```cpp
// Set logical size and presentation mode
SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, 
                                SDL_LOGICAL_PRESENTATION_LETTERBOX);
```

### Checking Current Mode
```cpp
int logicalW, logicalH;
SDL_RendererLogicalPresentation presentation;
SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);

switch(presentation) {
    case SDL_LOGICAL_PRESENTATION_DISABLED:
        // Handle direct mode
        break;
    case SDL_LOGICAL_PRESENTATION_LETTERBOX:
        // Handle letterbox mode
        break;
    case SDL_LOGICAL_PRESENTATION_STRETCH:
        // Handle stretch mode
        break;
    case SDL_LOGICAL_PRESENTATION_OVERSCAN:
        // Handle overscan mode
        break;
}
```

### Manual Coordinate Conversion
```cpp
// Convert specific coordinates (if needed outside UIManager)
float logicalX, logicalY;
SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY, &logicalX, &logicalY);
```

## Integration with Game Engine

The Forge Game Engine's UIManager handles all presentation modes transparently:

1. **Automatic Setup**: Game engine initializes with chosen presentation mode
2. **Transparent Operation**: UI code doesn't need mode-specific logic
3. **Runtime Flexibility**: Can change modes without UI code changes
4. **Debug Support**: Debug overlays work in all modes

## Template Usage Guidelines

### For Template Evaluation
- Use the integrated UI stress tests to validate performance characteristics
- Test all presentation modes with your target resolutions
- Verify UI behavior meets your project requirements
- Benchmark performance on your target hardware

### For Production Projects
- **Keep Testing System**: Maintain ongoing UI performance validation
- **Remove Testing System**: Clean removal after initial validation
- **Adapt Testing System**: Modify for project-specific requirements

### Recommended Workflow
1. **Evaluation Phase**: Run comprehensive stress tests to understand capabilities
2. **Development Phase**: Use lightweight tests during UI development
3. **Production Decision**: Choose whether to maintain testing infrastructure

## Conclusion

SDL3's logical presentation system provides flexible scaling options while the UIManager ensures consistent behavior across all modes. By understanding each mode's characteristics and trade-offs, developers can choose the best option for their specific use case while maintaining a seamless user experience.

This template provides both the production-ready UI system and the tools to validate its performance, giving developers confidence in their UI implementation choices.