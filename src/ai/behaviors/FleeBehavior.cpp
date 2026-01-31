/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp" // For TransformData
#include "managers/PathfinderManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cmath>

FleeBehavior::FleeBehavior(float fleeSpeed, float detectionRange,
                           float safeDistance)
    : m_fleeSpeed(fleeSpeed), m_detectionRange(detectionRange),
      m_safeDistance(safeDistance) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
}

FleeBehavior::FleeBehavior(FleeMode mode, float fleeSpeed, float detectionRange)
    : m_fleeMode(mode), m_fleeSpeed(fleeSpeed),
      m_detectionRange(detectionRange) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
  // Adjust parameters based on mode
  switch (mode) {
  case FleeMode::PANIC_FLEE:
    m_fleeSpeed = fleeSpeed * 1.2f; // Faster in panic
    m_panicDuration = 2000.0f;      // 2 seconds of panic
    break;
  case FleeMode::STRATEGIC_RETREAT:
    m_fleeSpeed = fleeSpeed * 0.8f; // Slower, more calculated
    m_safeDistance = detectionRange * 1.8f;
    break;
  case FleeMode::EVASIVE_MANEUVER:
    m_zigzagInterval = 300; // More frequent direction changes
    break;
  case FleeMode::SEEK_COVER:
    m_safeDistance = detectionRange * 1.5f;
    break;
  }
}

FleeBehavior::FleeBehavior(const HammerEngine::FleeBehaviorConfig &config,
                           FleeMode mode)
    : m_config(config), m_fleeMode(mode), m_fleeSpeed(config.fleeSpeed),
      m_detectionRange(
          config.safeDistance) // Use safeDistance as detection trigger
      ,
      m_safeDistance(config.safeDistance),
      m_boundaryPadding(config.worldPadding) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
  // Mode-specific adjustments using config values
  switch (mode) {
  case FleeMode::PANIC_FLEE:
    m_fleeSpeed = config.fleeSpeed * 1.2f; // Faster in panic
    m_panicDuration = 2.0f;                // 2 seconds of panic
    break;
  case FleeMode::STRATEGIC_RETREAT:
    m_fleeSpeed = config.fleeSpeed * 0.8f; // Slower, more calculated
    m_safeDistance = config.safeDistance * 1.5f;
    break;
  case FleeMode::EVASIVE_MANEUVER:
    m_zigzagInterval = 0.3f; // More frequent direction changes
    break;
  case FleeMode::SEEK_COVER:
    m_safeDistance = config.safeDistance * 1.2f;
    break;
  }
}

void FleeBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  // Initialize behavior data in EDM (pre-allocated alongside hotData)
  edm.initBehaviorData(idx, BehaviorType::Flee);
  auto &data = edm.getBehaviorData(idx);
  auto &flee = data.state.flee;
  auto &hotData = edm.getHotDataByIndex(idx);

  // Initialize flee state
  flee.lastThreatPosition = hotData.transform.position;
  flee.fleeDirection = Vector2D(0, 0);
  flee.lastKnownSafeDirection = Vector2D(0, 0);
  flee.fleeTimer = 0.0f;
  flee.directionChangeTimer = 0.0f;
  flee.panicTimer = 0.0f;
  flee.currentStamina = m_maxStamina;
  flee.zigzagTimer = 0.0f;
  flee.navRadius = 18.0f;
  flee.backoffTimer = 0.0f;
  flee.zigzagDirection = 1;
  flee.isFleeing = false;
  flee.isInPanic = false;
  flee.hasValidThreat = false;

  data.setInitialized(true);
}

