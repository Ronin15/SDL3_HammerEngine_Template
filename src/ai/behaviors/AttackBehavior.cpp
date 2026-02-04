/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "ai/BehaviorExecutors.hpp"
#include "events/EntityEvents.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include <algorithm>
#include <cmath>
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

// ============================================================================
// CONSTANTS
// ============================================================================

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

    auto damageEvent = std::make_shared<DamageEvent>(
        EntityEventType::DamageIntent, attackerHandle, targetHandle,
        damage, scaledKnockback);

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

bool shouldRetreat(size_t edmIndex, float retreatThreshold, float aggression) {
    if (edmIndex == SIZE_MAX) return false;

    auto& edm = EntityDataManager::Instance();
    const auto& charData = edm.getCharacterDataByIndex(edmIndex);
    float healthRatio = charData.health / charData.maxHealth;

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
                         const HammerEngine::AttackBehaviorConfig& config) {
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
    EntityHandle targetHandle = AIManager::Instance().getPlayerHandle();

    // Check for explicit target
    if (attack.hasExplicitTarget && attack.explicitTarget.isValid()) {
        targetHandle = attack.explicitTarget;
    }

    applyDamageToTarget(targetHandle, damage, knockback, attackerHandle);

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

} // anonymous namespace

namespace Behaviors {

// ============================================================================
// PUBLIC API
// ============================================================================

void initAttack(size_t edmIndex, const HammerEngine::AttackBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Attack);
    auto& data = edm.getBehaviorData(edmIndex);
    auto& attack = data.state.attack;

    attack.lastTargetPosition = Vector2D(0, 0);
    attack.attackPosition = Vector2D(0, 0);
    attack.retreatPosition = Vector2D(0, 0);
    attack.strafeVector = Vector2D(0, 0);
    attack.currentState = 0; // SEEKING
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
    attack.hasExplicitTarget = false;
    attack.explicitTarget = EntityHandle{};

    data.setInitialized(true);
    (void)config;
}

void executeAttack(BehaviorContext& ctx, const HammerEngine::AttackBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid()) return;

    auto& data = *ctx.behaviorData;
    auto& attack = data.state.attack;

    // Determine target position
    Vector2D targetPos;
    bool hasTarget = false;

    // Check explicit target first
    if (attack.hasExplicitTarget && attack.explicitTarget.isValid()) {
        auto& edm = EntityDataManager::Instance();
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

    // Fall back to player
    if (!hasTarget && ctx.playerValid) {
        targetPos = ctx.playerPosition;
        hasTarget = true;
    }

    // Neutral entities become enemy when attacking
    if (hasTarget && ctx.edmIndex != SIZE_MAX) {
        auto& edm = EntityDataManager::Instance();
        const auto& charData = edm.getCharacterDataByIndex(ctx.edmIndex);
        if (charData.faction == 2) {
            edm.setFaction(edm.getHandle(ctx.edmIndex), 1);
            AIManager::Instance().broadcastMessage("player_under_attack", true);
        }
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
        if (attack.currentState != static_cast<uint8_t>(AttackState::SEEKING)) {
            changeState(data, AttackState::SEEKING);
        }
        return;
    }

    // Check for berserker mode
    bool berserkerMode = false;
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        float aggression = ctx.memoryData->emotions.aggression;
        float personalAggression = ctx.memoryData->personality.aggression;
        berserkerMode = (aggression > 0.8f && personalAggression > 0.7f);
    }

    // Check retreat conditions
    if (!berserkerMode && shouldRetreat(ctx.edmIndex, config.retreatThreshold, config.aggression) &&
        attack.currentState != static_cast<uint8_t>(AttackState::RETREATING)) {

        bool shouldFlee = false;
        if (ctx.memoryData && ctx.memoryData->isValid()) {
            float fear = ctx.memoryData->emotions.fear;
            float bravery = ctx.memoryData->personality.bravery;
            shouldFlee = (fear > FEAR_FLEE_THRESHOLD && bravery < BRAVERY_FLEE_THRESHOLD);
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
    } else if (currentState == AttackState::COOLDOWN && timeInState > config.attackCooldown) {
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
                moveToPosition(ctx.edmIndex, targetPos, config.movementSpeed);
            }
            break;

        case AttackState::POSITIONING: {
            float distToTarget = attack.targetDistance;
            Vector2D direction = normalizeDir(entityPos - targetPos);

            // Enforce minimum range - back off if too close
            if (distToTarget < minimumRange && minimumRange > 0.0f) {
                Vector2D backoffPos = targetPos + direction * (minimumRange + 10.0f);
                moveToPosition(ctx.edmIndex, backoffPos, config.movementSpeed * 0.8f);
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
                moveToPosition(ctx.edmIndex, strafePos, config.movementSpeed);
                break;
            }

            // Standard optimal positioning
            if (config.preferredAttackAngle != 0.0f) {
                direction = rotateVector(direction, config.preferredAttackAngle);
            }
            Vector2D optimalPos = targetPos + direction * optimalRange;

            if ((entityPos - optimalPos).length() > 15.0f) {
                moveToPosition(ctx.edmIndex, optimalPos, config.movementSpeed);
            } else if (attack.canAttack) {
                changeState(data, AttackState::ATTACKING);
            }
            break;
        }

        case AttackState::ATTACKING:
            if (s_specialRoll(s_rng) < config.specialAttackChance && attack.specialAttackReady) {
                float specialDamage = calculateDamage(data, config) * SPECIAL_ATTACK_MULTIPLIER;
                Vector2D knockback = calculateKnockbackVector(entityPos, targetPos) * config.knockbackForce * SPECIAL_ATTACK_MULTIPLIER;
                EntityHandle targetHandle = attack.hasExplicitTarget ? attack.explicitTarget : AIManager::Instance().getPlayerHandle();
                auto& edm = EntityDataManager::Instance();
                applyDamageToTarget(targetHandle, specialDamage, knockback, edm.getHandle(ctx.edmIndex));
                attack.specialAttackReady = false;
            } else {
                executeAttackAction(ctx.edmIndex, targetPos, data, config);
            }
            changeState(data, AttackState::RECOVERING);
            break;

        case AttackState::RECOVERING:
            // Stay in place during recovery
            break;

        case AttackState::RETREATING: {
            auto& edm = EntityDataManager::Instance();
            Vector2D retreatDir = normalizeDir(entityPos - targetPos);
            Vector2D retreatVelocity = retreatDir * (config.movementSpeed * RETREAT_SPEED_MULTIPLIER);
            edm.getHotDataByIndex(ctx.edmIndex).transform.velocity = retreatVelocity;

            if (attack.targetDistance > config.attackRange * 2.0f || !shouldRetreat(ctx.edmIndex, config.retreatThreshold, config.aggression)) {
                attack.isRetreating = false;
                changeState(data, AttackState::SEEKING);
            }
            break;
        }

        case AttackState::COOLDOWN:
            // Wait during cooldown
            break;
    }
}

std::vector<EventManager::DeferredEvent> collectDeferredDamageEvents() {
    std::vector<EventManager::DeferredEvent> result = std::move(t_deferredDamageEvents);
    t_deferredDamageEvents.clear();
    return result;
}

} // namespace Behaviors
