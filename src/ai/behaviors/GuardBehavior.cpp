/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/GuardBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"  // For PathPriority enum
#include <algorithm>
#include <cmath>

GuardBehavior::GuardBehavior(const Vector2D &guardPosition, float guardRadius,
                             float alertRadius)
    : m_entityStates(),
      m_guardMode(GuardMode::STATIC_GUARD), m_guardPosition(guardPosition),
      m_guardRadius(guardRadius), m_alertRadius(alertRadius),
      m_movementSpeed(1.5f), m_alertSpeed(3.0f), m_patrolWaypoints(),
      m_patrolReverse(false), m_areaCenter(guardPosition), m_areaTopLeft(),
      m_areaBottomRight(), m_areaRadius(guardRadius), m_useCircularArea(false) {
}

GuardBehavior::GuardBehavior(GuardMode mode, const Vector2D &guardPosition,
                             float guardRadius)
    : m_entityStates(), m_guardMode(mode),
      m_guardPosition(guardPosition), m_guardRadius(guardRadius),
      m_alertRadius(guardRadius * 1.5f), m_movementSpeed(1.5f),
      m_alertSpeed(3.0f), m_patrolWaypoints(), m_patrolReverse(false),
      m_areaCenter(guardPosition), m_areaTopLeft(), m_areaBottomRight(),
      m_areaRadius(guardRadius), m_useCircularArea(false) {
  // Adjust parameters based on mode
  switch (mode) {
  case GuardMode::STATIC_GUARD:
    m_movementSpeed = 0.0f; // No movement unless investigating
    m_alertRadius = guardRadius * 1.5f;
    break;
  case GuardMode::PATROL_GUARD:
    m_movementSpeed = 1.5f;
    m_alertRadius = guardRadius * 1.8f;
    break;
  case GuardMode::AREA_GUARD:
    m_movementSpeed = 1.2f;
    m_alertRadius = guardRadius * 2.0f;
    break;
  case GuardMode::ROAMING_GUARD:
    m_movementSpeed = 1.8f;
    m_alertRadius = guardRadius * 1.6f;
    m_roamInterval = 6.0f;
    break;
  case GuardMode::ALERT_GUARD:
    m_movementSpeed = 2.5f;
    m_alertSpeed = 4.0f;
    m_alertRadius = guardRadius * 2.5f;
    m_threatDetectionRange = guardRadius * 2.0f;
    break;
  }
}

GuardBehavior::GuardBehavior(const HammerEngine::GuardBehaviorConfig& config,
                             const Vector2D& guardPosition,
                             GuardMode mode)
    : m_config(config)
    , m_guardMode(mode)
    , m_guardPosition(guardPosition)
    , m_guardRadius(config.guardRadius)
    , m_alertRadius(config.guardRadius * 1.5f)
    , m_movementSpeed(config.guardSpeed)
    , m_alertSpeed(config.guardSpeed * 1.5f)
    , m_areaCenter(guardPosition)
    , m_areaRadius(config.guardRadius)
{
  // Mode-specific adjustments using config values
  switch (mode) {
  case GuardMode::STATIC_GUARD:
    m_movementSpeed = 0.0f; // No movement unless investigating
    break;
  case GuardMode::PATROL_GUARD:
    m_alertRadius = config.guardRadius * 1.8f;
    break;
  case GuardMode::AREA_GUARD:
    m_movementSpeed = config.guardSpeed * 0.8f;
    m_alertRadius = config.guardRadius * 2.0f;
    break;
  case GuardMode::ROAMING_GUARD:
    m_movementSpeed = config.guardSpeed * 1.2f;
    m_alertRadius = config.guardRadius * 1.6f;
    m_roamInterval = 6.0f;
    break;
  case GuardMode::ALERT_GUARD:
    m_movementSpeed = config.guardSpeed * 1.7f;
    m_alertSpeed = config.guardSpeed * 2.0f;
    m_alertRadius = config.guardRadius * 2.5f;
    m_threatDetectionRange = config.guardRadius * 2.0f;
    break;
  }
}

void GuardBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto &state = m_entityStates[handle.getId()];
  state = EntityState(); // Reset to default state
  state.assignedPosition = m_guardPosition;
  state.currentMode = m_guardMode;
  state.onDuty = true;

  // Initialize based on mode
  if (m_guardMode == GuardMode::PATROL_GUARD && !m_patrolWaypoints.empty()) {
    state.currentPatrolTarget = m_patrolWaypoints[0];
    state.currentPatrolIndex = 0;
  } else if (m_guardMode == GuardMode::ROAMING_GUARD) {
    state.roamTarget = generateRoamTarget(state);
    state.roamTimer = m_roamInterval;
  }
}

void GuardBehavior::executeLogic(BehaviorContext& ctx) {
  if (!isActive())
    return;

  auto it = m_entityStates.find(ctx.entityId);
  if (it == m_entityStates.end()) {
    // Need EntityPtr for init - this is rare (first frame only)
    return;
  }

  EntityState &state = it->second;

  if (!state.onDuty) {
    return; // Guard is off duty
  }

  // Update all timers
  state.threatSightingTimer += ctx.deltaTime;
  state.alertTimer += ctx.deltaTime;
  state.investigationTimer += ctx.deltaTime;
  state.positionCheckTimer += ctx.deltaTime;
  state.patrolMoveTimer += ctx.deltaTime;
  state.alertDecayTimer += ctx.deltaTime;
  state.roamTimer -= ctx.deltaTime;
  state.baseState.pathUpdateTimer += ctx.deltaTime;
  state.baseState.progressTimer += ctx.deltaTime;
  if (state.baseState.backoffTimer > 0.0f) state.baseState.backoffTimer -= ctx.deltaTime;

  // Detect threats (uses ctx.transform.position internally)
  EntityHandle threat = detectThreat(ctx, state);
  bool threatPresent = threat.isValid();

  // Update alert level based on threat presence
  updateAlertLevel(state, threatPresent);

  if (threatPresent) {
    handleThreatDetection(ctx, state, threat);
  } else if (state.isInvestigating) {
    handleInvestigation(ctx, state);
  } else if (state.returningToPost) {
    handleReturnToPost(ctx, state);
  } else {
    // Normal guard behavior based on mode
    switch (state.currentMode) {
    case GuardMode::STATIC_GUARD:
      updateStaticGuard(ctx, state);
      break;
    case GuardMode::PATROL_GUARD:
      updatePatrolGuard(ctx, state);
      break;
    case GuardMode::AREA_GUARD:
      updateAreaGuard(ctx, state);
      break;
    case GuardMode::ROAMING_GUARD:
      updateRoamingGuard(ctx, state);
      break;
    case GuardMode::ALERT_GUARD:
      updateAlertGuard(ctx, state);
      break;
    }
  }

  // Handle alert decay
  if (state.currentAlertLevel > AlertLevel::CALM &&
      state.alertDecayTimer > m_alertDecayTime) {

    // Reduce alert level by one step
    state.currentAlertLevel =
        static_cast<AlertLevel>(static_cast<int>(state.currentAlertLevel) - 1);
    state.alertDecayTimer = 0.0f;
  }
}

void GuardBehavior::clean(EntityHandle handle) {
  if (handle.isValid()) {
    // Reset velocity via EDM
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }
    m_entityStates.erase(handle.getId());
  }
}

void GuardBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto it = m_entityStates.find(handle.getId());
  if (it == m_entityStates.end())
    return;

  EntityState &state = it->second;

  if (message == "go_on_duty") {
    state.onDuty = true;
  } else if (message == "go_off_duty") {
    state.onDuty = false;
    state.currentAlertLevel = AlertLevel::CALM;
  } else if (message == "raise_alert") {
    state.currentAlertLevel = AlertLevel::HOSTILE;
    state.alertTimer = 0.0f;
  } else if (message == "clear_alert") {
    // Clear alert for this entity
    state.currentAlertLevel = AlertLevel::CALM;
    state.hasActiveThreat = false;
    state.isInvestigating = false;
    state.alertRaised = false;
    state.helpCalled = false;
  } else if (message == "investigate_position") {
    state.isInvestigating = true;
    // Get position from EDM
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      state.investigationTarget = edm.getHotDataByIndex(idx).transform.position;
    }
    state.investigationTimer = 0.0f;
  } else if (message == "return_to_post") {
    state.returningToPost = true;
    state.isInvestigating = false;
  } else if (message == "patrol_mode") {
    state.currentMode = GuardMode::PATROL_GUARD;
  } else if (message == "static_mode") {
    state.currentMode = GuardMode::STATIC_GUARD;
  } else if (message == "roam_mode") {
    state.currentMode = GuardMode::ROAMING_GUARD;
  }
}

std::string GuardBehavior::getName() const { return "Guard"; }

void GuardBehavior::setGuardPosition(const Vector2D &position) {
  m_guardPosition = position;
  m_areaCenter = position;

  // Update all entity states
  for (auto &pair : m_entityStates) {
    pair.second.assignedPosition = position;
  }
}

void GuardBehavior::setGuardRadius(float radius) {
  m_guardRadius = std::max(0.0f, radius);
  if (!m_useCircularArea) {
    m_areaRadius = m_guardRadius;
  }
}

void GuardBehavior::setAlertRadius(float radius) {
  m_alertRadius = std::max(0.0f, radius);
}

void GuardBehavior::setGuardMode(GuardMode mode) {
  m_guardMode = mode;

  // Update all entity states
  for (auto &pair : m_entityStates) {
    pair.second.currentMode = mode;
  }
}

void GuardBehavior::setMovementSpeed(float speed) {
  m_movementSpeed = std::max(0.0f, speed);
}

void GuardBehavior::setAlertSpeed(float speed) {
  m_alertSpeed = std::max(0.0f, speed);
}

void GuardBehavior::setInvestigationTime(float seconds) {
  m_investigationTime = std::max(0.0f, seconds);
}

void GuardBehavior::setReturnToPostTime(float seconds) {
  m_returnToPostTime = std::max(0.0f, seconds);
}

void GuardBehavior::addPatrolWaypoint(const Vector2D &waypoint) {
  m_patrolWaypoints.push_back(waypoint);
}

void GuardBehavior::setPatrolWaypoints(const std::vector<Vector2D> &waypoints) {
  m_patrolWaypoints = waypoints;
}

void GuardBehavior::setGuardArea(const Vector2D &center, float radius) {
  m_areaCenter = center;
  m_areaRadius = radius;
  m_useCircularArea = true;
}

void GuardBehavior::setGuardArea(const Vector2D &topLeft,
                                 const Vector2D &bottomRight) {
  m_areaTopLeft = topLeft;
  m_areaBottomRight = bottomRight;
  m_areaCenter = (topLeft + bottomRight) * 0.5f;
  m_useCircularArea = false;
}

void GuardBehavior::setAlertLevel(AlertLevel level) {
  for (auto &pair : m_entityStates) {
    pair.second.currentAlertLevel = level;
    if (level > AlertLevel::CALM) {
      pair.second.alertTimer = 0.0f;
    }
  }
}

void GuardBehavior::raiseAlert(EntityHandle handle,
                               const Vector2D &alertPosition) {
  if (!handle.isValid())
    return;

  auto it = m_entityStates.find(handle.getId());
  if (it != m_entityStates.end()) {
    EntityState &state = it->second;
    state.currentAlertLevel = AlertLevel::HOSTILE;
    state.alertTimer = 0.0f;
    state.lastKnownThreatPosition = alertPosition;
    state.alertRaised = true;

    if (m_canCallForHelp && !state.helpCalled) {
      callForHelp(alertPosition);
      state.helpCalled = true;
    }
  }
}

void GuardBehavior::clearAlert(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto it = m_entityStates.find(handle.getId());
  if (it != m_entityStates.end()) {
    EntityState &state = it->second;
    state.currentAlertLevel = AlertLevel::CALM;
    state.hasActiveThreat = false;
    state.isInvestigating = false;
    state.alertRaised = false;
    state.helpCalled = false;
  }
}

