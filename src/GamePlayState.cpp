#include "GamePlayState.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include "PauseState.hpp"
#include <iostream>

void GamePlayState::enter() {
  std::cout << "Entering GAME State" << std::endl;

  // Remove PauseState if we're coming from it
  if (GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
    GameEngine::Instance()->getGameStateManager()->removeState("PauseState");
    std::cout << "Removing PAUSE State" << std::endl;
  }
}

void GamePlayState::update() {
  std::cout << "Updating GAME State" << std::endl;
  // Handle pause and ESC key.
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_P)) {
      // Create PauseState if it doesn't exist
      if (!GameEngine::Instance()->getGameStateManager()->hasState("PauseState")) {
          GameEngine::Instance()->getGameStateManager()->addState(std::make_unique<PauseState>());
          std::cout << "Created PAUSE State" << std::endl;
      }
      GameEngine::Instance()->getGameStateManager()->setState("PauseState");
  }
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance()->setRunning(false);
  }
}

void GamePlayState::render() {
  std::cout << "Rendering GAME State" << std::endl;
}
void GamePlayState::exit() {
  std::cout << "Exiting GAME State" << std::endl;
}

std::string GamePlayState::getName() const {
  return "GamePlayState";
}
