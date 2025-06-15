/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/MainMenuState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State\n";
  
  auto& gameEngine = GameEngine::Instance();
  auto& ui = UIManager::Instance();
  int windowWidth = gameEngine.getWindowWidth();
  int windowHeight = gameEngine.getWindowHeight();

  // No overlay needed for main menu - keep clean appearance

  // Create title
  ui.createTitle("mainmenu_title", {0, 100, windowWidth, 60}, "Forge Game Engine - Main Menu");
  ui.setTitleAlignment("mainmenu_title", UIAlignment::CENTER_CENTER);

  // Create menu buttons
  int buttonWidth = 300;
  int buttonHeight = 50;
  int buttonSpacing = 20;
  int startY = windowHeight / 2 - 100;

  ui.createButton("mainmenu_start_game_btn", {windowWidth/2 - buttonWidth/2, startY, buttonWidth, buttonHeight}, "Start Game");
  ui.createButton("mainmenu_ai_demo_btn", {windowWidth/2 - buttonWidth/2, startY + (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "AI Demo");
  ui.createButton("mainmenu_advanced_ai_demo_btn", {windowWidth/2 - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Advanced AI Demo");
  ui.createButton("mainmenu_event_demo_btn", {windowWidth/2 - buttonWidth/2, startY + 3 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Event Demo");
  ui.createButton("mainmenu_ui_example_btn", {windowWidth/2 - buttonWidth/2, startY + 4 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "UI Example");
  ui.createButton("mainmenu_overlay_demo_btn", {windowWidth/2 - buttonWidth/2, startY + 5 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Overlay Demo");
  ui.createButtonDanger("mainmenu_exit_btn", {windowWidth/2 - buttonWidth/2, startY + 6 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Exit");

  // Set up button callbacks
  ui.setOnClick("mainmenu_start_game_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("GamePlayState");
  });

  ui.setOnClick("mainmenu_ai_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("AIDemo");
  });

  ui.setOnClick("mainmenu_advanced_ai_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("AdvancedAIDemo");
  });

  ui.setOnClick("mainmenu_event_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("EventDemo");
  });

  ui.setOnClick("mainmenu_ui_example_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("UIExampleState");
  });

  ui.setOnClick("mainmenu_overlay_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->setState("OverlayDemoState");
  });

  ui.setOnClick("mainmenu_exit_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    gameEngine.setRunning(false);
  });


  
  return true;
}

void MainMenuState::update([[maybe_unused]] float deltaTime) {
  // UI updates handled in render() for thread safety
}

void MainMenuState::render(float deltaTime) {
  // Update and render UI components through UIManager using cached renderer for cleaner API
  auto& ui = UIManager::Instance();
  if (!ui.isShutdown()) {
      ui.update(deltaTime); // Use actual deltaTime from update cycle
  }
  ui.render(); // Uses cached renderer from GameEngine
}

bool MainMenuState::exit() {
  std::cout << "Forge Game Engine - Exiting MAIN MENU State\n";
  
  // Clean up UI components using simplified method
  auto& ui = UIManager::Instance();
  ui.prepareForStateTransition();
  
  return true;
}

void MainMenuState::handleInput() {
  auto& inputManager = InputManager::Instance();

  // Keyboard shortcuts for quick navigation
  if (inputManager.wasKeyPressed(SDL_SCANCODE_RETURN)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->setState("GamePlayState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_A)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->setState("AIDemo");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_E)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->setState("EventDemo");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_U)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->setState("UIExampleState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_O)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->setState("OverlayDemoState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      auto& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
