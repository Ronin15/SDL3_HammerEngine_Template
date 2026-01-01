/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/AttackBehavior.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/WorldManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"  // For PathPriority enum
#include <algorithm>

AttackBehavior::AttackBehavior(float attackRange, float attackDamage,
                               float attackSpeed)
    : m_attackRange(attackRange), m_attackDamage(attackDamage),
      m_attackSpeed(attackSpeed) {
  m_optimalRange = attackRange * 0.8f;
  m_minimumRange = attackRange * 0.4f;
}

void AttackBehavior::applyConfig(const HammerEngine::AttackBehaviorConfig& config) {
  // Range parameters
  m_attackRange = config.attackRange;
  m_optimalRange = config.attackRange * config.optimalRangeMultiplier;
  m_minimumRange = config.attackRange * config.minimumRangeMultiplier;

  // Combat parameters
  m_attackSpeed = config.attackSpeed;
  m_movementSpeed = config.movementSpeed;
  m_attackCooldown = config.attackCooldown;
  m_recoveryTime = config.recoveryTime;

  // Damage parameters
  m_attackDamage = config.attackDamage;
  m_damageVariation = config.damageVariation;
  m_criticalHitChance = config.criticalHitChance;
  m_criticalHitMultiplier = config.criticalHitMultiplier;
  m_knockbackForce = config.knockbackForce;

  // Positioning parameters
  m_circleStrafe = config.circleStrafe;
  m_strafeRadius = config.strafeRadius;
  m_flankingEnabled = config.flankingEnabled;
  m_preferredAttackAngle = config.preferredAttackAngle;

  // Tactical parameters
  m_retreatThreshold = config.retreatThreshold;
  m_aggression = config.aggression;
  m_teamwork = config.teamwork;
  m_avoidFriendlyFire = config.avoidFriendlyFire;

  // Special abilities
  m_comboAttacks = config.comboAttacks;
  m_maxCombo = config.maxCombo;
  m_specialAttackChance = config.specialAttackChance;
  m_aoeRadius = config.aoeRadius;

  // Mode-specific parameters
  m_chargeDamageMultiplier = config.chargeDamageMultiplier;
}

AttackBehavior::AttackBehavior(AttackMode mode, float attackRange,
                               float attackDamage)
    : m_attackMode(mode) {
  // Create mode-specific configuration
  HammerEngine::AttackBehaviorConfig config;

  switch (mode) {
  case AttackMode::MELEE_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createMeleeConfig(attackRange);
    break;
  case AttackMode::RANGED_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createRangedConfig(attackRange);
    break;
  case AttackMode::CHARGE_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createChargeConfig(attackRange);
    break;
  case AttackMode::AMBUSH_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createAmbushConfig(attackRange);
    break;
  case AttackMode::COORDINATED_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createCoordinatedConfig(attackRange);
    break;
  case AttackMode::HIT_AND_RUN:
    config = HammerEngine::AttackBehaviorConfig::createHitAndRunConfig(attackRange);
    break;
  case AttackMode::BERSERKER_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createBerserkerConfig(attackRange);
    break;
  }

  // Override base damage if provided
  if (attackDamage > 0.0f) {
    config.attackDamage = attackDamage;
  }

  // Apply the configuration
  applyConfig(config);
}

void AttackBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  EntityHandle::IDType entityId = handle.getId();

  // NOTE: EntityPtr cache is populated by AIManager when behavior is assigned
  // The cache is needed for helper methods during migration - will be removed later

  auto &state = m_entityStates[entityId];
  state = EntityState(); // Reset to default state
  state.currentState = AttackState::SEEKING;
  state.stateChangeTimer = 0.0f;
  state.currentHealth = state.maxHealth;
  state.currentStamina = 100.0f;
  state.canAttack = true;

  // Animation notification will happen in executeLogic when state changes
  // (Can't do it here without EntityPtr - will be migrated to use EDM events)
}

void AttackBehavior::updateTimers(EntityState& state, float deltaTime) {
  state.attackTimer += deltaTime;
  state.stateChangeTimer += deltaTime;
  state.damageTimer += deltaTime;
  if (state.comboTimer > 0.0f) {
    state.comboTimer -= deltaTime;
  }
  state.strafeTimer += deltaTime;
  state.baseState.pathUpdateTimer += deltaTime;
  state.baseState.progressTimer += deltaTime;
  if (state.baseState.backoffTimer > 0.0f) {
    state.baseState.backoffTimer -= deltaTime;
  }
}

AttackBehavior::EntityState& AttackBehavior::ensureEntityState(EntityHandle::IDType entityId) {
  auto it = m_entityStates.find(entityId);
  if (it == m_entityStates.end()) {
    // Create default state if not found
    auto [insertIt, inserted] = m_entityStates.emplace(entityId, EntityState{});
    if (!inserted) {
      // This should never happen, but provide fallback
      AI_ERROR("Failed to initialize entity state for AttackBehavior");
      static EntityState fallbackState;
      return fallbackState;
    }
    return insertIt->second;
  }
  return it->second;
}

