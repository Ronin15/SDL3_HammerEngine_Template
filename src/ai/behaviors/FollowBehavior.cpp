/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/FollowBehavior.hpp"
#include "managers/AIManager.hpp"
#include <algorithm>
#include <cmath>

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
    state.lastTargetPosition = AIManager::Instance().getPlayerPosition();
    state.desiredPosition = entity->getPosition();
    state.isFollowing = true;
  }
  // Assign formation slot for escort formation
  if (m_followMode == FollowMode::ESCORT_FORMATION) {
    state.formationSlot = assignFormationSlot();
    state.formationOffset = calculateFormationOffset(state);
    state.inFormation = true;
  } else {
    state.formationOffset = m_formationOffset;
  }
}

void FollowBehavior::executeLogic(EntityPtr entity) {
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
    state.isFollowing = false;
    return;
  }

  Vector2D currentPos = entity->getPosition();
  Vector2D targetPos = AIManager::Instance().getPlayerPosition();
  float distanceToTarget = (currentPos - targetPos).length();

  // Update target movement tracking
  Uint64 currentTime = SDL_GetTicks();
  bool targetMoved = isTargetMoving(target, state);

  if (targetMoved) {
    state.lastTargetMoveTime = currentTime;
    state.targetMoving = true;
    state.stationaryStartTime = 0;
  } else if (state.targetMoving &&
             currentTime - state.lastTargetMoveTime > m_stationaryThreshold) {
    state.targetMoving = false;
    state.stationaryStartTime = currentTime;
  }

  state.lastTargetPosition = targetPos;

  // Check if we should follow
  if (m_stopWhenTargetStops && !state.targetMoving &&
      distanceToTarget <= m_followDistance) {
    // Target is stationary and we're close enough, stop following
    state.isFollowing = false;
    return;
  }

  // Start following if target moves or we're too far
  if (!state.isFollowing &&
      (state.targetMoving || distanceToTarget > m_followDistance)) {
    state.isFollowing = true;
  }

  if (state.isFollowing) {
    // Path-following to desired position if available
    auto tryFollowPath = [&](const Vector2D& desiredPos, float speed)->bool {
      Uint64 now = SDL_GetTicks();
      // Refresh if TTL expired or no progress towards node
      const Uint64 pathTTL = 1500; // ms
      const Uint64 noProgressWindow = 300; // ms
      bool needRefresh = state.pathPoints.empty() || state.currentPathIndex >= state.pathPoints.size();
      if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
        float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
        if (d + 1.0f < state.lastNodeDistance) { // progressed
          state.lastNodeDistance = d;
          state.lastProgressTime = now;
        } else if (state.lastProgressTime == 0) {
          state.lastProgressTime = now;
        } else if (now - state.lastProgressTime > noProgressWindow) {
          needRefresh = true;
        }
      }
      if (now - state.lastPathUpdate > pathTTL) needRefresh = true;
      if (needRefresh) {
        AIManager::Instance().requestPath(entity, currentPos, desiredPos);
        state.pathPoints = AIManager::Instance().getPath(entity);
        state.currentPathIndex = 0;
        state.lastPathUpdate = now;
        state.lastNodeDistance = std::numeric_limits<float>::infinity();
        state.lastProgressTime = now;
      }
      if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
        Vector2D node = state.pathPoints[state.currentPathIndex];
        Vector2D dir = node - currentPos;
        float len = dir.length();
        if (len > 0.01f) {
          dir = dir * (1.0f / len);
          entity->setVelocity(dir * speed);
        }
        float dist = (node - currentPos).length();
        if (dist <= 16.0f) {
          ++state.currentPathIndex;
          state.lastNodeDistance = std::numeric_limits<float>::infinity();
          state.lastProgressTime = now;
        }
        return true;
      }
      return false;
    };

    // Execute appropriate follow behavior based on mode
    switch (m_followMode) {
    case FollowMode::CLOSE_FOLLOW:
      if (!tryFollowPath(calculateDesiredPosition(entity, target, state), m_followSpeed))
        updateCloseFollow(entity, state);
      break;
    case FollowMode::LOOSE_FOLLOW:
      if (!tryFollowPath(calculateDesiredPosition(entity, target, state), m_followSpeed))
        updateLooseFollow(entity, state);
      break;
    case FollowMode::FLANKING_FOLLOW:
      if (!tryFollowPath(calculateDesiredPosition(entity, target, state), m_followSpeed))
        updateFlankingFollow(entity, state);
      break;
    case FollowMode::REAR_GUARD:
      if (!tryFollowPath(calculateDesiredPosition(entity, target, state), m_followSpeed))
        updateRearGuard(entity, state);
      break;
    case FollowMode::ESCORT_FORMATION:
      if (!tryFollowPath(calculateDesiredPosition(entity, target, state), m_followSpeed))
        updateEscortFormation(entity, state);
      break;
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
  return (it != m_entityStates.end())
             ? (it->first->getPosition() - target->getPosition()).length()
             : -1.0f;
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
  return clone;
}