void FleeBehavior::executeLogic(BehaviorContext &ctx) {
  if (!isActive())
    return;

  // Use pre-fetched BehaviorData and PathData from context (no Instance() calls
  // needed)
  if (!ctx.behaviorData || !ctx.behaviorData->isValid()) {
    return;
  }
  if (!ctx.pathData) {
    return;
  }
  auto &data = *ctx.behaviorData;
  auto &flee = data.state.flee;
  auto &pathData = *ctx.pathData;

  // Update all timers
  flee.fleeTimer += ctx.deltaTime;
  flee.directionChangeTimer += ctx.deltaTime;
  if (flee.panicTimer > 0.0f)
    flee.panicTimer -= ctx.deltaTime;
  flee.zigzagTimer += ctx.deltaTime;
  if (flee.backoffTimer > 0.0f)
    flee.backoffTimer -= ctx.deltaTime;
  data.lastCrowdAnalysis += ctx.deltaTime;

  // Cache fear from emotions for speed modifier
  // Cowardly NPCs (low bravery) get a bonus to fear effect
  if (ctx.memoryData && ctx.memoryData->isValid()) {
    float braveryFactor = ctx.memoryData->personality.bravery;
    float baseFear = ctx.memoryData->emotions.fear;
    // Low bravery amplifies fear effect (up to +50% for completely cowardly)
    flee.fearBoost = baseFear * (1.5f - braveryFactor);
  } else {
    flee.fearBoost = 0.0f;
  }

  // Update EDM path timers (single source of truth for path state)
  pathData.pathUpdateTimer += ctx.deltaTime;
  pathData.progressTimer += ctx.deltaTime;
  if (pathData.pathRequestCooldown > 0.0f)
    pathData.pathRequestCooldown -= ctx.deltaTime;

  // Determine threat: use lastAttacker if valid (no player fallback)
  Vector2D threatPos;
  bool threatValid = false;

  if (ctx.memoryData && ctx.memoryData->lastAttacker.isValid()) {
    auto &edm = EntityDataManager::Instance();
    size_t attackerIdx = edm.getIndex(ctx.memoryData->lastAttacker);
    if (attackerIdx != SIZE_MAX && edm.getHotDataByIndex(attackerIdx).isAlive()) {
      threatPos = edm.getHotDataByIndex(attackerIdx).transform.position;
      threatValid = true;
    }
  }

  if (!threatValid) {
    // No valid attacker - stop fleeing (don't default to player)
    if (flee.isFleeing) {
      flee.isFleeing = false;
      flee.isInPanic = false;
      flee.hasValidThreat = false;
    }

    if (m_useStamina) {
      updateStamina(data, ctx.deltaTime, false);
    }
    return;
  }

  // threatPos is now set from lastAttacker
  float distanceToThreatSquared =
      (ctx.transform.position - threatPos).lengthSquared();
  float const detectionRangeSquared = m_detectionRange * m_detectionRange;
  bool threatInRange = (distanceToThreatSquared <= detectionRangeSquared);

  if (threatInRange) {
    // Start fleeing if not already
    if (!flee.isFleeing) {
      flee.isFleeing = true;
      flee.fleeTimer = 0.0f;
      flee.lastThreatPosition = threatPos;

      // Determine if this should trigger panic
      if (m_fleeMode == FleeMode::PANIC_FLEE) {
        flee.isInPanic = true;
        flee.panicTimer = m_panicDuration * m_panicVariation(m_rng);
      }
    }

    flee.hasValidThreat = true;
    flee.lastThreatPosition = threatPos;
  } else if (flee.isFleeing) {
    // PERFORMANCE: Use squared distance for comparison
    float const safeDistanceSquared = m_safeDistance * m_safeDistance;
    if (distanceToThreatSquared >= safeDistanceSquared) {
      flee.isFleeing = false;
      flee.isInPanic = false;
      flee.hasValidThreat = false;
    }
  }

  // Update panic state
  if (flee.isInPanic && flee.panicTimer <= 0.0f) {
    flee.isInPanic = false;
  }

  // Execute appropriate flee behavior
  if (flee.isFleeing) {
    switch (m_fleeMode) {
    case FleeMode::PANIC_FLEE:
      updatePanicFlee(ctx, threatPos);
      break;
    case FleeMode::STRATEGIC_RETREAT:
      updateStrategicRetreat(ctx, threatPos);
      break;
    case FleeMode::EVASIVE_MANEUVER:
      updateEvasiveManeuver(ctx, threatPos);
      break;
    case FleeMode::SEEK_COVER:
      updateSeekCover(ctx, threatPos);
      break;
    }

    if (m_useStamina) {
      updateStamina(data, ctx.deltaTime, true);
    }
  }
}

