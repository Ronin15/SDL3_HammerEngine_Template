# UIManager System Guide

## Overview

The UIManager is a comprehensive UI system for SDL3 games that provides reusable UI components with professional theming, content-aware auto-sizing, animations, and event handling. Key features include:

1. **Content-Aware Auto-Sizing** - Components automatically size to fit their content
2. **Professional Theme System** - Light, dark, and custom themes with automatic styling
3. **Smart Text Backgrounds** - Configurable text background colors and padding
4. **Comprehensive Component Types** - Buttons (including semantic variants), labels, titles, input fields, progress bars, sliders, checkboxes, lists, event logs, images, tooltips, dialogs, and modals
5. **Layout System** - Multiple layout types: Absolute, Flow, Grid, Stack, and Anchor
6. **Animation System** - Move and color animations with completion callbacks
7. **Event Handling** - Both callback-based and state-based event handling
8. **Modal Management** - Create and manage modal dialogs with automatic background handling
9. **Component Management** - Efficient cleanup, visibility control, and state management
10. **Z-Order Management** - Automatic depth sorting for proper rendering order
11. **Single-Threaded Architecture** - Optimized for 2D strategy and simulation games

## Quick Start

### Basic Usage

```cpp
// In your GameState's enter() method
auto& ui = UIManager::Instance();

// Create components with automatic professional styling
ui.createButton("play_btn", {300, 200, 0, 0}, "Play Game");  // Auto-sizes to fit text
ui.createTitle("header", {0, 50, windowWidth, 0}, "My Game Title");  // Auto-centers

// Set up callbacks
ui.setOnClick("play_btn", [this]() {
    gameStateManager->setState("GamePlayState");
});
```

### Essential Integration Pattern

```cpp
class MyGameState : public GameState {
public:
    void update(float deltaTime) override {
        // Update UIManager for states using UI
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
        }
        // Your state logic...
    }
    
    void render() override {
        // Render UI components
        auto& ui = UIManager::Instance();
        ui.render(GameEngine::Instance().getRenderer());
    }
    
    bool exit() override {
        // Clean up UI components
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mystate_");
        ui.resetToDefaultTheme();
        return true;
    }
};
```

## Core Features

### 1. Content-Aware Auto-Sizing

Components automatically size themselves based on content:

```cpp
// Components auto-size to fit content (width/height = 0)
ui.createLabel("info", {x, y, 0, 0}, "Dynamic Content");
ui.createButton("action", {x, y, 0, 0}, "Click Me");

// Multi-line text automatically detected and sized
ui.createLabel("multi", {x, y, 0, 0}, "Line 1\nLine 2\nLine 3");

// Titles with CENTER alignment automatically center on screen
ui.createTitle("header", {0, y, windowWidth, 0}, "Page Title");
ui.setTitleAlignment("header", UIAlignment::CENTER_CENTER);  // Auto-repositions
```

**Auto-Sizing Features:**
- **Text Measurement**: Uses FontManager for precise text dimensions
- **Multi-line Detection**: Automatically handles newlines in text
- **Content Padding**: Automatic spacing around content
- **Title Centering**: CENTER-aligned titles automatically reposition
- **Font-Based Spacing**: List items use actual font metrics for height

### 2. Professional Theme System

Automatic professional styling with theme switching:

```cpp
// Set theme mode (dark is default)
ui.setThemeMode("dark");   // Professional dark theme
ui.setThemeMode("light");  // Professional light theme

// Components automatically get consistent styling
ui.createButton("standard", bounds, "Standard");           // Blue/gray
ui.createButtonDanger("quit", bounds, "Quit");             // Red (destructive)
ui.createButtonSuccess("save", bounds, "Save");            // Green (positive)
ui.createButtonWarning("reset", bounds, "Reset");          // Orange (caution)
```

**Theme Benefits:**
- Consistent appearance across all components
- Enhanced contrast and readability
- Automatic text color coordination
- Easy light/dark mode switching

### 3. Automatic Z-Order Management

Components automatically layer correctly:

```cpp
// No manual z-order needed - automatic layering by component type
ui.createDialog("background", bounds);    // Z-order: -10 (backgrounds)
ui.createPanel("container", bounds);      // Z-order: 0 (containers)
ui.createButton("action", bounds, "OK");  // Z-order: 10 (interactive)
ui.createLabel("text", bounds, "Label");  // Z-order: 20 (text on top)

// Manual override only if needed (rarely required)
ui.setComponentZOrder("special", 50);
```

