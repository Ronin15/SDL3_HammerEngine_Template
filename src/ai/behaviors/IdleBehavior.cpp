/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/EntityDataManager.hpp"
#include <cmath>
#include <random>

// Static thread-local RNG pool for memory optimization and thread safety
namespace {
thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleDistribution{0.0f, 2.0f * static_cast<float>(M_PI)};
thread_local std::uniform_real_distribution<float> s_radiusDistribution{0.0f, 1.0f};
thread_local std::uniform_real_distribution<float> s_frequencyVariation{0.5f, 1.5f};

Vector2D generateRandomOffset(float idleRadius) {
    float angle = s_angleDistribution(s_rng);
    float radius = s_radiusDistribution(s_rng) * idleRadius;
    return Vector2D(radius * std::cos(angle), radius * std::sin(angle));
}

float getRandomMovementInterval(float movementFrequency) {
    if (movementFrequency <= 0.0f)
        return std::numeric_limits<float>::max();
    float baseInterval = 1.0f / movementFrequency;
    return baseInterval * s_frequencyVariation(s_rng);
}

float getRandomTurnInterval(float turnFrequency) {
    if (turnFrequency <= 0.0f)
        return std::numeric_limits<float>::max();
    float baseInterval = 1.0f / turnFrequency;
    return baseInterval * s_frequencyVariation(s_rng);
}

void initializeIdleState(const Vector2D& position, BehaviorData& data, const HammerEngine::IdleBehaviorConfig& config) {
    auto& idle = data.state.idle;
    idle.originalPosition = position;
    idle.currentOffset = Vector2D(0, 0);
    idle.movementTimer = 0.0f;
    idle.turnTimer = 0.0f;
    idle.movementInterval = getRandomMovementInterval(config.movementFrequency);
    idle.turnInterval = getRandomTurnInterval(config.turnFrequency);
    idle.currentAngle = 0.0f;
    idle.initialized = true;
}

void updateStationary(BehaviorContext& ctx) {
    ctx.transform.velocity = Vector2D(0, 0);
}

void updateSubtleSway(BehaviorContext& ctx, const HammerEngine::IdleBehaviorConfig& config) {
    auto& data = ctx.behaviorData;
    auto& idle = data.state.idle;
    idle.movementTimer += ctx.deltaTime;

    if (config.movementFrequency > 0.0f && idle.movementTimer >= idle.movementInterval) {
        Vector2D swayDirection = generateRandomOffset(config.idleRadius);
        swayDirection.normalize();
        ctx.transform.velocity = swayDirection * config.swaySpeed;
        idle.movementTimer = 0.0f;
        idle.movementInterval = getRandomMovementInterval(config.movementFrequency);
    }
}

void updateOccasionalTurn(BehaviorContext& ctx, const HammerEngine::IdleBehaviorConfig& config) {
    auto& data = ctx.behaviorData;
    auto& idle = data.state.idle;
    idle.turnTimer += ctx.deltaTime;

    if (config.turnFrequency > 0.0f && idle.turnTimer >= idle.turnInterval) {
        idle.currentAngle = s_angleDistribution(s_rng);
        idle.turnTimer = 0.0f;
        idle.turnInterval = getRandomTurnInterval(config.turnFrequency);
    }

    ctx.transform.velocity = Vector2D(0, 0);
}

void updateLightFidget(BehaviorContext& ctx, const HammerEngine::IdleBehaviorConfig& config) {
    auto& data = ctx.behaviorData;
    auto& idle = data.state.idle;
    idle.movementTimer += ctx.deltaTime;
    idle.turnTimer += ctx.deltaTime;

    if (config.movementFrequency > 0.0f && idle.movementTimer >= idle.movementInterval) {
        Vector2D fidgetDirection = generateRandomOffset(config.idleRadius);
        fidgetDirection.normalize();
        ctx.transform.velocity = fidgetDirection * config.fidgetSpeed;
        idle.movementTimer = 0.0f;
        idle.movementInterval = getRandomMovementInterval(config.movementFrequency);
    }

    if (config.turnFrequency > 0.0f && idle.turnTimer >= idle.turnInterval) {
        idle.currentAngle = s_angleDistribution(s_rng);
        idle.turnTimer = 0.0f;
        idle.turnInterval = getRandomTurnInterval(config.turnFrequency);
    }
}

} // anonymous namespace

namespace Behaviors {

void initIdle(size_t edmIndex, const HammerEngine::IdleBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Idle);
    auto& data = edm.getBehaviorData(edmIndex);
    auto& hotData = edm.getHotDataByIndex(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    initializeIdleState(hotData.transform.position, data, config);
    data.setInitialized(true);
}

void executeIdle(BehaviorContext& ctx, const HammerEngine::IdleBehaviorConfig& config) {
    auto& data = ctx.behaviorData;
    if (!data.isValid()) return;

    if (!data.isInitialized()) {
        initializeIdleState(ctx.transform.position, data, config);
        data.setInitialized(true);
    }

    // Process pending behavior messages
    for (uint8_t i = 0; i < data.pendingMessageCount; ++i) {
        switch (data.pendingMessages[i].messageId) {
            case BehaviorMessage::PANIC:
                // Panic overrides everything — flee regardless of bravery
                data.pendingMessageCount = 0;
                switchBehavior(ctx.edmIndex, BehaviorType::Flee);
                return;
            case BehaviorMessage::CALM_DOWN:
                // Clear fear so NPC doesn't re-trigger flee from residual emotion
                if (ctx.memoryData.isValid()) {
                    ctx.memoryData.emotions.fear = std::max(0.0f, ctx.memoryData.emotions.fear - 0.5f);
                }
                break;
            case BehaviorMessage::RAISE_ALERT:
                if (ctx.memoryData.personality.bravery < 0.4f) {
                    data.pendingMessageCount = 0;
                    switchBehavior(ctx.edmIndex, BehaviorType::Flee);
                    return;
                }
                break;
            default: break;
        }
    }
    data.pendingMessageCount = 0;

    // Combat reaction: recent combat memory drives self-preservation/retaliation.
    if (isUnderRecentAttack(ctx, 2.0f)) {
        if (shouldRetaliate(ctx)) {
            switchBehavior(ctx.edmIndex, BehaviorType::Chase);
        } else {
            switchBehavior(ctx.edmIndex, BehaviorType::Flee);
        }
        return;
    }
    if (shouldFleeFromFear(ctx)) {
        switchBehavior(ctx.edmIndex, BehaviorType::Flee);
        return;
    }

    // Execute behavior based on current mode
    switch (static_cast<HammerEngine::IdleBehaviorConfig::IdleMode>(config.mode)) {
    case HammerEngine::IdleBehaviorConfig::IdleMode::STATIONARY:
        updateStationary(ctx);
        break;
    case HammerEngine::IdleBehaviorConfig::IdleMode::SUBTLE_SWAY:
        updateSubtleSway(ctx, config);
        break;
    case HammerEngine::IdleBehaviorConfig::IdleMode::OCCASIONAL_TURN:
        updateOccasionalTurn(ctx, config);
        break;
    case HammerEngine::IdleBehaviorConfig::IdleMode::LIGHT_FIDGET:
        updateLightFidget(ctx, config);
        break;
    }
}

} // namespace Behaviors
