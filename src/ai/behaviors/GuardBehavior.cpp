/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/GuardBehavior.hpp"
#include "managers/AIManager.hpp"
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

void GuardBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  auto &state = m_entityStates[entity];
  state = EntityState(); // Reset to default state
  state.assignedPosition = m_guardPosition;
  state.currentMode = m_guardMode;
  state.onDuty = true;

  // Initialize based on mode
  if (m_guardMode == GuardMode::PATROL_GUARD && !m_patrolWaypoints.empty()) {
    state.currentPatrolTarget = m_patrolWaypoints[0];
    state.currentPatrolIndex = 0;
  } else if (m_guardMode == GuardMode::ROAMING_GUARD) {
    state.roamTarget = generateRoamTarget(entity, state);
    state.nextRoamTime =
        SDL_GetTicks() + static_cast<Uint64>(m_roamInterval * 1000);
  }
}

void GuardBehavior::executeLogic(EntityPtr entity) {
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

  if (!state.onDuty) {
    return; // Guard is off duty
  }

  Uint64 currentTime = SDL_GetTicks();

  // Detect threats
  EntityPtr threat = detectThreat(entity, state);
  bool threatPresent = (threat != nullptr);

  // Update alert level based on threat presence
  updateAlertLevel(entity, state, threatPresent);

  if (threatPresent) {
    handleThreatDetection(entity, state, threat);
  } else if (state.isInvestigating) {
    handleInvestigation(entity, state);
  } else if (state.returningToPost) {
    handleReturnToPost(entity, state);
  } else {
    // Normal guard behavior based on mode
    switch (state.currentMode) {
    case GuardMode::STATIC_GUARD:
      updateStaticGuard(entity, state);
      break;
    case GuardMode::PATROL_GUARD:
      updatePatrolGuard(entity, state);
      break;
    case GuardMode::AREA_GUARD:
      updateAreaGuard(entity, state);
      break;
    case GuardMode::ROAMING_GUARD:
      updateRoamingGuard(entity, state);
      break;
    case GuardMode::ALERT_GUARD:
      updateAlertGuard(entity, state);
      break;
    }
  }

  // Handle alert decay
  if (state.currentAlertLevel > AlertLevel::CALM &&
      currentTime - state.lastAlertDecay >
          static_cast<Uint64>(m_alertDecayTime * 1000)) {

    // Reduce alert level by one step
    state.currentAlertLevel =
        static_cast<AlertLevel>(static_cast<int>(state.currentAlertLevel) - 1);
    state.lastAlertDecay = currentTime;
  }
}

void GuardBehavior::clean(EntityPtr entity) {
  if (entity) {
    m_entityStates.erase(entity);
  }
}

void GuardBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (!entity)
    return;

  auto it = m_entityStates.find(entity);
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
    state.alertStartTime = SDL_GetTicks();
  } else if (message == "clear_alert") {
    clearAlert(entity);
  } else if (message == "investigate_position") {
    state.isInvestigating = true;
    state.investigationTarget =
        entity->getPosition(); // Use current position as default
    state.investigationStartTime = SDL_GetTicks();
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
      pair.second.alertStartTime = SDL_GetTicks();
    }
  }
}

void GuardBehavior::raiseAlert(EntityPtr entity,
                               const Vector2D &alertPosition) {
  if (!entity)
    return;

  auto it = m_entityStates.find(entity);
  if (it != m_entityStates.end()) {
    EntityState &state = it->second;
    state.currentAlertLevel = AlertLevel::HOSTILE;
    state.alertStartTime = SDL_GetTicks();
    state.lastKnownThreatPosition = alertPosition;
    state.alertRaised = true;

    if (m_canCallForHelp && !state.helpCalled) {
      callForHelp(entity, alertPosition);
      state.helpCalled = true;
    }
  }
}

void GuardBehavior::clearAlert(EntityPtr entity) {
  if (!entity)
    return;

  auto it = m_entityStates.find(entity);
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
  auto it = std::find_if(m_entityStates.begin(), m_entityStates.end(),
                         [](const auto &pair) { return pair.second.onDuty; });
  return (it != m_entityStates.end())
             ? (it->first->getPosition() - it->second.assignedPosition).length()
             : 0.0f;
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
  return clone;
}

EntityPtr GuardBehavior::detectThreat(EntityPtr entity,
                                      const EntityState &state) const {
  if (!entity)
    return nullptr;

  EntityPtr threat = AIManager::Instance().getPlayerReference();
  if (!threat)
    return nullptr;

  // Check if threat is in detection range
  if (!isThreatInRange(entity, threat)) {
    return nullptr;
  }

  // Check field of view if required
  if (m_fieldOfView < 360.0f && !isThreatInFieldOfView(entity, threat, state)) {
    return nullptr;
  }

  // Check line of sight if required
  // Note: hasLineOfSight currently always returns true (simplified
  // implementation)
  if (m_lineOfSightRequired && !hasLineOfSight(entity, threat)) {
    return nullptr;
  }

  return threat;
}

