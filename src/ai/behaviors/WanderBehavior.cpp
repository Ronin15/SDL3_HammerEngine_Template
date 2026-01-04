/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "ai/internal/SpatialPriority.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

// Static thread-local RNG pool for memory optimization
thread_local std::uniform_real_distribution<float>
    WanderBehavior::s_angleDistribution{0.0f, 2.0f * M_PI};
thread_local std::uniform_int_distribution<Uint64>
    WanderBehavior::s_delayDistribution{0, 5000};

std::mt19937 &WanderBehavior::getSharedRNG() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return rng;
}

WanderBehavior::WanderBehavior(const HammerEngine::WanderBehaviorConfig& config)
    : m_config(config), m_speed(config.speed),
      m_changeDirectionInterval(config.changeDirectionIntervalMin),
      m_areaRadius(300.0f) {
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
}

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval,
                               float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval),
      m_areaRadius(areaRadius) {
  m_config.speed = speed;
  m_config.changeDirectionIntervalMin = changeDirectionInterval;
  m_config.changeDirectionIntervalMax = changeDirectionInterval + 5000.0f;
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
}

WanderBehavior::WanderMode
getDefaultModeForFrequency(WanderBehavior::WanderMode mode) {
  return mode;
}

WanderBehavior::WanderBehavior(WanderMode mode, float speed) : m_speed(speed) {
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
  setupModeDefaults(mode);
}

void WanderBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  // Get EDM index for vector-based storage (contention-free)
  auto& edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex == SIZE_MAX)
    return;

  // Ensure vector is large enough for this index
  if (edmIndex >= m_entityStatesByIndex.size()) {
    m_entityStatesByIndex.resize(edmIndex + 1);
  }

  EntityState &state = m_entityStatesByIndex[edmIndex];
  state.valid = true;
  state.directionChangeTimer = 0.0f;
  state.startDelay = s_delayDistribution(getSharedRNG()) / 1000.0f;
  state.movementStarted = false;

  state.baseState.cachedNearbyPositions.reserve(50);

  // Initialize direction
  float angle = s_angleDistribution(getSharedRNG());
  state.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
}

// LOCK-FREE HOT PATH: Uses BehaviorContext for direct EDM access
void WanderBehavior::executeLogic(BehaviorContext& ctx) {
  if (!m_active)
    return;

  // Get state by EDM index (contention-free vector access)
  if (ctx.edmIndex >= m_entityStatesByIndex.size()) {
    return;
  }
  EntityState &state = m_entityStatesByIndex[ctx.edmIndex];
  if (!state.valid) {
    return;
  }

  // Update all timers (including EDM path timers)
  updateTimers(state, ctx.deltaTime, ctx.edmIndex);

  // Check if we need to start movement after delay
  if (!handleStartDelay(ctx, state)) {
    return;
  }

  // Handle movement logic
  if (state.movementStarted) {
    handleMovement(ctx, state);
  }
}

void WanderBehavior::updateTimers(EntityState& state, float deltaTime, size_t edmIndex) {
  state.directionChangeTimer += deltaTime;
  state.lastDirectionFlip += deltaTime;
  state.stallTimer += deltaTime;
  state.unstickTimer += deltaTime;

  // Update path timers in EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  auto& pathData = edm.getPathData(edmIndex);
  pathData.pathUpdateTimer += deltaTime;
  pathData.progressTimer += deltaTime;
  if (pathData.pathRequestCooldown > 0.0f) {
    pathData.pathRequestCooldown -= deltaTime;
  }
}

bool WanderBehavior::handleStartDelay(BehaviorContext& ctx, EntityState& state) {
  if (state.movementStarted) {
    return true;
  }

  if (state.directionChangeTimer < state.startDelay) {
    return false;
  }

  // Time to start moving
  state.movementStarted = true;
  Vector2D const intended = state.currentDirection * m_speed;

  // Set velocity directly - CollisionManager handles overlap resolution
  ctx.transform.velocity = intended;

  return false;
}

float WanderBehavior::calculateMoveDistance(EntityState& state,
                                           const Vector2D& position, float baseDistance) {
  int nearbyCount = state.baseState.cachedNearbyCount;

  // Dynamic distance adjustment based on crowding
  float moveDistance = baseDistance;

  if (nearbyCount > m_config.crowdEscapeThreshold) {
    moveDistance = baseDistance * m_config.crowdEscapeDistanceMultiplier;

    if (!state.baseState.cachedNearbyPositions.empty()) {
      Vector2D escapeDirection = (position - state.baseState.cachedClusterCenter).normalized();
      float randomOffset = (state.baseState.cachedNearbyCount % 60 - 30) * 0.01f;
      escapeDirection.setX(escapeDirection.getX() + randomOffset);
      escapeDirection.setY(escapeDirection.getY() + randomOffset);
      escapeDirection.normalize();
      state.currentDirection = escapeDirection;
    }
  } else if (nearbyCount > 5) {
    moveDistance = baseDistance * 2.0f;
    state.currentDirection = state.currentDirection.normalized();
  } else if (nearbyCount > 2) {
    moveDistance = baseDistance * 1.3f;
  }

  return moveDistance;
}

