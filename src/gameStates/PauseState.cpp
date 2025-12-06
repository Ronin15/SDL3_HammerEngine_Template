/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/PauseState.hpp"
#include "managers/InputManager.hpp"
#include "managers/UIManager.hpp"
#include "managers/UIConstants.hpp"
#include "core/GameEngine.hpp"
#include "core/GameTime.hpp"

bool PauseState::enter() {
  // Pause game time while in pause state
  GameTime::Instance().pause();

  // Create pause state UI
  auto& gameEngine = GameEngine::Instance();
  auto& ui = UIManager::Instance();
  int windowWidth = gameEngine.getLogicalWidth();
  int windowHeight = gameEngine.getLogicalHeight();

  // Create overlay background to dim the game behind the pause menu
  ui.createOverlay(windowWidth, windowHeight);
  // Overlay auto-repositions via createOverlay's positioning rules

  ui.createTitle("pause_title", {0, UIConstants::TITLE_TOP_OFFSET * 10, windowWidth, UIConstants::DEFAULT_TITLE_HEIGHT},
                 "Game Paused");
  ui.setTitleAlignment("pause_title", UIAlignment::CENTER_CENTER);
  // Set auto-repositioning: centered horizontally, fixed Y position
  ui.setComponentPositioning("pause_title", {UIPositionMode::CENTERED_H, 0, UIConstants::TITLE_TOP_OFFSET * 10,
                                             -1, UIConstants::DEFAULT_TITLE_HEIGHT});

  // Create centered buttons for pause menu
  int buttonWidth = 200;
  int buttonHeight = 40;
  int buttonSpacing = 60;
  int firstButtonY = 50;  // Offset from center

  ui.createCenteredButton("pause_resume_btn", firstButtonY, buttonWidth, buttonHeight, "Resume Game");
  ui.createCenteredButton("pause_mainmenu_btn", firstButtonY + buttonSpacing, buttonWidth, buttonHeight, "Main Menu");

  // Set button callbacks
  ui.setOnClick("pause_resume_btn", []() {
      const auto& gameEngine = GameEngine::Instance();
      gameEngine.getGameStateManager()->popState();
  });

  ui.setOnClick("pause_mainmenu_btn", []() {
      const auto& gameEngine = GameEngine::Instance();
      gameEngine.getGameStateManager()->changeState("MainMenuState");
  });

  return true;
}

void PauseState::update([[maybe_unused]] float deltaTime) {
}

void PauseState::render(SDL_Renderer* renderer, [[maybe_unused]] float interpolationAlpha) {
    auto& ui = UIManager::Instance();

    // Update and render UI components through UIManager
    if (!ui.isShutdown()) {
        ui.update(0.0);  // UI updates are not time-dependent in this state
    }
    ui.render(renderer);
}
bool PauseState::exit() {
  // Resume game time when leaving pause state
  GameTime::Instance().resume();

  // Only clean up PauseState-specific UI components
  // Do NOT use prepareForStateTransition() as it would clear GamePlayState's preserved UI
  auto& ui = UIManager::Instance();
  ui.removeComponent("pause_title");
  ui.removeComponent("pause_resume_btn");
  ui.removeComponent("pause_mainmenu_btn");
  ui.removeOverlay();  // Remove the pause overlay to restore GamePlayState visibility

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
