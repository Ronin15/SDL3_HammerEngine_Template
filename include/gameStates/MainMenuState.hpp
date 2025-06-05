/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MAIN_MENU_STATE_HPP
#define MAIN_MENU_STATE_HPP

#include "gameStates/GameState.hpp"
#include "ui/UIScreen.hpp"
#include <memory>

class MainMenuState : public GameState {
 public:
  bool enter() override;
  void update(float deltaTime) override;
  void render() override;
  bool exit() override;
  std::string getName() const override;

 private:
  std::unique_ptr<UIScreen> m_uiScreen{nullptr};
};

#endif  // MAIN_MENU_STATE_HPP