### 4. Smart Text Backgrounds

Automatic text backgrounds for readability on any surface:

```cpp
// Text backgrounds automatically applied to labels/titles
ui.createLabel("hud_health", {20, 20, 0, 0}, "Health: 100%");
ui.createTitle("level_name", {0, 50, windowWidth, 0}, "Forest Temple");

// Manual control when needed
ui.enableTextBackground("my_label", false);              // Disable
ui.setTextBackgroundColor("my_label", {0, 0, 0, 120});   // Custom color
ui.setTextBackgroundPadding("my_label", 8);              // Custom padding
```

**Smart Features:**
- Only applies to transparent-background components
- Skips components with solid backgrounds (buttons, modals)
- Theme-coordinated colors
- Perfect sizing using actual text dimensions

## Component Types

### Buttons & Semantic Types

```cpp
// Standard button
ui.createButton("my_btn", {x, y, 0, 0}, "Button Text");

// Semantic button types with automatic color coding
ui.createButtonDanger("quit_btn", {x, y, 0, 0}, "Exit");     // Red
ui.createButtonSuccess("save_btn", {x, y, 0, 0}, "Save");    // Green
ui.createButtonWarning("reset_btn", {x, y, 0, 0}, "Reset");  // Orange

// Callbacks work for all button types
ui.setOnClick("my_btn", []() { /* callback */ });

// State checking
if (ui.isButtonClicked("my_btn")) { /* handle click */ }
if (ui.isButtonHovered("my_btn")) { /* handle hover */ }
```

**Semantic Usage Guidelines:**
- **DANGER (Red)**: Quit, Exit, Delete, Remove, Destroy
- **SUCCESS (Green)**: Save, Confirm, Accept, Yes, Apply
- **WARNING (Orange)**: Cancel, Reset, Discard, Clear
- **STANDARD (Blue/Gray)**: Navigation, neutral operations

### Labels & Titles

```cpp
ui.createLabel("my_label", {x, y, 0, 0}, "Label Text");
ui.createTitle("my_title", {x, y, 0, 0}, "Title Text");  // Larger, styled
ui.setText("my_label", "New Text");
```

### Input Fields

```cpp
ui.createInputField("my_input", {x, y, 200, 30}, "Placeholder");
ui.setInputFieldMaxLength("my_input", 50);
ui.setOnTextChanged("my_input", [](const std::string& text) {
    // Handle text change
});
```

### Progress Bars & Sliders

```cpp
ui.createProgressBar("progress", {x, y, 200, 20}, 0.0f, 1.0f);
ui.setValue("progress", 0.75f);  // 75%

ui.createSlider("volume", {x, y, 200, 20}, 0.0f, 100.0f);
ui.setOnValueChanged("volume", [](float value) {
    // Handle value change
});
```

### Lists & Event Logs

```cpp
ui.createList("my_list", {x, y, 200, 150});
ui.addListItem("my_list", "Item 1");
ui.setListMaxItems("my_list", 10);  // Auto-scroll when exceeded

ui.createEventLog("events", {x, y, 400, 200}, 20);
ui.addEventLogEntry("events", "System started");
ui.addEventLogEntry("events", "Long messages automatically wrap");
```

**Event Log Features:**
- Fixed-size design (industry standard for game logs)
- Automatic word wrapping for long messages
- FIFO scrolling (oldest entries scroll out)
- Display-only (no user interaction)

### Other Components

```cpp
ui.createCheckbox("option", {x, y, 150, 30}, "Enable Feature");
ui.setChecked("option", true);

ui.createImage("logo", {x, y, 100, 100}, "texture_id");
ui.createPanel("background", {x, y, 300, 200});  // Container/background
ui.createDialog("modal", {x, y, 400, 300});      // Modal dialog background
```

## Layout System

### Layout Types

```cpp
// Automatic component positioning
ui.createLayout("my_layout", UILayoutType::FLOW, {50, 50, 700, 500});
ui.createLayout("grid", UILayoutType::GRID, {50, 50, 600, 400});
ui.createLayout("stack", UILayoutType::STACK, {100, 100, 300, 400});

// Configure layout
ui.setLayoutSpacing("my_layout", 10);
ui.setLayoutColumns("grid", 3);
ui.setLayoutAlignment("stack", UIAlignment::CENTER_CENTER);

// Add components and update
ui.addComponentToLayout("my_layout", "button1");
ui.addComponentToLayout("my_layout", "button2");
ui.updateLayout("my_layout");  // Repositions components
```

