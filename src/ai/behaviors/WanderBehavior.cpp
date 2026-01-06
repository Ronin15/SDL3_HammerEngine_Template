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

// Static world bounds cache (shared by all wander entities)
WanderBehavior::WorldBoundsCache WanderBehavior::s_worldBounds{};

std::mt19937 &WanderBehavior::getSharedRNG() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return rng;
}

WanderBehavior::WanderBehavior(const HammerEngine::WanderBehaviorConfig& config)
    : m_config(config), m_speed(config.speed),
      m_changeDirectionInterval(config.changeDirectionIntervalMin),
      m_areaRadius(300.0f) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
}

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval,
                               float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval),
      m_areaRadius(areaRadius) {
  m_config.speed = speed;
  m_config.changeDirectionIntervalMin = changeDirectionInterval;
  m_config.changeDirectionIntervalMax = changeDirectionInterval + 5000.0f;
  // Entity state now stored in EDM BehaviorData - no local allocation needed
}

WanderBehavior::WanderMode
getDefaultModeForFrequency(WanderBehavior::WanderMode mode) {
  return mode;
}

WanderBehavior::WanderBehavior(WanderMode mode, float speed) : m_speed(speed) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
  setupModeDefaults(mode);
}

void WanderBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  // Get EDM index for centralized storage
  auto& edm = EntityDataManager::Instance();
  size_t edmIndex = edm.getIndex(handle);
  if (edmIndex == SIZE_MAX)
    return;

  // Initialize behavior data in EDM (pre-allocated alongside hotData)
  edm.initBehaviorData(edmIndex, BehaviorType::Wander);
  auto& data = edm.getBehaviorData(edmIndex);
  auto& wander = data.state.wander;

  // Initialize wander-specific state
  wander.directionChangeTimer = 0.0f;
  wander.lastDirectionFlip = 0.0f;
  wander.startDelay = s_delayDistribution(getSharedRNG()) / 1000.0f;
  wander.movementStarted = false;
  wander.stallTimer = 0.0f;
  wander.lastStallPosition = Vector2D{0, 0};
  wander.stallPositionVariance = 0.0f;
  wander.unstickTimer = 0.0f;

  // Initialize direction
  float angle = s_angleDistribution(getSharedRNG());
  wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
  wander.previousVelocity = Vector2D{0, 0};

  data.setInitialized(true);
}

// LOCK-FREE HOT PATH: Uses BehaviorContext for direct EDM access
void WanderBehavior::executeLogic(BehaviorContext& ctx) {
  if (!m_active || !ctx.behaviorData)
    return;

  // Use pre-fetched behavior data from context (no Instance() call needed)
  auto& data = *ctx.behaviorData;
  if (!data.isValid()) {
    return;
  }

  // Update all timers (including EDM path timers) - pass pathData directly
  updateTimers(data, ctx.deltaTime, ctx.pathData);

  // Check if we need to start movement after delay
  if (!handleStartDelay(ctx, data)) {
    return;
  }

  // Handle movement logic
  if (data.state.wander.movementStarted) {
    handleMovement(ctx, data);
  }
}

void WanderBehavior::updateTimers(BehaviorData& data, float deltaTime, PathData* pathData) {
  auto& wander = data.state.wander;
  wander.directionChangeTimer += deltaTime;
  wander.lastDirectionFlip += deltaTime;
  wander.stallTimer += deltaTime;
  wander.unstickTimer += deltaTime;

  // Update path timers in EDM (single source of truth) - no Instance() call needed
  if (pathData) {
    pathData->pathUpdateTimer += deltaTime;
    pathData->progressTimer += deltaTime;
    if (pathData->pathRequestCooldown > 0.0f) {
      pathData->pathRequestCooldown -= deltaTime;
    }
  }
}

bool WanderBehavior::handleStartDelay(BehaviorContext& ctx, BehaviorData& data) {
  auto& wander = data.state.wander;
  if (wander.movementStarted) {
    return true;
  }

  if (wander.directionChangeTimer < wander.startDelay) {
    return false;
  }

  // Time to start moving
  wander.movementStarted = true;
  Vector2D const intended = wander.currentDirection * m_speed;

  // Set velocity directly - CollisionManager handles overlap resolution
  ctx.transform.velocity = intended;

  return true;  // Movement started - continue to handleMovement()
}

