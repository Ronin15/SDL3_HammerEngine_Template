/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "MainMenuState.hpp"
#include "InputHandler.hpp"
#include "FontManager.hpp"
#include "GameEngine.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State\n";
  return true;
}

void MainMenuState::update() {
  //std::cout << "Updating Main Menu State\n";

      // Handle Play (enter) and ESC key.
      if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_RETURN)) {
          GameEngine::Instance().getGameStateManager()->setState("GamePlayState");
      }
      if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_ESCAPE)) {
          GameEngine::Instance().setRunning(false);
      }
  }

void MainMenuState::render() {
   SDL_Color fontColor = {200, 200, 200, 255};//Gray
    FontManager::Instance().drawText(
      "Main Menu State Place Holder <----> Press Enter to Render test Player",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 180,
      fontColor,
      GameEngine::Instance().getRenderer());
}
bool MainMenuState::exit() {
  std::cout << "Forge Game Engine - Exiting MAIN MENU State\n";
  return true;
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
