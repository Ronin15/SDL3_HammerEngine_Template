/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/SettingsMenuState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/SettingsManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"

#include <thread>
#include <chrono>

bool SettingsMenuState::enter() {
    GAMESTATE_INFO("Entering SETTINGS MENU State");

    auto& ui = UIManager::Instance();
    auto& fontMgr = FontManager::Instance();

    // Wait for fonts to load
    constexpr int kMaxWaitMs = 1500;
    int waitedMs = 0;
    while (!fontMgr.areFontsLoaded() && waitedMs < kMaxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        waitedMs += 1;
    }

    // Load current settings from SettingsManager
    loadCurrentSettings();

    // Create title
    ui.createTitleAtTop("settings_title", "Settings", 60);

    // Create tab buttons
    createTabButtons();

    // Create all settings UIs (visibility controlled by current tab)
    createGraphicsUI();
    createAudioUI();
    createGameplayUI();

    // Create action buttons (Apply, Cancel, Back)
    createActionButtons();

    // Show Graphics tab by default
    switchTab(SettingsTab::Graphics);

    return true;
}

void SettingsMenuState::update([[maybe_unused]] float deltaTime) {
    // UI updates handled in render() for thread safety
}

void SettingsMenuState::render(SDL_Renderer* renderer, [[maybe_unused]] float interpolationAlpha) {
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(0.0);
    }
    ui.render(renderer);
}

bool SettingsMenuState::exit() {
    GAMESTATE_INFO("Exiting SETTINGS MENU State");

    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();

    return true;
}

void SettingsMenuState::handleInput() {
    const auto& inputManager = InputManager::Instance();

    // ESC to go back without saving
    if (inputManager.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->changeState("MainMenuState");
    }

    // Tab switching shortcuts
    if (inputManager.wasKeyPressed(SDL_SCANCODE_1)) {
        switchTab(SettingsTab::Graphics);
    }
    if (inputManager.wasKeyPressed(SDL_SCANCODE_2)) {
        switchTab(SettingsTab::Audio);
    }
    if (inputManager.wasKeyPressed(SDL_SCANCODE_3)) {
        switchTab(SettingsTab::Gameplay);
    }
}

std::string SettingsMenuState::getName() const {
    return "SettingsMenuState";
}

void SettingsMenuState::loadCurrentSettings() {
    using namespace HammerEngine;
    auto& settings = SettingsManager::Instance();

    // Graphics
    m_tempSettings.resolutionWidth = settings.get<int>("graphics", "resolution_width", 1920);
    m_tempSettings.resolutionHeight = settings.get<int>("graphics", "resolution_height", 1080);
    m_tempSettings.fullscreen = settings.get<bool>("graphics", "fullscreen", false);
    m_tempSettings.vsync = settings.get<bool>("graphics", "vsync", true);
    m_tempSettings.fpsLimit = settings.get<int>("graphics", "fps_limit", 60);
    m_tempSettings.showFps = settings.get<bool>("graphics", "show_fps", false);

    // Audio
    m_tempSettings.masterVolume = settings.get<float>("audio", "master_volume", 1.0f);
    m_tempSettings.musicVolume = settings.get<float>("audio", "music_volume", 0.7f);
    m_tempSettings.sfxVolume = settings.get<float>("audio", "sfx_volume", 0.8f);
    m_tempSettings.muted = settings.get<bool>("audio", "muted", false);

    // Gameplay
    m_tempSettings.difficulty = settings.get<std::string>("gameplay", "difficulty", "normal");
    m_tempSettings.autosaveEnabled = settings.get<bool>("gameplay", "autosave_enabled", true);
    m_tempSettings.autosaveInterval = settings.get<int>("gameplay", "autosave_interval", 300);

    // Graphics (Advanced)
    m_tempSettings.bufferCount = settings.get<int>("graphics", "buffer_count", 2);
}

