/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/AttackBehavior.hpp"
#include "managers/AIManager.hpp"
#include <algorithm>

AttackBehavior::AttackBehavior(float attackRange, float attackDamage, float attackSpeed)
    : m_attackRange(attackRange)
    , m_attackDamage(attackDamage)
    , m_attackSpeed(attackSpeed)
{
    m_optimalRange = attackRange * 0.8f;
    m_minimumRange = attackRange * 0.4f;
}

AttackBehavior::AttackBehavior(AttackMode mode, float attackRange, float attackDamage)
    : m_attackMode(mode)
    , m_attackRange(attackRange)
    , m_attackDamage(attackDamage)
{
    // Adjust parameters based on mode
    switch (mode) {
        case AttackMode::MELEE_ATTACK:
            m_attackRange = std::min(attackRange, 100.0f);
            m_optimalRange = m_attackRange * 0.8f;
            m_minimumRange = m_attackRange * 0.3f;
            m_attackSpeed = 1.2f;
            m_movementSpeed = 2.5f;
            break;

        case AttackMode::RANGED_ATTACK:
            m_attackRange = std::max(attackRange, 200.0f);
            m_optimalRange = m_attackRange * 0.7f;
            m_minimumRange = m_attackRange * 0.4f;
            m_attackSpeed = 0.8f;
            m_movementSpeed = 2.0f;
            break;

        case AttackMode::CHARGE_ATTACK:
            m_attackRange = attackRange * 1.5f;
            m_optimalRange = m_attackRange;
            m_minimumRange = 50.0f;
            m_attackSpeed = 0.5f;
            m_movementSpeed = 3.5f;
            m_chargeDamageMultiplier = 2.0f;
            break;

        case AttackMode::AMBUSH_ATTACK:
            m_optimalRange = attackRange * 0.6f;
            m_attackSpeed = 2.0f;
            m_criticalHitChance = 0.3f;
            m_movementSpeed = 1.5f;
            break;

        case AttackMode::COORDINATED_ATTACK:
            m_teamwork = true;
            m_flankingEnabled = true;
            m_movementSpeed = 2.2f;
            break;

        case AttackMode::HIT_AND_RUN:
            m_attackSpeed = 1.5f;
            m_movementSpeed = 3.0f;
            m_retreatThreshold = 0.8f;
            break;

        case AttackMode::BERSERKER_ATTACK:
            m_attackSpeed = 1.8f;
            m_movementSpeed = 2.8f;
            m_aggression = 1.0f;
            m_retreatThreshold = 0.1f;
            m_comboAttacks = true;
            break;
    }
}

void AttackBehavior::init(EntityPtr entity) {
    if (!entity) return;

    auto& state = m_entityStates[entity];
    state = EntityState(); // Reset to default state
    state.currentState = AttackState::SEEKING;
    state.stateChangeTime = SDL_GetTicks();
    state.currentHealth = state.maxHealth;
    state.currentStamina = 100.0f;
    state.canAttack = true;
}

void AttackBehavior::executeLogic(EntityPtr entity) {
    if (!entity || !isActive()) return;

    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) {
        init(entity);
        it = m_entityStates.find(entity);
        if (it == m_entityStates.end()) return;
    }

    EntityState& state = it->second;
    EntityPtr target = getTarget();

    // Update target tracking
    if (target) {
        state.hasTarget = true;
        state.lastTargetPosition = target->getPosition();
        state.targetDistance = (entity->getPosition() - target->getPosition()).length();

        if (!state.inCombat && state.targetDistance <= m_attackRange * 1.2f) {
            state.inCombat = true;
        } else if (state.inCombat && state.targetDistance > m_attackRange * 2.0f) {
            state.inCombat = false;
            state.currentState = AttackState::SEEKING;
        }
    } else {
        state.hasTarget = false;
        state.inCombat = false;
        if (state.currentState != AttackState::SEEKING) {
            changeState(state, AttackState::SEEKING);
        }
    }

    // Update state timer
    updateStateTimer(state);

    // Check for retreat conditions
    if (shouldRetreat(state) && state.currentState != AttackState::RETREATING) {
        changeState(state, AttackState::RETREATING);
    }

    // Execute behavior based on attack mode
    switch (m_attackMode) {
        case AttackMode::MELEE_ATTACK:
            updateMeleeAttack(entity, state);
            break;
        case AttackMode::RANGED_ATTACK:
            updateRangedAttack(entity, state);
            break;
        case AttackMode::CHARGE_ATTACK:
            updateChargeAttack(entity, state);
            break;
        case AttackMode::AMBUSH_ATTACK:
            updateAmbushAttack(entity, state);
            break;
        case AttackMode::COORDINATED_ATTACK:
            updateCoordinatedAttack(entity, state);
            break;
        case AttackMode::HIT_AND_RUN:
            updateHitAndRun(entity, state);
            break;
        case AttackMode::BERSERKER_ATTACK:
            updateBerserkerAttack(entity, state);
            break;
    }
}

