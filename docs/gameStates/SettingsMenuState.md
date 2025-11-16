# SettingsMenuState Documentation

## Overview

SettingsMenuState is a tab-based settings UI implementation for the Hammer Engine that provides Apply/Cancel functionality for modifying game settings. Changes are stored in temporary local state and only written to SettingsManager when the user clicks "Apply", allowing users to preview settings without commitment.

## Architecture

### Design Patterns
- **Temporary State Pattern**: Changes staged in local `TempSettings` struct
- **Apply/Cancel Workflow**: Standard UI pattern for settings dialogs
- **Tab-Based UI**: Organized by category (Graphics, Audio, Gameplay)
- **Reactive UI**: Sliders and checkboxes update temp state immediately

### Core Components

#### TempSettings Struct
```cpp
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
```

#### Settings Tabs
```cpp
enum class SettingsTab {
    Graphics,  // VSync, fullscreen, FPS display
    Audio,     // Volume sliders, mute
    Gameplay   // Difficulty, autosave options
};
```

## User Workflow

### Flow Diagram
```
Enter State → Load Settings → Display UI (Graphics Tab)
                                        ↓
                User Interacts (modify settings)
                                        ↓
                    ┌──────────────┬────────────────┐
                    ↓              ↓                ↓
              Apply Button    Back Button    ESC Key
                    ↓              ↓                ↓
            Save to Disk     Discard Changes   Discard Changes
                    ↓              ↓                ↓
            Main Menu State  Main Menu State  Main Menu State
```

### Button Behavior
- **Apply**: Save temp settings to SettingsManager, persist to disk, apply immediately
- **Back**: Discard temp settings, return to main menu without saving
- **ESC**: Same as Back button

## GameState Interface Implementation

### `bool enter()`
Initializes settings menu UI.
- **Font Wait**: Waits up to 1500ms for fonts to load
- **Load Current**: Populates `m_tempSettings` from SettingsManager
- **Create UI**: Tab buttons, settings controls, action buttons
- **Default Tab**: Shows Graphics tab initially

### `void update(float deltaTime)`
Empty implementation - UI updates handled in `render()` for thread safety.

### `void render()`
Renders settings menu UI.
```cpp
void render() {
    auto& ui = UIManager::Instance();
    if (!ui.isShutdown()) {
        ui.update(0.0);
    }
    ui.render();
}
```

### `void handleInput()`
Handles keyboard shortcuts.
- **ESC**: Exit without saving
- **1**: Switch to Graphics tab
- **2**: Switch to Audio tab
- **3**: Switch to Gameplay tab

### `bool exit()`
Cleans up UI on state exit.
```cpp
bool exit() {
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition();
    return true;
}
```

### `std::string getName() const`
Returns `"SettingsMenuState"`.

## Settings Operations

### `void loadCurrentSettings()`
Loads current settings from SettingsManager into temporary local state.
```cpp
void loadCurrentSettings() {
    auto& settings = SettingsManager::Instance();

    // Graphics
    m_tempSettings.resolutionWidth = settings.get<int>("graphics", "resolution_width", 1920);
    m_tempSettings.resolutionHeight = settings.get<int>("graphics", "resolution_height", 1080);
    m_tempSettings.fullscreen = settings.get<bool>("graphics", "fullscreen", false);
    // ... more settings

    // Audio
    m_tempSettings.masterVolume = settings.get<float>("audio", "master_volume", 1.0f);
    // ... more settings

    // Gameplay
    m_tempSettings.difficulty = settings.get<std::string>("gameplay", "difficulty", "normal");
    // ... more settings
}
```

### `void applySettings()`
Writes temporary settings to SettingsManager and saves to disk.
```cpp
void applySettings() {
    auto& settings = SettingsManager::Instance();
    auto& gameEngine = GameEngine::Instance();

    // Write all temp settings to SettingsManager
    settings.set("graphics", "resolution_width", m_tempSettings.resolutionWidth);
    settings.set("graphics", "fullscreen", m_tempSettings.fullscreen);
    // ... all other settings

    // Apply fullscreen immediately (triggers SDL resize event)
    if (gameEngine.isFullscreen() != m_tempSettings.fullscreen) {
        gameEngine.setFullscreen(m_tempSettings.fullscreen);
    }

    // Apply VSync immediately
    gameEngine.setVSyncEnabled(m_tempSettings.vsync);

    // Save to disk
    settings.saveToFile("res/settings.json");
}
```