void AttackBehavior::updateTargetDistance(const Vector2D& entityPos, const Vector2D& targetPos, EntityState& state) {
  // PERFORMANCE: Store squared distance and compute actual distance only when needed
  float distSquared = (entityPos - targetPos).lengthSquared();
  state.targetDistance = std::sqrt(distSquared);
}

void AttackBehavior::updateCombatState(EntityState& state) {
  if (!state.inCombat && state.targetDistance <= m_attackRange * COMBAT_ENTER_RANGE_MULT) {
    state.inCombat = true;
  } else if (state.inCombat && state.targetDistance > m_attackRange * COMBAT_EXIT_RANGE_MULT) {
    state.inCombat = false;
    state.currentState = AttackState::SEEKING;
  }
}

void AttackBehavior::handleNoTarget(EntityState& state) {
  state.hasTarget = false;
  state.inCombat = false;
  if (state.currentState != AttackState::SEEKING) {
    changeState(state, AttackState::SEEKING);
  }
}

void AttackBehavior::updateTargetTracking(const Vector2D& entityPos, EntityState& state, const Vector2D& targetPos, bool hasTarget) {
  if (hasTarget) {
    state.hasTarget = true;
    state.lastTargetPosition = targetPos;
    updateTargetDistance(entityPos, targetPos, state);
    updateCombatState(state);
  } else {
    handleNoTarget(state);
  }
}

