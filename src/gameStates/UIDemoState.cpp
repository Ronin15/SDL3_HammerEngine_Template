/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/UIDemoState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"


#include <sstream>
#include <iomanip>

// UIExampleState Implementation
UIExampleState::UIExampleState() {
    // Pure UIManager approach - no UIScreen
}

bool UIExampleState::enter() {
    GAMESTATE_INFO("Entering UI Example State");

    // Create UI components directly with UIManager using auto-detecting methods
    auto& ui = UIManager::Instance();

    // Calculate relative positioning for cross-resolution compatibility
    int leftColumnX = 50;
    int leftColumnWidth = 220;
    int rightColumnX = ui.getLogicalWidth() / 2 + 50;
    int rightColumnWidth = ui.getLogicalWidth() - rightColumnX - 50;

    // Create overlay background using auto-detection
    ui.createOverlay();
    // Set overlay to resize with window (full width and height)
    ui.setComponentPositioning("__overlay", {UIPositionMode::TOP_ALIGNED, 0, 0, -1, -1});

    // Title using auto-positioning
    ui.createTitleAtTop("uiexample_title_label", "UI Demo State", 40);

    // Back button using auto-positioning and instruction
    ui.createButtonAtBottom("uiexample_back_btn", "Back", 120, 40);
    // Position instruction label to the right of the button (button is at offsetX=20, width=120, so start at 20+120+10=150)
    ui.createLabel("uiexample_back_instruction", {150, ui.getLogicalHeight() - 75, 200, 30}, "Press B to go back");
    ui.setComponentPositioning("uiexample_back_instruction", {UIPositionMode::BOTTOM_ALIGNED, 150, 20, 200, 30});

    // Slider demo (left column - left-aligned for fullscreen compatibility)
    ui.createSlider("uiexample_demo_slider", {leftColumnX, 140, 200, 30}, 0.0f, 1.0f);
    ui.setValue("uiexample_demo_slider", 0.5f);
    ui.setComponentPositioning("uiexample_demo_slider", {UIPositionMode::LEFT_ALIGNED, 50, 140, 200, 30});

    ui.createLabel("uiexample_slider_label", {leftColumnX + 210, 140, 200, 30}, "Slider: 0.50");
    ui.setComponentPositioning("uiexample_slider_label", {UIPositionMode::LEFT_ALIGNED, 260, 140, 200, 30});

    // Checkbox demo (left column - left-aligned for fullscreen compatibility)
    ui.createCheckbox("uiexample_demo_checkbox", {leftColumnX, 190, 250, 30}, "Toggle Option");
    ui.setComponentPositioning("uiexample_demo_checkbox", {UIPositionMode::LEFT_ALIGNED, 50, 190, 250, 30});

    // Input field demo (left column - left-aligned for fullscreen compatibility)
    ui.createInputField("uiexample_demo_input", {leftColumnX, 240, 200, 30}, "Type here...");
    ui.setComponentPositioning("uiexample_demo_input", {UIPositionMode::LEFT_ALIGNED, 50, 240, 200, 30});

    ui.createLabel("uiexample_input_label", {leftColumnX + 210, 240, 300, 30}, "Input: (empty)");
    ui.setComponentPositioning("uiexample_input_label", {UIPositionMode::LEFT_ALIGNED, 260, 240, 300, 30});

    // Progress bar demo (left column - left-aligned for fullscreen compatibility)
    ui.createProgressBar("uiexample_demo_progress", {leftColumnX, 290, 200, 20}, 0.0f, 1.0f);
    ui.setComponentPositioning("uiexample_demo_progress", {UIPositionMode::LEFT_ALIGNED, 50, 290, 200, 20});

    ui.createLabel("uiexample_progress_label", {leftColumnX + 210, 290, 200, 20}, "Auto Progress");
    ui.setComponentPositioning("uiexample_progress_label", {UIPositionMode::LEFT_ALIGNED, 260, 290, 200, 20});

    // List demo (left column - left-aligned for fullscreen compatibility)
    ui.createList("uiexample_demo_list", {leftColumnX, 340, leftColumnWidth, 140});
    ui.setComponentPositioning("uiexample_demo_list", {UIPositionMode::LEFT_ALIGNED, 50, 340, 220, 140});

    // Event Log demo - mirroring EventDemoState pattern but on right side
    // EventDemoState uses: BOTTOM_ALIGNED, offsetX=10, offsetY=20, width=730, height=180
    // We mirror this with BOTTOM_RIGHT for right-side positioning
    ui.createEventLog("uiexample_demo_event_log", {rightColumnX, ui.getLogicalHeight() - 200, rightColumnWidth, 180}, 6);
    ui.setComponentPositioning("uiexample_demo_event_log", {UIPositionMode::BOTTOM_RIGHT, 10, 20, 730, 180});

    ui.createLabel("uiexample_event_log_label", {rightColumnX, ui.getLogicalHeight() - 220, rightColumnWidth/2, 20}, "Event Log (Fixed Size):");
    ui.setComponentPositioning("uiexample_event_log_label", {UIPositionMode::BOTTOM_RIGHT, 10, 210, 730, 20});

    ui.setupDemoEventLog("uiexample_demo_event_log");

    // Animation button (right column - left-aligned at baseline center+50)
    ui.createButton("uiexample_animate_btn", {rightColumnX, 340, 120, 40}, "Animate");
    ui.setComponentPositioning("uiexample_animate_btn", {UIPositionMode::LEFT_ALIGNED, 1010, 340, 120, 40});

    // Theme toggle button (right column - left-aligned at baseline center+50)
    ui.createButton("uiexample_theme_btn", {rightColumnX, 390, 150, 40}, "Dark Theme");
    ui.setComponentPositioning("uiexample_theme_btn", {UIPositionMode::LEFT_ALIGNED, 1010, 390, 150, 40});

    // Instructions (right column - left-aligned with fixed width for stability)
    ui.createLabel("uiexample_instructions", {rightColumnX + 150, 340, rightColumnWidth - 150, 120},
                   "Controls:\n- Click buttons and UI elements\n- Type in input field\n- Select list items\n- B key to go back");
    ui.setComponentPositioning("uiexample_instructions", {UIPositionMode::LEFT_ALIGNED, 1160, 340, 750, 120});

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
        gameStateManager->changeState("MainMenuState");
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
    // Advance UI animations and interactions
    UIManager::Instance().update(deltaTime);

    // Update progress bar animation
    updateProgressBar(deltaTime);
}

