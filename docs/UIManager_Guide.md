# UIManager System Guide

## Overview

The UIManager is a comprehensive UI system for SDL3 games that provides reusable building blocks for creating user interfaces in GameStates and EntityStates. It follows a component-based architecture with support for layouts, animations, theming, and event handling.

## Architecture

### Core Components

- **UIManager**: Singleton that manages all UI components, rendering, and systems
- **UIScreen**: Base class for GameStates to create reusable UI screens
- **UIComponent**: Individual UI elements (buttons, labels, panels, etc.)
- **UILayout**: Layout managers for automatic component arrangement
- **UITheme**: Styling and theming system

### Design Philosophy

- **State-Driven**: GameStates and EntityStates use UIManager to compose their specific UI needs
- **Reusable Building Blocks**: All UI components can be reused across different states
- **Event-Driven**: Components communicate through callbacks and events
- **Performance-Oriented**: Efficient rendering and memory management

## Quick Start

### 1. Basic Component Creation

```cpp
// In your GameState's enter() method
auto& ui = UIManager::Instance();

// Create theme background (for full-screen UI)
ui.createThemeBackground(windowWidth, windowHeight);

// Create components - they automatically use theme styling
ui.createButton("play_btn", {100, 100, 200, 50}, "Play Game");
ui.createLabel("title", {0, 50, 800, 60}, "My Game Title");

// No manual styling needed - UIManager handles everything!
```

### 2. Handle Button Clicks

```cpp
// Set up a callback
ui.setOnClick("play_btn", [this]() {
    // Switch to gameplay state
    gameStateManager->setState("GamePlayState");
});

// Or check in update loop
void MyState::update(float deltaTime) {
    if (ui.isButtonClicked("play_btn")) {
        // Handle button click
    }
}
```

### 3. Using UIScreen Base Class with Centralized Theming

```cpp
class MainMenuScreen : public UIScreen {
public:
    MainMenuScreen() : UIScreen("MainMenuScreen") {}
    
    void create() override {
        auto& ui = getUIManager();
        
        // Create theme background automatically
        auto& gameEngine = GameEngine::Instance();
        ui.createThemeBackground(gameEngine.getWindowWidth(), gameEngine.getWindowHeight());
        
        // Components automatically use theme styling
        createButton("play_btn", {300, 200, 200, 50}, "Play Game");
        createButton("quit_btn", {300, 270, 200, 50}, "Quit");
        
        // Optional: Switch themes easily
        // ui.setThemeMode("dark"); // or "light"
    }
    
    void onButtonClicked(const std::string& buttonID) override {
        if (buttonID == "play_btn") {
            if (m_onPlayGame) m_onPlayGame();
        }
    }
    
    void setOnPlayGame(std::function<void()> callback) { 
        m_onPlayGame = callback; 
    }
    
private:
    std::function<void()> m_onPlayGame;
};
```

## Component Types

### Buttons
```cpp
ui.createButton("my_btn", {x, y, width, height}, "Button Text");
ui.setOnClick("my_btn", []() { /* callback */ });

// Check state
bool clicked = ui.isButtonClicked("my_btn");
bool hovered = ui.isButtonHovered("my_btn");
bool pressed = ui.isButtonPressed("my_btn");
```

### Labels
```cpp
ui.createLabel("my_label", {x, y, width, height}, "Label Text");
ui.setText("my_label", "New Text");
std::string text = ui.getText("my_label");
```

### Input Fields
```cpp
ui.createInputField("my_input", {x, y, width, height}, "Placeholder");
ui.setInputFieldMaxLength("my_input", 50);
ui.setOnTextChanged("my_input", [](const std::string& text) {
    // Handle text change
});
```

### Sliders
```cpp
ui.createSlider("my_slider", {x, y, width, height}, 0.0f, 100.0f);
ui.setValue("my_slider", 50.0f);
ui.setOnValueChanged("my_slider", [](float value) {
    // Handle value change
});
```

### Progress Bars
```cpp
ui.createProgressBar("my_progress", {x, y, width, height}, 0.0f, 1.0f);
ui.updateProgressBar("my_progress", 0.75f); // 75%
```

### Checkboxes
```cpp
ui.createCheckbox("my_checkbox", {x, y, width, height}, "Option Text");
ui.setChecked("my_checkbox", true);
bool checked = ui.getChecked("my_checkbox");
```

### Lists
```cpp
ui.createList("my_list", {x, y, width, height});
ui.addListItem("my_list", "Item 1");
ui.addListItem("my_list", "Item 2");
int selected = ui.getSelectedListItem("my_list");
```

### Images
```cpp
ui.createImage("my_image", {x, y, width, height}, "texture_id");
ui.setTexture("my_image", "new_texture_id");
```

### Panels
```cpp
ui.createPanel("my_panel", {x, y, width, height});
// Panels are typically used as backgrounds or containers
```

## Layout System

### Layout Types

