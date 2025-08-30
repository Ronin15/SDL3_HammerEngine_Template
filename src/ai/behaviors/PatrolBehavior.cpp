/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/PatrolBehavior.hpp"
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
    : m_waypoints(waypoints), m_currentWaypoint(0),
      m_moveSpeed(moveSpeed), m_waypointRadius(25.0f),
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_screenWidth(1280.0f), m_screenHeight(720.0f),
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
    : m_waypoints(), m_currentWaypoint(0),
      m_moveSpeed(moveSpeed), m_waypointRadius(25.0f),
      m_includeOffscreenPoints(includeOffscreenPoints), m_needsReset(false),
      m_screenWidth(1280.0f), m_screenHeight(720.0f), m_patrolMode(mode),
      m_rng(getSharedRNG()), m_seedSet(true) {
  setupModeDefaults(mode, m_screenWidth, m_screenHeight);
}

void PatrolBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  m_currentWaypoint = 0;

  if (isAtWaypoint(entity->getPosition(), m_waypoints[m_currentWaypoint])) {
    m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
  }

  NPC *npc = dynamic_cast<NPC *>(entity.get());
  if (npc) {
    npc->setBoundsCheckEnabled(!m_includeOffscreenPoints);
  }
}

void PatrolBehavior::executeLogic(EntityPtr entity) {
  if (!entity || !m_active || m_waypoints.empty()) {
    return;
  }

  if (m_currentWaypoint >= m_waypoints.size()) {
    m_currentWaypoint = 0;
  }

  Vector2D position = entity->getPosition();

  if (m_needsReset && isWellOffscreen(position)) {
    resetEntityPosition(entity);
    m_needsReset = false;
    return;
  }

  Vector2D targetWaypoint = m_waypoints[m_currentWaypoint];
  // Clamp target to world bounds to avoid edge chasing
  float minX, minY, maxX, maxY;
  if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    const float TILE = 32.0f; const float margin = 16.0f;
    float worldMinX = minX * TILE + margin;
    float worldMinY = minY * TILE + margin;
    float worldMaxX = maxX * TILE - margin;
    float worldMaxY = maxY * TILE - margin;
    targetWaypoint.setX(std::clamp(targetWaypoint.getX(), worldMinX, worldMaxX));
    targetWaypoint.setY(std::clamp(targetWaypoint.getY(), worldMinY, worldMaxY));
  }

  // Request/refresh path to current waypoint if needed
  static thread_local Uint64 s_lastPathUpdate = 0;
  static thread_local float s_lastNodeDist = std::numeric_limits<float>::infinity();
  static thread_local Uint64 s_lastProgressTime = 0;
  Uint64 now = SDL_GetTicks();
  bool needRefresh = m_navPath.empty() || m_navIndex >= m_navPath.size();
  if (!needRefresh && m_navIndex < m_navPath.size()) {
    float d = (m_navPath[m_navIndex] - position).length();
    if (d + 1.0f < s_lastNodeDist) { s_lastNodeDist = d; s_lastProgressTime = now; }
    else if (s_lastProgressTime == 0) { s_lastProgressTime = now; }
    else if (now - s_lastProgressTime > 300) { needRefresh = true; }
  }
  if (now - s_lastPathUpdate > 1500) needRefresh = true;
  if (needRefresh) {
    AIManager::Instance().requestPath(entity, position, targetWaypoint);
    m_navPath = AIManager::Instance().getPath(entity);
    m_navIndex = 0;
    s_lastPathUpdate = now;
    s_lastNodeDist = std::numeric_limits<float>::infinity();
    s_lastProgressTime = now;
  }

  if (isAtWaypoint(position, targetWaypoint)) {
    m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();

    if (m_currentWaypoint == 0 && m_autoRegenerate &&
        m_patrolMode != PatrolMode::FIXED_WAYPOINTS) {
      regenerateRandomWaypoints();
    }

    targetWaypoint = m_waypoints[m_currentWaypoint];

    if (m_includeOffscreenPoints && isOffscreen(targetWaypoint)) {
      m_needsReset = true;
    }

    // Recompute path for next waypoint
    AIManager::Instance().requestPath(entity, position, targetWaypoint);
    m_navPath = AIManager::Instance().getPath(entity);
    m_navIndex = 0;
  }

  // Follow nav path if available; otherwise, direct seek
  if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
    Vector2D target = m_navPath[m_navIndex];
    Vector2D dir = target - position;
    float len = dir.length();
    if (len > 0.01f) {
      dir = dir * (1.0f / len);
      entity->setVelocity(dir * m_moveSpeed);
    } else {
      entity->setVelocity(Vector2D(0, 0));
    }
    if ((target - position).length() <= m_navRadius) {
      ++m_navIndex;
    }
  } else {
    Vector2D direction = targetWaypoint - position;
    float length = direction.length();
    if (length > 0.1f) direction = direction * (1.0f / length);
    else direction.normalize();
    entity->setVelocity(direction * m_moveSpeed);
  }

  // Stall detection: if moving very slowly, reset path and advance waypoint
  static thread_local Uint64 s_stallStart = 0; float spd = entity->getVelocity().length();
  if (spd < 5.0f) { if (s_stallStart == 0) s_stallStart = SDL_GetTicks(); }
  else s_stallStart = 0;
  if (s_stallStart && SDL_GetTicks() - s_stallStart > 600) {
    m_navPath.clear(); m_navIndex = 0; s_stallStart = 0;
    m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
  }
}

