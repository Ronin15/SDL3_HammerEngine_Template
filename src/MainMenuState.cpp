#include "MainMenuState.hpp"
#include "InputHandler.hpp"
#include "GameEngine.hpp"
#include <iostream>

void MainMenuState::enter() {
  std::cout << "Entering Main Menu State" << std::endl;
}

void MainMenuState::update() {
  std::cout << "Updating Main Menu State" << std::endl;

      // Handle Play (enter) and ESC key.
      if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_RETURN)) {
          GameEngine::Instance()->getGameStateManager()->setState("GamePlayState");
      }
      if (InputHandler::Instance()->isKeyDown(SDL_SCANCODE_ESCAPE)) {
          GameEngine::Instance()->setRunning(false);
      }

  }

void MainMenuState::render() {
  std::cout << "Rendering Main Menu State" << std::endl;
}
void MainMenuState::exit() {
  std::cout << "Exiting Main Menu State" << std::endl;
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}
