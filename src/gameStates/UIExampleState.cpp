/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/UIExampleState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

// UIExampleState Implementation
UIExampleState::UIExampleState() {
    m_uiScreen = std::make_unique<UIExampleScreen>();
}

bool UIExampleState::enter() {
    std::cout << "Entering UI Example State\n";
    
    // Initialize and show the UI screen
    m_uiScreen->show();
    
    // Set up callbacks
    auto screen = static_cast<UIExampleScreen*>(m_uiScreen.get());
    screen->setOnBack([]() {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->setState("MainMenuState");
    });
    
    screen->setOnSliderChanged([this](float value) {
        handleSliderChange(value);
    });
    
    screen->setOnCheckboxToggled([this]() {
        handleCheckboxToggle();
    });
    
    screen->setOnInputChanged([this](const std::string& text) {
        handleInputChange(text);
    });
    
    screen->setOnListSelected([this]() {
        handleListSelection();
    });
    
    screen->setOnAnimate([this]() {
        handleAnimation();
    });
    
    screen->setOnThemeChange([this]() {
        handleThemeChange();
    });
    
    return true;
}

void UIExampleState::update(float deltaTime) {
    // Update UI Manager - each state that uses UI is responsible for updating it
    // This architectural pattern ensures:
    // 1. UI is only updated when needed (performance optimization)
    // 2. States have full control over their UI lifecycle
    // 3. No global UI updates that might interfere with state-specific behavior
    // 4. Clean separation of concerns between engine and game logic
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    if (m_uiScreen) {
        m_uiScreen->update(deltaTime);
    }
    
    // Update progress bar animation
    updateProgressBar(deltaTime);
    
    // Handle B key to go back
    auto& inputManager = InputManager::Instance();
    if (inputManager.isKeyDown(SDL_SCANCODE_B)) {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->setState("MainMenuState");
    }
}

void UIExampleState::render() {
    // Render UI components through UIManager
    // Each state that uses UI is responsible for rendering its own UI components
    // This ensures proper render order and state-specific UI management
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
}

bool UIExampleState::exit() {
    std::cout << "Exiting UI Example State\n";
    
    if (m_uiScreen) {
        m_uiScreen->hide();
    }
    
    return true;
}

void UIExampleState::handleSliderChange(float value) {
    m_sliderValue = value;
    auto screen = static_cast<UIExampleScreen*>(m_uiScreen.get());
    screen->updateSliderLabel(value);
    std::cout << "Slider value changed: " << value << "\n";
}

void UIExampleState::handleCheckboxToggle() {
    m_checkboxValue = !m_checkboxValue;
    std::cout << "Checkbox toggled: " << (m_checkboxValue ? "checked" : "unchecked") << "\n";
}

void UIExampleState::handleInputChange(const std::string& text) {
    m_inputText = text;
    auto screen = static_cast<UIExampleScreen*>(m_uiScreen.get());
    screen->updateInputLabel(text);
    std::cout << "Input text changed: " << text << "\n";
}

void UIExampleState::handleListSelection() {
    auto& ui = UIManager::Instance();
    m_selectedListItem = ui.getSelectedListItem(LIST_COMPONENT);
    std::cout << "List item selected: " << m_selectedListItem << "\n";
}

void UIExampleState::handleAnimation() {
    auto& ui = UIManager::Instance();
    
    // Animate the animation button
    UIRect currentBounds = ui.getBounds(ANIMATION_BUTTON);
    UIRect targetBounds = currentBounds;
    targetBounds.x += 50;
    
    ui.animateMove(ANIMATION_BUTTON, targetBounds, 0.5f, [&ui, currentBounds]() {
        // Animate back to original position
        ui.animateMove(ANIMATION_BUTTON, currentBounds, 0.5f);
    });
    
    std::cout << "Animation triggered\n";
}

