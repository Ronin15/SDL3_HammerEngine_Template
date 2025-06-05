/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_EXAMPLE_STATE_HPP
#define UI_EXAMPLE_STATE_HPP

#include "gameStates/GameState.hpp"

// Example GameState that demonstrates comprehensive UIManager usage
class UIExampleState : public GameState {
public:
    UIExampleState();
    ~UIExampleState() override = default;

    // GameState interface
    bool enter() override;
    void update(float deltaTime) override;
    void render() override;
    bool exit() override;
    std::string getName() const override { return "UIExampleState"; }

private:
    // Demo state variables
    float m_sliderValue{0.5f};
    bool m_checkboxValue{false};
    int m_selectedListItem{-1};
    std::string m_inputText{};
    
    // Component IDs
    static constexpr const char* BACK_BUTTON = "back_btn";
    static constexpr const char* TITLE_LABEL = "title_label";
    static constexpr const char* SLIDER_COMPONENT = "demo_slider";
    static constexpr const char* SLIDER_LABEL = "slider_label";
    static constexpr const char* CHECKBOX_COMPONENT = "demo_checkbox";
    static constexpr const char* INPUT_FIELD = "demo_input";
    static constexpr const char* INPUT_LABEL = "input_label";
    static constexpr const char* LIST_COMPONENT = "demo_list";
    static constexpr const char* PROGRESS_BAR = "demo_progress";
    static constexpr const char* PROGRESS_LABEL = "progress_label";
    static constexpr const char* ANIMATION_BUTTON = "animate_btn";
    static constexpr const char* THEME_BUTTON = "theme_btn";
    static constexpr const char* MAIN_PANEL = "main_panel";
    
    // Helper methods
    void handleSliderChange(float value);
    void handleCheckboxToggle();
    void handleInputChange(const std::string& text);
    void handleListSelection();
    void handleAnimation();
    void handleThemeChange();
    void updateProgressBar(float deltaTime);
    void updateSliderLabel(float value);
    void updateInputLabel(const std::string& text);
    void applyDarkTheme(bool dark);
    
    // Animation and theme state
    bool m_darkTheme{false};
    float m_progressValue{0.0f};
    bool m_progressIncreasing{true};
};

#endif // UI_EXAMPLE_STATE_HPP