void AttackBehavior::dispatchModeUpdate(EntityPtr entity, EntityState& state, float deltaTime, const Vector2D& targetPos) {
  switch (m_attackMode) {
  case AttackMode::MELEE_ATTACK:
    updateMeleeAttack(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::RANGED_ATTACK:
    updateRangedAttack(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::CHARGE_ATTACK:
    updateChargeAttack(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::AMBUSH_ATTACK:
    updateAmbushAttack(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::COORDINATED_ATTACK:
    updateCoordinatedAttack(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::HIT_AND_RUN:
    updateHitAndRun(entity, state, deltaTime, targetPos);
    break;
  case AttackMode::BERSERKER_ATTACK:
    updateBerserkerAttack(entity, state, deltaTime, targetPos);
    break;
  }
}

void AttackBehavior::executeLogic(BehaviorContext& ctx) {
  if (!isActive())
    return;

  EntityState &state = ensureEntityState(ctx.entityId);

  // Get cached EntityPtr for helper methods that still use it (attack execution, movement)
  auto it = m_entityPtrCache.find(ctx.entityId);
  if (it == m_entityPtrCache.end()) return;
  EntityPtr entity = it->second;

  // Phase 2 EDM Migration: Use handle-based target lookup
  auto& edm = EntityDataManager::Instance();
  EntityHandle targetHandle = getTargetHandle();
  Vector2D targetPos;
  bool hasTarget = false;

  if (targetHandle.isValid()) {
    size_t targetIdx = edm.getIndex(targetHandle);
    if (targetIdx != SIZE_MAX) {
      const auto& targetHotData = edm.getHotDataByIndex(targetIdx);
      if (targetHotData.isAlive()) {
        targetPos = targetHotData.transform.position;
        hasTarget = true;
      }
    }
  }

  Vector2D entityPos = ctx.transform.position;

  // Track state for animation notification
  AttackState const previousState = state.currentState;

  // Update all timers
  updateTimers(state, ctx.deltaTime);

  // Update target tracking and combat state
  updateTargetTracking(entityPos, state, targetPos, hasTarget);

  // Update state timer
  updateStateTimer(state);

  // Check for retreat conditions
  if (shouldRetreat(state) && state.currentState != AttackState::RETREATING) {
    changeState(state, AttackState::RETREATING);
  }

  // Execute behavior based on attack mode
  if (hasTarget) {
    dispatchModeUpdate(entity, state, ctx.deltaTime, targetPos);
  }

  // Notify animation state change if state changed
  if (state.currentState != previousState) {
    notifyAnimationStateChange(entity, state.currentState);
  }
}

void AttackBehavior::clean(EntityHandle handle) {
  if (handle.isValid()) {
    // Reset velocity via EDM
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }

    EntityHandle::IDType entityId = handle.getId();
    m_entityStates.erase(entityId);
    m_entityPtrCache.erase(entityId);
  }
}

void AttackBehavior::onMessage(EntityHandle handle, const std::string &message) {
  if (!handle.isValid())
    return;

  auto it = m_entityStates.find(handle.getId());
  if (it == m_entityStates.end())
    return;

  EntityState &state = it->second;

  if (message == "attack_target") {
    if (state.canAttack && state.hasTarget) {
      changeState(state, AttackState::ATTACKING);
    }
  } else if (message == "retreat") {
    changeState(state, AttackState::RETREATING);
  } else if (message == "stop_attack") {
    changeState(state, AttackState::SEEKING);
    state.inCombat = false;
  } else if (message == "enable_combo") {
    m_comboAttacks = true;
  } else if (message == "disable_combo") {
    m_comboAttacks = false;
    state.currentCombo = 0;
  } else if (message == "heal") {
    state.currentHealth = state.maxHealth;
  } else if (message == "berserk") {
    m_aggression = 1.0f;
    m_attackSpeed *= 1.5f;
    m_movementSpeed *= 1.3f;
  }
}

std::string AttackBehavior::getName() const { return "Attack"; }

void AttackBehavior::setAttackMode(AttackMode mode) { m_attackMode = mode; }

void AttackBehavior::setAttackRange(float range) {
  m_attackRange = std::max(0.0f, range);
  m_optimalRange = m_attackRange * 0.8f;
  m_minimumRange = m_attackRange * 0.3f;
}

void AttackBehavior::setAttackDamage(float damage) {
  m_attackDamage = std::max(0.0f, damage);
}

void AttackBehavior::setAttackSpeed(float speed) {
  m_attackSpeed = std::max(0.1f, speed);
}

void AttackBehavior::setMovementSpeed(float speed) {
  m_movementSpeed = std::max(0.0f, speed);
}

void AttackBehavior::setAttackCooldown(float cooldown) {
  m_attackCooldown = std::max(0.0f, cooldown);
}

void AttackBehavior::setRecoveryTime(float recoveryTime) {
  m_recoveryTime = std::max(0.0f, recoveryTime);
}

void AttackBehavior::setOptimalRange(float range) {
  m_optimalRange = std::max(0.0f, range);
}

void AttackBehavior::setMinimumRange(float range) {
  m_minimumRange = std::max(0.0f, range);
}

void AttackBehavior::setCircleStrafe(bool enabled, float radius) {
  m_circleStrafe = enabled;
  m_strafeRadius = std::max(0.0f, radius);
}

void AttackBehavior::setFlankingEnabled(bool enabled) {
  m_flankingEnabled = enabled;
}

void AttackBehavior::setPreferredAttackAngle(float angleDegrees) {
  m_preferredAttackAngle = angleDegrees * M_PI / 180.0f;
}

void AttackBehavior::setDamageVariation(float variation) {
  m_damageVariation = std::clamp(variation, 0.0f, 1.0f);
}

void AttackBehavior::setCriticalHitChance(float chance) {
  m_criticalHitChance = std::clamp(chance, 0.0f, 1.0f);
}

void AttackBehavior::setCriticalHitMultiplier(float multiplier) {
  m_criticalHitMultiplier = std::max(1.0f, multiplier);
}

void AttackBehavior::setKnockbackForce(float force) {
  m_knockbackForce = std::max(0.0f, force);
}

void AttackBehavior::setRetreatThreshold(float healthPercentage) {
  m_retreatThreshold = std::clamp(healthPercentage, 0.0f, 1.0f);
}

void AttackBehavior::setAggression(float aggression) {
  m_aggression = std::clamp(aggression, 0.0f, 1.0f);
}

void AttackBehavior::setTeamwork(bool enabled) { m_teamwork = enabled; }

void AttackBehavior::setAvoidFriendlyFire(bool enabled) {
  m_avoidFriendlyFire = enabled;
}

void AttackBehavior::setComboAttacks(bool enabled, int maxCombo) {
  m_comboAttacks = enabled;
  m_maxCombo = std::max(1, maxCombo);
}

void AttackBehavior::setSpecialAttackChance(float chance) {
  m_specialAttackChance = std::clamp(chance, 0.0f, 1.0f);
}

void AttackBehavior::setAreaOfEffectRadius(float radius) {
  m_aoeRadius = std::max(0.0f, radius);
}

void AttackBehavior::setChargeDamageMultiplier(float multiplier) {
  m_chargeDamageMultiplier = std::max(1.0f, multiplier);
}

bool AttackBehavior::isInCombat() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto& pair) { return pair.second.inCombat; });
}

bool AttackBehavior::isAttacking() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto& pair) { return pair.second.currentState == AttackState::ATTACKING; });
}

bool AttackBehavior::canAttack() const {
  return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                     [](const auto& pair) { return pair.second.canAttack; });
}

AttackBehavior::AttackState AttackBehavior::getCurrentAttackState() const {
  auto it = std::find_if(m_entityStates.begin(), m_entityStates.end(),
                         [](const auto &pair) { return pair.second.inCombat; });
  return (it != m_entityStates.end()) ? it->second.currentState
                                      : AttackState::SEEKING;
}

AttackBehavior::AttackMode AttackBehavior::getAttackMode() const {
  return m_attackMode;
}

