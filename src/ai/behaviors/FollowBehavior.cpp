/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/FollowBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"  // For PathPriority enum
#include "core/Logger.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include "managers/WorldManager.hpp"

namespace {
// Thread-safe RNG for stall recovery jitter
std::mt19937& getThreadLocalRNG() {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}
} // namespace

// Static member initialization (atomic for thread safety)
std::atomic<int> FollowBehavior::s_nextFormationSlot{0};
std::vector<Vector2D> FollowBehavior::s_escortFormationOffsets;
std::once_flag FollowBehavior::s_formationInitFlag;

FollowBehavior::FollowBehavior(float followSpeed, float followDistance,
                               float maxDistance)
    : m_followSpeed(followSpeed), m_followDistance(followDistance),
      m_maxDistance(maxDistance) {
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
  initializeFormationOffsets();
}

FollowBehavior::FollowBehavior(FollowMode mode, float followSpeed)
    : m_followMode(mode), m_followSpeed(followSpeed) {
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
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

FollowBehavior::FollowBehavior(const HammerEngine::FollowBehaviorConfig& config, FollowMode mode)
    : m_config(config)
    , m_followMode(mode)
    , m_followSpeed(config.followSpeed)
    , m_followDistance(config.followDistance)
    , m_maxDistance(config.catchupRange)
{
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
  initializeFormationOffsets();

  // Mode-specific adjustments using config values
  switch (mode) {
  case FollowMode::CLOSE_FOLLOW:
    m_followDistance = config.followDistance * 0.5f;
    m_maxDistance = config.catchupRange * 0.75f;
    m_catchUpSpeedMultiplier = 2.0f;
    break;
  case FollowMode::LOOSE_FOLLOW:
    m_followDistance = config.followDistance * 1.2f;
    m_catchUpSpeedMultiplier = 1.5f;
    break;
  case FollowMode::FLANKING_FOLLOW:
    m_formationOffset = Vector2D(config.followDistance * 0.8f, 0.0f);
    break;
  case FollowMode::REAR_GUARD:
    m_followDistance = config.followDistance * 1.5f;
    m_formationOffset = Vector2D(0.0f, -config.followDistance * 1.2f);
    break;
  case FollowMode::ESCORT_FORMATION:
    m_formationRadius = config.followDistance;
    break;
  }
}

void FollowBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  // Ensure vector is large enough for this index
  if (idx >= m_entityStatesByIndex.size()) {
    m_entityStatesByIndex.resize(idx + 1);
  }

  auto& hotData = edm.getHotDataByIndex(idx);
  auto &state = m_entityStatesByIndex[idx];
  state = EntityState(); // Reset to default state
  state.valid = true;

  // Phase 2 EDM Migration: Use handle-based target lookup
  EntityHandle targetHandle = getTargetHandle();
  if (targetHandle.isValid()) {
    size_t targetIdx = edm.getIndex(targetHandle);
    if (targetIdx != SIZE_MAX) {
      auto& targetHotData = edm.getHotDataByIndex(targetIdx);
      state.lastTargetPosition = targetHotData.transform.position;
      state.desiredPosition = hotData.transform.position;
      state.isFollowing = true;
    }
  }
  // Assign formation slot for all follow modes to prevent clumping
  state.formationSlot = assignFormationSlot();
  state.formationOffset = calculateFormationOffset(state);
  if (m_followMode == FollowMode::ESCORT_FORMATION) {
    state.inFormation = true;
  }
}