void GuardBehavior::setAlertDecayTime(float seconds) {
  m_alertDecayTime = std::max(0.0f, seconds);
}

void GuardBehavior::setThreatDetectionRange(float range) {
  m_threatDetectionRange = std::max(0.0f, range);
}

void GuardBehavior::setFieldOfView(float angleDegrees) {
  m_fieldOfView = std::clamp(angleDegrees, 0.0f, 360.0f);
}

void GuardBehavior::setLineOfSightRequired(bool required) {
  m_lineOfSightRequired = required;
}

void GuardBehavior::setCanCallForHelp(bool canCall) {
  m_canCallForHelp = canCall;
}

void GuardBehavior::setHelpCallRadius(float radius) {
  m_helpCallRadius = std::max(0.0f, radius);
}

void GuardBehavior::setGuardGroup(int groupId) { m_guardGroup = groupId; }

bool GuardBehavior::isOnDuty() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto &pair) { return pair.second.onDuty; });
}

bool GuardBehavior::isAlerted() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto &pair) {
                       return pair.second.currentAlertLevel > AlertLevel::CALM;
                     });
}

bool GuardBehavior::isInvestigating() const {
  return std::any_of(
      m_entityStates.begin(), m_entityStates.end(),
      [](const auto &pair) { return pair.second.isInvestigating; });
}

GuardBehavior::AlertLevel GuardBehavior::getCurrentAlertLevel() const {
  if (m_entityStates.empty())
    return AlertLevel::CALM;
  auto it = std::max_element(m_entityStates.begin(), m_entityStates.end(),
                             [](const auto &a, const auto &b) {
                               return a.second.currentAlertLevel <
                                      b.second.currentAlertLevel;
                             });
  return it->second.currentAlertLevel;
}

GuardBehavior::GuardMode GuardBehavior::getGuardMode() const {
  return m_guardMode;
}

Vector2D GuardBehavior::getGuardPosition() const { return m_guardPosition; }

float GuardBehavior::getDistanceFromPost() const {
  // NOTE: This method cannot be easily implemented with the new ID-based entity map
  // as we don't have direct position access. Would need EntityPtr parameter.
  // Returning 0.0f as this is not used in critical paths.
  return 0.0f;
}

std::shared_ptr<AIBehavior> GuardBehavior::clone() const {
  auto clone = std::make_shared<GuardBehavior>(m_guardMode, m_guardPosition,
                                               m_guardRadius);
  clone->m_alertRadius = m_alertRadius;
  clone->m_movementSpeed = m_movementSpeed;
  clone->m_alertSpeed = m_alertSpeed;
  clone->m_patrolWaypoints = m_patrolWaypoints;
  clone->m_areaCenter = m_areaCenter;
  clone->m_areaTopLeft = m_areaTopLeft;
  clone->m_areaBottomRight = m_areaBottomRight;
  clone->m_areaRadius = m_areaRadius;
  clone->m_useCircularArea = m_useCircularArea;
  clone->m_investigationTime = m_investigationTime;
  clone->m_returnToPostTime = m_returnToPostTime;
  clone->m_alertDecayTime = m_alertDecayTime;
  clone->m_roamInterval = m_roamInterval;
  clone->m_threatDetectionRange = m_threatDetectionRange;
  clone->m_fieldOfView = m_fieldOfView;
  clone->m_lineOfSightRequired = m_lineOfSightRequired;
  clone->m_canCallForHelp = m_canCallForHelp;
  clone->m_helpCallRadius = m_helpCallRadius;
  clone->m_guardGroup = m_guardGroup;
  // PATHFINDING CONSOLIDATION: Removed async flag - always uses PathfindingScheduler now
  return clone;
}