bool GuardBehavior::isThreatInRange(EntityPtr entity, EntityPtr threat) const {
  if (!entity || !threat)
    return false;

  float distance = calculateThreatDistance(entity, threat);
  return distance <= m_threatDetectionRange;
}

bool GuardBehavior::isThreatInFieldOfView(EntityPtr entity, EntityPtr threat,
                                          const EntityState &state) const {
  if (!entity || !threat)
    return false;

  Vector2D entityPos = entity->getPosition();
  Vector2D threatPos = threat->getPosition();

  // Calculate angle to threat
  float angleToThreat = calculateAngleToTarget(entityPos, threatPos);

  // Calculate angular difference from current heading
  float angleDiff =
      std::abs(normalizeAngle(angleToThreat - state.currentHeading));

  // Check if within field of view
  float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
  return angleDiff <= halfFOV;
}

bool GuardBehavior::hasLineOfSight(EntityPtr /*entity*/,
                                   EntityPtr /*threat*/) const {
  // Simplified line of sight check
  // In a full implementation, this would do proper collision detection
  return true;
}

float GuardBehavior::calculateThreatDistance(EntityPtr entity,
                                             EntityPtr threat) const {
  if (!entity || !threat)
    return std::numeric_limits<float>::max();

  return (entity->getPosition() - threat->getPosition()).length();
}

