/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/OverlayDemoState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"

#include <string>

// OverlayDemoState Implementation
OverlayDemoState::OverlayDemoState() {
    // Pure UIManager approach - no UIScreen
}

bool OverlayDemoState::enter() {
    GAMESTATE_INFO("Entering Overlay Demo State");

    // Reset theme to prevent contamination from other states
    auto& ui = UIManager::Instance();
    ui.resetToDefaultTheme();

    // Create persistent control components using auto-detecting methods

    // Add title using auto-positioning
    ui.createTitleAtTop("overlay_control_title", "Overlay Demo State", 30);

    // Control buttons that persist across all modes using auto-detected dimensions
    ui.createButtonAtBottom("overlay_control_back_btn", "Back", 100, 40);

    ui.createButton("overlay_control_next_mode_btn", {140, ui.getLogicalHeight() - 60, 150, 40}, "Next Mode");
    ui.setComponentPositioning("overlay_control_next_mode_btn", {UIPositionMode::BOTTOM_ALIGNED, 140, 20, 150, 40});

    ui.createLabel("overlay_control_instructions", {310, ui.getLogicalHeight() - 55, 400, 30}, "Space = Next Mode, B = Back");
    ui.setComponentPositioning("overlay_control_instructions", {UIPositionMode::BOTTOM_ALIGNED, 310, 15, 400, 30});

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

void OverlayDemoState::render() {
    // Update and render UI components through UIManager using cached renderer for cleaner API
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(0.0); // UI updates are not time-dependent in this state
    }
    ui.render();
}

bool OverlayDemoState::exit() {
    GAMESTATE_INFO("Exiting Overlay Demo State");

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

    // NO OVERLAY - Perfect for HUD elements
    // This shows how HUD elements look without any background interference

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: HUD Elements (No Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, ui.getLogicalWidth() - 40), 50},
                   "Perfect for: Health bars, Score, Minimap, Chat\nGame content remains fully visible");

    // Enable text backgrounds for readability over variable backgrounds
    ui.enableTextBackground(MODE_LABEL, true);
    ui.enableTextBackground(DESCRIPTION_LABEL, true);

    // Simulate HUD elements
    ui.createProgressBar(HEALTH_BAR, {20, 150, 200, 25}, 0.0f, 100.0f);
    ui.setValue(HEALTH_BAR, 75.0f);
    ui.createLabel(SCORE_LABEL, {20, 185, 150, 20}, "Score: 12,450");

    // Minimap simulation
    ui.createPanel(MINIMAP_PANEL, {ui.getLogicalWidth() - 160, 20, 140, 140});

    // HUD elements and minimap use theme styling - no custom colors needed
}

void OverlayDemoState::setupLightOverlayMode() {
    auto& ui = UIManager::Instance();

    // LIGHT OVERLAY - Set theme BEFORE creating components
    ui.setThemeMode("light");
    ui.createOverlay(); // Auto-detecting overlay creation

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Main Menu (Light Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, ui.getLogicalWidth() - 40), 50},
                   "Perfect for: Main menus, Settings screens\nSubtle separation from background");

    // Enable text backgrounds for readability over variable backgrounds
    ui.enableTextBackground(MODE_LABEL, true);
    ui.enableTextBackground(DESCRIPTION_LABEL, true);

    // Simulate menu buttons using baseline coordinates for centering
    const int baselineWidth = 1920;
    const int buttonX = baselineWidth/2 - 100; // Center in baseline space
    ui.createButton(MENU_BUTTON_1, {buttonX, 150, 200, 50}, "New Game");
    ui.createButton(MENU_BUTTON_2, {buttonX, 220, 200, 50}, "Load Game");
    ui.createButton(MENU_BUTTON_3, {buttonX, 290, 200, 50}, "Options");
}

void OverlayDemoState::setupDarkOverlayMode() {
    auto& ui = UIManager::Instance();

    // DARK OVERLAY - Set theme BEFORE creating components
    ui.setThemeMode("dark");
    ui.createOverlay(); // Auto-detecting overlay creation

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Pause Menu (Dark Theme)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, ui.getLogicalWidth() - 40), 50},
                   "Perfect for: Pause menus, In-game menus\nDarker theme for focus during gameplay");

    // Simulate pause menu using baseline coordinates for centering
    const int baselineWidth = 1920;
    const int buttonX = baselineWidth/2 - 100; // Center in baseline space
    ui.createButton(MENU_BUTTON_1, {buttonX, 150, 200, 50}, "Resume");
    ui.createButton(MENU_BUTTON_2, {buttonX, 220, 200, 50}, "Settings");
    ui.createButtonDanger(MENU_BUTTON_3, {buttonX, 290, 200, 50}, "Quit to Menu");

}

void OverlayDemoState::setupModalOverlayMode() {
    auto& ui = UIManager::Instance();

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Modal Dialog (Strong Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, ui.getLogicalWidth() - 40), 50},
                   "Perfect for: Confirmation dialogs, Settings panels\nStrong overlay demands attention");

    // Calculate dialog position in baseline space (1920x1080)
    const int baselineWidth = 1920;
    const int baselineHeight = 1080;
    const int dialogWidth = 400;
    const int dialogHeight = 200;
    const int dialogX = (baselineWidth - dialogWidth) / 2;   // 760
    const int dialogY = (baselineHeight - dialogHeight) / 2; // 440

    // Create centered dialog
    ui.createCenteredDialog("overlay_demo_dialog_panel", dialogWidth, dialogHeight, "dark");

    // Position child components using absolute baseline coordinates relative to dialog
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

    // Mode indicator
    ui.createLabel(MODE_LABEL, {20, 50, 400, 30}, "Mode: Light Modal Dialog (Strong Overlay)");
    ui.createLabel(DESCRIPTION_LABEL, {20, 85, std::min(600, ui.getLogicalWidth() - 40), 50},
                   "Perfect for: Light-themed dialogs, Settings panels\nLight strong overlay with good contrast");

    // Calculate dialog position in baseline space (1920x1080)
    const int baselineWidth = 1920;
    const int baselineHeight = 1080;
    const int dialogWidth = 400;
    const int dialogHeight = 200;
    const int dialogX = (baselineWidth - dialogWidth) / 2;   // 760
    const int dialogY = (baselineHeight - dialogHeight) / 2; // 440

    // Create centered dialog
    ui.createCenteredDialog("overlay_demo_dialog_panel", dialogWidth, dialogHeight, "light");

    // Position child components using absolute baseline coordinates relative to dialog
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
    GAMESTATE_DEBUG("Switched to: " + getModeDescription());
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
    gameStateManager->changeState("MainMenuState");
}

// Pure UIManager implementation - no UIScreen needed
