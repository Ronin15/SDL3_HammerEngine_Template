/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/AttackBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include <algorithm>

AttackBehavior::AttackBehavior(float attackRange, float attackDamage,
                               float attackSpeed)
    : m_attackRange(attackRange), m_attackDamage(attackDamage),
      m_attackSpeed(attackSpeed) {
  // Entity state now stored in EDM BehaviorData - no local allocation needed
  m_optimalRange = attackRange * 0.8f;
  m_minimumRange = attackRange * 0.4f;
}

void AttackBehavior::applyConfig(
    const HammerEngine::AttackBehaviorConfig &config) {
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
  // Entity state now stored in EDM BehaviorData - no local allocation needed
  // Create mode-specific configuration
  HammerEngine::AttackBehaviorConfig config;

  switch (mode) {
  case AttackMode::MELEE_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createMeleeConfig(attackRange);
    break;
  case AttackMode::RANGED_ATTACK:
    config =
        HammerEngine::AttackBehaviorConfig::createRangedConfig(attackRange);
    break;
  case AttackMode::CHARGE_ATTACK:
    config =
        HammerEngine::AttackBehaviorConfig::createChargeConfig(attackRange);
    break;
  case AttackMode::AMBUSH_ATTACK:
    config =
        HammerEngine::AttackBehaviorConfig::createAmbushConfig(attackRange);
    break;
  case AttackMode::COORDINATED_ATTACK:
    config = HammerEngine::AttackBehaviorConfig::createCoordinatedConfig(
        attackRange);
    break;
  case AttackMode::HIT_AND_RUN:
    config =
        HammerEngine::AttackBehaviorConfig::createHitAndRunConfig(attackRange);
    break;
  case AttackMode::BERSERKER_ATTACK:
    config =
        HammerEngine::AttackBehaviorConfig::createBerserkerConfig(attackRange);
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

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  // Initialize behavior data in EDM (pre-allocated alongside hotData)
  edm.initBehaviorData(idx, BehaviorType::Attack);
  auto &data = edm.getBehaviorData(idx);
  auto &attack = data.state.attack;

  // Initialize attack-specific state
  attack.lastTargetPosition = Vector2D(0, 0);
  attack.attackPosition = Vector2D(0, 0);
  attack.retreatPosition = Vector2D(0, 0);
  attack.strafeVector = Vector2D(0, 0);
  attack.currentState = 0; // AttackState::SEEKING
  attack.attackTimer = 0.0f;
  attack.stateChangeTimer = 0.0f;
  attack.damageTimer = 0.0f;
  attack.comboTimer = 0.0f;
  attack.strafeTimer = 0.0f;
  attack.currentHealth = 100.0f;
  attack.maxHealth = 100.0f;
  attack.currentStamina = 100.0f;
  attack.targetDistance = 0.0f;
  attack.attackChargeTime = 0.0f;
  attack.recoveryTimer = 0.0f;
  attack.currentCombo = 0;
  attack.attacksInCombo = 0;
  attack.strafeDirectionInt = 1;
  attack.preferredAttackAngle = 0.0f;
  attack.inCombat = false;
  attack.hasTarget = false;
  attack.isCharging = false;
  attack.isRetreating = false;
  attack.canAttack = true;
  attack.lastAttackHit = false;
  attack.specialAttackReady = false;
  attack.circleStrafing = false;
  attack.flanking = false;

  data.setInitialized(true);
}

void AttackBehavior::updateTimers(BehaviorData &data, float deltaTime) {
  auto &attack = data.state.attack;
  attack.attackTimer += deltaTime;
  attack.stateChangeTimer += deltaTime;
  attack.damageTimer += deltaTime;
  if (attack.comboTimer > 0.0f) {
    attack.comboTimer -= deltaTime;
  }
  attack.strafeTimer += deltaTime;
}

void AttackBehavior::updateTargetDistance(const Vector2D &entityPos,
                                          const Vector2D &targetPos,
                                          BehaviorData &data) {
  // PERFORMANCE: Store squared distance and compute actual distance only when
  // needed
  float distSquared = (entityPos - targetPos).lengthSquared();
  data.state.attack.targetDistance = std::sqrt(distSquared);
}

void AttackBehavior::updateCombatState(BehaviorData &data) {
  auto &attack = data.state.attack;
  if (!attack.inCombat &&
      attack.targetDistance <= m_attackRange * COMBAT_ENTER_RANGE_MULT) {
    attack.inCombat = true;
  } else if (attack.inCombat &&
             attack.targetDistance > m_attackRange * COMBAT_EXIT_RANGE_MULT) {
    attack.inCombat = false;
    attack.currentState = static_cast<uint8_t>(AttackState::SEEKING);
  }
}

void AttackBehavior::handleNoTarget(BehaviorData &data) {
  auto &attack = data.state.attack;
  attack.hasTarget = false;
  attack.inCombat = false;
  if (attack.currentState != static_cast<uint8_t>(AttackState::SEEKING)) {
    changeState(data, AttackState::SEEKING);
  }
}

void AttackBehavior::updateTargetTracking(const Vector2D &entityPos,
                                          BehaviorData &data,
                                          const Vector2D &targetPos,
                                          bool hasTarget) {
  if (hasTarget) {
    data.state.attack.hasTarget = true;
    data.state.attack.lastTargetPosition = targetPos;
    updateTargetDistance(entityPos, targetPos, data);
    updateCombatState(data);
  } else {
    handleNoTarget(data);
  }
}

void AttackBehavior::dispatchModeUpdate(size_t edmIndex, BehaviorData &data,
                                        float deltaTime,
                                        const Vector2D &targetPos) {
  switch (m_attackMode) {
  case AttackMode::MELEE_ATTACK:
    updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::RANGED_ATTACK:
    updateRangedAttack(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::CHARGE_ATTACK:
    updateChargeAttack(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::AMBUSH_ATTACK:
    updateAmbushAttack(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::COORDINATED_ATTACK:
    updateCoordinatedAttack(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::HIT_AND_RUN:
    updateHitAndRun(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackMode::BERSERKER_ATTACK:
    updateBerserkerAttack(edmIndex, data, deltaTime, targetPos);
    break;
  }
}

void AttackBehavior::executeLogic(BehaviorContext &ctx) {
  if (!isActive())
    return;

  // Use pre-fetched behavior data from context (avoids redundant EDM lookup)
  if (!ctx.behaviorData || !ctx.behaviorData->isValid()) {
    return;
  }
  auto &data = *ctx.behaviorData;
  auto &attack = data.state.attack;

  // Use cached player info from context (lock-free, cached once per frame)
  Vector2D targetPos = ctx.playerPosition;
  bool hasTarget = ctx.playerValid;

  Vector2D entityPos = ctx.transform.position;

  // Track state for animation notification
  uint8_t const previousState = attack.currentState;

  // Update all timers
  updateTimers(data, ctx.deltaTime);

  // Update target tracking and combat state
  updateTargetTracking(entityPos, data, targetPos, hasTarget);

  // Update state timer
  updateStateTimer(data);

  // Check for retreat conditions
  if (shouldRetreat(ctx.edmIndex, data) &&
      attack.currentState != static_cast<uint8_t>(AttackState::RETREATING)) {
    changeState(data, AttackState::RETREATING);
  }

  // Execute behavior based on attack mode
  if (hasTarget) {
    dispatchModeUpdate(ctx.edmIndex, data, ctx.deltaTime, targetPos);
  }

  // Notify animation state change if state changed
  if (attack.currentState != previousState) {
    notifyAnimationStateChange(ctx.edmIndex,
                               static_cast<AttackState>(attack.currentState),
                               targetPos);
  }
}

void AttackBehavior::clean(EntityHandle handle) {
  auto &edm = EntityDataManager::Instance();
  if (handle.isValid()) {
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      edm.clearBehaviorData(idx);
    }
  }
  // Bulk cleanup handled by EDM::prepareForStateTransition()
}

void AttackBehavior::onMessage(EntityHandle handle,
                               const std::string &message) {
  if (!handle.isValid())
    return;

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX)
    return;

  auto &data = edm.getBehaviorData(idx);
  if (!data.isValid())
    return;

  auto &attack = data.state.attack;

  if (message == "attack_target") {
    if (attack.canAttack && attack.hasTarget) {
      changeState(data, AttackState::ATTACKING);
    }
  } else if (message == "retreat") {
    changeState(data, AttackState::RETREATING);
  } else if (message == "stop_attack") {
    changeState(data, AttackState::SEEKING);
    attack.inCombat = false;
  } else if (message == "enable_combo") {
    m_comboAttacks = true;
  } else if (message == "disable_combo") {
    m_comboAttacks = false;
    attack.currentCombo = 0;
  } else if (message == "heal") {
    attack.currentHealth = attack.maxHealth;
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
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return false;
}

bool AttackBehavior::isAttacking() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return false;
}

bool AttackBehavior::canAttack() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return true;
}

AttackBehavior::AttackState AttackBehavior::getCurrentAttackState() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return AttackState::SEEKING;
}

AttackBehavior::AttackMode AttackBehavior::getAttackMode() const {
  return m_attackMode;
}

float AttackBehavior::getDistanceToTarget() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return -1.0f;
}

float AttackBehavior::getLastAttackTime() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return 0.0f;
}

