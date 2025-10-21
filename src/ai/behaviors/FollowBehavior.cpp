/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/FollowBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"  // For PathPriority enum
#include "core/Logger.hpp"
#include <algorithm>
#include <cmath>
#include "managers/WorldManager.hpp"

// Static member initialization
int FollowBehavior::s_nextFormationSlot = 0;
std::vector<Vector2D> FollowBehavior::s_escortFormationOffsets;

FollowBehavior::FollowBehavior(float followSpeed, float followDistance,
                               float maxDistance)
    : m_followSpeed(followSpeed), m_followDistance(followDistance),
      m_maxDistance(maxDistance) {
  initializeFormationOffsets();
}

FollowBehavior::FollowBehavior(FollowMode mode, float followSpeed)
    : m_followMode(mode), m_followSpeed(followSpeed) {
  initializeFormationOffsets();

  // Adjust parameters based on mode
  switch (mode) {
  case FollowMode::CLOSE_FOLLOW:
    m_followDistance = 50.0f;
    m_maxDistance = 150.0f;
    m_catchUpSpeedMultiplier = 2.0f;
    break;
  case FollowMode::LOOSE_FOLLOW:
    m_followDistance = 120.0f;
    m_maxDistance = 300.0f;
    m_catchUpSpeedMultiplier = 1.5f;
    break;
  case FollowMode::FLANKING_FOLLOW:
    m_followDistance = 100.0f;
    m_maxDistance = 250.0f;
    m_formationOffset = Vector2D(80.0f, 0.0f);
    break;
  case FollowMode::REAR_GUARD:
    m_followDistance = 150.0f;
    m_maxDistance = 350.0f;
    m_formationOffset = Vector2D(0.0f, -120.0f);
    break;
  case FollowMode::ESCORT_FORMATION:
    m_followDistance = 100.0f;
    m_maxDistance = 250.0f;
    m_formationRadius = 100.0f;
    break;
  }
}

void FollowBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  auto &state = m_entityStates[entity];
  state = EntityState(); // Reset to default state

  EntityPtr target = getTarget();
  if (target) {
    state.lastTargetPosition = target->getPosition();  // Use live position instead of cached
    state.desiredPosition = entity->getPosition();
    state.isFollowing = true;
  }
  // Assign formation slot for all follow modes to prevent clumping
  state.formationSlot = assignFormationSlot();
  state.formationOffset = calculateFormationOffset(state);
  if (m_followMode == FollowMode::ESCORT_FORMATION) {
    state.inFormation = true;
  }
}

