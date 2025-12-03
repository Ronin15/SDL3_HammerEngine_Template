/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef SETTINGS_MENU_STATE_HPP
#define SETTINGS_MENU_STATE_HPP

#include "gameStates/GameState.hpp"
#include <string>

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
    void render() override;
    void handleInput() override;
    bool exit() override;
    std::string getName() const override;

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

        // Graphics (Advanced) - Buffer mode configuration
        int bufferCount = 2;  // 2 = double, 3 = triple buffering (restart required)
    } m_tempSettings;

    /**
     * @brief Current active tab
     */
    enum class SettingsTab {
        Graphics,
        Audio,
        Gameplay
    };
    SettingsTab m_currentTab = SettingsTab::Graphics;

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
};

#endif  // SETTINGS_MENU_STATE_HPP
