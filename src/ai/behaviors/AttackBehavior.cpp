/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/AttackBehavior.hpp"
#include "entities/NPC.hpp"
#include "entities/Player.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
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

void AttackBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  auto &state = m_entityStates[entity];
  state = EntityState(); // Reset to default state
  state.currentState = AttackState::SEEKING;
  state.stateChangeTimer = 0.0f;
  state.currentHealth = state.maxHealth;
  state.currentStamina = 100.0f;
  state.canAttack = true;

  // Set initial animation state
  notifyAnimationStateChange(entity, state.currentState);
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

AttackBehavior::EntityState& AttackBehavior::ensureEntityState(EntityPtr entity) {
  auto it = m_entityStates.find(entity);
  if (it == m_entityStates.end()) {
    init(entity);
    it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) {
      // This should never happen, but provide fallback
      AI_ERROR("Failed to initialize entity state for AttackBehavior");
      static EntityState fallbackState;
      return fallbackState;
    }
  }
  return it->second;
}

void AttackBehavior::updateTargetDistance(EntityPtr entity, EntityPtr target, EntityState& state) {
  // PERFORMANCE: Store squared distance and compute actual distance only when needed
  float distSquared = (entity->getPosition() - target->getPosition()).lengthSquared();
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

void AttackBehavior::updateTargetTracking(EntityPtr entity, EntityState& state, EntityPtr target) {
  if (target) {
    state.hasTarget = true;
    state.lastTargetPosition = target->getPosition();
    updateTargetDistance(entity, target, state);
    updateCombatState(state);
  } else {
    handleNoTarget(state);
  }
}

void AttackBehavior::dispatchModeUpdate(EntityPtr entity, EntityState& state, float deltaTime) {
  switch (m_attackMode) {
  case AttackMode::MELEE_ATTACK:
    updateMeleeAttack(entity, state, deltaTime);
    break;
  case AttackMode::RANGED_ATTACK:
    updateRangedAttack(entity, state, deltaTime);
    break;
  case AttackMode::CHARGE_ATTACK:
    updateChargeAttack(entity, state, deltaTime);
    break;
  case AttackMode::AMBUSH_ATTACK:
    updateAmbushAttack(entity, state, deltaTime);
    break;
  case AttackMode::COORDINATED_ATTACK:
    updateCoordinatedAttack(entity, state, deltaTime);
    break;
  case AttackMode::HIT_AND_RUN:
    updateHitAndRun(entity, state, deltaTime);
    break;
  case AttackMode::BERSERKER_ATTACK:
    updateBerserkerAttack(entity, state, deltaTime);
    break;
  }
}

void AttackBehavior::executeLogic(EntityPtr entity, float deltaTime) {
  if (!entity || !isActive())
    return;

  EntityState &state = ensureEntityState(entity);
  EntityPtr target = getTarget();

  // Track state for animation notification
  AttackState previousState = state.currentState;

  // Update all timers
  updateTimers(state, deltaTime);

  // Update target tracking and combat state
  updateTargetTracking(entity, state, target);

  // Update state timer
  updateStateTimer(state);

  // Check for retreat conditions
  if (shouldRetreat(state) && state.currentState != AttackState::RETREATING) {
    changeState(state, AttackState::RETREATING);
  }

  // Execute behavior based on attack mode
  dispatchModeUpdate(entity, state, deltaTime);

  // Notify animation state change if state changed
  if (state.currentState != previousState) {
    notifyAnimationStateChange(entity, state.currentState);
  }
}

void AttackBehavior::clean(EntityPtr entity) {
  if (entity) {
    m_entityStates.erase(entity);
  }
}

void AttackBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (!entity)
    return;

  auto it = m_entityStates.find(entity);
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

EntityPtr AttackBehavior::getTarget() const {
  return AIManager::Instance().getPlayerReference();
}

bool AttackBehavior::isTargetInRange(EntityPtr entity, EntityPtr target) const {
  if (!entity || !target)
    return false;

  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entity->getPosition() - target->getPosition()).lengthSquared();
  float attackRangeSquared = m_attackRange * m_attackRange;
  return distanceSquared <= attackRangeSquared;
}

