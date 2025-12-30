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
#include <limits>
#include <memory>
#include <vector>

// Forward declarations
class PathfinderManager;
struct TransformData;
struct EntityHotData;

/**
 * @brief Context for behavior execution - provides lock-free access to entity data
 *
 * This replaces EntityPtr in the hot path. AIManager resolves the EDM index once
 * and passes direct references to transform/hotData. No mutex per behavior call.
 */
struct BehaviorContext {
    TransformData& transform;      // Direct read/write access (lock-free)
    EntityHotData& hotData;        // Entity metadata (halfWidth, halfHeight, etc.)
    EntityHandle::IDType entityId; // For staggering calculations
    float deltaTime;

    BehaviorContext(TransformData& t, EntityHotData& h, EntityHandle::IDType id, float dt)
        : transform(t), hotData(h), entityId(id), deltaTime(dt) {}
};

#include <string>

class AIBehavior : public std::enable_shared_from_this<AIBehavior> {
public:
  virtual ~AIBehavior();

  // =========================================================================
  // CORE BEHAVIOR METHODS
  // =========================================================================

  /**
   * @brief Execute behavior logic with lock-free EDM access
   *
   * Hot path method called every frame. Receives direct references to
   * EntityDataManager data - no mutex acquisition per call.
   *
   * @param ctx BehaviorContext with transform, hotData, entityId, deltaTime
   */
  virtual void executeLogic(BehaviorContext& ctx) = 0;

  /**
   * @brief Legacy executeLogic for compatibility (DEPRECATED - DO NOT USE IN NEW CODE)
   *
   * This method triggers mutex locks per Entity accessor call.
   * Only use for behaviors not yet migrated to BehaviorContext.
   * Default implementation calls the new executeLogic(BehaviorContext&).
   */
  virtual void executeLogic(EntityPtr entity, float deltaTime);

  // Initialization and cleanup (called rarely, EntityPtr is fine)
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

  // Common state structure for pathfinding and movement behaviors
  struct AIBehaviorState {
    // Pathfinding state
    std::vector<Vector2D> pathPoints;
    size_t currentPathIndex = 0;
    float navRadius = 64.0f;
    float pathUpdateTimer = 0.0f;
    float progressTimer = 0.0f;
    float lastNodeDistance = std::numeric_limits<float>::infinity();

    // Separation state
    float separationTimer = 0.0f;
    Vector2D lastSepVelocity{0, 0};

    // Cooldown timers
    float pathRequestCooldown = 0.0f;
    float backoffTimer = 0.0f;

    // Crowd analysis caching (optional per behavior)
    float lastCrowdAnalysis = 0.0f;
    int cachedNearbyCount = 0;
    std::vector<Vector2D> cachedNearbyPositions;
    Vector2D cachedClusterCenter{0, 0}; // OPTIMIZATION: Cache cluster center to avoid std::accumulate

    // Memory management: trim vector capacity to prevent unbounded growth
    void trimVectorCapacity() {
      if (pathPoints.capacity() > 50 && pathPoints.size() < 20) {
        pathPoints.shrink_to_fit();
      }
      if (cachedNearbyPositions.capacity() > 100 && cachedNearbyPositions.size() < 50) {
        cachedNearbyPositions.shrink_to_fit();
      }
    }
  };

  // Cached PathfinderManager reference for all behaviors to eliminate Instance() calls
  PathfinderManager& pathfinder() const;

  // OBSTACLE DETECTION: Check if entity is stuck and needs path refresh
  // Returns true if entity hasn't made progress for 800ms
  inline bool isStuckOnObstacle(Uint64 lastProgressTime, Uint64 now) const {
    static constexpr Uint64 STUCK_THRESHOLD_MS = 800;
    return (lastProgressTime > 0 && (now - lastProgressTime) > STUCK_THRESHOLD_MS);
  }

  // NOTE: Separation functions removed - CollisionManager handles overlap resolution

  // Common utility functions for behaviors

  // Move entity towards target position using pathfinding
  // Priority: 0=Low, 1=Normal, 2=High, 3=Critical (maps to PathfinderManager::Priority)
  void moveToPosition(EntityPtr entity, const Vector2D &targetPos,
                     float speed, float deltaTime, AIBehaviorState &state,
                     int priority = 1); // Default to Normal priority

  // Vector and angle utilities
  Vector2D normalizeDirection(const Vector2D &vector) const;
  float calculateAngleToTarget(const Vector2D &from, const Vector2D &to) const;
  float normalizeAngle(float angle) const;
  Vector2D rotateVector(const Vector2D &vector, float angle) const;

  // =========================================================================
  // LOCK-FREE HELPER METHODS (use BehaviorContext instead of EntityPtr)
  // =========================================================================

  /**
   * @brief Move entity towards target using pathfinding (LOCK-FREE version)
   */
  void moveToPositionDirect(BehaviorContext& ctx, const Vector2D &targetPos,
                            float speed, AIBehaviorState &state,
                            int priority = 1);
};

#endif // AI_BEHAVIOR_HPP
