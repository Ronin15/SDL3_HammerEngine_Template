/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "states/LogoState.hpp"
#include "core/GameEngine.hpp"
#include "managers/SoundManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/TextureManager.hpp"
#include <iostream>

Uint32 stateTimer{0};

bool LogoState::enter() {
  std::cout << "Forge Game Engine - Entering LOGO State\n";
  SoundManager::Instance().playSFX("sfx_logo", 0, 5);
  return true;
}

void LogoState::update() {
  // std::cout << "Forge Game Engine - Updating LOGO State\n";

  stateTimer = SDL_GetTicks();
  if (stateTimer > 7000) {
    GameEngine::Instance().getGameStateManager()->setState("MainMenuState");
  }
}

void LogoState::render() {
  // std::cout << "Rendering Main Menu State\n";
  TextureManager::Instance().draw(
      "HammerForgeBanner",
      GameEngine::Instance().getWindowWidth() / 2 - 373,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) - 352,
      727, 352,
      GameEngine::Instance().getRenderer());
  TextureManager::Instance().draw(
      "ForgeEngine",
      GameEngine::Instance().getWindowWidth() / 2 - 65,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 10,
      128, 128,
      GameEngine::Instance().getRenderer());

  TextureManager::Instance().draw(
      "sdl",
      GameEngine::Instance().getWindowWidth() / 2 - 100, // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 300,
      203, 125,
      GameEngine::Instance().getRenderer());

  TextureManager::Instance().draw(
      "cpp",
      GameEngine::Instance().getWindowWidth() / 2 + 150,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 215,
      50, 50,
      GameEngine::Instance().getRenderer());



  // Render text using SDL_TTF
  //SDL_Color titleColor = {185, 71, 0, 200}; // Forge Orange
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray

  // Draw title text
  FontManager::Instance().drawText(
      "<]==={ }* FORGE GAME ENGINE *{ }===]>",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 180,
      fontColor,
      GameEngine::Instance().getRenderer());

  // Draw subtitle text
  FontManager::Instance().drawText(
      "Powered by SDL3",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2 ,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 220,
      fontColor,
      GameEngine::Instance().getRenderer());

  // Draw version text
  FontManager::Instance().drawText(

      "v0.0.5",
      "fonts_Arial",
      GameEngine::Instance().getWindowWidth() / 2,  // Center horizontally
      (GameEngine::Instance().getWindowHeight() / 2) + 260,
      fontColor,
      GameEngine::Instance().getRenderer());
}

bool LogoState::exit() {
  std::cout << "Forge Game Engine - Exiting LOGO State\n";
  return true;
}

std::string LogoState::getName() const {
  return "LogoState";
}
