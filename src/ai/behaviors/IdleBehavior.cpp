/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/IdleBehavior.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "ai/internal/Crowd.hpp"
#include <cmath>

IdleBehavior::IdleBehavior(IdleMode mode, float idleRadius)
    : m_idleMode(mode), m_idleRadius(idleRadius) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed

  // Initialize parameters based on mode with mode-specific radii
  switch (mode) {
  case IdleMode::STATIONARY:
    m_movementFrequency = 0.0f; // No movement
    m_turnFrequency = 0.0f;     // No turning
    m_idleRadius = 0.0f;        // No movement radius (completely still)
    break;
  case IdleMode::SUBTLE_SWAY:
    m_movementFrequency = 2.0f; // Gentle swaying every 2 seconds
    m_turnFrequency = 8.0f;     // Occasional turns
    m_idleRadius = 30.0f;       // ~1 tile subtle sway radius
    break;
  case IdleMode::OCCASIONAL_TURN:
    m_movementFrequency = 0.0f; // No position movement
    m_turnFrequency = 4.0f;     // Turn every 4 seconds
    m_idleRadius = 0.0f;        // No movement radius (rotation only)
    break;
  case IdleMode::LIGHT_FIDGET:
    m_movementFrequency = 1.5f; // Light fidgeting
    m_turnFrequency = 3.0f;     // More frequent turns
    m_idleRadius = 50.0f;       // ~1.5 tiles moderate fidget radius
    break;
  }
}

void IdleBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  // Initialize behavior data in EDM (pre-allocated alongside hotData)
  edm.initBehaviorData(idx, BehaviorType::Idle);
  auto& data = edm.getBehaviorData(idx);
  auto& hotData = edm.getHotDataByIndex(idx);

  initializeIdleState(hotData.transform.position, data);
  data.setInitialized(true);
}

void IdleBehavior::executeLogic(BehaviorContext& ctx) {
  if (!isActive() || !ctx.behaviorData)
    return;

  // Use pre-fetched behavior data from context (no Instance() call needed)
  auto& data = *ctx.behaviorData;
  if (!data.isValid()) {
    return;
  }

  if (!data.isInitialized()) {
    initializeIdleState(ctx.transform.position, data);
    data.setInitialized(true);
  }

  // Execute behavior based on current mode
  switch (m_idleMode) {
  case IdleMode::STATIONARY:
    updateStationary(ctx);
    break;
  case IdleMode::SUBTLE_SWAY:
    updateSubtleSway(ctx, data);
    break;
  case IdleMode::OCCASIONAL_TURN:
    updateOccasionalTurn(ctx, data);
    break;
  case IdleMode::LIGHT_FIDGET:
    updateLightFidget(ctx, data);
    break;
  }
}

void IdleBehavior::clean(EntityHandle handle) {
  auto& edm = EntityDataManager::Instance();
  if (handle.isValid()) {
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.clearBehaviorData(idx);
    }
  }
  // Note: Bulk cleanup handled by EDM::prepareForStateTransition()
}

void IdleBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  // Handle mode changes via messages
  if (message == "idle_stationary") {
    setIdleMode(IdleMode::STATIONARY);
  } else if (message == "idle_sway") {
    setIdleMode(IdleMode::SUBTLE_SWAY);
  } else if (message == "idle_turn") {
    setIdleMode(IdleMode::OCCASIONAL_TURN);
  } else if (message == "idle_fidget") {
    setIdleMode(IdleMode::LIGHT_FIDGET);
  } else if (message == "reset_position") {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      auto& data = edm.getBehaviorData(idx);
      if (data.isValid()) {
        const auto& hotData = edm.getHotDataByIndex(idx);
        data.state.idle.originalPosition = hotData.transform.position;
        data.state.idle.currentOffset = Vector2D(0, 0);
      }
    }
  }
}

std::string IdleBehavior::getName() const { return "Idle"; }

void IdleBehavior::setIdleMode(IdleMode mode) {
  m_idleMode = mode;

  // Update timing parameters and radius based on new mode
  switch (mode) {
  case IdleMode::STATIONARY:
    m_movementFrequency = 0.0f;
    m_turnFrequency = 0.0f;
    m_idleRadius = 0.0f;        // No movement radius
    break;
  case IdleMode::SUBTLE_SWAY:
    m_movementFrequency = 2.0f;
    m_turnFrequency = 8.0f;
    m_idleRadius = 30.0f;       // ~1 tile subtle sway
    break;
  case IdleMode::OCCASIONAL_TURN:
    m_movementFrequency = 0.0f;
    m_turnFrequency = 4.0f;
    m_idleRadius = 0.0f;        // No movement radius
    break;
  case IdleMode::LIGHT_FIDGET:
    m_movementFrequency = 1.5f;
    m_turnFrequency = 3.0f;
    m_idleRadius = 50.0f;       // ~1.5 tiles moderate fidget
    break;
  }
}

