/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/OverlayDemoState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>
#include <string>

// OverlayDemoState Implementation
OverlayDemoState::OverlayDemoState() {
    // Pure UIManager approach - no UIScreen
}

bool OverlayDemoState::enter() {
    std::cout << "Entering Overlay Demo State\n";

    // Reset theme to prevent contamination from other states
    auto& ui = UIManager::Instance();
    ui.resetToDefaultTheme();

    // Create persistent control components
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Add title
    ui.createTitle("overlay_control_title", {0, 10, windowWidth, 30}, "Overlay Demo State");
    ui.setTitleAlignment("overlay_control_title", UIAlignment::CENTER_CENTER);

    // Control buttons that persist across all modes
    ui.createButtonDanger("overlay_control_back_btn", {20, windowHeight - 60, 100, 40}, "Back");
    ui.createButton("overlay_control_next_mode_btn", {140, windowHeight - 60, 150, 40}, "Next Mode");
    ui.createLabel("overlay_control_instructions", {310, windowHeight - 55, 400, 30}, "Space = Next Mode, B = Back");

    // Set up button callbacks
    ui.setOnClick("overlay_control_back_btn", [this]() {
        handleBackButton();
    });

    ui.setOnClick("overlay_control_next_mode_btn", [this]() {
        handleModeSwitch();
    });



    // Start with no overlay mode
    m_currentMode = DemoMode::NO_OVERLAY;
    setupModeUI();

    return true;
}

void OverlayDemoState::update(float deltaTime) {
    // Update transition timer
    m_transitionTimer += deltaTime;
}

void OverlayDemoState::render(float deltaTime) {
    // Update and render UI components through UIManager using cached renderer for cleaner API
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(deltaTime);
    }
    ui.render();
}

bool OverlayDemoState::exit() {
    std::cout << "Exiting Overlay Demo State\n";

    // Clean up UI components using simplified method
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    return true;
}

void OverlayDemoState::switchToNextMode() {
    // Cycle through modes
    switch (m_currentMode) {
        case DemoMode::NO_OVERLAY:
            m_currentMode = DemoMode::LIGHT_OVERLAY;
            break;
        case DemoMode::LIGHT_OVERLAY:
            m_currentMode = DemoMode::DARK_OVERLAY;
            break;
        case DemoMode::DARK_OVERLAY:
            m_currentMode = DemoMode::LIGHT_MODAL_OVERLAY;
            break;
        case DemoMode::LIGHT_MODAL_OVERLAY:
            m_currentMode = DemoMode::MODAL_OVERLAY;
            break;
        case DemoMode::MODAL_OVERLAY:
            m_currentMode = DemoMode::NO_OVERLAY;
            break;
    }

    setupModeUI();
    m_transitionTimer = 0.0f;
}

void OverlayDemoState::setupModeUI() {
    clearCurrentUI();

    switch (m_currentMode) {
        case DemoMode::NO_OVERLAY:
            setupNoOverlayMode();
            break;
        case DemoMode::LIGHT_OVERLAY:
            setupLightOverlayMode();
            break;
        case DemoMode::DARK_OVERLAY:
            setupDarkOverlayMode();
            break;
        case DemoMode::LIGHT_MODAL_OVERLAY:
            setupLightModalOverlayMode();
            break;
        case DemoMode::MODAL_OVERLAY:
            setupModalOverlayMode();
            break;
    }
}

void OverlayDemoState::clearCurrentUI() {
    auto& ui = UIManager::Instance();

    // Remove overlay
    ui.removeOverlay();

    // Only remove demo components, not control components
    ui.removeComponentsWithPrefix("overlay_demo_");
    
    // Reset to default theme to prevent modal themes from contaminating other modes
    ui.resetToDefaultTheme();
}

void OverlayDemoState::setupNoOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();

    // NO OVERLAY - Perfect for HUD elements
    // This shows how HUD elements look without any background interference

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: HUD Elements (No Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, windowWidth - 40), 50},
                   "Perfect for: Health bars, Score, Minimap, Chat\nGame content remains fully visible");

    // Enable text backgrounds for readability over variable backgrounds
    ui.enableTextBackground(MODE_LABEL, true);
    ui.enableTextBackground(DESCRIPTION_LABEL, true);

    // Simulate HUD elements
    ui.createProgressBar(HEALTH_BAR, {20, 150, 200, 25}, 0.0f, 100.0f);
    ui.setValue(HEALTH_BAR, 75.0f);
    ui.createLabel(SCORE_LABEL, {20, 185, 150, 20}, "Score: 12,450");

    // Minimap simulation
    ui.createPanel(MINIMAP_PANEL, {windowWidth - 160, 20, 140, 140});

    // HUD elements and minimap use theme styling - no custom colors needed
}

void OverlayDemoState::setupLightOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // LIGHT OVERLAY - Set theme BEFORE creating components
    ui.setThemeMode("light");
    ui.createOverlay(windowWidth, windowHeight);

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Main Menu (Light Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, windowWidth - 40), 50},
                   "Perfect for: Main menus, Settings screens\nSubtle separation from background");

    // Enable text backgrounds for readability over variable backgrounds
    ui.enableTextBackground(MODE_LABEL, true);
    ui.enableTextBackground(DESCRIPTION_LABEL, true);

    // Simulate menu buttons
    ui.createButton(MENU_BUTTON_1, {windowWidth/2 - 100, 150, 200, 50}, "New Game");
    ui.createButton(MENU_BUTTON_2, {windowWidth/2 - 100, 220, 200, 50}, "Load Game");
    ui.createButton(MENU_BUTTON_3, {windowWidth/2 - 100, 290, 200, 50}, "Options");
}

