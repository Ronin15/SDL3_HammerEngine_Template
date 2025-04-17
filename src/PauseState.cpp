#include "PauseState.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include <iostream>

void PauseState::enter() {
  std::cout << "Entering PAUSE State" << std::endl;
}

void PauseState::update() {
  std::cout << "Updating PAUSE State" << std::endl;
  // Handle pause and ESC key.
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_R)) {
      // Flag the GamePlayState transition
      // We'll do the actual removal in GamePlayState::enter() 
      GameEngine::Instance()->getGameStateManager()->setState("GamePlayState");
  }
  if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance()->setRunning(false);
  }
}

void PauseState::render() {
  std::cout << "Rendering PAUSE State" << std::endl;
}
void PauseState::exit() {
  std::cout << "Exiting PAUSE State" << std::endl;
  // Don't call removeState here - it would cause recursive call and segfault
  // The state should be removed by the caller after exit() completes
}

std::string PauseState::getName() const {
  return "PauseState";
}