void FollowBehavior::executeLogic(EntityPtr entity, float deltaTime) {
  if (!entity || !isActive())
    return;

  auto it = m_entityStates.find(entity);
  if (it == m_entityStates.end()) {
    init(entity);
    it = m_entityStates.find(entity);
    if (it == m_entityStates.end())
      return;
  }

  EntityState &state = it->second;
  EntityPtr target = getTarget();

  if (!target) {
    // No target, stop following
    AI_ERROR("FollowBehavior: No target found for entity " + std::to_string(entity->getID()));
    state.isFollowing = false;
    return;
  }

  Vector2D currentPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();  // Use live position instead of cached

  // Update target movement tracking (velocity-based, no delay)
  bool targetMoved = isTargetMoving(target);

  if (targetMoved) {
    state.targetMoving = true;
  } else {
    state.targetMoving = false;
  }

  state.lastTargetPosition = targetPos;

  // If target is stationary, only stop if already in range (prevent path spam but allow catch-up)
  if (!state.targetMoving) {
    float distanceToPlayer = (currentPos - targetPos).length();
    const float CATCHUP_RANGE = 200.0f; // Let distant followers catch up before stopping

    if (distanceToPlayer < CATCHUP_RANGE) {
      // Close enough - stop to prevent path spam
      entity->setVelocity(Vector2D(0, 0));
      entity->setAcceleration(Vector2D(0, 0));
      state.progressTimer = 0.0f; // Reset progress timer
      return;
    }
    // Else: too far, keep following to catch up even though player stopped
  }

  // ALWAYS follow like a pet/party member - no range limits, never stop
  state.isFollowing = true;

  if (state.isFollowing) {
    // Increment timers (deltaTime in seconds, timers in seconds)
    state.pathUpdateTimer += deltaTime;
    state.progressTimer += deltaTime;
    if (state.backoffTimer > 0.0f) {
      state.backoffTimer -= deltaTime; // Countdown timer
    }

    // Path-following to desired position if available
    auto tryFollowPath = [&](Vector2D desiredPos, float speed)->bool {
      const float nodeRadius = 20.0f; // Increased for faster path following
      const float pathTTL = 10.0f; // 10 seconds - reduce path churn when stationary

      // Dynamic backoff: if in a backoff window, don't refresh — just try to follow existing path
      if (state.backoffTimer > 0.0f) {
        bool following = pathfinder().followPathStep(entity, currentPos,
                            state.pathPoints, state.currentPathIndex,
                            speed, nodeRadius);
        if (following) { state.progressTimer = 0.0f; }
        return following;
      }

      // PERFORMANCE: Reduce path request frequency with higher thresholds
      const float GOAL_CHANGE_THRESH_SQUARED = 200.0f * 200.0f; // Require 200px goal change to recalculate
      bool stale = state.pathUpdateTimer > pathTTL;
      bool goalChanged = true;
      if (!state.pathPoints.empty()) {
        Vector2D lastGoal = state.pathPoints.back();
        // Use squared distance for performance
        goalChanged = ((desiredPos - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
      }

      // OBSTACLE DETECTION: Force path refresh if stuck on obstacle (800ms = 0.8s)
      bool stuckOnObstacle = (state.progressTimer > 0.8f);

      if (stale || goalChanged || stuckOnObstacle) {
        auto& pf = this->pathfinder();
        pf.requestPath(entity->getID(), currentPos, desiredPos, PathfinderManager::Priority::Normal,
          [this, entity](EntityID, const std::vector<Vector2D>& path) {
            // Safely look up state when callback executes
            auto it = m_entityStates.find(entity);
            if (it != m_entityStates.end()) {
              it->second.pathPoints = path;
              it->second.currentPathIndex = 0;
              it->second.pathUpdateTimer = 0.0f;
            }
          });
      }

      bool pathStep = pathfinder().followPathStep(entity, currentPos,
                          state.pathPoints, state.currentPathIndex,
                          speed, nodeRadius);
      if (pathStep) { state.progressTimer = 0.0f; }

      // REMOVED: Separation was overwriting path-following velocity with stale cached data
      // Follow behavior uses followDistance (50px) to maintain spacing, so aggressive
      // separation isn't needed and was causing erratic movement

      return pathStep;
    };

    // Stall detection: only check when not actively following a fresh path AND not intentionally stopped
    // Prevents false positives when NPCs naturally slow down near waypoints or are stopped at personal space
    bool hasActivePath = !state.pathPoints.empty() && state.pathUpdateTimer < 2.0f;

    if (!hasActivePath && !state.isStopped) {
      float speedNow = entity->getVelocity().length();
      const float stallSpeed = std::max(0.5f, m_followSpeed * 0.5f);
      const float stallTime = 0.6f; // 600ms
      if (speedNow < stallSpeed) {
        if (state.progressTimer > stallTime) {
          // Enter a brief backoff to reduce clumping; vary per-entity
          state.backoffTimer = 0.25f + (entity->getID() % 400) * 0.001f; // 250-650ms
          // Clear path and small micro-jitter to yield
          state.pathPoints.clear(); state.currentPathIndex = 0; state.pathUpdateTimer = 0.0f;
          float jitter = ((float)rand() / RAND_MAX - 0.5f) * 0.3f; // ~±17deg
          Vector2D v = entity->getVelocity(); if (v.length() < 0.01f) v = Vector2D(1,0);
          float c = std::cos(jitter), s = std::sin(jitter);
          Vector2D rotated(v.getX()*c - v.getY()*s, v.getX()*s + v.getY()*c);
          // Use reduced speed for stall recovery to prevent shooting off at high speed
          rotated.normalize(); entity->setVelocity(rotated * (m_followSpeed * 0.5f));
          state.progressTimer = 0.0f;
        }
      } else {
        state.progressTimer = 0.0f;
      }
    }

    // Calculate desired position with formation offset
    Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
    float distanceToDesired = (currentPos - desiredPos).length();

    // CRITICAL: Check distance to PLAYER for stop (prevent pushing)
    // Use distance to formation for pathfinding
    float distanceToPlayer = (currentPos - targetPos).length();

    // ARRIVAL RADIUS: If very close to desired formation position, stop (prevent micro-oscillations)
    const float ARRIVAL_RADIUS = 25.0f;
    if (distanceToDesired < ARRIVAL_RADIUS && !state.isStopped) {
      entity->setVelocity(Vector2D(0, 0));
      entity->setAcceleration(Vector2D(0, 0));
      state.progressTimer = 0.0f;
      state.isStopped = true;
      state.pathPoints.clear();
      state.currentPathIndex = 0;
      return;
    }

    // Hysteresis to prevent jittering at boundary
    // Stop at 40px from PLAYER (not formation), resume at 55px (prevents pushing player)
    if (state.isStopped) {
      // Already stopped - only resume if beyond resume distance FROM PLAYER
      if (distanceToPlayer < m_resumeDistance) {
        entity->setVelocity(Vector2D(0, 0));
        entity->setAcceleration(Vector2D(0, 0)); // Clear acceleration too
        state.progressTimer = 0.0f;
        return;
      }
      // Resuming - clear the stopped flag and any old path data
      state.isStopped = false;
      state.pathPoints.clear();
      state.currentPathIndex = 0;
    } else {
      // Moving - check if we should stop based on distance to PLAYER
      if (distanceToPlayer < m_stopDistance) {
        entity->setVelocity(Vector2D(0, 0));
        entity->setAcceleration(Vector2D(0, 0)); // Clear acceleration too
        state.progressTimer = 0.0f;
        state.isStopped = true;
        // Clear path to prevent any residual path-following
        state.pathPoints.clear();
        state.currentPathIndex = 0;
        return;
      }
    }

    // Use distance to player for catch-up speed calculation
    float dynamicSpeed = calculateFollowSpeed(entity, state, distanceToPlayer);

    // Execute appropriate follow behavior based on mode (use pre-calculated desiredPos)
    // Track whether we're using pathfinding or direct movement
    bool usingPathfinding = false;
    switch (m_followMode) {
    case FollowMode::CLOSE_FOLLOW:
      usingPathfinding = tryFollowPath(desiredPos, dynamicSpeed);
      if (!usingPathfinding)
        updateCloseFollow(entity, state);
      break;
    case FollowMode::LOOSE_FOLLOW:
      usingPathfinding = tryFollowPath(desiredPos, dynamicSpeed);
      if (!usingPathfinding)
        updateLooseFollow(entity, state);
      break;
    case FollowMode::FLANKING_FOLLOW:
      usingPathfinding = tryFollowPath(desiredPos, dynamicSpeed);
      if (!usingPathfinding)
        updateFlankingFollow(entity, state);
      break;
    case FollowMode::REAR_GUARD:
      usingPathfinding = tryFollowPath(desiredPos, dynamicSpeed);
      if (!usingPathfinding)
        updateRearGuard(entity, state);
      break;
    case FollowMode::ESCORT_FORMATION:
      usingPathfinding = tryFollowPath(desiredPos, dynamicSpeed);
      if (!usingPathfinding)
        updateEscortFormation(entity, state);
      break;
    }

    // Only apply separation during DIRECT MOVEMENT (not pathfinding)
    // Pathfinder already handles obstacle avoidance; separation during pathfinding causes oscillation
    if (!usingPathfinding) {
      applyAdditiveDecimatedSeparation(entity, currentPos, entity->getVelocity(),
                                       dynamicSpeed, 25.0f, 0.08f, 8,
                                       state.separationTimer, state.lastSepForce, deltaTime);
    }
  }
}

void FollowBehavior::clean(EntityPtr entity) {
  if (entity) {
    auto it = m_entityStates.find(entity);
    if (it != m_entityStates.end()) {
      // Release formation slot if using escort formation
      if (m_followMode == FollowMode::ESCORT_FORMATION) {
        releaseFormationSlot(it->second.formationSlot);
      }
      m_entityStates.erase(it);
    }
  }
}

void FollowBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (!entity)
    return;

  auto it = m_entityStates.find(entity);
  if (it == m_entityStates.end())
    return;

  EntityState &state = it->second;

  if (message == "follow_close") {
    setFollowMode(FollowMode::CLOSE_FOLLOW);
  } else if (message == "follow_loose") {
    setFollowMode(FollowMode::LOOSE_FOLLOW);
  } else if (message == "follow_flank") {
    setFollowMode(FollowMode::FLANKING_FOLLOW);
  } else if (message == "follow_rear") {
    setFollowMode(FollowMode::REAR_GUARD);
  } else if (message == "follow_formation") {
    setFollowMode(FollowMode::ESCORT_FORMATION);
  } else if (message == "stop_following") {
    state.isFollowing = false;
  } else if (message == "start_following") {
    state.isFollowing = true;
  } else if (message == "reset_formation") {
    if (m_followMode == FollowMode::ESCORT_FORMATION) {
      releaseFormationSlot(state.formationSlot);
      state.formationSlot = assignFormationSlot();
      state.formationOffset = calculateFormationOffset(state);
    }
  }
}

std::string FollowBehavior::getName() const { return "Follow"; }

void FollowBehavior::setFollowSpeed(float speed) {
  m_followSpeed = std::max(0.1f, speed);
}

void FollowBehavior::setFollowDistance(float distance) {
  m_followDistance = std::max(0.0f, distance);
}

void FollowBehavior::setMaxDistance(float maxDistance) {
  m_maxDistance = std::max(m_followDistance, maxDistance);
}

void FollowBehavior::setFollowMode(FollowMode mode) {
  m_followMode = mode;

  // Update all entity states for new mode
  for (auto &pair : m_entityStates) {
    EntityState &state = pair.second;
    if (mode == FollowMode::ESCORT_FORMATION && state.formationSlot == 0) {
      state.formationSlot = assignFormationSlot();
    } else if (mode != FollowMode::ESCORT_FORMATION &&
               state.formationSlot != 0) {
      releaseFormationSlot(state.formationSlot);
      state.formationSlot = 0;
    }
    state.formationOffset = calculateFormationOffset(state);
  }
}

void FollowBehavior::setCatchUpSpeed(float speedMultiplier) {
  m_catchUpSpeedMultiplier = std::max(1.0f, speedMultiplier);
}

void FollowBehavior::setFormationOffset(const Vector2D &offset) {
  m_formationOffset = offset;

  // Update non-escort formation entities
  if (m_followMode != FollowMode::ESCORT_FORMATION) {
    for (auto &pair : m_entityStates) {
      pair.second.formationOffset = offset;
    }
  }
}

void FollowBehavior::setAvoidanceRadius(float radius) {
  m_avoidanceRadius = std::max(0.0f, radius);
}

void FollowBehavior::setPathSmoothing(bool enabled) {
  m_pathSmoothing = enabled;
}

void FollowBehavior::setMaxTurnRate(float degreesPerSecond) {
  m_maxTurnRate = std::max(0.0f, degreesPerSecond);
}

void FollowBehavior::setStopWhenTargetStops(bool stopWhenTargetStops) {
  m_stopWhenTargetStops = stopWhenTargetStops;
}

void FollowBehavior::setMinimumMovementThreshold(float threshold) {
  m_minimumMovementThreshold = std::max(0.0f, threshold);
}

void FollowBehavior::setPredictiveFollowing(bool enabled,
                                            float predictionTime) {
  m_predictiveFollowing = enabled;
  m_predictionTime = std::max(0.0f, predictionTime);
}

bool FollowBehavior::isFollowing() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto &pair) { return pair.second.isFollowing; });
}