float AttackBehavior::getDistanceToTarget() const {
  auto it = std::find_if(m_entityStates.begin(), m_entityStates.end(),
                         [](const auto &pair) {
                           return pair.second.hasTarget && pair.second.inCombat;
                         });
  return (it != m_entityStates.end()) ? it->second.targetDistance : -1.0f;
}

float AttackBehavior::getLastAttackTime() const {
  if (m_entityStates.empty())
    return 0.0f;
  auto it = std::max_element(m_entityStates.begin(), m_entityStates.end(),
                             [](const auto &a, const auto &b) {
                               return a.second.attackTimer <
                                      b.second.attackTimer;
                             });
  return it->second.attackTimer;
}

int AttackBehavior::getCurrentCombo() const {
  if (m_entityStates.empty())
    return 0;
  auto it =
      std::max_element(m_entityStates.begin(), m_entityStates.end(),
                       [](const auto &a, const auto &b) {
                         return a.second.currentCombo < b.second.currentCombo;
                       });
  return it->second.currentCombo;
}

std::shared_ptr<AIBehavior> AttackBehavior::clone() const {
  // Use compiler-generated copy constructor to copy all member variables
  return std::make_shared<AttackBehavior>(*this);
}

EntityHandle AttackBehavior::getTargetHandle() const {
  return AIManager::Instance().getPlayerHandle();
}

Vector2D AttackBehavior::getTargetPosition() const {
  return AIManager::Instance().getPlayerPosition();
}

bool AttackBehavior::isTargetInRange(const Vector2D& entityPos, const Vector2D& targetPos) const {
  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entityPos - targetPos).lengthSquared();
  float const attackRangeSquared = m_attackRange * m_attackRange;
  return distanceSquared <= attackRangeSquared;
}

bool AttackBehavior::isTargetInAttackRange(const Vector2D& entityPos, const Vector2D& targetPos, const EntityState& state) const {
  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entityPos - targetPos).lengthSquared();
  float effectiveRange = calculateEffectiveRange(state);
  float const effectiveRangeSquared = effectiveRange * effectiveRange;
  return distanceSquared <= effectiveRangeSquared;
}

float AttackBehavior::calculateDamage(const EntityState &state) const {
  float baseDamage = m_attackDamage;

  // Apply damage variation
  float variation = (m_damageRoll(m_rng) - 0.5f) * 2.0f * m_damageVariation;
  baseDamage *= (1.0f + variation);

  // Check for critical hit
  if (m_criticalRoll(m_rng) < m_criticalHitChance) {
    baseDamage *= m_criticalHitMultiplier;
  }

  // Apply combo multiplier
  if (m_comboAttacks && state.currentCombo > 0) {
    float const comboMultiplier = 1.0f + (state.currentCombo * COMBO_DAMAGE_PER_LEVEL);
    baseDamage *= comboMultiplier;
  }

  // Apply charge multiplier if charging
  if (state.isCharging) {
    baseDamage *= m_chargeDamageMultiplier;
  }

  return baseDamage;
}

Vector2D AttackBehavior::calculateOptimalAttackPosition(
    const Vector2D& entityPos, const Vector2D& targetPos, const EntityState & /*state*/) const {
  // Calculate direction from target to optimal position
  Vector2D direction = normalizeDirection(entityPos - targetPos);

  // Apply preferred attack angle if set
  if (m_preferredAttackAngle != 0.0f) {
    direction = rotateVector(direction, m_preferredAttackAngle);
  }

  // Calculate optimal position
  Vector2D const optimalPos = targetPos + direction * m_optimalRange;

  return optimalPos;
}

Vector2D AttackBehavior::calculateFlankingPosition(const Vector2D& entityPos,
                                                   const Vector2D& targetPos) const {
  // Calculate perpendicular direction for flanking
  Vector2D toTarget = normalizeDirection(targetPos - entityPos);
  Vector2D flankDirection =
      Vector2D(-toTarget.getY(), toTarget.getX()); // Perpendicular

  // Choose left or right flank based on current position
  if ((entityPos - targetPos).getX() < 0) {
    flankDirection = Vector2D(toTarget.getY(), -toTarget.getX());
  }

  return targetPos + flankDirection * m_optimalRange;
}

Vector2D
AttackBehavior::calculateStrafePosition(const Vector2D& entityPos, const Vector2D& targetPos,
                                        const EntityState &state) const {
  // Calculate strafe direction (perpendicular to target direction)
  Vector2D toTarget = normalizeDirection(targetPos - entityPos);
  Vector2D const strafeDir =
      Vector2D(-toTarget.getY(), toTarget.getX()) * state.strafeDirectionInt;

  return entityPos + strafeDir * (m_movementSpeed * 2.0f);
}