void GuardBehavior::updateAlertLevel(EntityPtr /*entity*/, EntityState &state,
                                     bool threatPresent) {
  Uint64 currentTime = SDL_GetTicks();

  if (threatPresent) {
    state.lastThreatSighting = currentTime;
    state.hasActiveThreat = true;

    // Escalate alert level based on how long threat has been present
    Uint64 threatDuration = currentTime - state.alertStartTime;

    if (state.currentAlertLevel == AlertLevel::CALM) {
      state.currentAlertLevel = AlertLevel::SUSPICIOUS;
      state.alertStartTime = currentTime;
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
    if (currentTime - state.lastThreatSighting >
        static_cast<Uint64>(m_alertDecayTime * 1000 * 0.5f)) {
      state.lastAlertDecay = currentTime;
    }
  }
}

void GuardBehavior::handleThreatDetection(EntityPtr entity, EntityState &state,
                                          EntityPtr threat) {
  if (!entity || !threat)
    return;

  Vector2D threatPos = threat->getPosition();
  state.lastKnownThreatPosition = threatPos;

  // React based on alert level
  switch (state.currentAlertLevel) {
  case AlertLevel::SUSPICIOUS:
    // Turn towards threat, slow approach
    state.currentHeading =
        calculateAngleToTarget(entity->getPosition(), threatPos);
    break;

  case AlertLevel::INVESTIGATING:
    // Move towards threat for investigation
    state.isInvestigating = true;
    state.investigationTarget = threatPos;
    state.investigationStartTime = SDL_GetTicks();
    moveToPosition(entity, threatPos, m_movementSpeed);
    break;

  case AlertLevel::HOSTILE:
    // Engage threat or call for help
    if (m_canCallForHelp && !state.helpCalled) {
      callForHelp(entity, threatPos);
      state.helpCalled = true;
    }
    // Move towards threat at alert speed
    moveToPosition(entity, threatPos, m_alertSpeed);
    break;

  case AlertLevel::ALARM:
    // Maximum response - could switch to combat behavior
    moveToPosition(entity, threatPos, m_alertSpeed * 1.2f);
    break;

  default:
    break;
  }
}

void GuardBehavior::handleInvestigation(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  Uint64 currentTime = SDL_GetTicks();

  // Check if investigation time has expired
  if (currentTime - state.investigationStartTime >
      static_cast<Uint64>(m_investigationTime * 1000)) {
    state.isInvestigating = false;
    state.returningToPost = true;
    return;
  }

  // Move to investigation target
  Vector2D currentPos = entity->getPosition();
  if (!isAtPosition(currentPos, state.investigationTarget)) {
    moveToPosition(entity, state.investigationTarget, m_movementSpeed);
  }
}

void GuardBehavior::handleReturnToPost(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  Vector2D currentPos = entity->getPosition();

  // Return to assigned position
  if (!isAtPosition(currentPos, state.assignedPosition)) {
    moveToPosition(entity, state.assignedPosition, m_movementSpeed);
  } else {
    state.returningToPost = false;
    state.currentAlertLevel = AlertLevel::CALM;
  }
}

void GuardBehavior::updateStaticGuard(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  Vector2D currentPos = entity->getPosition();

  // Stay at assigned position
  if (!isAtPosition(currentPos, state.assignedPosition, 10.0f)) {
    moveToPosition(entity, state.assignedPosition, m_movementSpeed);
  }

  // Update heading to scan area
  Uint64 currentTime = SDL_GetTicks();
  if (currentTime - state.lastPositionCheck > 2000) { // Check every 2 seconds
    state.currentHeading += 0.5f;                     // Slow rotation
    state.currentHeading = normalizeAngle(state.currentHeading);
    state.lastPositionCheck = currentTime;
  }
}

void GuardBehavior::updatePatrolGuard(EntityPtr entity, EntityState &state) {
  if (!entity || m_patrolWaypoints.empty())
    return;

  Vector2D currentPos = entity->getPosition();

  // Move to current patrol waypoint
  if (isAtPosition(currentPos, state.currentPatrolTarget)) {
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
    moveToPosition(entity, state.currentPatrolTarget, m_movementSpeed);
  }
}

void GuardBehavior::updateAreaGuard(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  Vector2D currentPos = entity->getPosition();

  // Ensure we're within the guard area
  if (!isWithinGuardArea(currentPos)) {
    Vector2D clampedPos = clampToGuardArea(currentPos);
    moveToPosition(entity, clampedPos, m_movementSpeed);
  } else {
    // Patrol within the area
    Uint64 currentTime = SDL_GetTicks();
    if (currentTime >= state.nextRoamTime) {
      state.roamTarget = generateRoamTarget(entity, state);
      state.nextRoamTime =
          currentTime + static_cast<Uint64>(m_roamInterval * 1000);
    }

    if (!isAtPosition(currentPos, state.roamTarget)) {
      moveToPosition(entity, state.roamTarget, m_movementSpeed);
    }
  }
}

void GuardBehavior::updateRoamingGuard(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  Vector2D currentPos = entity->getPosition();
  Uint64 currentTime = SDL_GetTicks();

  // Generate new roam target if needed
  if (currentTime >= state.nextRoamTime ||
      isAtPosition(currentPos, state.roamTarget)) {
    state.roamTarget = generateRoamTarget(entity, state);
    state.nextRoamTime =
        currentTime + static_cast<Uint64>(m_roamInterval * 1000);
  }

  // Move to roam target
  moveToPosition(entity, state.roamTarget, m_movementSpeed);
}

void GuardBehavior::updateAlertGuard(EntityPtr entity, EntityState &state) {
  if (!entity)
    return;

  // Alert guard moves faster and has heightened awareness
  if (state.currentAlertLevel >= AlertLevel::INVESTIGATING) {
    // Move towards last known threat position
    if (state.lastKnownThreatPosition.length() > 0) {
      moveToPosition(entity, state.lastKnownThreatPosition, m_alertSpeed);
    }
  } else {
    // Patrol more aggressively
    updatePatrolGuard(entity, state);
  }
}

void GuardBehavior::moveToPosition(EntityPtr entity, const Vector2D &targetPos,
                                   float speed) {
  if (!entity || speed <= 0.0f)
    return;

  Vector2D currentPos = entity->getPosition();
  Vector2D direction = normalizeDirection(targetPos - currentPos);

  if (direction.length() > 0.001f) {
    Vector2D velocity = direction * speed;
    entity->setVelocity(velocity);
  }
}

Vector2D GuardBehavior::getNextPatrolWaypoint(const EntityState &state) const {
  if (m_patrolWaypoints.empty()) {
    return Vector2D(0, 0);
  }

  return m_patrolWaypoints[state.currentPatrolIndex];
}

Vector2D
GuardBehavior::generateRoamTarget(EntityPtr entity,
                                  const EntityState & /*state*/) const {
  if (!entity)
    return Vector2D(0, 0);

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

Vector2D GuardBehavior::normalizeDirection(const Vector2D &direction) const {
  float magnitude = direction.length();
  if (magnitude < 0.001f) {
    return Vector2D(0, 0);
  }
  return direction / magnitude;
}

float GuardBehavior::normalizeAngle(float angle) const {
  while (angle > M_PI)
    angle -= 2.0f * M_PI;
  while (angle < -M_PI)
    angle += 2.0f * M_PI;
  return angle;
}

float GuardBehavior::calculateAngleToTarget(const Vector2D &from,
                                            const Vector2D &to) const {
  Vector2D direction = to - from;
  return std::atan2(direction.getY(), direction.getX());
}

Vector2D GuardBehavior::clampToGuardArea(const Vector2D &position) const {
  if (m_useCircularArea) {
    Vector2D direction = position - m_areaCenter;
    float distance = direction.length();

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

void GuardBehavior::callForHelp(EntityPtr entity,
                                const Vector2D & /*threatPosition*/) {
  if (!entity)
    return;

  // In a full implementation, this would communicate with nearby guards
  // For now, we'll use the AI Manager's message system
  std::string helpMessage = "guard_help_needed";
  AIManager::Instance().broadcastMessage(helpMessage, false);
}

[[maybe_unused]] void GuardBehavior::broadcastAlert(EntityPtr entity, AlertLevel level,
                                   const Vector2D & /*alertPosition*/) {
  if (!entity)
    return;

  // Broadcast alert to other guards in the same group
  std::string alertMessage =
      "guard_alert_" + std::to_string(static_cast<int>(level));
  AIManager::Instance().broadcastMessage(alertMessage, false);
}