void SettingsMenuState::applySettings() {
    using namespace HammerEngine;
    auto& settings = SettingsManager::Instance();
    auto& gameEngine = GameEngine::Instance();

    // Graphics
    settings.set("graphics", "resolution_width", m_tempSettings.resolutionWidth);
    settings.set("graphics", "resolution_height", m_tempSettings.resolutionHeight);
    settings.set("graphics", "fullscreen", m_tempSettings.fullscreen);
    settings.set("graphics", "fps_limit", m_tempSettings.fpsLimit);
    settings.set("graphics", "show_fps", m_tempSettings.showFps);

    // Apply fullscreen setting immediately
    // SDL will automatically fire SDL_EVENT_WINDOW_RESIZED which triggers
    // InputManager::onWindowResize() → UIManager::onWindowResize()
    // This ensures clean, single-path UI repositioning
    if (gameEngine.isFullscreen() != m_tempSettings.fullscreen) {
        gameEngine.setFullscreen(m_tempSettings.fullscreen);
        GAMESTATE_INFO("Fullscreen setting applied - UI will update via SDL resize event");
    }

    // Apply VSync setting to GameEngine (also saves to SettingsManager internally)
    gameEngine.setVSyncEnabled(m_tempSettings.vsync);

    // Audio
    settings.set("audio", "master_volume", m_tempSettings.masterVolume);
    settings.set("audio", "music_volume", m_tempSettings.musicVolume);
    settings.set("audio", "sfx_volume", m_tempSettings.sfxVolume);
    settings.set("audio", "muted", m_tempSettings.muted);

    // Gameplay
    settings.set("gameplay", "difficulty", m_tempSettings.difficulty);
    settings.set("gameplay", "autosave_enabled", m_tempSettings.autosaveEnabled);
    settings.set("gameplay", "autosave_interval", m_tempSettings.autosaveInterval);

    // Graphics (Advanced)
    settings.set("graphics", "buffer_count", m_tempSettings.bufferCount);

    // Save to disk
    settings.saveToFile("res/settings.json");

    GAMESTATE_INFO("Settings saved successfully");
    GAMESTATE_INFO("Buffer mode changed to " + std::string(m_tempSettings.bufferCount == 2 ? "Double(2)" : "Triple(3)") +
                   " - restart required for changes to take effect");
}


void SettingsMenuState::createTabButtons() {
    auto& ui = UIManager::Instance();

    int tabWidth = 200;
    int tabHeight = 40;
    int tabSpacing = 10;
    int startX = ui.getLogicalWidth() / 2 - (3 * tabWidth + 2 * tabSpacing) / 2;
    int tabY = 80;

    ui.createButton("settings_tab_graphics", {startX, tabY, tabWidth, tabHeight}, "Graphics (1)");
    ui.setComponentPositioning("settings_tab_graphics", {UIPositionMode::CENTERED_H, -(tabWidth + tabSpacing), tabY, tabWidth, tabHeight});

    ui.createButton("settings_tab_audio", {startX + tabWidth + tabSpacing, tabY, tabWidth, tabHeight}, "Audio (2)");
    ui.setComponentPositioning("settings_tab_audio", {UIPositionMode::CENTERED_H, 0, tabY, tabWidth, tabHeight});

    ui.createButton("settings_tab_gameplay", {startX + 2 * (tabWidth + tabSpacing), tabY, tabWidth, tabHeight}, "Gameplay (3)");
    ui.setComponentPositioning("settings_tab_gameplay", {UIPositionMode::CENTERED_H, tabWidth + tabSpacing, tabY, tabWidth, tabHeight});

    // Tab callbacks
    ui.setOnClick("settings_tab_graphics", [this]() {
        switchTab(SettingsTab::Graphics);
    });

    ui.setOnClick("settings_tab_audio", [this]() {
        switchTab(SettingsTab::Audio);
    });

    ui.setOnClick("settings_tab_gameplay", [this]() {
        switchTab(SettingsTab::Gameplay);
    });
}

