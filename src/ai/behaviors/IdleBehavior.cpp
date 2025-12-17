/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/IdleBehavior.hpp"
#include "managers/CollisionManager.hpp"
#include "ai/internal/Crowd.hpp"
#include <cmath>

IdleBehavior::IdleBehavior(IdleMode mode, float idleRadius)
    : m_entityStates(), m_idleMode(mode), m_idleRadius(idleRadius) {
  // m_movementFrequency, m_turnFrequency, m_rng, m_angleDistribution,
  // m_radiusDistribution, m_frequencyVariation use default initializers

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

void IdleBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  auto &state = m_entityStates[entity];
  initializeEntityState(entity, state);
}

void IdleBehavior::executeLogic(EntityPtr entity, float deltaTime) {
  if (!entity || !isActive())
    return;

  auto it = m_entityStates.find(entity);
  if (it == m_entityStates.end()) {
    init(entity); // Initialize if not found
    it = m_entityStates.find(entity);
    if (it == m_entityStates.end())
      return;
  }

  EntityState &state = it->second;

  if (!state.initialized) {
    initializeEntityState(entity, state);
  }

  // Execute behavior based on current mode
  switch (m_idleMode) {
  case IdleMode::STATIONARY:
    updateStationary(entity, state);
    break;
  case IdleMode::SUBTLE_SWAY:
    updateSubtleSway(entity, state, deltaTime);
    break;
  case IdleMode::OCCASIONAL_TURN:
    updateOccasionalTurn(entity, state, deltaTime);
    break;
  case IdleMode::LIGHT_FIDGET:
    updateLightFidget(entity, state, deltaTime);
    break;
  }
}

void IdleBehavior::clean(EntityPtr entity) {
  if (entity) {
    m_entityStates.erase(entity);
  }
}

void IdleBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (!entity)
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
    auto it = m_entityStates.find(entity);
    if (it != m_entityStates.end()) {
      it->second.originalPosition = entity->getPosition();
      it->second.currentOffset = Vector2D(0, 0);
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

void IdleBehavior::initializeEntityState(EntityPtr entity, EntityState &state) const {
  state.originalPosition = entity->getPosition();
  state.currentOffset = Vector2D(0, 0);
  state.movementTimer = 0.0f;
  state.turnTimer = 0.0f;
  state.movementInterval = getRandomMovementInterval();
  state.turnInterval = getRandomTurnInterval();
  state.currentAngle = 0.0f;
  state.initialized = true;
}

void IdleBehavior::updateStationary(EntityPtr entity,
                                    EntityState & /* state */) {
  // Keep entity stationary with zero velocity
  entity->setVelocity(Vector2D(0, 0));
}

void IdleBehavior::updateSubtleSway(EntityPtr entity, EntityState &state, float deltaTime) const {
  state.movementTimer += deltaTime;

  if (m_movementFrequency > 0.0f && state.movementTimer >= state.movementInterval) {
    // Generate gentle swaying direction
    Vector2D swayDirection = generateRandomOffset();
    swayDirection.normalize();
    entity->setVelocity(swayDirection * 35.0f); // Increased from 20px for world-scale movement
    state.movementTimer = 0.0f;
    state.movementInterval = getRandomMovementInterval();
  }
  // Keep velocity applied for smooth animation - don't reset to zero
  // Apply very light separation (decimated) so idlers don't stack perfectly
  applyDecimatedSeparation(entity, entity->getPosition(), entity->getVelocity(),
                           35.0f, 30.0f, 0.15f, 4, state.separationTimer,
                           state.lastSepVelocity, deltaTime);
}

void IdleBehavior::updateOccasionalTurn(EntityPtr entity, EntityState &state, float deltaTime) const {
  state.turnTimer += deltaTime;

  if (m_turnFrequency > 0.0f && state.turnTimer >= state.turnInterval) {
    // Change facing direction
    state.currentAngle = m_angleDistribution(m_rng);
    state.turnTimer = 0.0f;
    state.turnInterval = getRandomTurnInterval();

    // Note: In a full implementation, you might set entity rotation here
    // entity->setRotation(state.currentAngle);
  }

  // Stay at original position with zero velocity
  entity->setVelocity(Vector2D(0, 0));
}

void IdleBehavior::updateLightFidget(EntityPtr entity, EntityState &state, float deltaTime) const {
  state.movementTimer += deltaTime;
  state.turnTimer += deltaTime;

  // Handle movement fidgeting
  if (m_movementFrequency > 0.0f && state.movementTimer >= state.movementInterval) {
    // Generate light fidgeting direction
    Vector2D fidgetDirection = generateRandomOffset();
    fidgetDirection.normalize();
    entity->setVelocity(fidgetDirection * 40.0f); // Increased from 25px for world-scale fidgeting
    state.movementTimer = 0.0f;
    state.movementInterval = getRandomMovementInterval();
  }
  // Keep velocity applied for smooth animation and apply very light separation (decimated)
  applyDecimatedSeparation(entity, entity->getPosition(), entity->getVelocity(),
                           40.0f, 30.0f, 0.15f, 4, state.separationTimer,
                           state.lastSepVelocity, deltaTime);

  // Handle turning
  if (m_turnFrequency > 0.0f && state.turnTimer >= state.turnInterval) {
    state.currentAngle = m_angleDistribution(m_rng);
    state.turnTimer = 0.0f;
    state.turnInterval = getRandomTurnInterval();
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

  float baseInterval = 1.0f / m_movementFrequency; // Convert to seconds
  float variation = m_frequencyVariation(m_rng);

  return baseInterval * variation;
}

float IdleBehavior::getRandomTurnInterval() const {
  if (m_turnFrequency <= 0.0f)
    return std::numeric_limits<float>::max();

  float baseInterval = 1.0f / m_turnFrequency; // Convert to seconds
  float variation = m_frequencyVariation(m_rng);

  return baseInterval * variation;
}
