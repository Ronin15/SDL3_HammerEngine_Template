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
#include "managers/GameStateManager.hpp"
#include <algorithm>


bool LogoState::enter() {
  GAMESTATE_INFO("Entering LOGO State");

  // Pause all gameplay managers during logo display
  GameEngine::Instance().setGlobalPause(true);

  // Reset timer when entering state
  m_stateTimer = 0.0f;

  // Cache layout calculations (done once, used every render frame)
  GameEngine& gameEngine = GameEngine::Instance();
  m_windowWidth = gameEngine.getLogicalWidth();
  m_windowHeight = gameEngine.getLogicalHeight();

  // Calculate scale factor for resolution-aware sizing (1920x1080 baseline)
  // Cap at 1.0 to prevent logos from scaling larger than original at high resolutions
  float scale = std::min(1.0f, std::min(m_windowWidth / 1920.0f, m_windowHeight / 1080.0f));

  // Scale all image dimensions proportionally
  m_bannerSize = static_cast<int>(256 * scale);
  m_engineSize = static_cast<int>(128 * scale);
  m_sdlSize = static_cast<int>(128 * scale);
  m_cppSize = static_cast<int>(50 * scale);

  // Calculate positions
  m_bannerX = m_windowWidth / 2 - m_bannerSize / 2;
  m_bannerY = (m_windowHeight / 2) - static_cast<int>(300 * scale);

  m_engineX = m_windowWidth / 2 - m_engineSize / 2;
  m_engineY = (m_windowHeight / 2) + static_cast<int>(10 * scale);

  m_cppX = m_windowWidth / 2 + static_cast<int>(120 * scale);
  m_cppY = (m_windowHeight / 2) + static_cast<int>(195 * scale);

  m_sdlX = (m_windowWidth / 2) - (m_sdlSize / 2) + static_cast<int>(20 * scale);
  m_sdlY = (m_windowHeight / 2) + static_cast<int>(260 * scale);

  m_titleY = (m_windowHeight / 2) + static_cast<int>(180 * scale);
  m_subtitleY = (m_windowHeight / 2) + static_cast<int>(220 * scale);
  m_versionY = (m_windowHeight / 2) + static_cast<int>(260 * scale);

  // Cache SoundManager reference for better performance
  SoundManager& soundMgr = SoundManager::Instance();
  soundMgr.playSFX("sfx_logo", 0, 0);//change right value from 0 -> 1. For dev.
  return true;
}

void LogoState::update(float deltaTime) {
  m_stateTimer += deltaTime;

  if (m_stateTimer > 3.0f) {
    // Use immediate state change - proper enter/exit sequencing handles timing
    if (mp_stateManager->hasState("MainMenuState")) {
      mp_stateManager->changeState("MainMenuState");
    }
  }
}
void LogoState::render(SDL_Renderer* renderer, [[maybe_unused]] float interpolationAlpha) {
  // Cache manager references for better performance
  TextureManager& texMgr = TextureManager::Instance();
  FontManager& fontMgr = FontManager::Instance();

  // Draw banner logo (positions cached in enter())
  texMgr.draw("HammerForgeBanner", m_bannerX, m_bannerY, m_bannerSize, m_bannerSize, renderer);

  // Draw engine logo
  texMgr.draw("HammerEngine", m_engineX, m_engineY, m_engineSize, m_engineSize, renderer);

  // Draw C++ logo
  texMgr.draw("cpp", m_cppX, m_cppY, m_cppSize, m_cppSize, renderer);

  // Render text using SDL_TTF
  SDL_Color fontColor = {200, 200, 200, 255}; // Light gray
  int centerX = m_windowWidth / 2;

  // Draw title text
  fontMgr.drawText("<]==={ }* Hammer Game Engine *{ }===]>", "fonts_Arial",
                   centerX, m_titleY, fontColor, renderer);

  // Draw subtitle text
  fontMgr.drawText("Powered by SDL3", "fonts_Arial",
                   centerX, m_subtitleY, fontColor, renderer);

  // Draw version text
  fontMgr.drawText("v0.8.5", "fonts_Arial",
                   centerX, m_versionY, fontColor, renderer);

  // Draw SDL logo centered below version text
  texMgr.draw("sdl_logo", m_sdlX, m_sdlY, m_sdlSize, m_sdlSize, renderer);
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
