/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/MainMenuState.hpp"
#include "managers/UIManager.hpp"
#include "core/GameEngine.hpp"
#include "ui/MainMenuScreen.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State\n";
  
  // Create and show the main menu screen
  m_uiScreen = std::make_unique<MainMenuScreen>();
  m_uiScreen->show();
  
  // Set up callbacks
  auto screen = static_cast<MainMenuScreen*>(m_uiScreen.get());
  screen->setOnStartGame([]() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("GamePlayState");
  });
  
  screen->setOnAIDemo([]() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("AIDemo");
  });
  
  screen->setOnEventDemo([]() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("EventDemo");
  });
  
  screen->setOnUIExample([]() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("UIExampleState");
  });
  
  screen->setOnOverlayDemo([]() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("OverlayDemoState");
  });
  
  screen->setOnExit([]() {
    auto& gameEngine = GameEngine::Instance();
    gameEngine.setRunning(false);
  });
  
  return true;
}

void MainMenuState::update(float deltaTime) {
  // Update UI Manager
  auto& uiManager = UIManager::Instance();
  if (!uiManager.isShutdown()) {
      uiManager.update(deltaTime);
  }
  
  if (m_uiScreen) {
      m_uiScreen->update(deltaTime);
  }
}

void MainMenuState::render() {
  // Render UI components through UIManager
  auto& gameEngine = GameEngine::Instance();
  auto& ui = UIManager::Instance();
  ui.render(gameEngine.getRenderer());
}
bool MainMenuState::exit() {
  std::cout << "Forge Game Engine - Exiting MAIN MENU State\n";
  
  if (m_uiScreen) {
      m_uiScreen->hide();
  }
  
  return true;
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