bool FollowBehavior::isInFormation() const {
  if (m_followMode != FollowMode::ESCORT_FORMATION)
    return true;
  return std::all_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto &pair) { return pair.second.inFormation; });
}

float FollowBehavior::getDistanceToTarget() const {
  EntityPtr target = getTarget();
  if (!target)
    return -1.0f;
  auto it =
      std::find_if(m_entityStates.begin(), m_entityStates.end(),
                   [](const auto &pair) { return pair.second.isFollowing; });
  if (it != m_entityStates.end()) {
    // PERFORMANCE: Only compute sqrt when actually returning distance
    float distSquared = (it->first->getPosition() - target->getPosition()).lengthSquared();
    return std::sqrt(distSquared);
  }
  return -1.0f;
}

FollowBehavior::FollowMode FollowBehavior::getFollowMode() const {
  return m_followMode;
}

Vector2D FollowBehavior::getTargetPosition() const {
  EntityPtr target = getTarget();
  return target ? target->getPosition() : Vector2D(0, 0);
}

std::shared_ptr<AIBehavior> FollowBehavior::clone() const {
  auto clone = std::make_shared<FollowBehavior>(m_followMode, m_followSpeed);
  clone->m_followDistance = m_followDistance;
  clone->m_maxDistance = m_maxDistance;
  clone->m_catchUpSpeedMultiplier = m_catchUpSpeedMultiplier;
  clone->m_formationOffset = m_formationOffset;
  clone->m_formationRadius = m_formationRadius;
  clone->m_avoidanceRadius = m_avoidanceRadius;
  clone->m_maxTurnRate = m_maxTurnRate;
  clone->m_minimumMovementThreshold = m_minimumMovementThreshold;
  clone->m_pathSmoothing = m_pathSmoothing;
  clone->m_stopWhenTargetStops = m_stopWhenTargetStops;
  clone->m_predictiveFollowing = m_predictiveFollowing;
  clone->m_predictionTime = m_predictionTime;
  // PATHFINDING CONSOLIDATION: Removed async flag - always uses PathfindingScheduler now
  return clone;
}