void FleeBehavior::clean(EntityHandle handle) {
  auto &edm = EntityDataManager::Instance();
  if (handle.isValid()) {
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.clearBehaviorData(idx);
      edm.clearPathData(idx);
    }
  }
  // Note: Bulk cleanup handled by EDM::prepareForStateTransition()
}

void FleeBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto &data = edm.getBehaviorData(idx);
  if (!data.isValid())
    return;
  auto &flee = data.state.flee;

  if (message == "panic") {
    flee.isInPanic = true;
    flee.panicTimer = m_panicDuration;
  } else if (message == "calm_down") {
    flee.isInPanic = false;
  } else if (message == "stop_fleeing") {
    flee.isFleeing = false;
    flee.isInPanic = false;
    flee.hasValidThreat = false;
  } else if (message == "recover_stamina") {
    flee.currentStamina = m_maxStamina;
  }
}

std::string FleeBehavior::getName() const { return "Flee"; }

void FleeBehavior::setFleeSpeed(float speed) {
  m_fleeSpeed = std::max(0.1f, speed);
}

void FleeBehavior::setDetectionRange(float range) {
  m_detectionRange = std::max(0.0f, range);
}

void FleeBehavior::setSafeDistance(float distance) {
  m_safeDistance = std::max(0.0f, distance);
}

void FleeBehavior::setFleeMode(FleeMode mode) { m_fleeMode = mode; }

void FleeBehavior::setPanicDuration(float duration) {
  m_panicDuration = std::max(0.0f, duration);
}

void FleeBehavior::setStaminaSystem(bool enabled, float maxStamina,
                                    float staminaDrain) {
  m_useStamina = enabled;
  m_maxStamina = std::max(1.0f, maxStamina);
  m_staminaDrain = std::max(0.0f, staminaDrain);
}

void FleeBehavior::addSafeZone(const Vector2D &center, float radius) {
  m_safeZones.emplace_back(center, radius);
}

void FleeBehavior::clearSafeZones() { m_safeZones.clear(); }

bool FleeBehavior::isFleeing() const {
  // Note: Would require EDM iteration to check all entities - return false as
  // default
  return false;
}

bool FleeBehavior::isInPanic() const {
  // Note: Would require EDM iteration to check all entities - return false as
  // default
  return false;
}

float FleeBehavior::getDistanceToThreat() const {
  // This method would require knowing which entity to check
  // Returns -1 as caller should use per-entity distance in executeLogic
  return -1.0f;
}

FleeBehavior::FleeMode FleeBehavior::getFleeMode() const { return m_fleeMode; }

std::shared_ptr<AIBehavior> FleeBehavior::clone() const {
  auto clone =
      std::make_shared<FleeBehavior>(m_fleeMode, m_fleeSpeed, m_detectionRange);
  clone->m_safeDistance = m_safeDistance;
  clone->m_panicDuration = m_panicDuration;
  clone->m_useStamina = m_useStamina;
  clone->m_maxStamina = m_maxStamina;
  clone->m_staminaDrain = m_staminaDrain;
  clone->m_safeZones = m_safeZones;
  return clone;
}

EntityHandle FleeBehavior::getThreatHandle() const {
  return AIManager::Instance().getPlayerHandle();
}

Vector2D FleeBehavior::getThreatPosition() const {
  return AIManager::Instance().getPlayerPosition();
}

bool FleeBehavior::isThreatInRange(const Vector2D &entityPos,
                                   const Vector2D &threatPos) const {
  // PERFORMANCE: Use squared distance
  float distanceSquared = (entityPos - threatPos).lengthSquared();
  float const detectionRangeSquared = m_detectionRange * m_detectionRange;
  return distanceSquared <= detectionRangeSquared;
}