void PatrolBehavior::clean(EntityPtr entity) {
  if (entity) {
    entity->setVelocity(Vector2D(0, 0));

    NPC *npc = dynamic_cast<NPC *>(entity.get());
    if (npc) {
      npc->setBoundsCheckEnabled(true);
    }
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

      NPC *npc = dynamic_cast<NPC *>(entity.get());
      if (npc) {
        npc->setBoundsCheckEnabled(true);
      }
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

  cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
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

void PatrolBehavior::setScreenDimensions(float width, float height) {
  m_screenWidth = width;
  m_screenHeight = height;
}

const std::vector<Vector2D> &PatrolBehavior::getWaypoints() const {
  return m_waypoints;
}

void PatrolBehavior::setMoveSpeed(float speed) { m_moveSpeed = speed; }

bool PatrolBehavior::isAtWaypoint(const Vector2D &position,
                                  const Vector2D &waypoint) const {
  Vector2D difference = position - waypoint;
  return difference.length() < m_waypointRadius;
}

bool PatrolBehavior::isOffscreen(const Vector2D &position) const {
  return position.getX() < 0 || position.getX() > m_screenWidth ||
         position.getY() < 0 || position.getY() > m_screenHeight;
}

bool PatrolBehavior::isWellOffscreen(const Vector2D &position) const {
  const float buffer =
      100.0f; // Distance past the edge to consider "well offscreen"
  return position.getX() < -buffer ||
         position.getX() > m_screenWidth + buffer ||
         position.getY() < -buffer || position.getY() > m_screenHeight + buffer;
}

void PatrolBehavior::resetEntityPosition(EntityPtr entity) {
  if (!entity)
    return;

  for (size_t i = 0; i < m_waypoints.size(); i++) {
    size_t index = (m_currentWaypoint + i) % m_waypoints.size();
    if (!isOffscreen(m_waypoints[index])) {
      entity->setPosition(m_waypoints[index]);
      m_currentWaypoint =
          (index + 1) % m_waypoints.size();
      return;
    }
  }

  entity->setPosition(Vector2D(m_screenWidth / 2, m_screenHeight / 2));
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

void PatrolBehavior::setupModeDefaults(PatrolMode mode, float screenWidth,
                                       float screenHeight) {
  m_patrolMode = mode;
  m_screenWidth = screenWidth;
  m_screenHeight = screenHeight;

  switch (mode) {
  case PatrolMode::FIXED_WAYPOINTS:
    if (m_waypoints.empty()) {
      float margin = 100.0f;
      m_waypoints.reserve(4);
      m_waypoints.emplace_back(margin, margin);
      m_waypoints.emplace_back(screenWidth - margin, margin);
      m_waypoints.emplace_back(screenWidth - margin, screenHeight - margin);
      m_waypoints.emplace_back(margin, screenHeight - margin);
    }
    break;

  case PatrolMode::RANDOM_AREA:
    m_useCircularArea = false;
    m_areaTopLeft = Vector2D(50, 50);
    m_areaBottomRight = Vector2D(screenWidth * 0.4f, screenHeight - 50);
    m_waypointCount = 6;
    m_autoRegenerate = true;
    m_minWaypointDistance = 80.0f;
    generateRandomWaypointsInRectangle();
    break;

  case PatrolMode::EVENT_TARGET:
    m_eventTarget = Vector2D(screenWidth * 0.5f, screenHeight * 0.5f);
    m_eventTargetRadius = 150.0f;
    m_waypointCount = 8;
    generateWaypointsAroundTarget();
    break;

  case PatrolMode::CIRCULAR_AREA:
    m_useCircularArea = true;
    m_areaCenter = Vector2D(screenWidth * 0.75f, screenHeight * 0.5f);
    m_areaRadius = 120.0f;
    m_waypointCount = 5;
    m_autoRegenerate = true;
    m_minWaypointDistance = 60.0f;
    generateRandomWaypointsInCircle();
    break;
  }
}