void AttackBehavior::changeState(EntityState &state, AttackState newState) {
  if (state.currentState != newState) {
    state.currentState = newState;
    state.stateChangeTimer = 0.0f;

    // Reset state-specific flags
    switch (newState) {
    case AttackState::ATTACKING:
      state.recoveryTimer = 0.0f;
      break;
    case AttackState::RECOVERING:
      state.recoveryTimer = 0.0f;
      break;
    case AttackState::RETREATING:
      state.isRetreating = true;
      break;
    default:
      state.isRetreating = false;
      break;
    }
  }
}

void AttackBehavior::notifyAnimationStateChange(EntityPtr entity, AttackState newState) {
  // Try to cast to NPC to notify animation state change
  auto npc = std::dynamic_pointer_cast<NPC>(entity);
  if (!npc) {
    return; // Not an NPC (could be Player or other entity type)
  }

  // Map AttackBehavior states to NPC animation states
  switch (newState) {
  case AttackState::ATTACKING:
    npc->setAnimationState("Attacking");
    break;
  case AttackState::RECOVERING:
    npc->setAnimationState("Recovering");
    break;
  case AttackState::COOLDOWN:
    npc->setAnimationState("Idle");
    break;
  case AttackState::SEEKING:
  case AttackState::APPROACHING:
  case AttackState::POSITIONING:
  case AttackState::RETREATING:
  default:
    npc->setAnimationState("Walking");
    break;
  }
}

void AttackBehavior::updateStateTimer(EntityState &state) {

  float const timeInState = state.stateChangeTimer;

  // Handle state transitions based on timing
  switch (state.currentState) {
  case AttackState::ATTACKING:
    if (timeInState > (1.0f / m_attackSpeed)) {
      changeState(state, AttackState::RECOVERING);
    }
    break;

  case AttackState::RECOVERING:
    if (timeInState > m_recoveryTime) {
      changeState(state, AttackState::COOLDOWN);
    }
    break;

  case AttackState::COOLDOWN:
    if (timeInState > m_attackCooldown) {
      changeState(state, state.hasTarget ? AttackState::APPROACHING
                                         : AttackState::SEEKING);
    }
    break;

  default:
    break;
  }
}

bool AttackBehavior::shouldRetreat(const EntityState &state) const {
  float const healthRatio = state.currentHealth / state.maxHealth;
  return healthRatio <= m_retreatThreshold && m_aggression < 0.8f;
}

bool AttackBehavior::shouldCharge(float distance, const EntityState &state) const {
  if (m_attackMode != AttackMode::CHARGE_ATTACK || !state.hasTarget)
    return false;

  return distance > m_optimalRange * CHARGE_DISTANCE_THRESHOLD_MULT && distance <= m_attackRange;
}

void AttackBehavior::executeAttack(EntityPtr entity, const Vector2D& targetPos,
                                   EntityState &state) {
  if (!entity)
    return;

  Vector2D entityPos = entity->getPosition();

  // Calculate damage
  float const damage = calculateDamage(state);

  // Calculate knockback
  Vector2D knockback = calculateKnockbackVector(entityPos, targetPos);
  knockback = knockback * m_knockbackForce;

  // Apply damage via handle-based system
  EntityHandle targetHandle = getTargetHandle();
  applyDamageToTarget(targetHandle, damage, knockback);

  // Update attack state
  state.attackTimer = 0.0f;
  state.lastAttackHit = true; // Simplified - assume all attacks hit

  // Handle combo system
  if (m_comboAttacks) {
    if (state.comboTimer > 0.0f) {
      state.currentCombo = std::min(state.currentCombo + 1, m_maxCombo);
    } else {
      state.currentCombo = 1;
      state.comboTimer = COMBO_TIMEOUT;
    }
  }

  // Apply area of effect damage if enabled
  if (m_aoeRadius > 0.0f) {
    applyAreaOfEffectDamage(entityPos, targetPos, damage * 0.5f);
  }
}

void AttackBehavior::executeSpecialAttack(EntityPtr entity, const Vector2D& targetPos,
                                          EntityState &state) {
  if (!entity)
    return;

  Vector2D entityPos = entity->getPosition();

  // Enhanced attack with special effects
  float const specialDamage = calculateDamage(state) * SPECIAL_ATTACK_MULTIPLIER;
  Vector2D knockback =
      calculateKnockbackVector(entityPos, targetPos) * (m_knockbackForce * SPECIAL_ATTACK_MULTIPLIER);

  EntityHandle targetHandle = getTargetHandle();
  applyDamageToTarget(targetHandle, specialDamage, knockback);

  state.attackTimer = 0.0f;
  state.specialAttackReady = false;
}