EntityPtr FollowBehavior::getTarget() const {
  return AIManager::Instance().getPlayerReference();
}

Vector2D FollowBehavior::calculateDesiredPosition(EntityPtr entity,
                                                  EntityPtr target,
                                                  const EntityState &state) {
  if (!entity || !target)
    return Vector2D(0, 0);

  Vector2D targetPos = AIManager::Instance().getPlayerPosition();

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
  case FollowMode::LOOSE_FOLLOW:
    return Vector2D(0, 0);

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

  Vector2D currentPos = AIManager::Instance().getPlayerPosition();
  Vector2D velocity =
      (currentPos - state.lastTargetPosition) / 0.016f; // Assume 60 FPS

  return currentPos + velocity * m_predictionTime;
}

bool FollowBehavior::isTargetMoving(EntityPtr target,
                                    const EntityState &state) const {
  if (!target)
    return false;

  Vector2D currentPos = AIManager::Instance().getPlayerPosition();
  float movementDistance = (currentPos - state.lastTargetPosition).length();
  return movementDistance > m_minimumMovementThreshold;
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

[[maybe_unused]] Vector2D FollowBehavior::calculateSteeringForce(EntityPtr entity,
                                                const Vector2D &desiredPosition,
                                                const EntityState &state) {
  if (!entity)
    return Vector2D(0, 0);

  Vector2D currentPos = entity->getPosition();
  Vector2D desiredVelocity = (desiredPosition - currentPos);

  if (desiredVelocity.length() < 0.001f) {
    return Vector2D(0, 0);
  }

  desiredVelocity = normalizeVector(desiredVelocity);

  // Apply obstacle avoidance
  desiredVelocity = avoidObstacles(entity, desiredVelocity);

  // Calculate steering force
  Vector2D steeringForce = desiredVelocity - state.currentVelocity;

  return steeringForce;
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
  float distanceToDesired = (currentPos - desiredPos).length();

  if (distanceToDesired > m_followDistance * 0.3f) {
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
  float distanceToDesired = (currentPos - desiredPos).length();

  // Only move if outside the follow distance
  if (distanceToDesired > m_followDistance) {
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

[[maybe_unused]] float FollowBehavior::angleDifference(float angle1, float angle2) const {
  float diff = angle2 - angle1;
  while (diff > M_PI)
    diff -= 2.0f * M_PI;
  while (diff < -M_PI)
    diff += 2.0f * M_PI;
  return diff;
}

[[maybe_unused]] float FollowBehavior::clampAngle(float angle) const {
  while (angle > M_PI)
    angle -= 2.0f * M_PI;
  while (angle < -M_PI)
    angle += 2.0f * M_PI;
  return angle;
}

[[maybe_unused]] Vector2D FollowBehavior::rotateVector(const Vector2D &vector,
                                      float angle) const {
  float cos_a = std::cos(angle);
  float sin_a = std::sin(angle);

  return Vector2D(vector.getX() * cos_a - vector.getY() * sin_a,
                  vector.getX() * sin_a + vector.getY() * cos_a);
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
