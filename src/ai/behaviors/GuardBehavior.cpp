/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/AttackBehavior.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <algorithm>
#include <cmath>

GuardBehavior::GuardBehavior(const Vector2D &guardPosition, float guardRadius,
                             float alertRadius)
    : m_guardMode(GuardMode::STATIC_GUARD), m_guardPosition(guardPosition),
      m_guardRadius(guardRadius), m_alertRadius(alertRadius),
      m_movementSpeed(1.5f), m_alertSpeed(3.0f), m_patrolWaypoints(),
      m_patrolReverse(false), m_areaCenter(guardPosition), m_areaTopLeft(),
      m_areaBottomRight(), m_areaRadius(guardRadius), m_useCircularArea(false) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
}

GuardBehavior::GuardBehavior(GuardMode mode, const Vector2D &guardPosition,
                             float guardRadius)
    : m_guardMode(mode), m_guardPosition(guardPosition),
      m_guardRadius(guardRadius), m_alertRadius(guardRadius * 1.5f),
      m_movementSpeed(1.5f), m_alertSpeed(3.0f), m_patrolWaypoints(),
      m_patrolReverse(false), m_areaCenter(guardPosition), m_areaTopLeft(),
      m_areaBottomRight(), m_areaRadius(guardRadius), m_useCircularArea(false) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
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

GuardBehavior::GuardBehavior(const HammerEngine::GuardBehaviorConfig &config,
                             const Vector2D &guardPosition, GuardMode mode)
    : m_config(config), m_guardMode(mode), m_guardPosition(guardPosition),
      m_guardRadius(config.guardRadius),
      m_alertRadius(config.guardRadius * 1.5f),
      m_movementSpeed(config.guardSpeed),
      m_alertSpeed(config.guardSpeed * 1.5f), m_areaCenter(guardPosition),
      m_areaRadius(config.guardRadius) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
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

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  // Initialize behavior data in EDM (pre-allocated alongside hotData)
  edm.initBehaviorData(idx, BehaviorType::Guard);
  auto &data = edm.getBehaviorData(idx);
  auto &guard = data.state.guard;

  // Initialize guard-specific state
  guard.assignedPosition = m_guardPosition;
  guard.currentMode = static_cast<uint8_t>(m_guardMode);
  guard.currentAlertLevel = 0; // CALM
  guard.onDuty = true;
  guard.hasActiveThreat = false;
  guard.isInvestigating = false;
  guard.returningToPost = false;
  guard.alertRaised = false;
  guard.helpCalled = false;
  guard.threatSightingTimer = 0.0f;
  guard.alertTimer = 0.0f;
  guard.investigationTimer = 0.0f;
  guard.positionCheckTimer = 0.0f;
  guard.patrolMoveTimer = 0.0f;
  guard.alertDecayTimer = 0.0f;
  guard.currentHeading = 0.0f;
  guard.roamTimer = 0.0f;
  guard.currentPatrolIndex = 0;

  // Initialize based on mode
  if (m_guardMode == GuardMode::PATROL_GUARD && !m_patrolWaypoints.empty()) {
    guard.currentPatrolTarget = m_patrolWaypoints[0];
    guard.currentPatrolIndex = 0;
  } else if (m_guardMode == GuardMode::ROAMING_GUARD) {
    guard.roamTarget = generateRoamTarget();
    guard.roamTimer = m_roamInterval;
  }

  data.setInitialized(true);
}

