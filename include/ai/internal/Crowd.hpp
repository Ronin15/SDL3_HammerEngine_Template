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

// NOTE: ApplySeparation functions removed - CollisionManager handles overlap resolution

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

// Invalidates spatial query cache for new frame
// Call this at the start of each AI update cycle to ensure cache freshness
// - frameNumber: current frame number for cache invalidation
void InvalidateSpatialCache(uint64_t frameNumber);

} // namespace AIInternal

#endif // AI_INTERNAL_CROWD_HPP