void UIExampleState::render() {
    // Update and render UI components through UIManager using cached renderer for cleaner API
    // Each state that uses UI is responsible for rendering its own UI components
    // This ensures proper render order and state-specific UI management
    auto& ui = UIManager::Instance();
    ui.render();
}

bool UIExampleState::exit() {
    GAMESTATE_INFO("Exiting UI Example State");

    // Clean up UI components using simplified method
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    return true;
}

void UIExampleState::handleSliderChange(float value) {
    m_sliderValue = value;
    updateSliderLabel(value);
    GAMESTATE_DEBUG("Slider value changed: " + std::to_string(value));
}

void UIExampleState::handleCheckboxToggle() {
    m_checkboxValue = !m_checkboxValue;
    GAMESTATE_DEBUG("Checkbox toggled: " + std::string(m_checkboxValue ? "checked" : "unchecked"));
}

void UIExampleState::handleInputChange(const std::string& text) {
    m_inputText = text;
    updateInputLabel(text);
    GAMESTATE_DEBUG("Input text changed: " + text);
}

void UIExampleState::handleListSelection() {
    const auto& ui = UIManager::Instance();
    m_selectedListItem = ui.getSelectedListItem("uiexample_demo_list");
    GAMESTATE_DEBUG("List item selected: " + std::to_string(m_selectedListItem));
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

    GAMESTATE_DEBUG("Animation triggered");
}

void UIExampleState::handleThemeChange() {
    m_darkTheme = !m_darkTheme;
    applyDarkTheme(m_darkTheme);
    GAMESTATE_DEBUG("Theme changed to: " + std::string(m_darkTheme ? "dark" : "light"));
}

void UIExampleState::handleInput() {
    // Handle B key to go back
    const auto& inputManager = InputManager::Instance();
    if (inputManager.wasKeyPressed(SDL_SCANCODE_B)) {
        const auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->changeState("MainMenuState");
    }
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