EntityPtr FollowBehavior::getTarget() const {
  return AIManager::Instance().getPlayerReference();
}

Vector2D FollowBehavior::calculateDesiredPosition(EntityPtr entity,
                                                  EntityPtr target,
                                                  const EntityState &state) const {
  if (!entity || !target)
    return Vector2D(0, 0);

  Vector2D targetPos = target->getPosition();  // Use live position instead of cached

  // Apply predictive following if enabled
  if (m_predictiveFollowing && state.targetMoving) {
    targetPos = predictTargetPosition(target, state);
  }

  // Apply formation offset
  Vector2D desiredPos = targetPos + state.formationOffset;

  return desiredPos;
}

Vector2D
FollowBehavior::calculateFormationOffset(const EntityState &state) const {
  switch (m_followMode) {
  case FollowMode::CLOSE_FOLLOW:
    return Vector2D(0, 0);

  case FollowMode::LOOSE_FOLLOW:
    // First 3 NPCs: tight pet formation, stay very close
    if (state.formationSlot < 3) {
      float angle = (state.formationSlot * 2.0f * M_PI / 3.0f) + (M_PI / 2.0f); // Spread behind (90-270 degrees)
      float distance = 40.0f + (state.formationSlot * 15.0f); // 40px, 55px, 70px
      return Vector2D(std::cos(angle) * distance, std::sin(angle) * distance);
    }
    // Next 3 NPCs: medium formation ring
    else if (state.formationSlot < 6) {
      float angle = ((state.formationSlot - 3) * 2.0f * M_PI / 3.0f) + (M_PI / 6.0f);
      float distance = 85.0f + ((state.formationSlot - 3) * 15.0f); // 85px, 100px, 115px
      return Vector2D(std::cos(angle) * distance, std::sin(angle) * distance);
    }
    // Additional NPCs: wide spread using golden angle for even distribution
    else {
      float angle = (state.formationSlot * 0.618f * 2.0f * M_PI); // Golden angle
      float distance = 130.0f + ((state.formationSlot % 4) * 20.0f); // 130-190px range
      return Vector2D(std::cos(angle) * distance, std::sin(angle) * distance);
    }

  case FollowMode::FLANKING_FOLLOW:
    // Alternate left and right flanking positions
    return Vector2D(m_formationOffset.getX() *
                        (state.formationSlot % 2 == 0 ? 1 : -1),
                    m_formationOffset.getY());

  case FollowMode::REAR_GUARD:
    return m_formationOffset;

  case FollowMode::ESCORT_FORMATION:
    if (state.formationSlot <
        static_cast<int>(s_escortFormationOffsets.size())) {
      return s_escortFormationOffsets[state.formationSlot] * m_formationRadius;
    }
    break;
  }

  return Vector2D(0, 0);
}

