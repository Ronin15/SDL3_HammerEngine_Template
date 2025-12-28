/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef LOGO_STATE_HPP
#define LOGO_STATE_HPP

#include "gameStates/GameState.hpp"

class LogoState : public GameState {
 public:
  bool enter() override;
  void update(float deltaTime) override;
  void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;

 private:
  float m_stateTimer{0.0f};

  // Cached layout calculations (computed once in enter())
  int m_windowWidth{0};
  int m_windowHeight{0};
  int m_bannerSize{0};
  int m_engineSize{0};
  int m_sdlSize{0};
  int m_cppSize{0};

  // Cached positions
  int m_bannerX{0}, m_bannerY{0};
  int m_engineX{0}, m_engineY{0};
  int m_cppX{0}, m_cppY{0};
  int m_sdlX{0}, m_sdlY{0};
  int m_titleY{0};
  int m_subtitleY{0};
  int m_versionY{0};
};

#endif  // LOGO_STATE_HPP
