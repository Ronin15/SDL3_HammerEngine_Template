/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "states/MainMenuState.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State\n";
  return true;
}

void MainMenuState::update() {
  //std::cout << "Updating Main Menu State\n";

      // Handle menu options
      if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_RETURN)) {
          GameEngine::Instance().getGameStateManager()->setState("GamePlayState");
      }
      if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_A)) {
          GameEngine::Instance().getGameStateManager()->setState("AIDemo");
      }
      if (InputHandler::Instance().isKeyDown(SDL_SCANCODE_ESCAPE)) {
          GameEngine::Instance().setRunning(false);
      }
  }

void MainMenuState::render() {
   SDL_Color fontColor = {200, 200, 200, 255};//Gray
    // Title
    FontManager::Instance().drawText(
      "Main Menu",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 200,
      fontColor,
      GameEngine::Instance().getRenderer());

    // Menu options
    FontManager::Instance().drawText(
      "Press ENTER - Start Game",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 120,
      fontColor,
      GameEngine::Instance().getRenderer());

    FontManager::Instance().drawText(
      "Press A - AI Demo",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 70,
      fontColor,
      GameEngine::Instance().getRenderer());

    FontManager::Instance().drawText(
      "Press ESC - Exit",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,     // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 20,
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