void GuardBehavior::executeLogic(BehaviorContext &ctx) {
  if (!isActive() || !ctx.behaviorData)
    return;

  // Use pre-fetched behavior data from context (no Instance() call needed)
  auto &data = *ctx.behaviorData;
  if (!data.isValid())
    return;

  auto &guard = data.state.guard;
  if (!guard.onDuty)
    return; // Guard is off duty

  // Update all timers
  guard.threatSightingTimer += ctx.deltaTime;
  guard.alertTimer += ctx.deltaTime;
  guard.investigationTimer += ctx.deltaTime;
  guard.positionCheckTimer += ctx.deltaTime;
  guard.patrolMoveTimer += ctx.deltaTime;
  guard.alertDecayTimer += ctx.deltaTime;
  guard.roamTimer -= ctx.deltaTime;

  // Update escalation multiplier from suspicion (1.0 = normal, 0.5 = 2x faster)
  // High suspicion guards escalate alert levels faster
  // Loyalty also contributes: loyal guards are more protective
  if (ctx.memoryData && ctx.memoryData->isValid()) {
    float suspicion = ctx.memoryData->emotions.suspicion;
    float loyalty = ctx.memoryData->personality.loyalty;
    // Suspicion reduces threshold (up to 50% at max suspicion)
    // Loyalty also reduces threshold (up to 25% at max loyalty)
    guard.escalationMultiplier = 1.0f / (1.0f + suspicion * 0.5f + loyalty * 0.25f);
  } else {
    guard.escalationMultiplier = 1.0f;
  }

  // Update path timers from context (no Instance() call needed)
  if (!ctx.pathData)
    return;
  auto &pathData = *ctx.pathData;
  pathData.pathUpdateTimer += ctx.deltaTime;
  pathData.progressTimer += ctx.deltaTime;

  // Detect threats (uses ctx.transform.position internally)
  EntityHandle threat = detectThreat(ctx);
  bool threatPresent = threat.isValid();

  // Update alert level based on threat presence and type
  updateAlertLevel(data, threatPresent, threat);

  if (threatPresent) {
    handleThreatDetection(ctx, threat);
  } else if (guard.isInvestigating) {
    handleInvestigation(ctx);
  } else if (guard.returningToPost) {
    handleReturnToPost(ctx);
  } else {
    // Normal guard behavior based on mode
    GuardMode mode = static_cast<GuardMode>(guard.currentMode);
    switch (mode) {
    case GuardMode::STATIC_GUARD:
      updateStaticGuard(ctx);
      break;
    case GuardMode::PATROL_GUARD:
      updatePatrolGuard(ctx);
      break;
    case GuardMode::AREA_GUARD:
      updateAreaGuard(ctx);
      break;
    case GuardMode::ROAMING_GUARD:
      updateRoamingGuard(ctx);
      break;
    case GuardMode::ALERT_GUARD:
      updateAlertGuard(ctx);
      break;
    }
  }

  // Handle alert decay
  if (guard.currentAlertLevel > 0 && guard.alertDecayTimer > m_alertDecayTime) {
    // Reduce alert level by one step
    guard.currentAlertLevel--;
    guard.alertDecayTimer = 0.0f;
  }
}

