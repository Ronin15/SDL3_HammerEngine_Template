#include "LogoState.hpp"
#include "GameEngine.hpp"
#include "SoundManager.hpp"
#include <iostream>

Uint64 stateTimer{0};

bool LogoState::enter() {
  std::cout << "Forge Game Engine - Entering LOGO State" << std::endl;
  SoundManager::Instance()->playSFX("sfx_logo");
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
  TextureManager::Instance()->draw("HammerForgeBanner", (GameEngine::Instance()->getWindowWidth() / 2) - 400, (GameEngine::Instance()->getWindowHeight() / 2) - 352, 727, 352, GameEngine::Instance()->getRenderer());
  TextureManager::Instance()->draw("ForgeEngine", (GameEngine::Instance()->getWindowWidth() / 2) - 100, (GameEngine::Instance()->getWindowHeight() / 2) + 50, 128, 128, GameEngine::Instance()->getRenderer());
}

bool LogoState::exit() {
  std::cout << "Forge Game Engine - Exiting LOGO State" << std::endl;
  return true;
}

std::string LogoState::getName() const {
  return "LogoState";
}