void SettingsMenuState::createGraphicsUI() {
    auto& ui = UIManager::Instance();

    int leftColumnX = 200;
    int startY = 160;
    int rowHeight = 60;
    int labelWidth = 250;
    int controlWidth = 300;
    int controlX = leftColumnX + labelWidth + 20;

    // VSync checkbox
    ui.createLabel("settings_vsync_label", {leftColumnX, startY, labelWidth, 40}, "VSync:");
    ui.createCheckbox("settings_vsync_checkbox", {controlX, startY, 30, 30}, "Enabled");
    ui.setChecked("settings_vsync_checkbox", m_tempSettings.vsync);
    ui.setOnClick("settings_vsync_checkbox", [this]() {
        const auto& ui = UIManager::Instance();
        m_tempSettings.vsync = ui.getChecked("settings_vsync_checkbox");
    });

    // Fullscreen checkbox
    ui.createLabel("settings_fullscreen_label", {leftColumnX, startY + rowHeight, labelWidth, 40}, "Fullscreen:");
    ui.createCheckbox("settings_fullscreen_checkbox", {controlX, startY + rowHeight, 30, 30}, "Enabled");
    ui.setChecked("settings_fullscreen_checkbox", m_tempSettings.fullscreen);
    ui.setOnClick("settings_fullscreen_checkbox", [this]() {
        const auto& ui = UIManager::Instance();
        m_tempSettings.fullscreen = ui.getChecked("settings_fullscreen_checkbox");
    });

    // Show FPS checkbox
    ui.createLabel("settings_showfps_label", {leftColumnX, startY + 2 * rowHeight, labelWidth, 40}, "Show FPS:");
    ui.createCheckbox("settings_showfps_checkbox", {controlX, startY + 2 * rowHeight, 30, 30}, "Enabled");
    ui.setChecked("settings_showfps_checkbox", m_tempSettings.showFps);
    ui.setOnClick("settings_showfps_checkbox", [this]() {
        const auto& ui = UIManager::Instance();
        m_tempSettings.showFps = ui.getChecked("settings_showfps_checkbox");
    });

    // Resolution label (informational)
    ui.createLabel("settings_resolution_label", {leftColumnX, startY + 3 * rowHeight, labelWidth + controlWidth, 40},
                   "Resolution: " + std::to_string(m_tempSettings.resolutionWidth) + "x" +
                   std::to_string(m_tempSettings.resolutionHeight));

    // Buffer Mode toggle (Double vs Triple buffering)
    ui.createLabel("settings_buffer_label", {leftColumnX, startY + 4 * rowHeight, labelWidth, 40}, "Buffer Mode:");
    ui.createButton("settings_buffer_button", {controlX, startY + 4 * rowHeight, 150, 40},
                    m_tempSettings.bufferCount == 2 ? "Double (2)" : "Triple (3)");
    ui.setOnClick("settings_buffer_button", [this]() {
        // Toggle between 2 and 3
        m_tempSettings.bufferCount = (m_tempSettings.bufferCount == 2) ? 3 : 2;
        auto& ui = UIManager::Instance();
        ui.setText("settings_buffer_button", m_tempSettings.bufferCount == 2 ? "Double (2)" : "Triple (3)");
    });

    // Add info label about restart requirement
    ui.createLabel("settings_buffer_info", {controlX + 160, startY + 4 * rowHeight, 400, 40},
                   "(Restart required)");
}