## Animation System

### Move & Color Animations

```cpp
// Smooth position changes
UIRect targetBounds = {newX, newY, width, height};
ui.animateMove("my_component", targetBounds, 0.5f, []() {
    std::cout << "Animation complete!\n";
});

// Color transitions
SDL_Color targetColor = {255, 0, 0, 255};
ui.animateColor("my_component", targetColor, 1.0f);

// Animation control
ui.stopAnimation("my_component");
bool isMoving = ui.isAnimating("my_component");
```

## Event Handling

### Callback-Based Events

```cpp
ui.setOnClick("button_id", []() { /* handle click */ });
ui.setOnValueChanged("slider_id", [](float value) { /* handle change */ });
ui.setOnTextChanged("input_id", [](const std::string& text) { /* handle text */ });
ui.setOnHover("component_id", []() { /* handle hover */ });
ui.setOnFocus("component_id", []() { /* handle focus */ });
```

### State-Based Event Checking

```cpp
void MyState::update(float deltaTime) {
    auto& ui = UIManager::Instance();
    
    if (ui.isButtonClicked("my_button")) {
        // Handle click
    }
    
    if (ui.isComponentFocused("my_input")) {
        // Input field has focus
    }
}
```

## Modal Creation & Management

### Creating Modals

```cpp
// Simplified modal creation with overlay
int dialogX = (windowWidth - 400) / 2;
int dialogY = (windowHeight - 200) / 2;

ui.createModal("dialog_id", {dialogX, dialogY, 400, 200}, "dark", windowWidth, windowHeight);

// Automatically handles:
// - Theme switching and component refresh
// - Overlay creation for background dimming
// - Dialog creation with proper z-order
// - Theme-appropriate styling for all components
```

### Background Management

```cpp
// Full-screen overlays (for menus/modals)
ui.createOverlay(windowWidth, windowHeight);

// Clean removal
ui.removeOverlay();

// For HUD elements (no overlay - game remains visible)
ui.createProgressBar("hud_health", {10, 10, 200, 20}, 0.0f, 1.0f);
// No overlay creation - game world stays visible
```

## Component Management

### Efficient Cleanup

```cpp
// Clean up by prefix (recommended)
ui.removeComponentsWithPrefix("menustate_");

// Individual removal
ui.removeComponent("specific_id");

// Nuclear cleanup (preserves theme settings)
ui.clearAllComponents();

// Theme cleanup
ui.removeOverlay();
ui.resetToDefaultTheme();
```

### Visibility & State Control

```cpp
ui.setComponentVisible("my_component", false);  // Hide temporarily
ui.setComponentEnabled("my_component", false);   // Disable interaction
ui.setComponentBounds("my_component", newBounds);

// Manual z-order override (rarely needed)
ui.setComponentZOrder("my_component", 50);
```

## Integration Patterns

### Full-Screen Menu State

```cpp
class MainMenuState : public GameState {
    bool enter() override {
        auto& ui = UIManager::Instance();
        
        // Create overlay and components
        ui.createOverlay(windowWidth, windowHeight);
        ui.createTitle("mainmenu_title", {0, 100, windowWidth, 0}, "My Game");
        ui.createButton("mainmenu_play", {300, 250, 0, 0}, "Play Game");
        ui.createButtonDanger("mainmenu_quit", {300, 320, 0, 0}, "Quit");
        
        // Set up callbacks
        ui.setOnClick("mainmenu_play", [this]() {
            gameStateManager->setState("GamePlayState");
        });
        
        return true;
    }
    
    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mainmenu_");
        ui.removeOverlay();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

### HUD/Overlay State

```cpp
class PlayerHUDState : public EntityState {
    void enter() override {
        auto& ui = UIManager::Instance();
        
        // Create HUD elements (no overlay - game visible)
        ui.createProgressBar("hud_health", {10, 10, 200, 20}, 0.0f, 100.0f);
        ui.createLabel("hud_score", {10, 40, 0, 0}, "Score: 0");
        ui.createPanel("hud_minimap", {650, 10, 140, 140});
    }
    
