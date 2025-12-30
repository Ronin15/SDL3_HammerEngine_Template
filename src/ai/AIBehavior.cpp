/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/AIBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"
#include <chrono>
#include <cmath>
#include <random>

namespace {
// Thread-safe RNG for stall recovery jitter
std::mt19937& getThreadLocalRNG() {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}
} // namespace

AIBehavior::~AIBehavior() = default;

// Legacy executeLogic - creates BehaviorContext from EntityPtr and calls new interface
// This triggers mutex locks via Entity accessors - use executeLogic(BehaviorContext&) instead
void AIBehavior::executeLogic(EntityPtr entity, float deltaTime) {
    if (!entity || !entity->hasValidHandle()) return;

    // Get EDM index (single mutex acquisition)
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(entity->getHandle());
    if (idx == SIZE_MAX) return;

    // Create BehaviorContext with direct references
    auto& transform = edm.getTransformByIndex(idx);
    auto& hotData = edm.getHotDataByIndex(idx);
    BehaviorContext ctx(transform, hotData, entity->getID(), deltaTime);

    // Call the new lock-free interface
    executeLogic(ctx);
}

void AIBehavior::cleanupEntity(EntityPtr entity) {
    // Default implementation does nothing
    (void)entity;
}

PathfinderManager& AIBehavior::pathfinder() const {
    // Static reference cached on first use - eliminates repeated Instance() calls
    static PathfinderManager& pf = PathfinderManager::Instance();
    return pf;
}

// Common utility function implementations

void AIBehavior::moveToPosition(EntityPtr entity, const Vector2D &targetPos,
                                float speed, float deltaTime, AIBehaviorState &state,
                                int priority) {
  if (!entity || speed <= 0.0f)
    return;

  // Cast int priority to PathfinderManager::Priority enum
  auto priorityEnum = static_cast<PathfinderManager::Priority>(priority);

  Vector2D currentPos = entity->getPosition();

  // PERFORMANCE: Increase path TTL and reduce refresh frequency
  constexpr float pathTTL = 3.0f;         // Path staleness threshold
  constexpr float noProgressWindow = 0.5f; // Stuck detection threshold
  bool needRefresh = state.pathPoints.empty() ||
                     state.currentPathIndex >= state.pathPoints.size();

  // Check for progress along current path
  if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
    float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
    if (d + 1.0f < state.lastNodeDistance) {
      state.lastNodeDistance = d;
      state.progressTimer = 0.0f;
    } else if (state.progressTimer > noProgressWindow) {
      needRefresh = true; // Stuck detection
    }
  }

  // Check if path is stale
  if (state.pathUpdateTimer > pathTTL)
    needRefresh = true;

  // Check if goal changed significantly
  if (!needRefresh && !state.pathPoints.empty()) {
    const float GOAL_CHANGE_THRESH_SQUARED = 150.0f * 150.0f;
    Vector2D lastGoal = state.pathPoints.back();
    float goalDeltaSquared = (targetPos - lastGoal).lengthSquared();
    if (goalDeltaSquared > GOAL_CHANGE_THRESH_SQUARED)
      needRefresh = true;
  }

  // Request new path if needed (respecting backoff timer)
  if (needRefresh && state.backoffTimer <= 0.0f) {
    Vector2D clampedStart = pathfinder().clampToWorldBounds(currentPos, 100.0f);
    Vector2D clampedGoal = pathfinder().clampToWorldBounds(targetPos, 100.0f);

    pathfinder().requestPath(
        entity->getID(), clampedStart, clampedGoal, priorityEnum,
        [&state](EntityID, const std::vector<Vector2D> &path) {
          if (!path.empty()) {
            state.pathPoints = path;
            state.currentPathIndex = 0;
            state.pathUpdateTimer = 0.0f;
            state.lastNodeDistance = std::numeric_limits<float>::infinity();
            state.progressTimer = 0.0f;
          }
        });

    // Apply per-entity backoff to prevent request spam
    state.backoffTimer = 0.3f + (entity->getID() % 300) * 0.001f;
  }

  // Follow path if available
  bool following = pathfinder().followPathStep(
      entity, currentPos, state.pathPoints, state.currentPathIndex, speed,
      state.navRadius);

  if (following) {
    state.progressTimer = 0.0f;
    // CollisionManager handles overlap resolution
  } else {
    // Fallback: direct movement
    Vector2D direction = normalizeDirection(targetPos - currentPos);
    if (direction.length() > 0.001f) {
      entity->setVelocity(direction * speed);
      state.progressTimer = 0.0f;
    }
  }

  // Stall detection and recovery
  float currentSpeed = entity->getVelocity().length();
  const float stallSpeed = std::max(0.5f, speed * 0.5f);
  constexpr float stallSeconds = 0.6f;

  if (currentSpeed < stallSpeed) {
    if (state.progressTimer > stallSeconds) {
      // Force path refresh with micro-jitter to break clumps
      state.pathPoints.clear();
      state.currentPathIndex = 0;
      state.pathUpdateTimer = 0.0f;
      state.progressTimer = 0.0f;
      state.backoffTimer = 0.2f + (entity->getID() % 400) * 0.001f;

      // Apply small random jitter (thread-safe)
      std::uniform_real_distribution<float> jitterDist(-0.15f, 0.15f);
      float jitter = jitterDist(getThreadLocalRNG());
      Vector2D v = entity->getVelocity();
      if (v.length() < 0.01f)
        v = Vector2D(1, 0);
      float c = std::cos(jitter), s = std::sin(jitter);
      Vector2D rotated(v.getX() * c - v.getY() * s,
                      v.getX() * s + v.getY() * c);
      rotated.normalize();
      entity->setVelocity(rotated * speed);
    }
  } else {
    state.progressTimer = 0.0f;
  }
}

