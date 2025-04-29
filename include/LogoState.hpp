/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef LOGO_STATE_HPP
#define LOGO_STATE_HPP

#include "GameState.hpp"

class LogoState : public GameState {
 public:
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;
};

#endif  // LOGO_STATE_HPP
