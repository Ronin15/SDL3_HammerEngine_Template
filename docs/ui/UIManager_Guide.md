# UIManager System Guide

## Overview

The UIManager is a comprehensive UI system for SDL3 games that provides reusable UI components with professional theming, animations, layouts, and event handling. It follows a component-based architecture designed for use in GameStates and EntityStates.

## Quick Start

### Basic Usage

```cpp
// In your GameState's enter() method
auto& ui = UIManager::Instance();

// Create components with automatic professional styling and z-order
ui.createButton("play_btn", {300, 200, 200, 50}, "Play Game");
ui.createLabel("title", {0, 50, 800, 60}, "My Game Title");

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
        // REQUIRED: Update UIManager for states using UI
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
        }
        // Your state logic...
    }
    
    void render() override {
        // REQUIRED: Render UI components
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
    bool exit() override {
        // REQUIRED: Clean up UI components
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mystate_");
        return true;
    }
};
```

## Component Types & Usage

### Automatic Z-Order System
```cpp
// Components automatically get proper layering - no manual z-order needed!
ui.createDialog("background", bounds);    // Auto z-order: -10 (background)
ui.createPanel("container", bounds);      // Auto z-order: 0 (containers)
ui.createButton("action", bounds, "OK");  // Auto z-order: 10 (interactive)
ui.createLabel("text", bounds, "Label");  // Auto z-order: 20 (text on top)

// Manual override only if needed (rarely required)
ui.setComponentZOrder("special_component", 50);
```

### Buttons
```cpp
ui.createButton("my_btn", {x, y, width, height}, "Button Text");
ui.setOnClick("my_btn", []() { /* callback */ });

// State checking
if (ui.isButtonClicked("my_btn")) { /* handle click */ }
if (ui.isButtonHovered("my_btn")) { /* handle hover */ }
```

### Labels & Titles
```cpp
ui.createLabel("my_label", {x, y, width, height}, "Label Text");
ui.createTitle("my_title", {x, y, width, height}, "Title Text"); // Larger, gold styling
ui.setText("my_label", "New Text");
```

### Input Fields
```cpp
ui.createInputField("my_input", {x, y, width, height}, "Placeholder");
ui.setInputFieldMaxLength("my_input", 50);
ui.setOnTextChanged("my_input", [](const std::string& text) {
    // Handle text change
});
```

### Progress Bars & Sliders
```cpp
ui.createProgressBar("progress", {x, y, width, height}, 0.0f, 1.0f);
ui.setValue("progress", 0.75f); // 75%

ui.createSlider("volume", {x, y, width, height}, 0.0f, 100.0f);
ui.setOnValueChanged("volume", [](float value) {
    // Handle value change
});
```

### Lists & Event Logs
```cpp
ui.createList("my_list", {x, y, width, height});
ui.addListItem("my_list", "Item 1");
ui.setListMaxItems("my_list", 10); // Auto-scroll when exceeded

ui.createEventLog("events", {x, y, width, height}, 20);
ui.addEventLogEntry("events", "System started");
ui.enableEventLogAutoUpdate("events", 2.0f); // Demo updates every 2 seconds
```

### Checkboxes, Images & Dialogs
```cpp
ui.createCheckbox("option", {x, y, width, height}, "Enable Feature");
ui.setChecked("option", true);

ui.createImage("logo", {x, y, width, height}, "texture_id");
ui.createPanel("background", {x, y, width, height}); // Background/container
ui.createDialog("dialog", {x, y, width, height}); // Modal dialog backgrounds
```

### Professional Theming System

### Automatic Theme Styling & Z-Order
```cpp
// Components automatically use professional themes and z-order - no manual management needed!
ui.setThemeMode("light");  // Professional light theme
ui.setThemeMode("dark");   // Professional dark theme (default)

// All components get consistent:
// - Professional colors and contrast (theme-appropriate text colors)
// - Automatic z-order layering (dialogs: -10, panels: 0, buttons: 10, labels: 20)
// - Enhanced mouse accuracy (36px list items)
// - Proper hover/pressed states
// - Optimized typography
// - Automatic theme refresh when themes change mid-state
```

### Modal Creation & Overlays
```cpp
// Simplified modal creation - combines theme + overlay + dialog in one call
int dialogX = (windowWidth - 400) / 2;
int dialogY = (windowHeight - 200) / 2;
ui.createModal("dialog_id", {dialogX, dialogY, 400, 200}, "dark", windowWidth, windowHeight);

// Automatically handles:
// - Theme switching and refreshing existing components
// - Overlay creation for background dimming
// - Dialog creation with proper z-order
// - All components get theme-appropriate styling

// For HUD elements (no overlay - game remains visible)
ui.createProgressBar("health_bar", {10, 10, 200, 20});
```

### Custom Styling (When Needed)
```cpp
// Only customize for special cases - 98% of components use theme defaults
UIStyle customStyle;
customStyle.textColor = {255, 215, 0, 255}; // Gold
customStyle.fontSize = 32;
customStyle.textAlign = UIAlignment::CENTER_CENTER;
ui.setStyle("special_title", customStyle);
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
ui.updateLayout("my_layout"); // Repositions components
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

## Component Management

### Efficient Cleanup
```cpp
// Clean up by prefix (recommended)
ui.removeComponentsWithPrefix("menustate_");

// Individual removal
ui.removeComponent("specific_id");

