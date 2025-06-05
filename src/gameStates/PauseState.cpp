/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/PauseState.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>

bool PauseState::enter() {
  std::cout << "Forge Game Engine - Entering PAUSE State\n";
  return true;
}

void PauseState::update([[maybe_unused]] float deltaTime) {
  //std::cout << "Updating PAUSE State\n";
  
  // Handle input with proper key press detection
  handleInput();
}

void PauseState::render() {
    // Cache manager references for better performance
    FontManager& fontMgr = FontManager::Instance();
    GameEngine& gameEngine = GameEngine::Instance();
    
    SDL_Color fontColor = {200, 200, 200, 255};//gray
     fontMgr.drawText(
       "Pause State Place Holder <----> Press R to Return to test Player",
       "fonts_Arial",
       gameEngine.getWindowWidth() / 2,     // Center horizontally
       20,
       fontColor,
       gameEngine.getRenderer());
}
bool PauseState::exit() {
  std::cout << "Forge Game Engine - Exiting PAUSE State\n";

  return true;
}

std::string PauseState::getName() const {
  return "PauseState";
}

void PauseState::handleInput() {
  InputManager& inputMgr = InputManager::Instance();
  
  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_R)) {
      // Flag the GamePlayState transition
      // We'll do the actual removal in GamePlayState::enter()
      GameEngine& gameEngine = GameEngine::Instance();
      gameEngine.getGameStateManager()->setState("GamePlayState");
  }
  
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      GameEngine& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}
