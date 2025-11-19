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

  // Z-Order Layering Constants
  // Controls the render order of UI components (lower values render first/behind)
  constexpr int ZORDER_DIALOG = -10;          // Dialog backgrounds render behind everything
  constexpr int ZORDER_PANEL = 0;             // Background panels
  constexpr int ZORDER_IMAGE = 1;             // Background images
  constexpr int ZORDER_PROGRESS_BAR = 5;      // Progress indicators
  constexpr int ZORDER_EVENT_LOG = 6;         // Event log displays
  constexpr int ZORDER_LIST = 8;              // List components
  constexpr int ZORDER_BUTTON = 10;           // Interactive buttons
  constexpr int ZORDER_SLIDER = 12;           // Slider controls
  constexpr int ZORDER_CHECKBOX = 13;         // Checkbox controls
  constexpr int ZORDER_INPUT_FIELD = 15;      // Text input fields
  constexpr int ZORDER_LABEL = 20;            // Text labels
  constexpr int ZORDER_TITLE = 25;            // Title text
  constexpr int ZORDER_TOOLTIP = 1000;        // Tooltips always on top

  // Border Width Constants
  constexpr int BORDER_WIDTH_NONE = 0;        // No border
  constexpr int BORDER_WIDTH_NORMAL = 1;      // Standard 1px border
  constexpr int BORDER_WIDTH_DIALOG = 2;      // Thicker dialog border
  constexpr int DEBUG_BORDER_WIDTH = 1;       // Debug mode border width

  // Font Size Constants
  constexpr int DEFAULT_FONT_SIZE = 16;       // Standard UI text size
  constexpr int TITLE_FONT_SIZE = 24;         // Title text size

  // Component Default Size Constants
  constexpr int MIN_COMPONENT_WIDTH = 32;     // Minimum component width
  constexpr int MIN_COMPONENT_HEIGHT = 16;    // Minimum component height
  constexpr int MAX_COMPONENT_WIDTH = 800;    // Maximum component width
  constexpr int MAX_COMPONENT_HEIGHT = 600;   // Maximum component height
  constexpr int DEFAULT_INPUT_MAX_LENGTH = 256; // Max input field characters

  // Padding and Spacing Defaults
  constexpr int DEFAULT_COMPONENT_PADDING = 8;  // Default component internal padding
  constexpr int DEFAULT_CONTENT_PADDING = 8;    // Default content padding for auto-sizing
  constexpr int DEFAULT_MARGIN = 4;             // Default component margin
  constexpr int DEFAULT_LAYOUT_SPACING = 4;    // Default spacing between layout children
  constexpr int DEFAULT_TEXT_BG_PADDING = 4;   // Default text background padding
  constexpr int LABEL_TEXT_BG_PADDING = 6;     // Label-specific text background padding
  constexpr int TITLE_TEXT_BG_PADDING = 8;     // Title-specific text background padding

  // List Component Constants
  constexpr int DEFAULT_LIST_ITEM_HEIGHT = 32; // Default height per list item
  constexpr int FALLBACK_LIST_ITEM_HEIGHT = 29; // Fallback when font metrics unavailable (21px font + 8px padding)
  constexpr int MIN_LIST_WIDTH = 150;          // Minimum list width
  constexpr int DEFAULT_LIST_WIDTH = 200;      // Default list width
  constexpr int DEFAULT_LIST_VISIBLE_ITEMS = 3; // Default number of visible list items
  constexpr int MAX_LIST_TEXTURE_CACHE = 1000; // Maximum cached list item textures

  // Slider Component Constants
  constexpr int SLIDER_TRACK_HEIGHT = 4;       // Height of slider track
  constexpr int SLIDER_TRACK_OFFSET = 2;       // Vertical offset for slider track centering
  constexpr int SLIDER_HANDLE_WIDTH = 16;      // Width of slider handle

  // Tooltip Constants
  constexpr int TOOLTIP_FALLBACK_WIDTH = 200;  // Fallback tooltip width
  constexpr int TOOLTIP_FALLBACK_HEIGHT = 32;  // Fallback tooltip height

  // Event Log Constants
  constexpr int DEFAULT_EVENT_LOG_MAX_ENTRIES = 5; // Default max event log entries
  constexpr float DEFAULT_EVENT_LOG_UPDATE_INTERVAL = 2.0f; // Default update interval in seconds

  // Timing and Animation Constants
  constexpr float DEFAULT_TOOLTIP_DELAY = 1.0f; // Default tooltip hover delay in seconds
  constexpr float MAX_UI_SCALE = 1.0f;          // Maximum UI scale factor (prevents upscaling beyond baseline)

  // Positioning Constants
  constexpr int TITLE_TOP_OFFSET = 10;          // Default top offset for titles
  constexpr int BUTTON_BOTTOM_OFFSET = 20;      // Default bottom offset for buttons
  constexpr int DEFAULT_TITLE_HEIGHT = 40;      // Default title component height
  constexpr int DEFAULT_BUTTON_WIDTH = 120;     // Default button width
  constexpr int DEFAULT_BUTTON_HEIGHT = 40;     // Default button height

  // Text Measurement Estimates
  constexpr int CHAR_WIDTH_ESTIMATE = 12;       // Estimated character width for fallback calculations
  constexpr int INPUT_CURSOR_CHAR_WIDTH = 8;    // Character width estimate for input cursor positioning

  // Performance/Memory Constants
  constexpr int DEFAULT_COMPONENT_BATCH_SIZE = 32;  // Reserve size for component removal operations
  constexpr int MAX_COMPONENT_BATCH_SIZE = 64;      // Reserve size for bulk component clearing

} // namespace UIConstants

#endif // UI_CONSTANTS_HPP
