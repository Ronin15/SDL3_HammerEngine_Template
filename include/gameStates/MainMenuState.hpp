/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MAIN_MENU_STATE_HPP
#define MAIN_MENU_STATE_HPP

#include "gameStates/GameState.hpp"

class MainMenuState : public GameState {
 public:
  bool enter() override;
  void update(float deltaTime) override;
  void render(SDL_Renderer* renderer) override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;
  void onWindowResize(int newLogicalWidth, int newLogicalHeight) override;

 private:
  // Pure UIManager approach - no UIScreen needed
};

#endif  // MAIN_MENU_STATE_HPP
