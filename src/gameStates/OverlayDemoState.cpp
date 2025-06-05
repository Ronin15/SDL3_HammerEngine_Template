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
    m_uiScreen = std::make_unique<OverlayDemoScreen>();
}

bool OverlayDemoState::enter() {
    std::cout << "Entering Overlay Demo State\n";
    
    // Reset theme to prevent contamination from other states
    auto& ui = UIManager::Instance();
    ui.resetToDefaultTheme();
    
    // Initialize and show the UI screen
    m_uiScreen->show();
    
    // Set up callbacks
    auto screen = static_cast<OverlayDemoScreen*>(m_uiScreen.get());
    screen->setOnBack([this]() {
        handleBackButton();
    });
    
    screen->setOnModeSwitch([this]() {
        handleModeSwitch();
    });
    
    // Start with no overlay mode
    m_currentMode = DemoMode::NO_OVERLAY;
    setupModeUI();
    
    return true;
}

void OverlayDemoState::update(float deltaTime) {
    // Update UI Manager
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    if (m_uiScreen) {
        m_uiScreen->update(deltaTime);
    }
    
    // Handle input with proper key press detection
    handleInput();
    
    // Update transition timer
    m_transitionTimer += deltaTime;
}

void OverlayDemoState::render() {
    // Render UI components through UIManager
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
}

bool OverlayDemoState::exit() {
    std::cout << "Exiting Overlay Demo State\n";
    
    if (m_uiScreen) {
        m_uiScreen->hide();
    }
    
    // Use new UIManager cleanup methods
    auto& ui = UIManager::Instance();
    ui.removeComponentsWithPrefix("overlay_demo_"); // Remove all overlay demo components
    ui.removeComponentsWithPrefix("overlay_control_"); // Remove all control components
    ui.removeThemeBackground();
    ui.resetToDefaultTheme(); // Reset theme state
    
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
        case DemoMode::MODAL_OVERLAY:
            setupModalOverlayMode();
            break;
    }
}

void OverlayDemoState::clearCurrentUI() {
    auto& ui = UIManager::Instance();
    
    // Remove overlay
    ui.removeThemeBackground();
    
    // Only remove demo components, not control components
    ui.removeComponentsWithPrefix("overlay_demo_");
}

void OverlayDemoState::setupNoOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    
    // NO OVERLAY - Perfect for HUD elements
    // This shows how HUD elements look without any background interference
    
    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 20, 400, 30}, "Mode: HUD Elements (No Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 55, 600, 50}, 
                   "Perfect for: Health bars, Score, Minimap, Chat\nGame content remains fully visible");
    
    // Simulate HUD elements
    ui.createProgressBar(HEALTH_BAR, {20, 120, 200, 25}, 0.0f, 100.0f);
    ui.setValue(HEALTH_BAR, 75.0f);
    ui.createLabel(SCORE_LABEL, {20, 155, 150, 20}, "Score: 12,450");
    
    // Minimap simulation
    ui.createPanel(MINIMAP_PANEL, {windowWidth - 160, 20, 140, 140});
    
    // Style the HUD elements for clarity
    UIStyle hudLabelStyle;
    hudLabelStyle.backgroundColor = {0, 0, 0, 100}; // Slight background for readability
    hudLabelStyle.textColor = {255, 255, 255, 255};
    hudLabelStyle.borderColor = {100, 100, 100, 200};
    hudLabelStyle.borderWidth = 1;
    hudLabelStyle.fontID = "fonts_UI_Arial";
    ui.setStyle(SCORE_LABEL, hudLabelStyle);
    
    // Minimap style
    UIStyle minimapStyle;
    minimapStyle.backgroundColor = {0, 0, 0, 150};
    minimapStyle.borderColor = {100, 100, 100, 255};
    minimapStyle.borderWidth = 2;
    ui.setStyle(MINIMAP_PANEL, minimapStyle);
}

void OverlayDemoState::setupLightOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();
    
    // LIGHT OVERLAY - Good for main menus
    ui.setThemeMode("light");
    ui.createThemeBackground(windowWidth, windowHeight);
    
    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 20, 400, 30}, "Mode: Main Menu (Light Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 55, 600, 50}, 
                   "Perfect for: Main menus, Settings screens\nSubtle separation from background");
    
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
    
    // DARK OVERLAY - Good for pause menus
    ui.setThemeMode("dark");
    ui.createThemeBackground(windowWidth, windowHeight);
    
    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 20, 400, 30}, "Mode: Pause Menu (Dark Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 55, 600, 50}, 
                   "Perfect for: Pause menus, In-game menus\nDarker theme for focus during gameplay");
    
    // Simulate pause menu
    ui.createButton(MENU_BUTTON_1, {windowWidth/2 - 100, 150, 200, 50}, "Resume");
    ui.createButton(MENU_BUTTON_2, {windowWidth/2 - 100, 220, 200, 50}, "Settings");
    ui.createButton(MENU_BUTTON_3, {windowWidth/2 - 100, 290, 200, 50}, "Quit to Menu");
}