Vector2D FollowBehavior::predictTargetPosition(EntityPtr target,
                                               const EntityState &state) const {
  if (!target || !state.targetMoving) {
    return target ? target->getPosition() : Vector2D(0, 0);
  }

  Vector2D currentPos = target->getPosition();  // Use live position instead of cached
  Vector2D velocity =
      (currentPos - state.lastTargetPosition) / 0.016f; // Assume 60 FPS

  return currentPos + velocity * m_predictionTime;
}

bool FollowBehavior::isTargetMoving(EntityPtr target) const {
  if (!target)
    return false;

  // Check velocity instead of position - more reliable for detecting actual movement
  Vector2D targetVel = target->getVelocity();
  float velocityMagnitude = targetVel.length();

  // Consider target moving if velocity > 10 pixels/second
  const float VELOCITY_THRESHOLD = 10.0f;
  return velocityMagnitude > VELOCITY_THRESHOLD;
}

bool FollowBehavior::shouldCatchUp(float distanceToTarget) const {
  return distanceToTarget > m_maxDistance;
}

float FollowBehavior::calculateFollowSpeed(EntityPtr /*entity*/,
                                           const EntityState & /*state*/,
                                           float distanceToTarget) const {
  float speed = m_followSpeed;

  // Apply catch-up speed if far behind
  if (shouldCatchUp(distanceToTarget)) {
    speed *= m_catchUpSpeedMultiplier;
  }

  // Reduce speed if very close to desired position
  if (distanceToTarget < m_followDistance * 0.5f) {
    speed *= 0.7f;
  }

  return speed;
}