#### Absolute Layout
```cpp
ui.createLayout("absolute_layout", UILayoutType::ABSOLUTE, {0, 0, 800, 600});
// Components keep their manually set positions
```

#### Flow Layout
```cpp
ui.createLayout("flow_layout", UILayoutType::FLOW, {50, 50, 700, 500});
ui.setLayoutSpacing("flow_layout", 10);
// Components flow left-to-right, wrapping to next line
```

#### Grid Layout
```cpp
ui.createLayout("grid_layout", UILayoutType::GRID, {50, 50, 600, 400});
ui.setLayoutColumns("grid_layout", 3); // 3 columns
// Components arranged in a grid
```

#### Stack Layout
```cpp
ui.createLayout("stack_layout", UILayoutType::STACK, {100, 100, 300, 400});
ui.setLayoutSpacing("stack_layout", 15);
// Components stacked vertically
```

### Using Layouts
```cpp
// Add components to layout
ui.addComponentToLayout("my_layout", "button1");
ui.addComponentToLayout("my_layout", "button2");

// Update layout (repositions components)
ui.updateLayout("my_layout");
```

## Centralized Theme System

### Easy Theme Switching
```cpp
// Switch between built-in professional themes
ui.setThemeMode("light");  // Professional light theme
ui.setThemeMode("dark");   // Professional dark theme

// Check current theme
std::string currentTheme = ui.getCurrentThemeMode();
```

### Automatic Styling
```cpp
// Components automatically use current theme - no manual styling needed!
ui.createButton("my_btn", {x, y, w, h}, "Button Text");
ui.createList("my_list", {x, y, w, h});

// All components get:
// - Professional colors and contrast
// - Enhanced mouse accuracy (36px list items)
// - Consistent appearance across the app
```

### Custom Styling (When Needed)
```cpp
// Only customize when you need something special
UIStyle titleStyle;
titleStyle.textColor = {255, 215, 0, 255}; // Gold title
titleStyle.fontSize = 32;
ui.setStyle("title_label", titleStyle);

// Everything else uses theme defaults automatically
```

### Background/Overlay Management
```cpp
// For full-screen menus (overlays background)
ui.createThemeBackground(windowWidth, windowHeight);

// For HUD elements (no overlay - game remains visible)
// Simply don't call createThemeBackground()
ui.createProgressBar("health_bar", {10, 10, 200, 20});
```

### Global Settings
```cpp
// Set global font
ui.setGlobalFont("my_font_id");

// Set global scale
ui.setGlobalScale(1.5f); // 150% scale

// Enable/disable tooltips
ui.enableTooltips(true);
ui.setTooltipDelay(1.0f); // 1 second delay
```

## Animation System

### Position Animations
```cpp
UIRect targetBounds = {newX, newY, width, height};
ui.animateMove("my_component", targetBounds, 0.5f, []() {
    // Animation complete callback
    std::cout << "Animation finished!\n";
});
```

### Color Animations
```cpp
SDL_Color targetColor = {255, 0, 0, 255}; // Red
ui.animateColor("my_component", targetColor, 1.0f, []() {
    // Animation complete
});
```

### Animation Control
```cpp
// Stop animation
ui.stopAnimation("my_component");

// Check if animating
bool isAnimating = ui.isAnimating("my_component");
```

## Event Handling

### Callback-Based Events
```cpp
// Button clicks
ui.setOnClick("button_id", []() { /* handle click */ });

// Value changes (sliders, progress bars)
ui.setOnValueChanged("slider_id", [](float value) { /* handle change */ });

// Text changes (input fields)
ui.setOnTextChanged("input_id", [](const std::string& text) { /* handle text */ });

// Hover events
ui.setOnHover("component_id", []() { /* handle hover */ });

// Focus events
ui.setOnFocus("component_id", []() { /* handle focus */ });
```

### State-Based Event Checking
```cpp
void MyState::update(float deltaTime) {
    auto& ui = UIManager::Instance();
    
    // Check for clicks
    if (ui.isButtonClicked("my_button")) {
        // Handle click
    }
    
    // Check component states
    if (ui.isComponentFocused("my_input")) {
        // Input field has focus
    }
    
    if (ui.isButtonHovered("my_button")) {
        // Button is being hovered
    }
}
```

## Integration Patterns

### GameState Integration
```cpp
class MyGameState : public GameState {
private:
    std::unique_ptr<UIScreen> m_uiScreen;
    
public:
    bool enter() override {
        m_uiScreen = std::make_unique<MyUIScreen>();
        m_uiScreen->show();
        
        // Set up callbacks
        static_cast<MyUIScreen*>(m_uiScreen.get())->setOnButtonClick([this]() {
            handleButtonClick();
        });
        
        return true;
    }
    
    void update(float deltaTime) override {
        if (m_uiScreen) {
            m_uiScreen->update(deltaTime);
        }
    }
    
    void render() override {
        // IMPORTANT: GameState must call UIManager render
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
    bool exit() override {
        if (m_uiScreen) {
            m_uiScreen->hide();
        }
        return true;
    }
};
```

