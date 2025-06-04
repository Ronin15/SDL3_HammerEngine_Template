/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MAIN_MENU_SCREEN_HPP
#define MAIN_MENU_SCREEN_HPP

#include "ui/UIScreen.hpp"
#include <functional>

class MainMenuScreen : public UIScreen {
public:
    MainMenuScreen();
    ~MainMenuScreen() override = default;

    // UIScreen interface
    void create() override;
    void update(float deltaTime) override;

    // Event callbacks
    void onButtonClicked(const std::string& buttonID) override;

    // Callback setters for state transitions
    void setOnStartGame(std::function<void()> callback) { m_onStartGame = callback; }
    void setOnAIDemo(std::function<void()> callback) { m_onAIDemo = callback; }
    void setOnEventDemo(std::function<void()> callback) { m_onEventDemo = callback; }
    void setOnExit(std::function<void()> callback) { m_onExit = callback; }

private:
    // Callback functions
    std::function<void()> m_onStartGame{};
    std::function<void()> m_onAIDemo{};
    std::function<void()> m_onEventDemo{};
    std::function<void()> m_onExit{};

    // Component IDs
    static constexpr const char* TITLE_LABEL = "main_menu_title";
    static constexpr const char* START_BUTTON = "start_game_btn";
    static constexpr const char* AI_DEMO_BUTTON = "ai_demo_btn";
    static constexpr const char* EVENT_DEMO_BUTTON = "event_demo_btn";
    static constexpr const char* EXIT_BUTTON = "exit_btn";
    static constexpr const char* MAIN_PANEL = "main_menu_panel";
    static constexpr const char* BUTTON_LAYOUT = "menu_button_layout";

    // Layout helpers
    void setupLayout();
    void setupStyling();
    void centerAllComponents();
};

#endif // MAIN_MENU_SCREEN_HPP