Vector2D FleeBehavior::calculateFleeDirection(const Vector2D &entityPos,
                                              const Vector2D &threatPos,
                                              const BehaviorData &data) const {
  const auto &flee = data.state.flee;

  // Basic flee direction (away from threat)
  Vector2D fleeDir = entityPos - threatPos;

  if (fleeDir.length() < 0.001f) {
    // If too close, use last known direction or random
    if (flee.fleeDirection.length() > 0.001f) {
      fleeDir = flee.fleeDirection;
    } else {
      // Random direction
      float angle = m_angleVariation(m_rng) * 2.0f * M_PI;
      fleeDir = Vector2D(std::cos(angle), std::sin(angle));
    }
  }

  // Avoid boundaries
  fleeDir = avoidBoundaries(entityPos, fleeDir);

  return normalizeVector(fleeDir);
}

Vector2D FleeBehavior::findNearestSafeZone(const Vector2D &position) const {
  if (m_safeZones.empty())
    return Vector2D(0, 0);

  const SafeZone *nearest = nullptr;
  float minDistanceSquared = std::numeric_limits<float>::max();

  for (const auto &zone : m_safeZones) {
    // PERFORMANCE: Use squared distance throughout - avoid sqrt in loop
    float distanceSquared = (position - zone.center).lengthSquared();
    if (distanceSquared < minDistanceSquared) {
      minDistanceSquared = distanceSquared;
      nearest = &zone;
    }
  }

  return nearest ? (nearest->center - position) : Vector2D(0, 0);
}

[[maybe_unused]] bool
FleeBehavior::isPositionSafe(const Vector2D &position) const {
  // Phase 2 EDM Migration: Use handle-based lookup
  EntityHandle threatHandle = getThreatHandle();
  if (!threatHandle.isValid())
    return true;

  auto &edm = EntityDataManager::Instance();
  size_t threatIdx = edm.getIndex(threatHandle);
  if (threatIdx == SIZE_MAX)
    return true;

  Vector2D threatPos = edm.getHotDataByIndex(threatIdx).transform.position;

  // PERFORMANCE: Use squared distance
  float distanceToThreatSquared = (position - threatPos).lengthSquared();
  float const safeDistanceSquared = m_safeDistance * m_safeDistance;
  return distanceToThreatSquared >= safeDistanceSquared;
}

[[maybe_unused]] bool
FleeBehavior::isNearBoundary(const Vector2D &position) const {
  // Use world bounds for boundary detection
  float minX, minY, maxX, maxY;
  if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    // No world loaded - can't determine boundaries
    return false;
  }

  constexpr float TILE = HammerEngine::TILE_SIZE;
  float const worldMinX = minX * TILE + m_boundaryPadding;
  float const worldMinY = minY * TILE + m_boundaryPadding;
  float const worldMaxX = maxX * TILE - m_boundaryPadding;
  float const worldMaxY = maxY * TILE - m_boundaryPadding;

  return (position.getX() < worldMinX || position.getX() > worldMaxX ||
          position.getY() < worldMinY || position.getY() > worldMaxY);
}

Vector2D FleeBehavior::avoidBoundaries(const Vector2D &position,
                                       const Vector2D &direction) const {
  // Use world bounds for boundary avoidance with world-scale padding
  float minX, minY, maxX, maxY;
  if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    // No world loaded - return direction unchanged
    return direction;
  }

  Vector2D adjustedDir = direction;
  constexpr float TILE = HammerEngine::TILE_SIZE;
  const float worldPadding =
      80.0f; // Increased padding for world-scale movement
  float const worldMinX = minX * TILE + worldPadding;
  float const worldMinY = minY * TILE + worldPadding;
  float const worldMaxX = maxX * TILE - worldPadding;
  float const worldMaxY = maxY * TILE - worldPadding;

  // Check world boundaries and adjust direction
  if (position.getX() < worldMinX && direction.getX() < 0) {
    adjustedDir.setX(std::abs(direction.getX())); // Force rightward
  } else if (position.getX() > worldMaxX && direction.getX() > 0) {
    adjustedDir.setX(-std::abs(direction.getX())); // Force leftward
  }

  if (position.getY() < worldMinY && direction.getY() < 0) {
    adjustedDir.setY(std::abs(direction.getY())); // Force downward
  } else if (position.getY() > worldMaxY && direction.getY() > 0) {
    adjustedDir.setY(-std::abs(direction.getY())); // Force upward
  }

  return adjustedDir;
}