void FollowBehavior::executeLogic(BehaviorContext& ctx) {
  if (!isActive())
    return;

  // Get state by EDM index (contention-free vector access)
  if (ctx.edmIndex >= m_entityStatesByIndex.size()) {
    return;
  }
  EntityState &state = m_entityStatesByIndex[ctx.edmIndex];
  if (!state.valid) {
    return;
  }

  // Phase 2 EDM Migration: Use handle-based target lookup
  auto& edm = EntityDataManager::Instance();
  auto& pathData = edm.getPathData(ctx.edmIndex);  // Path state from EDM
  EntityHandle targetHandle = getTargetHandle();

  if (!targetHandle.isValid()) {
    // No target, stop following
    AI_ERROR(std::format("FollowBehavior: No target found for entity {}", ctx.entityId));
    state.isFollowing = false;
    return;
  }

  size_t targetIdx = edm.getIndex(targetHandle);
  if (targetIdx == SIZE_MAX) {
    state.isFollowing = false;
    return;
  }

  auto& targetHotData = edm.getHotDataByIndex(targetIdx);
  Vector2D currentPos = ctx.transform.position;
  Vector2D targetPos = targetHotData.transform.position;

  // Update target movement tracking (velocity-based, no delay)
  Vector2D targetVel = targetHotData.transform.velocity;
  bool targetMoved = (targetVel.length() > 10.0f); // VELOCITY_THRESHOLD

  if (targetMoved) {
    state.targetMoving = true;
  } else {
    state.targetMoving = false;
  }

  state.lastTargetPosition = targetPos;

  // If target is stationary, only stop if already in range (prevent path spam but allow catch-up)
  if (!state.targetMoving) {
    float const distanceToPlayer = (currentPos - targetPos).length();
    const float CATCHUP_RANGE = 200.0f; // Let distant followers catch up before stopping

    if (distanceToPlayer < CATCHUP_RANGE) {
      // Close enough - stop to prevent path spam
      ctx.transform.velocity = Vector2D(0, 0);
      ctx.transform.acceleration = Vector2D(0, 0);
      pathData.progressTimer = 0.0f; // Reset progress timer in EDM
      return;
    }
    // Else: too far, keep following to catch up even though player stopped
  }

  // ALWAYS follow like a pet/party member - no range limits, never stop
  state.isFollowing = true;

  // Increment path timers in EDM (single source of truth)
  pathData.pathUpdateTimer += ctx.deltaTime;
  pathData.progressTimer += ctx.deltaTime;
  if (state.backoffTimer > 0.0f) {
    state.backoffTimer -= ctx.deltaTime; // Countdown timer
  }

  // OPTIMIZATION: Path-following extracted to method for better compiler optimization

  // Stall detection: only check when not actively following a fresh path AND not intentionally stopped
  // Prevents false positives when NPCs naturally slow down near waypoints or are stopped at personal space
  bool hasActivePath = pathData.hasPath && pathData.pathUpdateTimer < 2.0f;

  if (!hasActivePath && !state.isStopped) {
    float speedNow = ctx.transform.velocity.length();
    const float stallSpeed = std::max(0.5f, m_followSpeed * 0.5f);
    const float stallTime = 0.6f; // 600ms
    if (speedNow < stallSpeed) {
      if (pathData.progressTimer > stallTime) {
        // Enter a brief backoff to reduce clumping; vary per-entity
        state.backoffTimer = 0.25f + (ctx.entityId % 400) * 0.001f; // 250-650ms
        // Clear path in EDM and small micro-jitter to yield
        pathData.clear();
        std::uniform_real_distribution<float> jitterDist(-0.15f, 0.15f);
        float jitter = jitterDist(getThreadLocalRNG()); // ~±17deg (thread-safe)
        Vector2D v = ctx.transform.velocity; if (v.length() < 0.01f) v = Vector2D(1,0);
        float c = std::cos(jitter), s = std::sin(jitter);
        Vector2D rotated(v.getX()*c - v.getY()*s, v.getX()*s + v.getY()*c);
        // Use reduced speed for stall recovery to prevent shooting off at high speed
        rotated.normalize(); ctx.transform.velocity = rotated * (m_followSpeed * 0.5f);
        pathData.progressTimer = 0.0f;
      }
    } else {
      pathData.progressTimer = 0.0f;
    }
  }

  // Calculate desired position with formation offset (inline to avoid EntityPtr dependency)
  Vector2D targetPosAdjusted = targetPos;
  if (m_predictiveFollowing && state.targetMoving) {
    // Predictive following: estimate where target will be
    Vector2D const velocity = (targetPos - state.lastTargetPosition) / 0.016f; // Assume 60 FPS
    targetPosAdjusted = targetPos + velocity * m_predictionTime;
  }
  Vector2D desiredPos = targetPosAdjusted + state.formationOffset;
  float const distanceToDesired = (currentPos - desiredPos).length();

  // CRITICAL: Check distance to PLAYER for stop (prevent pushing)
  // Use distance to formation for pathfinding
  float const distanceToPlayer = (currentPos - targetPos).length();

  // ARRIVAL RADIUS: If very close to desired formation position, stop (prevent micro-oscillations)
  const float ARRIVAL_RADIUS = 25.0f;
  if (distanceToDesired < ARRIVAL_RADIUS && !state.isStopped) {
    ctx.transform.velocity = Vector2D(0, 0);
    ctx.transform.acceleration = Vector2D(0, 0);
    pathData.progressTimer = 0.0f;
    state.isStopped = true;
    pathData.clear();
    return;
  }

  // Hysteresis to prevent jittering at boundary
  // Stop at 40px from PLAYER (not formation), resume at 55px (prevents pushing player)
  if (state.isStopped) {
    // Already stopped - only resume if beyond resume distance FROM PLAYER
    if (distanceToPlayer < m_resumeDistance) {
      ctx.transform.velocity = Vector2D(0, 0);
      ctx.transform.acceleration = Vector2D(0, 0); // Clear acceleration too
      pathData.progressTimer = 0.0f;
      return;
    }
    // Resuming - clear the stopped flag and any old path data
    state.isStopped = false;
    pathData.clear();
  } else {
    // Moving - check if we should stop based on distance to PLAYER
    if (distanceToPlayer < m_stopDistance) {
      ctx.transform.velocity = Vector2D(0, 0);
      ctx.transform.acceleration = Vector2D(0, 0); // Clear acceleration too
      pathData.progressTimer = 0.0f;
      state.isStopped = true;
      // Clear path to prevent any residual path-following
      pathData.clear();
      return;
    }
  }

  // Use distance to player for catch-up speed calculation
  float dynamicSpeed = calculateFollowSpeed(distanceToPlayer);

  // Execute appropriate follow behavior based on mode (use pre-calculated desiredPos)
  // Track whether we're using pathfinding or direct movement
  bool usingPathfinding = false;

  // Try pathfinding first for all modes
  usingPathfinding = tryFollowPathToGoal(ctx, state, desiredPos, dynamicSpeed);

  // If pathfinding isn't active, fall back to direct movement toward desired position
  if (!usingPathfinding) {
    // Simple direct movement - just move toward desiredPos
    Vector2D direction = desiredPos - currentPos;
    float const length = direction.length();

    if (length > 0.1f) {
      direction = direction * (1.0f / length);  // Normalize
      ctx.transform.velocity = direction * dynamicSpeed;
    }
  }

  // CollisionManager handles overlap resolution
  (void)usingPathfinding;  // No longer used
}