### `void revertSettings()`
Reloads settings from SettingsManager, discarding temporary changes.
```cpp
void revertSettings() {
    loadCurrentSettings();

    // Update UI to reflect reverted values
    auto& ui = UIManager::Instance();
    ui.setChecked("settings_vsync_checkbox", m_tempSettings.vsync);
    ui.setValue("settings_master_volume_slider", m_tempSettings.masterVolume);
    // ... update all UI components
}
```

## UI Creation

### Tab Buttons

#### `void createTabButtons()`
Creates horizontal tab buttons at top of settings area.
```cpp
void createTabButtons() {
    auto& ui = UIManager::Instance();

    int tabWidth = 200;
    int tabHeight = 40;
    int tabY = 80;

    // Three centered buttons
    ui.createButton("settings_tab_graphics", {...}, "Graphics (1)");
    ui.createButton("settings_tab_audio", {...}, "Audio (2)");
    ui.createButton("settings_tab_gameplay", {...}, "Gameplay (3)");

    // Tab callbacks switch active tab
    ui.setOnClick("settings_tab_graphics", [this]() {
        switchTab(SettingsTab::Graphics);
    });
    // ... other tabs
}
```

### Graphics Settings

#### `void createGraphicsUI()`
Creates graphics settings controls (checkboxes, labels).
```cpp
void createGraphicsUI() {
    auto& ui = UIManager::Instance();

    // VSync checkbox
    ui.createLabel("settings_vsync_label", {...}, "VSync:");
    ui.createCheckbox("settings_vsync_checkbox", {...}, "Enabled");
    ui.setChecked("settings_vsync_checkbox", m_tempSettings.vsync);
    ui.setOnClick("settings_vsync_checkbox", [this]() {
        m_tempSettings.vsync = ui.getChecked("settings_vsync_checkbox");
    });

    // Fullscreen checkbox
    ui.createLabel("settings_fullscreen_label", {...}, "Fullscreen:");
    ui.createCheckbox("settings_fullscreen_checkbox", {...}, "Enabled");
    // ... similar pattern

    // Show FPS checkbox
    // Resolution label (read-only display)
}
```

### Audio Settings

#### `void createAudioUI()`
Creates audio settings controls (sliders, checkbox).
```cpp
void createAudioUI() {
    auto& ui = UIManager::Instance();

    // Master Volume slider (0.0 - 1.0)
    ui.createLabel("settings_master_volume_label", {...}, "Master Volume:");
    ui.createSlider("settings_master_volume_slider", {...}, 0.0f, 1.0f);
    ui.setValue("settings_master_volume_slider", m_tempSettings.masterVolume);
    ui.setOnValueChanged("settings_master_volume_slider", [this](float value) {
        m_tempSettings.masterVolume = value;
        // Update percentage label
        ui.setText("settings_master_volume_value", std::to_string((int)(value * 100)) + "%");
    });
    ui.createLabel("settings_master_volume_value", {...}, "100%");

    // Music Volume slider
    // SFX Volume slider
    // Mute checkbox

    // Hide all audio UI by default (tab switching controls visibility)
    ui.setComponentVisible("settings_master_volume_label", false);
    // ... hide all audio components
}
```

### Gameplay Settings

#### `void createGameplayUI()`
Creates gameplay settings controls.
```cpp
void createGameplayUI() {
    auto& ui = UIManager::Instance();

    // Difficulty label (read-only for now)
    ui.createLabel("settings_difficulty_label", {...},
                   "Difficulty: " + m_tempSettings.difficulty);

    // Autosave checkbox
    ui.createLabel("settings_autosave_label", {...}, "Autosave:");
    ui.createCheckbox("settings_autosave_checkbox", {...}, "Enabled");
    ui.setOnClick("settings_autosave_checkbox", [this]() {
        m_tempSettings.autosaveEnabled = ui.getChecked("settings_autosave_checkbox");
    });

    // Autosave interval label (read-only)
    ui.createLabel("settings_autosave_interval_label", {...},
                   "Autosave Interval: " + std::to_string(m_tempSettings.autosaveInterval) + " seconds");

    // Hide all gameplay UI by default
    ui.setComponentVisible("settings_difficulty_label", false);
    // ... hide all gameplay components
}
```

