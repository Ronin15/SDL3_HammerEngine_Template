# Auto-Sizing System Documentation

## Overview

The Auto-Sizing System is a core feature of the UIManager that automatically calculates optimal component dimensions based on content. This system eliminates the need for manual sizing calculations and provides consistent, content-aware layouts across all UI components.

## Core Features

### Content-Aware Sizing
- **Text Measurement**: Precise calculation of text dimensions using FontManager
- **Multi-line Detection**: Automatic detection and sizing for text containing newlines
- **Font-Based Calculations**: All measurements based on actual font metrics rather than estimates
- **Padding Integration**: Automatic content padding for proper spacing

### Smart Centering
- **Title Auto-Centering**: Titles with CENTER alignment automatically reposition to stay centered on screen
- **Alignment Preservation**: Component alignment is maintained after auto-sizing
- **Position Stability**: Auto-sizing only affects dimensions, never base positioning

### Display Integration
- **SDL3 Compatibility**: Works seamlessly with SDL3's logical presentation system
- **DPI Awareness**: Integrates with GameEngine's DPI detection for optimal sizing
- **Coordinate Conversion**: Proper mouse coordinate handling with SDL3's coordinate transformation

## Architecture

### Component Structure

Each UIComponent includes auto-sizing properties:

```cpp
struct UIComponent {
    // Auto-sizing properties
    bool autoSize{true};                // Enable content-aware auto-sizing by default
    UIRect minBounds{0, 0, 32, 16};    // Minimum size constraints (only width/height used)
    UIRect maxBounds{0, 0, 800, 600};  // Maximum size constraints (only width/height used)
    int contentPadding{8};             // Padding around content for size calculations
    bool autoWidth{true};              // Auto-size width based on content
    bool autoHeight{true};             // Auto-size height based on content
    bool sizeToContent{true};          // Size exactly to fit content (vs. expand to fill)
    
    // Callback for content changes
    std::function<void()> onContentChanged;  // Called when content changes and resize is needed
};
```

### FontManager Integration

The auto-sizing system leverages FontManager's text measurement utilities:

```cpp
// Single-line text measurement
bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);

// Multi-line text measurement (automatically detects newlines)
bool measureMultilineText(const std::string& text, const std::string& fontID, 
                         int maxWidth, int* width, int* height);

// Font metrics for spacing calculations
bool getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent);
```

### Auto-Sizing Algorithm

1. **Content Analysis**: Detect text type (single-line vs multi-line)
2. **Measurement**: Use appropriate FontManager measurement function
3. **Padding Application**: Add content padding to measured dimensions
4. **Constraint Enforcement**: Apply minimum and maximum size limits
5. **Position Adjustment**: Handle special cases like title centering
6. **Callback Execution**: Trigger onContentChanged if defined

## Usage Examples

### Basic Auto-Sizing

```cpp
// Components automatically size to fit their content
ui.createLabel("info", {x, y, 0, 0}, "This text will auto-size");
ui.createButton("action", {x, y, 0, 0}, "Click Me");  // Sizes to fit text + padding

// Multi-line content is automatically detected and sized
ui.createLabel("multi", {x, y, 0, 0}, "Line 1\nLine 2\nLine 3");
```

### Fixed-Size Components

Some components use fixed-size design for specific purposes:

```cpp
// Event logs use fixed dimensions (industry standard for game event displays)
ui.createEventLog("events", {x, y, 400, 200}, 10);
ui.addEventLogEntry("events", "Long event messages automatically wrap within fixed bounds");

// Lists also use fixed dimensions with proper padding
ui.createList("options", {x, y, 220, 140});
ui.addListItem("options", "List items fit within bounds with proper padding");
```

**Fixed-Size Benefits:**
- **Predictable layout**: UI elements don't shift when content changes
- **Performance**: No recalculation needed when adding content
- **Industry standard**: Follows established patterns for event logs and chat systems
- **Word wrapping**: Long content wraps automatically within bounds

### Title Auto-Centering

```cpp
// Titles automatically center themselves when using CENTER alignment
ui.createTitle("header", {0, y, windowWidth, 40}, "Page Title");
ui.setTitleAlignment("header", UIAlignment::CENTER_CENTER);
// Title automatically repositions to center of screen after auto-sizing
```