void WanderBehavior::applyBoundaryAvoidance(EntityState& state, const Vector2D& position) {
  if (state.cachedBounds.maxX == 0.0f) {
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
      state.cachedBounds.minX = minX;
      state.cachedBounds.minY = minY;
      state.cachedBounds.maxX = maxX;
      state.cachedBounds.maxY = maxY;
    } else {
      return;
    }
  }

  const float EDGE_THRESHOLD = m_config.edgeThreshold;
  Vector2D boundaryForce(0, 0);

  if (position.getX() < state.cachedBounds.minX + EDGE_THRESHOLD) {
    float const strength = 1.0f - ((position.getX() - state.cachedBounds.minX) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(strength, 0);
  } else if (position.getX() > state.cachedBounds.maxX - EDGE_THRESHOLD) {
    float const strength = 1.0f - ((state.cachedBounds.maxX - position.getX()) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(-strength, 0);
  }

  if (position.getY() < state.cachedBounds.minY + EDGE_THRESHOLD) {
    float const strength = 1.0f - ((position.getY() - state.cachedBounds.minY) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(0, strength);
  } else if (position.getY() > state.cachedBounds.maxY - EDGE_THRESHOLD) {
    float const strength = 1.0f - ((state.cachedBounds.maxY - position.getY()) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(0, -strength);
  }

  if (boundaryForce.lengthSquared() > 0.01f) {
    state.currentDirection = (state.currentDirection * 0.4f + boundaryForce.normalized() * 0.6f).normalized();
  }
}

void WanderBehavior::handlePathfinding(const BehaviorContext& ctx, EntityState& state, const Vector2D& dest) {
  Vector2D position = ctx.transform.position;
  float const distanceToGoal = (dest - position).length();
  if (distanceToGoal < 64.0f) {
    return;
  }

  // Read path state from EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  auto& pathData = edm.getPathData(ctx.edmIndex);

  bool needsNewPath = !pathData.hasPath ||
                     pathData.navIndex >= pathData.navPath.size() ||
                     pathData.pathUpdateTimer > 15.0f;

  bool stuckOnObstacle = pathData.progressTimer > 0.8f;
  if (stuckOnObstacle) {
    pathData.clear();
  }

  if ((needsNewPath || stuckOnObstacle) && pathData.pathRequestCooldown <= 0.0f) {
    const float MIN_GOAL_CHANGE = m_config.minGoalChangeDistance;
    bool goalChanged = true;
    if (pathData.hasPath && !pathData.navPath.empty()) {
      Vector2D lastGoal = pathData.navPath.back();
      float const goalDistanceSquared = (dest - lastGoal).lengthSquared();
      goalChanged = (goalDistanceSquared >= MIN_GOAL_CHANGE * MIN_GOAL_CHANGE);
    }

    if (goalChanged) {
      // Use EDM-integrated async pathfinding - no callback needed
      // Result written directly to EDM::getPathData(edmIndex)
      pathfinder().requestPathToEDM(ctx.edmIndex, position, dest,
                                    PathfinderManager::Priority::Normal);
      pathData.pathRequestCooldown = m_config.pathRequestCooldown;
    }
  }
}

void WanderBehavior::handleMovement(BehaviorContext& ctx, EntityState& state) {
  float baseDistance = std::min(600.0f, m_areaRadius * 1.5f);
  Vector2D position = ctx.transform.position;

  float moveDistance = calculateMoveDistance(state, position, baseDistance);
  applyBoundaryAvoidance(state, position);

  Vector2D dest = position + state.currentDirection * moveDistance;

  const float MARGIN = m_config.worldPaddingMargin;
  dest.setX(std::clamp(dest.getX(), state.cachedBounds.minX + MARGIN, state.cachedBounds.maxX - MARGIN));
  dest.setY(std::clamp(dest.getY(), state.cachedBounds.minY + MARGIN, state.cachedBounds.maxY - MARGIN));

  handlePathfinding(ctx, state, dest);

  // Read path state from EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  auto& pathData = edm.getPathData(ctx.edmIndex);

  // Follow path or apply base movement - write directly to transform
  if (pathData.isFollowingPath()) {
    Vector2D waypoint = pathData.getCurrentWaypoint();
    Vector2D toWaypoint = waypoint - position;
    float dist = toWaypoint.length();

    if (dist < 64.0f) {  // navRadius
      pathData.advanceWaypoint();
      if (pathData.isFollowingPath()) {
        waypoint = pathData.getCurrentWaypoint();
        toWaypoint = waypoint - position;
        dist = toWaypoint.length();
      }
    }

    if (dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * m_speed;
    }
  } else {
    ctx.transform.velocity = state.currentDirection * m_speed;
  }

  // Stall detection
  float speed = ctx.transform.velocity.length();
  const float stallSpeed = std::max(m_config.stallSpeed, m_speed * 0.5f);
  const float stallSeconds = m_config.stallTimeout;

  if (speed < stallSpeed) {
    if (state.stallTimer >= stallSeconds) {
      pathData.clear();
      chooseNewDirection(ctx, state);
      pathData.pathRequestCooldown = 0.6f;
      state.stallTimer = 0.0f;
      return;
    }
  } else {
    state.stallTimer = 0.0f;
  }

  // Check if it's time to change direction
  float const changeIntervalSeconds = m_changeDirectionInterval / 1000.0f;
  if (state.directionChangeTimer >= changeIntervalSeconds) {
    chooseNewDirection(ctx, state);
    state.directionChangeTimer = 0.0f;
  }

  // Micro-jitter to break small jams
  if (speed < (m_speed * 1.5f) && speed >= stallSpeed) {
    float jitter = (s_angleDistribution(getSharedRNG()) - static_cast<float>(M_PI)) * 0.1f;
    Vector2D dir = state.currentDirection;
    float c = std::cos(jitter), s = std::sin(jitter);
    Vector2D rotated(dir.getX() * c - dir.getY() * s, dir.getX() * s + dir.getY() * c);
    if (rotated.length() > 0.001f) {
      rotated.normalize();
      state.currentDirection = rotated;
      ctx.transform.velocity = state.currentDirection * m_speed;
    }
  }

  state.previousVelocity = ctx.transform.velocity;
}

void WanderBehavior::clean(EntityHandle handle) {
  if (handle.isValid()) {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      // Mark state as invalid (contention-free)
      if (idx < m_entityStatesByIndex.size()) {
        m_entityStatesByIndex[idx].valid = false;
      }
    }
  } else {
    // Clear all states - reset valid flags
    for (auto& state : m_entityStatesByIndex) {
      state.valid = false;
    }
  }
}

void WanderBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  // Get state by EDM index (contention-free)
  EntityState* statePtr = nullptr;
  if (idx < m_entityStatesByIndex.size() && m_entityStatesByIndex[idx].valid) {
    statePtr = &m_entityStatesByIndex[idx];
  }

  if (message == "pause") {
    setActive(false);
    edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
  } else if (message == "resume") {
    setActive(true);
    if (statePtr) {
      float angle = s_angleDistribution(getSharedRNG());
      statePtr->currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    }
  } else if (message == "new_direction") {
    if (statePtr) {
      float angle = s_angleDistribution(getSharedRNG());
      statePtr->currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    }
  } else if (message == "increase_speed") {
    m_speed *= 1.5f;
    if (m_active && statePtr) {
      edm.getHotDataByIndex(idx).transform.velocity = statePtr->currentDirection * m_speed;
    }
  } else if (message == "decrease_speed") {
    m_speed *= 0.75f;
    if (m_active && statePtr) {
      edm.getHotDataByIndex(idx).transform.velocity = statePtr->currentDirection * m_speed;
    }
  } else if (message == "release_entities") {
    edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    if (statePtr) {
      statePtr->valid = false;
    }
  }
}

std::string WanderBehavior::getName() const { return "Wander"; }

std::shared_ptr<AIBehavior> WanderBehavior::clone() const {
  auto cloned = std::make_shared<WanderBehavior>(
      m_speed, m_changeDirectionInterval, m_areaRadius);
  cloned->setCenterPoint(m_centerPoint);
  cloned->setActive(m_active);
  return cloned;
}

void WanderBehavior::setCenterPoint(const Vector2D &centerPoint) {
  m_centerPoint = centerPoint;
}

void WanderBehavior::setAreaRadius(float radius) { m_areaRadius = radius; }

void WanderBehavior::setSpeed(float speed) { m_speed = speed; }

void WanderBehavior::setChangeDirectionInterval(float interval) {
  m_changeDirectionInterval = interval;
}

void WanderBehavior::chooseNewDirection(BehaviorContext& ctx, EntityState& state) {
  float angle = s_angleDistribution(getSharedRNG());
  state.currentDirection = Vector2D(std::cos(angle), std::sin(angle));

  if (state.movementStarted) {
    ctx.transform.velocity = state.currentDirection * m_speed;
  }
}

void WanderBehavior::setupModeDefaults(WanderMode mode) {
  float minX, minY, maxX, maxY;
  if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    float const worldWidth = (maxX - minX);
    float const worldHeight = (maxY - minY);
    m_centerPoint = Vector2D(worldWidth * 0.5f, worldHeight * 0.5f);
  } else {
    m_centerPoint = Vector2D(1000.0f, 1000.0f);
  }

  switch (mode) {
  case WanderMode::SMALL_AREA:
    m_areaRadius = 400.0f;
    m_changeDirectionInterval = 1500.0f;
    break;
  case WanderMode::MEDIUM_AREA:
    m_areaRadius = 1200.0f;
    m_changeDirectionInterval = 2500.0f;
    break;
  case WanderMode::LARGE_AREA:
    m_areaRadius = 2400.0f;
    m_changeDirectionInterval = 3500.0f;
    break;
  case WanderMode::EVENT_TARGET:
    m_areaRadius = 2500.0f;
    m_changeDirectionInterval = 2000.0f;
    break;
  }
}
