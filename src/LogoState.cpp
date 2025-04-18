#include "LogoState.hpp"
#include "GameEngine.hpp"
#include <iostream>

Uint64 stateTimer{0};

bool LogoState::enter() {
  std::cout << "Forge Game Engine - Entering LOGO State" << std::endl;
  return true;
}

void LogoState::update() {
  //std::cout << "Forge Game Engine - Updating LOGO State" << std::endl;

      stateTimer = SDL_GetTicks();
      if (stateTimer > 8000) {
          GameEngine::Instance()->getGameStateManager()->setState("MainMenuState");
      }
  }

void LogoState::render() {
  //std::cout << "Rendering Main Menu State" << std::endl;
}
bool LogoState::exit() {
  std::cout << "Forge Game Engine - Exiting LOGO State" << std::endl;
  return true;
}

std::string LogoState::getName() const {
  return "LogoState";
}
