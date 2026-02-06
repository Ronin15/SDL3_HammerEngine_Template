/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/EntityDataManager.hpp"
#include <cmath>

namespace Behaviors {

// ============================================================================
// MAIN DISPATCHER
// ============================================================================

void execute(BehaviorContext& ctx, const HammerEngine::BehaviorConfigData& configData) {
    switch (configData.type) {
        case BehaviorType::Idle:
            executeIdle(ctx, configData.config.idle);
            break;
        case BehaviorType::Wander:
            executeWander(ctx, configData.config.wander);
            break;
        case BehaviorType::Chase:
            executeChase(ctx, configData.config.chase);
            break;
        case BehaviorType::Patrol:
            executePatrol(ctx, configData.config.patrol);
            break;
        case BehaviorType::Guard:
            executeGuard(ctx, configData.config.guard);
            break;
        case BehaviorType::Attack:
            executeAttack(ctx, configData.config.attack);
            break;
        case BehaviorType::Flee:
            executeFlee(ctx, configData.config.flee);
            break;
        case BehaviorType::Follow:
            executeFollow(ctx, configData.config.follow);
            break;
        case BehaviorType::Custom:
        case BehaviorType::COUNT:
        case BehaviorType::None:
        default:
            // No-op for unknown behavior types
            break;
    }
}

void init(size_t edmIndex, const HammerEngine::BehaviorConfigData& configData) {
    switch (configData.type) {
        case BehaviorType::Idle:
            initIdle(edmIndex, configData.config.idle);
            break;
        case BehaviorType::Wander:
            initWander(edmIndex, configData.config.wander);
            break;
        case BehaviorType::Chase:
            initChase(edmIndex, configData.config.chase);
            break;
        case BehaviorType::Patrol:
            initPatrol(edmIndex, configData.config.patrol);
            break;
        case BehaviorType::Guard:
            initGuard(edmIndex, configData.config.guard);
            break;
        case BehaviorType::Attack:
            initAttack(edmIndex, configData.config.attack);
            break;
        case BehaviorType::Flee:
            initFlee(edmIndex, configData.config.flee);
            break;
        case BehaviorType::Follow:
            initFollow(edmIndex, configData.config.follow);
            break;
        case BehaviorType::Custom:
        case BehaviorType::COUNT:
        case BehaviorType::None:
        default:
            // No-op for unknown behavior types
            break;
    }
}

// ============================================================================
// BEHAVIOR SWITCHING
// ============================================================================

void switchBehavior(size_t edmIndex, BehaviorType newType) {
    auto config = getDefaultConfig(newType);
    switchBehavior(edmIndex, config);
}

void switchBehavior(size_t edmIndex, const HammerEngine::BehaviorConfigData& config) {
    auto& edm = EntityDataManager::Instance();

    // Clear old behavior state
    edm.clearBehaviorData(edmIndex);

    // Set new config
    edm.setBehaviorConfig(edmIndex, config);

    // Initialize new behavior state
    init(edmIndex, config);
}

// ============================================================================
// DEFAULT CONFIGS
// ============================================================================

HammerEngine::BehaviorConfigData getDefaultConfig(BehaviorType type) {
    using namespace HammerEngine;

    switch (type) {
        case BehaviorType::Idle:
            return BehaviorConfigData::makeIdle(IdleBehaviorConfig::createSubtleSway());
        case BehaviorType::Wander:
            return BehaviorConfigData::makeWander();
        case BehaviorType::Chase:
            return BehaviorConfigData::makeChase();
        case BehaviorType::Patrol:
            return BehaviorConfigData::makePatrol();
        case BehaviorType::Guard:
            return BehaviorConfigData::makeGuard();
        case BehaviorType::Attack:
            return BehaviorConfigData::makeAttack();
        case BehaviorType::Flee:
            return BehaviorConfigData::makeFlee();
        case BehaviorType::Follow:
            return BehaviorConfigData::makeFollow();
        default:
            // Return an "empty" config for unknown types
            return BehaviorConfigData{};
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

bool isUnderRecentAttack(const BehaviorContext& ctx, float thresholdSeconds) {
    if (!ctx.memoryData) return false;

    const auto& memory = *ctx.memoryData;
    if (!memory.isValid() || !memory.lastAttacker.isValid()) return false;

    // Delta-based timing: lastCombatTime starts at 0 when combat occurs and
    // increments each frame via updateEmotionalDecay(). Combat is "recent"
    // if elapsed time is less than threshold.
    return memory.lastCombatTime < thresholdSeconds;
}

bool shouldFleeFromFear(const BehaviorContext& ctx) {
    if (!ctx.memoryData || !ctx.memoryData->isValid()) return false;
    float fear = ctx.memoryData->emotions.fear;
    float bravery = ctx.memoryData->personality.bravery;

    // Crowd courage: nearby allies boost effective bravery
    if (ctx.behaviorData) {
        int nearbyCount = ctx.behaviorData->cachedNearbyCount;
        float crowdBoost = std::min(0.3f, nearbyCount * 0.05f);  // Up to +0.3 from 6+ allies
        bravery = std::min(1.0f, bravery + crowdBoost);
    }

    return (fear > 0.7f && bravery < 0.3f);
}

bool isOnAlert(const BehaviorContext& ctx, float suspicionThreshold) {
    if (!ctx.memoryData || !ctx.memoryData->isValid()) return false;
    return ctx.memoryData->emotions.suspicion > suspicionThreshold;
}

bool shouldEngageEnemy(const BehaviorContext& ctx) {
    if (!ctx.memoryData || !ctx.memoryData->isValid()) return false;
    if (!ctx.characterData) return false;

    // Only hostile-faction NPCs proactively engage
    if (ctx.characterData->faction != 1) return false;

    float aggression = ctx.memoryData->emotions.aggression;
    float personalAggression = ctx.memoryData->personality.aggression;

    // Need meaningful aggression to proactively attack
    if (aggression + personalAggression < 0.8f) return false;

    // Check if player is valid and within reasonable range
    if (ctx.playerValid) {
        float distSq = Vector2D::distanceSquared(ctx.transform.position, ctx.playerPosition);
        constexpr float ENGAGE_RANGE_SQ = 300.0f * 300.0f;
        if (distSq < ENGAGE_RANGE_SQ) return true;
    }

    return false;
}

EntityHandle getLastAttacker(const BehaviorContext& ctx) {
    if (!ctx.memoryData) return EntityHandle{};
    return ctx.memoryData->lastAttacker;
}

Vector2D normalizeDirection(const Vector2D& vector) {
    float len = std::sqrt(vector.getX() * vector.getX() + vector.getY() * vector.getY());
    if (len < 0.0001f) return Vector2D{0.0f, 0.0f};
    return Vector2D{vector.getX() / len, vector.getY() / len};
}

float calculateAngleToTarget(const Vector2D& from, const Vector2D& to) {
    float dx = to.getX() - from.getX();
    float dy = to.getY() - from.getY();
    return std::atan2(dy, dx);
}

// ============================================================================
// COMBAT EVENT PROCESSING
// ============================================================================

void processCombatEvent(size_t index, EntityHandle attacker, EntityHandle target,
                        float damage, bool wasAttacked, float gameTime) {
    auto& edm = EntityDataManager::Instance();

    // Record pure data in EDM
    edm.recordCombatEvent(index, attacker, target, damage, wasAttacked, gameTime);

    // Apply personality-scaled emotional response
    if (!edm.hasMemoryData(index)) return;
    auto& memData = edm.getMemoryData(index);

    float classResilience = edm.getCharacterDataByIndex(index).emotionalResilience;
    float effectiveResilience = memData.personality.getEffectiveResilience(classResilience);
    float emotionScale = 1.0f - effectiveResilience;

    if (wasAttacked) {
        float fearScale = emotionScale * (1.0f - memData.personality.bravery * 0.5f);
        float fearIncrease = (damage / 100.0f) * fearScale;
        edm.modifyEmotions(index, 0.0f, fearIncrease, 0.0f, 0.0f);
    } else {
        float aggressionScale = emotionScale * (1.0f + memData.personality.aggression * 0.5f);
        float aggressionIncrease = (damage / 150.0f) * aggressionScale;
        edm.modifyEmotions(index, aggressionIncrease, 0.0f, 0.0f, 0.0f);
    }
}

void processWitnessedCombat(size_t witnessIndex, EntityHandle attacker,
                            const Vector2D& combatLocation,
                            float gameTime, bool wasDeath) {
    auto& edm = EntityDataManager::Instance();

    if (!edm.hasMemoryData(witnessIndex)) return;
    auto& memData = edm.getMemoryData(witnessIndex);

    // Distance-based intensity falloff
    constexpr float MAX_WITNESS_RANGE_SQ = 500.0f * 500.0f;
    Vector2D witnessPos = edm.getHotDataByIndex(witnessIndex).transform.position;
    float distSq = Vector2D::distanceSquared(witnessPos, combatLocation);
    if (distSq > MAX_WITNESS_RANGE_SQ) return;

    float intensity = 1.0f - (distSq / MAX_WITNESS_RANGE_SQ);

    // Personality modulation: composure reduces emotional impact
    float emotionScale = intensity * (1.0f - memData.personality.composure * 0.5f);

    // Apply emotions via EDM
    float fearDelta = (wasDeath ? 0.3f : 0.15f) * emotionScale;
    float suspicionDelta = 0.2f * emotionScale;
    edm.modifyEmotions(witnessIndex, 0.0f, fearDelta, 0.0f, suspicionDelta);

    // Create and store memory entry
    MemoryEntry mem;
    mem.subject = attacker;
    mem.location = combatLocation;
    mem.timestamp = gameTime;
    mem.value = intensity * 100.0f;
    mem.type = wasDeath ? MemoryType::WitnessedDeath : MemoryType::WitnessedCombat;
    mem.importance = static_cast<uint8_t>(std::min(255.0f, (wasDeath ? 200.0f : 100.0f) * intensity));
    mem.flags = MemoryEntry::FLAG_VALID;
    edm.addMemory(witnessIndex, mem, false);
}

// ============================================================================
// MESSAGE QUEUE FUNCTIONS
// ============================================================================

void queueBehaviorMessage(size_t edmIndex, uint8_t messageId, uint8_t param) {
    auto& edm = EntityDataManager::Instance();
    auto& data = edm.getBehaviorData(edmIndex);
    if (data.pendingMessageCount < 4) {
        data.pendingMessages[data.pendingMessageCount].messageId = messageId;
        data.pendingMessages[data.pendingMessageCount].param = param;
        data.pendingMessageCount++;
    }
}

void clearPendingMessages(size_t edmIndex) {
    auto& edm = EntityDataManager::Instance();
    auto& data = edm.getBehaviorData(edmIndex);
    data.pendingMessageCount = 0;
}

} // namespace Behaviors