// Nuclear cleanup (preserves theme background)
ui.clearAllComponents();

// Theme cleanup
ui.removeThemeBackground();
ui.resetToDefaultTheme();
```

### Visibility & State Control
```cpp
ui.setComponentVisible("my_component", false); // Hide temporarily
ui.setComponentEnabled("my_component", false);  // Disable interaction
ui.setComponentBounds("my_component", newBounds);

// Z-order is automatic by component type:
// DIALOG: -10, PANEL: 0, IMAGE: 1, PROGRESS_BAR: 5, EVENT_LOG: 6, LIST: 8
// BUTTON: 10, SLIDER: 12, CHECKBOX: 13, INPUT_FIELD: 15, LABEL: 20, TITLE: 25
// Override only if needed (rarely required):
ui.setComponentZOrder("my_component", 50);
```

## Best Practices

### GameState Integration
1. **Always call UIManager update and render** in states that use UI
2. **Clean up components** when exiting states
3. **Use meaningful IDs** with state prefixes: `"menustate_play_btn"`
4. **Call resetToDefaultTheme()** when exiting to prevent theme contamination

### Performance Tips
- Use `setComponentVisible()` instead of remove/recreate for temporary hiding
- Minimize component creation/destruction in update loops
- Use `removeComponentsWithPrefix()` for efficient bulk cleanup
- Cache component references when accessing frequently
- Automatic z-order eliminates manual layering management overhead
- Theme refresh is automatic when using `createModal()` or `setThemeMode()`

### Component Naming Convention
```cpp
// Recommended ID patterns
ui.createButton("mainmenu_play_btn", bounds, "Play");
ui.createButton("options_volume_slider", bounds);
ui.createButton("hud_health_bar", bounds);
```

### Error Handling
```cpp
// Always check for shutdown state
auto& ui = UIManager::Instance();
if (!ui.isShutdown()) {
    ui.update(deltaTime);
}

// Verify components exist before accessing
if (ui.hasComponent("my_component")) {
    ui.setText("my_component", "New Text");
}
```

## Integration Patterns

### Full-Screen Menu State
```cpp
class MainMenuState : public GameState {
    bool enter() override {
        auto& ui = UIManager::Instance();
        auto& gameEngine = GameEngine::Instance();
        
        // Create menu components with automatic z-order
        ui.createTitle("mainmenu_title", {0, 100, 800, 80}, "My Game");
        ui.createButton("mainmenu_play", {300, 250, 200, 50}, "Play Game");
        ui.createButton("mainmenu_quit", {300, 320, 200, 50}, "Quit");
        
        // Set up callbacks
        ui.setOnClick("mainmenu_play", [this]() {
            gameStateManager->setState("GamePlayState");
        });
        
        return true;
    }
    
    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("mainmenu_");
        return true;
    }
};
```

### HUD/Overlay State
```cpp
class PlayerHUDState : public EntityState {
    void enter() override {
        auto& ui = UIManager::Instance();
        
        // Create HUD elements (no theme background)
        ui.createProgressBar("hud_health", {10, 10, 200, 20}, 0.0f, 100.0f);
        ui.createLabel("hud_score", {10, 40, 150, 20}, "Score: 0");
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

## Debugging & Troubleshooting

### Debug Mode
```cpp
ui.setDebugMode(true);
ui.drawDebugBounds(true); // Shows red component boundaries
```

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

### Global Settings
```cpp
ui.setGlobalFont("my_font_id");
ui.setGlobalScale(1.5f);        // 150% scale
ui.enableTooltips(true);
ui.setTooltipDelay(1.0f);       // 1 second delay
```

## Complete Example: Options Menu

```cpp
class OptionsMenuState : public GameState {
public:
    bool enter() override {
        auto& ui = UIManager::Instance();
        auto& gameEngine = GameEngine::Instance();
        int windowWidth = gameEngine.getWindowWidth();
        int windowHeight = gameEngine.getWindowHeight();
        
        // Create modal with centered positioning
        int dialogX = (windowWidth - 400) / 2;
        int dialogY = (windowHeight - 200) / 2;
        
        ui.createModal("dialog", {dialogX, dialogY, 400, 200}, "dark", windowWidth, windowHeight);
        ui.createLabel("dialog_title", {dialogX + 20, dialogY + 20, 360, 30}, "Confirm Action");
        ui.createLabel("dialog_text", {dialogX + 20, dialogY + 60, 360, 40}, "Are you sure you want to quit?");
        ui.createButton("dialog_yes", {dialogX + 50, dialogY + 120, 100, 40}, "Yes");
        ui.createButton("dialog_cancel", {dialogX + 250, dialogY + 120, 100, 40}, "Cancel");
        
        // Set up callbacks
        ui.setOnClick("dialog_yes", []() {
            GameEngine::Instance().setRunning(false);
        });
        
        ui.setOnClick("dialog_cancel", []() {
            // Close dialog logic here
        });
        
        return true;
    }
</invoke>
    
    void update(float deltaTime) override {
        auto& ui = UIManager::Instance();
        if (!ui.isShutdown()) {
            ui.update(deltaTime);
        }
    }
    
    void render() override {
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("dialog_");
        ui.removeOverlay(); // Clean up modal overlay
        return true;
    }
};
```

This UIManager system provides everything needed for sophisticated game UIs while maintaining simplicity and professional appearance out-of-the-box.