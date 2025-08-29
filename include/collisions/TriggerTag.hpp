/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef TRIGGER_TAG_HPP
#define TRIGGER_TAG_HPP

namespace HammerEngine {

// Enum tags for world trigger volumes. Extend as needed.
enum class TriggerTag {
  None = 0,
  Door,
  Checkpoint,
  Water,
  Lava,
  Portal,
  AreaEnter,
  AreaExit,
  Custom1,
  Custom2
};

} // namespace HammerEngine

#endif // TRIGGER_TAG_HPP