void AttackBehavior::executeComboAttack(EntityPtr entity, const Vector2D& targetPos,
                                        EntityState &state) {
  if (!m_comboAttacks || state.currentCombo == 0) {
    executeAttack(entity, targetPos, state);
    return;
  }

  if (!entity)
    return;

  Vector2D entityPos = entity->getPosition();

  // Combo finisher
  if (state.currentCombo >= m_maxCombo) {
    float const comboDamage = calculateDamage(state) * COMBO_FINISHER_MULTIPLIER;
    Vector2D knockback =
        calculateKnockbackVector(entityPos, targetPos) * (m_knockbackForce * COMBO_FINISHER_MULTIPLIER);

    EntityHandle targetHandle = getTargetHandle();
    applyDamageToTarget(targetHandle, comboDamage, knockback);

    // Reset combo
    state.currentCombo = 0;
    state.comboTimer = 0.0f;
  } else {
    executeAttack(entity, targetPos, state);
  }
}

void AttackBehavior::applyDamageToTarget(EntityHandle targetHandle, float damage,
                                         const Vector2D &knockback) {
  if (!targetHandle.isValid()) {
    return;
  }

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(targetHandle);
  if (idx == SIZE_MAX) {
    return;
  }

  // Scale knockback for visual effect
  Vector2D scaledKnockback = knockback * 0.1f;

  // Apply damage and knockback via EDM
  auto& hotData = edm.getHotDataByIndex(idx);
  auto& charData = edm.getCharacterData(targetHandle);

  charData.health = std::max(0.0f, charData.health - damage);
  hotData.transform.velocity = hotData.transform.velocity + scaledKnockback;

  // Check for death
  if (charData.health <= 0.0f) {
    hotData.flags &= ~EntityHotData::FLAG_ALIVE;
  }
}

void AttackBehavior::applyAreaOfEffectDamage(const Vector2D& /*entityPos*/,
                                             const Vector2D& /*targetPos*/,
                                             float /*damage*/) {
  // In a full implementation, this would find all entities within the AOE
  // radius and apply damage to them
}

void AttackBehavior::updateMeleeAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  switch (state.currentState) {
  case AttackState::SEEKING:
    updateSeeking(state);
    break;
  case AttackState::APPROACHING:
    updateApproaching(entity, state, deltaTime, targetPos);
    break;
  case AttackState::POSITIONING:
    updatePositioning(entity, state, deltaTime, targetPos);
    break;
  case AttackState::ATTACKING:
    updateAttacking(entity, state, targetPos);
    break;
  case AttackState::RECOVERING:
    updateRecovering(state);
    break;
  case AttackState::RETREATING:
    updateRetreating(entity, state, targetPos);
    break;
  case AttackState::COOLDOWN:
    updateCooldown(state);
    break;
  }
}

void AttackBehavior::updateRangedAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  Vector2D entityPos = entity->getPosition();
  float const distance = state.targetDistance;

  // Ranged attackers need to maintain distance - back off if too close
  bool const tooClose = distance < m_minimumRange;
  bool const inOptimalRange = distance >= m_minimumRange && distance <= m_optimalRange * 1.2f;

  switch (state.currentState) {
  case AttackState::SEEKING:
    updateSeeking(state);
    break;

  case AttackState::APPROACHING:
    // For ranged, approach but stop at optimal range
    if (distance <= m_optimalRange) {
      changeState(state, AttackState::POSITIONING);
    } else {
      moveToPosition(entity, targetPos, m_movementSpeed, deltaTime);
    }
    break;

  case AttackState::POSITIONING:
    if (tooClose) {
      // Back off from target to maintain ranged distance
      Vector2D const awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D const retreatPos = entityPos + awayFromTarget * (m_optimalRange * 0.5f);
      moveToPosition(entity, retreatPos, m_movementSpeed * 1.2f, deltaTime);
    } else if (inOptimalRange && state.canAttack) {
      changeState(state, AttackState::ATTACKING);
    } else if (distance > m_optimalRange * 1.3f) {
      // Too far, approach slightly
      changeState(state, AttackState::APPROACHING);
    } else {
      // Circle strafe at optimal range to avoid being an easy target
      Vector2D const toTarget = (targetPos - entityPos).normalized();
      Vector2D const perpendicular(-toTarget.getY(), toTarget.getX());
      float const strafeDir = (state.strafeDirectionInt > 0) ? 1.0f : -1.0f;
      Vector2D const strafePos = entityPos + perpendicular * (40.0f * strafeDir);
      moveToPosition(entity, strafePos, m_movementSpeed * 0.6f, deltaTime);
    }
    break;

  case AttackState::ATTACKING:
    updateAttacking(entity, state, targetPos);
    break;

  case AttackState::RECOVERING:
    // After ranged attack, check if we need to reposition
    if (tooClose) {
      changeState(state, AttackState::RETREATING);
    } else {
      updateRecovering(state);
    }
    break;

  case AttackState::RETREATING:
    // Ranged retreat: back off to optimal range
    if (distance >= m_optimalRange) {
      changeState(state, AttackState::POSITIONING);
    } else {
      Vector2D const awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D const retreatPos = entityPos + awayFromTarget * (m_minimumRange);
      moveToPosition(entity, retreatPos, m_movementSpeed, deltaTime);
    }
    break;

  case AttackState::COOLDOWN:
    updateCooldown(state);
    break;
  }
}

