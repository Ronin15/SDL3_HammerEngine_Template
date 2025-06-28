/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/PauseState.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/UIManager.hpp"
#include "core/GameEngine.hpp"
#include <iostream>

bool PauseState::enter() {
  std::cout << "Hammer Game Engine - Entering PAUSE State\n";

  // Create pause state UI
  auto& gameEngine = GameEngine::Instance();
  auto& ui = UIManager::Instance();
  int windowWidth = gameEngine.getLogicalWidth();
  int windowHeight = gameEngine.getLogicalHeight();
  
  // Create overlay background to dim the game behind the pause menu
  ui.createOverlay(windowWidth, windowHeight);
  
  ui.createTitle("pause_title", {0, 100, windowWidth, 40}, "Game Paused");
  ui.setTitleAlignment("pause_title", UIAlignment::CENTER_CENTER);
  
  return true;
}

void PauseState::update([[maybe_unused]] float deltaTime) {
  //std::cout << "Updating PAUSE State\n";
}

void PauseState::render(float deltaTime) {
    // Cache manager references for better performance
    FontManager& fontMgr = FontManager::Instance();
    const auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    
    // Update and render UI components through UIManager using cached renderer for cleaner API
    if (!ui.isShutdown()) {
        ui.update(deltaTime);
    }
    ui.render();
    
    // Additional instruction text below title
    SDL_Color fontColor = {200, 200, 200, 255};//gray
     fontMgr.drawText(
       "Press R to Return to Game",
       "fonts_Arial",
       gameEngine.getLogicalWidth() / 2,     // Center horizontally
       160,
       fontColor,
       gameEngine.getRenderer());
}
bool PauseState::exit() {
  std::cout << "Hammer Game Engine - Exiting PAUSE State\n";

  // Clean up UI components using simplified method
  auto& ui = UIManager::Instance();
  ui.prepareForStateTransition();

  return true;
}

std::string PauseState::getName() const {
  return "PauseState";
}

void PauseState::handleInput() {
  const auto& inputMgr = InputManager::Instance();
  
  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_R)) {
      // Flag the GamePlayState transition
      // We'll do the actual removal in GamePlayState::enter()
      const auto& gameEngine = GameEngine::Instance();
      gameEngine.getGameStateManager()->setState("GamePlayState");
  }
  
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      auto& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}