### Manual Control

```cpp
// Disable auto-sizing for specific components
ui.enableAutoSizing("fixed_button", false);

// Set custom size constraints
UIRect minBounds{0, 0, 100, 30};   // Minimum 100x30
UIRect maxBounds{0, 0, 400, 100};  // Maximum 400x100
ui.setAutoSizingConstraints("constrained_label", minBounds, maxBounds);

// Manually trigger recalculation
ui.calculateOptimalSize("dynamic_content");
```

## Component-Specific Behavior

### Labels and Titles
- **Single-line text**: Measured using `measureText()`
- **Multi-line text**: Automatically detected and measured using `measureMultilineText()`
- **Empty text**: Falls back to minimum bounds
- **Title centering**: Automatic repositioning for CENTER-aligned titles

### Buttons
- **Content-based sizing**: Measures button text and adds appropriate padding
- **Consistent padding**: All button types use the same padding calculations
- **Type independence**: All button types (regular, success, warning, danger) behave consistently

### Lists
- **Font-based item heights**: Uses font line height + padding instead of hardcoded values
- **Dynamic item sizing**: List items automatically size based on actual font metrics
- **Content measurement**: Measures list item text for width calculations

### Input Fields
- **Placeholder consideration**: Measures placeholder text if no content is present
- **Content adaptation**: Sizes to fit current text content
- **Interaction space**: Adds extra space for cursor and user interaction

### Tooltips
- **Compact sizing**: Uses smaller tooltip-specific font (11pt) for better fit
- **Content-aware containers**: Tooltip containers automatically size to fit text
- **Multi-line support**: Properly handles tooltips with multiple lines

## Integration with Coordinate System

### SDL3 Coordinate Conversion

The system integrates with SDL3's coordinate transformation for accurate mouse input:

```cpp
// In InputManager::update() - automatic coordinate conversion
SDL_ConvertEventToRenderCoordinates(gameEngine.getRenderer(), &event);
```

**Benefits:**
- **Automatic Scaling**: SDL3 handles logical presentation, scaling, and viewport
- **Accurate Targeting**: Mouse clicks align properly with auto-sized components
- **DPI Compatibility**: Works correctly on all display types
- **No Manual Calculation**: Eliminates need for manual coordinate transformation

### Display-Aware Font Loading

FontManager automatically calculates optimal font sizes:

```cpp
// Automatic font size calculation in FontManager::loadFontsForDisplay()
float baseSizeFloat = 22.0f;  // Base font size

// Adjust for screen resolution
if (windowWidth > 1920 || windowHeight > 1080) {
    baseSizeFloat *= 1.2f; // 20% larger for high-res displays
} else if (windowWidth < 1366 || windowHeight < 768) {
    baseSizeFloat *= 0.9f; // 10% smaller for low-res displays
}

// Calculate proportional sizes
int baseFontSize = static_cast<int>(std::round(baseSizeFloat));      // 22pt
int uiFontSize = static_cast<int>(std::round(baseSizeFloat * 0.875f)); // 19pt
int titleFontSize = static_cast<int>(std::round(baseSizeFloat * 1.5f)); // 33pt
int tooltipFontSize = static_cast<int>(std::round(baseSizeFloat * 0.5f)); // 11pt
```

## Performance Considerations

### Efficient Measurement
- **Caching**: FontManager provides caching for font metrics
- **Single Calculation**: Auto-sizing occurs once during component creation
- **Lazy Evaluation**: Only recalculates when content actually changes

### Threading Compatibility
- **Main Thread Only**: Auto-sizing operations occur on the main rendering thread
- **No Blocking**: Quick calculations don't impact frame rate
- **Background Loading**: Font loading occurs in background threads

### Memory Efficiency
- **Shared Resources**: FontManager instances are shared across all components
- **Smart Pointers**: Automatic memory management for font resources
- **Minimal Overhead**: Auto-sizing properties add minimal memory footprint

## Best Practices

### Component Creation
```cpp
// Recommended: Let auto-sizing handle dimensions
ui.createLabel("dynamic", {x, y, 0, 0}, "Content");  // Width/height = 0

// Avoid: Hardcoded sizes that conflict with content
ui.createLabel("fixed", {x, y, 200, 30}, "Very long text that might not fit");
```