    void update(float deltaTime) override {
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
            
            // Update HUD with game data
            ui.setValue("hud_health", player->getHealth());
            ui.setText("hud_score", "Score: " + std::to_string(player->getScore()));
        }
    }
    
    void exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("hud_");
    }
};
```

## Architecture & Performance

### Single-Threaded Design

The UIManager uses a single-threaded architecture optimized for 2D games:

**Benefits:**
- Zero threading complexity (no locks, mutexes, race conditions)
- Excellent performance for 2D games (10+ ms headroom available)
- Easy debugging and maintenance
- Predictable control flow

**Performance Characteristics:**
- **Light UI** (HUD): ~0.5ms overhead
- **Medium UI** (menus): ~1ms overhead
- **Heavy UI** (complex interfaces): ~1.5ms overhead
- **Excellent headroom** for all scenarios in 2D games

### Integration with Game Engine

```cpp
// GameEngine automatically handles UI updates
void GameEngine::render(float interpolation) {
    // All rendering on main thread (SDL requirement)
    mp_gameStateManager->render();  // Includes UI rendering
}
```

### Manager Dependencies

- **FontManager**: Text rendering and measurement for auto-sizing
- **InputManager**: Mouse and keyboard input handling
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

### Performance Guidelines

```cpp
// ✅ GOOD: Update dynamic content only when needed
if (playerHealthChanged) {
    ui.setValue("health_bar", newHealth);
}

// ❌ BAD: Update every frame unnecessarily
ui.setValue("health_bar", player.getHealth());  // Wasteful

// ✅ GOOD: Batch related updates
ui.setText("score", scoreText);
ui.setText("level", levelText);
ui.setText("lives", livesText);

// ✅ GOOD: Use auto-sizing for dynamic content
ui.createLabel("status", {x, y, 0, 0}, dynamicText);  // Auto-sizes
```

### Error Handling

```cpp
void YourState::update(float deltaTime) {
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        try {
            ui.update(deltaTime);
            updateDynamicUI();
        } catch (const std::exception& e) {
            std::cerr << "UI error: " << e.what() << std::endl;
        }
    }
}
```

### State Lifecycle Management

```cpp
bool GameState::enter() {
    // Create UI components
    ui.createOverlay(windowWidth, windowHeight);  // For full-screen menus
    ui.createButton("state_button", bounds, "Click Me");
    ui.createButtonDanger("state_back", bounds, "Back");
    return true;
}

bool GameState::exit() {
    // Clean up efficiently
    ui.removeComponentsWithPrefix("state_");
    ui.removeOverlay();
    ui.resetToDefaultTheme();  // Prevent theme contamination
    return true;
}
```

## Custom Styling (Advanced)

### When Manual Styling is Needed

```cpp
// Only customize for special cases - 98% of components use theme defaults
UIStyle customStyle;
customStyle.textColor = {255, 215, 0, 255};  // Gold
customStyle.fontSize = 32;
customStyle.textAlign = UIAlignment::CENTER_CENTER;
ui.setStyle("special_title", customStyle);
```

### Global Settings

```cpp
ui.setGlobalFont("my_font_id");
ui.setGlobalScale(1.5f);        // 150% scale
ui.enableTooltips(true);
ui.setTooltipDelay(1.0f);       // 1 second delay
```

## Troubleshooting

### Common Issues

**UI not updating/responding:**
- Ensure `UIManager::update()` is called in your state's update method

**UI not rendering:**
- Ensure `UIManager::render()` is called in your state's render method

**Components persist between states:**
- Use `removeComponentsWithPrefix()` or `clearAllComponents()` in state exit

**Inconsistent styling:**
- Use `resetToDefaultTheme()` when exiting states
- Avoid manual styling unless necessary

**Auto-sizing not working:**
- Check that width/height are set to 0 for auto-sizing
- Verify font is loaded and accessible
- Ensure content is not empty

### Debug Support

```cpp
ui.setDebugMode(true);
ui.drawDebugBounds(true);  // Shows red component boundaries