Vector2D FollowBehavior::avoidObstacles(EntityPtr /*entity*/,
                                        const Vector2D &desiredVelocity) const {
  // Simple obstacle avoidance - in a full implementation, this would check for
  // collisions For now, just return the desired velocity
  return desiredVelocity;
}

Vector2D FollowBehavior::smoothPath(const Vector2D &currentPos,
                                    const Vector2D &targetPos,
                                    const EntityState &state) const {
  if (!m_pathSmoothing) {
    return targetPos - currentPos;
  }

  // Simple path smoothing - blend current direction with desired direction
  Vector2D desiredDirection = normalizeVector(targetPos - currentPos);
  Vector2D currentDirection = normalizeVector(state.currentVelocity);

  if (currentDirection.length() < 0.001f) {
    return desiredDirection;
  }

  // Blend directions for smoother turning
  Vector2D blendedDirection =
      (currentDirection * 0.7f + desiredDirection * 0.3f);
  return normalizeVector(blendedDirection);
}

void FollowBehavior::updateCloseFollow(EntityPtr entity, EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
  // PERFORMANCE: Use squared distance
  float distanceToDesiredSquared = (currentPos - desiredPos).lengthSquared();
  float thresholdSquared = (m_followDistance * 0.3f) * (m_followDistance * 0.3f);

  if (distanceToDesiredSquared > thresholdSquared) {
    // Compute actual distance only when needed for speed calculation
    float distanceToDesired = std::sqrt(distanceToDesiredSquared);
    float speed = calculateFollowSpeed(entity, state, distanceToDesired);
    Vector2D direction = normalizeVector(desiredPos - currentPos);

    if (m_pathSmoothing) {
      direction = smoothPath(currentPos, desiredPos, state);
    }

    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);

    state.currentVelocity = direction * speed;
  }
}

