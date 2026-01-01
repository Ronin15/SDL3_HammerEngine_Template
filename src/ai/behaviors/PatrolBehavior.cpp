/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp" // For TransformData definition
#include "ai/internal/SpatialPriority.hpp"
#include "core/Logger.hpp"
#include "entities/Entity.hpp"
#include "entities/NPC.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shared RNG pool for performance optimization
namespace {
std::mt19937 &getSharedRNG() {
  static thread_local std::mt19937 rng(
      std::chrono::steady_clock::now().time_since_epoch().count());
  return rng;
}
} // namespace

PatrolBehavior::PatrolBehavior(const HammerEngine::PatrolBehaviorConfig& config)
    : m_config(config), m_waypoints(), m_currentWaypoint(0), m_moveSpeed(config.moveSpeed),
      m_waypointRadius(config.waypointReachedRadius),
      m_includeOffscreenPoints(false), m_needsReset(false),
      m_patrolMode(PatrolMode::FIXED_WAYPOINTS), m_rng(getSharedRNG()),
      m_seedSet(true) {
  m_waypoints.reserve(10);
}

PatrolBehavior::PatrolBehavior(const std::vector<Vector2D> &waypoints,
                               float moveSpeed, bool includeOffscreenPoints)
    : m_waypoints(waypoints), m_currentWaypoint(0), m_moveSpeed(moveSpeed),
      m_waypointRadius(80.0f), // 2.5 tiles - natural waypoint arrival distance
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_patrolMode(PatrolMode::FIXED_WAYPOINTS), m_rng(getSharedRNG()),
      m_seedSet(true) {
  // Update config to match legacy parameters
  m_config.moveSpeed = moveSpeed;
  m_config.waypointReachedRadius = 80.0f;

  m_waypoints.reserve(10);

  if (m_waypoints.size() < 2) {
    m_waypoints.push_back(Vector2D(100, 100));
    m_waypoints.push_back(Vector2D(200, 200));
  }
}

PatrolBehavior::PatrolBehavior(PatrolMode mode, float moveSpeed,
                               bool includeOffscreenPoints)
    : m_waypoints(), m_currentWaypoint(0), m_moveSpeed(moveSpeed),
      m_waypointRadius(80.0f), // 2.5 tiles - natural waypoint arrival distance
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_patrolMode(mode), m_rng(getSharedRNG()), m_seedSet(true) {
  setupModeDefaults(mode);
}

void PatrolBehavior::init(EntityHandle handle) {
  if (!handle.isValid() || m_waypoints.empty())
    return;

  m_currentWaypoint = 0;

  // Get position from EDM
  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx != SIZE_MAX) {
    Vector2D position = edm.getHotDataByIndex(idx).transform.position;
    if (isAtWaypoint(position, m_waypoints[m_currentWaypoint])) {
      m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
    }
  }

  // Bounds are enforced centrally by AIManager; no per-entity toggles needed
}