void AttackBehavior::clean(EntityPtr entity) {
    if (entity) {
        m_entityStates.erase(entity);
    }
}

void AttackBehavior::onMessage(EntityPtr entity, const std::string& message) {
    if (!entity) return;

    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) return;

    EntityState& state = it->second;

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

std::string AttackBehavior::getName() const {
    return "Attack";
}

void AttackBehavior::setAttackMode(AttackMode mode) {
    m_attackMode = mode;
}

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

void AttackBehavior::setTeamwork(bool enabled) {
    m_teamwork = enabled;
}

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
    for (const auto& pair : m_entityStates) {
        if (pair.second.inCombat) return true;
    }
    return false;
}

bool AttackBehavior::isAttacking() const {
    for (const auto& pair : m_entityStates) {
        if (pair.second.currentState == AttackState::ATTACKING) return true;
    }
    return false;
}

bool AttackBehavior::canAttack() const {
    for (const auto& pair : m_entityStates) {
        if (pair.second.canAttack) return true;
    }
    return false;
}

AttackBehavior::AttackState AttackBehavior::getCurrentAttackState() const {
    for (const auto& pair : m_entityStates) {
        if (pair.second.inCombat) {
            return pair.second.currentState;
        }
    }
    return AttackState::SEEKING;
}

AttackBehavior::AttackMode AttackBehavior::getAttackMode() const {
    return m_attackMode;
}

float AttackBehavior::getDistanceToTarget() const {
    for (const auto& pair : m_entityStates) {
        if (pair.second.hasTarget && pair.second.inCombat) {
            return pair.second.targetDistance;
        }
    }
    return -1.0f;
}

float AttackBehavior::getLastAttackTime() const {
    Uint64 lastTime = 0;
    for (const auto& pair : m_entityStates) {
        if (pair.second.lastAttackTime > lastTime) {
            lastTime = pair.second.lastAttackTime;
        }
    }
    return static_cast<float>(lastTime) / 1000.0f;
}

int AttackBehavior::getCurrentCombo() const {
    int maxCombo = 0;
    for (const auto& pair : m_entityStates) {
        if (pair.second.currentCombo > maxCombo) {
            maxCombo = pair.second.currentCombo;
        }
    }
    return maxCombo;
}

std::shared_ptr<AIBehavior> AttackBehavior::clone() const {
    auto clone = std::make_shared<AttackBehavior>(m_attackMode, m_attackRange, m_attackDamage);
    clone->m_attackSpeed = m_attackSpeed;
    clone->m_movementSpeed = m_movementSpeed;
    clone->m_attackCooldown = m_attackCooldown;
    clone->m_recoveryTime = m_recoveryTime;
    clone->m_optimalRange = m_optimalRange;
    clone->m_minimumRange = m_minimumRange;
    clone->m_circleStrafe = m_circleStrafe;
    clone->m_strafeRadius = m_strafeRadius;
    clone->m_flankingEnabled = m_flankingEnabled;
    clone->m_preferredAttackAngle = m_preferredAttackAngle;
    clone->m_damageVariation = m_damageVariation;
    clone->m_criticalHitChance = m_criticalHitChance;
    clone->m_criticalHitMultiplier = m_criticalHitMultiplier;
    clone->m_knockbackForce = m_knockbackForce;
    clone->m_retreatThreshold = m_retreatThreshold;
    clone->m_aggression = m_aggression;
    clone->m_teamwork = m_teamwork;
    clone->m_avoidFriendlyFire = m_avoidFriendlyFire;
    clone->m_comboAttacks = m_comboAttacks;
    clone->m_maxCombo = m_maxCombo;
    clone->m_specialAttackChance = m_specialAttackChance;
    clone->m_aoeRadius = m_aoeRadius;
    clone->m_chargeDamageMultiplier = m_chargeDamageMultiplier;
    return clone;
}