### Action Buttons

#### `void createActionButtons()`
Creates Apply and Back buttons at bottom of screen.
```cpp
void createActionButtons() {
    auto& ui = UIManager::Instance();

    int buttonWidth = 150;
    int buttonHeight = 50;
    int bottomOffset = 80;

    // Apply button (Success green) - bottom left of center
    ui.createButtonSuccess("settings_apply_btn", {...}, "Apply");
    ui.setComponentPositioning("settings_apply_btn", {UIPositionMode::BOTTOM_CENTERED, ...});
    ui.setOnClick("settings_apply_btn", [this]() {
        applySettings();
    });

    // Back button (Danger red) - bottom right of center
    ui.createButtonDanger("settings_back_btn", {...}, "Back");
    ui.setComponentPositioning("settings_back_btn", {UIPositionMode::BOTTOM_CENTERED, ...});
    ui.setOnClick("settings_back_btn", []() {
        // Return to main menu without saving
        gameStateManager->changeState("MainMenuState");
    });
}
```

## Tab Management

### `void switchTab(SettingsTab tab)`
Switches active tab and updates UI highlighting.
```cpp
void switchTab(SettingsTab tab) {
    m_currentTab = tab;
    updateTabVisibility();

    // Update tab button colors
    auto& ui = UIManager::Instance();

    // Reset all tabs to normal theme
    ui.applyThemeToComponent("settings_tab_graphics", UIComponentType::BUTTON);
    ui.applyThemeToComponent("settings_tab_audio", UIComponentType::BUTTON);
    ui.applyThemeToComponent("settings_tab_gameplay", UIComponentType::BUTTON);

    // Highlight active tab with success theme
    switch (m_currentTab) {
        case SettingsTab::Graphics:
            ui.applyThemeToComponent("settings_tab_graphics", UIComponentType::BUTTON_SUCCESS);
            break;
        // ... other tabs
    }
}
```

### `void updateTabVisibility()`
Shows only the active tab's UI components.
```cpp
void updateTabVisibility() {
    auto& ui = UIManager::Instance();

    // Hide all tabs
    ui.setComponentVisible("settings_vsync_label", false);
    ui.setComponentVisible("settings_vsync_checkbox", false);
    // ... hide all components from all tabs

    // Show only active tab's components
    switch (m_currentTab) {
        case SettingsTab::Graphics:
            ui.setComponentVisible("settings_vsync_label", true);
            ui.setComponentVisible("settings_vsync_checkbox", true);
            // ... show all graphics components
            break;

        case SettingsTab::Audio:
            ui.setComponentVisible("settings_master_volume_label", true);
            // ... show all audio components
            break;

        case SettingsTab::Gameplay:
            ui.setComponentVisible("settings_difficulty_label", true);
            // ... show all gameplay components
            break;
    }
}
```

## Integration Example

### MainMenuState Integration
```cpp
// In MainMenuState - create Settings button
void MainMenuState::enter() {
    auto& ui = UIManager::Instance();

    // Create settings button
    ui.createButton("main_menu_settings_btn", {...}, "Settings");
    ui.setOnClick("main_menu_settings_btn", []() {
        auto& gameEngine = GameEngine::Instance();
        auto* gameStateManager = gameEngine.getGameStateManager();
        gameStateManager->changeState("SettingsMenuState");
    });
}
```

### GameStateManager Registration
```cpp
// In GameEngine or HammerMain - register state
gameStateManager->registerState("SettingsMenuState", std::make_shared<SettingsMenuState>());
```

## UI Layout

### Visual Structure
```
┌─────────────────────────────────────────────────────┐
│                     Settings                         │  ← Title
│                                                      │
│   [Graphics (1)]  [Audio (2)]  [Gameplay (3)]      │  ← Tab Buttons
│                                                      │
│   ┌──────────────────────────────────────────┐     │
│   │ VSync:         [✓] Enabled               │     │
│   │ Fullscreen:    [ ] Enabled               │     │  ← Settings
│   │ Show FPS:      [ ] Enabled               │     │     Controls
│   │ Resolution: 1920x1080                    │     │     (active tab)
│   └──────────────────────────────────────────┘     │
│                                                      │
│                                                      │
│                                                      │
│                [Apply]        [Back]                │  ← Action Buttons
└─────────────────────────────────────────────────────┘
```

