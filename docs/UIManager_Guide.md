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

// Create a button
ui.createButton("play_btn", {100, 100, 200, 50}, "Play Game");

// Create a label
ui.createLabel("title", {0, 50, 800, 60}, "My Game Title");

// Create a panel (background)
ui.createPanel("main_panel", {0, 0, 800, 600});
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

### 3. Using UIScreen Base Class

```cpp
class MainMenuScreen : public UIScreen {
public:
    MainMenuScreen() : UIScreen("MainMenuScreen") {}
    
    void create() override {
        createButton("play_btn", {300, 200, 200, 50}, "Play Game");
        createButton("quit_btn", {300, 270, 200, 50}, "Quit");
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

## Styling and Theming

### Basic Styling
```cpp
UIStyle buttonStyle;
buttonStyle.backgroundColor = {70, 130, 180, 255}; // Steel blue
buttonStyle.hoverColor = {100, 149, 237, 255};     // Cornflower blue
buttonStyle.pressedColor = {25, 25, 112, 255};     // Midnight blue
buttonStyle.textColor = {255, 255, 255, 255};      // White
buttonStyle.borderColor = {255, 255, 255, 255};    // White border
buttonStyle.borderWidth = 2;
buttonStyle.padding = 10;
buttonStyle.textAlign = UIAlignment::CENTER_CENTER;

ui.setStyle("my_button", buttonStyle);
```

### Theme System
```cpp
// Create a custom theme
UITheme darkTheme;
darkTheme.name = "dark";

// Set component styles for theme
UIStyle darkButton;
darkButton.backgroundColor = {50, 50, 60, 255};
darkButton.textColor = {255, 255, 255, 255};
darkTheme.componentStyles[UIComponentType::BUTTON] = darkButton;

// Apply theme
ui.loadTheme(darkTheme);

// Apply theme to specific component
ui.applyThemeToComponent("my_button", UIComponentType::BUTTON);
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
- Always remove components when exiting states to prevent memory leaks
- Use meaningful, unique IDs for components
- Group related components using consistent naming (e.g., "menu_play_btn", "menu_quit_btn")

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
void MenuScreen::create() {
    auto& gameEngine = GameEngine::Instance();
    int width = gameEngine.getWindowWidth();
    int height = gameEngine.getWindowHeight();
    
    // Background
    createPanel("bg_panel", {0, 0, width, height});
    
    // Title
    createLabel("title", {0, 100, width, 80}, "My Amazing Game");
    
    // Menu buttons
    createButton("play_btn", {width/2 - 100, 250, 200, 50}, "Play Game");
    createButton("options_btn", {width/2 - 100, 320, 200, 50}, "Options");
    createButton("quit_btn", {width/2 - 100, 390, 200, 50}, "Quit");
    
    // Style the components
    auto& ui = getUIManager();
    
    // Title styling
    UIStyle titleStyle;
    titleStyle.textColor = {255, 215, 0, 255}; // Gold
    titleStyle.fontSize = 32;
    titleStyle.textAlign = UIAlignment::CENTER_CENTER;
    ui.setStyle("title", titleStyle);
    
    // Button styling
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {70, 130, 180, 255};
    buttonStyle.hoverColor = {100, 149, 237, 255};
    buttonStyle.pressedColor = {25, 25, 112, 255};
    buttonStyle.textColor = {255, 255, 255, 255};
    buttonStyle.borderWidth = 2;
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
    
    ui.setStyle("play_btn", buttonStyle);
    ui.setStyle("options_btn", buttonStyle);
    ui.setStyle("quit_btn", buttonStyle);
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