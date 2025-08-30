// Internal crowd utilities for lightweight separation steering.
#ifndef AI_INTERNAL_CROWD_HPP
#define AI_INTERNAL_CROWD_HPP

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"

namespace AIInternal {

// Standard separation parameters for different behavior types
namespace SeparationParams {
  // Combat behaviors (attack, chase, guard when alert)
  constexpr float COMBAT_RADIUS = 28.0f;
  constexpr float COMBAT_STRENGTH = 0.25f;
  constexpr size_t COMBAT_MAX_NEIGHBORS = 4;
  
  // Movement behaviors (follow, patrol, wander)
  constexpr float MOVEMENT_RADIUS = 24.0f;
  constexpr float MOVEMENT_STRENGTH = 0.20f;
  constexpr size_t MOVEMENT_MAX_NEIGHBORS = 4;
  
  // Idle/light behaviors
  constexpr float IDLE_RADIUS = 20.0f;
  constexpr float IDLE_STRENGTH = 0.12f;
  constexpr size_t IDLE_MAX_NEIGHBORS = 3;
  
  // Flee behaviors (lighter touch to preserve escape direction)
  constexpr float FLEE_RADIUS = 22.0f;
  constexpr float FLEE_STRENGTH = 0.18f;
  constexpr size_t FLEE_MAX_NEIGHBORS = 3;
}

// Applies proximity-weighted separation to an intended velocity and returns
// the adjusted velocity normalized to the given speed.
// - radius: query radius (px)
// - strength: blend factor for separation contribution (~0.1f - 0.35f)
// - maxNeighbors: cap number of neighbors considered (3-4 keeps it stable)
Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors = 4);

} // namespace AIInternal

#endif // AI_INTERNAL_CROWD_HPP

