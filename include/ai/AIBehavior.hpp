/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <vector>

// Forward declarations
class PathfinderManager;

// Forward declare separation to avoid pulling internal headers here
namespace AIInternal {
Vector2D ApplySeparation(EntityPtr entity, const Vector2D &position,
                         const Vector2D &intendedVelocity, float speed,
                         float queryRadius, float strength,
                         size_t maxNeighbors);

// Overload with pre-fetched neighbor data
Vector2D ApplySeparation(EntityPtr entity, const Vector2D &position,
                         const Vector2D &intendedVelocity, float speed,
                         float queryRadius, float strength,
                         size_t maxNeighbors,
                         const std::vector<Vector2D> &preFetchedNeighbors);
}
#include <string>

class AIBehavior {
public:
  virtual ~AIBehavior();

  // Core behavior methods - pure logic only
  virtual void executeLogic(EntityPtr entity) = 0;
  virtual void init(EntityPtr entity) = 0;
  virtual void clean(EntityPtr entity) = 0;

  // Behavior identification
  virtual std::string getName() const = 0;

  // Optional message handling for behavior communication
  virtual void onMessage([[maybe_unused]] EntityPtr entity,
                         [[maybe_unused]] const std::string &message) {}

  // Behavior state access
  virtual bool isActive() const { return m_active; }
  virtual void setActive(bool active) { m_active = active; }

  // Entity range checks (behavior-specific logic)
  virtual bool isEntityInRange([[maybe_unused]] EntityPtr entity) const {
    return true;
  }

  // Entity cleanup
  virtual void cleanupEntity(EntityPtr entity);

  // Clone method for creating unique behavior instances
  virtual std::shared_ptr<AIBehavior> clone() const = 0;


  // Expose to AIManager for behavior management
  friend class AIManager;

protected:
  bool m_active{true};
  // PERFORMANCE FIX: Dramatically increased separation decimation interval (2 seconds)
  static constexpr Uint32 kSeparationIntervalMs = 2000;
  
  // Cached PathfinderManager reference for all behaviors to eliminate Instance() calls
  PathfinderManager& pathfinder() const;

  // OBSTACLE DETECTION: Check if entity is stuck and needs path refresh
  // Returns true if entity hasn't made progress for 800ms
  inline bool isStuckOnObstacle(Uint64 lastProgressTime, Uint64 now) const {
    static constexpr Uint64 STUCK_THRESHOLD_MS = 800;
    return (lastProgressTime > 0 && (now - lastProgressTime) > STUCK_THRESHOLD_MS);
  }

  // Apply separation at most every kSeparationIntervalMs, with entity-based staggering
  inline void applyDecimatedSeparation(EntityPtr entity,
                                       const Vector2D &position,
                                       const Vector2D &intendedVelocity,
                                       float speed, float queryRadius,
                                       float strength, int maxNeighbors,
                                       Uint64 &lastSepTick,
                                       Vector2D &lastSepVelocity) const {
    Uint64 now = SDL_GetTicks();

    // PERFORMANCE FIX: Entity-based staggered separation to prevent all entities
    // from doing expensive separation calculations on the same frame
    Uint32 entityStaggerOffset = (entity->getID() % 200) * 10; // Stagger by up to 2 seconds
    Uint32 effectiveInterval = kSeparationIntervalMs + entityStaggerOffset;

    if (now - lastSepTick >= effectiveInterval) {
      // Only do the expensive separation calculation when absolutely necessary
      lastSepVelocity = AIInternal::ApplySeparation(
          entity, position, intendedVelocity, speed, queryRadius, strength,
          static_cast<size_t>(maxNeighbors));
      lastSepTick = now;
    }
    entity->setVelocity(lastSepVelocity);
  }

  // PERFORMANCE OPTIMIZATION: Apply decimated separation using pre-fetched neighbor data
  // This version maintains the same decimation logic but uses cached collision data
  inline void applySeparationWithCache(EntityPtr entity,
                                       const Vector2D &position,
                                       const Vector2D &intendedVelocity,
                                       float speed, float queryRadius,
                                       float strength, int maxNeighbors,
                                       Uint64 &lastSepTick,
                                       Vector2D &lastSepVelocity,
                                       const std::vector<Vector2D> &preFetchedNeighbors) const {
    Uint64 now = SDL_GetTicks();

    // PERFORMANCE FIX: Entity-based staggered separation to prevent all entities
    // from doing expensive separation calculations on the same frame
    Uint32 entityStaggerOffset = (entity->getID() % 200) * 10; // Stagger by up to 2 seconds
    Uint32 effectiveInterval = kSeparationIntervalMs + entityStaggerOffset;

    if (now - lastSepTick >= effectiveInterval) {
      // Calculate separation using pre-fetched data (no collision query!)
      lastSepVelocity = AIInternal::ApplySeparation(
          entity, position, intendedVelocity, speed, queryRadius, strength,
          static_cast<size_t>(maxNeighbors), preFetchedNeighbors);
      lastSepTick = now;
    }
    entity->setVelocity(lastSepVelocity);
  }
};

#endif // AI_BEHAVIOR_HPP
