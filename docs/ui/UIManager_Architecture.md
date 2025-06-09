# UIManager Architecture Documentation

## Overview

The UIManager follows a **hybrid architecture** with **centralized theme management** that balances performance, flexibility, and ease of use. This design ensures optimal performance for world simulation systems while providing state-specific UI control with consistent professional styling.

## Architectural Principles

### 1. Hybrid System Management

**Global Systems (Updated by GameEngine):**
- **InputManager**: Core input handling, always needed across states
- **AIManager**: World simulation with thousands of entities, benefits from consistent updates
- **EventManager**: Global game events, batch processing optimization

**State-Managed Systems (Updated by individual states):**
- **UIManager**: Optional, state-specific, only updated when UI is actually used
- **Audio/Visual effects**: State-specific requirements and lifecycle

### 2. Content-Aware Auto-Sizing System

**Intelligent Component Sizing:**
The UIManager features a core auto-sizing system that automatically calculates optimal component dimensions based on content:

```cpp
// Auto-sizing is enabled by default for all components
ui.createLabel("info", {x, y, 0, 0}, "Dynamic Content");    // Sizes to fit text
ui.createButton("action", {x, y, 0, 0}, "Click Me");       // Sizes to fit text + padding
ui.createTitle("header", {0, y, windowWidth, 0}, "Title"); // Auto-centers when CENTER aligned

// Multi-line text automatically detected and sized correctly
ui.createLabel("multi", {x, y, 0, 0}, "Line 1\nLine 2\nLine 3"); // Height = 3 * line height

// List items use font-based heights instead of hardcoded values
ui.createList("items", bounds); // Item height = font line height + padding
```

**Auto-Sizing Features:**
- **Text Measurement**: Uses FontManager to measure actual text dimensions
- **Multi-line Detection**: Automatically detects newlines and calculates multi-line text height
- **Font-Based Spacing**: List items and UI elements size based on actual font metrics
- **Content Padding**: Automatic padding around content for proper spacing
- **Size Constraints**: Configurable minimum and maximum size limits
- **Title Centering**: Titles with CENTER alignment automatically reposition to stay centered

**Auto-Sizing Control:**
```cpp
// Enable/disable auto-sizing per component
ui.enableAutoSizing("component_id", true/false);

// Set size constraints
UIRect minBounds{0, 0, 32, 16};   // Minimum 32x16
UIRect maxBounds{0, 0, 800, 600}; // Maximum 800x600
ui.setAutoSizingConstraints("component_id", minBounds, maxBounds);

// Manual size calculation
ui.calculateOptimalSize("component_id"); // Recalculate size after content changes
```

### 3. Centralized Theme Management

**Professional Styling and Auto-Sizing Out-of-the-Box:**
```cpp
// Components automatically use professional themes and content-aware sizing
ui.createButton("my_btn", bounds, "Text");           // Auto-sizes to fit text content
ui.createButtonDanger("quit_btn", bounds, "Exit");   // Red styling, auto-sized
ui.createButtonSuccess("save_btn", bounds, "Save");  // Green styling, auto-sized
ui.createButtonWarning("reset_btn", bounds, "Reset"); // Orange styling, auto-sized
ui.createLabel("info", bounds, "Multi-line\nContent"); // Auto-detects newlines, sizes correctly
ui.createTitle("header", bounds, "Page Title");      // Auto-centers on screen when CENTER aligned
ui.createList("my_list", bounds);                    // Font-based item heights for accuracy
ui.setThemeMode("dark");                             // Switch themes instantly
```

**Benefits:**
- **Content-Aware Auto-Sizing**: Components automatically size to fit their content
- **Multi-line Text Support**: Automatic detection and sizing for text with newlines
- **Title Auto-Centering**: Titles with CENTER alignment automatically position themselves
- **Font-Based Measurements**: List items and spacing based on actual font metrics
- No manual styling required for 98% of components
- Consistent appearance across entire application
- Enhanced UX with improved contrast and usability
- Easy theme switching (light/dark modes)

### 4. State-Driven UI Updates

**Correct Implementation Pattern:**
```cpp
void UIExampleState::update(float deltaTime) {
    // Each state that uses UI is responsible for updating it
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime);
    }
    
    // State-specific logic...
}

void UIExampleState::render() {
    // Each state renders its UI components
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
}

void UIExampleState::exit() {
    // Clean up using centralized methods
    auto& ui = UIManager::Instance();
    ui.removeComponentsWithPrefix("mystate_");
    ui.removeThemeBackground();
    ui.resetToDefaultTheme();
}
```

