/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/LogoState.hpp"
#include "core/GameEngine.hpp"
#include "managers/SoundManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/UIManager.hpp"
#include <iostream>

bool LogoState::enter() {
  std::cout << "Hammer Game Engine - Entering LOGO State\n";

  // Reset timer when entering state
  m_stateTimer = 0.0f;

  // Cache SoundManager reference for better performance
  SoundManager& soundMgr = SoundManager::Instance();
  soundMgr.playSFX("sfx_logo", 0, 1);
  return true;
}

void LogoState::update(float deltaTime) {
  m_stateTimer += deltaTime;
  if (m_stateTimer > 3.0f) {  // 3 seconds using deltaTime
    // Cache GameEngine reference for better performance
    const auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    
    // Ensure MainMenuState exists before transitioning (prevents race condition)
    if (gameStateManager && gameStateManager->hasState("MainMenuState")) {
      gameStateManager->changeState("MainMenuState");
    }
  }
}
void LogoState::render([[maybe_unused]] double alpha) {
  // Cache manager references for better performance
  TextureManager& texMgr = TextureManager::Instance();
  GameEngine& gameEngine = GameEngine::Instance();
  FontManager& fontMgr = FontManager::Instance();
  SDL_Renderer* renderer = gameEngine.getRenderer();
  // Use logical rendering dimensions for proper UI positioning in all display modes
  int windowWidth = gameEngine.getLogicalWidth();
  int windowHeight = gameEngine.getLogicalHeight();

  // std::cout << "Rendering Main Menu State\n";
  texMgr.draw(
      "HammerForgeBanner",
      windowWidth / 2 - 128,  // Center horizontally (256/2 = 128)
      (windowHeight / 2) - 300,  // Position above HammerEngine with spacing
      256, 256,
      renderer);
  texMgr.draw(
      "HammerEngine",
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
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray

  // Draw title text
  fontMgr.drawText(
      "<]==={ }* Hammer Game Engine *{ }===]>",
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

      "v0.2.0",
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
  std::cout << "Hammer Game Engine - Exiting LOGO State\n";

  // LogoState doesn't create UI components, so no UI cleanup needed

  return true;
}

void LogoState::handleInput() {
  // LogoState doesn't need input handling
}

std::string LogoState::getName() const {
  return "LogoState";
}