float WanderBehavior::calculateMoveDistance(const BehaviorData& data,
                                           const Vector2D& position, float baseDistance) {
  int nearbyCount = data.cachedNearbyCount;
  auto& wander = const_cast<BehaviorData&>(data).state.wander;

  // Dynamic distance adjustment based on crowding
  float moveDistance = baseDistance;

  if (nearbyCount > m_config.crowdEscapeThreshold) {
    moveDistance = baseDistance * m_config.crowdEscapeDistanceMultiplier;

    if (nearbyCount > 0) {
      Vector2D escapeDirection = (position - data.cachedClusterCenter).normalized();
      float randomOffset = (nearbyCount % 60 - 30) * 0.01f;
      escapeDirection.setX(escapeDirection.getX() + randomOffset);
      escapeDirection.setY(escapeDirection.getY() + randomOffset);
      escapeDirection.normalize();
      wander.currentDirection = escapeDirection;
    }
  } else if (nearbyCount > 5) {
    moveDistance = baseDistance * 2.0f;
    wander.currentDirection = wander.currentDirection.normalized();
  } else if (nearbyCount > 2) {
    moveDistance = baseDistance * 1.3f;
  }

  return moveDistance;
}

void WanderBehavior::applyBoundaryAvoidance(BehaviorData& data, const Vector2D& position) {
  // Use static world bounds cache (same for all entities)
  if (!s_worldBounds.initialized) {
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
      s_worldBounds.minX = minX;
      s_worldBounds.minY = minY;
      s_worldBounds.maxX = maxX;
      s_worldBounds.maxY = maxY;
      s_worldBounds.initialized = true;
    } else {
      return;
    }
  }

  auto& wander = data.state.wander;
  const float EDGE_THRESHOLD = m_config.edgeThreshold;
  Vector2D boundaryForce(0, 0);

  if (position.getX() < s_worldBounds.minX + EDGE_THRESHOLD) {
    float const strength = 1.0f - ((position.getX() - s_worldBounds.minX) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(strength, 0);
  } else if (position.getX() > s_worldBounds.maxX - EDGE_THRESHOLD) {
    float const strength = 1.0f - ((s_worldBounds.maxX - position.getX()) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(-strength, 0);
  }

  if (position.getY() < s_worldBounds.minY + EDGE_THRESHOLD) {
    float const strength = 1.0f - ((position.getY() - s_worldBounds.minY) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(0, strength);
  } else if (position.getY() > s_worldBounds.maxY - EDGE_THRESHOLD) {
    float const strength = 1.0f - ((s_worldBounds.maxY - position.getY()) / EDGE_THRESHOLD);
    boundaryForce = boundaryForce + Vector2D(0, -strength);
  }

  if (boundaryForce.lengthSquared() > 0.01f) {
    wander.currentDirection = (wander.currentDirection * 0.4f + boundaryForce.normalized() * 0.6f).normalized();
  }
}

void WanderBehavior::handlePathfinding(const BehaviorContext& ctx, const Vector2D& dest) {
  Vector2D position = ctx.transform.position;
  float const distanceToGoal = (dest - position).length();
  if (distanceToGoal < 64.0f || !ctx.pathData) {
    return;
  }

  // Use pre-fetched path data from context (no Instance() call needed)
  auto& pathData = *ctx.pathData;

  const bool skipRefresh = (pathData.pathRequestCooldown > 0.0f &&
                            pathData.isFollowingPath() &&
                            pathData.progressTimer < 0.8f);
  bool needsNewPath = false;
  if (!skipRefresh) {
    needsNewPath = !pathData.hasPath ||
                   pathData.navIndex >= pathData.pathLength ||
                   pathData.pathUpdateTimer > 25.0f;
  }

  bool stuckOnObstacle = pathData.progressTimer > 0.8f;
  if (stuckOnObstacle) {
    pathData.clear();
  }

  if ((needsNewPath || stuckOnObstacle) && pathData.pathRequestCooldown <= 0.0f) {
    const float MIN_GOAL_CHANGE = m_config.minGoalChangeDistance;
    bool goalChanged = true;
    auto& edm = EntityDataManager::Instance();
    if (!skipRefresh && pathData.hasPath && pathData.pathLength > 0) {
      Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
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

void WanderBehavior::handleMovement(BehaviorContext& ctx, BehaviorData& data) {
  auto& wander = data.state.wander;
  float baseDistance = std::min(600.0f, m_areaRadius * 1.5f);
  Vector2D position = ctx.transform.position;

  float moveDistance = calculateMoveDistance(data, position, baseDistance);
  applyBoundaryAvoidance(data, position);

  Vector2D dest = position + wander.currentDirection * moveDistance;

  // Clamp destination to world bounds (use static cache)
  if (s_worldBounds.initialized) {
    const float MARGIN = m_config.worldPaddingMargin;
    dest.setX(std::clamp(dest.getX(), s_worldBounds.minX + MARGIN, s_worldBounds.maxX - MARGIN));
    dest.setY(std::clamp(dest.getY(), s_worldBounds.minY + MARGIN, s_worldBounds.maxY - MARGIN));
  }

  handlePathfinding(ctx, dest);

  // Use pre-fetched path data from context (no Instance() call needed)
  if (!ctx.pathData) {
    ctx.transform.velocity = wander.currentDirection * m_speed;
    return;
  }
  auto& pathData = *ctx.pathData;

  // Follow path or apply base movement - write directly to transform
  if (pathData.isFollowingPath()) {
    Vector2D waypoint = ctx.pathData->currentWaypoint;
    Vector2D toWaypoint = waypoint - position;
    float dist = toWaypoint.length();

    if (dist < 64.0f) {  // navRadius
      auto& edm = EntityDataManager::Instance();
      edm.advanceWaypointWithCache(ctx.edmIndex);
      if (pathData.isFollowingPath()) {
        waypoint = ctx.pathData->currentWaypoint;
        toWaypoint = waypoint - position;
        dist = toWaypoint.length();
      }
    }

    if (dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * m_speed;
    }
  } else {
    ctx.transform.velocity = wander.currentDirection * m_speed;
  }

  // Stall detection
  float speed = ctx.transform.velocity.length();
  const float stallSpeed = std::max(m_config.stallSpeed, m_speed * 0.5f);
  const float stallSeconds = m_config.stallTimeout;

  if (speed < stallSpeed) {
    if (wander.stallTimer >= stallSeconds) {
      pathData.clear();
      chooseNewDirection(ctx, data);
      pathData.pathRequestCooldown = 0.6f;
      wander.stallTimer = 0.0f;
      return;
    }
  } else {
    wander.stallTimer = 0.0f;
  }

  // Check if it's time to change direction
  float const changeIntervalSeconds = m_changeDirectionInterval / 1000.0f;
  if (wander.directionChangeTimer >= changeIntervalSeconds) {
    chooseNewDirection(ctx, data);
    wander.directionChangeTimer = 0.0f;
  }

  // Micro-jitter to break small jams
  if (speed < (m_speed * 1.5f) && speed >= stallSpeed) {
    float jitter = (s_angleDistribution(getSharedRNG()) - static_cast<float>(M_PI)) * 0.1f;
    Vector2D dir = wander.currentDirection;
    float c = std::cos(jitter), s = std::sin(jitter);
    Vector2D rotated(dir.getX() * c - dir.getY() * s, dir.getX() * s + dir.getY() * c);
    if (rotated.length() > 0.001f) {
      rotated.normalize();
      wander.currentDirection = rotated;
      ctx.transform.velocity = wander.currentDirection * m_speed;
    }
  }

  wander.previousVelocity = ctx.transform.velocity;
}

void WanderBehavior::clean(EntityHandle handle) {
  auto& edm = EntityDataManager::Instance();
  if (handle.isValid()) {
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      edm.clearBehaviorData(idx);
      edm.clearPathData(idx);
    }
  }
  // Note: Bulk cleanup handled by EDM::prepareForStateTransition()
}

void WanderBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto& data = edm.getBehaviorData(idx);
  bool hasValidData = data.isValid();

  if (message == "pause") {
    setActive(false);
    edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
  } else if (message == "resume") {
    setActive(true);
    if (hasValidData) {
      float angle = s_angleDistribution(getSharedRNG());
      data.state.wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    }
  } else if (message == "new_direction") {
    if (hasValidData) {
      float angle = s_angleDistribution(getSharedRNG());
      data.state.wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    }
  } else if (message == "increase_speed") {
    m_speed *= 1.5f;
    if (m_active && hasValidData) {
      edm.getHotDataByIndex(idx).transform.velocity = data.state.wander.currentDirection * m_speed;
    }
  } else if (message == "decrease_speed") {
    m_speed *= 0.75f;
    if (m_active && hasValidData) {
      edm.getHotDataByIndex(idx).transform.velocity = data.state.wander.currentDirection * m_speed;
    }
  } else if (message == "release_entities") {
    edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    edm.clearBehaviorData(idx);
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

void WanderBehavior::chooseNewDirection(BehaviorContext& ctx, BehaviorData& data) {
  auto& wander = data.state.wander;
  float angle = s_angleDistribution(getSharedRNG());
  wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));

  if (wander.movementStarted) {
    ctx.transform.velocity = wander.currentDirection * m_speed;
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