void SettingsMenuState::createAudioUI() {
    auto& ui = UIManager::Instance();

    int leftColumnX = 200;
    int startY = 160;
    int rowHeight = 60;
    int labelWidth = 250;
    int sliderWidth = 300;
    int sliderX = leftColumnX + labelWidth + 20;

    // Master Volume slider
    ui.createLabel("settings_master_volume_label", {leftColumnX, startY, labelWidth, 40}, "Master Volume:");
    ui.createSlider("settings_master_volume_slider", {sliderX, startY, sliderWidth, 30}, 0.0f, 1.0f);
    ui.setValue("settings_master_volume_slider", m_tempSettings.masterVolume);
    ui.setOnValueChanged("settings_master_volume_slider", [this](float value) {
        m_tempSettings.masterVolume = value;
        auto& ui = UIManager::Instance();
        ui.setText("settings_master_volume_value", std::to_string(static_cast<int>(value * 100)) + "%");
    });
    ui.createLabel("settings_master_volume_value", {sliderX + sliderWidth + 10, startY, 80, 40},
                   std::to_string(static_cast<int>(m_tempSettings.masterVolume * 100)) + "%");

    // Music Volume slider
    ui.createLabel("settings_music_volume_label", {leftColumnX, startY + rowHeight, labelWidth, 40}, "Music Volume:");
    ui.createSlider("settings_music_volume_slider", {sliderX, startY + rowHeight, sliderWidth, 30}, 0.0f, 1.0f);
    ui.setValue("settings_music_volume_slider", m_tempSettings.musicVolume);
    ui.setOnValueChanged("settings_music_volume_slider", [this](float value) {
        m_tempSettings.musicVolume = value;
        auto& ui = UIManager::Instance();
        ui.setText("settings_music_volume_value", std::to_string(static_cast<int>(value * 100)) + "%");
    });
    ui.createLabel("settings_music_volume_value", {sliderX + sliderWidth + 10, startY + rowHeight, 80, 40},
                   std::to_string(static_cast<int>(m_tempSettings.musicVolume * 100)) + "%");

    // SFX Volume slider
    ui.createLabel("settings_sfx_volume_label", {leftColumnX, startY + 2 * rowHeight, labelWidth, 40}, "SFX Volume:");
    ui.createSlider("settings_sfx_volume_slider", {sliderX, startY + 2 * rowHeight, sliderWidth, 30}, 0.0f, 1.0f);
    ui.setValue("settings_sfx_volume_slider", m_tempSettings.sfxVolume);
    ui.setOnValueChanged("settings_sfx_volume_slider", [this](float value) {
        m_tempSettings.sfxVolume = value;
        auto& ui = UIManager::Instance();
        ui.setText("settings_sfx_volume_value", std::to_string(static_cast<int>(value * 100)) + "%");
    });
    ui.createLabel("settings_sfx_volume_value", {sliderX + sliderWidth + 10, startY + 2 * rowHeight, 80, 40},
                   std::to_string(static_cast<int>(m_tempSettings.sfxVolume * 100)) + "%");

    // Mute checkbox
    ui.createLabel("settings_mute_label", {leftColumnX, startY + 3 * rowHeight, labelWidth, 40}, "Mute All:");
    ui.createCheckbox("settings_mute_checkbox", {sliderX, startY + 3 * rowHeight, 30, 30}, "Muted");
    ui.setChecked("settings_mute_checkbox", m_tempSettings.muted);
    ui.setOnClick("settings_mute_checkbox", [this]() {
        const auto& ui = UIManager::Instance();
        m_tempSettings.muted = ui.getChecked("settings_mute_checkbox");
    });

    // Hide audio UI by default
    ui.setComponentVisible("settings_master_volume_label", false);
    ui.setComponentVisible("settings_master_volume_slider", false);
    ui.setComponentVisible("settings_master_volume_value", false);
    ui.setComponentVisible("settings_music_volume_label", false);
    ui.setComponentVisible("settings_music_volume_slider", false);
    ui.setComponentVisible("settings_music_volume_value", false);
    ui.setComponentVisible("settings_sfx_volume_label", false);
    ui.setComponentVisible("settings_sfx_volume_slider", false);
    ui.setComponentVisible("settings_sfx_volume_value", false);
    ui.setComponentVisible("settings_mute_label", false);
    ui.setComponentVisible("settings_mute_checkbox", false);
}