void AttackBehavior::updateChargeAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  if (shouldCharge(state.targetDistance, state) && !state.isCharging) {
    state.isCharging = true;
    state.attackChargeTime = 0.0f;
  }

  if (state.isCharging) {
    // Charge towards target at high speed
    moveToPosition(entity, targetPos,
                   m_movementSpeed * CHARGE_SPEED_MULTIPLIER, deltaTime);

    // Check if charge is complete or target reached
    if (state.targetDistance <= m_minimumRange) {
      executeAttack(entity, targetPos, state);
      state.isCharging = false;
      changeState(state, AttackState::RECOVERING);
    }
  } else {
    updateMeleeAttack(entity, state, deltaTime, targetPos);
  }
}

void AttackBehavior::updateAmbushAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  // Wait for optimal moment to strike
  if (state.currentState == AttackState::POSITIONING &&
      state.targetDistance <= m_optimalRange) {
    // Ambush when target is close
    executeAttack(entity, targetPos, state);
    changeState(state, AttackState::RECOVERING);
  } else {
    updateMeleeAttack(entity, state, deltaTime, targetPos);
  }
}

void AttackBehavior::updateCoordinatedAttack(EntityPtr entity,
                                             EntityState &state, float deltaTime, const Vector2D& targetPos) {
  if (m_teamwork) {
    coordinateWithTeam(state);
  }
  updateMeleeAttack(entity, state, deltaTime, targetPos);
}

void AttackBehavior::updateHitAndRun(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  // After attacking, immediately retreat
  if (state.currentState == AttackState::RECOVERING) {
    changeState(state, AttackState::RETREATING);
  }

  updateMeleeAttack(entity, state, deltaTime, targetPos);
}

void AttackBehavior::updateBerserkerAttack(EntityPtr entity,
                                           EntityState &state, float deltaTime, const Vector2D& targetPos) {
  // Aggressive continuous attacks with reduced cooldown
  if (state.currentState == AttackState::COOLDOWN) {
    float const timeInState = state.stateChangeTimer;
    if (timeInState > (m_attackCooldown * 0.5f)) { // Half cooldown
      changeState(state, AttackState::APPROACHING);
    }
  }

  updateMeleeAttack(entity, state, deltaTime, targetPos);
}

void AttackBehavior::updateSeeking(EntityState &state) {
  if (state.hasTarget && state.targetDistance <= m_attackRange * 1.5f) {
    changeState(state, AttackState::APPROACHING);
  }
}