void PatrolBehavior::executeLogic(BehaviorContext& ctx) {
  if (!m_active || m_waypoints.empty()) {
    return;
  }

  // Ensure waypoint index is valid
  if (m_currentWaypoint >= m_waypoints.size()) {
    m_currentWaypoint = 0;
  }

  Vector2D position = ctx.transform.position;
  float deltaTime = ctx.deltaTime;

  // Increment timers (deltaTime in seconds)
  m_pathUpdateTimer += deltaTime;
  m_progressTimer += deltaTime;
  if (m_backoffTimer > 0.0f) {
    m_backoffTimer -= deltaTime; // Countdown
  }
  if (m_waypointCooldown > 0.0f) {
    m_waypointCooldown -= deltaTime; // Countdown
  }

  Vector2D targetWaypoint = m_waypoints[m_currentWaypoint];

  // State: APPROACHING_WAYPOINT - Check if we've reached current waypoint
  if (isAtWaypoint(position, targetWaypoint)) {
    // Enforce minimum time between waypoint transitions to prevent oscillation (750ms)
    if (m_waypointCooldown <= 0.0f) {
      m_waypointCooldown = m_config.waypointCooldown; // Cooldown before advancing to next waypoint

      // Advance to next waypoint
      m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();

      // Auto-regenerate waypoints if we completed a full cycle
      if (m_currentWaypoint == 0 && m_autoRegenerate &&
          m_patrolMode != PatrolMode::FIXED_WAYPOINTS) {
        regenerateRandomWaypoints();
      }

      // Update target after waypoint advance
      targetWaypoint = m_waypoints[m_currentWaypoint];

      // Clear path state when changing waypoints to force new pathfinding
      m_navPath.clear();
      m_navIndex = 0;
      m_pathUpdateTimer = 0.0f;
      m_stallTimer = 0.0f; // Reset stall detection
    }
  }

  // CACHE-AWARE PATROL: Smart pathfinding with cooldown system
  bool needsNewPath = false;

  // Only request new path if:
  // 1. No current path exists, OR
  // 2. Path is completed, OR
  // 3. Path is stale (>5 seconds), OR
  // 4. We changed waypoints
  if (m_navPath.empty() || m_navIndex >= m_navPath.size()) {
    needsNewPath = true;
  } else if (m_pathUpdateTimer > 5.0f) { // Path older than 5 seconds
    needsNewPath = true;
  } else {
    // Check if we're targeting a different waypoint than when path was computed
    Vector2D pathGoal = m_navPath.back();
    float const waypointChangeSquared = (targetWaypoint - pathGoal).lengthSquared();
    needsNewPath = (waypointChangeSquared > 2500.0f); // 50^2 = 2500
  }

  // OBSTACLE DETECTION: Force path refresh if stuck on obstacle (800ms = 0.8s)
  bool const stuckOnObstacle = (m_progressTimer > 0.8f);
  if (stuckOnObstacle) {
    m_navPath.clear();
    m_navIndex = 0;
  }

  // Per-instance cooldown via m_backoffTimer; no global static throttle
  if ((needsNewPath || stuckOnObstacle) && m_backoffTimer <= 0.0f) {
    // GOAL VALIDATION: Don't request path if already at waypoint
    float const distanceSquared = (targetWaypoint - position).lengthSquared();
    if (distanceSquared < (m_waypointRadius * m_waypointRadius)) {
      return;
    }

    Vector2D clampedStart = position;
    Vector2D clampedGoal = targetWaypoint;

    auto self = std::static_pointer_cast<PatrolBehavior>(shared_from_this());
    EntityID entityId = ctx.entityId;
    pathfinder().requestPath(
        entityId, clampedStart, clampedGoal,
        PathfinderManager::Priority::Normal,
        [self](EntityID, const std::vector<Vector2D>& path) {
          if (!path.empty()) {
            self->m_navPath = path;
            self->m_navIndex = 0;
            self->m_pathUpdateTimer = 0.0f;
          }
        });
    m_backoffTimer = m_config.pathRequestCooldown + (ctx.entityId % static_cast<int>(m_config.pathRequestCooldownVariation * 1000)) * 0.001f;
  }

  // State: FOLLOWING_PATH or DIRECT_MOVEMENT
  if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
    // Following computed path - lock-free path following
    Vector2D waypoint = m_navPath[m_navIndex];
    Vector2D toWaypoint = waypoint - position;
    float dist = toWaypoint.length();

    if (dist < m_navRadius) {
      m_navIndex++;
      if (m_navIndex < m_navPath.size()) {
        waypoint = m_navPath[m_navIndex];
        toWaypoint = waypoint - position;
        dist = toWaypoint.length();
      }
    }

    if (dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * m_moveSpeed;
      m_progressTimer = 0.0f;
      // CollisionManager handles overlap resolution
    }
  } else {
    // Direct movement to waypoint
    Vector2D direction = targetWaypoint - position;
    float const length = direction.length();
    if (length > 0.1f) {
      direction = direction * (1.0f / length);
    } else {
      direction.normalize();
    }
    ctx.transform.velocity = direction * m_moveSpeed;
    m_progressTimer = 0.0f;
  }

  // State: STALL_DETECTION - Handle entities that get stuck
  float currentSpeedSquared = ctx.transform.velocity.lengthSquared();
  const float stallThreshold = std::max(1.0f, m_moveSpeed * m_config.stallSpeedMultiplier);
  const float stallThresholdSquared = stallThreshold * stallThreshold;

  if (currentSpeedSquared < stallThresholdSquared) {
    m_stallTimer += deltaTime;
    if (m_stallTimer > 2.0f) {
      // Apply stall recovery: try sidestep maneuver or advance waypoint
      if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
        Vector2D toNode = m_navPath[m_navIndex] - position;
        float const len = toNode.length();
        if (len > 0.01f) {
          Vector2D const dir = toNode * (1.0f / len);
          Vector2D const perp(-dir.getY(), dir.getX());
          float side = ((ctx.entityId & 1) ? 1.0f : -1.0f);
          Vector2D sidestep = pathfinder().clampToWorldBounds(
              position + perp * (96.0f * side), 100.0f);
          auto self = std::static_pointer_cast<PatrolBehavior>(shared_from_this());
          EntityID entityId = ctx.entityId;
          pathfinder().requestPath(
              entityId, pathfinder().clampToWorldBounds(position, 100.0f), sidestep,
              PathfinderManager::Priority::Normal,
              [self](EntityID, const std::vector<Vector2D> &path) {
                if (!path.empty()) {
                  self->m_navPath = path;
                  self->m_navIndex = 0;
                  self->m_pathUpdateTimer = 0.0f;
                }
              });

          if (m_navPath.empty()) {
            m_backoffTimer = 10.0f + (ctx.entityId % 2000) * 0.001f;
            m_navPath.clear();
            m_navIndex = 0;
            if (m_waypointCooldown <= 0.0f) {
              m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
              m_waypointCooldown = 1.5f;
            }
          }
        }
      } else {
        m_backoffTimer = 10.0f + (ctx.entityId % 2000) * 0.001f;
        if (m_waypointCooldown <= 0.0f) {
          m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
          m_waypointCooldown = 1.5f;
        }
      }
      m_stallTimer = 0.0f;
    }
  } else {
    m_stallTimer = 0.0f;
  }
}

