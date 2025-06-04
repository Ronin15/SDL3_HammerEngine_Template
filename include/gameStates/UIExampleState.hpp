/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef UI_EXAMPLE_STATE_HPP
#define UI_EXAMPLE_STATE_HPP

#include "gameStates/GameState.hpp"
#include "ui/UIScreen.hpp"
#include <memory>

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
    std::unique_ptr<UIScreen> m_uiScreen{nullptr};
    
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
    void setupUI();
    void handleSliderChange(float value);
    void handleCheckboxToggle();
    void handleInputChange(const std::string& text);
    void handleListSelection();
    void handleAnimation();
    void handleThemeChange();
    void updateProgressBar(float deltaTime);
    
    // Animation and theme state
    bool m_darkTheme{false};
    float m_progressValue{0.0f};
    bool m_progressIncreasing{true};
};

// Custom UIScreen for this example
class UIExampleScreen : public UIScreen {
public:
    UIExampleScreen();
    ~UIExampleScreen() override = default;

    // UIScreen interface
    void create() override;
    void update(float deltaTime) override;
    void onButtonClicked(const std::string& buttonID) override;
    void onValueChanged(const std::string& componentID, float value) override;
    void onTextChanged(const std::string& componentID, const std::string& text) override;

    // Callback setters for parent state
    void setOnBack(std::function<void()> callback) { m_onBack = callback; }
    void setOnSliderChanged(std::function<void(float)> callback) { m_onSliderChanged = callback; }
    void setOnCheckboxToggled(std::function<void()> callback) { m_onCheckboxToggled = callback; }
    void setOnInputChanged(std::function<void(const std::string&)> callback) { m_onInputChanged = callback; }
    void setOnListSelected(std::function<void()> callback) { m_onListSelected = callback; }
    void setOnAnimate(std::function<void()> callback) { m_onAnimate = callback; }
    void setOnThemeChange(std::function<void()> callback) { m_onThemeChange = callback; }

    // Update methods
    void updateSliderLabel(float value);
    void updateProgressBar(float value);
    void updateInputLabel(const std::string& text);
    void applyDarkTheme(bool dark);

private:
    // Callbacks
    std::function<void()> m_onBack{};
    std::function<void(float)> m_onSliderChanged{};
    std::function<void()> m_onCheckboxToggled{};
    std::function<void(const std::string&)> m_onInputChanged{};
    std::function<void()> m_onListSelected{};
    std::function<void()> m_onAnimate{};
    std::function<void()> m_onThemeChange{};

    void setupLayout();
    void setupComponents();
    void setupStyling();
    void populateList();
};

#endif // UI_EXAMPLE_STATE_HPP