void FleeBehavior::updatePanicFlee(BehaviorContext &ctx,
                                   const Vector2D &threatPos) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &flee = data.state.flee;
  Vector2D currentPos = ctx.transform.position;

  // In panic mode, change direction more frequently and use longer distances
  if (flee.directionChangeTimer > 0.2f ||
      flee.fleeDirection.length() < 0.001f) {
    flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data);

    // Add more randomness to panic movement for world-scale escape
    float randomAngle = m_angleVariation(m_rng) * 0.8f; // Increased randomness
    float cos_a = std::cos(randomAngle);
    float sin_a = std::sin(randomAngle);

    Vector2D const rotated(
        flee.fleeDirection.getX() * cos_a - flee.fleeDirection.getY() * sin_a,
        flee.fleeDirection.getX() * sin_a + flee.fleeDirection.getY() * cos_a);

    flee.fleeDirection = rotated;
    flee.directionChangeTimer = 0.0f;

    // In panic, try to flee much further to break out of small areas
    Vector2D panicDest =
        currentPos +
        flee.fleeDirection * 1000.0f; // Much longer panic flee distance

    // Clamp to world bounds with larger margin for panic mode
    panicDest = pathfinder().clampToWorldBounds(panicDest, 100.0f);
  }

  float const speedModifier = calculateFleeSpeedModifier(data);
  Vector2D const intended = flee.fleeDirection * m_fleeSpeed * speedModifier;
  // Set velocity directly - CollisionManager handles overlap resolution
  ctx.transform.velocity = intended;
}

void FleeBehavior::updateStrategicRetreat(BehaviorContext &ctx,
                                          const Vector2D &threatPos) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &flee = data.state.flee;
  Vector2D currentPos = ctx.transform.position;

  // Strategic retreat: aim for a point away from threat (or toward nearest safe
  // zone)
  if (flee.directionChangeTimer > 1.0f ||
      flee.fleeDirection.length() < 0.001f) {
    flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data);

    // Blend with nearest safe zone direction if exists
    Vector2D const safeZoneDirection = findNearestSafeZone(currentPos);
    if (safeZoneDirection.length() > 0.001f) {
      Vector2D const blended = (flee.fleeDirection * 0.6f +
                                normalizeVector(safeZoneDirection) * 0.4f);
      flee.fleeDirection = normalizeVector(blended);
    }
    flee.directionChangeTimer = 0.0f;
  }

  // Dynamic retreat distance based on local entity density
  float const baseRetreatDistance = 800.0f; // Increased base for world scale
  int nearbyCount = 0;

  // Check for other fleeing entities to avoid clustering in same escape routes
  nearbyCount =
      AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 100.0f);

  float retreatDistance = baseRetreatDistance;
  if (nearbyCount > 2) {
    // High density: encourage longer retreat to spread fleeing entities
    retreatDistance = baseRetreatDistance * 1.8f; // Up to 1440px retreat

    // Also add lateral bias to prevent all entities fleeing in same direction
    Vector2D const lateral(-flee.fleeDirection.getY(),
                           flee.fleeDirection.getX());
    float lateralBias =
        ((float)(ctx.entityId % 5) - 2.0f) * 0.3f; // -0.6 to +0.6
    flee.fleeDirection =
        (flee.fleeDirection + lateral * lateralBias).normalized();
  } else if (nearbyCount > 0) {
    // Medium density: moderate expansion
    retreatDistance = baseRetreatDistance * 1.3f;
  }

  // Compute a retreat destination further ahead and clamp to world bounds
  Vector2D dest = pathfinder().clampToWorldBounds(
      currentPos + flee.fleeDirection * retreatDistance, 100.0f);

  // OPTIMIZATION: Use extracted method instead of lambda for better compiler
  // optimization
  float const speedModifier = calculateFleeSpeedModifier(data);
  if (!tryFollowPathToGoal(ctx, dest, m_fleeSpeed * speedModifier)) {
    // Fallback to direct flee when no path available
    Vector2D const intended2 = flee.fleeDirection * m_fleeSpeed * speedModifier;
    // Set velocity directly - CollisionManager handles overlap resolution
    ctx.transform.velocity = intended2;
  }
}

