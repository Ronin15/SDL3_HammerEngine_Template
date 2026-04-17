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
  static void applySelection(std::span<const std::string_view> navOrder,
                             size_t index);

  static void step(std::span<const std::string_view> navOrder, size_t &index,
                   int delta);

  static bool readInputs(std::span<const std::string_view> navOrder,
                         size_t &index, bool enabled = true);
};

} // namespace VoidLight

#endif
