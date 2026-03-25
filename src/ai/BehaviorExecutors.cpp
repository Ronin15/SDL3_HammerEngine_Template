/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "ai/AICommandBus.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/WorldManager.hpp"
#include <cmath>
#include <iterator>
#include <numeric>

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
    HammerEngine::AICommandBus::Instance().enqueueBehaviorTransition(
        edm.getHandle(edmIndex), edmIndex, config);
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
    const auto& memory = ctx.memoryData;
    if (!memory.isValid() || !memory.lastAttacker.isValid()) return false;

    // Delta-based timing: lastCombatTime starts at 0 when combat occurs and
    // increments each frame via updateEmotionalDecay(). Combat is "recent"
    // if elapsed time is less than threshold.
    return memory.lastCombatTime < thresholdSeconds;
}

bool shouldFleeFromFear(const BehaviorContext& ctx) {
    if (!ctx.memoryData.isValid()) return false;
    float fear = ctx.memoryData.emotions.fear;
    float bravery = ctx.memoryData.personality.bravery;

    // Crowd courage: nearby allies boost effective bravery
    int nearbyCount = ctx.behaviorData.cachedNearbyCount;
    float crowdBoost = std::min(0.3f, nearbyCount * 0.05f);  // Up to +0.3 from 6+ allies
    bravery = std::min(1.0f, bravery + crowdBoost);

    return (fear > 0.7f && bravery < 0.3f);
}

bool isOnAlert(const BehaviorContext& ctx, float suspicionThreshold) {
    if (!ctx.memoryData.isValid()) return false;
    return ctx.memoryData.emotions.suspicion > suspicionThreshold;
}

bool shouldRetaliate(const BehaviorContext& ctx) {
    if (!ctx.memoryData.isValid()) return false;
    if (!ctx.memoryData.lastAttacker.isValid()) return false;

    // Verify attacker is still alive
    auto& edm = EntityDataManager::Instance();
    size_t attackerIdx = edm.getIndex(ctx.memoryData.lastAttacker);
    if (attackerIdx == SIZE_MAX) return false;
    if (!edm.getHotDataByIndex(attackerIdx).isAlive()) return false;

    float bravery = ctx.memoryData.personality.bravery;
    float aggression = ctx.memoryData.emotions.aggression + ctx.memoryData.personality.aggression;

    // Crowd courage: nearby allies boost effective bravery
    float crowdBoost = std::min(0.3f, ctx.behaviorData.cachedNearbyCount * 0.05f);
    bravery = std::min(1.0f, bravery + crowdBoost);

    // Fight response: brave + aggressive NPCs retaliate
    return (bravery > 0.4f && aggression > 0.6f);
}

float getRelationshipLevel(EntityHandle npcHandle, EntityHandle subjectHandle) {
    if (!npcHandle.isValid()) {
        return 0.0f;
    }

    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(npcHandle);
    if (idx == SIZE_MAX) {
        return 0.0f;
    }

    if (!edm.hasMemoryData(idx)) {
        return 0.0f;
    }

    const auto& memoryData = edm.getMemoryData(idx);
    if (!memoryData.isValid()) {
        return 0.0f;
    }

    // Relationship from emotional state
    const auto& emotions = memoryData.emotions;
    float relationship = 0.0f;
    relationship -= emotions.suspicion * 0.4f;
    relationship -= emotions.fear * 0.3f;
    relationship -= emotions.aggression * 0.5f;

    // Sum interaction memory values with subject (inline memories only)
    if (subjectHandle.isValid()) {
        relationship += std::accumulate(
            std::begin(memoryData.memories),
            std::end(memoryData.memories),
            0.0f,
            [subjectHandle](float sum, const auto& mem) {
                if (mem.isValid() && mem.subject == subjectHandle &&
                    mem.type == MemoryType::Interaction) {
                    return sum + (mem.value * 0.1f);
                }
                return sum;
            });
    }

    return std::clamp(relationship, -1.0f, 1.0f);
}

EntityHandle getLastAttacker(const BehaviorContext& ctx) {
    return ctx.memoryData.lastAttacker;
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

// ============================================================================
// MESSAGE QUEUE FUNCTIONS
// ============================================================================

void queueBehaviorMessage(size_t edmIndex, uint8_t messageId, uint8_t param) {
    auto& edm = EntityDataManager::Instance();
    HammerEngine::AICommandBus::Instance().enqueueBehaviorMessage(
        edm.getHandle(edmIndex), edmIndex, messageId, param);
}

void clearPendingMessages(size_t edmIndex) {
    auto& edm = EntityDataManager::Instance();
    HammerEngine::AICommandBus::Instance().clearBehaviorMessages(
        edm.getHandle(edmIndex), edmIndex);
    auto& data = edm.getBehaviorData(edmIndex);
    data.pendingMessageCount = 0;
}

// ============================================================================
// WORLD BOUNDS CACHE
// ============================================================================

// Updated from main thread before batch processing; read by worker threads.
// Sequential game loop ordering guarantees happens-before.
namespace {
    struct CachedBounds {
        float minX{0.0f};
        float minY{0.0f};
        float maxX{0.0f};
        float maxY{0.0f};
        bool valid{false};
    };
    CachedBounds s_cachedBounds;
} // anonymous namespace

void cacheWorldBounds() {
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        s_cachedBounds.minX = minX;
        s_cachedBounds.minY = minY;
        s_cachedBounds.maxX = maxX;
        s_cachedBounds.maxY = maxY;
        s_cachedBounds.valid = true;
    }
}

bool getCachedWorldBounds(float& minX, float& minY, float& maxX, float& maxY) {
    if (!s_cachedBounds.valid) return false;
    minX = s_cachedBounds.minX;
    minY = s_cachedBounds.minY;
    maxX = s_cachedBounds.maxX;
    maxY = s_cachedBounds.maxY;
    return true;
}

// ============================================================================
// DEFERRED BEHAVIOR MESSAGE FUNCTIONS
// ============================================================================

void deferBehaviorMessage(size_t targetEdmIndex, uint8_t messageId, uint8_t param) {
    auto& edm = EntityDataManager::Instance();
    HammerEngine::AICommandBus::Instance().enqueueBehaviorMessage(
        edm.getHandle(targetEdmIndex), targetEdmIndex, messageId, param);
}

} // namespace Behaviors
