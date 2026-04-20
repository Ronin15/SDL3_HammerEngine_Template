/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/MenuNavigation.hpp"

#include "managers/InputManager.hpp"
#include "managers/UIManager.hpp"

#include <string>

namespace VoidLight {

void MenuNavigation::applySelection(std::span<const std::string_view> navOrder,
                                    size_t index) {
  if (navOrder.empty()) {
    return;
  }
  if (index >= navOrder.size()) {
    index = 0;
  }
  auto &ui = UIManager::Instance();
  if (!InputManager::Instance().isGamepadConnected()) {
    ui.clearKeyboardSelection();
    return;
  }
  const std::string_view target = navOrder[index];
  if (ui.getKeyboardSelection() == target) {
    return;
  }
  ui.setKeyboardSelection(std::string(target));
}

void MenuNavigation::step(std::span<const std::string_view> navOrder,
                          size_t &index, int delta) {
  if (navOrder.empty()) {
    return;
  }
  const int n = static_cast<int>(navOrder.size());
  int idx = static_cast<int>(index) + delta;
  idx = ((idx % n) + n) % n;
  index = static_cast<size_t>(idx);
  applySelection(navOrder, index);
}

bool MenuNavigation::readInputs(std::span<const std::string_view> navOrder,
                                size_t &index, bool enabled) {
  if (!enabled || navOrder.empty()) {
    return false;
  }
  const auto &input = InputManager::Instance();
  const bool controllerActive = input.isGamepadConnected();
  if (controllerActive) {
    if (input.isCommandPressed(InputManager::Command::MenuUp)) {
      step(navOrder, index, -1);
    }
    if (input.isCommandPressed(InputManager::Command::MenuDown)) {
      step(navOrder, index, +1);
    }
    if (input.isCommandPressed(InputManager::Command::MenuConfirm)) {
      if (index < navOrder.size()) {
        UIManager::Instance().simulateClick(std::string(navOrder[index]));
        return true;
      }
    }
  }
  return false;
}

bool MenuNavigation::cancelPressed() {
  const auto &input = InputManager::Instance();
  return input.isGamepadConnected() &&
         input.isCommandPressed(InputManager::Command::MenuCancel);
}

bool MenuNavigation::leftPressed() {
  const auto &input = InputManager::Instance();
  return input.isGamepadConnected() &&
         input.isCommandPressed(InputManager::Command::MenuLeft);
}

bool MenuNavigation::rightPressed() {
  const auto &input = InputManager::Instance();
  return input.isGamepadConnected() &&
         input.isCommandPressed(InputManager::Command::MenuRight);
}

} // namespace VoidLight