// Verify components exist
if (ui.hasComponent("my_component")) {
    ui.setText("my_component", "New Text");
}
```

## Complete Example: Confirmation Dialog

```cpp
class ConfirmationDialogState : public GameState {
public:
    bool enter() override {
        auto& ui = UIManager::Instance();
        auto& gameEngine = GameEngine::Instance();
        int windowWidth = gameEngine.getLogicalWidth();
        int windowHeight = gameEngine.getLogicalHeight();
        
        // Create modal with centered positioning
        int dialogX = (windowWidth - 400) / 2;
        int dialogY = (windowHeight - 200) / 2;
        
        ui.createModal("dialog", {dialogX, dialogY, 400, 200}, "dark", windowWidth, windowHeight);
        ui.createTitle("dialog_title", {dialogX + 20, dialogY + 20, 360, 0}, "Confirm Action");
        ui.createLabel("dialog_text", {dialogX + 20, dialogY + 60, 360, 0}, "Are you sure you want to quit?");
        ui.createButtonSuccess("dialog_yes", {dialogX + 50, dialogY + 120, 0, 0}, "Yes");
        ui.createButtonWarning("dialog_cancel", {dialogX + 250, dialogY + 120, 0, 0}, "Cancel");
        
        // Set up callbacks
        ui.setOnClick("dialog_yes", []() {
            GameEngine::Instance().setRunning(false);
        });
        
        ui.setOnClick("dialog_cancel", [this]() {
            gameStateManager->popState();
        });
        
        return true;
    }
    
    void update(float deltaTime) override {
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
        }
    }
    
    void render() override {
        auto& ui = UIManager::Instance();
        ui.render(GameEngine::Instance().getRenderer());
    }
    
    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("dialog_");
        ui.removeOverlay();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

## API Reference

### Core Initialization Methods

```cpp
bool init()                              // Initialize UIManager
void clean()                            // Clean shutdown
bool isShutdown() const                 // Check shutdown state
void update(float deltaTime)            // Update animations and input
void render(SDL_Renderer* renderer)    // Render all components
void render()                           // Render using cached renderer
void setRenderer(SDL_Renderer* renderer) // Set cached renderer
SDL_Renderer* getRenderer() const      // Get cached renderer
```

### Component Creation Methods

```cpp
// Buttons and semantic variants
void createButton(const std::string& id, const UIRect& bounds, const std::string& text = "");
void createButtonDanger(const std::string& id, const UIRect& bounds, const std::string& text = "");
void createButtonSuccess(const std::string& id, const UIRect& bounds, const std::string& text = "");
void createButtonWarning(const std::string& id, const UIRect& bounds, const std::string& text = "");

// Text components
void createLabel(const std::string& id, const UIRect& bounds, const std::string& text = "");
void createTitle(const std::string& id, const UIRect& bounds, const std::string& text);

// Input components
void createInputField(const std::string& id, const UIRect& bounds, const std::string& placeholder = "");
void createCheckbox(const std::string& id, const UIRect& bounds, const std::string& text = "");
void createSlider(const std::string& id, const UIRect& bounds, float minVal = 0.0f, float maxVal = 1.0f);

// Visual components
void createPanel(const std::string& id, const UIRect& bounds);
void createProgressBar(const std::string& id, const UIRect& bounds, float minVal = 0.0f, float maxVal = 1.0f);
void createImage(const std::string& id, const UIRect& bounds, const std::string& textureID = "");

// Container components
void createList(const std::string& id, const UIRect& bounds);
void createEventLog(const std::string& id, const UIRect& bounds, int maxEntries = 5);
void createDialog(const std::string& id, const UIRect& bounds);
void createTooltip(const std::string& id, const std::string& text = "");

// Modal system
void createModal(const std::string& dialogId, const UIRect& bounds, const std::string& theme, int windowWidth, int windowHeight);
void createOverlay(const std::string& id, int windowWidth, int windowHeight);
void removeOverlay();
```

### Component Management

```cpp
void removeComponent(const std::string& id);
void removeComponentsWithPrefix(const std::string& prefix);
void clearAllComponents();
bool hasComponent(const std::string& id) const;
void setComponentVisible(const std::string& id, bool visible);
void setComponentEnabled(const std::string& id, bool enabled);
void setComponentBounds(const std::string& id, const UIRect& bounds);
void setComponentZOrder(const std::string& id, int zOrder);
```

### Property Setters and Getters

```cpp
// Setters
void setText(const std::string& id, const std::string& text);
void setTexture(const std::string& id, const std::string& textureID);
void setValue(const std::string& id, float value);
void setChecked(const std::string& id, bool checked);
void setStyle(const std::string& id, const UIStyle& style);

// Getters
std::string getText(const std::string& id) const;
float getValue(const std::string& id) const;
bool getChecked(const std::string& id) const;
UIRect getBounds(const std::string& id) const;
UIState getComponentState(const std::string& id) const;
```

### Event Handling

