/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef UI_CONSTANTS_HPP
#define UI_CONSTANTS_HPP

#include <string_view>

namespace UIConstants {
  // Standard UI Fonts
  constexpr std::string_view UI_FONT = "fonts_UI_Arial";
  constexpr std::string_view TITLE_FONT = "fonts_title_Arial";
  constexpr std::string_view TOOLTIP_FONT = "fonts_tooltip_Arial";
  constexpr std::string_view DEFAULT_FONT = "fonts_UI_Arial";

  // UI Baseline Resolution (for scaling calculations)
  // This is the reference resolution used for UI design - all UI elements are defined
  // in this coordinate space and then scaled to the actual window resolution
  constexpr int BASELINE_WIDTH = 1920;
  constexpr int BASELINE_HEIGHT = 1080;
  constexpr float BASELINE_WIDTH_F = 1920.0f;
  constexpr float BASELINE_HEIGHT_F = 1080.0f;

  // UI Component Spacing Constants (in baseline pixels)
  // These values are scaled by m_globalScale at runtime for resolution-aware spacing
  constexpr int CHECKBOX_SIZE = 24;
  constexpr int TOOLTIP_PADDING_WIDTH = 16;
  constexpr int TOOLTIP_PADDING_HEIGHT = 8;
  constexpr int TOOLTIP_MOUSE_OFFSET = 10;
  constexpr int INPUT_CURSOR_SPACE = 20;
  constexpr int LIST_ITEM_PADDING = 8;
  constexpr int SCROLLBAR_WIDTH = 20;

} // namespace UIConstants

#endif // UI_CONSTANTS_HPP
