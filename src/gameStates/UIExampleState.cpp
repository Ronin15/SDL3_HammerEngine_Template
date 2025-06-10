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

    // Create overlay background
    ui.createOverlay(windowWidth, windowHeight);
    
    // Title
    ui.createTitle("uiexample_title_label", {0, 30, windowWidth, 40}, "UIManager Feature Demo");
    ui.setTitleAlignment("uiexample_title_label", UIAlignment::CENTER_CENTER);
    
    // Back button and instruction
    ui.createButtonDanger("uiexample_back_btn", {50, windowHeight - 80, 120, 40}, "Back");
    ui.createLabel("uiexample_back_instruction", {180, windowHeight - 75, 200, 30}, "Press B to go back");
    
    // Slider demo
    ui.createSlider("uiexample_demo_slider", {50, 140, 200, 30}, 0.0f, 1.0f);
    ui.setValue("uiexample_demo_slider", 0.5f);
    ui.createLabel("uiexample_slider_label", {260, 140, 200, 30}, "Slider: 0.50");
    
    // Checkbox demo
    ui.createCheckbox("uiexample_demo_checkbox", {50, 190, 250, 30}, "Toggle Option");
    
    // Input field demo
    ui.createInputField("uiexample_demo_input", {50, 240, 200, 30}, "Type here...");
    ui.createLabel("uiexample_input_label", {260, 240, 300, 30}, "Input: (empty)");
    
    // Progress bar demo
    ui.createProgressBar("uiexample_demo_progress", {50, 290, 200, 20}, 0.0f, 1.0f);
    ui.createLabel("uiexample_progress_label", {260, 290, 200, 20}, "Auto Progress");
    
    // List demo
    ui.createList("uiexample_demo_list", {50, 340, 200, 140});
    
    // Event Log demo
    ui.createEventLog("uiexample_demo_event_log", {300, 520, 400, 140}, 6);
    ui.createLabel("uiexample_event_log_label", {300, 495, 200, 20}, "Event Log (Auto-updating):");
    ui.setupDemoEventLog("uiexample_demo_event_log");
    
    // Animation button
    ui.createButton("uiexample_animate_btn", {300, 340, 120, 40}, "Animate");
    
    // Theme toggle button
    ui.createButton("uiexample_theme_btn", {300, 390, 120, 40}, "Dark Theme");
    
    // Instructions
    ui.createLabel("uiexample_instructions", {450, 340, 300, 120},
                   "Controls:\n- Click buttons and UI elements\n- Type in input field\n- Select list items\n- B key to go back");

    // Populate list
    ui.addListItem("uiexample_demo_list", "Option 1: Basic Item");
    ui.addListItem("uiexample_demo_list", "Option 2: Second Item");
    ui.addListItem("uiexample_demo_list", "Option 3: Third Item");
    ui.addListItem("uiexample_demo_list", "Option 4: Fourth Item");
    ui.addListItem("uiexample_demo_list", "Option 5: Fifth Item");

    // Set up button callbacks
    ui.setOnClick("uiexample_back_btn", []() {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->setState("MainMenuState");
    });

    ui.setOnClick("uiexample_animate_btn", [this]() {
        handleAnimation();
    });

    ui.setOnClick("uiexample_theme_btn", [this]() {
        handleThemeChange();
    });

    ui.setOnClick("uiexample_demo_checkbox", [this]() {
        handleCheckboxToggle();
    });

    ui.setOnClick("uiexample_demo_list", [this]() {
        handleListSelection();
    });

    ui.setOnValueChanged("uiexample_demo_slider", [this](float value) {
        handleSliderChange(value);
    });

    ui.setOnTextChanged("uiexample_demo_input", [this](const std::string& text) {
        handleInputChange(text);
    });
    
    return true;
}

void UIExampleState::update(float deltaTime) {
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

void UIExampleState::render(float deltaTime) {
    // Update and render UI components through UIManager using cached renderer for cleaner API
    // Each state that uses UI is responsible for rendering its own UI components
    // This ensures proper render order and state-specific UI management
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime);
    }
    ui.render();
}

bool UIExampleState::exit() {
    std::cout << "Exiting UI Example State\n";
    
    // Clean up all UI components efficiently
    auto& ui = UIManager::Instance();
    ui.removeComponentsWithPrefix("uiexample_");
    ui.removeOverlay();
    
    // Reset theme to prevent contamination of other states (UIExampleState changes themes)
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
    m_selectedListItem = ui.getSelectedListItem("uiexample_demo_list");
    std::cout << "List item selected: " << m_selectedListItem << "\n";
}

void UIExampleState::handleAnimation() {
    auto& ui = UIManager::Instance();
    
    // Animate the animation button
    UIRect currentBounds = ui.getBounds("uiexample_animate_btn");
    UIRect targetBounds = currentBounds;
    targetBounds.x += 50;
    
    ui.animateMove("uiexample_animate_btn", targetBounds, 0.5f, [&ui, currentBounds]() {
        // Animate back to original position
        ui.animateMove("uiexample_animate_btn", currentBounds, 0.5f);
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
    ui.setValue("uiexample_demo_progress", m_progressValue);
}



void UIExampleState::updateSliderLabel(float value) {
    auto& ui = UIManager::Instance();
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << "Slider: " << value;
    ui.setText("uiexample_slider_label", oss.str());
}

void UIExampleState::updateInputLabel(const std::string& text) {
    auto& ui = UIManager::Instance();
    std::string labelText = "Input: " + (text.empty() ? "(empty)" : text);
    ui.setText("uiexample_input_label", labelText);
}

void UIExampleState::applyDarkTheme(bool dark) {
    auto& ui = UIManager::Instance();
    
    if (dark) {
        // Use centralized dark theme
        ui.setThemeMode("dark");
        ui.setText("uiexample_theme_btn", "Light Theme");
    } else {
        // Use centralized light theme
        ui.setThemeMode("light");
        ui.setText("uiexample_theme_btn", "Dark Theme");
    }
    
    // Title styling is handled automatically by UIManager's TITLE component type
}

// Pure UIManager implementation - no UIScreen needed