### EntityState UI (HUD Elements)
```cpp
class PlayerState : public EntityState {
public:
    void enter() override {
        auto& ui = UIManager::Instance();
        
        // Create HUD elements
        ui.createProgressBar("health_bar", {10, 10, 200, 20}, 0.0f, 100.0f);
        ui.createLabel("health_text", {10, 35, 100, 20}, "Health: 100");
        ui.createPanel("minimap", {650, 10, 140, 140});
    }
    
    void update(float deltaTime) override {
        auto& ui = UIManager::Instance();
        
        // Update HUD based on player state
        ui.setValue("health_bar", player->getHealth());
        ui.setText("health_text", "Health: " + std::to_string(player->getHealth()));
    }
    
    void exit() override {
        auto& ui = UIManager::Instance();
        ui.removeComponent("health_bar");
        ui.removeComponent("health_text");
        ui.removeComponent("minimap");
    }
};
```

## Best Practices

### Component Management
- Use `ui.removeComponentsWithPrefix("statename_")` for easy cleanup
- Use meaningful, unique IDs with prefixes to avoid conflicts
- Call `ui.resetToDefaultTheme()` when exiting states to prevent theme contamination

### Performance
- Minimize component creation/destruction in update loops
- Use `setComponentVisible()` instead of removing/recreating for temporary hiding
- Batch style changes when possible

### Rendering
- **CRITICAL**: Always call `UIManager::render(renderer)` in your GameState's `render()` method
- UIManager does NOT render automatically - it must be called by each state
- Call UIManager render after any background/game content rendering for proper layering
- **Renderer State**: UIManager automatically saves and restores renderer draw color to avoid interfering with GameState rendering

### Maintainability
- Use UIScreen base class for complex UI layouts
- Define component IDs as constants
- Separate UI logic from game logic using callbacks

### Responsive Design
- Use layouts for automatic positioning
- Get window dimensions from GameEngine for responsive layouts
- Consider different screen sizes when positioning components

### Error Handling
- Always check if components exist before accessing them
- Handle missing textures gracefully in image components
- Validate input ranges for sliders and progress bars

## Debugging

### Debug Mode
```cpp
ui.setDebugMode(true);
ui.drawDebugBounds(true);
// Shows component boundaries in red
```

### Common Issues
1. **Components not visible**: Check if they're added to the screen's component list
2. **Events not firing**: Ensure callbacks are set and components are enabled
3. **Layout issues**: Verify layout bounds and spacing settings
4. **Rendering order**: Use z-order to control which components appear on top

## Example: Complete Menu Implementation

```cpp
// MenuScreen.hpp
class MenuScreen : public UIScreen {
public:
    MenuScreen();
    void create() override;
    void onButtonClicked(const std::string& buttonID) override;
    
    void setOnPlay(std::function<void()> callback) { m_onPlay = callback; }
    void setOnOptions(std::function<void()> callback) { m_onOptions = callback; }
    void setOnQuit(std::function<void()> callback) { m_onQuit = callback; }

private:
    std::function<void()> m_onPlay, m_onOptions, m_onQuit;
};

// MenuScreen.cpp
### Modern Styling with Centralized Themes
```cpp
void MenuScreen::create() {
    auto& gameEngine = GameEngine::Instance();
    int width = gameEngine.getWindowWidth();
    int height = gameEngine.getWindowHeight();
    
    // Create theme background automatically
    auto& ui = getUIManager();
    ui.createThemeBackground(width, height);
    
    // Components automatically use professional theme styling
    createLabel("title", {0, 100, width, 80}, "My Amazing Game");
    createButton("play_btn", {width/2 - 100, 250, 200, 50}, "Play Game");
    createButton("options_btn", {width/2 - 100, 320, 200, 50}, "Options");
    createButton("quit_btn", {width/2 - 100, 390, 200, 50}, "Quit");
    
    // Only customize what needs to be special
    UIStyle titleStyle;
    titleStyle.textColor = {255, 215, 0, 255}; // Gold
    titleStyle.fontSize = 32;
    titleStyle.textAlign = UIAlignment::CENTER_CENTER;
    ui.setStyle("title", titleStyle);
    
    // All buttons automatically get professional theme styling!
    // 98% less code, 100% more consistent appearance
}

void MenuScreen::onButtonClicked(const std::string& buttonID) {
    if (buttonID == "play_btn" && m_onPlay) m_onPlay();
    else if (buttonID == "options_btn" && m_onOptions) m_onOptions();
    else if (buttonID == "quit_btn" && m_onQuit) m_onQuit();
}

// In your GameState that uses this screen:
void MyMenuState::render() override {
    // Render background/game content first
    // ... your background rendering code ...
    
    // Then render UI on top (UIManager preserves renderer state)
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
}
```

This UIManager system provides a complete, reusable solution for creating sophisticated user interfaces in your SDL3 game while maintaining clean separation between UI logic and game logic.