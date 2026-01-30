/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef OVERLAY_DEMO_STATE_HPP
#define OVERLAY_DEMO_STATE_HPP

#include "gameStates/GameState.hpp"

// Demo state to showcase different overlay usage scenarios
class OverlayDemoState : public GameState {
public:
    OverlayDemoState();
    ~OverlayDemoState() override = default;

    // GameState interface
    bool enter() override;
    void update(float deltaTime) override;
    void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
    void handleInput() override;
    bool exit() override;
    std::string getName() const override { return "OverlayDemoState"; }

#ifdef USE_SDL3_GPU
    // GPU rendering support
    void recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                           float interpolationAlpha) override;
    void renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* swapchainPass) override;
    bool supportsGPURendering() const override { return true; }
#endif

private:
    
    // Demo modes
    enum class DemoMode {
        NO_OVERLAY,        // HUD elements - no background overlay
        LIGHT_OVERLAY,     // Menu with light overlay
        DARK_OVERLAY,      // Pause menu with darker overlay
        LIGHT_MODAL_OVERLAY, // Light strong overlay for dialogs
        MODAL_OVERLAY      // Dark strong overlay for dialogs
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
    void setupLightModalOverlayMode();
    void setupModalOverlayMode();
    std::string getModeDescription() const;
    void handleModeSwitch();
    void handleBackButton();
};

#endif // OVERLAY_DEMO_STATE_HPP