void PatrolBehavior::clean(EntityHandle handle) {
  if (handle.isValid()) {
    // Reset velocity via EDM
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }
  }

  m_needsReset = false;
}

void PatrolBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (message == "pause") {
    setActive(false);
    if (handle.isValid()) {
      auto& edm = EntityDataManager::Instance();
      size_t idx = edm.getIndex(handle);
      if (idx != SIZE_MAX) {
        edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      }
    }
  } else if (message == "resume") {
    setActive(true);
  } else if (message == "reverse") {
    reverseWaypoints();
  } else if (message == "release_entities") {
    if (handle.isValid()) {
      auto& edm = EntityDataManager::Instance();
      size_t idx = edm.getIndex(handle);
      if (idx != SIZE_MAX) {
        edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      }
    }

    m_needsReset = false;
  }
}

std::string PatrolBehavior::getName() const { return "Patrol"; }

std::shared_ptr<AIBehavior> PatrolBehavior::clone() const {
  std::shared_ptr<PatrolBehavior> cloned;

  // For FIXED_WAYPOINTS: copy waypoints (intentional shared routes)
  // For RANDOM/CIRCULAR/EVENT modes: regenerate unique waypoints per entity
  if (m_patrolMode == PatrolMode::FIXED_WAYPOINTS && !m_waypoints.empty()) {
    // Copy waypoints for fixed patrol routes (formations, convoys, etc.)
    cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed,
                                              m_includeOffscreenPoints);
  } else {
    // Use mode constructor to regenerate unique waypoints for random modes
    // This ensures each NPC clone gets different patrol routes
    cloned = std::make_shared<PatrolBehavior>(m_patrolMode, m_moveSpeed,
                                              m_includeOffscreenPoints);
  }

  cloned->setActive(m_active);

  if (m_patrolMode != PatrolMode::FIXED_WAYPOINTS) {
    cloned->m_patrolMode = m_patrolMode;
    cloned->m_areaTopLeft = m_areaTopLeft;
    cloned->m_areaBottomRight = m_areaBottomRight;
    cloned->m_areaCenter = m_areaCenter;
    cloned->m_areaRadius = m_areaRadius;
    cloned->m_useCircularArea = m_useCircularArea;
    cloned->m_waypointCount = m_waypointCount;
    cloned->m_autoRegenerate = m_autoRegenerate;
    cloned->m_minWaypointDistance = m_minWaypointDistance;
    cloned->m_eventTarget = m_eventTarget;
    cloned->m_eventTargetRadius = m_eventTargetRadius;
  }

  // PATHFINDING CONSOLIDATION: Removed async flag - always uses
  // PathfindingScheduler now

  return cloned;
}