bool AttackBehavior::isTargetInAttackRange(EntityPtr entity,
                                           EntityPtr target) const {
  if (!entity || !target)
    return false;

  // PERFORMANCE: Use squared distance to avoid sqrt
  float distanceSquared = (entity->getPosition() - target->getPosition()).lengthSquared();
  float effectiveRange = calculateEffectiveRange(m_entityStates.at(entity));
  float effectiveRangeSquared = effectiveRange * effectiveRange;
  return distanceSquared <= effectiveRangeSquared;
}

bool AttackBehavior::canReachTarget(EntityPtr entity, EntityPtr target) const {
  // Simplified - in a full implementation, this would check pathfinding
  return isTargetInRange(entity, target);
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
    float comboMultiplier = 1.0f + (state.currentCombo * COMBO_DAMAGE_PER_LEVEL);
    baseDamage *= comboMultiplier;
  }

  // Apply charge multiplier if charging
  if (state.isCharging) {
    baseDamage *= m_chargeDamageMultiplier;
  }

  return baseDamage;
}

Vector2D AttackBehavior::calculateOptimalAttackPosition(
    EntityPtr entity, EntityPtr target, const EntityState & /*state*/) const {
  if (!entity || !target)
    return Vector2D(0, 0);

  Vector2D targetPos = target->getPosition();
  Vector2D entityPos = entity->getPosition();

  // Calculate direction from target to optimal position
  Vector2D direction = normalizeDirection(entityPos - targetPos);

  // Apply preferred attack angle if set
  if (m_preferredAttackAngle != 0.0f) {
    direction = rotateVector(direction, m_preferredAttackAngle);
  }

  // Calculate optimal position
  Vector2D optimalPos = targetPos + direction * m_optimalRange;

  return optimalPos;
}

Vector2D AttackBehavior::calculateFlankingPosition(EntityPtr entity,
                                                   EntityPtr target) const {
  if (!entity || !target)
    return Vector2D(0, 0);

  Vector2D targetPos = target->getPosition();
  Vector2D entityPos = entity->getPosition();

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
AttackBehavior::calculateStrafePosition(EntityPtr entity, EntityPtr target,
                                        const EntityState &state) const {
  if (!entity || !target)
    return Vector2D(0, 0);

  Vector2D targetPos = target->getPosition();
  Vector2D entityPos = entity->getPosition();

  // Calculate strafe direction (perpendicular to target direction)
  Vector2D toTarget = normalizeDirection(targetPos - entityPos);
  Vector2D strafeDir =
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

  float timeInState = state.stateChangeTimer;

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
  float healthRatio = state.currentHealth / state.maxHealth;
  return healthRatio <= m_retreatThreshold && m_aggression < 0.8f;
}

bool AttackBehavior::shouldCharge(EntityPtr entity, EntityPtr target,
                                  const EntityState &state) const {
  if (!entity || !target || m_attackMode != AttackMode::CHARGE_ATTACK)
    return false;

  float distance = state.targetDistance;
  return distance > m_optimalRange * CHARGE_DISTANCE_THRESHOLD_MULT && distance <= m_attackRange;
}

void AttackBehavior::executeAttack(EntityPtr entity, EntityPtr target,
                                   EntityState &state) {
  if (!entity || !target)
    return;

  // Calculate damage
  float damage = calculateDamage(state);

  // Calculate knockback
  Vector2D knockback = calculateKnockbackVector(entity, target);
  knockback = knockback * m_knockbackForce;

  // Apply damage (in a full implementation, this would interact with a damage
  // system)
  applyDamage(target, damage, knockback);

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
    applyAreaOfEffectDamage(entity, target, damage * 0.5f);
  }
}

void AttackBehavior::executeSpecialAttack(EntityPtr entity, EntityPtr target,
                                          EntityState &state) {
  // Enhanced attack with special effects
  float specialDamage = calculateDamage(state) * SPECIAL_ATTACK_MULTIPLIER;
  Vector2D knockback =
      calculateKnockbackVector(entity, target) * (m_knockbackForce * SPECIAL_ATTACK_MULTIPLIER);

  applyDamage(target, specialDamage, knockback);

  state.attackTimer = 0.0f;
  state.specialAttackReady = false;
}

