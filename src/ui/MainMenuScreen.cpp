/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ui/MainMenuScreen.hpp"
#include "core/GameEngine.hpp"
#include "managers/InputManager.hpp"

MainMenuScreen::MainMenuScreen() : UIScreen("MainMenuScreen") {
}

void MainMenuScreen::create() {
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Create main background panel
    createPanel(MAIN_PANEL, {0, 0, windowWidth, windowHeight});

    // Create title
    createLabel(TITLE_LABEL, {0, 100, windowWidth, 60}, "Forge Game Engine - Main Menu");

    // Create menu buttons
    int buttonWidth = 300;
    int buttonHeight = 50;
    int buttonSpacing = 20;
    int startY = windowHeight / 2 - 100;

    createButton(START_BUTTON, {0, startY, buttonWidth, buttonHeight}, "Start Game");
    createButton(AI_DEMO_BUTTON, {0, startY + (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "AI Demo");
    createButton(EVENT_DEMO_BUTTON, {0, startY + 2 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Event Demo");
    createButton(EXIT_BUTTON, {0, startY + 3 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Exit");

    // Setup layout and styling
    setupLayout();
    setupStyling();
    centerAllComponents();

    // Set up button callbacks
    auto& ui = getUIManager();
    
    ui.setOnClick(START_BUTTON, [this]() {
        if (m_onStartGame) {
            m_onStartGame();
        }
    });

    ui.setOnClick(AI_DEMO_BUTTON, [this]() {
        if (m_onAIDemo) {
            m_onAIDemo();
        }
    });

    ui.setOnClick(EVENT_DEMO_BUTTON, [this]() {
        if (m_onEventDemo) {
            m_onEventDemo();
        }
    });

    ui.setOnClick(EXIT_BUTTON, [this]() {
        if (m_onExit) {
            m_onExit();
        }
    });
}

void MainMenuScreen::update(float deltaTime) {
    UIScreen::update(deltaTime);

    // Handle keyboard shortcuts
    auto& inputManager = InputManager::Instance();
    
    if (inputManager.isKeyDown(SDL_SCANCODE_RETURN)) {
        if (m_onStartGame) {
            m_onStartGame();
        }
    }
    
    if (inputManager.isKeyDown(SDL_SCANCODE_A)) {
        if (m_onAIDemo) {
            m_onAIDemo();
        }
    }
    
    if (inputManager.isKeyDown(SDL_SCANCODE_E)) {
        if (m_onEventDemo) {
            m_onEventDemo();
        }
    }
    
    if (inputManager.isKeyDown(SDL_SCANCODE_ESCAPE)) {
        if (m_onExit) {
            m_onExit();
        }
    }
}

void MainMenuScreen::onButtonClicked(const std::string& buttonID) {
    if (buttonID == START_BUTTON && m_onStartGame) {
        m_onStartGame();
    } else if (buttonID == AI_DEMO_BUTTON && m_onAIDemo) {
        m_onAIDemo();
    } else if (buttonID == EVENT_DEMO_BUTTON && m_onEventDemo) {
        m_onEventDemo();
    } else if (buttonID == EXIT_BUTTON && m_onExit) {
        m_onExit();
    }
}

void MainMenuScreen::setupLayout() {
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    // Create a vertical stack layout for buttons
    createLayout(BUTTON_LAYOUT, UILayoutType::STACK, {windowWidth/2 - 150, windowHeight/2 - 100, 300, 300});
    
    // Add buttons to layout
    addToLayout(BUTTON_LAYOUT, START_BUTTON);
    addToLayout(BUTTON_LAYOUT, AI_DEMO_BUTTON);
    addToLayout(BUTTON_LAYOUT, EVENT_DEMO_BUTTON);
    addToLayout(BUTTON_LAYOUT, EXIT_BUTTON);

    auto& ui = getUIManager();
    ui.setLayoutSpacing(BUTTON_LAYOUT, 20);
}

void MainMenuScreen::setupStyling() {
    auto& ui = getUIManager();

    // Style the main panel
    UIStyle panelStyle;
    panelStyle.backgroundColor = {20, 25, 35, 200}; // Dark blue transparent
    panelStyle.borderWidth = 0;
    ui.setStyle(MAIN_PANEL, panelStyle);

    // Style the title
    UIStyle titleStyle;
    titleStyle.backgroundColor = {0, 0, 0, 0}; // Transparent
    titleStyle.textColor = {255, 215, 0, 255}; // Gold
    titleStyle.fontSize = 32;
    titleStyle.textAlign = UIAlignment::CENTER_CENTER;
    ui.setStyle(TITLE_LABEL, titleStyle);

    // Style buttons
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {70, 130, 180, 255}; // Steel blue
    buttonStyle.hoverColor = {100, 149, 237, 255}; // Cornflower blue  
    buttonStyle.pressedColor = {25, 25, 112, 255}; // Midnight blue
    buttonStyle.borderColor = {255, 255, 255, 255}; // White border
    buttonStyle.textColor = {255, 255, 255, 255}; // White text
    buttonStyle.borderWidth = 2;
    buttonStyle.padding = 10;
    buttonStyle.textAlign = UIAlignment::CENTER_CENTER;

    ui.setStyle(START_BUTTON, buttonStyle);
    ui.setStyle(AI_DEMO_BUTTON, buttonStyle);
    ui.setStyle(EVENT_DEMO_BUTTON, buttonStyle);
    
    // Make exit button red
    UIStyle exitStyle = buttonStyle;
    exitStyle.backgroundColor = {180, 70, 70, 255}; // Dark red
    exitStyle.hoverColor = {220, 100, 100, 255}; // Light red
    exitStyle.pressedColor = {120, 50, 50, 255}; // Darker red
    ui.setStyle(EXIT_BUTTON, exitStyle);
}

void MainMenuScreen::centerAllComponents() {
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getWindowWidth();
    int windowHeight = gameEngine.getWindowHeight();

    centerComponent(TITLE_LABEL, windowWidth, windowHeight);
    
    // Manually adjust title position to be higher
    auto& ui = getUIManager();
    UIRect titleBounds = ui.getBounds(TITLE_LABEL);
    titleBounds.y = 100;
    ui.setComponentBounds(TITLE_LABEL, titleBounds);
}