void PatrolBehavior::addWaypoint(const Vector2D &waypoint) {
  m_waypoints.push_back(waypoint);
}

void PatrolBehavior::setWaypoints(const std::vector<Vector2D> &waypoints) {
  if (waypoints.size() >= 2) {
    m_waypoints = waypoints;
    m_currentWaypoint = 0;
  }
}

void PatrolBehavior::setIncludeOffscreenPoints(bool include) {
  m_includeOffscreenPoints = include;
}

const std::vector<Vector2D> &PatrolBehavior::getWaypoints() const {
  return m_waypoints;
}

void PatrolBehavior::setMoveSpeed(float speed) { m_moveSpeed = speed; }

bool PatrolBehavior::isAtWaypoint(const Vector2D &position,
                                  const Vector2D &waypoint) const {
  // PERFORMANCE: Use squared distance to avoid expensive sqrt()
  Vector2D const difference = position - waypoint;
  float const distanceSquared = difference.lengthSquared();

  // Use a more forgiving radius when moving fast to prevent oscillation
  float dynamicRadius = m_waypointRadius;

  // Add speed-based tolerance (approximation)
  if (m_moveSpeed > 50.0f) {
    dynamicRadius *= 1.5f; // Larger radius for fast-moving entities
  }

  return distanceSquared < (dynamicRadius * dynamicRadius);
}

void PatrolBehavior::resetEntityPosition(EntityPtr entity) {
  if (!entity)
    return;

  // Simply reset to the first waypoint since we no longer use screen bounds
  if (!m_waypoints.empty()) {
    entity->setPosition(m_waypoints[0]);
    m_currentWaypoint = 1 % m_waypoints.size();
  }
}

void PatrolBehavior::reverseWaypoints() {
  if (m_waypoints.size() < 2)
    return;

  std::reverse(m_waypoints.begin(), m_waypoints.end());

  if (m_currentWaypoint > 0) {
    m_currentWaypoint = m_waypoints.size() - m_currentWaypoint;
  }
}

void PatrolBehavior::setRandomPatrolArea(const Vector2D &topLeft,
                                         const Vector2D &bottomRight,
                                         int waypointCount) {
  m_patrolMode = PatrolMode::RANDOM_AREA;
  m_useCircularArea = false;
  m_areaTopLeft = topLeft;
  m_areaBottomRight = bottomRight;
  m_waypointCount = waypointCount;
  m_currentWaypoint = 0;

  generateRandomWaypointsInRectangle();
}

void PatrolBehavior::setRandomPatrolArea(const Vector2D &center, float radius,
                                         int waypointCount) {
  m_patrolMode = PatrolMode::RANDOM_AREA;
  m_useCircularArea = true;
  m_areaCenter = center;
  m_areaRadius = radius;
  m_waypointCount = waypointCount;
  m_currentWaypoint = 0;

  generateRandomWaypointsInCircle();
}

void PatrolBehavior::setEventTarget(const Vector2D &target, float radius,
                                    int waypointCount) {
  m_patrolMode = PatrolMode::EVENT_TARGET;
  m_eventTarget = target;
  m_eventTargetRadius = radius;
  m_waypointCount = waypointCount;
  m_currentWaypoint = 0;

  generateWaypointsAroundTarget();
}