void SettingsMenuState::createGameplayUI() {
    auto& ui = UIManager::Instance();

    int leftColumnX = 200;
    int startY = 160;
    int rowHeight = 60;
    int labelWidth = 250;
    int controlX = leftColumnX + labelWidth + 20;

    // Difficulty label
    ui.createLabel("settings_difficulty_label", {leftColumnX, startY, labelWidth + 200, 40},
                   "Difficulty: " + m_tempSettings.difficulty);

    // Autosave checkbox
    ui.createLabel("settings_autosave_label", {leftColumnX, startY + rowHeight, labelWidth, 40}, "Autosave:");
    ui.createCheckbox("settings_autosave_checkbox", {controlX, startY + rowHeight, 30, 30}, "Enabled");
    ui.setChecked("settings_autosave_checkbox", m_tempSettings.autosaveEnabled);
    ui.setOnClick("settings_autosave_checkbox", [this]() {
        const auto& ui = UIManager::Instance();
        m_tempSettings.autosaveEnabled = ui.getChecked("settings_autosave_checkbox");
    });

    // Autosave interval label
    ui.createLabel("settings_autosave_interval_label", {leftColumnX, startY + 2 * rowHeight, labelWidth + 200, 40},
                   "Autosave Interval: " + std::to_string(m_tempSettings.autosaveInterval) + " seconds");

    // Hide gameplay UI by default
    ui.setComponentVisible("settings_difficulty_label", false);
    ui.setComponentVisible("settings_autosave_label", false);
    ui.setComponentVisible("settings_autosave_checkbox", false);
    ui.setComponentVisible("settings_autosave_interval_label", false);
}

void SettingsMenuState::createActionButtons() {
    auto& ui = UIManager::Instance();

    int buttonWidth = 150;
    int buttonHeight = 50;
    int buttonSpacing = 20;
    int bottomOffset = 80;  // Distance from bottom edge
    int centerX = ui.getLogicalWidth() / 2;
    int bottomY = ui.getLogicalHeight() - bottomOffset;

    // Apply button (Success green) - left of center, bottom centered
    int applyX = centerX - buttonWidth - buttonSpacing/2;
    ui.createButtonSuccess("settings_apply_btn",
        {applyX, bottomY, buttonWidth, buttonHeight},
        "Apply");
    ui.setComponentPositioning("settings_apply_btn", {UIPositionMode::BOTTOM_CENTERED, -(buttonWidth/2 + buttonSpacing/2), bottomOffset, buttonWidth, buttonHeight});
    ui.setOnClick("settings_apply_btn", [this]() {
        applySettings();
    });

    // Back button (goes back without saving) - right of center, bottom centered
    int backX = centerX + buttonSpacing/2;
    ui.createButtonDanger("settings_back_btn",
        {backX, bottomY, buttonWidth, buttonHeight},
        "Back");
    ui.setComponentPositioning("settings_back_btn", {UIPositionMode::BOTTOM_CENTERED, buttonWidth/2 + buttonSpacing/2, bottomOffset, buttonWidth, buttonHeight});
    ui.setOnClick("settings_back_btn", []() {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->changeState("MainMenuState");
    });
}

void SettingsMenuState::switchTab(SettingsTab tab) {
    m_currentTab = tab;
    updateTabVisibility();

    // Update tab button colors to show active tab
    auto& ui = UIManager::Instance();

    // Reset all tabs to normal
    ui.applyThemeToComponent("settings_tab_graphics", UIComponentType::BUTTON);
    ui.applyThemeToComponent("settings_tab_audio", UIComponentType::BUTTON);
    ui.applyThemeToComponent("settings_tab_gameplay", UIComponentType::BUTTON);

    // Highlight active tab (using pressed color)
    switch (m_currentTab) {
        case SettingsTab::Graphics:
            ui.applyThemeToComponent("settings_tab_graphics", UIComponentType::BUTTON_SUCCESS);
            break;
        case SettingsTab::Audio:
            ui.applyThemeToComponent("settings_tab_audio", UIComponentType::BUTTON_SUCCESS);
            break;
        case SettingsTab::Gameplay:
            ui.applyThemeToComponent("settings_tab_gameplay", UIComponentType::BUTTON_SUCCESS);
            break;
    }
}