EntityHandle GuardBehavior::detectThreat(const BehaviorContext& ctx,
                                         const EntityState &state) const {
  const auto& aiMgr = AIManager::Instance();
  if (!aiMgr.isPlayerValid())
    return EntityHandle{};

  Vector2D threatPos = aiMgr.getPlayerPosition();

  // Check if threat is in detection range
  float distance = (ctx.transform.position - threatPos).length();
  if (distance > m_threatDetectionRange) {
    return EntityHandle{};
  }

  // Check field of view if required
  if (m_fieldOfView < 360.0f) {
    float angleToThreat = calculateAngleToTarget(ctx.transform.position, threatPos);
    float angleDiff = std::abs(normalizeAngle(angleToThreat - state.currentHeading));
    float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
    if (angleDiff > halfFOV) {
      return EntityHandle{};
    }
  }

  return aiMgr.getPlayerHandle();
}

bool GuardBehavior::isThreatInRange(const Vector2D& entityPos, const Vector2D& threatPos) const {
  float distance = calculateThreatDistance(entityPos, threatPos);
  return distance <= m_threatDetectionRange;
}

bool GuardBehavior::isThreatInFieldOfView(const Vector2D& entityPos, const Vector2D& threatPos,
                                          const EntityState &state) const {
  // Calculate angle to threat
  float angleToThreat = calculateAngleToTarget(entityPos, threatPos);

  // Calculate angular difference from current heading
  float angleDiff =
      std::abs(normalizeAngle(angleToThreat - state.currentHeading));

  // Check if within field of view
  float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
  return angleDiff <= halfFOV;
}

bool GuardBehavior::hasLineOfSight(const Vector2D& /*entityPos*/,
                                   const Vector2D& /*threatPos*/) const {
  // Simplified line of sight check
  // In a full implementation, this would do proper collision detection
  return true;
}

float GuardBehavior::calculateThreatDistance(const Vector2D& entityPos,
                                             const Vector2D& threatPos) const {
  return (entityPos - threatPos).length();
}

void GuardBehavior::updateAlertLevel(EntityState &state,
                                     bool threatPresent) const {


  if (threatPresent) {
    state.threatSightingTimer = 0.0f; // Reset threat sighting timer
    state.hasActiveThreat = true;

    // Escalate alert level based on how long threat has been present
    float const threatDuration = state.alertTimer;

    if (state.currentAlertLevel == AlertLevel::CALM) {
      state.currentAlertLevel = AlertLevel::SUSPICIOUS;
      state.alertTimer = 0.0f;
    } else if (state.currentAlertLevel == AlertLevel::SUSPICIOUS &&
               threatDuration > SUSPICIOUS_THRESHOLD) {
      state.currentAlertLevel = AlertLevel::INVESTIGATING;
    } else if (state.currentAlertLevel == AlertLevel::INVESTIGATING &&
               threatDuration > INVESTIGATING_THRESHOLD) {
      state.currentAlertLevel = AlertLevel::HOSTILE;
    }
  } else {
    state.hasActiveThreat = false;

    // Start alert decay if no threat seen for a while
    if (state.threatSightingTimer > m_alertDecayTime * 0.5f) {
      state.alertDecayTimer = 0.0f;
    }
  }
}

void GuardBehavior::handleThreatDetection(BehaviorContext& ctx, EntityState &state,
                                          EntityHandle threat) {
  if (!threat.isValid())
    return;

  // Get threat position from AIManager (which reads from EDM)
  Vector2D threatPos = AIManager::Instance().getPlayerPosition();
  state.lastKnownThreatPosition = threatPos;

  // React based on alert level
  switch (state.currentAlertLevel) {
  case AlertLevel::SUSPICIOUS:
    // Turn towards threat, slow approach
    state.currentHeading =
        calculateAngleToTarget(ctx.transform.position, threatPos);
    break;

  case AlertLevel::INVESTIGATING:
    // Move towards threat for investigation
    state.isInvestigating = true;
    state.investigationTarget = threatPos;
    state.investigationTimer = 0.0f;
    moveToPositionDirect(ctx, threatPos, m_movementSpeed, state.baseState);
    break;

  case AlertLevel::HOSTILE:
    // Engage threat or call for help
    if (m_canCallForHelp && !state.helpCalled) {
      callForHelp(threatPos);
      state.helpCalled = true;
    }
    // Move towards threat at alert speed
    moveToPositionDirect(ctx, threatPos, m_alertSpeed, state.baseState, 2);
    break;

  case AlertLevel::ALARM:
    // Maximum response - could switch to combat behavior
    moveToPositionDirect(ctx, threatPos, m_alertSpeed * 1.2f, state.baseState, 3);
    break;

  default:
    break;
  }
}