void FleeBehavior::updateEvasiveManeuver(BehaviorContext &ctx,
                                         const Vector2D &threatPos) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &flee = data.state.flee;
  Vector2D currentPos = ctx.transform.position;

  // Zigzag pattern
  if (flee.zigzagTimer > m_zigzagInterval) {
    flee.zigzagDirection *= -1; // Flip direction
    flee.zigzagTimer = 0.0f;
  }

  // Base flee direction
  Vector2D baseFleeDir = calculateFleeDirection(currentPos, threatPos, data);

  // Apply zigzag
  float zigzagAngleRad = (m_zigzagAngle * M_PI / 180.0f) * flee.zigzagDirection;
  float cos_z = std::cos(zigzagAngleRad);
  float sin_z = std::sin(zigzagAngleRad);

  Vector2D const zigzagDir(
      baseFleeDir.getX() * cos_z - baseFleeDir.getY() * sin_z,
      baseFleeDir.getX() * sin_z + baseFleeDir.getY() * cos_z);

  flee.fleeDirection = normalizeVector(zigzagDir);

  float const speedModifier = calculateFleeSpeedModifier(data);
  Vector2D const intended3 = flee.fleeDirection * m_fleeSpeed * speedModifier;
  // Set velocity directly - CollisionManager handles overlap resolution
  ctx.transform.velocity = intended3;
}

void FleeBehavior::updateSeekCover(BehaviorContext &ctx,
                                   const Vector2D &threatPos) {
  if (!ctx.behaviorData)
    return;
  auto &data = *ctx.behaviorData;
  auto &flee = data.state.flee;
  // Move toward nearest safe zone using pathfinding when possible
  Vector2D currentPos = ctx.transform.position;
  Vector2D const safeZoneDirection = findNearestSafeZone(currentPos);

  // Dynamic cover seeking distance based on entity density
  float const baseCoverDistance = 720.0f; // Increased base for world scale
  int nearbyCount = 0;

  // Check for clustering of other cover-seekers
  nearbyCount =
      AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 90.0f);

  float coverDistance = baseCoverDistance;
  if (nearbyCount > 2) {
    // High density: seek cover further away to spread entities
    coverDistance = baseCoverDistance * 1.6f; // Up to 1152px
  } else if (nearbyCount > 0) {
    // Medium density: moderate expansion
    coverDistance = baseCoverDistance * 1.2f;
  }

  Vector2D dest;
  if (safeZoneDirection.length() > 0.001f) {
    flee.fleeDirection = normalizeVector(safeZoneDirection);
    dest = currentPos + flee.fleeDirection * coverDistance;
  } else {
    // No safe zones, move away from threat with larger distance
    flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data);
    dest = currentPos + flee.fleeDirection * coverDistance;
  }

  // Clamp destination within world bounds
  dest = pathfinder().clampToWorldBounds(dest, 100.0f);

  // OPTIMIZATION: Use extracted method instead of lambda for better compiler
  // optimization
  float const speedModifier = calculateFleeSpeedModifier(data);
  if (!tryFollowPathToGoal(ctx, dest, m_fleeSpeed * speedModifier)) {
    // Fallback to straight-line movement
    Vector2D const intended4 = flee.fleeDirection * m_fleeSpeed * speedModifier;
    // Set velocity directly - CollisionManager handles overlap resolution
    ctx.transform.velocity = intended4;
  }
}

void FleeBehavior::updateStamina(BehaviorData &data, float deltaTime,
                                 bool fleeing) {
  auto &flee = data.state.flee;
  if (fleeing) {
    flee.currentStamina -= m_staminaDrain * deltaTime;
    flee.currentStamina = std::max(0.0f, flee.currentStamina);
  } else {
    flee.currentStamina += m_staminaRecovery * deltaTime;
    flee.currentStamina = std::min(m_maxStamina, flee.currentStamina);
  }
}

