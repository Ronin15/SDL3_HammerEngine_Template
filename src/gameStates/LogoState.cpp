/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "gameStates/LogoState.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "managers/SoundManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/UIManager.hpp"
#include <algorithm>


bool LogoState::enter() {
  GAMESTATE_INFO("Entering LOGO State");

  // Reset timer when entering state
  m_stateTimer = 0.0f;

  // Cache SoundManager reference for better performance
  SoundManager& soundMgr = SoundManager::Instance();
  soundMgr.playSFX("sfx_logo", 0, 0);//change right value from 0 -> 1. For dev.
  return true;
}

void LogoState::update(float deltaTime) {
  m_stateTimer += deltaTime;

  if (m_stateTimer > 3.0f) {
    // Cache GameEngine reference for better performance
    const auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();

    // Use immediate state change - proper enter/exit sequencing handles timing
    if (gameStateManager && gameStateManager->hasState("MainMenuState")) {
      gameStateManager->changeState("MainMenuState");
    }
  }
}
void LogoState::render() {
  // Cache manager references for better performance
  TextureManager& texMgr = TextureManager::Instance();
  GameEngine& gameEngine = GameEngine::Instance();
  FontManager& fontMgr = FontManager::Instance();
  SDL_Renderer* renderer = gameEngine.getRenderer();
  // Use logical rendering dimensions for proper UI positioning in all display modes
  int windowWidth = gameEngine.getLogicalWidth();
  int windowHeight = gameEngine.getLogicalHeight();

  // Calculate scale factor for resolution-aware sizing (1920x1080 baseline)
  // Cap at 1.0 to prevent logos from scaling larger than original at high resolutions
  float scale = std::min(1.0f, std::min(windowWidth / 1920.0f, windowHeight / 1080.0f));

  // Scale all image dimensions and positions proportionally
  int bannerSize = static_cast<int>(256 * scale);
  int engineSize = static_cast<int>(128 * scale);
  int sdlWidth = static_cast<int>(179 * scale);
  int sdlHeight = static_cast<int>(99 * scale);
  int cppSize = static_cast<int>(50 * scale);

  // GAMESTATE_DEBUG("Rendering Main Menu State");
  texMgr.draw(
      "HammerForgeBanner",
      windowWidth / 2 - bannerSize / 2,  // Center horizontally with scaled size
      (windowHeight / 2) - static_cast<int>(300 * scale),  // Scaled vertical position
      bannerSize, bannerSize,
      renderer);
  texMgr.draw(
      "HammerEngine",
      windowWidth / 2 - engineSize / 2,  // Center horizontally with scaled size
      (windowHeight / 2) + static_cast<int>(10 * scale),  // Scaled vertical position
      engineSize, engineSize,
      renderer);

  texMgr.draw(
      "cpp",
      windowWidth / 2 + static_cast<int>(120 * scale),  // Scaled horizontal offset
      (windowHeight / 2) + static_cast<int>(195 * scale),  // Scaled vertical position
      cppSize, cppSize,
      renderer);

  // Render text using SDL_TTF
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray

  // Draw title text (scaled position)
  fontMgr.drawText(
      "<]==={ }* Hammer Game Engine *{ }===]>",
      "fonts_Arial",
      windowWidth / 2,  // Center horizontally
      (windowHeight / 2) + static_cast<int>(180 * scale),  // Scaled vertical position
      fontColor,
      renderer);

  // Draw subtitle text (scaled position)
  fontMgr.drawText(
      "Powered by SDL3",
      "fonts_Arial",
      windowWidth / 2 ,  // Center horizontally
      (windowHeight / 2) + static_cast<int>(220 * scale),  // Scaled vertical position
      fontColor,
      renderer);

  // Draw version text (scaled position)
  fontMgr.drawText(
      "v0.4.5",
      "fonts_Arial",
      windowWidth / 2,  // Center horizontally
      (windowHeight / 2) + static_cast<int>(260 * scale),  // Scaled vertical position
      fontColor,
      renderer);

  // Draw SDL logo centered below version text
  texMgr.draw(
      "sdl_logo",
      windowWidth / 2 - sdlWidth / 2,  // Center horizontally with scaled size
      (windowHeight / 2) + static_cast<int>(290 * scale),  // Scaled vertical position
      sdlWidth, sdlHeight,
      renderer);
}

bool LogoState::exit() {
  GAMESTATE_INFO("Exiting LOGO State");

  // LogoState doesn't create UI components, so no UI cleanup needed

  return true;
}

void LogoState::handleInput() {
  // LogoState doesn't need input handling
}

std::string LogoState::getName() const {
  return "LogoState";
}
