/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/MainMenuState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

bool MainMenuState::enter() {
  std::cout << "Hammer Game Engine - Entering MAIN MENU State\n";

  auto& ui = UIManager::Instance();
  auto& fontMgr = FontManager::Instance();

  // Wait briefly for fonts to be loaded before creating UI components.
  // Avoid unbounded waits on macOS where early resize/display events can trigger
  // a reload loop. Proceed after a short timeout and let UI relayout when fonts finish.
  constexpr int kMaxWaitMs = 1500; // 1.5s max wait
  int waitedMs = 0;
  while (!fontMgr.areFontsLoaded() && waitedMs < kMaxWaitMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    waitedMs += 1;
  }

  // No overlay needed for main menu - keep clean appearance

  // Create title using auto-positioning
  ui.createTitleAtTop("mainmenu_title", "Hammer Game Engine - Main Menu", 60);

  // Create menu buttons
  int buttonWidth = 300;
  int buttonHeight = 50;
  int buttonSpacing = 20;
  int startY = ui.getLogicalHeight() / 2 - 100;

  ui.createButton("mainmenu_start_game_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY, buttonWidth, buttonHeight}, "Start Game");
  ui.createButton("mainmenu_ai_demo_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "AI Demo");
  ui.createButton("mainmenu_advanced_ai_demo_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + 2 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Advanced AI Demo");
  ui.createButton("mainmenu_event_demo_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + 3 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Event Demo");
  ui.createButton("mainmenu_ui_example_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + 4 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "UI Demo");
  ui.createButton("mainmenu_overlay_demo_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + 5 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Overlay Demo");
  ui.createButtonDanger("mainmenu_exit_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, startY + 6 * (buttonHeight + buttonSpacing), buttonWidth, buttonHeight}, "Exit");

  // Set up button callbacks
  ui.setOnClick("mainmenu_start_game_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("GamePlayState");
  });

  ui.setOnClick("mainmenu_ai_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("AIDemo");
  });

  ui.setOnClick("mainmenu_advanced_ai_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("AdvancedAIDemo");
  });

  ui.setOnClick("mainmenu_event_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("EventDemo");
  });

  ui.setOnClick("mainmenu_ui_example_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("UIExampleState");
  });

  ui.setOnClick("mainmenu_overlay_demo_btn", []() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    gameStateManager->changeState("OverlayDemoState");
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

void MainMenuState::render() {
  // Update and render UI components through UIManager using cached renderer for cleaner API
  auto& ui = UIManager::Instance();
  if (!ui.isShutdown()) {
      ui.update(0.0); // UI updates are not time-dependent in this state
  }
  ui.render(); // Uses cached renderer from GameEngine
}

bool MainMenuState::exit() {
  std::cout << "Hammer Game Engine - Exiting MAIN MENU State\n";

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
      gameStateManager->changeState("GamePlayState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_A)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->changeState("AIDemo");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_E)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->changeState("EventDemo");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_U)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->changeState("UIExampleState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_O)) {
      auto& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      gameStateManager->changeState("OverlayDemoState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      auto& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