void AttackBehavior::executeComboAttack(EntityPtr entity, EntityPtr target,
                                        EntityState &state) {
  if (!m_comboAttacks || state.currentCombo == 0) {
    executeAttack(entity, target, state);
    return;
  }

  // Combo finisher
  if (state.currentCombo >= m_maxCombo) {
    float comboDamage = calculateDamage(state) * COMBO_FINISHER_MULTIPLIER;
    Vector2D knockback =
        calculateKnockbackVector(entity, target) * (m_knockbackForce * COMBO_FINISHER_MULTIPLIER);

    applyDamage(target, comboDamage, knockback);

    // Reset combo
    state.currentCombo = 0;
    state.comboTimer = 0.0f;
  } else {
    executeAttack(entity, target, state);
  }
}

void AttackBehavior::applyDamage(EntityPtr target, float damage,
                                 const Vector2D &knockback) {
  if (!target) {
    return;
  }

  // Scale knockback for visual effect
  Vector2D scaledKnockback = knockback * 0.1f;

  // Try to apply damage to NPC
  auto npc = std::dynamic_pointer_cast<NPC>(target);
  if (npc) {
    npc->takeDamage(damage, scaledKnockback);
    return;
  }

  // Try to apply damage to Player
  auto player = std::dynamic_pointer_cast<Player>(target);
  if (player) {
    player->takeDamage(damage, scaledKnockback);
  }
}

void AttackBehavior::applyAreaOfEffectDamage(EntityPtr /*entity*/,
                                             EntityPtr /*target*/,
                                             float /*damage*/) {
  // In a full implementation, this would find all entities within the AOE
  // radius and apply damage to them
}

void AttackBehavior::updateMeleeAttack(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  switch (state.currentState) {
  case AttackState::SEEKING:
    updateSeeking(entity, state);
    break;
  case AttackState::APPROACHING:
    updateApproaching(entity, state, deltaTime);
    break;
  case AttackState::POSITIONING:
    updatePositioning(entity, state, deltaTime);
    break;
  case AttackState::ATTACKING:
    updateAttacking(entity, state);
    break;
  case AttackState::RECOVERING:
    updateRecovering(entity, state);
    break;
  case AttackState::RETREATING:
    updateRetreating(entity, state);
    break;
  case AttackState::COOLDOWN:
    updateCooldown(entity, state);
    break;
  }
}

void AttackBehavior::updateRangedAttack(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  float distance = state.targetDistance;

  // Ranged attackers need to maintain distance - back off if too close
  bool tooClose = distance < m_minimumRange;
  bool inOptimalRange = distance >= m_minimumRange && distance <= m_optimalRange * 1.2f;

  switch (state.currentState) {
  case AttackState::SEEKING:
    updateSeeking(entity, state);
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
      Vector2D awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D retreatPos = entityPos + awayFromTarget * (m_optimalRange * 0.5f);
      moveToPosition(entity, retreatPos, m_movementSpeed * 1.2f, deltaTime);
    } else if (inOptimalRange && state.canAttack) {
      changeState(state, AttackState::ATTACKING);
    } else if (distance > m_optimalRange * 1.3f) {
      // Too far, approach slightly
      changeState(state, AttackState::APPROACHING);
    } else {
      // Circle strafe at optimal range to avoid being an easy target
      Vector2D toTarget = (targetPos - entityPos).normalized();
      Vector2D perpendicular(-toTarget.getY(), toTarget.getX());
      float strafeDir = (state.strafeDirectionInt > 0) ? 1.0f : -1.0f;
      Vector2D strafePos = entityPos + perpendicular * (40.0f * strafeDir);
      moveToPosition(entity, strafePos, m_movementSpeed * 0.6f, deltaTime);
    }
    break;

  case AttackState::ATTACKING:
    updateAttacking(entity, state);
    break;

  case AttackState::RECOVERING:
    // After ranged attack, check if we need to reposition
    if (tooClose) {
      changeState(state, AttackState::RETREATING);
    } else {
      updateRecovering(entity, state);
    }
    break;

  case AttackState::RETREATING:
    // Ranged retreat: back off to optimal range
    if (distance >= m_optimalRange) {
      changeState(state, AttackState::POSITIONING);
    } else {
      Vector2D awayFromTarget = (entityPos - targetPos).normalized();
      Vector2D retreatPos = entityPos + awayFromTarget * (m_minimumRange);
      moveToPosition(entity, retreatPos, m_movementSpeed, deltaTime);
    }
    break;

  case AttackState::COOLDOWN:
    updateCooldown(entity, state);
    break;
  }
}

