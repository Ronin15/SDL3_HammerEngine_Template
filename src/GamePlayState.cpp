#include "GamePlayState.hpp"
#include "GameStateManager.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include "PauseState.hpp"
#include "TextureManager.hpp"
#include <iostream>

bool GamePlayState::enter() {
  std::cout << "Forge Engine - Entering GAME State" << std::endl;

  // Remove PauseState if we're coming from it
  if (GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
    GameEngine::Instance()->getGameStateManager()->removeState("PauseState");
    std::cout << "Forge Engine - Removing PAUSE State" << std::endl;
  }
  //loading Game Play State asset
  if (!TextureManager::Instance()->load("res/img/ForgeEngine.png", "ForgeEngine",GameEngine::Instance()->getRenderer())) {

    return true;
  }
  return true;
}

void GamePlayState::update() {
    //std::cout << "Updating GAME State" << std::endl;
  // Handle pause and ESC key.
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_P)) {
      // Create PauseState if it doesn't exist
      if (!GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
          GameEngine::Instance()->getGameStateManager()->addState(std::make_unique<PauseState>());
          std::cout << "Forge Engine - Created PAUSE State" << std::endl;
      }
      GameEngine::Instance()->getGameStateManager()->setState("PauseState");
  }
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance()->setRunning(false);
  }
}

void GamePlayState::render() {
  //std::cout << "Rendering GAME State" << std::endl;
}
bool GamePlayState::exit() {
  std::cout << "Forge Engine - Exiting GAME State" << std::endl;
if (!GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
        return true;
    }

  TextureManager::Instance()->clearFromTexMap("ForgeEngine");
  return true;
}

std::string GamePlayState::getName() const {
  return "GamePlayState";
}