void OverlayDemoState::setupModalOverlayMode() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();
    
    // MODAL OVERLAY - Just use theme background, no extra overlay
    ui.setThemeMode("light");
    ui.createThemeBackground(windowWidth, windowHeight);
    
    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 20, 400, 30}, "Mode: Modal Dialog (Strong Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 55, 600, 50}, 
                   "Perfect for: Confirmation dialogs, Settings panels\nStrong overlay demands attention");
    
    // Simulate modal dialog
    int dialogWidth = 400;
    int dialogHeight = 200;
    int dialogX = (windowWidth - dialogWidth) / 2;
    int dialogY = (windowHeight - dialogHeight) / 2;
    
    ui.createPanel("overlay_demo_dialog_panel", {dialogX, dialogY, dialogWidth, dialogHeight});
    ui.createLabel("overlay_demo_dialog_title", {dialogX + 20, dialogY + 20, dialogWidth - 40, 30}, "Confirm Action");
    ui.createLabel("overlay_demo_dialog_text", {dialogX + 20, dialogY + 60, dialogWidth - 40, 40}, "Are you sure you want to quit?");
    ui.createButton(MENU_BUTTON_1, {dialogX + 50, dialogY + 120, 100, 40}, "Yes");
    ui.createButton(MENU_BUTTON_2, {dialogX + 250, dialogY + 120, 100, 40}, "Cancel");
    
    // Style the dialog
    UIStyle dialogStyle;
    dialogStyle.backgroundColor = {240, 240, 240, 255};
    dialogStyle.borderColor = {100, 100, 100, 255};
    dialogStyle.borderWidth = 2;
    ui.setStyle("overlay_demo_dialog_panel", dialogStyle);
    
    // Style dialog text to be dark for readability
    UIStyle dialogTextStyle;
    dialogTextStyle.backgroundColor = {0, 0, 0, 0};
    dialogTextStyle.textColor = {20, 20, 20, 255}; // Dark text on light background
    dialogTextStyle.textAlign = UIAlignment::CENTER_LEFT;
    dialogTextStyle.fontID = "fonts_UI_Arial";
    ui.setStyle("overlay_demo_dialog_title", dialogTextStyle);
    ui.setStyle("overlay_demo_dialog_text", dialogTextStyle);
}

std::string OverlayDemoState::getModeDescription() const {
    switch (m_currentMode) {
        case DemoMode::NO_OVERLAY:
            return "No Overlay - HUD Elements";
        case DemoMode::LIGHT_OVERLAY:
            return "Light Overlay - Main Menu";
        case DemoMode::DARK_OVERLAY:
            return "Dark Overlay - Pause Menu";
        case DemoMode::MODAL_OVERLAY:
            return "Modal Overlay - Dialog";
        default:
            return "Unknown";
    }
}

void OverlayDemoState::handleModeSwitch() {
    switchToNextMode();
    std::cout << "Switched to: " << getModeDescription() << "\n";
}

void OverlayDemoState::handleInput() {
    auto& inputManager = InputManager::Instance();
    
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

// OverlayDemoScreen Implementation
OverlayDemoScreen::OverlayDemoScreen() : UIScreen("OverlayDemoScreen") {
}

void OverlayDemoScreen::create() {
    setupBaseComponents();
}

void OverlayDemoScreen::update(float deltaTime) {
    UIScreen::update(deltaTime);
}

void OverlayDemoScreen::hide() {
    cleanupAllComponents();
    UIScreen::hide();
}

void OverlayDemoScreen::onButtonClicked(const std::string& buttonID) {
    if (buttonID == "overlay_control_back_btn" && m_onBack) {
        m_onBack();
    } else if (buttonID == "overlay_control_next_mode_btn" && m_onModeSwitch) {
        m_onModeSwitch();
    }
}

void OverlayDemoScreen::setupBaseComponents() {
    auto& gameEngine = GameEngine::Instance();
    int windowHeight = gameEngine.getWindowHeight();
    
    // Control buttons that persist across all modes (use different prefix)
    createButton("overlay_control_back_btn", {20, windowHeight - 60, 100, 40}, "Back");
    createButton("overlay_control_next_mode_btn", {140, windowHeight - 60, 150, 40}, "Next Mode");
    createLabel("overlay_control_instructions", {310, windowHeight - 55, 400, 30}, "Space = Next Mode, B = Back");
    
    // Add components to tracking
    addComponent("overlay_control_back_btn");
    addComponent("overlay_control_next_mode_btn");
    addComponent("overlay_control_instructions");
}

void OverlayDemoScreen::cleanupAllComponents() {
    auto& ui = getUIManager();
    
    // Remove both demo and control components
    ui.removeComponentsWithPrefix("overlay_demo_");
    ui.removeComponentsWithPrefix("overlay_control_");
}