void AttackBehavior::updateApproaching(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  if (state.targetDistance <= m_optimalRange) {
    changeState(state, AttackState::POSITIONING);
  } else {
    moveToPosition(entity, targetPos, m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::updatePositioning(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos) {
  Vector2D currentPos = entity->getPosition();
  Vector2D optimalPos = calculateOptimalAttackPosition(currentPos, targetPos, state);

  if ((currentPos - optimalPos).length() > 15.0f) {
    moveToPosition(entity, optimalPos, m_movementSpeed, deltaTime);
  } else if (state.canAttack) {
    changeState(state, AttackState::ATTACKING);
  }
}

void AttackBehavior::updateAttacking(EntityPtr entity, EntityState &state, const Vector2D& targetPos) {
  // Execute the attack
  if (m_specialRoll(m_rng) < m_specialAttackChance &&
      state.specialAttackReady) {
    executeSpecialAttack(entity, targetPos, state);
  } else if (m_comboAttacks) {
    executeComboAttack(entity, targetPos, state);
  } else {
    executeAttack(entity, targetPos, state);
  }

  changeState(state, AttackState::RECOVERING);
}

void AttackBehavior::updateRecovering(EntityState & /*state*/) {
  // Stay in place during recovery
  // State transition handled by updateStateTimer
}

void AttackBehavior::updateRetreating(EntityPtr entity, EntityState &state, const Vector2D& targetPos) {
  // Move away from target
  Vector2D entityPos = entity->getPosition();
  Vector2D retreatDir = normalizeDirection(entityPos - targetPos);

  Vector2D retreatVelocity =
      retreatDir * (m_movementSpeed * RETREAT_SPEED_MULTIPLIER);
  entity->setVelocity(retreatVelocity);

  // Stop retreating if far enough or health recovered
  if (state.targetDistance > m_attackRange * 2.0f || !shouldRetreat(state)) {
    state.isRetreating = false;
    changeState(state, AttackState::SEEKING);
  }
}

void AttackBehavior::updateCooldown(EntityState & /*state*/) {
  // Wait during cooldown
  // State transition handled by updateStateTimer
}

void AttackBehavior::moveToPosition(EntityPtr entity, const Vector2D &targetPos,
                                    float speed, float /*deltaTime*/) {
  if (!entity || speed <= 0.0f)
    return;

  // Direct velocity-based movement for EntityPtr version
  // This is legacy - the hot path uses BehaviorContext::transform.velocity
  Vector2D direction = targetPos - entity->getPosition();
  float distance = direction.length();
  if (distance > 5.0f) {
    direction = direction * (1.0f / distance);
    entity->setVelocity(direction * speed);
  } else {
    entity->setVelocity(Vector2D(0, 0));
  }
}

void AttackBehavior::maintainDistance(EntityPtr entity, const Vector2D& targetPos,
                                      float desiredDistance, float deltaTime) {
  if (!entity)
    return;

  Vector2D entityPos = entity->getPosition();

  // PERFORMANCE: Use squared distance for comparison
  float const currentDistanceSquared = (entityPos - targetPos).lengthSquared();
  float const desiredDistanceSquared = desiredDistance * desiredDistance;
  float const toleranceSquared = 100.0f; // 10.0f * 10.0f

  float difference = std::abs(currentDistanceSquared - desiredDistanceSquared);
  if (difference > toleranceSquared) {
    Vector2D direction = normalizeDirection(entityPos - targetPos);
    Vector2D const desiredPos = targetPos + direction * desiredDistance;
    moveToPosition(entity, desiredPos, m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::circleStrafe(EntityPtr entity, const Vector2D& targetPos,
                                  EntityState &state, float deltaTime) {
  if (!entity || !m_circleStrafe)
    return;

  if (state.strafeTimer >= STRAFE_INTERVAL) {
    state.strafeDirectionInt *= -1; // Change direction
    state.strafeTimer = 0.0f;
  }

  Vector2D entityPos = entity->getPosition();
  Vector2D strafePos = calculateStrafePosition(entityPos, targetPos, state);
  moveToPosition(entity, strafePos, m_movementSpeed, deltaTime);
}

void AttackBehavior::performFlankingManeuver(EntityPtr entity, const Vector2D& targetPos,
                                             EntityState &state, float deltaTime) {
  if (!entity || !m_flankingEnabled)
    return;

  Vector2D entityPos = entity->getPosition();
  Vector2D flankPos = calculateFlankingPosition(entityPos, targetPos);
  moveToPosition(entity, flankPos, m_movementSpeed, deltaTime);
  state.flanking = true;
}

// Utility methods removed - now using base class implementations

bool AttackBehavior::isValidAttackPosition(const Vector2D &position,
                                           const Vector2D& targetPos) const {
  // PERFORMANCE: Use squared distance
  float distanceSquared = (position - targetPos).lengthSquared();
  float const minRangeSquared = m_minimumRange * m_minimumRange;
  float const maxRangeSquared = m_attackRange * m_attackRange;
  return distanceSquared >= minRangeSquared && distanceSquared <= maxRangeSquared;
}

float AttackBehavior::calculateEffectiveRange(const EntityState &state) const {
  float effectiveRange = m_attackRange;

  // Modify range based on state
  if (state.isCharging) {
    effectiveRange *= 1.2f;
  }

  if (state.currentCombo > 0) {
    effectiveRange *= (1.0f + state.currentCombo * 0.1f);
  }

  return effectiveRange;
}

float AttackBehavior::calculateAttackSuccessChance(
    const Vector2D& /*entityPos*/, const Vector2D& /*targetPos*/, const EntityState &state) const {
  float baseChance = 0.8f; // 80% base hit chance

  // Modify based on distance
  float const distance = state.targetDistance;
  if (distance > m_optimalRange) {
    baseChance *= (m_attackRange - distance) / (m_attackRange - m_optimalRange);
  }

  // Modify based on combo
  if (state.currentCombo > 0) {
    baseChance += state.currentCombo * 0.05f;
  }

  return std::clamp(baseChance, 0.0f, 1.0f);
}

Vector2D AttackBehavior::calculateKnockbackVector(const Vector2D& attackerPos,
                                                  const Vector2D& targetPos) const {
  return normalizeDirection(targetPos - attackerPos);
}

void AttackBehavior::coordinateWithTeam(const EntityState &state) {
  // In a full implementation, this would coordinate with nearby allies
  // For now, we just broadcast coordination messages
  if (state.inCombat && state.hasTarget) {
    AIManager::Instance().broadcastMessage("coordinate_attack", false);
  }
}

bool AttackBehavior::isFriendlyFireRisk(const Vector2D& /*entityPos*/,
                                        const Vector2D& /*targetPos*/) const {
  if (!m_avoidFriendlyFire)
    return false;

  // In a full implementation, this would check for allies in the line of fire
  // For now, return false (no friendly fire risk)
  return false;
}
