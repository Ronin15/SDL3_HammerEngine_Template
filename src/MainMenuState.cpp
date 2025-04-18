#include "MainMenuState.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include <iostream>

bool MainMenuState::enter() {
  std::cout << "Forge Game Engine - Entering MAIN MENU State" << std::endl;
  return true;
}

void MainMenuState::update() {
  //std::cout << "Updating Main Menu State" << std::endl;

      // Handle Play (enter) and ESC key.
      if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_RETURN)) {
          GameEngine::Instance()->getGameStateManager()->setState("GamePlayState");
      }
      if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
          GameEngine::Instance()->setRunning(false);
      }
  }

void MainMenuState::render() {
  //std::cout << "Rendering Main Menu State" << std::endl;
}
bool MainMenuState::exit() {
  std::cout << "Forge Game Engine - Exiting MAIN MENU State" << std::endl;
  return true;
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
