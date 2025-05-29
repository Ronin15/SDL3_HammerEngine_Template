/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/GamePlayState.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/InputManager.hpp"
#include "core/GameEngine.hpp"
#include "gameStates/PauseState.hpp"
#include <iostream>

bool GamePlayState::enter() {
  std::cout << "Forge Game Engine - Entering GAME State\n";

  // Reset transition flag when entering
  m_transitioningToPause = false;

  // Remove PauseState if we're coming from it
  if (GameEngine::Instance().getGameStateManager()->hasState("PauseState")) {
    GameEngine::Instance().getGameStateManager()->removeState("PauseState");
    std::cout << "Forge Game Engine - Removing PAUSE State\n";
  }

  // Create player if not already created
  if (!mp_Player) {
    mp_Player = std::make_unique<Player>();
    std::cout << "Forge Game Engine - Player created in GamePlayState\n";
  }
  return true;
}

void GamePlayState::update() {
  //std::cout << "Updating GAME State\n";
  // Handle pause, ESC, and back to main menu keys.
  if (InputManager::Instance().isKeyDown(SDL_SCANCODE_P)) {
      // Create PauseState if it doesn't exist
      if (!GameEngine::Instance().getGameStateManager()->hasState("PauseState")) {
          GameEngine::Instance().getGameStateManager()->addState(std::make_unique<PauseState>());
          std::cout << "Forge Game Engine - Created PAUSE State\n";
      }
      m_transitioningToPause = true; // Set flag before transitioning
      GameEngine::Instance().getGameStateManager()->setState("PauseState");
  }
  if (InputManager::Instance().isKeyDown(SDL_SCANCODE_B)) {
      std::cout << "Forge Game Engine - Transitioning to MainMenuState...\n";
      GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
  }
  if (InputManager::Instance().isKeyDown(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance().setRunning(false);
  }

  // Update player if it exists
  if (mp_Player) {
      mp_Player->update();
  }
}

void GamePlayState::render() {
  //std::cout << "Rendering GAME State\n";
  SDL_Color fontColor = {200, 200, 200, 255};
   FontManager::Instance().drawText(
     "Game State Place Holder <----> Press [P] to test Pause State <----> Press [B] to return to Main Menu",
     "fonts_Arial",
     GameEngine::Instance().getWindowWidth() / 2,  // Center horizontally
     20,
     fontColor,
     GameEngine::Instance().getRenderer());

    mp_Player->render();

}
bool GamePlayState::exit() {
  std::cout << "Forge Game Engine - Exiting GAME State\n";

  // Only clear specific textures if we're not transitioning to pause state
  if (!m_transitioningToPause) {
    // Reset player
    mp_Player = nullptr;
    //TODO need to evaluate if this entire block is needed. I want to keep all texture in the MAP
    // and not clear any as they may be needed. left over from testing but not hurting anything currently.
    std::cout << "Forge Game Engine - reset player pointer to null, not going to pause\n";
  } else {
    std::cout << "Forge Game Engine - Keeping textures and player, going to pause\n";
    // Reset flag for next time
    m_transitioningToPause = false;
  }

  return true;
}

std::string GamePlayState::getName() const {
  return "GamePlayState";
}
