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
  void render(double alpha) override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;
};

#endif  // LOGO_STATE_HPP
