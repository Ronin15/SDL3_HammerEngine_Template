/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"
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

PatrolBehavior::PatrolBehavior(const std::vector<Vector2D> &waypoints,
                               float moveSpeed, bool includeOffscreenPoints)
    : m_waypoints(waypoints), m_currentWaypoint(0), m_moveSpeed(moveSpeed),
      m_waypointRadius(250.0f), // 10X larger
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_patrolMode(PatrolMode::FIXED_WAYPOINTS), m_rng(getSharedRNG()),
      m_seedSet(true) {
  m_waypoints.reserve(10);

  if (m_waypoints.size() < 2) {
    m_waypoints.push_back(Vector2D(100, 100));
    m_waypoints.push_back(Vector2D(200, 200));
  }
}

PatrolBehavior::PatrolBehavior(PatrolMode mode, float moveSpeed,
                               bool includeOffscreenPoints)
    : m_waypoints(), m_currentWaypoint(0), m_moveSpeed(moveSpeed),
      m_waypointRadius(250.0f), // 10X larger
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_patrolMode(mode), m_rng(getSharedRNG()), m_seedSet(true) {
  setupModeDefaults(mode);
}

void PatrolBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  m_currentWaypoint = 0;

  if (isAtWaypoint(entity->getPosition(), m_waypoints[m_currentWaypoint])) {
    m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
  }

  // Bounds are enforced centrally by AIManager; no per-entity toggles needed
}

void PatrolBehavior::executeLogic(EntityPtr entity) {
  if (!entity || !m_active || m_waypoints.empty()) {
    return;
  }

  // Ensure waypoint index is valid
  if (m_currentWaypoint >= m_waypoints.size()) {
    m_currentWaypoint = 0;
  }

  Vector2D position = entity->getPosition();
  Uint64 now = SDL_GetTicks();

  Vector2D targetWaypoint = m_waypoints[m_currentWaypoint];

  // Use behavior-defined target; managers will normalize for pathfinding

  // State: APPROACHING_WAYPOINT - Check if we've reached current waypoint
  if (isAtWaypoint(position, targetWaypoint)) {
    // Enforce minimum time between waypoint transitions to prevent oscillation
    if (now - m_lastWaypointTime >= 750) { // Increased from 500ms to 750ms
      m_lastWaypointTime = now;

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
      m_lastPathUpdate = 0;
      m_stallStart = 0; // Reset stall detection
    }
    // If still in cooldown, continue toward current waypoint without advancing
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
  } else if (now - m_lastPathUpdate > 5000) { // Path older than 5 seconds
    needsNewPath = true;  
  } else {
    // Check if we're targeting a different waypoint than when path was computed
    Vector2D pathGoal = m_navPath.back();
    float waypointChange = (targetWaypoint - pathGoal).length();
    needsNewPath = (waypointChange > 50.0f); // Waypoint changed significantly
  }
  
  // Per-instance cooldown via m_backoffUntil; no global static throttle
  if (needsNewPath && now >= m_backoffUntil) {
    // GOAL VALIDATION: Don't request path if already at waypoint
    float distanceToWaypoint = (targetWaypoint - position).length();
    if (distanceToWaypoint < m_waypointRadius) { // Already at waypoint
      return; // Skip pathfinding request
    }
    
    // PATHFINDING CONSOLIDATION: All requests now use PathfinderManager  
    Vector2D clampedStart = position;
    Vector2D clampedGoal = targetWaypoint;
    
    pathfinder().requestPath(
        entity->getID(), clampedStart, clampedGoal,
        PathfinderManager::Priority::Normal,
        [this, entity](EntityID, const std::vector<Vector2D>& path) {
          if (!path.empty()) {
            m_navPath = path;
            m_navIndex = 0;
            m_lastPathUpdate = SDL_GetTicks();
          }
        });
    // Staggered per-instance backoff to prevent bursty re-requests (more conservative)
    m_backoffUntil = now + 1200 + (entity->getID() % 600);
  }

  // State: FOLLOWING_PATH or DIRECT_MOVEMENT
  if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
    // Following computed path
    using namespace AIInternal;
    bool following =
        pathfinder().followPathStep(entity, position, m_navPath, m_navIndex,
                                 m_moveSpeed, m_navRadius);
    if (following) {
      m_lastProgressTime = now;
      // Separation decimation: compute at most every 2 ticks
      applyDecimatedSeparation(entity, position, entity->getVelocity(),
                               m_moveSpeed, 24.0f, 0.20f, 4, m_lastSepTick,
                               m_lastSepVelocity);
    }
  } else {
    // Direct movement to waypoint
    Vector2D direction = targetWaypoint - position;
    float length = direction.length();
    if (length > 0.1f) {
      direction = direction * (1.0f / length);
    } else {
      direction.normalize();
    }
    entity->setVelocity(direction * m_moveSpeed);
    m_lastProgressTime = now;
  }

  // State: STALL_DETECTION - Handle entities that get stuck
  float currentSpeed = entity->getVelocity().length();
  const float stallThreshold = std::max(1.0f, m_moveSpeed * 0.3f);

  if (currentSpeed < stallThreshold) {
    if (m_stallStart == 0) {
      m_stallStart = now;
    } else if (now - m_stallStart > 2000) { // 2 second stall detection
      // Apply stall recovery: try sidestep maneuver or advance waypoint
      if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
        Vector2D toNode = m_navPath[m_navIndex] - position;
        float len = toNode.length();
        if (len > 0.01f) {
          Vector2D dir = toNode * (1.0f / len);
          Vector2D perp(-dir.getY(), dir.getX());
          float side = ((entity->getID() & 1) ? 1.0f : -1.0f);
          Vector2D sidestep = pathfinder().clampToWorldBounds(
              position + perp * (96.0f * side), 100.0f);
          pathfinder().requestPath(
              entity->getID(), pathfinder().clampToWorldBounds(position, 100.0f), sidestep,
              PathfinderManager::Priority::Normal,
              [this](EntityID, const std::vector<Vector2D> &path) {
                if (!path.empty()) {
                  m_navPath = path;
                  m_navIndex = 0;
                  m_lastPathUpdate = SDL_GetTicks();
                }
              });

          if (m_navPath.empty()) {
            // Fallback: advance waypoint and apply backoff
            m_backoffUntil = now + 800 + (entity->getID() % 400);
            m_navPath.clear();
            m_navIndex = 0;
            if (now - m_lastWaypointTime > 1500) {
              m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
              m_lastWaypointTime = now;
            }
          }
        }
      } else {
        // No path available, advance waypoint
        m_backoffUntil = now + 800 + (entity->getID() % 400);
        if (now - m_lastWaypointTime > 1500) {
          m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
          m_lastWaypointTime = now;
        }
      }
      m_stallStart = 0; // Reset stall timer after recovery attempt
    }
  } else {
    m_stallStart = 0; // Reset stall timer when moving normally
  }
}