Vector2D AIBehavior::normalizeDirection(const Vector2D &vector) const {
  float magnitude = vector.length();
  if (magnitude < 0.001f) {
    return Vector2D(0, 0);
  }
  return vector / magnitude;
}

float AIBehavior::calculateAngleToTarget(const Vector2D &from,
                                        const Vector2D &to) const {
  Vector2D direction = to - from;
  return std::atan2(direction.getY(), direction.getX());
}

float AIBehavior::normalizeAngle(float angle) const {
  while (angle > M_PI)
    angle -= 2.0f * M_PI;
  while (angle < -M_PI)
    angle += 2.0f * M_PI;
  return angle;
}

Vector2D AIBehavior::rotateVector(const Vector2D &vector, float angle) const {
  float cos_a = std::cos(angle);
  float sin_a = std::sin(angle);

  return Vector2D(vector.getX() * cos_a - vector.getY() * sin_a,
                  vector.getX() * sin_a + vector.getY() * cos_a);
}

// ============================================================================
// LOCK-FREE IMPLEMENTATIONS (use BehaviorContext)
// ============================================================================

void AIBehavior::moveToPositionDirect(BehaviorContext& ctx, const Vector2D &targetPos,
                                      float speed, AIBehaviorState &state,
                                      int priority) {
  if (speed <= 0.0f) return;

  auto priorityEnum = static_cast<PathfinderManager::Priority>(priority);
  Vector2D currentPos = ctx.transform.position;

  // PERFORMANCE: Increase path TTL and reduce refresh frequency
  constexpr float pathTTL = 3.0f;
  constexpr float noProgressWindow = 0.5f;
  bool needRefresh = state.pathPoints.empty() ||
                     state.currentPathIndex >= state.pathPoints.size();

  // Check for progress along current path
  if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
    float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
    if (d + 1.0f < state.lastNodeDistance) {
      state.lastNodeDistance = d;
      state.progressTimer = 0.0f;
    } else if (state.progressTimer > noProgressWindow) {
      needRefresh = true;
    }
  }

  if (state.pathUpdateTimer > pathTTL) needRefresh = true;

  // Check if goal changed significantly
  if (!needRefresh && !state.pathPoints.empty()) {
    const float GOAL_CHANGE_THRESH_SQUARED = 150.0f * 150.0f;
    Vector2D lastGoal = state.pathPoints.back();
    float goalDeltaSquared = (targetPos - lastGoal).lengthSquared();
    if (goalDeltaSquared > GOAL_CHANGE_THRESH_SQUARED) needRefresh = true;
  }

  // Request new path if needed
  if (needRefresh && state.backoffTimer <= 0.0f) {
    Vector2D clampedStart = pathfinder().clampToWorldBounds(currentPos, 100.0f);
    Vector2D clampedGoal = pathfinder().clampToWorldBounds(targetPos, 100.0f);

    pathfinder().requestPath(
        ctx.entityId, clampedStart, clampedGoal, priorityEnum,
        [&state](EntityID, const std::vector<Vector2D> &path) {
          if (!path.empty()) {
            state.pathPoints = path;
            state.currentPathIndex = 0;
            state.pathUpdateTimer = 0.0f;
            state.lastNodeDistance = std::numeric_limits<float>::infinity();
            state.progressTimer = 0.0f;
          }
        });

    state.backoffTimer = 0.3f + (ctx.entityId % 300) * 0.001f;
  }

  // Follow path - need to update velocity directly
  bool following = false;
  if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
    Vector2D waypoint = state.pathPoints[state.currentPathIndex];
    Vector2D toWaypoint = waypoint - currentPos;
    float dist = toWaypoint.length();

    if (dist < state.navRadius) {
      state.currentPathIndex++;
      if (state.currentPathIndex < state.pathPoints.size()) {
        waypoint = state.pathPoints[state.currentPathIndex];
        toWaypoint = waypoint - currentPos;
        dist = toWaypoint.length();
      }
    }

    if (dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * speed;
      following = true;
    }
  }

  if (following) {
    state.progressTimer = 0.0f;
    // CollisionManager handles overlap resolution
  } else {
    // Fallback: direct movement
    Vector2D direction = normalizeDirection(targetPos - currentPos);
    if (direction.length() > 0.001f) {
      ctx.transform.velocity = direction * speed;
      state.progressTimer = 0.0f;
    }
  }

  // Stall detection and recovery
  float currentSpeed = ctx.transform.velocity.length();
  const float stallSpeed = std::max(0.5f, speed * 0.5f);
  constexpr float stallSeconds = 0.6f;

  if (currentSpeed < stallSpeed) {
    if (state.progressTimer > stallSeconds) {
      state.pathPoints.clear();
      state.currentPathIndex = 0;
      state.pathUpdateTimer = 0.0f;
      state.progressTimer = 0.0f;
      state.backoffTimer = 0.2f + (ctx.entityId % 400) * 0.001f;

      std::uniform_real_distribution<float> jitterDist(-0.15f, 0.15f);
      float jitter = jitterDist(getThreadLocalRNG());
      Vector2D v = ctx.transform.velocity;
      if (v.length() < 0.01f) v = Vector2D(1, 0);
      float c = std::cos(jitter), s = std::sin(jitter);
      Vector2D rotated(v.getX() * c - v.getY() * s,
                       v.getX() * s + v.getY() * c);
      rotated.normalize();
      ctx.transform.velocity = rotated * speed;
    }
  } else {
    state.progressTimer = 0.0f;
  }
}

// NOTE: applyDecimatedSeparationDirect removed - CollisionManager handles overlap resolution



