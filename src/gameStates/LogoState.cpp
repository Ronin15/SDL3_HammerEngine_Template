/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/LogoState.hpp"
#include "core/GameEngine.hpp"
#include "managers/SoundManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/TextureManager.hpp"
#include <iostream>

Uint32 stateTimer{0};

bool LogoState::enter() {
  std::cout << "Forge Game Engine - Entering LOGO State\n";

  // Cache SoundManager reference for better performance
  SoundManager& soundMgr = SoundManager::Instance();
  soundMgr.playSFX("sfx_logo", 0, 5);
  return true;
}

void LogoState::update([[maybe_unused]] float deltaTime) {
  // std::cout << "Forge Game Engine - Updating LOGO State\n";

  stateTimer = SDL_GetTicks();
  if (stateTimer > 7000) {
    // Cache GameEngine reference for better performance
    GameEngine& gameEngine = GameEngine::Instance();
    gameEngine.getGameStateManager()->setState("MainMenuState");
  }
}

void LogoState::render() {
  // Cache manager references for better performance
  TextureManager& texMgr = TextureManager::Instance();
  GameEngine& gameEngine = GameEngine::Instance();
  FontManager& fontMgr = FontManager::Instance();
  SDL_Renderer* renderer = gameEngine.getRenderer();
  int windowWidth = gameEngine.getWindowWidth();
  int windowHeight = gameEngine.getWindowHeight();

  // std::cout << "Rendering Main Menu State\n";
  texMgr.draw(
      "HammerForgeBanner",
      windowWidth / 2 - 373,  // Center horizontally
      (windowHeight / 2) - 352,
      727, 352,
      renderer);
  texMgr.draw(
      "ForgeEngine",
      windowWidth / 2 - 64,  // Center horizontally (128/2 = 64)
      (windowHeight / 2) + 10,
      128, 128,
      renderer);

  texMgr.draw(
      "cpp",
      windowWidth / 2 + 120,  // Position to right of subtitle text
      (windowHeight / 2) + 220 - 25,  // Align vertically with subtitle text
      50, 50,
      renderer);

  // Render text using SDL_TTF
  //SDL_Color titleColor = {185, 71, 0, 200}; // Forge Orange
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray

  // Draw title text
  fontMgr.drawText(
      "<]==={ }* FORGE GAME ENGINE *{ }===]>",
      "fonts_Arial",
      windowWidth / 2,  // Center horizontally
      (windowHeight / 2) + 180,
      fontColor,
      renderer);

  // Draw subtitle text
  fontMgr.drawText(
      "Powered by SDL3",
      "fonts_Arial",
      windowWidth / 2 ,  // Center horizontally
      (windowHeight / 2) + 220,
      fontColor,
      renderer);

  // Draw version text
  fontMgr.drawText(

      "v0.1.0",
      "fonts_Arial",
      windowWidth / 2,  // Center horizontally
      (windowHeight / 2) + 260,
      fontColor,
      renderer);

  // Draw SDL logo centered below version text
  texMgr.draw(
      "sdl_logo",
      windowWidth / 2 - 70,  // Center horizontally (adjusted slightly right)
      (windowHeight / 2) + 290,
      179, 99,
      renderer);
}

bool LogoState::exit() {
  std::cout << "Forge Game Engine - Exiting LOGO State\n";
  return true;
}

std::string LogoState::getName() const {
  return "LogoState";
}