### Content Management
```cpp
// Good: Update text and let auto-sizing adapt
ui.setText("dynamic_label", "New longer content");
ui.calculateOptimalSize("dynamic_label");

// Good: Use callbacks for dynamic content
component->onContentChanged = [this]() {
    // Handle size changes, update layout, etc.
};
```

### Layout Integration
```cpp
// Recommended: Calculate auto-sized components before layout
ui.calculateOptimalSize("header");
ui.calculateOptimalSize("content");
ui.updateLayout("main_layout");

// Recommended: Use layout invalidation for content changes
ui.invalidateLayout("main_layout");  // Triggers recalculation of all child components
```

## Troubleshooting

### Common Issues

**Components not sizing correctly:**
- Verify font is loaded and accessible
- Check that autoSize is enabled for the component
- Ensure text content is not empty

**Text appearing cut off:**
- Check minimum/maximum size constraints
- Verify content padding settings
- Ensure container has sufficient space

**Titles not centering:**
- Confirm title alignment is set to CENTER_CENTER
- Verify auto-sizing is enabled for the title
- Check that title creation uses full window width

**Mouse clicks misaligned:**
- Ensure SDL_ConvertEventToRenderCoordinates() is being called
- Verify logical presentation mode is set correctly
- Check that input events are processed after coordinate conversion

### Debug Techniques

```cpp
// Enable debug logging for specific components
if (component->type == UIComponentType::BUTTON) {
    UI_INFO("Auto-sizing button '" + component->id + "' with text '" + component->text + 
           "': measured=" + std::to_string(contentWidth) + "x" + std::to_string(contentHeight));
}

// Verify font availability
if (!FontManager::Instance().isFontLoaded("fonts_UI_Arial")) {
    UI_ERROR("Required font not loaded");
}

// Check size constraints
UI_INFO("Component bounds: " + std::to_string(component->bounds.width) + "x" + 
        std::to_string(component->bounds.height) + 
        ", min: " + std::to_string(component->minBounds.width) + "x" + 
        std::to_string(component->minBounds.height));
```

## Future Enhancements

### Planned Features
- **Layout-Aware Sizing**: Auto-sizing that considers parent container constraints
- **Content Prediction**: Pre-calculation of sizes for dynamic content
- **Animation Support**: Smooth transitions when component sizes change
- **Custom Measurement**: Support for custom content measurement functions

### Extension Points
- **Custom Sizing Algorithms**: Pluggable sizing strategies for different component types
- **Content Observers**: Automatic detection of content changes
- **Layout Constraints**: Integration with constraint-based layout systems

## API Reference

### Core Methods
```cpp
// Enable/disable auto-sizing
void enableAutoSizing(const std::string& id, bool enable = true);

// Set size constraints
void setAutoSizingConstraints(const std::string& id, const UIRect& minBounds, const UIRect& maxBounds);

// Manual size calculation
void calculateOptimalSize(const std::string& id);
void calculateOptimalSize(std::shared_ptr<UIComponent> component);

// Content measurement
bool measureComponentContent(const std::shared_ptr<UIComponent>& component, int* width, int* height);

// Layout integration
void invalidateLayout(const std::string& layoutID);
void recalculateLayout(const std::string& layoutID);
```

### FontManager Integration
```cpp
// Text measurement utilities
bool measureText(const std::string& text, const std::string& fontID, int* width, int* height);
bool measureMultilineText(const std::string& text, const std::string& fontID, int maxWidth, int* width, int* height);
bool getFontMetrics(const std::string& fontID, int* lineHeight, int* ascent, int* descent);

// Display-aware font loading
bool loadFontsForDisplay(const std::string& fontPath, int windowWidth, int windowHeight);
```

## Conclusion

The Auto-Sizing System provides a robust, content-aware foundation for UI layout in the Forge Game Engine. By automatically calculating optimal component dimensions based on actual content and font metrics, it eliminates manual sizing calculations while ensuring consistent, professional-looking interfaces across all display types and resolutions.

The system's integration with SDL3's coordinate transformation and the engine's DPI detection ensures accurate input handling and crisp text rendering on all platforms, making it a powerful tool for creating responsive, accessible user interfaces.