int AttackBehavior::getCurrentCombo() const {
  // Note: Would require iterating EDM - return sensible default
  // Per-entity query should use EDM BehaviorData directly
  return 0;
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

bool AttackBehavior::isTargetInRange(const Vector2D &entityPos,
                                     const Vector2D &targetPos) const {
  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entityPos - targetPos).lengthSquared();
  float const attackRangeSquared = m_attackRange * m_attackRange;
  return distanceSquared <= attackRangeSquared;
}

bool AttackBehavior::isTargetInAttackRange(const Vector2D &entityPos,
                                           const Vector2D &targetPos,
                                           const BehaviorData &data) const {
  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entityPos - targetPos).lengthSquared();
  float effectiveRange = calculateEffectiveRange(data);
  float const effectiveRangeSquared = effectiveRange * effectiveRange;
  return distanceSquared <= effectiveRangeSquared;
}

float AttackBehavior::calculateDamage(const BehaviorData &data) const {
  const auto &attack = data.state.attack;
  float baseDamage = m_attackDamage;

  // Apply damage variation
  float variation = (m_damageRoll(m_rng) - 0.5f) * 2.0f * m_damageVariation;
  baseDamage *= (1.0f + variation);

  // Check for critical hit
  if (m_criticalRoll(m_rng) < m_criticalHitChance) {
    baseDamage *= m_criticalHitMultiplier;
  }

  // Apply combo multiplier
  if (m_comboAttacks && attack.currentCombo > 0) {
    float const comboMultiplier =
        1.0f + (attack.currentCombo * COMBO_DAMAGE_PER_LEVEL);
    baseDamage *= comboMultiplier;
  }

  // Apply charge multiplier if charging
  if (attack.isCharging) {
    baseDamage *= m_chargeDamageMultiplier;
  }

  return baseDamage;
}

