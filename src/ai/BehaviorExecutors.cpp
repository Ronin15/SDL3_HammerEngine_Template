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

    // Compare elapsed time since last combat against threshold
    // lastCombatTime stores the absolute game time when combat occurred
    float elapsed = ctx.gameTime - memory.lastCombatTime;
    return elapsed >= 0.0f && elapsed < thresholdSeconds;
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

} // namespace Behaviors