void PatrolBehavior::updateEventTarget(const Vector2D &newTarget) {
  if (m_patrolMode == PatrolMode::EVENT_TARGET) {
    m_eventTarget = newTarget;
    generateWaypointsAroundTarget();
    m_currentWaypoint = 0;
  }
}

void PatrolBehavior::regenerateRandomWaypoints() {
  if (m_patrolMode == PatrolMode::RANDOM_AREA) {
    if (m_useCircularArea) {
      generateRandomWaypointsInCircle();
    } else {
      generateRandomWaypointsInRectangle();
    }
  } else if (m_patrolMode == PatrolMode::EVENT_TARGET) {
    generateWaypointsAroundTarget();
  }
  m_currentWaypoint = 0;
}

PatrolBehavior::PatrolMode PatrolBehavior::getPatrolMode() const {
  return m_patrolMode;
}

void PatrolBehavior::setAutoRegenerate(bool autoRegen) {
  m_autoRegenerate = autoRegen;
}

void PatrolBehavior::setMinWaypointDistance(float distance) {
  m_minWaypointDistance = distance;
}

void PatrolBehavior::setRandomSeed(unsigned int seed) {
  m_rng.seed(seed);
  m_seedSet = true;
}

void PatrolBehavior::generateRandomWaypointsInRectangle() {
  ensureRandomSeed();
  m_waypoints.clear();

  for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
    Vector2D newPoint;
    int attempts = 0;
    const int maxAttempts = m_config.randomWaypointGenerationAttempts;

    do {
      newPoint = generateRandomPointInRectangle();
      attempts++;
    } while (!isValidWaypointDistance(newPoint) && attempts < maxAttempts);

    m_waypoints.push_back(newPoint);
  }

  if (m_waypoints.size() < 2) {
    Vector2D const center = (m_areaTopLeft + m_areaBottomRight) * 0.5f;
    Vector2D const size = m_areaBottomRight - m_areaTopLeft;
    m_waypoints.clear();
    m_waypoints.push_back(center +
                          Vector2D(-size.getX() * 0.25f, -size.getY() * 0.25f));
    m_waypoints.push_back(center +
                          Vector2D(size.getX() * 0.25f, size.getY() * 0.25f));
  }
}

void PatrolBehavior::generateRandomWaypointsInCircle() {
  ensureRandomSeed();
  m_waypoints.clear();

  for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
    Vector2D newPoint;
    int attempts = 0;
    const int maxAttempts = m_config.randomWaypointGenerationAttempts;

    do {
      newPoint = generateRandomPointInCircle();
      attempts++;
    } while (!isValidWaypointDistance(newPoint) && attempts < maxAttempts);

    m_waypoints.push_back(newPoint);
  }

  if (m_waypoints.size() < 2) {
    m_waypoints.clear();
    m_waypoints.push_back(m_areaCenter + Vector2D(-m_areaRadius * 0.5f, 0));
    m_waypoints.push_back(m_areaCenter + Vector2D(m_areaRadius * 0.5f, 0));
  }
}

void PatrolBehavior::generateWaypointsAroundTarget() {
  ensureRandomSeed();
  m_waypoints.clear();

  float const angleStep = 2.0f * M_PI / m_waypointCount;

  std::vector<float> cosValues, sinValues;
  cosValues.reserve(m_waypointCount);
  sinValues.reserve(m_waypointCount);

  for (int i = 0; i < m_waypointCount; ++i) {
    float const angle = i * angleStep;
    cosValues.push_back(std::cos(angle));
    sinValues.push_back(std::sin(angle));
  }

  for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
    std::uniform_real_distribution<float> radiusDist(0.7f, 1.0f);
    float randomRadius = m_eventTargetRadius * radiusDist(m_rng);

    Vector2D waypoint = m_eventTarget + Vector2D(cosValues[i] * randomRadius,
                                                 sinValues[i] * randomRadius);

    m_waypoints.push_back(waypoint);
  }

  if (m_waypoints.size() < 2) {
    m_waypoints.clear();
    m_waypoints.push_back(m_eventTarget + Vector2D(-m_eventTargetRadius, 0));
    m_waypoints.push_back(m_eventTarget + Vector2D(m_eventTargetRadius, 0));
  }
}