void UIExampleState::handleThemeChange() {
    m_darkTheme = !m_darkTheme;
    auto screen = static_cast<UIExampleScreen*>(m_uiScreen.get());
    screen->applyDarkTheme(m_darkTheme);
    std::cout << "Theme changed to: " << (m_darkTheme ? "dark" : "light") << "\n";
}

void UIExampleState::updateProgressBar(float deltaTime) {
    // Animate progress bar automatically
    if (m_progressIncreasing) {
        m_progressValue += deltaTime * 0.3f; // 30% per second
        if (m_progressValue >= 1.0f) {
            m_progressValue = 1.0f;
            m_progressIncreasing = false;
        }
    } else {
        m_progressValue -= deltaTime * 0.3f;
        if (m_progressValue <= 0.0f) {
            m_progressValue = 0.0f;
            m_progressIncreasing = true;
        }
    }
    
    auto screen = static_cast<UIExampleScreen*>(m_uiScreen.get());
    screen->updateProgressBar(m_progressValue);
}

// UIExampleScreen Implementation
UIExampleScreen::UIExampleScreen() : UIScreen("UIExampleScreen") {
}

void UIExampleScreen::create() {
    setupComponents();
    setupLayout();
    setupStyling();
    populateList();
}

void UIExampleScreen::update(float deltaTime) {
    UIScreen::update(deltaTime);
}

void UIExampleScreen::onButtonClicked(const std::string& buttonID) {
    if (buttonID == "back_btn" && m_onBack) {
        m_onBack();
    } else if (buttonID == "animate_btn" && m_onAnimate) {
        m_onAnimate();
    } else if (buttonID == "theme_btn" && m_onThemeChange) {
        m_onThemeChange();
    } else if (buttonID == "demo_checkbox" && m_onCheckboxToggled) {
        m_onCheckboxToggled();
    } else if (buttonID == "demo_list" && m_onListSelected) {
        m_onListSelected();
    }
}

void UIExampleScreen::onValueChanged(const std::string& componentID, float value) {
    if (componentID == "demo_slider" && m_onSliderChanged) {
        m_onSliderChanged(value);
    }
}

void UIExampleScreen::onTextChanged(const std::string& componentID, const std::string& text) {
    if (componentID == "demo_input" && m_onInputChanged) {
        m_onInputChanged(text);
    }
}

void UIExampleScreen::setupComponents() {
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();
    
    // Background panel now handled by theme system
    // createPanel("main_panel", {0, 0, windowWidth, windowHeight});
    
    // Title
    createLabel("title_label", {0, 30, windowWidth, 40}, "UIManager Feature Demo");
    
    // Back button and instruction
    createButton("back_btn", {50, windowHeight - 80, 120, 40}, "Back");
    createLabel("back_instruction", {180, windowHeight - 75, 200, 30}, "Press B to go back");
    
    // Slider demo
    auto& ui = getUIManager();
    ui.createSlider("demo_slider", {50, 120, 200, 30}, 0.0f, 1.0f);
    ui.setValue("demo_slider", 0.5f);
    createLabel("slider_label", {260, 120, 200, 30}, "Slider: 0.50");
    
    // Checkbox demo
    ui.createCheckbox("demo_checkbox", {50, 170, 250, 30}, "Toggle Option");
    
    // Input field demo
    ui.createInputField("demo_input", {50, 220, 200, 30}, "Type here...");
    createLabel("input_label", {260, 220, 300, 30}, "Input: (empty)");
    
    // Progress bar demo
    ui.createProgressBar("demo_progress", {50, 270, 200, 20}, 0.0f, 1.0f);
    createLabel("progress_label", {260, 270, 200, 20}, "Auto Progress");
    
    // List demo
    ui.createList("demo_list", {50, 320, 200, 180});
    
    // Animation button
    createButton("animate_btn", {300, 320, 120, 40}, "Animate");
    
    // Theme toggle button
    createButton("theme_btn", {300, 380, 120, 40}, "Dark Theme");
    
    // Instructions
    createLabel("instructions", {450, 320, 300, 100}, 
                "Controls:\n- Click buttons and UI elements\n- Type in input field\n- Select list items\n- B key to go back");
    
    // Add all components to tracking
    // addComponent("main_panel"); // Now handled by theme system
    addComponent("title_label");
    addComponent("back_btn");
    addComponent("back_instruction");
    addComponent("demo_slider");
    addComponent("slider_label");
    addComponent("demo_checkbox");
    addComponent("demo_input");
    addComponent("input_label");
    addComponent("demo_progress");
    addComponent("progress_label");
    addComponent("demo_list");
    addComponent("animate_btn");
    addComponent("theme_btn");
    addComponent("instructions");
}