void FollowBehavior::updateLooseFollow(EntityPtr entity, EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
  // PERFORMANCE: Use squared distance
  float distanceToDesiredSquared = (currentPos - desiredPos).lengthSquared();
  float followDistanceSquared = m_followDistance * m_followDistance;

  // Only move if outside the follow distance
  if (distanceToDesiredSquared > followDistanceSquared) {
    // Compute actual distance only when needed for speed calculation
    float distanceToDesired = std::sqrt(distanceToDesiredSquared);
    float speed = calculateFollowSpeed(entity, state, distanceToDesired);
    Vector2D direction = normalizeVector(desiredPos - currentPos);

    if (m_pathSmoothing) {
      direction = smoothPath(currentPos, desiredPos, state);
    }

    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);

    state.currentVelocity = direction * speed;
  } else {
    state.currentVelocity = Vector2D(0, 0);
  }
}

void FollowBehavior::updateFlankingFollow(EntityPtr entity,
                                          EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
  float distanceToDesired = (currentPos - desiredPos).length();

  if (distanceToDesired > m_followDistance * 0.5f) {
    float speed = calculateFollowSpeed(entity, state, distanceToDesired);
    Vector2D direction = normalizeVector(desiredPos - currentPos);

    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);

    state.currentVelocity = velocity;
  }
}

void FollowBehavior::updateRearGuard(EntityPtr entity, EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
  float distanceToDesired = (currentPos - desiredPos).length();

  // Rear guard follows more conservatively
  if (distanceToDesired > m_followDistance * 0.8f) {
    float speed = calculateFollowSpeed(entity, state, distanceToDesired) *
                  0.8f; // Slightly slower
    Vector2D direction = normalizeVector(desiredPos - currentPos);

    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);

    state.currentVelocity = velocity;
  }
}

void FollowBehavior::updateEscortFormation(EntityPtr entity,
                                           EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredPos = calculateDesiredPosition(entity, target, state);
  float distanceToDesired = (currentPos - desiredPos).length();

  // Check if in formation
  state.inFormation = (distanceToDesired <= m_followDistance);

  if (distanceToDesired > m_followDistance * 0.4f) {
    float speed = calculateFollowSpeed(entity, state, distanceToDesired);
    Vector2D direction = normalizeVector(desiredPos - currentPos);

    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);

    state.currentVelocity = direction * speed;
  }
}

Vector2D FollowBehavior::normalizeVector(const Vector2D &vector) const {
  float magnitude = vector.length();
  if (magnitude < 0.001f) {
    return Vector2D(0, 0);
  }
  return vector / magnitude;
}


void FollowBehavior::initializeFormationOffsets() {
  if (s_escortFormationOffsets.empty()) {
    // Initialize escort formation positions (8 positions around target)
    s_escortFormationOffsets.reserve(8);

    for (int i = 0; i < 8; ++i) {
      float angle = (i * 2.0f * M_PI) / 8.0f;
      s_escortFormationOffsets.emplace_back(std::cos(angle), std::sin(angle));
    }
  }
}

int FollowBehavior::assignFormationSlot() {
  int slot = s_nextFormationSlot;
  s_nextFormationSlot = (s_nextFormationSlot + 1) % 8; // Cycle through 8 slots
  return slot;
}

void FollowBehavior::releaseFormationSlot(int /*slot*/) {
  // In a more sophisticated implementation, you might track which slots are in
  // use For now, we just cycle through slots
}
