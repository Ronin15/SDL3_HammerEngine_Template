#include "LogoState.hpp"
#include "GameEngine.hpp"
#include "SoundManager.hpp"
#include "FontManager.hpp"
#include <iostream>

Uint64 stateTimer{0};

bool LogoState::enter() {
  std::cout << "Forge Game Engine - Entering LOGO State" << std::endl;
  SoundManager::Instance()->playSFX("sfx_logo", 0, 5);
  return true;
}

void LogoState::update() {
  // std::cout << "Forge Game Engine - Updating LOGO State" << std::endl;

  stateTimer = SDL_GetTicks();
  if (stateTimer > 8000) {
    GameEngine::Instance()->getGameStateManager()->setState("MainMenuState");
  }
}

void LogoState::render() {
  // std::cout << "Rendering Main Menu State" << std::endl;
  TextureManager::Instance()->draw(
      "HammerForgeBanner", (GameEngine::Instance()->getWindowWidth() / 2) - 350,
      (GameEngine::Instance()->getWindowHeight() / 2) - 352, 727, 352,
      GameEngine::Instance()->getRenderer());
  TextureManager::Instance()->draw(
      "ForgeEngine", (GameEngine::Instance()->getWindowWidth() / 2) - 45,
      (GameEngine::Instance()->getWindowHeight() / 2) + 30, 128, 128,
      GameEngine::Instance()->getRenderer());

  TextureManager::Instance()->draw(
      "sdl", (GameEngine::Instance()->getWindowWidth() / 2) - 80,
      (GameEngine::Instance()->getWindowHeight() / 2) + 300, 203, 125,
      GameEngine::Instance()->getRenderer());

  // Render text using SDL_TTF
  SDL_Color titleColor = {185, 71, 0, 200}; // Light gray
  SDL_Color subtitleColor = {200, 200, 200, 255}; // Forge Orange

  // Use existing fonts from the FontManager instead of loading new ones
  // The GameEngine already loads fonts with ID "fonts_Arial" (from "res/fonts/Arial.ttf")

  // Draw title text
  FontManager::Instance()->drawText(
      "<]==={} FORGE GAME ENGINE {}===]>",
      "fonts_Arial",
      (GameEngine::Instance()->getWindowWidth() / 2) - 150,
      (GameEngine::Instance()->getWindowHeight() / 2) + 180,
      titleColor,
      GameEngine::Instance()->getRenderer());

  // Draw subtitle text
  FontManager::Instance()->drawText(
      "Powered by SDL3",
      "fonts_Arial",
      (GameEngine::Instance()->getWindowWidth() / 2) - 65,
      (GameEngine::Instance()->getWindowHeight() / 2) + 220,
      subtitleColor,
      GameEngine::Instance()->getRenderer());

  // Draw version text
  FontManager::Instance()->drawText(
      "v0.0.5",
      "fonts_Arial",
      (GameEngine::Instance()->getWindowWidth() / 2) - 15,
      (GameEngine::Instance()->getWindowHeight() / 2) + 260,
      subtitleColor,
      GameEngine::Instance()->getRenderer());
}

bool LogoState::exit() {
  std::cout << "Forge Game Engine - Exiting LOGO State" << std::endl;
  return true;
}

std::string LogoState::getName() const {
  return "LogoState";
}
