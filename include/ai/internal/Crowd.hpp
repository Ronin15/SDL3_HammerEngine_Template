/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Internal crowd utilities for lightweight separation steering.
#ifndef AI_INTERNAL_CROWD_HPP
#define AI_INTERNAL_CROWD_HPP

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <vector>

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

// PERFORMANCE OPTIMIZATION: ApplySeparation with pre-fetched neighbor data
// Use this overload to avoid redundant collision queries when you already have
// nearby entity positions from a previous query (e.g., crowd analysis)
// - preFetchedNeighbors: vector of nearby entity positions (from previous query)
Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors,
                         const std::vector<Vector2D> &preFetchedNeighbors);

// Smooths velocity changes to reduce visual glitching
// - currentVel: entity's current velocity
// - targetVel: desired velocity from AI logic
// - smoothingFactor: how much to smooth (0.0f = no smoothing, 1.0f = full smoothing)
// - maxChange: maximum velocity change per frame to prevent jitter
Vector2D SmoothVelocityTransition(const Vector2D &currentVel,
                                  const Vector2D &targetVel,
                                  float smoothingFactor = 0.15f,
                                  float maxChange = 50.0f);

// Counts nearby entities within a given area, filtering for actual entities only
// (excludes static objects, triggers, and self)
// - entity: the querying entity (excluded from count)
// - center: center of query area
// - radius: query radius
// Returns: count of nearby dynamic/kinematic entities
int CountNearbyEntities(EntityPtr entity, const Vector2D &center, float radius);

// Gets nearby entities with their positions for crowd analysis
// - entity: the querying entity (excluded from results)
// - center: center of query area
// - radius: query radius
// - outPositions: vector to fill with nearby entity positions
// Returns: count of nearby entities (same as outPositions.size())
int GetNearbyEntitiesWithPositions(EntityPtr entity, const Vector2D &center, float radius, 
                                   std::vector<Vector2D> &outPositions);

} // namespace AIInternal

#endif // AI_INTERNAL_CROWD_HPP

