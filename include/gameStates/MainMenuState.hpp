/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef MAIN_MENU_STATE_HPP
#define MAIN_MENU_STATE_HPP

#include "gameStates/GameState.hpp"

#include <array>
#include <cstddef>
#include <string_view>

class MainMenuState : public GameState {
 public:
  bool enter() override;
  void update(float deltaTime) override;
  void handleInput() override;
  bool exit() override;
  GameStateId getStateId() const override { return GameStateId::MAIN_MENU; }

  // GPU rendering support
  void recordGPUVertices(VoidLight::GPURenderer& gpuRenderer,
                         float interpolationAlpha) override;
  void renderGPUUI(VoidLight::GPURenderer& gpuRenderer,
                   SDL_GPURenderPass* swapchainPass) override;
  bool supportsGPURendering() const override { return true; }

 private:
  // Keyboard/gamepad navigation — ordered list of focusable buttons, matches
  // the vertical order on screen. Index wraps on MenuUp/MenuDown.
  static constexpr std::array<std::string_view, 8> kNavOrder{
      "mainmenu_start_game_btn",
      "mainmenu_ai_demo_btn",
      "mainmenu_advanced_ai_demo_btn",
      "mainmenu_event_demo_btn",
      "mainmenu_ui_example_btn",
      "mainmenu_overlay_demo_btn",
      "mainmenu_settings_btn",
      "mainmenu_exit_btn",
  };
  size_t m_selectedIndex{0};

  void applyKeyboardSelection() const;
  void stepSelection(int delta);
};

#endif  // MAIN_MENU_STATE_HPP