void IdleBehavior::setIdleRadius(float radius) {
  m_idleRadius = std::max(0.0f, radius);
}

void IdleBehavior::setMovementFrequency(float frequency) {
  m_movementFrequency = std::max(0.0f, frequency);
}

void IdleBehavior::setTurnFrequency(float frequency) {
  m_turnFrequency = std::max(0.0f, frequency);
}

IdleBehavior::IdleMode IdleBehavior::getIdleMode() const { return m_idleMode; }

float IdleBehavior::getIdleRadius() const { return m_idleRadius; }

std::shared_ptr<AIBehavior> IdleBehavior::clone() const {
  auto cloned = std::make_shared<IdleBehavior>(m_idleMode, m_idleRadius);
  cloned->m_movementFrequency = m_movementFrequency;
  cloned->m_turnFrequency = m_turnFrequency;
  cloned->setActive(m_active);
  return cloned;
}

void IdleBehavior::initializeIdleState(const Vector2D& position, BehaviorData& data) const {
  auto& idle = data.state.idle;
  idle.originalPosition = position;
  idle.currentOffset = Vector2D(0, 0);
  idle.movementTimer = 0.0f;
  idle.turnTimer = 0.0f;
  idle.movementInterval = getRandomMovementInterval();
  idle.turnInterval = getRandomTurnInterval();
  idle.currentAngle = 0.0f;
  idle.initialized = true;
}

void IdleBehavior::updateStationary(BehaviorContext& ctx) {
  // Keep entity stationary with zero velocity
  ctx.transform.velocity = Vector2D(0, 0);
}

void IdleBehavior::updateSubtleSway(BehaviorContext& ctx, BehaviorData& data) const {
  auto& idle = data.state.idle;
  idle.movementTimer += ctx.deltaTime;

  if (m_movementFrequency > 0.0f && idle.movementTimer >= idle.movementInterval) {
    // Generate gentle swaying direction
    Vector2D swayDirection = generateRandomOffset();
    swayDirection.normalize();
    ctx.transform.velocity = swayDirection * 35.0f; // Increased from 20px for world-scale movement
    idle.movementTimer = 0.0f;
    idle.movementInterval = getRandomMovementInterval();
  }
  // Keep velocity applied for smooth animation
  // CollisionManager handles overlap resolution
}

void IdleBehavior::updateOccasionalTurn(BehaviorContext& ctx, BehaviorData& data) const {
  auto& idle = data.state.idle;
  idle.turnTimer += ctx.deltaTime;

  if (m_turnFrequency > 0.0f && idle.turnTimer >= idle.turnInterval) {
    // Change facing direction
    idle.currentAngle = m_angleDistribution(m_rng);
    idle.turnTimer = 0.0f;
    idle.turnInterval = getRandomTurnInterval();

    // Note: In a full implementation, you might set entity rotation here
    // entity->setRotation(idle.currentAngle);
  }

  // Stay at original position with zero velocity
  ctx.transform.velocity = Vector2D(0, 0);
}

void IdleBehavior::updateLightFidget(BehaviorContext& ctx, BehaviorData& data) const {
  auto& idle = data.state.idle;
  idle.movementTimer += ctx.deltaTime;
  idle.turnTimer += ctx.deltaTime;

  // Handle movement fidgeting
  if (m_movementFrequency > 0.0f && idle.movementTimer >= idle.movementInterval) {
    // Generate light fidgeting direction
    Vector2D fidgetDirection = generateRandomOffset();
    fidgetDirection.normalize();
    ctx.transform.velocity = fidgetDirection * 40.0f; // Increased from 25px for world-scale fidgeting
    idle.movementTimer = 0.0f;
    idle.movementInterval = getRandomMovementInterval();
  }
  // Keep velocity applied for smooth animation
  // CollisionManager handles overlap resolution

  // Handle turning
  if (m_turnFrequency > 0.0f && idle.turnTimer >= idle.turnInterval) {
    idle.currentAngle = m_angleDistribution(m_rng);
    idle.turnTimer = 0.0f;
    idle.turnInterval = getRandomTurnInterval();
  }
}

Vector2D IdleBehavior::generateRandomOffset() const {
  float angle = m_angleDistribution(m_rng);
  float radius = m_radiusDistribution(m_rng) * m_idleRadius;

  return Vector2D(radius * std::cos(angle), radius * std::sin(angle));
}

float IdleBehavior::getRandomMovementInterval() const {
  if (m_movementFrequency <= 0.0f)
    return std::numeric_limits<float>::max();

  float const baseInterval = 1.0f / m_movementFrequency; // Convert to seconds
  float variation = m_frequencyVariation(m_rng);

  return baseInterval * variation;
}

float IdleBehavior::getRandomTurnInterval() const {
  if (m_turnFrequency <= 0.0f)
    return std::numeric_limits<float>::max();

  float const baseInterval = 1.0f / m_turnFrequency; // Convert to seconds
  float variation = m_frequencyVariation(m_rng);

  return baseInterval * variation;
}
