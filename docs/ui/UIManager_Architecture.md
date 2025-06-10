# UIManager Architecture Documentation

## Overview

The UIManager follows a **single-threaded architecture** designed for **2D strategy and simulation games**. This architecture prioritizes simplicity, maintainability, and excellent performance for UI-heavy gameplay while eliminating threading complexity.

## Architectural Principles

### 1. Single-Thread UI Architecture

**All UI operations happen on the main thread:**
- **UI Updates**: Component state changes, animations, input handling
- **UI Rendering**: Drawing all UI components to screen
- **Input Processing**: Mouse/keyboard interaction with UI elements

**Benefits:**
- ✅ **Zero threading complexity** - no locks, mutexes, or race conditions
- ✅ **Excellent performance** for 2D games - plenty of headroom on main thread
- ✅ **Easy debugging** - all UI operations in single thread
- ✅ **Maintainable code** - straightforward control flow

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

### 4. Unified Update-Render Pattern

**New Simplified Pattern:**
```cpp
void MyGameState::update(float deltaTime) {
    // Game logic only - no UI updates
    handleInput();
    updateGameLogic(deltaTime);
}

void MyGameState::render(float deltaTime) {
    // Update and render UI in single method for simplicity
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime);  // Update UI state
        
        // Update dynamic UI content
        updatePlayerStats();
        updateInventoryDisplay();
    }
    ui.render();  // Render UI components
}
```

## Key Architectural Benefits

### Performance for 2D Games
- **Excellent headroom**: 2D rendering leaves 10+ ms available for UI operations
- **Perfect for strategy games**: UI-heavy gameplay is expected and handled efficiently
- **No threading overhead**: Eliminates context switching and synchronization costs
- **Predictable timing**: All operations happen in deterministic order

### Simplified Development
- **Single thread model**: No complex threading patterns to understand
- **Easy debugging**: All UI state changes happen in predictable sequence
- **Straightforward profiling**: Performance bottlenecks easy to identify
- **Reduced complexity**: No locks, atomics, or race conditions to manage

### Ideal for Strategy/Simulation Games
- **UI-heavy gameplay**: Perfect for complex interfaces with many panels
- **Real-time updates**: Inventory, stats, resource counters update smoothly
- **Complex interactions**: Drag-and-drop, tooltips, modals work seamlessly
- **Professional appearance**: Consistent theming across complex UIs

## Implementation Guidelines

### For GameStates Using UI

```cpp
class MyUIState : public GameState {
public:
    bool enter() override {
        // Create UI components when entering state
        auto& ui = UIManager::Instance();
        ui.createTitle("mystate_title", {0, 50, windowWidth, 0}, "My Game State");
        ui.createButton("mystate_button", {100, 200, 0, 0}, "Click Me");
        ui.createButtonDanger("mystate_back", {100, 300, 0, 0}, "Back");
        ui.setOnClick("mystate_button", [this]() { handleClick(); });
        return true;
    }
    
    void update(float deltaTime) override {
        // Game logic only - UI updates moved to render()
        handleInput();
        updateGameLogic(deltaTime);
    }
    
    void render(float deltaTime) override {
        // Update and render UI components
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);  // UI animations, input handling, etc.
            
            // Update dynamic content
            ui.setText("mystate_status", getCurrentStatus());
            ui.setValue("mystate_progress", getProgressValue());
        }
        ui.render();  // Draw all UI components
    }
    
    bool exit() override {
        // Clean up UI components when exiting state
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mystate_");
        ui.resetToDefaultTheme();
        return true;
    }
};
```

### For GameStates Not Using UI

```cpp
class NonUIGameState : public GameState {
    void update(float deltaTime) override {
        // Pure game logic, no UI
        updatePhysics(deltaTime);
        updateAI(deltaTime);
    }
    
    void render(float deltaTime) override {
        // Render game world, no UI
        renderGameWorld();
        renderEntities();
    }
};
```

## Threading Model

### Main Thread Responsibilities
- **SDL Event Handling**: Input processing (SDL requirement)
- **UI Operations**: All UIManager operations for thread safety
- **Game Rendering**: 2D graphics rendering (SDL requirement)
- **Game Logic**: Update thread delegates to main thread for frame consistency

### Update Thread Responsibilities  
- **AI Processing**: Large-scale AI behavior updates
- **Physics**: Game world simulation
- **Background Tasks**: Non-UI data processing

### Thread Communication
- **Minimal coupling**: Update thread focuses on data, main thread on presentation
- **No shared UI state**: All UI operations contained on main thread
- **Simple coordination**: GameLoop coordinates update/render timing