Vector2D PatrolBehavior::generateRandomPointInRectangle() const {
  std::uniform_real_distribution<float> xDist(m_areaTopLeft.getX(),
                                              m_areaBottomRight.getX());
  std::uniform_real_distribution<float> yDist(m_areaTopLeft.getY(),
                                              m_areaBottomRight.getY());

  float x = xDist(m_rng);
  float y = yDist(m_rng);

  return Vector2D(x, y);
}

Vector2D PatrolBehavior::generateRandomPointInCircle() const {
  std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
  std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);

  float angle = angleDist(m_rng);
  float radius = std::sqrt(radiusDist(m_rng)) *
                 m_areaRadius; // sqrt for uniform distribution

  return m_areaCenter +
         Vector2D(std::cos(angle) * radius, std::sin(angle) * radius);
}

bool PatrolBehavior::isValidWaypointDistance(const Vector2D &newPoint) const {
  return std::all_of(m_waypoints.begin(), m_waypoints.end(),
                     [this, &newPoint](const Vector2D &existingPoint) {
                       Vector2D diff = newPoint - existingPoint;
                       return diff.length() >= m_minWaypointDistance;
                     });
}

void PatrolBehavior::ensureRandomSeed() const {}

void PatrolBehavior::setupModeDefaults(PatrolMode mode) {
  m_patrolMode = mode;

  // Use world bounds instead of screen dimensions for true world-scale patrolling
  float minX, minY, maxX, maxY;
  if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    // No world loaded - use fallback defaults and log warning
    AI_WARN("PatrolBehavior: World bounds unavailable, using fallback waypoints");
    m_waypoints.clear();
    m_waypoints.reserve(2);
    m_waypoints.emplace_back(100.0f, 100.0f);
    m_waypoints.emplace_back(300.0f, 300.0f);
    return;
  }

  constexpr float TILE = HammerEngine::TILE_SIZE;
  float const worldWidth = (maxX - minX) * TILE;
  float const worldHeight = (maxY - minY) * TILE;
  float const worldMinX = minX * TILE;
  float const worldMinY = minY * TILE;

  switch (mode) {
  case PatrolMode::FIXED_WAYPOINTS:
    if (m_waypoints.empty()) {
      float const margin = 100.0f;
      m_waypoints.reserve(4);
      m_waypoints.emplace_back(worldMinX + margin, worldMinY + margin);
      m_waypoints.emplace_back(worldMinX + worldWidth - margin,
                               worldMinY + margin);
      m_waypoints.emplace_back(worldMinX + worldWidth - margin,
                               worldMinY + worldHeight - margin);
      m_waypoints.emplace_back(worldMinX + margin,
                               worldMinY + worldHeight - margin);
    }
    break;

  case PatrolMode::RANDOM_AREA:
    m_useCircularArea = false;
    m_areaTopLeft = Vector2D(worldMinX + 50, worldMinY + 50);
    m_areaBottomRight =
        Vector2D(worldMinX + worldWidth * 0.4f, worldMinY + worldHeight - 50);
    m_waypointCount = 6;
    m_autoRegenerate = true;
    m_minWaypointDistance = 800.0f; // 10X larger for world scale
    generateRandomWaypointsInRectangle();
    break;

  case PatrolMode::EVENT_TARGET:
    m_eventTarget =
        Vector2D(worldMinX + worldWidth * 0.5f, worldMinY + worldHeight * 0.5f);
    m_eventTargetRadius = 1500.0f; // 10X larger for world scale
    m_waypointCount = 8;
    generateWaypointsAroundTarget();
    break;

  case PatrolMode::CIRCULAR_AREA:
    m_useCircularArea = true;
    m_areaCenter = Vector2D(worldMinX + worldWidth * 0.75f,
                            worldMinY + worldHeight * 0.5f);
    m_areaRadius = 1200.0f; // 10X larger for world scale
    m_waypointCount = 5;
    m_autoRegenerate = true;
    m_minWaypointDistance = 600.0f; // 10X larger for world scale
    generateRandomWaypointsInCircle();
    break;
  }
}
