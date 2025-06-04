/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/MainMenuState.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State\n";
  return true;
}

void MainMenuState::update([[maybe_unused]] float deltaTime) {
  //std::cout << "Updating Main Menu State\n";

      // Cache manager references for better performance
      InputManager& inputMgr = InputManager::Instance();
      GameEngine& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();

      // Handle menu options
      if (inputMgr.isKeyDown(SDL_SCANCODE_RETURN)) {
          gameStateManager->setState("GamePlayState");
      }
      if (inputMgr.isKeyDown(SDL_SCANCODE_A)) {
          gameStateManager->setState("AIDemo");
      }
      if (inputMgr.isKeyDown(SDL_SCANCODE_E)) {
          gameStateManager->setState("EventDemo");
      }
      if (inputMgr.isKeyDown(SDL_SCANCODE_U)) {
          gameStateManager->setState("UIExampleState");
      }
      if (inputMgr.isKeyDown(SDL_SCANCODE_ESCAPE)) {
          gameEngine.setRunning(false);
      }
  }

void MainMenuState::render() {
   // Cache manager references for better performance
   FontManager& fontMgr = FontManager::Instance();
   GameEngine& gameEngine = GameEngine::Instance();
   SDL_Renderer* renderer = gameEngine.getRenderer();
   int windowWidth = gameEngine.getWindowWidth();
   int windowHeight = gameEngine.getWindowHeight();

   SDL_Color fontColor = {200, 200, 200, 255};//Gray
    // Title
    fontMgr.drawText(
      "Main Menu",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) - 200,
      fontColor,
      renderer);

    // Menu options
    fontMgr.drawText(
      "Press ENTER - Start Game",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) - 120,
      fontColor,
      renderer);

    fontMgr.drawText(
      "Press A - AI Demo",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) - 70,
      fontColor,
      renderer);

    fontMgr.drawText(
      "Press E - Event Demo",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) - 20,
      fontColor,
      renderer);

    fontMgr.drawText(
      "Press U - UI Demo",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) + 30,
      fontColor,
      renderer);

    fontMgr.drawText(
      "Press ESC - Exit",
      "fonts_Arial",
      windowWidth / 2,     // Center horizontally
      (windowHeight / 2) + 80,
      fontColor,
      renderer);
}
bool MainMenuState::exit() {
  std::cout << "Forge Game Engine - Exiting MAIN MENU State\n";
  return true;
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