## Performance Considerations

### Temporary State Benefits
- **No Premature Writes**: Settings only written on Apply click
- **Fast Cancel**: Discarding changes is instant (no I/O)
- **Preview Support**: Could add live preview without persistence

### UI Update Patterns
```cpp
// BAD: Updating SettingsManager on every slider change
ui.setOnValueChanged("volume_slider", [](float value) {
    settings.set("audio", "master_volume", value); // Writes every frame!
    settings.saveToFile("res/settings.json");      // Disk I/O every frame!
});

// GOOD: Update temp state, write on Apply
ui.setOnValueChanged("volume_slider", [this](float value) {
    m_tempSettings.masterVolume = value; // Fast, in-memory only
});
```

### Best Practices
1. **Temp State Pattern**: Always stage changes in local struct
2. **Single Save**: Write to disk once on Apply, not on every change
3. **Immediate Apply**: Apply critical settings (fullscreen, vsync) immediately after save
4. **Revert on Cancel**: Load fresh from SettingsManager to discard changes

## Common Patterns

### Adding New Setting

#### 1. Add to TempSettings struct
```cpp
struct TempSettings {
    // ... existing settings
    float mouseSensitivity = 1.0f; // New setting
};
```

#### 2. Load in loadCurrentSettings()
```cpp
void loadCurrentSettings() {
    // ... existing loads
    m_tempSettings.mouseSensitivity = settings.get<float>("input", "mouse_sensitivity", 1.0f);
}
```

#### 3. Save in applySettings()
```cpp
void applySettings() {
    // ... existing saves
    settings.set("input", "mouse_sensitivity", m_tempSettings.mouseSensitivity);
}
```

#### 4. Create UI control
```cpp
void createGameplayUI() {
    // ... existing UI

    // Mouse sensitivity slider
    ui.createLabel("settings_mouse_sensitivity_label", {...}, "Mouse Sensitivity:");
    ui.createSlider("settings_mouse_sensitivity_slider", {...}, 0.5f, 2.0f);
    ui.setValue("settings_mouse_sensitivity_slider", m_tempSettings.mouseSensitivity);
    ui.setOnValueChanged("settings_mouse_sensitivity_slider", [this](float value) {
        m_tempSettings.mouseSensitivity = value;
    });
}
```

#### 5. Add to visibility management
```cpp
void updateTabVisibility() {
    // ... hide in "hide all" section
    ui.setComponentVisible("settings_mouse_sensitivity_label", false);
    ui.setComponentVisible("settings_mouse_sensitivity_slider", false);

    // ... show in appropriate tab's "show" section
    case SettingsTab::Gameplay:
        ui.setComponentVisible("settings_mouse_sensitivity_label", true);
        ui.setComponentVisible("settings_mouse_sensitivity_slider", true);
        // ...
}
```

### Adding New Tab
```cpp
// 1. Add to enum
enum class SettingsTab {
    Graphics,
    Audio,
    Gameplay,
    Controls  // New tab
};

// 2. Create tab button
ui.createButton("settings_tab_controls", {...}, "Controls (4)");
ui.setOnClick("settings_tab_controls", [this]() {
    switchTab(SettingsTab::Controls);
});

// 3. Create UI creation function
void createControlsUI() {
    // ... create controls settings UI
}

// 4. Update switchTab() theme application
case SettingsTab::Controls:
    ui.applyThemeToComponent("settings_tab_controls", UIComponentType::BUTTON_SUCCESS);
    break;

// 5. Update updateTabVisibility()
case SettingsTab::Controls:
    ui.setComponentVisible("settings_controls_...", true);
    break;

// 6. Handle keyboard shortcut in handleInput()
if (inputManager.wasKeyPressed(SDL_SCANCODE_4)) {
    switchTab(SettingsTab::Controls);
}
```

## See Also

- [SettingsManager Documentation](../managers/SettingsManager.md) - Settings storage and persistence
- [UIManager Guide](../ui/UIManager_Guide.md) - UI component creation and theming
- [GameStateManager Documentation](../managers/GameStateManager.md) - State management system
- [InputManager Documentation](../managers/InputManager.md) - Keyboard/mouse input handling
