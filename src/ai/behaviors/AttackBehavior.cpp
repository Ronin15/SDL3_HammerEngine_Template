/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace {

// ============================================================================
// THREAD-LOCAL STATE
// ============================================================================

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_damageRoll{0.0f, 1.0f};
thread_local std::uniform_real_distribution<float> s_criticalRoll{0.0f, 1.0f};
thread_local std::uniform_real_distribution<float> s_specialRoll{0.0f, 1.0f};
thread_local std::uniform_real_distribution<float> s_angleVariation{-0.5f, 0.5f};
thread_local std::vector<EventManager::DeferredEvent> t_deferredDamageEvents;
thread_local std::vector<size_t> s_scanBuffer;

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr float SEEK_SCAN_RANGE = 300.0f;

constexpr float COMBAT_ENTER_RANGE_MULT = 1.2f;
constexpr float COMBAT_EXIT_RANGE_MULT = 2.0f;
constexpr float COMBO_DAMAGE_PER_LEVEL = 0.15f;
constexpr float COMBO_TIMEOUT = 2.0f;
constexpr float SPECIAL_ATTACK_MULTIPLIER = 1.5f;
constexpr float RETREAT_SPEED_MULTIPLIER = 1.3f;
constexpr float STRAFE_INTERVAL = 3.0f;

// Future features: Charge attacks and combo finishers
// Config support exists (chargeDamageMultiplier, etc.) but state machine integration pending
[[maybe_unused]] constexpr float COMBO_FINISHER_MULTIPLIER = 1.8f;
[[maybe_unused]] constexpr float CHARGE_SPEED_MULTIPLIER = 2.0f;
[[maybe_unused]] constexpr float CHARGE_DISTANCE_THRESHOLD_MULT = 1.5f;
constexpr float FEAR_FLEE_THRESHOLD = 0.7f;
constexpr float BRAVERY_FLEE_THRESHOLD = 0.3f;
constexpr float BRAVERY_RETREAT_FACTOR = 0.3f;

// Attack state enumeration (matches uint8_t in EDM)
enum class AttackState : uint8_t {
    SEEKING = 0,
    APPROACHING = 1,
    POSITIONING = 2,
    ATTACKING = 3,
    RECOVERING = 4,
    RETREATING = 5,
    COOLDOWN = 6
};

// Attack mode enumeration (matches uint8_t attackMode in EDM AttackState)
enum class AttackMode : uint8_t {
    MELEE = 0,
    RANGED = 1,
    CHARGE = 2,
    AMBUSH = 3,
    COORDINATED = 4,
    HIT_AND_RUN = 5,
    BERSERKER = 6
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

Vector2D normalizeDir(const Vector2D& v) {
    float len = v.length();
    if (len < 0.0001f) return Vector2D(0, 0);
    return v * (1.0f / len);
}

Vector2D rotateVector(const Vector2D& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return Vector2D(v.getX() * c - v.getY() * s, v.getX() * s + v.getY() * c);
}

// Scan for nearest different-faction entity (fully generic, no player hardcoding)
EntityHandle scanForNearestTarget(const Vector2D& pos, uint8_t myFaction, size_t myEdmIndex) {
    s_scanBuffer.clear();
    AIManager::Instance().queryEdmIndicesInRadius(pos, SEEK_SCAN_RANGE, s_scanBuffer, false);
    auto& edm = EntityDataManager::Instance();

    EntityHandle bestTarget{};
    float bestDistSq = std::numeric_limits<float>::max();

    for (size_t idx : s_scanBuffer) {
        if (idx == myEdmIndex) continue;
        const auto& hot = edm.getHotDataByIndex(idx);
        if (!hot.isAlive()) continue;
        const auto& charData = edm.getCharacterDataByIndex(idx);
        if (charData.faction == myFaction) continue;
        float distSq = Vector2D::distanceSquared(pos, hot.transform.position);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestTarget = edm.getHandle(idx);
        }
    }
    return bestTarget;
}

void updateTimers(BehaviorData& data, float deltaTime) {
    auto& attack = data.state.attack;
    attack.attackTimer += deltaTime;
    attack.stateChangeTimer += deltaTime;
    attack.damageTimer += deltaTime;
    if (attack.comboTimer > 0.0f) {
        attack.comboTimer -= deltaTime;
    }
    attack.strafeTimer += deltaTime;
}

// Process pending messages for Attack behavior
void processAttackMessages(BehaviorData& data, const HammerEngine::AttackBehaviorConfig& config) {
    auto& attack = data.state.attack;

    for (uint8_t i = 0; i < data.pendingMessageCount; ++i) {
        uint8_t msgId = data.pendingMessages[i].messageId;
        uint8_t param = data.pendingMessages[i].param;

        switch (msgId) {
            case BehaviorMessage::ATTACK_TARGET:
                // param could encode target info if needed
                (void)param;
                break;

            case BehaviorMessage::RETREAT:
                // Force retreat state
                attack.currentState = static_cast<uint8_t>(AttackState::RETREATING);
                attack.isRetreating = true;
                attack.stateChangeTimer = 0.0f;
                break;

            default:
                break;
        }
    }
    data.pendingMessageCount = 0;
    (void)config;
}

void changeState(BehaviorData& data, AttackState newState) {
    auto& attack = data.state.attack;
    uint8_t newStateVal = static_cast<uint8_t>(newState);
    if (attack.currentState != newStateVal) {
        attack.currentState = newStateVal;
        attack.stateChangeTimer = 0.0f;

        switch (newState) {
            case AttackState::ATTACKING:
                attack.recoveryTimer = 0.0f;
                attack.canAttack = false;
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

float calculateDamage(const BehaviorData& data, const HammerEngine::AttackBehaviorConfig& config) {
    const auto& attack = data.state.attack;
    float baseDamage = config.attackDamage;

    // Apply damage variation
    float variation = (s_damageRoll(s_rng) - 0.5f) * 2.0f * config.damageVariation;
    baseDamage *= (1.0f + variation);

    // Check for critical hit
    if (s_criticalRoll(s_rng) < config.criticalHitChance) {
        baseDamage *= config.criticalHitMultiplier;
    }

    // Apply combo multiplier
    if (config.comboAttacks && attack.currentCombo > 0) {
        float comboMultiplier = 1.0f + (attack.currentCombo * COMBO_DAMAGE_PER_LEVEL);
        baseDamage *= comboMultiplier;
    }

    // Apply charge multiplier
    if (attack.isCharging) {
        baseDamage *= config.chargeDamageMultiplier;
    }

    return baseDamage;
}

Vector2D calculateKnockbackVector(const Vector2D& attackerPos, const Vector2D& targetPos) {
    return normalizeDir(targetPos - attackerPos);
}

void applyDamageToTarget(EntityHandle targetHandle, float damage, const Vector2D& knockback,
                         EntityHandle attackerHandle) {
    if (!targetHandle.isValid()) return;

    Vector2D scaledKnockback = knockback * 0.1f;

    auto damageEvent = EventManager::Instance().acquireDamageEvent();
    damageEvent->configure(attackerHandle, targetHandle, damage, scaledKnockback);

    EventData eventData;
    eventData.typeId = EventTypeId::Combat;
    eventData.setActive(true);
    eventData.priority = EventPriority::HIGH;
    eventData.event = damageEvent;

    t_deferredDamageEvents.push_back({EventTypeId::Combat, std::move(eventData)});
}

void moveToPosition(size_t edmIndex, const Vector2D& targetPos, float speed) {
    if (edmIndex == SIZE_MAX || speed <= 0.0f) return;

    auto& edm = EntityDataManager::Instance();
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

bool shouldRetreat(size_t edmIndex, const CharacterData* charData, float retreatThreshold, float aggression) {
    if (edmIndex == SIZE_MAX || !charData) return false;

    auto& edm = EntityDataManager::Instance();
    float healthRatio = charData->health / charData->maxHealth;

    float effectiveThreshold = retreatThreshold;
    if (edm.hasMemoryData(edmIndex)) {
        const auto& memData = edm.getMemoryData(edmIndex);
        if (memData.isValid()) {
            effectiveThreshold *= (1.0f + memData.emotions.fear * 0.5f - memData.emotions.aggression * 0.3f);
            effectiveThreshold *= (1.0f - memData.personality.bravery * BRAVERY_RETREAT_FACTOR);
            effectiveThreshold *= (1.0f - memData.personality.aggression * 0.2f);
            effectiveThreshold = std::clamp(effectiveThreshold, 0.1f, 0.7f);
        }
    }

    return healthRatio <= effectiveThreshold && aggression < 0.8f;
}

void executeAttackAction(size_t edmIndex, const Vector2D& targetPos, BehaviorData& data,
                         const HammerEngine::AttackBehaviorConfig& config, const CharacterData* charData) {
    if (edmIndex == SIZE_MAX) return;

    auto& edm = EntityDataManager::Instance();
    auto& attack = data.state.attack;
    Vector2D entityPos = edm.getHotDataByIndex(edmIndex).transform.position;

    float damage = calculateDamage(data, config);

    // Personality-based damage scaling
    if (edm.hasMemoryData(edmIndex)) {
        const auto& memData = edm.getMemoryData(edmIndex);
        if (memData.isValid()) {
            float personalityBonus = 1.0f + (memData.personality.aggression * 0.2f);
            float emotionalBonus = 1.0f + (memData.emotions.aggression * 0.2f);
            damage *= personalityBonus * emotionalBonus;
        }
    }

    Vector2D knockback = calculateKnockbackVector(entityPos, targetPos) * config.knockbackForce;
    EntityHandle attackerHandle = edm.getHandle(edmIndex);

    // Resolve target handle: explicit > memory lastTarget > memory lastAttacker
    EntityHandle targetHandle{};
    if (attack.hasExplicitTarget && attack.explicitTarget.isValid()) {
        targetHandle = attack.explicitTarget;
    } else if (edm.hasMemoryData(edmIndex)) {
        const auto& memData = edm.getMemoryData(edmIndex);
        if (memData.lastTarget.isValid()) {
            targetHandle = memData.lastTarget;
        } else if (memData.lastAttacker.isValid()) {
            targetHandle = memData.lastAttacker;
        }
    }
    if (!targetHandle.isValid()) return;  // No valid target — main execute handles re-acquisition

    applyDamageToTarget(targetHandle, damage, knockback, attackerHandle);

    // Faction escalation: neutral attacker (faction > 1) attacking a friendly (faction 0)
    // reveals them as hostile (faction 1), which also updates collision layers
    if (charData && charData->faction > 1) {
        size_t targetIdx = edm.getIndex(targetHandle);
        if (targetIdx != SIZE_MAX && edm.getCharacterDataByIndex(targetIdx).faction == 0) {
            edm.setFaction(edm.getHandle(edmIndex), 1);
        }
    }

    // Track who we attacked
    if (edm.hasMemoryData(edmIndex)) {
        auto& memData = edm.getMemoryData(edmIndex);
        memData.lastTarget = targetHandle;
    }

    attack.attackTimer = 0.0f;
    attack.lastAttackHit = true;

    // Handle combo system
    if (config.comboAttacks) {
        if (attack.comboTimer > 0.0f) {
            attack.currentCombo = std::min(attack.currentCombo + 1, config.maxCombo);
        } else {
            attack.currentCombo = 1;
            attack.comboTimer = COMBO_TIMEOUT;
        }
    }
}

// ============================================================================
// MODE-SPECIFIC POSITIONING FUNCTIONS
// ============================================================================

/**
 * @brief Apply ranged attack positioning (kiting behavior)
 * @return true if we should skip normal positioning, false to continue
 */
bool applyRangedPositioning(size_t edmIndex, const Vector2D& entityPos, const Vector2D& targetPos,
                            BehaviorData& data, const HammerEngine::AttackBehaviorConfig& config) {
    auto& attack = data.state.attack;
    float optimalRange = config.attackRange * config.optimalRangeMultiplier;
    float minimumRange = config.attackRange * config.minimumRangeMultiplier;

    // Ranged units kite: back off if too close
    if (attack.targetDistance < minimumRange && minimumRange > 0.0f) {
        Vector2D direction = normalizeDir(entityPos - targetPos);
        Vector2D backoffPos = targetPos + direction * (optimalRange * 0.8f);
        moveToPosition(edmIndex, backoffPos, data.moveSpeed);
        return true;
    }

    // Move closer if too far
    if (attack.targetDistance > config.attackRange) {
        moveToPosition(edmIndex, targetPos, data.moveSpeed);
        return true;
    }

    return false; // Use normal positioning
}

/**
 * @brief Apply charge attack positioning (rush attacks)
 */
bool applyChargePositioning(size_t edmIndex, const Vector2D& entityPos, const Vector2D& targetPos,
                            BehaviorData& data, const HammerEngine::AttackBehaviorConfig& config) {
    auto& attack = data.state.attack;
    auto& edm = EntityDataManager::Instance();
    float chargeDistance = config.attackRange * CHARGE_DISTANCE_THRESHOLD_MULT;

    // If far enough for a charge, start charging
    if (attack.targetDistance > chargeDistance && !attack.isCharging) {
        attack.isCharging = true;
    }

    // During charge, move at 2x speed toward target using direct velocity (faster than moveToPosition)
    if (attack.isCharging) {
        Vector2D chargeDir = normalizeDir(targetPos - entityPos);
        edm.getHotDataByIndex(edmIndex).transform.velocity = chargeDir * (data.moveSpeed * CHARGE_SPEED_MULTIPLIER);
        return true;
    }

    return false; // Use normal positioning
}

/**
 * @brief Apply hit-and-run positioning (frequent retreats)
 */
bool applyHitAndRunPositioning(size_t edmIndex, const Vector2D& entityPos, const Vector2D& targetPos,
                               BehaviorData& data, const HammerEngine::AttackBehaviorConfig& config) {
    auto& attack = data.state.attack;
    auto& edm = EntityDataManager::Instance();

    // After recovering, force retreat - scale speed by optimal range preference
    if (attack.currentState == static_cast<uint8_t>(AttackState::RECOVERING) ||
        attack.currentState == static_cast<uint8_t>(AttackState::COOLDOWN)) {

        Vector2D retreatDir = normalizeDir(entityPos - targetPos);
        // Retreat faster for longer-range configurations (optimalRangeMultiplier closer to 1.0)
        float rangeScaling = 1.0f + (config.optimalRangeMultiplier - 0.5f) * 0.4f;
        Vector2D retreatVelocity = retreatDir * (data.moveSpeed * RETREAT_SPEED_MULTIPLIER * rangeScaling);
        edm.getHotDataByIndex(edmIndex).transform.velocity = retreatVelocity;
        return true;
    }

    return false; // Use normal positioning
}

/**
 * @brief Apply berserker mode modifiers (no retreat, fast attacks)
 */
void applyBerserkerModifiers(BehaviorData& data, float& effectiveRetreatThreshold,
                             float& effectiveCooldown) {
    auto& attack = data.state.attack;
    (void)attack;  // Used for potential future state checks

    // Berserkers almost never retreat
    effectiveRetreatThreshold = 0.1f;

    // Half cooldown
    effectiveCooldown *= 0.5f;
}

} // anonymous namespace

namespace Behaviors {

// ============================================================================
// PUBLIC API
// ============================================================================

void initAttack(size_t edmIndex, const HammerEngine::AttackBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Attack);
    auto& data = edm.getBehaviorData(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    auto& attack = data.state.attack;

    attack.lastTargetPosition = Vector2D(0, 0);
    attack.attackPosition = Vector2D(0, 0);
    attack.retreatPosition = Vector2D(0, 0);
    attack.strafeVector = Vector2D(0, 0);
    attack.currentState = 0; // SEEKING
    attack.attackMode = 0;   // MELEE by default
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
    attack.scanCooldown = 0.0f;
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
    attack.hasExplicitTarget = false;
    attack.comboEnabled = false;
    attack.explicitTarget = EntityHandle{};

    data.setInitialized(true);
    (void)config;
}

void executeAttack(BehaviorContext& ctx, const HammerEngine::AttackBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid()) return;

    auto& data = *ctx.behaviorData;
    auto& attack = data.state.attack;

    // Process any pending messages before main logic
    processAttackMessages(data, config);

    // Consume engagement target from centralized pre-pass
    if (data.pendingEngageTarget.isValid()) {
        attack.hasExplicitTarget = true;
        attack.explicitTarget = data.pendingEngageTarget;
        data.pendingEngageTarget = EntityHandle{};
    }

    // Target priority: explicit > memory lastTarget > memory lastAttacker > player (if hostile)
    Vector2D targetPos;
    bool hasTarget = false;
    auto& edm = EntityDataManager::Instance();

    // Check explicit target first
    if (attack.hasExplicitTarget && attack.explicitTarget.isValid()) {
        size_t targetIdx = edm.getIndex(attack.explicitTarget);
        if (targetIdx != SIZE_MAX) {
            const auto& targetHot = edm.getHotDataByIndex(targetIdx);
            if (targetHot.isAlive()) {
                targetPos = targetHot.transform.position;
                hasTarget = true;
            }
        }
        if (!hasTarget) {
            attack.hasExplicitTarget = false;
            attack.explicitTarget = EntityHandle{};
        }
    }

    // Check memory lastTarget
    if (!hasTarget && ctx.memoryData && ctx.memoryData->lastTarget.isValid()) {
        size_t targetIdx = edm.getIndex(ctx.memoryData->lastTarget);
        if (targetIdx != SIZE_MAX && edm.getHotDataByIndex(targetIdx).isAlive()) {
            targetPos = edm.getHotDataByIndex(targetIdx).transform.position;
            hasTarget = true;
        }
    }

    // Check memory lastAttacker
    if (!hasTarget && ctx.memoryData && ctx.memoryData->lastAttacker.isValid()) {
        size_t attackerIdx = edm.getIndex(ctx.memoryData->lastAttacker);
        if (attackerIdx != SIZE_MAX && edm.getHotDataByIndex(attackerIdx).isAlive()) {
            targetPos = edm.getHotDataByIndex(attackerIdx).transform.position;
            hasTarget = true;
        }
    }

    // Throttled scan for nearest different-faction entity (safety net)
    if (!hasTarget && ctx.characterData) {
        attack.scanCooldown -= ctx.deltaTime;
        if (attack.scanCooldown <= 0.0f) {
            EntityHandle scannedTarget = scanForNearestTarget(
                ctx.transform.position, ctx.characterData->faction, ctx.edmIndex);
            if (scannedTarget.isValid()) {
                size_t scanIdx = edm.getIndex(scannedTarget);
                if (scanIdx != SIZE_MAX) {
                    targetPos = edm.getHotDataByIndex(scanIdx).transform.position;
                    hasTarget = true;
                    // Set as explicit target for subsequent frames
                    attack.hasExplicitTarget = true;
                    attack.explicitTarget = scannedTarget;
                    attack.scanCooldown = 0.0f;
                }
            }
            if (!hasTarget) {
                attack.scanCooldown = 0.5f; // Backoff on failed scan
            }
        }
    }

    // No valid target
    if (!hasTarget) {
        attack.hasTarget = false;
        ctx.transform.velocity = Vector2D(0, 0);
        // Was actively fighting or stuck seeking too long — return to passive behavior
        if (attack.inCombat || attack.stateChangeTimer > 1.5f) {
            switchBehavior(ctx.edmIndex, BehaviorType::Idle);
        }
        return;
    }

    Vector2D entityPos = ctx.transform.position;

    // Update timers
    updateTimers(data, ctx.deltaTime);

    // Update target tracking
    if (hasTarget) {
        attack.hasTarget = true;
        attack.lastTargetPosition = targetPos;
        float distSquared = (entityPos - targetPos).lengthSquared();
        attack.targetDistance = std::sqrt(distSquared);

        // Update combat state
        float attackRange = config.attackRange;
        if (!attack.inCombat && attack.targetDistance <= attackRange * COMBAT_ENTER_RANGE_MULT) {
            attack.inCombat = true;
        } else if (attack.inCombat && attack.targetDistance > attackRange * COMBAT_EXIT_RANGE_MULT) {
            attack.inCombat = false;
            changeState(data, AttackState::SEEKING);
        }
    } else {
        attack.hasTarget = false;
        attack.inCombat = false;

        // Don't override message-triggered states like RETREATING
        // RETREATING can continue without a target (using lastTargetPosition)
        if (attack.currentState == static_cast<uint8_t>(AttackState::RETREATING)) {
            // Use lastTargetPosition for retreat direction if available
            if (attack.lastTargetPosition.lengthSquared() > 0.01f) {
                targetPos = attack.lastTargetPosition;
            } else {
                // No known target position - just stop but stay in RETREATING state
                return;
            }
        } else {
            if (attack.currentState != static_cast<uint8_t>(AttackState::SEEKING)) {
                changeState(data, AttackState::SEEKING);
            }
            return;
        }
    }

    // Check for berserker mode
    bool berserkerMode = false;
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        float aggression = ctx.memoryData->emotions.aggression;
        float personalAggression = ctx.memoryData->personality.aggression;
        berserkerMode = (aggression > 0.8f && personalAggression > 0.7f);
    }

    // Determine attack mode and apply mode-specific modifiers
    AttackMode attackMode = static_cast<AttackMode>(attack.attackMode);
    float effectiveRetreatThreshold = config.retreatThreshold;
    float effectiveCooldown = config.attackCooldown;

    // Apply berserker mode modifiers
    if (berserkerMode || attackMode == AttackMode::BERSERKER) {
        applyBerserkerModifiers(data, effectiveRetreatThreshold, effectiveCooldown);
    }

    // Check retreat conditions (respects berserker mode's low threshold)
    if (!berserkerMode && attackMode != AttackMode::BERSERKER &&
        shouldRetreat(ctx.edmIndex, ctx.characterData, effectiveRetreatThreshold, config.aggression) &&
        attack.currentState != static_cast<uint8_t>(AttackState::RETREATING)) {

        bool shouldFlee = false;
        if (ctx.memoryData && ctx.memoryData->isValid()) {
            float fear = ctx.memoryData->emotions.fear;
            float bravery = ctx.memoryData->personality.bravery;
            shouldFlee = (fear > FEAR_FLEE_THRESHOLD && bravery < BRAVERY_FLEE_THRESHOLD);
        }

        // Signal nearby same-faction allies to retreat
        if (ctx.characterData) {
            s_scanBuffer.clear();
            AIManager::Instance().queryEdmIndicesInRadius(
                ctx.transform.position, 200.0f, s_scanBuffer, true);
            auto& edm2 = EntityDataManager::Instance();
            for (size_t idx : s_scanBuffer) {
                if (idx == ctx.edmIndex) continue;
                if (edm2.getCharacterDataByIndex(idx).faction != ctx.characterData->faction) continue;
                Behaviors::deferBehaviorMessage(idx, BehaviorMessage::RETREAT);
            }
        }

        if (shouldFlee) {
            switchBehavior(ctx.edmIndex, BehaviorType::Flee);
            return;
        } else {
            changeState(data, AttackState::RETREATING);
        }
    }

    // Calculate optimal range
    float optimalRange = config.attackRange * config.optimalRangeMultiplier;
    float minimumRange = config.attackRange * config.minimumRangeMultiplier;

    // State machine
    AttackState currentState = static_cast<AttackState>(attack.currentState);

    // Update state timer transitions
    float timeInState = attack.stateChangeTimer;
    if (currentState == AttackState::ATTACKING && timeInState > (1.0f / config.attackSpeed)) {
        changeState(data, AttackState::RECOVERING);
        currentState = AttackState::RECOVERING;
    } else if (currentState == AttackState::RECOVERING && timeInState > config.recoveryTime) {
        changeState(data, AttackState::COOLDOWN);
        currentState = AttackState::COOLDOWN;
    } else if (currentState == AttackState::COOLDOWN && timeInState > effectiveCooldown) {
        attack.canAttack = true;
        changeState(data, attack.hasTarget ? AttackState::APPROACHING : AttackState::SEEKING);
        currentState = static_cast<AttackState>(attack.currentState);
    }

    switch (currentState) {
        case AttackState::SEEKING:
            if (attack.hasTarget) {
                changeState(data, AttackState::APPROACHING);
            }
            break;

        case AttackState::APPROACHING:
            if (attack.targetDistance <= optimalRange) {
                changeState(data, AttackState::POSITIONING);
            } else {
                moveToPosition(ctx.edmIndex, targetPos, data.moveSpeed);
            }
            break;

        case AttackState::POSITIONING: {
            float distToTarget = attack.targetDistance;
            Vector2D direction = normalizeDir(entityPos - targetPos);

            // Apply mode-specific positioning
            bool modeHandled = false;
            switch (attackMode) {
                case AttackMode::RANGED:
                    modeHandled = applyRangedPositioning(ctx.edmIndex, entityPos, targetPos, data, config);
                    break;
                case AttackMode::CHARGE:
                    modeHandled = applyChargePositioning(ctx.edmIndex, entityPos, targetPos, data, config);
                    break;
                case AttackMode::HIT_AND_RUN:
                    modeHandled = applyHitAndRunPositioning(ctx.edmIndex, entityPos, targetPos, data, config);
                    break;
                default:
                    // MELEE, AMBUSH, COORDINATED, BERSERKER use standard positioning
                    break;
            }

            if (modeHandled) break;

            // Enforce minimum range - back off if too close
            if (distToTarget < minimumRange && minimumRange > 0.0f) {
                Vector2D backoffPos = targetPos + direction * (minimumRange + 10.0f);
                moveToPosition(ctx.edmIndex, backoffPos, data.moveSpeed * 0.8f);
                break;
            }

            // Circle strafing if enabled and within optimal range
            if (config.circleStrafe && distToTarget <= optimalRange * 1.2f) {
                attack.strafeTimer += ctx.deltaTime;
                if (attack.strafeTimer > STRAFE_INTERVAL) {
                    attack.strafeTimer = 0.0f;
                    // Alternate strafe direction
                    attack.circleStrafing = !attack.circleStrafing;
                }

                // Calculate perpendicular strafe direction
                Vector2D perpendicular(-direction.getY(), direction.getX());
                float strafeSign = attack.circleStrafing ? 1.0f : -1.0f;
                Vector2D strafePos = targetPos + direction * optimalRange +
                                    perpendicular * (config.strafeRadius * strafeSign * 0.5f);
                moveToPosition(ctx.edmIndex, strafePos, data.moveSpeed);
                break;
            }

            // Standard optimal positioning
            if (config.preferredAttackAngle != 0.0f) {
                direction = rotateVector(direction, config.preferredAttackAngle);
            }
            Vector2D optimalPos = targetPos + direction * optimalRange;

            if ((entityPos - optimalPos).length() > 15.0f) {
                moveToPosition(ctx.edmIndex, optimalPos, data.moveSpeed);
            } else if (attack.canAttack) {
                changeState(data, AttackState::ATTACKING);
            }
            break;
        }

        case AttackState::ATTACKING:
            if (s_specialRoll(s_rng) < config.specialAttackChance && attack.specialAttackReady) {
                float specialDamage = calculateDamage(data, config) * SPECIAL_ATTACK_MULTIPLIER;
                Vector2D knockback = calculateKnockbackVector(entityPos, targetPos) * config.knockbackForce * SPECIAL_ATTACK_MULTIPLIER;
                // Resolve target: explicit > memory lastTarget > memory lastAttacker
                EntityHandle targetHandle{};
                if (attack.hasExplicitTarget && attack.explicitTarget.isValid()) {
                    targetHandle = attack.explicitTarget;
                } else if (ctx.memoryData && ctx.memoryData->lastTarget.isValid()) {
                    targetHandle = ctx.memoryData->lastTarget;
                } else if (ctx.memoryData && ctx.memoryData->lastAttacker.isValid()) {
                    targetHandle = ctx.memoryData->lastAttacker;
                }
                if (targetHandle.isValid()) {
                    applyDamageToTarget(targetHandle, specialDamage, knockback, edm.getHandle(ctx.edmIndex));
                }
                attack.specialAttackReady = false;
            } else {
                executeAttackAction(ctx.edmIndex, targetPos, data, config, ctx.characterData);
            }
            changeState(data, AttackState::RECOVERING);
            break;

        case AttackState::RECOVERING:
            // Stay in place during recovery
            break;

        case AttackState::RETREATING: {
            auto& edm = EntityDataManager::Instance();
            Vector2D retreatDir = normalizeDir(entityPos - targetPos);
            Vector2D retreatVelocity = retreatDir * (data.moveSpeed * RETREAT_SPEED_MULTIPLIER);
            edm.getHotDataByIndex(ctx.edmIndex).transform.velocity = retreatVelocity;

            // Don't exit retreat immediately - require minimum time in state
            // This prevents message-triggered retreats from being instantly cancelled
            constexpr float MIN_RETREAT_TIME = 0.5f;
            if (attack.stateChangeTimer >= MIN_RETREAT_TIME) {
                if (attack.targetDistance > config.attackRange * 2.0f ||
                    !shouldRetreat(ctx.edmIndex, ctx.characterData, config.retreatThreshold, config.aggression)) {
                    attack.isRetreating = false;
                    changeState(data, AttackState::SEEKING);
                }
            }
            break;
        }

        case AttackState::COOLDOWN:
            // Wait during cooldown
            break;
    }
}

std::vector<EventManager::DeferredEvent> collectDeferredDamageEvents() {
    std::vector<EventManager::DeferredEvent> result;
    result.swap(t_deferredDamageEvents);
    return result;
}

} // namespace Behaviors