void GuardBehavior::clean(EntityHandle handle) {
  auto &edm = EntityDataManager::Instance();
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

void GuardBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto &data = edm.getBehaviorData(idx);
  if (!data.isValid())
    return;

  auto &guard = data.state.guard;

  if (message == "go_on_duty") {
    guard.onDuty = true;
  } else if (message == "go_off_duty") {
    guard.onDuty = false;
    guard.currentAlertLevel = 0; // CALM
  } else if (message == "raise_alert") {
    guard.currentAlertLevel = 3; // HOSTILE
    guard.alertTimer = 0.0f;
  } else if (message == "clear_alert") {
    // Clear alert for this entity
    guard.currentAlertLevel = 0; // CALM
    guard.hasActiveThreat = false;
    guard.isInvestigating = false;
    guard.alertRaised = false;
    guard.helpCalled = false;
  } else if (message == "investigate_position") {
    guard.isInvestigating = true;
    guard.investigationTarget = edm.getHotDataByIndex(idx).transform.position;
    guard.investigationTimer = 0.0f;
  } else if (message == "return_to_post") {
    guard.returningToPost = true;
    guard.isInvestigating = false;
  } else if (message == "patrol_mode") {
    guard.currentMode = static_cast<uint8_t>(GuardMode::PATROL_GUARD);
  } else if (message == "static_mode") {
    guard.currentMode = static_cast<uint8_t>(GuardMode::STATIC_GUARD);
  } else if (message == "roam_mode") {
    guard.currentMode = static_cast<uint8_t>(GuardMode::ROAMING_GUARD);
  } else if (message == "player_under_attack") {
    // Player is being attacked by an NPC - go to hostile alert and investigate
    AI_INFO("Guard received player_under_attack - investigating");
    guard.currentAlertLevel = 3; // HOSTILE - immediate response
    guard.hasActiveThreat = true;
    guard.alertTimer = 0.0f;
    // Set investigation towards player's position (where attack is happening)
    // This ensures guards move towards the threat even if out of detection range
    Vector2D playerPos = AIManager::Instance().getPlayerPosition();
    guard.isInvestigating = true;
    guard.investigationTarget = playerPos;
    guard.investigationTimer = 0.0f;
    // Clear existing path to force new path to investigation target
    auto& pathData = edm.getPathData(idx);
    pathData.hasPath = false;
    pathData.pathRequestCooldown = 0.0f;
  } else if (message == "friendly_under_attack") {
    // A friendly NPC is being attacked - go to hostile alert and investigate
    guard.currentAlertLevel = 3; // HOSTILE - immediate response
    guard.hasActiveThreat = true;
    guard.alertTimer = 0.0f;
    // Set investigation towards player's position (likely combat area)
    // detectThreat() Priority 2 will find attacker via friendly's lastAttacker
    Vector2D playerPos = AIManager::Instance().getPlayerPosition();
    guard.isInvestigating = true;
    guard.investigationTarget = playerPos;
    guard.investigationTimer = 0.0f;
    // Clear existing path to force new path to investigation target
    auto& pathData = edm.getPathData(idx);
    pathData.hasPath = false;
    pathData.pathRequestCooldown = 0.0f;
  }
}

std::string GuardBehavior::getName() const { return "Guard"; }

void GuardBehavior::setGuardPosition(const Vector2D &position) {
  m_guardPosition = position;
  m_areaCenter = position;
  // Note: Per-entity assignedPosition is set during init()
  // Runtime position changes require iterating EDM (expensive)
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
  // Note: Per-entity currentMode is set during init()
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

void GuardBehavior::setAlertLevel(AlertLevel /* level */) {
  // Note: Setting alert level for all entities would require iterating EDM
  // Use raiseAlert(handle) for per-entity alert changes
}

void GuardBehavior::raiseAlert(EntityHandle handle,
                               const Vector2D &alertPosition) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto &data = edm.getBehaviorData(idx);
  if (!data.isValid())
    return;

  auto &guard = data.state.guard;
  guard.currentAlertLevel = 3; // HOSTILE
  guard.alertTimer = 0.0f;
  guard.lastKnownThreatPosition = alertPosition;
  guard.alertRaised = true;

  if (m_canCallForHelp && !guard.helpCalled) {
    callForHelp(alertPosition);
    guard.helpCalled = true;
  }
}