void UIExampleScreen::setupLayout() {
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    
    // Center the title
    centerComponent("title_label", windowWidth, 100);
}

void UIExampleScreen::setupStyling() {
    auto& ui = getUIManager();
    
    // Create theme background with automatic styling
    auto& gameEngine = GameEngine::Instance();
    ui.createThemeBackground(gameEngine.getWindowWidth(), gameEngine.getWindowHeight());
    
    // Only customize the title style - everything else uses theme defaults
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0};
    titleStyle.textColor = {255, 245, 120, 255}; // Special gold color for the title
    titleStyle.fontSize = 28;
    titleStyle.textAlign = UIAlignment::CENTER_CENTER;
    titleStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("title_label", titleStyle);
    
    // All other components automatically use the current theme (light theme by default)
    // No manual styling needed - UIManager handles everything!
}

void UIExampleScreen::populateList() {
    auto& ui = getUIManager();
    
    ui.addListItem("demo_list", "Option 1: Basic Item");
    ui.addListItem("demo_list", "Option 2: Second Item");
    ui.addListItem("demo_list", "Option 3: Third Item");
    ui.addListItem("demo_list", "Option 4: Fourth Item");
    ui.addListItem("demo_list", "Option 5: Fifth Item");
}

void UIExampleScreen::updateSliderLabel(float value) {
    auto& ui = getUIManager();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "Slider: " << value;
    ui.setText("slider_label", oss.str());
}

void UIExampleScreen::updateProgressBar(float value) {
    auto& ui = getUIManager();
    ui.setValue("demo_progress", value);
}

void UIExampleScreen::updateInputLabel(const std::string& text) {
    auto& ui = getUIManager();
    std::string labelText = "Input: " + (text.empty() ? "(empty)" : text);
    ui.setText("input_label", labelText);
}

void UIExampleScreen::applyDarkTheme(bool dark) {
    auto& ui = getUIManager();
    
    if (dark) {
        // Use centralized dark theme
        ui.setThemeMode("dark");
        ui.setText("theme_btn", "Light Theme");
        
        // Reapply custom title styling for dark theme
        UIStyle titleStyle;
        titleStyle.backgroundColor = {0, 0, 0, 0};
        titleStyle.textColor = {255, 245, 120, 255}; // Keep gold title
        titleStyle.fontSize = 28;
        titleStyle.textAlign = UIAlignment::CENTER_CENTER;
        titleStyle.fontID = "fonts_UI_Arial";
        ui.setStyle("title_label", titleStyle);
    } else {
        // Use centralized light theme
        ui.setThemeMode("light");
        ui.setText("theme_btn", "Dark Theme");
        
        // Reapply custom title styling for light theme
        UIStyle titleStyle;
        titleStyle.backgroundColor = {0, 0, 0, 0};
        titleStyle.textColor = {255, 245, 120, 255}; // Keep gold title
        titleStyle.fontSize = 28;
        titleStyle.textAlign = UIAlignment::CENTER_CENTER;
        titleStyle.fontID = "fonts_UI_Arial";
        ui.setStyle("title_label", titleStyle);
    }
}