EntityPtr AttackBehavior::getTarget() const {
    return AIManager::Instance().getPlayerReference();
}

bool AttackBehavior::isTargetInRange(EntityPtr entity, EntityPtr target) const {
    if (!entity || !target) return false;

    float distance = (entity->getPosition() - target->getPosition()).length();
    return distance <= m_attackRange;
}

bool AttackBehavior::isTargetInAttackRange(EntityPtr entity, EntityPtr target) const {
    if (!entity || !target) return false;

    float distance = (entity->getPosition() - target->getPosition()).length();
    return distance <= calculateEffectiveRange(m_entityStates.at(entity));
}

bool AttackBehavior::canReachTarget(EntityPtr entity, EntityPtr target) const {
    // Simplified - in a full implementation, this would check pathfinding
    return isTargetInRange(entity, target);
}

float AttackBehavior::calculateDamage(const EntityState& state) const {
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
        float comboMultiplier = 1.0f + (state.currentCombo * 0.2f);
        baseDamage *= comboMultiplier;
    }

    // Apply charge multiplier if charging
    if (state.isCharging) {
        baseDamage *= m_chargeDamageMultiplier;
    }

    return baseDamage;
}

Vector2D AttackBehavior::calculateOptimalAttackPosition(EntityPtr entity, EntityPtr target, const EntityState& /*state*/) const {
    if (!entity || !target) return Vector2D(0, 0);

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

Vector2D AttackBehavior::calculateFlankingPosition(EntityPtr entity, EntityPtr target) const {
    if (!entity || !target) return Vector2D(0, 0);

    Vector2D targetPos = target->getPosition();
    Vector2D entityPos = entity->getPosition();

    // Calculate perpendicular direction for flanking
    Vector2D toTarget = normalizeDirection(targetPos - entityPos);
    Vector2D flankDirection = Vector2D(-toTarget.getY(), toTarget.getX()); // Perpendicular

    // Choose left or right flank based on current position
    if ((entityPos - targetPos).getX() < 0) {
        flankDirection = Vector2D(toTarget.getY(), -toTarget.getX());
    }

    return targetPos + flankDirection * m_optimalRange;
}

Vector2D AttackBehavior::calculateStrafePosition(EntityPtr entity, EntityPtr target, const EntityState& state) const {
    if (!entity || !target) return Vector2D(0, 0);

    Vector2D targetPos = target->getPosition();
    Vector2D entityPos = entity->getPosition();

    // Calculate strafe direction (perpendicular to target direction)
    Vector2D toTarget = normalizeDirection(targetPos - entityPos);
    Vector2D strafeDir = Vector2D(-toTarget.getY(), toTarget.getX()) * state.strafeDirectionInt;

    return entityPos + strafeDir * (m_movementSpeed * 2.0f);
}

void AttackBehavior::changeState(EntityState& state, AttackState newState) {
    if (state.currentState != newState) {
        state.currentState = newState;
        state.stateChangeTime = SDL_GetTicks();

        // Reset state-specific flags
        switch (newState) {
            case AttackState::ATTACKING:
                state.recoveryStartTime = 0.0f;
                break;
            case AttackState::RECOVERING:
                state.recoveryStartTime = static_cast<float>(SDL_GetTicks()) / 1000.0f;
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

void AttackBehavior::updateStateTimer(EntityState& state) {
    Uint64 currentTime = SDL_GetTicks();
    Uint64 timeInState = currentTime - state.stateChangeTime;

    // Handle state transitions based on timing
    switch (state.currentState) {
        case AttackState::ATTACKING:
            if (timeInState > static_cast<Uint64>(1000.0f / m_attackSpeed)) {
                changeState(state, AttackState::RECOVERING);
            }
            break;

        case AttackState::RECOVERING:
            if (timeInState > static_cast<Uint64>(m_recoveryTime * 1000)) {
                changeState(state, AttackState::COOLDOWN);
            }
            break;

        case AttackState::COOLDOWN:
            if (timeInState > static_cast<Uint64>(m_attackCooldown * 1000)) {
                changeState(state, state.hasTarget ? AttackState::APPROACHING : AttackState::SEEKING);
            }
            break;

        default:
            break;
    }
}

bool AttackBehavior::shouldRetreat(const EntityState& state) const {
    float healthRatio = state.currentHealth / state.maxHealth;
    return healthRatio <= m_retreatThreshold && m_aggression < 0.8f;
}

bool AttackBehavior::shouldCharge(EntityPtr entity, EntityPtr target, const EntityState& state) const {
    if (!entity || !target || m_attackMode != AttackMode::CHARGE_ATTACK) return false;

    float distance = state.targetDistance;
    return distance > m_optimalRange * 1.5f && distance <= m_attackRange;
}

void AttackBehavior::executeAttack(EntityPtr entity, EntityPtr target, EntityState& state) {
    if (!entity || !target) return;

    // Calculate damage
    float damage = calculateDamage(state);

    // Calculate knockback
    Vector2D knockback = calculateKnockbackVector(entity, target);
    knockback = knockback * m_knockbackForce;

    // Apply damage (in a full implementation, this would interact with a damage system)
    applyDamage(target, damage, knockback);

    // Update attack state
    state.lastAttackTime = SDL_GetTicks();
    state.lastAttackHit = true; // Simplified - assume all attacks hit

    // Handle combo system
    if (m_comboAttacks) {
        Uint64 currentTime = SDL_GetTicks();
        if (currentTime - state.comboStartTime < COMBO_TIMEOUT) {
            state.currentCombo = std::min(state.currentCombo + 1, m_maxCombo);
        } else {
            state.currentCombo = 1;
            state.comboStartTime = currentTime;
        }
    }

    // Apply area of effect damage if enabled
    if (m_aoeRadius > 0.0f) {
        applyAreaOfEffectDamage(entity, target, damage * 0.5f);
    }
}

void AttackBehavior::executeSpecialAttack(EntityPtr entity, EntityPtr target, EntityState& state) {
    // Enhanced attack with special effects
    float specialDamage = calculateDamage(state) * 1.5f;
    Vector2D knockback = calculateKnockbackVector(entity, target) * (m_knockbackForce * 1.5f);

    applyDamage(target, specialDamage, knockback);

    state.lastAttackTime = SDL_GetTicks();
    state.specialAttackReady = false;
}

void AttackBehavior::executeComboAttack(EntityPtr entity, EntityPtr target, EntityState& state) {
    if (!m_comboAttacks || state.currentCombo == 0) {
        executeAttack(entity, target, state);
        return;
    }

    // Combo finisher
    if (state.currentCombo >= m_maxCombo) {
        float comboDamage = calculateDamage(state) * 2.0f;
        Vector2D knockback = calculateKnockbackVector(entity, target) * (m_knockbackForce * 2.0f);

        applyDamage(target, comboDamage, knockback);

        // Reset combo
        state.currentCombo = 0;
        state.comboStartTime = 0;
    } else {
        executeAttack(entity, target, state);
    }
}

void AttackBehavior::applyDamage(EntityPtr target, float /*damage*/, const Vector2D& knockback) {
    // In a full implementation, this would interact with the target's health system
    // For now, we just simulate the attack

    // Apply knockback by slightly moving the target
    if (knockback.length() > 0.001f) {
        Vector2D currentPos = target->getPosition();
        Vector2D newPos = currentPos + knockback * 0.1f; // Reduced for visual effect
        target->setPosition(newPos);
    }
}

void AttackBehavior::applyAreaOfEffectDamage(EntityPtr /*entity*/, EntityPtr /*target*/, float /*damage*/) {
    // In a full implementation, this would find all entities within the AOE radius
    // and apply damage to them
}

void AttackBehavior::updateMeleeAttack(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    switch (state.currentState) {
        case AttackState::SEEKING:
            updateSeeking(entity, state);
            break;
        case AttackState::APPROACHING:
            updateApproaching(entity, state);
            break;
        case AttackState::POSITIONING:
            updatePositioning(entity, state);
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

void AttackBehavior::updateRangedAttack(EntityPtr entity, EntityState& state) {
    // Similar to melee but with different positioning logic
    updateMeleeAttack(entity, state);
}

void AttackBehavior::updateChargeAttack(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    if (shouldCharge(entity, target, state) && !state.isCharging) {
        state.isCharging = true;
        state.attackChargeTime = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    }

    if (state.isCharging) {
        // Charge towards target at high speed
        moveToPosition(entity, target->getPosition(), m_movementSpeed * CHARGE_SPEED_MULTIPLIER);

        // Check if charge is complete or target reached
        if (state.targetDistance <= m_minimumRange) {
            executeAttack(entity, target, state);
            state.isCharging = false;
            changeState(state, AttackState::RECOVERING);
        }
    } else {
        updateMeleeAttack(entity, state);
    }
}

void AttackBehavior::updateAmbushAttack(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    // Wait for optimal moment to strike
    if (state.currentState == AttackState::POSITIONING && state.targetDistance <= m_optimalRange) {
        // Ambush when target is close
        executeAttack(entity, target, state);
        changeState(state, AttackState::RECOVERING);
    } else {
        updateMeleeAttack(entity, state);
    }
}

void AttackBehavior::updateCoordinatedAttack(EntityPtr entity, EntityState& state) {
    if (m_teamwork) {
        coordinateWithTeam(entity, state);
    }
    updateMeleeAttack(entity, state);
}

void AttackBehavior::updateHitAndRun(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    // After attacking, immediately retreat
    if (state.currentState == AttackState::RECOVERING) {
        changeState(state, AttackState::RETREATING);
    }

    updateMeleeAttack(entity, state);
}

void AttackBehavior::updateBerserkerAttack(EntityPtr entity, EntityState& state) {
    // Aggressive continuous attacks with reduced cooldown
    if (state.currentState == AttackState::COOLDOWN) {
        Uint64 timeInState = SDL_GetTicks() - state.stateChangeTime;
        if (timeInState > static_cast<Uint64>(m_attackCooldown * 500)) { // Half cooldown
            changeState(state, AttackState::APPROACHING);
        }
    }

    updateMeleeAttack(entity, state);
}

void AttackBehavior::updateSeeking(EntityPtr /*entity*/, EntityState& state) {
    if (state.hasTarget && state.targetDistance <= m_attackRange * 1.5f) {
        changeState(state, AttackState::APPROACHING);
    }
}

void AttackBehavior::updateApproaching(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    if (state.targetDistance <= m_optimalRange) {
        changeState(state, AttackState::POSITIONING);
    } else {
        moveToPosition(entity, target->getPosition(), m_movementSpeed);
    }
}

void AttackBehavior::updatePositioning(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    Vector2D optimalPos = calculateOptimalAttackPosition(entity, target, state);
    Vector2D currentPos = entity->getPosition();

    if ((currentPos - optimalPos).length() > 15.0f) {
        moveToPosition(entity, optimalPos, m_movementSpeed);
    } else if (state.canAttack) {
        changeState(state, AttackState::ATTACKING);
    }
}

void AttackBehavior::updateAttacking(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    // Execute the attack
    if (m_specialRoll(m_rng) < m_specialAttackChance && state.specialAttackReady) {
        executeSpecialAttack(entity, target, state);
    } else if (m_comboAttacks) {
        executeComboAttack(entity, target, state);
    } else {
        executeAttack(entity, target, state);
    }

    changeState(state, AttackState::RECOVERING);
}

void AttackBehavior::updateRecovering(EntityPtr /*entity*/, EntityState& /*state*/) {
    // Stay in place during recovery
    // State transition handled by updateStateTimer
}

void AttackBehavior::updateRetreating(EntityPtr entity, EntityState& state) {
    EntityPtr target = getTarget();
    if (!target) return;

    // Move away from target
    Vector2D entityPos = entity->getPosition();
    Vector2D targetPos = target->getPosition();
    Vector2D retreatDir = normalizeDirection(entityPos - targetPos);

    Vector2D retreatPos = entityPos + retreatDir * (m_movementSpeed * RETREAT_SPEED_MULTIPLIER);
    entity->setPosition(retreatPos);

    // Stop retreating if far enough or health recovered
    if (state.targetDistance > m_attackRange * 2.0f || !shouldRetreat(state)) {
        state.isRetreating = false;
        changeState(state, AttackState::SEEKING);
    }
}

void AttackBehavior::updateCooldown(EntityPtr /*entity*/, EntityState& /*state*/) {
    // Wait during cooldown
    // State transition handled by updateStateTimer
}

void AttackBehavior::moveToPosition(EntityPtr entity, const Vector2D& targetPos, float speed) {
    if (!entity || speed <= 0.0f) return;

    Vector2D currentPos = entity->getPosition();
    Vector2D direction = normalizeDirection(targetPos - currentPos);

    if (direction.length() > 0.001f) {
        Vector2D newPos = currentPos + direction * speed;
        entity->setPosition(newPos);
    }
}

void AttackBehavior::maintainDistance(EntityPtr entity, EntityPtr target, float desiredDistance) {
    if (!entity || !target) return;

    Vector2D entityPos = entity->getPosition();
    Vector2D targetPos = target->getPosition();
    float currentDistance = (entityPos - targetPos).length();

    if (std::abs(currentDistance - desiredDistance) > 10.0f) {
        Vector2D direction = normalizeDirection(entityPos - targetPos);
        Vector2D desiredPos = targetPos + direction * desiredDistance;
        moveToPosition(entity, desiredPos, m_movementSpeed);
    }
}

void AttackBehavior::circleStrafe(EntityPtr entity, EntityPtr target, EntityState& state) {
    if (!entity || !target || !m_circleStrafe) return;

    Uint64 currentTime = SDL_GetTicks();
    if (currentTime >= state.nextStrafeTime) {
        state.strafeDirectionInt *= -1; // Change direction
        state.nextStrafeTime = currentTime + STRAFE_INTERVAL;
    }

    Vector2D strafePos = calculateStrafePosition(entity, target, state);
    moveToPosition(entity, strafePos, m_movementSpeed);
}

void AttackBehavior::performFlankingManeuver(EntityPtr entity, EntityPtr target, EntityState& state) {
    if (!entity || !target || !m_flankingEnabled) return;

    Vector2D flankPos = calculateFlankingPosition(entity, target);
    moveToPosition(entity, flankPos, m_movementSpeed);
    state.flanking = true;
}

Vector2D AttackBehavior::normalizeDirection(const Vector2D& vector) const {
    float magnitude = vector.length();
    if (magnitude < 0.001f) {
        return Vector2D(0, 0);
    }
    return vector / magnitude;
}

float AttackBehavior::calculateAngleToTarget(const Vector2D& from, const Vector2D& to) const {
    Vector2D direction = to - from;
    return std::atan2(direction.getY(), direction.getX());
}

float AttackBehavior::normalizeAngle(float angle) const {
    while (angle > M_PI) angle -= 2.0f * M_PI;
    while (angle < -M_PI) angle += 2.0f * M_PI;
    return angle;
}

Vector2D AttackBehavior::rotateVector(const Vector2D& vector, float angle) const {
    float cos_a = std::cos(angle);
    float sin_a = std::sin(angle);

    return Vector2D(
        vector.getX() * cos_a - vector.getY() * sin_a,
        vector.getX() * sin_a + vector.getY() * cos_a
    );
}

bool AttackBehavior::isValidAttackPosition(const Vector2D& position, EntityPtr target) const {
    if (!target) return false;

    float distance = (position - target->getPosition()).length();
    return distance >= m_minimumRange && distance <= m_attackRange;
}

float AttackBehavior::calculateEffectiveRange(const EntityState& state) const {
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

float AttackBehavior::calculateAttackSuccessChance(EntityPtr entity, EntityPtr target, const EntityState& state) const {
    if (!entity || !target) return 0.0f;

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

Vector2D AttackBehavior::calculateKnockbackVector(EntityPtr attacker, EntityPtr target) const {
    if (!attacker || !target) return Vector2D(0, 0);

    Vector2D attackerPos = attacker->getPosition();
    Vector2D targetPos = target->getPosition();

    return normalizeDirection(targetPos - attackerPos);
}

void AttackBehavior::coordinateWithTeam(EntityPtr /*entity*/, const EntityState& state) {
    // In a full implementation, this would coordinate with nearby allies
    // For now, we just broadcast coordination messages
    if (state.inCombat && state.hasTarget) {
        AIManager::Instance().broadcastMessage("coordinate_attack", false);
    }
}

bool AttackBehavior::isFriendlyFireRisk(EntityPtr /*entity*/, EntityPtr /*target*/) const {
    if (!m_avoidFriendlyFire) return false;

    // In a full implementation, this would check for allies in the line of fire
    // For now, return false (no friendly fire risk)
    return false;
}

std::vector<EntityPtr> AttackBehavior::getNearbyAllies(EntityPtr /*entity*/, float /*radius*/) const {
    // In a full implementation, this would query the entity system for nearby allies
    // For now, return empty vector
    return std::vector<EntityPtr>();
}