void AttackBehavior::updateChargeAttack(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  if (shouldCharge(entity, target, state) && !state.isCharging) {
    state.isCharging = true;
    state.attackChargeTime = 0.0f;
  }

  if (state.isCharging) {
    // Charge towards target at high speed
    moveToPosition(entity, target->getPosition(),
                   m_movementSpeed * CHARGE_SPEED_MULTIPLIER, deltaTime);

    // Check if charge is complete or target reached
    if (state.targetDistance <= m_minimumRange) {
      executeAttack(entity, target, state);
      state.isCharging = false;
      changeState(state, AttackState::RECOVERING);
    }
  } else {
    updateMeleeAttack(entity, state, deltaTime);
  }
}

void AttackBehavior::updateAmbushAttack(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  // Wait for optimal moment to strike
  if (state.currentState == AttackState::POSITIONING &&
      state.targetDistance <= m_optimalRange) {
    // Ambush when target is close
    executeAttack(entity, target, state);
    changeState(state, AttackState::RECOVERING);
  } else {
    updateMeleeAttack(entity, state, deltaTime);
  }
}

void AttackBehavior::updateCoordinatedAttack(EntityPtr entity,
                                             EntityState &state, float deltaTime) {
  if (m_teamwork) {
    coordinateWithTeam(entity, state);
  }
  updateMeleeAttack(entity, state, deltaTime);
}

void AttackBehavior::updateHitAndRun(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  // After attacking, immediately retreat
  if (state.currentState == AttackState::RECOVERING) {
    changeState(state, AttackState::RETREATING);
  }

  updateMeleeAttack(entity, state, deltaTime);
}

void AttackBehavior::updateBerserkerAttack(EntityPtr entity,
                                           EntityState &state, float deltaTime) {
  // Aggressive continuous attacks with reduced cooldown
  if (state.currentState == AttackState::COOLDOWN) {
    float timeInState = state.stateChangeTimer;
    if (timeInState > (m_attackCooldown * 0.5f)) { // Half cooldown
      changeState(state, AttackState::APPROACHING);
    }
  }

  updateMeleeAttack(entity, state, deltaTime);
}

void AttackBehavior::updateSeeking(EntityPtr /*entity*/, EntityState &state) {
  if (state.hasTarget && state.targetDistance <= m_attackRange * 1.5f) {
    changeState(state, AttackState::APPROACHING);
  }
}