Vector2D FleeBehavior::normalizeVector(const Vector2D &direction) const {
  float const magnitude = direction.length();
  if (magnitude < 0.001f) {
    return Vector2D(1, 0); // Default direction
  }
  return direction / magnitude;
}

float FleeBehavior::calculateFleeSpeedModifier(const BehaviorData &data) const {
  const auto &flee = data.state.flee;
  float modifier = 1.0f;

  // Panic increases speed
  if (flee.isInPanic) {
    modifier *= 1.3f;
  }

  // Fear boost from emotions (cached in flee state during executeLogic)
  // Up to 40% faster when terrified
  modifier *= (1.0f + flee.fearBoost * 0.4f);

  // Stamina affects speed
  if (m_useStamina) {
    float const staminaRatio = flee.currentStamina / m_maxStamina;
    modifier *= (0.3f + 0.7f * staminaRatio); // Speed ranges from 30% to 100%
  }

  return modifier;
}

// OPTIMIZATION: Uses EDM path state for per-entity isolation and no
// shared_from_this() overhead This method handles path-following logic with TTL
// and no-progress checks
bool FleeBehavior::tryFollowPathToGoal(BehaviorContext &ctx,
                                       const Vector2D &goal, float speed) {
  if (!ctx.behaviorData)
    return false;
  const auto &data = *ctx.behaviorData;
  const auto &flee = data.state.flee;
  // PERFORMANCE: Increase TTL to reduce pathfinding frequency
  constexpr float pathTTL = 3.5f;
  constexpr float noProgressWindow = 0.4f;
  constexpr float GOAL_CHANGE_THRESH_SQUARED =
      180.0f * 180.0f; // Increased from 96px

  Vector2D currentPos = ctx.transform.position;

  // Use pre-fetched path state from context (no Instance() call needed)
  if (!ctx.pathData)
    return false;
  auto &pathData = *ctx.pathData;

  const bool skipRefresh =
      (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
       pathData.progressTimer < noProgressWindow);
  // Check if path needs refresh
  auto &edm = EntityDataManager::Instance();
  bool needRefresh =
      !pathData.hasPath || pathData.navIndex >= pathData.pathLength;

  // Check for progress towards current waypoint
  if (!skipRefresh && !needRefresh && pathData.isFollowingPath()) {
    float d = (edm.getWaypoint(ctx.edmIndex, pathData.navIndex) - currentPos)
                  .length();
    if (d + 1.0f < pathData.lastNodeDistance) {
      pathData.lastNodeDistance = d;
      pathData.progressTimer = 0.0f;
    } else if (pathData.progressTimer > noProgressWindow) {
      needRefresh = true;
    }
  }

  // Check if path is stale
  if (!skipRefresh && pathData.pathUpdateTimer > pathTTL) {
    needRefresh = true;
  }

  // Request new path if needed and cooldown allows
  if (needRefresh && pathData.pathRequestCooldown <= 0.0f) {
    // Gate refresh on significant goal change to avoid thrash
    bool goalChanged = true;
    if (!skipRefresh && pathData.hasPath && pathData.pathLength > 0) {
      Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
      goalChanged =
          ((goal - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
    }

    // Only request new path if goal changed significantly
    if (goalChanged) {
      // EDM-integrated async pathfinding - result written directly to EDM
      auto &pf = this->pathfinder();
      pf.requestPathToEDM(ctx.edmIndex,
                          pf.clampToWorldBounds(currentPos, 100.0f), goal,
                          PathfinderManager::Priority::High);

      // Apply cooldown to prevent spam
      pathData.pathRequestCooldown =
          0.8f; // Shorter cooldown for flee (more urgent)
    }
  }

  // Follow existing path if available (using EDM path state)
  if (pathData.isFollowingPath()) {
    Vector2D node = ctx.pathData->currentWaypoint;
    Vector2D dir = node - currentPos;
    float const len = dir.length();

    if (len > 0.01f) {
      dir = dir * (1.0f / len);
      ctx.transform.velocity = dir * speed;
    }

    // Check if reached current waypoint
    if (len <= flee.navRadius) {
      edm.advanceWaypointWithCache(ctx.edmIndex);
    }

    return true;
  }

  return false;
}
