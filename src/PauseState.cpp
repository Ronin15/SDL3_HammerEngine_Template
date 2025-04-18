#include "PauseState.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include <iostream>

bool PauseState::enter() {
  std::cout << "Forge Game Engine - Entering PAUSE State" << std::endl;
  return true;
}

void PauseState::update() {
  //std::cout << "Updating PAUSE State" << std::endl;
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
    //std::cout << "Rendering PAUSE State" << std::endl;
}
bool PauseState::exit() {
  std::cout << "Forge Game Engine - Exiting PAUSE State" << std::endl;

  return true;
}

std::string PauseState::getName() const {
  return "PauseState";
}
