# UIManager System Guide

## Overview

The UIManager is a comprehensive UI system for SDL3 games that provides reusable UI components with professional theming, animations, layouts, and event handling. It follows a component-based architecture designed for use in GameStates and EntityStates.

## Quick Start

### Basic Usage

```cpp
// In your GameState's enter() method
auto& ui = UIManager::Instance();

// Create theme background for full-screen UI
ui.createThemeBackground(windowWidth, windowHeight);

// Create components with automatic professional styling
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
        ui.removeThemeBackground();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

## Component Types & Usage

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
ui.updateProgressBar("progress", 0.75f); // 75%

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

### Checkboxes & Images
```cpp
ui.createCheckbox("option", {x, y, width, height}, "Enable Feature");
ui.setChecked("option", true);

ui.createImage("logo", {x, y, width, height}, "texture_id");
ui.createPanel("background", {x, y, width, height}); // Background/container
```

## Professional Theming System

### Automatic Theme Styling
```cpp
// Components automatically use professional themes - no manual styling needed!
ui.setThemeMode("light");  // Professional light theme
ui.setThemeMode("dark");   // Professional dark theme (default)

// All components get consistent:
// - Professional colors and contrast
// - Enhanced mouse accuracy (36px list items)
// - Proper hover/pressed states
// - Optimized typography
```

### Background Management
```cpp
// For full-screen menus (adds theme background overlay)
ui.createThemeBackground(windowWidth, windowHeight);

// For HUD elements (no overlay - game remains visible)
// Simply don't call createThemeBackground()
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
ui.setComponentZOrder("my_component", 10);      // Rendering order
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
        
        // Create theme background
        ui.createThemeBackground(gameEngine.getWindowWidth(), gameEngine.getWindowHeight());
        
        // Create menu components
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
        ui.removeThemeBackground();
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
        
        // Create theme background
        ui.createThemeBackground(gameEngine.getWindowWidth(), gameEngine.getWindowHeight());
        
        // Create title and controls
        ui.createTitle("options_title", {0, 50, 800, 60}, "Options");
        
        ui.createLabel("options_volume_label", {200, 150, 150, 30}, "Volume:");
        ui.createSlider("options_volume_slider", {360, 150, 200, 30}, 0.0f, 100.0f);
        
        ui.createCheckbox("options_fullscreen", {200, 200, 300, 30}, "Fullscreen");
        ui.createCheckbox("options_vsync", {200, 240, 300, 30}, "V-Sync");
        
        ui.createButton("options_back", {300, 350, 200, 50}, "Back");
        
        // Set up callbacks
        ui.setOnClick("options_back", [this]() {
            gameStateManager->setState("MainMenuState");
        });
        
        ui.setOnValueChanged("options_volume_slider", [](float value) {
            // Update game volume
            AudioManager::Instance().setMasterVolume(value / 100.0f);
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
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponentsWithPrefix("options_");
        ui.removeThemeBackground();
        ui.resetToDefaultTheme();
        return true;
    }
};
```

This UIManager system provides everything needed for sophisticated game UIs while maintaining simplicity and professional appearance out-of-the-box.