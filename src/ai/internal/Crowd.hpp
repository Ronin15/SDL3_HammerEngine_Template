// Internal crowd utilities for lightweight separation steering.
#ifndef AI_INTERNAL_CROWD_HPP
#define AI_INTERNAL_CROWD_HPP

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"

namespace AIInternal {

// Enhanced separation parameters for different behavior types
namespace SeparationParams {
  // Combat behaviors (attack, chase, guard when alert)
  constexpr float COMBAT_RADIUS = 32.0f;
  constexpr float COMBAT_STRENGTH = 0.35f;
  constexpr size_t COMBAT_MAX_NEIGHBORS = 6;
  
  // Movement behaviors (follow, patrol, wander) - enhanced for better crowd control
  constexpr float MOVEMENT_RADIUS = 28.0f;
  constexpr float MOVEMENT_STRENGTH = 0.30f;
  constexpr size_t MOVEMENT_MAX_NEIGHBORS = 6;
  
  // Idle/light behaviors - increased to prevent clustering
  constexpr float IDLE_RADIUS = 24.0f;
  constexpr float IDLE_STRENGTH = 0.20f;
  constexpr size_t IDLE_MAX_NEIGHBORS = 5;
  
  // Flee behaviors (lighter touch to preserve escape direction)
  constexpr float FLEE_RADIUS = 26.0f;
  constexpr float FLEE_STRENGTH = 0.25f;
  constexpr size_t FLEE_MAX_NEIGHBORS = 4;
}

// Applies proximity-weighted separation to an intended velocity and returns
// the adjusted velocity normalized to the given speed.
// - radius: query radius (px)
// - strength: blend factor for separation contribution (~0.1f - 0.35f)
// - maxNeighbors: cap number of neighbors considered (4-6 for better crowd control)
Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors = 6);

// Smooths velocity changes to reduce visual glitching
// - currentVel: entity's current velocity
// - targetVel: desired velocity from AI logic
// - smoothingFactor: how much to smooth (0.0f = no smoothing, 1.0f = full smoothing)
// - maxChange: maximum velocity change per frame to prevent jitter
Vector2D SmoothVelocityTransition(const Vector2D &currentVel,
                                  const Vector2D &targetVel,
                                  float smoothingFactor = 0.15f,
                                  float maxChange = 50.0f);

} // namespace AIInternal

#endif // AI_INTERNAL_CROWD_HPP