void AttackBehavior::updateApproaching(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  if (state.targetDistance <= m_optimalRange) {
    changeState(state, AttackState::POSITIONING);
  } else {
    moveToPosition(entity, target->getPosition(), m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::updatePositioning(EntityPtr entity, EntityState &state, float deltaTime) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  Vector2D optimalPos = calculateOptimalAttackPosition(entity, target, state);
  Vector2D currentPos = entity->getPosition();

  if ((currentPos - optimalPos).length() > 15.0f) {
    moveToPosition(entity, optimalPos, m_movementSpeed, deltaTime);
  } else if (state.canAttack) {
    changeState(state, AttackState::ATTACKING);
  }
}

void AttackBehavior::updateAttacking(EntityPtr entity, EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  // Execute the attack
  if (m_specialRoll(m_rng) < m_specialAttackChance &&
      state.specialAttackReady) {
    executeSpecialAttack(entity, target, state);
  } else if (m_comboAttacks) {
    executeComboAttack(entity, target, state);
  } else {
    executeAttack(entity, target, state);
  }

  changeState(state, AttackState::RECOVERING);
}

void AttackBehavior::updateRecovering(EntityPtr /*entity*/,
                                      EntityState & /*state*/) {
  // Stay in place during recovery
  // State transition handled by updateStateTimer
}

void AttackBehavior::updateRetreating(EntityPtr entity, EntityState &state) {
  EntityPtr target = getTarget();
  if (!target)
    return;

  // Move away from target
  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
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

void AttackBehavior::updateCooldown(EntityPtr /*entity*/,
                                    EntityState & /*state*/) {
  // Wait during cooldown
  // State transition handled by updateStateTimer
}

void AttackBehavior::moveToPosition(EntityPtr entity, const Vector2D &targetPos,
                                    float speed, float deltaTime) {
  if (!entity || speed <= 0.0f)
    return;

  // Access per-entity state
  auto it = m_entityStates.find(entity);
  if (it == m_entityStates.end()) return;
  EntityState &state = it->second;

  // Use base class moveToPosition with Critical priority (3) for attacks
  AIBehavior::moveToPosition(entity, targetPos, speed, deltaTime, state.baseState, 3);
}

void AttackBehavior::maintainDistance(EntityPtr entity, EntityPtr target,
                                      float desiredDistance, float deltaTime) {
  if (!entity || !target)
    return;

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  
  // PERFORMANCE: Use squared distance for comparison
  float currentDistanceSquared = (entityPos - targetPos).lengthSquared();
  float desiredDistanceSquared = desiredDistance * desiredDistance;
  float toleranceSquared = 100.0f; // 10.0f * 10.0f
  
  float difference = std::abs(currentDistanceSquared - desiredDistanceSquared);
  if (difference > toleranceSquared) {
    Vector2D direction = normalizeDirection(entityPos - targetPos);
    Vector2D desiredPos = targetPos + direction * desiredDistance;
    moveToPosition(entity, desiredPos, m_movementSpeed, deltaTime);
  }
}

void AttackBehavior::circleStrafe(EntityPtr entity, EntityPtr target,
                                  EntityState &state, float deltaTime) {
  if (!entity || !target || !m_circleStrafe)
    return;



  if (state.strafeTimer >= STRAFE_INTERVAL) {
    state.strafeDirectionInt *= -1; // Change direction
    state.strafeTimer = 0.0f;
  }

  Vector2D strafePos = calculateStrafePosition(entity, target, state);
  moveToPosition(entity, strafePos, m_movementSpeed, deltaTime);
}

void AttackBehavior::performFlankingManeuver(EntityPtr entity, EntityPtr target,
                                             EntityState &state, float deltaTime) {
  if (!entity || !target || !m_flankingEnabled)
    return;

  Vector2D flankPos = calculateFlankingPosition(entity, target);
  moveToPosition(entity, flankPos, m_movementSpeed, deltaTime);
  state.flanking = true;
}

// Utility methods removed - now using base class implementations

bool AttackBehavior::isValidAttackPosition(const Vector2D &position,
                                           EntityPtr target) const {
  if (!target)
    return false;

  // PERFORMANCE: Use squared distance
  float distanceSquared = (position - target->getPosition()).lengthSquared();
  float minRangeSquared = m_minimumRange * m_minimumRange;
  float maxRangeSquared = m_attackRange * m_attackRange;
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
    EntityPtr entity, EntityPtr target, const EntityState &state) const {
  if (!entity || !target)
    return 0.0f;

  float baseChance = 0.8f; // 80% base hit chance

  // Modify based on distance
  float distance = state.targetDistance;
  if (distance > m_optimalRange) {
    baseChance *= (m_attackRange - distance) / (m_attackRange - m_optimalRange);
  }

  // Modify based on combo
  if (state.currentCombo > 0) {
    baseChance += state.currentCombo * 0.05f;
  }

  return std::clamp(baseChance, 0.0f, 1.0f);
}

Vector2D AttackBehavior::calculateKnockbackVector(EntityPtr attacker,
                                                  EntityPtr target) const {
  if (!attacker || !target)
    return Vector2D(0, 0);

  Vector2D attackerPos = attacker->getPosition();
  Vector2D targetPos = target->getPosition();

  return normalizeDirection(targetPos - attackerPos);
}

void AttackBehavior::coordinateWithTeam(EntityPtr /*entity*/,
                                        const EntityState &state) {
  // In a full implementation, this would coordinate with nearby allies
  // For now, we just broadcast coordination messages
  if (state.inCombat && state.hasTarget) {
    AIManager::Instance().broadcastMessage("coordinate_attack", false);
  }
}

bool AttackBehavior::isFriendlyFireRisk(EntityPtr /*entity*/,
                                        EntityPtr /*target*/) const {
  if (!m_avoidFriendlyFire)
    return false;

  // In a full implementation, this would check for allies in the line of fire
  // For now, return false (no friendly fire risk)
  return false;
}

std::vector<EntityPtr> AttackBehavior::getNearbyAllies(EntityPtr /*entity*/,
                                                       float /*radius*/) const {
  // In a full implementation, this would query the entity system for nearby
  // allies For now, return empty vector
  return std::vector<EntityPtr>();
}
