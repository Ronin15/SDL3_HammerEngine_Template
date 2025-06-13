/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/GamePlayState.hpp"
#include "managers/GameStateManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/UIManager.hpp"
#include "core/GameEngine.hpp"
#include "gameStates/PauseState.hpp"
#include <iostream>

bool GamePlayState::enter() {
  std::cout << "Forge Game Engine - Entering GAME State\n";

  // Cache GameEngine reference for better performance
  GameEngine& gameEngine = GameEngine::Instance();
  auto* gameStateManager = gameEngine.getGameStateManager();

  // Reset transition flag when entering
  m_transitioningToPause = false;

  // Remove PauseState if we're coming from it
  if (gameStateManager->hasState("PauseState")) {
    gameStateManager->removeState("PauseState");
    std::cout << "Forge Game Engine - Removing PAUSE State\n";
  }

  // Create player if not already created
  if (!mp_Player) {
    mp_Player = std::make_unique<Player>();
    std::cout << "Forge Game Engine - Player created in GamePlayState\n";
  }
  return true;
}

void GamePlayState::update([[maybe_unused]] float deltaTime) {
  //std::cout << "Updating GAME State\n";

  // Update player if it exists
  if (mp_Player) {
      mp_Player->update(deltaTime);
  }
}

void GamePlayState::render([[maybe_unused]] float deltaTime) {
  //std::cout << "Rendering GAME State\n";
  
  // Cache manager references for better performance
  FontManager& fontMgr = FontManager::Instance();
  GameEngine& gameEngine = GameEngine::Instance();
  
  SDL_Color fontColor = {200, 200, 200, 255};
   fontMgr.drawText(
     "Game State Place Holder <----> Press [P] to test Pause State <----> Press [B] to return to Main Menu",
     "fonts_Arial",
     gameEngine.getWindowWidth() / 2,  // Center horizontally
     20,
     fontColor,
     gameEngine.getRenderer());

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
    std::cout << "Forge Game Engine - Not clearing player and textures, transitioning to pause\n";
  }

  // GamePlayState doesn't create UI components, so no UI cleanup needed

  return true;
}

std::string GamePlayState::getName() const {
  return "GamePlayState";
}

void GamePlayState::handleInput() {
  InputManager& inputMgr = InputManager::Instance();
  
  // Use InputManager's new event-driven key press detection
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_P)) {
      // Create PauseState if it doesn't exist
      GameEngine& gameEngine = GameEngine::Instance();
      auto* gameStateManager = gameEngine.getGameStateManager();
      if (!gameStateManager->hasState("PauseState")) {
          gameStateManager->addState(std::make_unique<PauseState>());
          std::cout << "Forge Game Engine - Created PAUSE State\n";
      }
      m_transitioningToPause = true; // Set flag before transitioning
      gameStateManager->setState("PauseState");
  }
  
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_B)) {
      std::cout << "Forge Game Engine - Transitioning to MainMenuState...\n";
      GameEngine& gameEngine = GameEngine::Instance();
      gameEngine.getGameStateManager()->setState("MainMenuState");
  }
  
  if (inputMgr.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      GameEngine& gameEngine = GameEngine::Instance();
      gameEngine.setRunning(false);
  }
}
