/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "entities/EntityHandle.hpp"
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
    size_t edmIndex;               // EDM index for vector-based state storage (contention-free)
    float deltaTime;

    // Player info cached once per update batch - avoids lock contention in behaviors
    EntityHandle playerHandle;     // Cached player handle (no lock needed)
    Vector2D playerPosition;       // Cached player position (no lock needed)
    bool playerValid{false};       // Whether player is valid this frame

    BehaviorContext(TransformData& t, EntityHotData& h, EntityHandle::IDType id, size_t idx, float dt)
        : transform(t), hotData(h), entityId(id), edmIndex(idx), deltaTime(dt) {}

    BehaviorContext(TransformData& t, EntityHotData& h, EntityHandle::IDType id, size_t idx, float dt,
                    EntityHandle pHandle, const Vector2D& pPos, bool pValid)
        : transform(t), hotData(h), entityId(id), edmIndex(idx), deltaTime(dt),
          playerHandle(pHandle), playerPosition(pPos), playerValid(pValid) {}
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

  // Initialization and cleanup (called when behavior assigned/unassigned)
  virtual void init(EntityHandle handle) = 0;
  virtual void clean(EntityHandle handle) = 0;

  // Behavior identification
  virtual std::string getName() const = 0;

  // Optional message handling for behavior communication
  virtual void onMessage([[maybe_unused]] EntityHandle handle,
                         [[maybe_unused]] const std::string &message) {}

  // Behavior state access
  virtual bool isActive() const { return m_active; }
  virtual void setActive(bool active) { m_active = active; }

  // Entity range checks (behavior-specific logic)
  virtual bool isEntityInRange([[maybe_unused]] EntityHandle handle) const {
    return true;
  }

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

  // Vector and angle utilities
  Vector2D normalizeDirection(const Vector2D &vector) const;
  float calculateAngleToTarget(const Vector2D &from, const Vector2D &to) const;
  float normalizeAngle(float angle) const;
  Vector2D rotateVector(const Vector2D &vector, float angle) const;

  /**
   * @brief Move entity towards target using pathfinding (lock-free via BehaviorContext)
   * @param ctx BehaviorContext with direct transform access
   * @param targetPos Target position to move towards
   * @param speed Movement speed
   * @param state Behavior state for pathfinding data
   * @param priority 0=Low, 1=Normal, 2=High, 3=Critical
   */
  void moveToPosition(BehaviorContext& ctx, const Vector2D &targetPos,
                      float speed, AIBehaviorState &state,
                      int priority = 1);
};

#endif // AI_BEHAVIOR_HPP
