/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MAIN_MENU_STATE_HPP
#define MAIN_MENU_STATE_HPP

#include "game_states/GameState.hpp"

class MainMenuState : public GameState {
 public:
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;
};

#endif  // MAIN_MENU_STATE_HPP