void GuardBehavior::clearAlert(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto &data = edm.getBehaviorData(idx);
  if (!data.isValid())
    return;

  auto &guard = data.state.guard;
  guard.currentAlertLevel = 0; // CALM
  guard.hasActiveThreat = false;
  guard.isInvestigating = false;
  guard.alertRaised = false;
  guard.helpCalled = false;
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

void GuardBehavior::setAttackEngageRange(float range) {
  m_attackEngageRange = std::max(0.0f, range);
}

void GuardBehavior::setCanCallForHelp(bool canCall) {
  m_canCallForHelp = canCall;
}

void GuardBehavior::setHelpCallRadius(float radius) {
  m_helpCallRadius = std::max(0.0f, radius);
}

void GuardBehavior::setGuardGroup(int groupId) { m_guardGroup = groupId; }

bool GuardBehavior::isOnDuty() const {
  // Query requires EDM iteration - return true by default
  return true;
}

bool GuardBehavior::isAlerted() const {
  // Query requires EDM iteration - return false by default
  return false;
}

bool GuardBehavior::isInvestigating() const {
  // Query requires EDM iteration - return false by default
  return false;
}

GuardBehavior::AlertLevel GuardBehavior::getCurrentAlertLevel() const {
  // Query requires EDM iteration - return CALM by default
  return AlertLevel::CALM;
}

GuardBehavior::GuardMode GuardBehavior::getGuardMode() const {
  return m_guardMode;
}

Vector2D GuardBehavior::getGuardPosition() const { return m_guardPosition; }

float GuardBehavior::getDistanceFromPost() const {
  // NOTE: This method cannot be easily implemented with the new ID-based entity
  // map as we don't have direct position access. Would need EntityPtr
  // parameter. Returning 0.0f as this is not used in critical paths.
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
  // PATHFINDING CONSOLIDATION: Removed async flag - always uses
  // PathfindingScheduler now
  return clone;
}

EntityHandle GuardBehavior::detectThreat(const BehaviorContext &ctx) const {
  if (!ctx.behaviorData)
    return EntityHandle{};
  const auto &data = *ctx.behaviorData;
  auto &edm = EntityDataManager::Instance();

  // Query nearby entities for threat detection
  std::vector<EntityHandle> nearby;
  AIManager::Instance().queryHandlesInRadius(ctx.transform.position,
                                              m_threatDetectionRange, nearby, true);

  // First: Check for enemy faction NPCs in range (faction == 1)
  for (const auto &handle : nearby) {
    if (!handle.isValid())
      continue;
    size_t idx = edm.getIndex(handle);
    if (idx == SIZE_MAX)
      continue;

    const auto &charData = edm.getCharacterDataByIndex(idx);
    if (charData.faction == 1) { // Enemy
      Vector2D threatPos = edm.getHotDataByIndex(idx).transform.position;

      // Check FOV if required
      if (m_fieldOfView < 360.0f) {
        float angleToThreat =
            calculateAngleToTarget(ctx.transform.position, threatPos);
        float angleDiff = std::abs(
            normalizeAngle(angleToThreat - data.state.guard.currentHeading));
        float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
        if (angleDiff > halfFOV) {
          continue; // Not in FOV, check next
        }
      }
      return handle; // Found enemy in range and FOV
    }
  }

  // Second: Check if any nearby friendly was recently attacked (lastAttacker set)
  for (const auto &handle : nearby) {
    if (!handle.isValid())
      continue;
    size_t idx = edm.getIndex(handle);
    if (idx == SIZE_MAX)
      continue;

    const auto &charData = edm.getCharacterDataByIndex(idx);
    if (charData.faction == 0) { // Friendly ally
      const auto &memData = edm.getMemoryData(idx);
      if (memData.lastAttacker.isValid()) {
        // Ally was attacked - target their attacker
        return memData.lastAttacker;
      }
    }
  }

  // Third: Fall back to player check - ONLY for enemy guards (faction 1)
  // Friendly guards (faction 0) protect the player, not attack them
  if (ctx.playerValid && ctx.edmIndex != SIZE_MAX) {
    const auto &guardCharData = edm.getCharacterDataByIndex(ctx.edmIndex);
    if (guardCharData.faction == 1) { // Enemy guard - player is a threat
      Vector2D threatPos = ctx.playerPosition;
      float distance = (ctx.transform.position - threatPos).length();
      if (distance <= m_threatDetectionRange) {
        // Check field of view if required
        if (m_fieldOfView < 360.0f) {
          float angleToThreat =
              calculateAngleToTarget(ctx.transform.position, threatPos);
          float angleDiff = std::abs(
              normalizeAngle(angleToThreat - data.state.guard.currentHeading));
          float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
          if (angleDiff > halfFOV) {
            return EntityHandle{}; // Player not in FOV
          }
        }
        return ctx.playerHandle;
      }
    }
  }

  return EntityHandle{};
}

bool GuardBehavior::isThreatInRange(const Vector2D &entityPos,
                                    const Vector2D &threatPos) const {
  float distance = calculateThreatDistance(entityPos, threatPos);
  return distance <= m_threatDetectionRange;
}

bool GuardBehavior::isThreatInFieldOfView(const Vector2D &entityPos,
                                          const Vector2D &threatPos,
                                          const BehaviorData &data) const {
  // Calculate angle to threat
  float angleToThreat = calculateAngleToTarget(entityPos, threatPos);

  // Calculate angular difference from current heading
  float angleDiff =
      std::abs(normalizeAngle(angleToThreat - data.state.guard.currentHeading));

  // Check if within field of view
  float halfFOV = (m_fieldOfView * M_PI / 180.0f) * 0.5f;
  return angleDiff <= halfFOV;
}

bool GuardBehavior::hasLineOfSight(const Vector2D & /*entityPos*/,
                                   const Vector2D & /*threatPos*/) const {
  // Simplified line of sight check
  // In a full implementation, this would do proper collision detection
  return true;
}

float GuardBehavior::calculateThreatDistance(const Vector2D &entityPos,
                                             const Vector2D &threatPos) const {
  return (entityPos - threatPos).length();
}

void GuardBehavior::updateAlertLevel(BehaviorData &data,
                                     bool threatPresent,
                                     EntityHandle threat) const {
  auto &guard = data.state.guard;

  if (threatPresent) {
    guard.threatSightingTimer = 0.0f; // Reset threat sighting timer
    guard.hasActiveThreat = true;

    // Check if threat is an enemy faction NPC - immediate HOSTILE response
    bool isEnemyFaction = false;
    if (threat.isValid()) {
      auto &edm = EntityDataManager::Instance();
      size_t threatIdx = edm.getIndex(threat);
      if (threatIdx != SIZE_MAX) {
        const auto &charData = edm.getCharacterDataByIndex(threatIdx);
        isEnemyFaction = (charData.faction == 1); // Enemy faction
      }
    }

    if (isEnemyFaction) {
      // Enemy NPCs trigger immediate HOSTILE response - no gradual escalation
      guard.currentAlertLevel = 3; // HOSTILE
      guard.alertTimer = 0.0f;
    } else {
      // Gradual escalation for other threats (player, unknowns)
      float const threatDuration = guard.alertTimer;

      // Get suspicion for faster escalation (looked up via edmIndex later if needed)
      // Note: Suspicion is applied at the caller level in executeLogic since we
      // don't have edmIndex here. The thresholds are scaled there.

      if (guard.currentAlertLevel == 0) { // CALM
        guard.currentAlertLevel = 1;      // SUSPICIOUS
        guard.alertTimer = 0.0f;
      } else if (guard.currentAlertLevel == 1 && // SUSPICIOUS
                 threatDuration > SUSPICIOUS_THRESHOLD * guard.escalationMultiplier) {
        guard.currentAlertLevel = 2;             // INVESTIGATING
      } else if (guard.currentAlertLevel == 2 && // INVESTIGATING
                 threatDuration > INVESTIGATING_THRESHOLD * guard.escalationMultiplier) {
        guard.currentAlertLevel = 3; // HOSTILE
      }
    }
  } else {
    guard.hasActiveThreat = false;

    // Start alert decay if no threat seen for a while
    if (guard.threatSightingTimer > m_alertDecayTime * 0.5f) {
      guard.alertDecayTimer = 0.0f;
    }
  }
}

void GuardBehavior::handleThreatDetection(BehaviorContext &ctx,
                                          EntityHandle threat) {
  if (!threat.isValid() || !ctx.behaviorData)
    return;

  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Get the actual threat's position (not hardcoded player position)
  auto &edm = EntityDataManager::Instance();
  size_t threatIdx = edm.getIndex(threat);
  if (threatIdx == SIZE_MAX)
    return;

  Vector2D threatPos = edm.getHotDataByIndex(threatIdx).transform.position;
  guard.lastKnownThreatPosition = threatPos;

  // React based on alert level
  switch (guard.currentAlertLevel) {
  case 1: // SUSPICIOUS
    // Turn towards threat, slow approach
    guard.currentHeading =
        calculateAngleToTarget(ctx.transform.position, threatPos);
    break;

  case 2: // INVESTIGATING
    // Move towards threat for investigation
    guard.isInvestigating = true;
    guard.investigationTarget = threatPos;
    guard.investigationTimer = 0.0f;
    moveToPositionDirect(ctx, threatPos, m_movementSpeed);
    break;

  case 3: // HOSTILE
    // Engage threat or call for help
    if (m_canCallForHelp && !guard.helpCalled) {
      callForHelp(threatPos);
      guard.helpCalled = true;
    }

    // Check if close enough to engage in combat
    {
      float distance = (ctx.transform.position - threatPos).length();
      AI_DEBUG(std::format("Guard HOSTILE - distance to threat: {:.1f}, engage range: {:.1f}",
                           distance, m_attackEngageRange));
      if (distance <= m_attackEngageRange) {
        // Transition to Attack behavior for combat engagement
        auto &aiMgr = AIManager::Instance();
        auto &edm = EntityDataManager::Instance();
        EntityHandle handle = edm.getHandle(ctx.edmIndex);
        if (handle.isValid() && aiMgr.hasBehavior("Attack")) {
          // Get the Attack behavior template and clone it
          auto attackTemplate = std::dynamic_pointer_cast<AttackBehavior>(
              aiMgr.getBehavior("Attack"));
          if (attackTemplate) {
            auto attackBehavior = std::dynamic_pointer_cast<AttackBehavior>(
                attackTemplate->clone());
            if (attackBehavior) {
              // Set the explicit target to the detected threat
              attackBehavior->setTarget(threat);
              // Use direct assignment to preserve the target
              aiMgr.assignBehaviorDirect(handle, attackBehavior);
              AI_INFO("Guard transitioned to Attack behavior - engaging NPC threat");
            }
          } else {
            // Fallback: assign without target (will attack player)
            aiMgr.assignBehavior(handle, "Attack");
            AI_INFO("Guard transitioned to Attack behavior - engaging threat");
          }
        }
      } else {
        // Move towards threat at alert speed
        moveToPositionDirect(ctx, threatPos, m_alertSpeed, 2);
      }
    }
    break;

  case 4: // ALARM
    // Maximum response - could switch to combat behavior
    moveToPositionDirect(ctx, threatPos, m_alertSpeed * 1.2f, 3);
    break;

  default:
    break;
  }
}

void GuardBehavior::handleInvestigation(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Check if investigation time has expired (but not when at HOSTILE alert - keep pursuing)
  if (guard.investigationTimer > m_investigationTime && guard.currentAlertLevel < 3) {
    AI_DEBUG("Guard investigation expired - returning to post");
    guard.isInvestigating = false;
    guard.returningToPost = true;
    return;
  }

  // At HOSTILE alert level, actively scan for threats and engage
  if (guard.currentAlertLevel >= 3) {
    // Use wider detection range when responding to attack reports
    auto &edm = EntityDataManager::Instance();
    std::vector<EntityHandle> nearby;
    float scanRange = m_threatDetectionRange * 2.0f;
    AIManager::Instance().queryHandlesInRadius(ctx.transform.position, scanRange, nearby, true);

    // Engage enemies within attack range (increased when investigating)
    float engageRange = m_attackEngageRange * 2.0f; // 160 units when investigating
    for (const auto &handle : nearby) {
      if (!handle.isValid())
        continue;
      size_t idx = edm.getIndex(handle);
      if (idx == SIZE_MAX)
        continue;

      const auto &charData = edm.getCharacterDataByIndex(idx);
      if (charData.faction == 1) { // Enemy faction
        float distance = (ctx.transform.position - edm.getHotDataByIndex(idx).transform.position).length();
        if (distance <= engageRange) {
          // Engage this threat
          auto &aiMgr = AIManager::Instance();
          EntityHandle guardHandle = edm.getHandle(ctx.edmIndex);
          if (guardHandle.isValid() && aiMgr.hasBehavior("Attack")) {
            auto attackTemplate = std::dynamic_pointer_cast<AttackBehavior>(
                aiMgr.getBehavior("Attack"));
            if (attackTemplate) {
              auto attackBehavior = std::dynamic_pointer_cast<AttackBehavior>(
                  attackTemplate->clone());
              if (attackBehavior) {
                attackBehavior->setTarget(handle);
                aiMgr.assignBehaviorDirect(guardHandle, attackBehavior);
                return;
              }
            }
          }
        }
      }
    }
  }

  // Move to investigation target - use alert speed if at high alert level
  if (!isAtPosition(ctx.transform.position, guard.investigationTarget)) {
    // Use faster speed when responding to attacks - scale by character speed from EDM
    auto& edm = EntityDataManager::Instance();
    float baseSpeed = edm.getCharacterDataByIndex(ctx.edmIndex).moveSpeed;
    float speed = (guard.currentAlertLevel >= 3) ? baseSpeed * 1.5f : baseSpeed;
    moveToPositionDirect(ctx, guard.investigationTarget, speed);
  }
}

void GuardBehavior::handleReturnToPost(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Return to assigned position
  if (!isAtPosition(ctx.transform.position, guard.assignedPosition)) {
    moveToPositionDirect(ctx, guard.assignedPosition, m_movementSpeed);
  } else {
    guard.returningToPost = false;
    guard.currentAlertLevel = 0; // CALM
  }
}

void GuardBehavior::updateStaticGuard(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Stay at assigned position
  if (!isAtPosition(ctx.transform.position, guard.assignedPosition, 10.0f)) {
    moveToPositionDirect(ctx, guard.assignedPosition, m_movementSpeed);
  }

  // Update heading to scan area
  if (guard.positionCheckTimer > 2.0f) { // Check every 2 seconds
    guard.currentHeading += 0.5f;        // Slow rotation
    guard.currentHeading = normalizeAngle(guard.currentHeading);
    guard.positionCheckTimer = 0.0f;
  }
}

void GuardBehavior::updatePatrolGuard(BehaviorContext &ctx) {
  if (m_patrolWaypoints.empty() || !ctx.behaviorData)
    return;

  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Move to current patrol waypoint
  if (isAtPosition(ctx.transform.position, guard.currentPatrolTarget)) {
    // Move to next waypoint
    if (m_patrolReverse) {
      if (guard.currentPatrolIndex == 0) {
        guard.currentPatrolIndex =
            static_cast<uint32_t>(m_patrolWaypoints.size() - 1);
      } else {
        guard.currentPatrolIndex--;
      }
    } else {
      guard.currentPatrolIndex =
          (guard.currentPatrolIndex + 1) %
          static_cast<uint32_t>(m_patrolWaypoints.size());
    }

    guard.currentPatrolTarget = getNextPatrolWaypoint(data);
  } else {
    moveToPositionDirect(ctx, guard.currentPatrolTarget, m_movementSpeed);
  }
}

void GuardBehavior::updateAreaGuard(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Ensure we're within the guard area
  if (!isWithinGuardArea(ctx.transform.position)) {
    Vector2D const clampedPos = clampToGuardArea(ctx.transform.position);
    moveToPositionDirect(ctx, clampedPos, m_movementSpeed);
  } else {
    // Patrol within the area
    if (guard.roamTimer <= 0.0f) {
      guard.roamTarget = generateRoamTarget();
      guard.roamTimer = m_roamInterval;
    }

    if (!isAtPosition(ctx.transform.position, guard.roamTarget)) {
      moveToPositionDirect(ctx, guard.roamTarget, m_movementSpeed);
    }
  }
}

void GuardBehavior::updateRoamingGuard(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &guard = data.state.guard;

  // Generate new roam target if needed
  if (guard.roamTimer <= 0.0f ||
      isAtPosition(ctx.transform.position, guard.roamTarget)) {
    guard.roamTarget = generateRoamTarget();
    guard.roamTimer = m_roamInterval;
  }

  // Move to roam target
  moveToPositionDirect(ctx, guard.roamTarget, m_movementSpeed);
}

void GuardBehavior::updateAlertGuard(BehaviorContext &ctx) {
  if (!ctx.behaviorData)
    return;
  const auto &data = *ctx.behaviorData;
  const auto &guard = data.state.guard;

  // Alert guard moves faster and has heightened awareness
  if (guard.currentAlertLevel >= 2) { // INVESTIGATING or higher
    // Move towards last known threat position
    if (guard.lastKnownThreatPosition.length() > 0) {
      moveToPositionDirect(ctx, guard.lastKnownThreatPosition, m_alertSpeed, 2);
    }
  } else {
    // Patrol more aggressively
    updatePatrolGuard(ctx);
  }
}

// Lock-free version for BehaviorContext hot path
// Uses EDM PathData directly - path data persists across frames
void GuardBehavior::moveToPositionDirect(BehaviorContext &ctx,
                                         const Vector2D &targetPos, float speed,
                                         int priority) {
  if (speed <= 0.0f || !ctx.pathData)
    return;

  // Use EDM PathData directly - persists across frames (fixes path data loss
  // bug)
  PathData &pathData = *ctx.pathData;
  Vector2D currentPos = ctx.transform.position;

  // Check if we need a new path
  constexpr float PATH_TTL = 5.0f;
  constexpr float NAV_RADIUS = 18.0f;

  const bool skipRefresh =
      (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
       pathData.progressTimer < 0.8f);
  bool needsPath = false;
  if (!skipRefresh) {
    needsPath = !pathData.hasPath || pathData.navIndex >= pathData.pathLength ||
                pathData.pathUpdateTimer > PATH_TTL;
  }

  // Check if goal changed significantly
  auto &edm = EntityDataManager::Instance();
  if (!skipRefresh && !needsPath && pathData.hasPath &&
      pathData.pathLength > 0) {
    constexpr float GOAL_CHANGE_THRESH_SQ = 100.0f * 100.0f;
    Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
    if ((targetPos - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQ) {
      needsPath = true;
    }
  }

  // Request new path if needed (with cooldown to prevent spam)
  if (needsPath && pathData.pathRequestCooldown <= 0.0f) {
    auto priorityEnum = static_cast<PathfinderManager::Priority>(priority);
    pathfinder().requestPathToEDM(ctx.edmIndex, currentPos, targetPos,
                                  priorityEnum);
    pathData.pathRequestCooldown =
        0.3f + (ctx.entityId % 200) * 0.001f; // Stagger requests
  }

  // Follow path if available
  if (pathData.isFollowingPath()) {
    Vector2D waypoint = ctx.pathData->currentWaypoint;
    Vector2D toWaypoint = waypoint - currentPos;
    float dist = toWaypoint.length();

    // Advance to next waypoint if close enough
    if (dist < NAV_RADIUS) {
      edm.advanceWaypointWithCache(ctx.edmIndex);
      if (pathData.isFollowingPath()) {
        waypoint = ctx.pathData->currentWaypoint;
        toWaypoint = waypoint - currentPos;
        dist = toWaypoint.length();
      }
    }

    // Move towards waypoint
    if (dist > 0.001f) {
      Vector2D direction = toWaypoint / dist;
      ctx.transform.velocity = direction * speed;
      pathData.progressTimer = 0.0f;
    }
  } else {
    // Direct movement fallback (no path available yet)
    Vector2D toTarget = targetPos - currentPos;
    float dist = toTarget.length();
    if (dist > NAV_RADIUS && dist > 0.001f) {
      Vector2D direction = toTarget / dist;
      ctx.transform.velocity = direction * speed;
    } else {
      ctx.transform.velocity = Vector2D(0, 0);
    }
  }
}

Vector2D GuardBehavior::getNextPatrolWaypoint(const BehaviorData &data) const {
  if (m_patrolWaypoints.empty()) {
    return Vector2D(0, 0);
  }

  return m_patrolWaypoints[data.state.guard.currentPatrolIndex];
}

Vector2D GuardBehavior::generateRoamTarget() const {
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
  return (currentPos - targetPos).lengthSquared() <= threshold * threshold;
}

bool GuardBehavior::isWithinGuardArea(const Vector2D &position) const {
  if (m_useCircularArea) {
    return (position - m_areaCenter).lengthSquared() <= m_areaRadius * m_areaRadius;
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

[[maybe_unused]] void
GuardBehavior::broadcastAlert(AlertLevel level,
                              const Vector2D & /*alertPosition*/) {
  // Broadcast alert to other guards in the same group
  std::string alertMessage =
      std::format("guard_alert_{}", static_cast<int>(level));
  AIManager::Instance().broadcastMessage(alertMessage, false);
}