void GuardBehavior::handleInvestigation(BehaviorContext& ctx, EntityState &state) {
  // Check if investigation time has expired
  if (state.investigationTimer > m_investigationTime) {
    state.isInvestigating = false;
    state.returningToPost = true;
    return;
  }

  // Move to investigation target
  if (!isAtPosition(ctx.transform.position, state.investigationTarget)) {
    moveToPositionDirect(ctx, state.investigationTarget, m_movementSpeed, state.baseState);
  }
}

void GuardBehavior::handleReturnToPost(BehaviorContext& ctx, EntityState &state) {
  // Return to assigned position
  if (!isAtPosition(ctx.transform.position, state.assignedPosition)) {
    moveToPositionDirect(ctx, state.assignedPosition, m_movementSpeed, state.baseState);
  } else {
    state.returningToPost = false;
    state.currentAlertLevel = AlertLevel::CALM;
  }
}

void GuardBehavior::updateStaticGuard(BehaviorContext& ctx, EntityState &state) {
  // Stay at assigned position
  if (!isAtPosition(ctx.transform.position, state.assignedPosition, 10.0f)) {
    moveToPositionDirect(ctx, state.assignedPosition, m_movementSpeed, state.baseState);
  }

  // Update heading to scan area
  if (state.positionCheckTimer > 2.0f) { // Check every 2 seconds
    state.currentHeading += 0.5f;        // Slow rotation
    state.currentHeading = normalizeAngle(state.currentHeading);
    state.positionCheckTimer = 0.0f;
  }
}

void GuardBehavior::updatePatrolGuard(BehaviorContext& ctx, EntityState &state) {
  if (m_patrolWaypoints.empty())
    return;

  // Move to current patrol waypoint
  if (isAtPosition(ctx.transform.position, state.currentPatrolTarget)) {
    // Move to next waypoint
    if (m_patrolReverse) {
      if (state.currentPatrolIndex == 0) {
        state.currentPatrolIndex = m_patrolWaypoints.size() - 1;
      } else {
        state.currentPatrolIndex--;
      }
    } else {
      state.currentPatrolIndex =
          (state.currentPatrolIndex + 1) % m_patrolWaypoints.size();
    }

    state.currentPatrolTarget = getNextPatrolWaypoint(state);
  } else {
    moveToPositionDirect(ctx, state.currentPatrolTarget, m_movementSpeed, state.baseState);
  }
}

void GuardBehavior::updateAreaGuard(BehaviorContext& ctx, EntityState &state) {
  // Ensure we're within the guard area
  if (!isWithinGuardArea(ctx.transform.position)) {
    Vector2D const clampedPos = clampToGuardArea(ctx.transform.position);
    moveToPositionDirect(ctx, clampedPos, m_movementSpeed, state.baseState);
  } else {
    // Patrol within the area
    if (state.roamTimer <= 0.0f) {
      state.roamTarget = generateRoamTarget(state);
      state.roamTimer = m_roamInterval;
    }

    if (!isAtPosition(ctx.transform.position, state.roamTarget)) {
      moveToPositionDirect(ctx, state.roamTarget, m_movementSpeed, state.baseState);
    }
  }
}

void GuardBehavior::updateRoamingGuard(BehaviorContext& ctx, EntityState &state) {
  // Generate new roam target if needed
  if (state.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, state.roamTarget)) {
    state.roamTarget = generateRoamTarget(state);
    state.roamTimer = m_roamInterval;
  }

  // Move to roam target
  moveToPositionDirect(ctx, state.roamTarget, m_movementSpeed, state.baseState);
}