void SettingsMenuState::updateTabVisibility() {
    auto& ui = UIManager::Instance();

    // Hide all tabs first
    // Graphics
    ui.setComponentVisible("settings_vsync_label", false);
    ui.setComponentVisible("settings_vsync_checkbox", false);
    ui.setComponentVisible("settings_fullscreen_label", false);
    ui.setComponentVisible("settings_fullscreen_checkbox", false);
    ui.setComponentVisible("settings_showfps_label", false);
    ui.setComponentVisible("settings_showfps_checkbox", false);
    ui.setComponentVisible("settings_resolution_label", false);
    ui.setComponentVisible("settings_buffer_label", false);
    ui.setComponentVisible("settings_buffer_button", false);
    ui.setComponentVisible("settings_buffer_info", false);

    // Audio
    ui.setComponentVisible("settings_master_volume_label", false);
    ui.setComponentVisible("settings_master_volume_slider", false);
    ui.setComponentVisible("settings_master_volume_value", false);
    ui.setComponentVisible("settings_music_volume_label", false);
    ui.setComponentVisible("settings_music_volume_slider", false);
    ui.setComponentVisible("settings_music_volume_value", false);
    ui.setComponentVisible("settings_sfx_volume_label", false);
    ui.setComponentVisible("settings_sfx_volume_slider", false);
    ui.setComponentVisible("settings_sfx_volume_value", false);
    ui.setComponentVisible("settings_mute_label", false);
    ui.setComponentVisible("settings_mute_checkbox", false);

    // Gameplay
    ui.setComponentVisible("settings_difficulty_label", false);
    ui.setComponentVisible("settings_autosave_label", false);
    ui.setComponentVisible("settings_autosave_checkbox", false);
    ui.setComponentVisible("settings_autosave_interval_label", false);

    // Show only active tab
    switch (m_currentTab) {
        case SettingsTab::Graphics:
            ui.setComponentVisible("settings_vsync_label", true);
            ui.setComponentVisible("settings_vsync_checkbox", true);
            ui.setComponentVisible("settings_fullscreen_label", true);
            ui.setComponentVisible("settings_fullscreen_checkbox", true);
            ui.setComponentVisible("settings_showfps_label", true);
            ui.setComponentVisible("settings_showfps_checkbox", true);
            ui.setComponentVisible("settings_resolution_label", true);
            ui.setComponentVisible("settings_buffer_label", true);
            ui.setComponentVisible("settings_buffer_button", true);
            ui.setComponentVisible("settings_buffer_info", true);
            break;

        case SettingsTab::Audio:
            ui.setComponentVisible("settings_master_volume_label", true);
            ui.setComponentVisible("settings_master_volume_slider", true);
            ui.setComponentVisible("settings_master_volume_value", true);
            ui.setComponentVisible("settings_music_volume_label", true);
            ui.setComponentVisible("settings_music_volume_slider", true);
            ui.setComponentVisible("settings_music_volume_value", true);
            ui.setComponentVisible("settings_sfx_volume_label", true);
            ui.setComponentVisible("settings_sfx_volume_slider", true);
            ui.setComponentVisible("settings_sfx_volume_value", true);
            ui.setComponentVisible("settings_mute_label", true);
            ui.setComponentVisible("settings_mute_checkbox", true);
            break;

        case SettingsTab::Gameplay:
            ui.setComponentVisible("settings_difficulty_label", true);
            ui.setComponentVisible("settings_autosave_label", true);
            ui.setComponentVisible("settings_autosave_checkbox", true);
            ui.setComponentVisible("settings_autosave_interval_label", true);
            break;
    }
}