void PatrolBehavior::clean(EntityPtr entity) {
  if (entity) {
    entity->setVelocity(Vector2D(0, 0));
  }

  m_needsReset = false;
}

void PatrolBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (message == "pause") {
    setActive(false);
    if (entity) {
      entity->setVelocity(Vector2D(0, 0));
    }
  } else if (message == "resume") {
    setActive(true);
  } else if (message == "reverse") {
    reverseWaypoints();
  } else if (message == "release_entities") {
    if (entity) {
      entity->setVelocity(Vector2D(0, 0));
    }

    m_needsReset = false;
  }
}

std::string PatrolBehavior::getName() const { return "Patrol"; }

std::shared_ptr<AIBehavior> PatrolBehavior::clone() const {
  std::shared_ptr<PatrolBehavior> cloned;
  if (m_patrolMode == PatrolMode::FIXED_WAYPOINTS && !m_waypoints.empty()) {
    cloned = std::make_shared<PatrolBehavior>(m_patrolMode, m_moveSpeed,
                                              m_includeOffscreenPoints);
  } else {
    cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed,
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
  Vector2D difference = position - waypoint;
  float distance = difference.length();

  // Use a more forgiving radius when moving fast to prevent oscillation
  float dynamicRadius = m_waypointRadius;

  // Add speed-based tolerance (approximation)
  if (m_moveSpeed > 50.0f) {
    dynamicRadius *= 1.5f; // Larger radius for fast-moving entities
  }

  return distance < dynamicRadius;
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
    const int maxAttempts = 50;

    do {
      newPoint = generateRandomPointInRectangle();
      attempts++;
    } while (!isValidWaypointDistance(newPoint) && attempts < maxAttempts);

    m_waypoints.push_back(newPoint);
  }

  if (m_waypoints.size() < 2) {
    Vector2D center = (m_areaTopLeft + m_areaBottomRight) * 0.5f;
    Vector2D size = m_areaBottomRight - m_areaTopLeft;
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
    const int maxAttempts = 50;

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

  float angleStep = 2.0f * M_PI / m_waypointCount;

  std::vector<float> cosValues, sinValues;
  cosValues.reserve(m_waypointCount);
  sinValues.reserve(m_waypointCount);

  for (int i = 0; i < m_waypointCount; ++i) {
    float angle = i * angleStep;
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

  // Use world bounds instead of screen dimensions for true world-scale
  // patrolling
  float minX, minY, maxX, maxY;
  bool hasWorldBounds =
      WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY);
  constexpr float TILE = HammerEngine::TILE_SIZE;
  float worldWidth = hasWorldBounds ? (maxX - minX) * TILE : HammerEngine::DEFAULT_WORLD_WIDTH;
  float worldHeight = hasWorldBounds ? (maxY - minY) * TILE : HammerEngine::DEFAULT_WORLD_HEIGHT;
  float worldMinX = hasWorldBounds ? minX * TILE : 0.0f;
  float worldMinY = hasWorldBounds ? minY * TILE : 0.0f;

  switch (mode) {
  case PatrolMode::FIXED_WAYPOINTS:
    if (m_waypoints.empty()) {
      float margin = 100.0f;
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