void OverlayDemoState::setupDarkOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // DARK OVERLAY - Set theme BEFORE creating components
    ui.setThemeMode("dark");
    ui.createOverlay(windowWidth, windowHeight);

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Pause Menu (Dark Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, windowWidth - 40), 50},
                   "Perfect for: Pause menus, In-game menus\nDarker theme for focus during gameplay");

    // Simulate pause menu
    ui.createButton(MENU_BUTTON_1, {windowWidth/2 - 100, 150, 200, 50}, "Resume");
    ui.createButton(MENU_BUTTON_2, {windowWidth/2 - 100, 220, 200, 50}, "Settings");
    ui.createButtonDanger(MENU_BUTTON_3, {windowWidth/2 - 100, 290, 200, 50}, "Quit to Menu");

}

void OverlayDemoState::setupModalOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Modal Dialog (Strong Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, windowWidth - 40), 50},
                   "Perfect for: Confirmation dialogs, Settings panels\nStrong overlay demands attention");

    // Simulate modal dialog - simple, clean positioning
    int dialogX = (windowWidth - 400) / 2;
    int dialogY = (windowHeight - 200) / 2;

    ui.createModal("overlay_demo_dialog_panel", {dialogX, dialogY, 400, 200}, "dark", windowWidth, windowHeight);
    ui.createLabel("overlay_demo_dialog_title", {dialogX + 20, dialogY + 20, 360, 30}, "Confirm Action");
    ui.createLabel("overlay_demo_dialog_text", {dialogX + 20, dialogY + 60, 360, 40}, "Are you sure you want to quit?");
    
    // Disable text backgrounds for labels inside modal (they have solid modal background)
    ui.enableTextBackground("overlay_demo_dialog_title", false);
    ui.enableTextBackground("overlay_demo_dialog_text", false);
    ui.createButtonSuccess("overlay_demo_modal_yes_btn", {dialogX + 50, dialogY + 120, 100, 40}, "Yes");
    ui.createButtonWarning("overlay_demo_modal_cancel_btn", {dialogX + 250, dialogY + 120, 100, 40}, "Cancel");

    // All styling handled by UIManager theme - no custom colors in state
}

void OverlayDemoState::setupLightModalOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Light Modal Dialog (Strong Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, windowWidth - 40), 50},
                   "Perfect for: Light-themed dialogs, Settings panels\nLight strong overlay with good contrast");

    // Simulate modal dialog - simple, clean positioning
    int dialogX = (windowWidth - 400) / 2;
    int dialogY = (windowHeight - 200) / 2;

    ui.createModal("overlay_demo_dialog_panel", {dialogX, dialogY, 400, 200}, "light", windowWidth, windowHeight);
    ui.createLabel("overlay_demo_dialog_title", {dialogX + 20, dialogY + 20, 360, 30}, "Confirm Action");
    ui.createLabel("overlay_demo_dialog_text", {dialogX + 20, dialogY + 60, 360, 40}, "Save changes before closing?");
    
    // Disable text backgrounds for labels inside modal (they have solid modal background)
    ui.enableTextBackground("overlay_demo_dialog_title", false);
    ui.enableTextBackground("overlay_demo_dialog_text", false);
    ui.createButtonSuccess("overlay_demo_modal_save_btn", {dialogX + 50, dialogY + 120, 100, 40}, "Save");
    ui.createButtonWarning("overlay_demo_modal_cancel_btn", {dialogX + 250, dialogY + 120, 100, 40}, "Cancel");

    // All styling handled by UIManager theme - no custom colors in state
}

std::string OverlayDemoState::getModeDescription() const {
    switch (m_currentMode) {
        case DemoMode::NO_OVERLAY:
            return "No Overlay - HUD Elements";
        case DemoMode::LIGHT_OVERLAY:
            return "Light Overlay - Main Menu";
        case DemoMode::DARK_OVERLAY:
            return "Dark Overlay - Pause Menu";
        case DemoMode::LIGHT_MODAL_OVERLAY:
            return "Light Modal Overlay - Light Dialog";
        case DemoMode::MODAL_OVERLAY:
            return "Modal Overlay - Dark Dialog";
        default:
            return "Unknown";
    }
}

void OverlayDemoState::handleModeSwitch() {
    switchToNextMode();
    std::cout << "Switched to: " << getModeDescription() << "\n";
}

void OverlayDemoState::handleInput() {
    const auto& inputManager = InputManager::Instance();

    // Use InputManager's new event-driven key press detection
    if (inputManager.wasKeyPressed(SDL_SCANCODE_SPACE)) {
        handleModeSwitch();
    }

    if (inputManager.wasKeyPressed(SDL_SCANCODE_B)) {
        handleBackButton();
    }
}

void OverlayDemoState::handleBackButton() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("MainMenuState");
}

// Pure UIManager implementation - no UIScreen needed