## Key Architectural Benefits

### Performance Optimization
- **Conditional Updates**: UI only processed when states actually use UI components
- **Global World Systems**: AI/Events benefit from consistent updates and threading
- **Manager Caching**: GameEngine caches manager references for optimal performance
- **Memory Efficiency**: No unnecessary UI processing in non-UI game states

### Clean Separation of Concerns
- **Engine Responsibility**: Handles world simulation, core systems, and resource management
- **State Responsibility**: Manages state-specific UI needs and optional systems
- **Manager Responsibility**: Provides framework without dictating usage patterns

### Maximum Flexibility
- **World Consistency**: Global systems ensure consistent world state across states
- **State-Specific Behavior**: Each state customizes UI without affecting global performance
- **Conditional Systems**: States opt-in to UI without affecting global performance
- **Independent Lifecycle**: UI lifecycle tied to state lifecycle, preventing resource leaks

## Implementation Guidelines

### For States Using UI

```cpp
class YourGameState : public GameState {
public:
    void update(float deltaTime) override {
        // REQUIRED: Update UIManager for states using UI
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
        }
        
        // Your state logic...
        // DO NOT update global systems (AI, Events, Input) - handled by GameEngine
    }
    
    void render() override {
        // REQUIRED: Render UI components
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
    bool exit() override {
        // REQUIRED: Clean up UI state
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("yourstate_");
        ui.removeThemeBackground();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

### For States Not Using UI

```cpp
class NonUIGameState : public GameState {
    void update(float deltaTime) override {
        // No UIManager calls needed - pattern is self-selecting
        // State-specific updates only
        // Global systems handled by GameEngine
    }
    
    void render() override {
        // State-specific rendering only
        // No UIManager rendering needed
    }
};
```

## Auto-Sizing Integration with FontManager

### Font System Integration
The auto-sizing system is deeply integrated with the FontManager for accurate text measurement:

```cpp
// FontManager provides text measurement utilities
FontManager& fontMgr = FontManager::Instance();

// Single-line text measurement
int width, height;
fontMgr.measureText("Button Text", "fonts_UI_Arial", &width, &height);

// Multi-line text measurement (detects newlines automatically)
fontMgr.measureMultilineText("Line 1\nLine 2", "fonts_UI_Arial", 0, &width, &height);

// Font metrics for spacing calculations
int lineHeight, ascent, descent;
fontMgr.getFontMetrics("fonts_UI_Arial", &lineHeight, &ascent, &descent);
```

### Automatic Font Size Selection
The system automatically selects appropriate font sizes based on display characteristics:

```cpp
// FontManager calculates display-aware font sizes
fontMgr.loadFontsForDisplay("res/fonts", windowWidth, windowHeight);

// Creates fonts with calculated sizes:
// - base font: 22pt (main content)
// - UI font: 19pt (interface elements)  
// - title font: 33pt (headings)
// - tooltip font: 11pt (compact tooltips)
```

### DPI-Aware Auto-Sizing
Auto-sizing works seamlessly with SDL3's logical presentation system:

- **Logical Coordinates**: All measurements done in logical units
- **Automatic Scaling**: SDL3 handles physical display scaling
- **Consistent Sizing**: Components appear correctly on all display types
- **Font Quality**: Crisp text rendering at any DPI

## Component Lifecycle Management

### Efficient Cleanup Patterns

```cpp
// Bulk cleanup by prefix (recommended)
ui.removeComponentsWithPrefix("menustate_");

// Theme management
ui.createThemeBackground(width, height);  // Full-screen overlay
ui.removeThemeBackground();               // Clean removal
ui.resetToDefaultTheme();                 // Prevent theme contamination

// Nuclear cleanup (preserves theme background)
ui.clearAllComponents();
```

### Background Management Strategies

```cpp
// Full-screen menus (with background overlay)
ui.createThemeBackground(windowWidth, windowHeight);
ui.createButton("menu_play", bounds, "Play Game");