void FollowBehavior::clean(EntityHandle handle) {
  if (handle.isValid()) {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX && idx < m_entityStatesByIndex.size() && m_entityStatesByIndex[idx].valid) {
      // Release formation slot if using escort formation
      if (m_followMode == FollowMode::ESCORT_FORMATION) {
        releaseFormationSlot(m_entityStatesByIndex[idx].formationSlot);
      }
      m_entityStatesByIndex[idx].valid = false;
    }
  } else {
    // Clear all states - reset valid flags
    for (auto& state : m_entityStatesByIndex) {
      if (state.valid && m_followMode == FollowMode::ESCORT_FORMATION) {
        releaseFormationSlot(state.formationSlot);
      }
      state.valid = false;
    }
  }
}

void FollowBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX || idx >= m_entityStatesByIndex.size() || !m_entityStatesByIndex[idx].valid)
    return;

  EntityState &state = m_entityStatesByIndex[idx];

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
  for (auto &state : m_entityStatesByIndex) {
    if (!state.valid) continue;
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
    for (auto &state : m_entityStatesByIndex) {
      if (state.valid) {
        state.formationOffset = offset;
      }
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
  return std::any_of(m_entityStatesByIndex.begin(), m_entityStatesByIndex.end(),
                     [](const auto &state) { return state.valid && state.isFollowing; });
}

bool FollowBehavior::isInFormation() const {
  if (m_followMode != FollowMode::ESCORT_FORMATION)
    return true;
  return std::all_of(m_entityStatesByIndex.begin(), m_entityStatesByIndex.end(),
                     [](const auto &state) { return !state.valid || state.inFormation; });
}

float FollowBehavior::getDistanceToTarget() const {
  // Phase 2 EDM Migration: Use handle-based target lookup
  auto& edm = EntityDataManager::Instance();
  EntityHandle targetHandle = getTargetHandle();
  if (!targetHandle.isValid())
    return -1.0f;

  size_t targetIdx = edm.getIndex(targetHandle);
  if (targetIdx == SIZE_MAX)
    return -1.0f;

  const auto& targetHotData = edm.getHotDataByIndex(targetIdx);
  Vector2D targetPos = targetHotData.transform.position;

  // Find a following entity to calculate distance
  auto it =
      std::find_if(m_entityStatesByIndex.begin(), m_entityStatesByIndex.end(),
                   [](const auto &state) { return state.valid && state.isFollowing; });
  if (it != m_entityStatesByIndex.end()) {
    Vector2D entityDesiredPos = it->desiredPosition;
    float distSquared = (entityDesiredPos - targetPos).lengthSquared();
    return std::sqrt(distSquared);
  }
  return -1.0f;
}

FollowBehavior::FollowMode FollowBehavior::getFollowMode() const {
  return m_followMode;
}

Vector2D FollowBehavior::getTargetPosition() const {
  // Phase 2 EDM Migration: Use handle-based target lookup
  auto& edm = EntityDataManager::Instance();
  EntityHandle targetHandle = getTargetHandle();
  if (!targetHandle.isValid())
    return Vector2D(0, 0);

  size_t targetIdx = edm.getIndex(targetHandle);
  if (targetIdx == SIZE_MAX)
    return Vector2D(0, 0);

  return edm.getHotDataByIndex(targetIdx).transform.position;
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

EntityHandle FollowBehavior::getTargetHandle() const {
  return AIManager::Instance().getPlayerHandle();
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
      float const distance = 40.0f + (state.formationSlot * 15.0f); // 40px, 55px, 70px
      return Vector2D(std::cos(angle) * distance, std::sin(angle) * distance);
    }
    // Next 3 NPCs: medium formation ring
    else if (state.formationSlot < 6) {
      float angle = ((state.formationSlot - 3) * 2.0f * M_PI / 3.0f) + (M_PI / 6.0f);
      float const distance = 85.0f + ((state.formationSlot - 3) * 15.0f); // 85px, 100px, 115px
      return Vector2D(std::cos(angle) * distance, std::sin(angle) * distance);
    }
    // Additional NPCs: wide spread using golden angle for even distribution
    else {
      float angle = (state.formationSlot * 0.618f * 2.0f * M_PI); // Golden angle
      float const distance = 130.0f + ((state.formationSlot % 4) * 20.0f); // 130-190px range
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

bool FollowBehavior::shouldCatchUp(float distanceToTarget) const {
  return distanceToTarget > m_maxDistance;
}

float FollowBehavior::calculateFollowSpeed(float distanceToTarget) const {
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

Vector2D FollowBehavior::smoothPath(const Vector2D &currentPos,
                                    const Vector2D &targetPos,
                                    const EntityState &state) const {
  if (!m_pathSmoothing) {
    return targetPos - currentPos;
  }

  // Simple path smoothing - blend current direction with desired direction
  Vector2D desiredDirection = normalizeVector(targetPos - currentPos);
  Vector2D const currentDirection = normalizeVector(state.currentVelocity);

  if (currentDirection.length() < 0.001f) {
    return desiredDirection;
  }

  // Blend directions for smoother turning
  Vector2D const blendedDirection =
      (currentDirection * 0.7f + desiredDirection * 0.3f);
  return normalizeVector(blendedDirection);
}

Vector2D FollowBehavior::normalizeVector(const Vector2D &vector) const {
  float const magnitude = vector.length();
  if (magnitude < 0.001f) {
    return Vector2D(0, 0);
  }
  return vector / magnitude;
}


void FollowBehavior::initializeFormationOffsets() {
  std::call_once(s_formationInitFlag, []() {
    s_escortFormationOffsets.reserve(8);
    for (int i = 0; i < 8; ++i) {
      float angle = (i * 2.0f * M_PI) / 8.0f;
      s_escortFormationOffsets.emplace_back(std::cos(angle), std::sin(angle));
    }
  });
}

int FollowBehavior::assignFormationSlot() {
  // Thread-safe atomic increment with wrap-around (8 slots)
  int slot = s_nextFormationSlot.fetch_add(1, std::memory_order_relaxed) % 8;
  return slot;
}

void FollowBehavior::releaseFormationSlot(int /*slot*/) {
  // In a more sophisticated implementation, you might track which slots are in
  // use For now, we just cycle through slots
}

// OPTIMIZATION: Extracted from lambda for better compiler optimization
// This method handles path-following logic with TTL, goal change detection, and obstacle handling
// LOCK-FREE VERSION: Uses BehaviorContext instead of EntityPtr
bool FollowBehavior::tryFollowPathToGoal(BehaviorContext& ctx, EntityState& state, const Vector2D& desiredPos, float speed) {
  const float nodeRadius = 20.0f; // Increased for faster path following
  const float pathTTL = 10.0f; // 10 seconds - reduce path churn when stationary
  const float GOAL_CHANGE_THRESH_SQUARED = 200.0f * 200.0f; // Require 200px goal change to recalculate

  Vector2D currentPos = ctx.transform.position;

  // Read path state from EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  auto& pathData = edm.getPathData(ctx.edmIndex);

  // Check if path is stale
  bool const stale = pathData.pathUpdateTimer > pathTTL;

  // Check if goal changed significantly
  bool goalChanged = true;
  if (pathData.hasPath && !pathData.navPath.empty()) {
    Vector2D lastGoal = pathData.navPath.back();
    // Use squared distance for performance
    goalChanged = ((desiredPos - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
  }

  // OBSTACLE DETECTION: Force path refresh if stuck on obstacle (800ms = 0.8s)
  bool const stuckOnObstacle = (pathData.progressTimer > 0.8f);

  // Request new path if needed - result written directly to EDM
  if (stale || goalChanged || stuckOnObstacle) {
    pathfinder().requestPathToEDM(ctx.edmIndex, currentPos, desiredPos,
                                  PathfinderManager::Priority::Normal);
  }

  // Try to follow the path from EDM
  bool pathStep = false;
  if (pathData.isFollowingPath()) {
    Vector2D waypoint = pathData.getCurrentWaypoint();
    Vector2D toWaypoint = waypoint - currentPos;
    float dist = toWaypoint.length();

    if (dist < nodeRadius) {
      pathData.advanceWaypoint();
      if (pathData.isFollowingPath()) {
        waypoint = pathData.getCurrentWaypoint();
        toWaypoint = waypoint - currentPos;
        dist = toWaypoint.length();
      }
    }

    if (pathData.isFollowingPath() && dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * speed;
      pathData.progressTimer = 0.0f;
      pathStep = true;
    }
  }

  return pathStep;
}
