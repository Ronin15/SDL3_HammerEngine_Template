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
    
    // Main background panel
    createPanel("main_panel", {0, 0, windowWidth, windowHeight});
    
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
    ui.createList("demo_list", {50, 320, 200, 120});
    
    // Animation button
    createButton("animate_btn", {300, 320, 120, 40}, "Animate");
    
    // Theme toggle button
    createButton("theme_btn", {300, 380, 120, 40}, "Dark Theme");
    
    // Instructions
    createLabel("instructions", {450, 320, 300, 100}, 
                "Controls:\n- Click buttons and UI elements\n- Type in input field\n- Select list items\n- B key to go back");
    
    // Add all components to tracking
    addComponent("main_panel");
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
    
    // Main panel style
    UIStyle panelStyle;
    panelStyle.backgroundColor = {30, 30, 40, 220};
    panelStyle.borderWidth = 0;
    panelStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("main_panel", panelStyle);
    
    // Title style
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0};
    titleStyle.textColor = {255, 245, 120, 255}; // Brighter gold for better contrast
    titleStyle.fontSize = 28;
    titleStyle.textAlign = UIAlignment::CENTER_CENTER;
    titleStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("title_label", titleStyle);
    
    // Button styles
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {60, 120, 180, 255};
    buttonStyle.hoverColor = {80, 140, 200, 255};
    buttonStyle.pressedColor = {40, 100, 160, 255};
    buttonStyle.borderColor = {255, 255, 255, 255};
    buttonStyle.textColor = {255, 255, 255, 255};
    buttonStyle.borderWidth = 1;
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
    buttonStyle.fontID = "fonts_UI_Arial";
    
    ui.setStyle("back_btn", buttonStyle);
    ui.setStyle("animate_btn", buttonStyle);
    ui.setStyle("theme_btn", buttonStyle);
    
    // Progress bar style
    UIStyle progressStyle;
    progressStyle.backgroundColor = {40, 40, 40, 255};
    progressStyle.borderColor = {100, 100, 100, 255};
    progressStyle.hoverColor = {0, 180, 0, 255}; // Green fill
    progressStyle.borderWidth = 1;
    progressStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("demo_progress", progressStyle);
    
    // Input field style
    UIStyle inputStyle;
    inputStyle.backgroundColor = {245, 245, 245, 255};
    inputStyle.textColor = {20, 20, 20, 255}; // Dark text for good contrast
    inputStyle.borderColor = {128, 128, 128, 255};
    inputStyle.hoverColor = {235, 245, 255, 255};
    inputStyle.borderWidth = 1;
    inputStyle.textAlign = UIAlignment::CENTER_LEFT;
    inputStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("demo_input", inputStyle);
    
    // List style
    UIStyle listStyle;
    listStyle.backgroundColor = {240, 240, 240, 255};
    listStyle.borderColor = {128, 128, 128, 255};
    listStyle.textColor = {20, 20, 20, 255}; // Dark text for good contrast on light background
    listStyle.hoverColor = {180, 200, 255, 255}; // Light blue selection
    listStyle.borderWidth = 1;
    listStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("demo_list", listStyle);
    
    // Label styles
    UIStyle labelStyle;
    labelStyle.backgroundColor = {0, 0, 0, 0};
    labelStyle.textColor = {220, 220, 220, 255}; // Light gray for better contrast
    labelStyle.textAlign = UIAlignment::CENTER_LEFT;
    labelStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("slider_label", labelStyle);
    ui.setStyle("input_label", labelStyle);
    ui.setStyle("progress_label", labelStyle);
    ui.setStyle("back_instruction", labelStyle);
    ui.setStyle("instructions", labelStyle);
    
    // Checkbox style
    UIStyle checkboxStyle = buttonStyle;
    checkboxStyle.backgroundColor = {180, 180, 180, 255};
    checkboxStyle.hoverColor = {200, 200, 200, 255};
    checkboxStyle.textColor = {220, 220, 220, 255}; // Light text for label on dark background
    checkboxStyle.textAlign = UIAlignment::CENTER_LEFT;
    checkboxStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("demo_checkbox", checkboxStyle);
    
    // Slider style
    UIStyle sliderStyle;
    sliderStyle.backgroundColor = {100, 100, 100, 255};
    sliderStyle.borderColor = {150, 150, 150, 255};
    sliderStyle.hoverColor = {60, 120, 180, 255}; // Blue handle
    sliderStyle.pressedColor = {40, 100, 160, 255};
    sliderStyle.borderWidth = 1;
    sliderStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("demo_slider", sliderStyle);
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
        // Dark theme
        UIStyle panelStyle;
        panelStyle.backgroundColor = {20, 20, 25, 240};
        panelStyle.fontID = "fonts_UI_Arial";
        ui.setStyle("main_panel", panelStyle);
        
        UIStyle buttonStyle;
        buttonStyle.backgroundColor = {50, 50, 60, 255};
        buttonStyle.hoverColor = {70, 70, 80, 255};
        buttonStyle.pressedColor = {30, 30, 40, 255};
        buttonStyle.borderColor = {150, 150, 150, 255};
        buttonStyle.textColor = {255, 255, 255, 255};
        buttonStyle.borderWidth = 1;
        buttonStyle.textAlign = UIAlignment::CENTER_CENTER;
        buttonStyle.fontID = "fonts_UI_Arial";
        
        ui.setStyle("back_btn", buttonStyle);
        ui.setStyle("animate_btn", buttonStyle);
        ui.setStyle("theme_btn", buttonStyle);
        
        ui.setText("theme_btn", "Light Theme");
    } else {
        // Light theme - restore original styles
        setupStyling();
        ui.setText("theme_btn", "Dark Theme");
    }
}