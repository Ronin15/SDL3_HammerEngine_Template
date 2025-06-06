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
    // Pure UIManager approach - no UIScreen
}

bool UIExampleState::enter() {
    std::cout << "Entering UI Example State\n";
    
    // Create UI components directly with UIManager
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Create theme background
    ui.createThemeBackground(windowWidth, windowHeight);
    
    // Title
    ui.createTitle("title_label", {0, 30, windowWidth, 40}, "UIManager Feature Demo");
    ui.setTitleAlignment("title_label", UIAlignment::CENTER_CENTER);
    
    // Back button and instruction
    ui.createButton("back_btn", {50, windowHeight - 80, 120, 40}, "Back");
    ui.createLabel("back_instruction", {180, windowHeight - 75, 200, 30}, "Press B to go back");
    
    // Slider demo
    ui.createSlider("demo_slider", {50, 120, 200, 30}, 0.0f, 1.0f);
    ui.setValue("demo_slider", 0.5f);
    ui.createLabel("slider_label", {260, 120, 200, 30}, "Slider: 0.50");
    
    // Checkbox demo
    ui.createCheckbox("demo_checkbox", {50, 170, 250, 30}, "Toggle Option");
    
    // Input field demo
    ui.createInputField("demo_input", {50, 220, 200, 30}, "Type here...");
    ui.createLabel("input_label", {260, 220, 300, 30}, "Input: (empty)");
    
    // Progress bar demo
    ui.createProgressBar("demo_progress", {50, 270, 200, 20}, 0.0f, 1.0f);
    ui.createLabel("progress_label", {260, 270, 200, 20}, "Auto Progress");
    
    // List demo
    ui.createList("demo_list", {50, 320, 200, 180});
    
    // Event Log demo
    ui.createEventLog("demo_event_log", {300, 450, 400, 180}, 6);
    ui.createLabel("event_log_label", {300, 430, 200, 20}, "Event Log (Auto-updating):");
    ui.setupDemoEventLog("demo_event_log");
    
    // Animation button
    ui.createButton("animate_btn", {300, 320, 120, 40}, "Animate");
    
    // Theme toggle button
    ui.createButton("theme_btn", {300, 380, 120, 40}, "Dark Theme");
    
    // Instructions
    ui.createLabel("instructions", {450, 320, 300, 100}, 
                   "Controls:\n- Click buttons and UI elements\n- Type in input field\n- Select list items\n- B key to go back");

    // Populate list
    ui.addListItem("demo_list", "Option 1: Basic Item");
    ui.addListItem("demo_list", "Option 2: Second Item");
    ui.addListItem("demo_list", "Option 3: Third Item");
    ui.addListItem("demo_list", "Option 4: Fourth Item");
    ui.addListItem("demo_list", "Option 5: Fifth Item");

    // Set up button callbacks
    ui.setOnClick("back_btn", []() {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->setState("MainMenuState");
    });

    ui.setOnClick("animate_btn", [this]() {
        handleAnimation();
    });

    ui.setOnClick("theme_btn", [this]() {
        handleThemeChange();
    });

    ui.setOnClick("demo_checkbox", [this]() {
        handleCheckboxToggle();
    });

    ui.setOnClick("demo_list", [this]() {
        handleListSelection();
    });

    ui.setOnValueChanged("demo_slider", [this](float value) {
        handleSliderChange(value);
    });

    ui.setOnTextChanged("demo_input", [this](const std::string& text) {
        handleInputChange(text);
    });
    
    return true;
}

void UIExampleState::update(float deltaTime) {
    // Update UI Manager
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    // Update progress bar animation
    updateProgressBar(deltaTime);
    
    // Handle B key to go back
    auto& inputManager = InputManager::Instance();
    if (inputManager.wasKeyPressed(SDL_SCANCODE_B)) {
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
    
    // Clean up all UI components
    auto& ui = UIManager::Instance();
    ui.removeComponent("title_label");
    ui.removeComponent("back_btn");
    ui.removeComponent("back_instruction");
    ui.removeComponent("demo_slider");
    ui.removeComponent("slider_label");
    ui.removeComponent("demo_checkbox");
    ui.removeComponent("demo_input");
    ui.removeComponent("input_label");
    ui.removeComponent("demo_progress");
    ui.removeComponent("progress_label");
    ui.removeComponent("demo_list");
    ui.removeComponent("demo_event_log");
    ui.removeComponent("event_log_label");
    ui.removeComponent("animate_btn");
    ui.removeComponent("theme_btn");
    ui.removeComponent("instructions");
    ui.removeThemeBackground();
    
    // Reset theme to prevent contamination of other states
    ui.resetToDefaultTheme();
    
    return true;
}

void UIExampleState::handleSliderChange(float value) {
    m_sliderValue = value;
    updateSliderLabel(value);
    std::cout << "Slider value changed: " << value << "\n";
}

void UIExampleState::handleCheckboxToggle() {
    m_checkboxValue = !m_checkboxValue;
    std::cout << "Checkbox toggled: " << (m_checkboxValue ? "checked" : "unchecked") << "\n";
}

void UIExampleState::handleInputChange(const std::string& text) {
    m_inputText = text;
    updateInputLabel(text);
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
    applyDarkTheme(m_darkTheme);
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
    
    auto& ui = UIManager::Instance();
    ui.setValue("demo_progress", m_progressValue);
}



void UIExampleState::updateSliderLabel(float value) {
    auto& ui = UIManager::Instance();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "Slider: " << value;
    ui.setText("slider_label", oss.str());
}

void UIExampleState::updateInputLabel(const std::string& text) {
    auto& ui = UIManager::Instance();
    std::string labelText = "Input: " + (text.empty() ? "(empty)" : text);
    ui.setText("input_label", labelText);
}

void UIExampleState::applyDarkTheme(bool dark) {
    auto& ui = UIManager::Instance();
    
    if (dark) {
        // Use centralized dark theme
        ui.setThemeMode("dark");
        ui.setText("theme_btn", "Light Theme");
    } else {
        // Use centralized light theme
        ui.setThemeMode("light");
        ui.setText("theme_btn", "Dark Theme");
    }
    
    // Title styling is handled automatically by UIManager's TITLE component type
}

// Pure UIManager implementation - no UIScreen needed