void GuardBehavior::updateAlertGuard(BehaviorContext& ctx, EntityState &state) {
  // Alert guard moves faster and has heightened awareness
  if (state.currentAlertLevel >= AlertLevel::INVESTIGATING) {
    // Move towards last known threat position
    if (state.lastKnownThreatPosition.length() > 0) {
      moveToPositionDirect(ctx, state.lastKnownThreatPosition, m_alertSpeed, state.baseState, 2);
    }
  } else {
    // Patrol more aggressively
    updatePatrolGuard(ctx, state);
  }
}

// Lock-free version for BehaviorContext hot path
void GuardBehavior::moveToPositionDirect(BehaviorContext& ctx, const Vector2D &targetPos,
                                         float speed, AIBehaviorState &state,
                                         int priority) {
  if (speed <= 0.0f)
    return;

  // Use base class moveToPosition implementation (renamed from moveToPositionDirect)
  AIBehavior::moveToPosition(ctx, targetPos, speed, state, priority);
}

Vector2D GuardBehavior::getNextPatrolWaypoint(const EntityState &state) const {
  if (m_patrolWaypoints.empty()) {
    return Vector2D(0, 0);
  }

  return m_patrolWaypoints[state.currentPatrolIndex];
}

Vector2D
GuardBehavior::generateRoamTarget(const EntityState & /*state*/) const {
  Vector2D target;

  if (m_useCircularArea) {
    // Generate random point within circular area
    float angle = m_angleDistribution(m_rng);
    float radius = m_radiusDistribution(m_rng) * m_areaRadius;

    target = m_areaCenter +
             Vector2D(radius * std::cos(angle), radius * std::sin(angle));
  } else {
    // Generate random point within rectangular area
    std::uniform_real_distribution<float> xDist(m_areaTopLeft.getX(),
                                                m_areaBottomRight.getX());
    std::uniform_real_distribution<float> yDist(m_areaTopLeft.getY(),
                                                m_areaBottomRight.getY());

    target = Vector2D(xDist(m_rng), yDist(m_rng));
  }

  return target;
}

bool GuardBehavior::isAtPosition(const Vector2D &currentPos,
                                 const Vector2D &targetPos,
                                 float threshold) const {
  return (currentPos - targetPos).length() <= threshold;
}

bool GuardBehavior::isWithinGuardArea(const Vector2D &position) const {
  if (m_useCircularArea) {
    return (position - m_areaCenter).length() <= m_areaRadius;
  } else {
    return (position.getX() >= m_areaTopLeft.getX() &&
            position.getX() <= m_areaBottomRight.getX() &&
            position.getY() >= m_areaTopLeft.getY() &&
            position.getY() <= m_areaBottomRight.getY());
  }
}

// Utility methods removed - now using base class implementations

Vector2D GuardBehavior::clampToGuardArea(const Vector2D &position) const {
  if (m_useCircularArea) {
    Vector2D direction = position - m_areaCenter;
    float const distance = direction.length();

    if (distance > m_areaRadius) {
      direction = normalizeDirection(direction);
      return m_areaCenter + direction * m_areaRadius;
    }
    return position;
  } else {
    return Vector2D(std::clamp(position.getX(), m_areaTopLeft.getX(),
                               m_areaBottomRight.getX()),
                    std::clamp(position.getY(), m_areaTopLeft.getY(),
                               m_areaBottomRight.getY()));
  }
}

void GuardBehavior::callForHelp(const Vector2D & /*threatPosition*/) {
  // In a full implementation, this would communicate with nearby guards
  // For now, we'll use the AI Manager's message system
  std::string helpMessage = "guard_help_needed";
  AIManager::Instance().broadcastMessage(helpMessage, false);
}

[[maybe_unused]] void GuardBehavior::broadcastAlert(AlertLevel level,
                                   const Vector2D & /*alertPosition*/) {
  // Broadcast alert to other guards in the same group
  std::string alertMessage = std::format("guard_alert_{}", static_cast<int>(level));
  AIManager::Instance().broadcastMessage(alertMessage, false);
}
