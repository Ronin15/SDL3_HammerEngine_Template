/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/PauseState.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/UIManager.hpp"
#include "core/GameEngine.hpp"

bool PauseState::enter() {
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
}

void PauseState::render() {
    // Cache manager references for better performance
    FontManager& fontMgr = FontManager::Instance();
    const auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    
    // Update and render UI components through UIManager using cached renderer for cleaner API
    if (!ui.isShutdown()) {
        ui.update(0.0); // UI updates are not time-dependent in this state
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
  // Only clean up PauseState-specific UI components
  // Do NOT use prepareForStateTransition() as it would clear GamePlayState's preserved UI
  auto& ui = UIManager::Instance();
  ui.removeComponent("pause_title");
  ui.removeOverlay(); // Remove the pause overlay to restore GamePlayState visibility

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
      gameEngine.getGameStateManager()->popState();
  }
  
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      auto& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}