```cpp
// Callback registration
void setOnClick(const std::string& id, std::function<void()> callback);
void setOnValueChanged(const std::string& id, std::function<void(float)> callback);
void setOnTextChanged(const std::string& id, std::function<void(const std::string&)> callback);
void setOnHover(const std::string& id, std::function<void()> callback);
void setOnFocus(const std::string& id, std::function<void()> callback);

// State checking
bool isButtonClicked(const std::string& id) const;
bool isButtonPressed(const std::string& id) const;
bool isButtonHovered(const std::string& id) const;
bool isComponentFocused(const std::string& id) const;
```

### Layout System

```cpp
void createLayout(const std::string& id, UILayoutType type, const UIRect& bounds);
void addComponentToLayout(const std::string& layoutId, const std::string& componentId);
void removeComponentFromLayout(const std::string& layoutId, const std::string& componentId);
void updateLayout(const std::string& layoutId);
void setLayoutSpacing(const std::string& layoutId, int spacing);
void setLayoutColumns(const std::string& layoutId, int columns);
void setLayoutAlignment(const std::string& layoutId, UIAlignment alignment);
```

### Specialized Component Features

```cpp
// Progress bars
void updateProgressBar(const std::string& id, float value);
void setProgressBarRange(const std::string& id, float minVal, float maxVal);

// Lists
void addListItem(const std::string& id, const std::string& item);
void removeListItem(const std::string& id, int index);
void clearList(const std::string& id);
int getSelectedListItem(const std::string& id) const;
void setSelectedListItem(const std::string& id, int index);
void setListMaxItems(const std::string& id, int maxItems);
void addListItemWithAutoScroll(const std::string& id, const std::string& item);
void clearListItems(const std::string& id);

// Event logs
void addEventLogEntry(const std::string& id, const std::string& entry);
void clearEventLog(const std::string& id);
void setEventLogMaxEntries(const std::string& id, int maxEntries);
void setupDemoEventLog(const std::string& id);
void enableEventLogAutoUpdate(const std::string& id, bool enable);
void disableEventLogAutoUpdate(const std::string& id);

// Title alignment
void setTitleAlignment(const std::string& id, UIAlignment alignment);
void centerTitleInContainer(const std::string& id, int containerWidth);

// Input fields
void setInputFieldPlaceholder(const std::string& id, const std::string& placeholder);
void setInputFieldMaxLength(const std::string& id, int maxLength);
bool isInputFieldFocused(const std::string& id) const;
```

### Animation System

```cpp
void animateMove(const std::string& id, const UIRect& targetBounds, float duration, std::function<void()> onComplete = nullptr);
void animateColor(const std::string& id, const SDL_Color& targetColor, float duration, std::function<void()> onComplete = nullptr);
void stopAnimation(const std::string& id);
bool isAnimating(const std::string& id) const;
```

### Theme Management

```cpp
void loadTheme(const std::string& themeName);
void setDefaultTheme();
void setLightTheme();
void setDarkTheme();
void setThemeMode(const std::string& mode);
std::string getCurrentThemeMode() const;
void applyThemeToComponent(const std::string& id, UIComponentType type);
void setGlobalStyle(const UIStyle& style);
void refreshAllComponentThemes();
void resetToDefaultTheme();
```

### Text Background Features

```cpp
void enableTextBackground(const std::string& id, bool enable);
void setTextBackgroundColor(const std::string& id, const SDL_Color& color);
void setTextBackgroundPadding(const std::string& id, int padding);
```

### Auto-Sizing and Layout

```cpp
void calculateOptimalSize(const std::string& id);
void calculateOptimalSize(const std::string& id, int maxWidth, int maxHeight);
bool measureComponentContent(const std::string& id, int& outWidth, int& outHeight);
void invalidateLayout(const std::string& id);
void recalculateLayout(const std::string& id);
void enableAutoSizing(const std::string& id, bool autoWidth, bool autoHeight);
void setAutoSizingConstraints(const std::string& id, const UIRect& minBounds, const UIRect& maxBounds);
```

### Global Settings

```cpp
void setGlobalFont(const std::string& fontID);
void setGlobalScale(float scale);
float getGlobalScale() const;
void enableTooltips(bool enable);
void setTooltipDelay(float delay);
```

### State Transition Support

```cpp
void cleanupForStateTransition();
void prepareForStateTransition();
```

This UIManager system provides everything needed for sophisticated game UIs while maintaining simplicity and professional appearance out-of-the-box. The content-aware auto-sizing, professional theming, and single-threaded architecture make it ideal for 2D strategy and simulation games.