Vector2D AttackBehavior::calculateOptimalAttackPosition(
    const Vector2D &entityPos, const Vector2D &targetPos,
    const BehaviorData & /*data*/) const {
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

Vector2D
AttackBehavior::calculateFlankingPosition(const Vector2D &entityPos,
                                          const Vector2D &targetPos) const {
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
AttackBehavior::calculateStrafePosition(const Vector2D &entityPos,
                                        const Vector2D &targetPos,
                                        const BehaviorData &data) const {
  // Calculate strafe direction (perpendicular to target direction)
  Vector2D toTarget = normalizeDirection(targetPos - entityPos);
  Vector2D const strafeDir = Vector2D(-toTarget.getY(), toTarget.getX()) *
                             data.state.attack.strafeDirectionInt;

  return entityPos + strafeDir * (m_movementSpeed * 2.0f);
}

void AttackBehavior::changeState(BehaviorData &data, AttackState newState) {
  auto &attack = data.state.attack;
  uint8_t newStateVal = static_cast<uint8_t>(newState);
  if (attack.currentState != newStateVal) {
    attack.currentState = newStateVal;
    attack.stateChangeTimer = 0.0f;

    // Reset state-specific flags
    switch (newState) {
    case AttackState::ATTACKING:
      attack.recoveryTimer = 0.0f;
      attack.canAttack = false; // Prevent re-attacking during attack cycle
      break;
    case AttackState::RECOVERING:
      attack.recoveryTimer = 0.0f;
      break;
    case AttackState::RETREATING:
      attack.isRetreating = true;
      break;
    default:
      attack.isRetreating = false;
      break;
    }
  }
}

void AttackBehavior::notifyAnimationStateChange(size_t edmIndex,
                                                AttackState newState,
                                                const Vector2D& targetPos) {
  // Data-driven NPCs: Animation handled by NPCRenderController via velocity
  // Apply velocity burst for lunge effect on attack states
  if (edmIndex == SIZE_MAX) {
    return;
  }

  // Lunge toward target when attacking (2x movement speed burst)
  if (newState == AttackState::ATTACKING) {
    auto& edm = EntityDataManager::Instance();
    // targetPos passed as parameter - avoids re-fetching from AIManager (thread-safe)
    Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
    Vector2D direction = (targetPos - entityPos);
    float dist = direction.length();
    if (dist > 1.0f) {
      direction = direction * (1.0f / dist);  // Normalize
      edm.getHotDataByIndex(edmIndex).transform.velocity = direction * (m_movementSpeed * CHARGE_SPEED_MULTIPLIER);
    }
  }
}

void AttackBehavior::updateStateTimer(BehaviorData &data) {
  const auto &attack = data.state.attack;
  float const timeInState = attack.stateChangeTimer;

  // Handle state transitions based on timing
  AttackState currentState = static_cast<AttackState>(attack.currentState);
  switch (currentState) {
  case AttackState::ATTACKING:
    if (timeInState > (1.0f / m_attackSpeed)) {
      changeState(data, AttackState::RECOVERING);
    }
    break;

  case AttackState::RECOVERING:
    if (timeInState > m_recoveryTime) {
      changeState(data, AttackState::COOLDOWN);
    }
    break;

  case AttackState::COOLDOWN:
    if (timeInState > m_attackCooldown) {
      data.state.attack.canAttack = true; // Reset attack capability after cooldown
      changeState(data, attack.hasTarget ? AttackState::APPROACHING
                                         : AttackState::SEEKING);
    }
    break;

  default:
    break;
  }
}

bool AttackBehavior::shouldRetreat(size_t edmIndex,
                                   const BehaviorData &data) const {
  // Read health from CharacterData (live EDM data) instead of stale BehaviorData copy
  if (edmIndex == SIZE_MAX) {
    return false;
  }
  auto &edm = EntityDataManager::Instance();
  const auto &charData = edm.getCharacterDataByIndex(edmIndex);
  float const healthRatio = charData.health / charData.maxHealth;
  return healthRatio <= m_retreatThreshold && m_aggression < 0.8f;
}

bool AttackBehavior::shouldCharge(float distance,
                                  const BehaviorData &data) const {
  if (m_attackMode != AttackMode::CHARGE_ATTACK || !data.state.attack.hasTarget)
    return false;

  return distance > m_optimalRange * CHARGE_DISTANCE_THRESHOLD_MULT &&
         distance <= m_attackRange;
}

void AttackBehavior::executeAttack(size_t edmIndex, const Vector2D &targetPos,
                                   BehaviorData &data) {
  if (edmIndex == SIZE_MAX)
    return;

  auto &edm = EntityDataManager::Instance();
  auto &attack = data.state.attack;
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;

  // Calculate damage
  float const damage = calculateDamage(data);

  // Calculate knockback
  Vector2D knockback = calculateKnockbackVector(entityPos, targetPos);
  knockback = knockback * m_knockbackForce;

  // Apply damage via handle-based system
  EntityHandle targetHandle = getTargetHandle();
  applyDamageToTarget(targetHandle, damage, knockback);

  // Update attack state
  attack.attackTimer = 0.0f;
  attack.lastAttackHit = true; // Simplified - assume all attacks hit

  // Handle combo system
  if (m_comboAttacks) {
    if (attack.comboTimer > 0.0f) {
      attack.currentCombo = std::min(attack.currentCombo + 1, m_maxCombo);
    } else {
      attack.currentCombo = 1;
      attack.comboTimer = COMBO_TIMEOUT;
    }
  }

  // Apply area of effect damage if enabled
  if (m_aoeRadius > 0.0f) {
    applyAreaOfEffectDamage(entityPos, targetPos, damage * 0.5f);
  }
}

void AttackBehavior::executeSpecialAttack(size_t edmIndex,
                                          const Vector2D &targetPos,
                                          BehaviorData &data) {
  if (edmIndex == SIZE_MAX)
    return;

  auto &edm = EntityDataManager::Instance();
  auto &attack = data.state.attack;
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;

  // Enhanced attack with special effects
  float const specialDamage = calculateDamage(data) * SPECIAL_ATTACK_MULTIPLIER;
  Vector2D knockback = calculateKnockbackVector(entityPos, targetPos) *
                       (m_knockbackForce * SPECIAL_ATTACK_MULTIPLIER);

  EntityHandle targetHandle = getTargetHandle();
  applyDamageToTarget(targetHandle, specialDamage, knockback);

  attack.attackTimer = 0.0f;
  attack.specialAttackReady = false;
}

void AttackBehavior::executeComboAttack(size_t edmIndex,
                                        const Vector2D &targetPos,
                                        BehaviorData &data) {
  auto &attack = data.state.attack;
  if (!m_comboAttacks || attack.currentCombo == 0) {
    executeAttack(edmIndex, targetPos, data);
    return;
  }

  if (edmIndex == SIZE_MAX)
    return;

  auto &edm = EntityDataManager::Instance();
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;

  // Combo finisher
  if (attack.currentCombo >= m_maxCombo) {
    float const comboDamage = calculateDamage(data) * COMBO_FINISHER_MULTIPLIER;
    Vector2D knockback = calculateKnockbackVector(entityPos, targetPos) *
                         (m_knockbackForce * COMBO_FINISHER_MULTIPLIER);

    EntityHandle targetHandle = getTargetHandle();
    applyDamageToTarget(targetHandle, comboDamage, knockback);

    // Reset combo
    attack.currentCombo = 0;
    attack.comboTimer = 0.0f;
  } else {
    executeAttack(edmIndex, targetPos, data);
  }
}

void AttackBehavior::applyDamageToTarget(EntityHandle targetHandle,
                                         float damage,
                                         const Vector2D &knockback) {
  if (!targetHandle.isValid()) {
    return;
  }

  auto &edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(targetHandle);
  if (idx == SIZE_MAX) {
    return;
  }

  // Scale knockback for visual effect
  Vector2D scaledKnockback = knockback * 0.1f;

  // Apply damage and knockback via EDM
  auto &hotData = edm.getHotDataByIndex(idx);
  auto &charData = edm.getCharacterData(targetHandle);

  charData.health = std::max(0.0f, charData.health - damage);
  hotData.transform.velocity = hotData.transform.velocity + scaledKnockback;

  // Check for death
  if (charData.health <= 0.0f) {
    hotData.flags &= ~EntityHotData::FLAG_ALIVE;
  }
}

void AttackBehavior::applyAreaOfEffectDamage(const Vector2D & /*entityPos*/,
                                             const Vector2D & /*targetPos*/,
                                             float /*damage*/) {
  // In a full implementation, this would find all entities within the AOE
  // radius and apply damage to them
}

void AttackBehavior::updateMeleeAttack(size_t edmIndex, BehaviorData &data,
                                       float deltaTime,
                                       const Vector2D &targetPos) {
  AttackState currentState =
      static_cast<AttackState>(data.state.attack.currentState);
  switch (currentState) {
  case AttackState::SEEKING:
    updateSeeking(data);
    break;
  case AttackState::APPROACHING:
    updateApproaching(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackState::POSITIONING:
    updatePositioning(edmIndex, data, deltaTime, targetPos);
    break;
  case AttackState::ATTACKING:
    updateAttacking(edmIndex, data, targetPos);
    break;
  case AttackState::RECOVERING:
    updateRecovering(data);
    break;
  case AttackState::RETREATING:
    updateRetreating(edmIndex, data, targetPos);
    break;
  case AttackState::COOLDOWN:
    updateCooldown(data);
    break;
  }
}

void AttackBehavior::updateRangedAttack(size_t edmIndex, BehaviorData &data,
                                        float deltaTime,
                                        const Vector2D &targetPos) {
  auto &edm = EntityDataManager::Instance();
  const auto &attack = data.state.attack;
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
  float const distance = attack.targetDistance;

  // Ranged attackers need to maintain distance - back off if too close
  bool const tooClose = distance < m_minimumRange;
  bool const inOptimalRange =
      distance >= m_minimumRange && distance <= m_optimalRange * 1.2f;

  AttackState currentState = static_cast<AttackState>(attack.currentState);
  switch (currentState) {
  case AttackState::SEEKING:
    updateSeeking(data);
    break;

  case AttackState::APPROACHING:
    // For ranged, approach but stop at optimal range
    if (distance <= m_optimalRange) {
      changeState(data, AttackState::POSITIONING);
    } else {
      moveToPosition(edmIndex, targetPos, m_movementSpeed, deltaTime);
    }
    break;

  case AttackState::POSITIONING:
    if (tooClose) {
      // Back off from target to maintain ranged distance
      Vector2D const awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D const retreatPos =
          entityPos + awayFromTarget * (m_optimalRange * 0.5f);
      moveToPosition(edmIndex, retreatPos, m_movementSpeed * 1.2f, deltaTime);
    } else if (inOptimalRange && attack.canAttack) {
      changeState(data, AttackState::ATTACKING);
    } else if (distance > m_optimalRange * 1.3f) {
      // Too far, approach slightly
      changeState(data, AttackState::APPROACHING);
    } else {
      // Circle strafe at optimal range to avoid being an easy target
      Vector2D const toTarget = (targetPos - entityPos).normalized();
      Vector2D const perpendicular(-toTarget.getY(), toTarget.getX());
      float const strafeDir = (attack.strafeDirectionInt > 0) ? 1.0f : -1.0f;
      Vector2D const strafePos =
          entityPos + perpendicular * (40.0f * strafeDir);
      moveToPosition(edmIndex, strafePos, m_movementSpeed * 0.6f, deltaTime);
    }
    break;

  case AttackState::ATTACKING:
    updateAttacking(edmIndex, data, targetPos);
    break;

  case AttackState::RECOVERING:
    // After ranged attack, check if we need to reposition
    if (tooClose) {
      changeState(data, AttackState::RETREATING);
    } else {
      updateRecovering(data);
    }
    break;

  case AttackState::RETREATING:
    // Ranged retreat: back off to optimal range
    if (distance >= m_optimalRange) {
      changeState(data, AttackState::POSITIONING);
    } else {
      Vector2D const awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D const retreatPos = entityPos + awayFromTarget * (m_minimumRange);
      moveToPosition(edmIndex, retreatPos, m_movementSpeed, deltaTime);
    }
    break;

  case AttackState::COOLDOWN:
    updateCooldown(data);
    break;
  }
}

void AttackBehavior::updateChargeAttack(size_t edmIndex, BehaviorData &data,
                                        float deltaTime,
                                        const Vector2D &targetPos) {
  auto &attack = data.state.attack;
  if (shouldCharge(attack.targetDistance, data) && !attack.isCharging) {
    attack.isCharging = true;
    attack.attackChargeTime = 0.0f;
  }

  if (attack.isCharging) {
    // Charge towards target at high speed
    moveToPosition(edmIndex, targetPos, m_movementSpeed * CHARGE_SPEED_MULTIPLIER,
                   deltaTime);

    // Check if charge is complete or target reached
    if (attack.targetDistance <= m_minimumRange) {
      executeAttack(edmIndex, targetPos, data);
      attack.isCharging = false;
      changeState(data, AttackState::RECOVERING);
    }
  } else {
    updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
  }
}

void AttackBehavior::updateAmbushAttack(size_t edmIndex, BehaviorData &data,
                                        float deltaTime,
                                        const Vector2D &targetPos) {
  const auto &attack = data.state.attack;
  // Wait for optimal moment to strike
  if (attack.currentState == static_cast<uint8_t>(AttackState::POSITIONING) &&
      attack.targetDistance <= m_optimalRange) {
    // Ambush when target is close
    executeAttack(edmIndex, targetPos, data);
    changeState(data, AttackState::RECOVERING);
  } else {
    updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
  }
}

void AttackBehavior::updateCoordinatedAttack(size_t edmIndex,
                                             BehaviorData &data,
                                             float deltaTime,
                                             const Vector2D &targetPos) {
  if (m_teamwork) {
    coordinateWithTeam(data);
  }
  updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
}

void AttackBehavior::updateHitAndRun(size_t edmIndex, BehaviorData &data,
                                     float deltaTime,
                                     const Vector2D &targetPos) {
  const auto &attack = data.state.attack;
  // After attacking, immediately retreat
  if (attack.currentState == static_cast<uint8_t>(AttackState::RECOVERING)) {
    changeState(data, AttackState::RETREATING);
  }

  updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
}

void AttackBehavior::updateBerserkerAttack(size_t edmIndex, BehaviorData &data,
                                           float deltaTime,
                                           const Vector2D &targetPos) {
  const auto &attack = data.state.attack;
  // Aggressive continuous attacks with reduced cooldown
  if (attack.currentState == static_cast<uint8_t>(AttackState::COOLDOWN)) {
    float const timeInState = attack.stateChangeTimer;
    if (timeInState > (m_attackCooldown * 0.5f)) { // Half cooldown
      changeState(data, AttackState::APPROACHING);
    }
  }

  updateMeleeAttack(edmIndex, data, deltaTime, targetPos);
}

void AttackBehavior::updateSeeking(BehaviorData &data) {
  const auto &attack = data.state.attack;
  // If we have a target, always transition to approaching (chase them)
  if (attack.hasTarget) {
    changeState(data, AttackState::APPROACHING);
  }
}

void AttackBehavior::updateApproaching(size_t edmIndex, BehaviorData &data,
                                       float deltaTime,
                                       const Vector2D &targetPos) {
  if (data.state.attack.targetDistance <= m_optimalRange) {
    changeState(data, AttackState::POSITIONING);
  } else {
    moveToPosition(edmIndex, targetPos, m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::updatePositioning(size_t edmIndex, BehaviorData &data,
                                       float deltaTime,
                                       const Vector2D &targetPos) {
  auto &edm = EntityDataManager::Instance();
  Vector2D currentPos = edm.getHotDataByIndex(edmIndex).transform.position;
  Vector2D optimalPos =
      calculateOptimalAttackPosition(currentPos, targetPos, data);

  if ((currentPos - optimalPos).length() > 15.0f) {
    moveToPosition(edmIndex, optimalPos, m_movementSpeed, deltaTime);
  } else if (data.state.attack.canAttack) {
    changeState(data, AttackState::ATTACKING);
  }
}

void AttackBehavior::updateAttacking(size_t edmIndex, BehaviorData &data,
                                     const Vector2D &targetPos) {
  // Execute the attack
  if (m_specialRoll(m_rng) < m_specialAttackChance &&
      data.state.attack.specialAttackReady) {
    executeSpecialAttack(edmIndex, targetPos, data);
  } else if (m_comboAttacks) {
    executeComboAttack(edmIndex, targetPos, data);
  } else {
    executeAttack(edmIndex, targetPos, data);
  }

  changeState(data, AttackState::RECOVERING);
}

void AttackBehavior::updateRecovering(BehaviorData & /*data*/) {
  // Stay in place during recovery
  // State transition handled by updateStateTimer
}

void AttackBehavior::updateRetreating(size_t edmIndex, BehaviorData &data,
                                      const Vector2D &targetPos) {
  auto &edm = EntityDataManager::Instance();
  auto &attack = data.state.attack;
  // Move away from target
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
  Vector2D retreatDir = normalizeDirection(entityPos - targetPos);

  Vector2D retreatVelocity =
      retreatDir * (m_movementSpeed * RETREAT_SPEED_MULTIPLIER);
  edm.getHotDataByIndex(edmIndex).transform.velocity = retreatVelocity;

  // Stop retreating if far enough or health recovered
  if (attack.targetDistance > m_attackRange * 2.0f ||
      !shouldRetreat(edmIndex, data)) {
    attack.isRetreating = false;
    changeState(data, AttackState::SEEKING);
  }
}

void AttackBehavior::updateCooldown(BehaviorData & /*data*/) {
  // Wait during cooldown
  // State transition handled by updateStateTimer
}

void AttackBehavior::moveToPosition(size_t edmIndex, const Vector2D &targetPos,
                                    float speed, float /*deltaTime*/) {
  if (edmIndex == SIZE_MAX || speed <= 0.0f)
    return;

  auto &edm = EntityDataManager::Instance();
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
  Vector2D direction = targetPos - entityPos;
  float distance = direction.length();
  if (distance > 5.0f) {
    direction = direction * (1.0f / distance);
    edm.getHotDataByIndex(edmIndex).transform.velocity = direction * speed;
  } else {
    edm.getHotDataByIndex(edmIndex).transform.velocity = Vector2D(0, 0);
  }
}

void AttackBehavior::maintainDistance(size_t edmIndex,
                                      const Vector2D &targetPos,
                                      float desiredDistance, float deltaTime) {
  if (edmIndex == SIZE_MAX)
    return;

  auto &edm = EntityDataManager::Instance();
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;

  // PERFORMANCE: Use squared distance for comparison
  float const currentDistanceSquared = (entityPos - targetPos).lengthSquared();
  float const desiredDistanceSquared = desiredDistance * desiredDistance;
  float const toleranceSquared = 100.0f; // 10.0f * 10.0f

  float difference = std::abs(currentDistanceSquared - desiredDistanceSquared);
  if (difference > toleranceSquared) {
    Vector2D direction = normalizeDirection(entityPos - targetPos);
    Vector2D const desiredPos = targetPos + direction * desiredDistance;
    moveToPosition(edmIndex, desiredPos, m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::circleStrafe(size_t edmIndex, const Vector2D &targetPos,
                                  BehaviorData &data, float deltaTime) {
  if (edmIndex == SIZE_MAX || !m_circleStrafe)
    return;

  auto &edm = EntityDataManager::Instance();
  auto &attack = data.state.attack;
  if (attack.strafeTimer >= STRAFE_INTERVAL) {
    attack.strafeDirectionInt *= -1; // Change direction
    attack.strafeTimer = 0.0f;
  }

  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
  Vector2D strafePos = calculateStrafePosition(entityPos, targetPos, data);
  moveToPosition(edmIndex, strafePos, m_movementSpeed, deltaTime);
}

void AttackBehavior::performFlankingManeuver(size_t edmIndex,
                                             const Vector2D &targetPos,
                                             BehaviorData &data,
                                             float deltaTime) {
  if (edmIndex == SIZE_MAX || !m_flankingEnabled)
    return;

  auto &edm = EntityDataManager::Instance();
  Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;
  Vector2D flankPos = calculateFlankingPosition(entityPos, targetPos);
  moveToPosition(edmIndex, flankPos, m_movementSpeed, deltaTime);
  data.state.attack.flanking = true;
}

// Utility methods removed - now using base class implementations

bool AttackBehavior::isValidAttackPosition(const Vector2D &position,
                                           const Vector2D &targetPos) const {
  // PERFORMANCE: Use squared distance
  float distanceSquared = (position - targetPos).lengthSquared();
  float const minRangeSquared = m_minimumRange * m_minimumRange;
  float const maxRangeSquared = m_attackRange * m_attackRange;
  return distanceSquared >= minRangeSquared &&
         distanceSquared <= maxRangeSquared;
}

float AttackBehavior::calculateEffectiveRange(const BehaviorData &data) const {
  const auto &attack = data.state.attack;
  float effectiveRange = m_attackRange;

  // Modify range based on state
  if (attack.isCharging) {
    effectiveRange *= 1.2f;
  }

  if (attack.currentCombo > 0) {
    effectiveRange *= (1.0f + attack.currentCombo * 0.1f);
  }

  return effectiveRange;
}

float AttackBehavior::calculateAttackSuccessChance(
    const Vector2D & /*entityPos*/, const Vector2D & /*targetPos*/,
    const BehaviorData &data) const {
  const auto &attack = data.state.attack;
  float baseChance = 0.8f; // 80% base hit chance

  // Modify based on distance
  float const distance = attack.targetDistance;
  if (distance > m_optimalRange) {
    baseChance *= (m_attackRange - distance) / (m_attackRange - m_optimalRange);
  }

  // Modify based on combo
  if (attack.currentCombo > 0) {
    baseChance += attack.currentCombo * 0.05f;
  }

  return std::clamp(baseChance, 0.0f, 1.0f);
}

Vector2D
AttackBehavior::calculateKnockbackVector(const Vector2D &attackerPos,
                                         const Vector2D &targetPos) const {
  return normalizeDirection(targetPos - attackerPos);
}

void AttackBehavior::coordinateWithTeam(const BehaviorData &data) {
  // In a full implementation, this would coordinate with nearby allies
  // For now, we just broadcast coordination messages
  const auto &attack = data.state.attack;
  if (attack.inCombat && attack.hasTarget) {
    AIManager::Instance().broadcastMessage("coordinate_attack", false);
  }
}

bool AttackBehavior::isFriendlyFireRisk(const Vector2D & /*entityPos*/,
                                        const Vector2D & /*targetPos*/) const {
  if (!m_avoidFriendlyFire)
    return false;

  // In a full implementation, this would check for allies in the line of fire
  // For now, return false (no friendly fire risk)
  return false;
}
