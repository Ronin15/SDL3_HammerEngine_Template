/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef EVENT_TYPE_ID_HPP
#define EVENT_TYPE_ID_HPP

#include <cstdint>

// Strongly typed event type enumeration for fast lookups
enum class EventTypeId : uint8_t {
  Weather = 0,
  SceneChange = 1,
  NPCSpawn = 2,
  ParticleEffect = 3,
  ResourceChange = 4,
  World = 5,
  Camera = 6,
  Harvest = 7,
  Custom = 8,
  COUNT = 9
};

#endif // EVENT_TYPE_ID_HPP

