/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef OVERLAY_DEMO_STATE_HPP
#define OVERLAY_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"
#include "ui/UIScreen.hpp"
#include <memory>

// Demo state to showcase different overlay usage scenarios
class OverlayDemoState : public GameState {
public:
    OverlayDemoState();
    ~OverlayDemoState() override = default;

    // GameState interface
    bool enter() override;
    void update(float deltaTime) override;
    void render() override;
    bool exit() override;
    std::string getName() const override { return "OverlayDemoState"; }

private:
    std::unique_ptr<UIScreen> m_uiScreen{nullptr};
    
    // Demo modes
    enum class DemoMode {
        NO_OVERLAY,      // HUD elements - no background overlay
        LIGHT_OVERLAY,   // Menu with light overlay
        DARK_OVERLAY,    // Pause menu with darker overlay
        MODAL_OVERLAY    // Dialog/Settings with strong overlay
    };
    
    DemoMode m_currentMode{DemoMode::NO_OVERLAY};
    float m_transitionTimer{0.0f};
    
    // Component IDs with unique prefixes to avoid conflicts
    static constexpr const char* BACK_BUTTON = "overlay_demo_back_btn";
    static constexpr const char* NEXT_MODE_BUTTON = "overlay_demo_next_mode_btn";
    static constexpr const char* MODE_LABEL = "overlay_demo_mode_label";
    static constexpr const char* DESCRIPTION_LABEL = "overlay_demo_description_label";
    static constexpr const char* HEALTH_BAR = "overlay_demo_health_bar";
    static constexpr const char* SCORE_LABEL = "overlay_demo_score_label";
    static constexpr const char* MINIMAP_PANEL = "overlay_demo_minimap_panel";
    static constexpr const char* MENU_BUTTON_1 = "overlay_demo_menu_btn_1";
    static constexpr const char* MENU_BUTTON_2 = "overlay_demo_menu_btn_2";
    static constexpr const char* MENU_BUTTON_3 = "overlay_demo_menu_btn_3";
    
    // Helper methods
    void switchToNextMode();
    void setupModeUI();
    void clearCurrentUI();
    void setupNoOverlayMode();
    void setupLightOverlayMode();
    void setupDarkOverlayMode();
    void setupModalOverlayMode();
    std::string getModeDescription() const;
    void handleModeSwitch();
    void handleBackButton();
};

// Custom UIScreen for overlay demonstrations
class OverlayDemoScreen : public UIScreen {
public:
    OverlayDemoScreen();
    ~OverlayDemoScreen() override = default;

    // UIScreen interface
    void create() override;
    void update(float deltaTime) override;
    void hide() override;
    void onButtonClicked(const std::string& buttonID) override;

    // Callback setters
    void setOnBack(std::function<void()> callback) { m_onBack = callback; }
    void setOnModeSwitch(std::function<void()> callback) { m_onModeSwitch = callback; }

private:
    std::function<void()> m_onBack{};
    std::function<void()> m_onModeSwitch{};
    
    void setupBaseComponents();
    void cleanupAllComponents();
};

#endif // OVERLAY_DEMO_STATE_HPP