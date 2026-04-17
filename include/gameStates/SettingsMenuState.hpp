/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef SETTINGS_MENU_STATE_HPP
#define SETTINGS_MENU_STATE_HPP

#include "gameStates/GameState.hpp"
#include "managers/InputManager.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

/**
 * @brief Settings menu state for editing game settings
 *
 * Provides a UI for modifying graphics, audio, input, and gameplay settings.
 * Changes are saved to SettingsManager and persisted to disk.
 */
class SettingsMenuState : public GameState {
public:
    bool enter() override;
    void update(float deltaTime) override;
    void handleInput() override;
    bool exit() override;
    GameStateId getStateId() const override { return GameStateId::SETTINGS_MENU; }

    // GPU rendering support
    void recordGPUVertices(VoidLight::GPURenderer& gpuRenderer,
                           float interpolationAlpha) override;
    void renderGPUUI(VoidLight::GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* swapchainPass) override;
    bool supportsGPURendering() const override { return true; }

private:
    /**
     * @brief Temporary settings struct for Apply/Cancel functionality
     */
    struct TempSettings {
        // Graphics
        int resolutionWidth = 1920;
        int resolutionHeight = 1080;
        bool fullscreen = false;
        bool vsync = true;
        int fpsLimit = 60;
        bool showFps = false;

        // Audio
        float masterVolume = 1.0f;
        float musicVolume = 0.7f;
        float sfxVolume = 0.8f;
        bool muted = false;

        // Gameplay
        std::string difficulty = "normal";
        bool autosaveEnabled = true;
        int autosaveInterval = 300;
    } m_tempSettings;

    /**
     * @brief Current active tab
     */
    enum class SettingsTab {
        Graphics,
        Audio,
        Gameplay,
        Controls
    };
    SettingsTab m_currentTab = SettingsTab::Graphics;

    // Track the command whose binding label we need to refresh after capture
    InputManager::Command m_pendingRefreshCommand{InputManager::Command::COUNT};

    /**
     * @brief Load current settings from SettingsManager into temp storage
     */
    void loadCurrentSettings();

    /**
     * @brief Apply temp settings to SettingsManager and save to disk
     */
    void applySettings();


    /**
     * @brief Create tab button UI
     */
    void createTabButtons();

    /**
     * @brief Create graphics settings UI
     */
    void createGraphicsUI();

    /**
     * @brief Create audio settings UI
     */
    void createAudioUI();

    /**
     * @brief Create gameplay settings UI
     */
    void createGameplayUI();

    /**
     * @brief Create controls (key-binding) settings UI
     */
    void createControlsUI();

    /**
     * @brief Refresh the button labels for a single command row on the Controls tab
     */
    void refreshBindingLabels(InputManager::Command c);

    /**
     * @brief Switch to a different settings tab
     */
    void switchTab(SettingsTab tab);

    /**
     * @brief Update UI visibility based on active tab
     */
    void updateTabVisibility();

    /**
     * @brief Create common buttons (Apply, Cancel, Back)
     */
    void createActionButtons();

    // Returns the stable UI component ID for a binding button
    static std::string bindingButtonId(InputManager::Command c,
                                       InputManager::DeviceCategory cat);

    // Keyboard/gamepad navigation — focus ring covers the tab row and the
    // Apply/Back action buttons. Settings body controls (sliders, checkboxes,
    // rebind buttons) remain mouse-only for this pass.
    static constexpr std::array<std::string_view, 6> kNavOrder{
        "settings_tab_graphics",
        "settings_tab_audio",
        "settings_tab_gameplay",
        "settings_tab_controls",
        "settings_apply_btn",
        "settings_back_btn",
    };
    size_t m_selectedIndex{0};
    void applyKeyboardSelection() const;
    void stepSelection(int delta);
};

#endif  // SETTINGS_MENU_STATE_HPP