// HUD elements (no overlay - game visible)
ui.createProgressBar("hud_health", bounds, 0.0f, 1.0f);
ui.createLabel("hud_score", bounds, "Score: 0");
// No background creation - game remains visible
```

## Threading Considerations

### Thread Safety
- UIManager designed for main thread only (OpenGL/SDL rendering thread)
- State updates occur on main thread, ensuring thread safety
- No additional synchronization needed for UIManager operations

### Performance with Threading
- UI updates don't block background thread operations
- Game logic threading remains independent of UI operations
- ThreadSystem continues processing while UI renders

## Integration with GameEngine

### GameEngine Responsibilities
```cpp
void GameEngine::update(float deltaTime) {
    // Global systems updated by GameEngine for optimal performance
    mp_inputManager->update();                    // Always needed
    mp_aiManager->update(deltaTime);             // World simulation
    mp_eventManager->update();                   // Global events
    
    // State-specific systems handled by states
    mp_gameStateManager->update(deltaTime);     // Delegates to current state
}
```

### Manager Dependencies
- **FontManager**: Text rendering for all UI components with DPI-aware scaling
- **InputManager**: Mouse and keyboard input handling
- **GameEngine**: DPI scale factor and rendering context

### DPI-Aware Scaling Integration

The UIManager integrates seamlessly with the engine's DPI detection system:

```cpp
// Automatic DPI integration during initialization
bool UIManager::init() {
    // Get centralized DPI scale from GameEngine
    auto& gameEngine = GameEngine::Instance();
    m_globalScale = gameEngine.getDPIScale();
    
    // All text positioning automatically scaled
    return true;
}

// All text rendering applies DPI scaling
void UIManager::renderButton(std::shared_ptr<UIComponent> component, SDL_Renderer* renderer) {
    // Apply global scale to font positioning
    int scaledX = static_cast<int>((component->bounds.x + component->bounds.width / 2) * m_globalScale);
    int scaledY = static_cast<int>((component->bounds.y + component->bounds.height / 2) * m_globalScale);
    
    FontManager::Instance().drawTextAligned(component->text, component->style.fontID,
                                           scaledX, scaledY, component->style.textColor, renderer, 0);
}
```

**DPI System Benefits:**
- **Automatic Detection**: GameEngine detects display pixel density during initialization
- **Centralized Management**: Single DPI scale value shared across all managers
- **Quality Fonts**: FontManager loads DPI-appropriate font sizes with quality optimizations
- **Crisp UI**: All text positioning scaled and pixel-aligned for sharp rendering
- **Cross-Platform**: Works on standard, high-DPI, and 4K/Retina displays
- **TextureManager**: Image loading for UI graphics
- **GameEngine**: Window dimensions and renderer access

## Best Practices

### Component Naming Convention
```cpp
// Use state prefixes for efficient cleanup
ui.createButton("mainmenu_play_btn", bounds, "Play");
ui.createButtonDanger("mainmenu_quit_btn", bounds, "Quit");
ui.createSlider("options_volume_slider", bounds, 0.0f, 100.0f);
ui.createProgressBar("hud_health_bar", bounds, 0.0f, 1.0f);
```

### Error Handling
```cpp
void YourState::update(float deltaTime) {
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        try {
            ui.update(deltaTime);
        } catch (const std::exception& e) {
            std::cerr << "UI update error: " << e.what() << std::endl;
        }
    }
}
```

### State Lifecycle Best Practices
```cpp
class YourUIState : public GameState {
public:
    bool enter() override {
        // Create UI components when entering state
        auto& ui = UIManager::Instance();
        ui.createThemeBackground(windowWidth, windowHeight);
        ui.createButton("mystate_button", bounds, "Click Me");
        ui.createButtonDanger("mystate_back", bounds, "Back");
        ui.setOnClick("mystate_button", [this]() { handleClick(); });
        return true;
    }
    
    bool exit() override {
        // Clean up UI components when exiting state
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mystate_");
        ui.removeThemeBackground();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

## Troubleshooting

### Common Issues & Solutions

**UI not updating/responding:**
- Ensure `UIManager::update()` is called in your UI state's update method

**UI not rendering:**
- Ensure `UIManager::render()` is called in your UI state's render method

**Components persist between states:**
- Use `removeComponentsWithPrefix()` or `clearAllComponents()` in state exit

**Inconsistent styling across states:**
- Use `resetToDefaultTheme()` when exiting states
- Avoid manual styling unless necessary for special cases

**Performance issues:**
- Remove redundant manager updates from states (AI/Event/Input handled globally)
- Verify GameEngine properly updates global systems before state updates

## Architecture Decision Summary

This hybrid architecture provides:

1. **Optimal Performance**: Global systems updated efficiently, UI only when needed
2. **Clean Code**: Clear separation between world simulation and UI concerns
3. **Flexibility**: States control their specific UI needs without affecting others
4. **Professional Appearance**: Centralized themes provide consistent, polished look
5. **Easy Maintenance**: Bulk cleanup methods and theme management simplify state transitions

The design successfully balances performance, maintainability, and ease of use while following established patterns in the existing codebase.