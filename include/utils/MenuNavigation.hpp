/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef VOIDLIGHT_MENU_NAVIGATION_HPP
#define VOIDLIGHT_MENU_NAVIGATION_HPP

#include <cstddef>
#include <span>
#include <string_view>

namespace VoidLight {

class MenuNavigation {
public:
  // Applies the keyboard-focus highlight to navOrder[index]. When no gamepad
  // is connected (InputManager::isGamepadConnected() == false) this instead
  // clears any existing keyboard selection — menu highlighting is a
  // controller-only affordance; mouse/keyboard users interact via clicks.
  // Safe to call every frame: handles gamepad hotplug by clearing/reapplying
  // selection as connection state changes.
  static void applySelection(std::span<const std::string_view> navOrder,
                             size_t index);

  static void step(std::span<const std::string_view> navOrder, size_t &index,
                   int delta);

  // Reads MenuUp/Down/Confirm edges. All direction and confirm handling is
  // gated on gamepad connection — returns false and does nothing when no
  // controller is attached.
  static bool readInputs(std::span<const std::string_view> navOrder,
                         size_t &index, bool enabled = true);

  // Returns true this frame if MenuCancel was pressed AND a gamepad is
  // connected. Menu commands are controller-only by design; keyboard+mouse
  // users navigate menus via mouse clicks.
  [[nodiscard]] static bool cancelPressed();

  // Returns true this frame if MenuLeft was pressed AND a gamepad is
  // connected. Controller-only — see cancelPressed() for the rationale.
  [[nodiscard]] static bool leftPressed();

  // Returns true this frame if MenuRight was pressed AND a gamepad is
  // connected. Controller-only — see cancelPressed() for the rationale.
  [[nodiscard]] static bool rightPressed();
};

} // namespace VoidLight

#endif