## Performance Characteristics

### Timing Breakdown (2D Strategy Game)
```
Target: 16.67ms per frame (60 FPS)

Main Thread Work:
├─ SDL Events:        0.1ms
├─ UI Updates:        0.5-1.5ms (depending on complexity)
├─ Game Rendering:    3-8ms (2D sprites, effects)
├─ UI Rendering:      2-6ms (text, panels, animations)
└─ Available:         2-11ms headroom

UI Update Details:
├─ Input Handling:    0.1-0.3ms
├─ Animations:        0.1-0.5ms  
├─ Text Updates:      0.1-0.3ms
├─ Layout Updates:    0.1-0.2ms
└─ State Management:  0.1-0.2ms
```

### Scalability
- **Light UI** (HUD): ~0.5ms overhead
- **Medium UI** (menus): ~1ms overhead  
- **Heavy UI** (complex strategy interface): ~1.5ms overhead
- **Excellent headroom** for all scenarios in 2D games

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
ui.createOverlay(width, height);  // Full-screen overlay
ui.removeOverlay();               // Clean removal
ui.resetToDefaultTheme();         // Prevent theme contamination

// Nuclear cleanup (preserves theme settings)
ui.clearAllComponents();
```

### Background Management Strategies

```cpp
// Full-screen menus (with overlay background)
ui.createOverlay(windowWidth, windowHeight);
ui.createButton("menu_play", bounds, "Play Game");

// HUD elements (no overlay - game visible)
ui.createProgressBar("hud_health", bounds, 0.0f, 1.0f);
ui.createLabel("hud_score", bounds, "Score: 0");
// No overlay creation - game remains visible
```

## Integration with GameEngine

### GameEngine Responsibilities
```cpp
void GameEngine::update(float deltaTime) {
    // Heavy data processing on update thread
    mp_aiManager->update(deltaTime);             // AI simulation
    mp_eventManager->update();                   // Global events
    
    // UI handled by states on main thread
    mp_gameStateManager->update(deltaTime);     // Delegates to current state
}

void GameEngine::render(float interpolation) {
    // All rendering on main thread (SDL requirement)
    mp_gameStateManager->render(deltaTime);     // Includes UI rendering
}
```

### Manager Dependencies
- **FontManager**: Text rendering for all UI components with DPI-aware scaling
- **InputManager**: Mouse and keyboard input handling (main thread)
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
void YourState::render(float deltaTime) {
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        try {
            ui.update(deltaTime);
            updateDynamicUI();
        } catch (const std::exception& e) {
            std::cerr << "UI error: " << e.what() << std::endl;
        }
    }
    ui.render();
}
```

### Performance Guidelines
```cpp
// ✅ GOOD: Update dynamic content only when needed
if (playerHealthChanged) {
    ui.setValue("health_bar", newHealth);
}

// ❌ BAD: Update every frame unnecessarily
ui.setValue("health_bar", player.getHealth()); // Every frame is wasteful

// ✅ GOOD: Batch related updates
ui.setText("score", scoreText);
ui.setText("level", levelText);
ui.setText("lives", livesText);

// ✅ GOOD: Use auto-sizing for dynamic content
ui.createLabel("status", {x, y, 0, 0}, dynamicText); // Auto-sizes
```

## Troubleshooting

### Common Issues & Solutions

**UI not updating/responding:**
- Ensure `UIManager::update()` is called in your state's render method
- Check that the state's render method is being called

**UI not rendering:**
- Ensure `UIManager::render()` is called after `ui.update()`
- Verify renderer is properly initialized

**Components persist between states:**
- Use `removeComponentsWithPrefix()` or `clearAllComponents()` in state exit
- Ensure exit() method is properly implemented

**Performance issues:**
- Profile UI update time vs rendering time
- Consider reducing update frequency for non-critical elements
- Use auto-sizing efficiently (don't recalculate unnecessarily)

**Input not working:**
- Check that InputManager is initialized and updating on main thread
- Verify UI components are visible and enabled
- Ensure proper z-order for overlapping components

## Architecture Decision Summary

This single-threaded architecture provides:

1. **Excellent Performance**: Perfect for 2D strategy/simulation games with complex UIs
2. **Zero Complexity**: No threading issues, locks, or race conditions  
3. **Easy Maintenance**: Simple, predictable control flow
4. **Professional Appearance**: Centralized themes and auto-sizing
5. **Developer Friendly**: Straightforward debugging and profiling

The design successfully eliminates threading complexity while delivering excellent performance for UI-heavy 2D games, making it ideal for strategy and simulation genres where complex interfaces are essential.