/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "ai/AIBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleVariation{-0.5f, 0.5f};
thread_local std::uniform_real_distribution<float> s_panicVariation{0.8f, 1.2f};

constexpr float DEFAULT_MAX_STAMINA = 100.0f;
constexpr float DEFAULT_STAMINA_DRAIN = 10.0f;
constexpr float DEFAULT_STAMINA_RECOVERY = 5.0f;
constexpr float DEFAULT_PANIC_DURATION = 5.0f;
constexpr float DEFAULT_ZIGZAG_INTERVAL = 0.5f;
constexpr float DEFAULT_ZIGZAG_ANGLE = 30.0f;

Vector2D normalizeVector(const Vector2D& direction) {
    float magnitude = direction.length();
    if (magnitude < 0.001f) return Vector2D(1, 0);
    return direction / magnitude;
}

Vector2D avoidBoundaries(const Vector2D& position, const Vector2D& direction, float padding) {
    float minX, minY, maxX, maxY;
    if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        return direction;
    }

    Vector2D adjustedDir = direction;
    constexpr float TILE = HammerEngine::TILE_SIZE;
    float worldMinX = minX * TILE + padding;
    float worldMinY = minY * TILE + padding;
    float worldMaxX = maxX * TILE - padding;
    float worldMaxY = maxY * TILE - padding;

    if (position.getX() < worldMinX && direction.getX() < 0) {
        adjustedDir.setX(std::abs(direction.getX()));
    } else if (position.getX() > worldMaxX && direction.getX() > 0) {
        adjustedDir.setX(-std::abs(direction.getX()));
    }

    if (position.getY() < worldMinY && direction.getY() < 0) {
        adjustedDir.setY(std::abs(direction.getY()));
    } else if (position.getY() > worldMaxY && direction.getY() > 0) {
        adjustedDir.setY(-std::abs(direction.getY()));
    }

    return adjustedDir;
}

Vector2D calculateFleeDirection(const Vector2D& entityPos, const Vector2D& threatPos,
                                const BehaviorData& data, float padding) {
    const auto& flee = data.state.flee;
    Vector2D fleeDir = entityPos - threatPos;

    if (fleeDir.length() < 0.001f) {
        if (flee.fleeDirection.length() > 0.001f) {
            fleeDir = flee.fleeDirection;
        } else {
            float angle = s_angleVariation(s_rng) * 2.0f * static_cast<float>(M_PI);
            fleeDir = Vector2D(std::cos(angle), std::sin(angle));
        }
    }

    fleeDir = avoidBoundaries(entityPos, fleeDir, padding);
    return normalizeVector(fleeDir);
}

float calculateFleeSpeedModifier(const BehaviorData& data, bool useStamina) {
    const auto& flee = data.state.flee;
    float modifier = 1.0f;

    if (flee.isInPanic) modifier *= 1.3f;
    modifier *= (1.0f + flee.fearBoost * 0.4f);

    if (useStamina) {
        float staminaRatio = flee.currentStamina / DEFAULT_MAX_STAMINA;
        modifier *= (0.3f + 0.7f * staminaRatio);
    }

    return modifier;
}

void updateStamina(BehaviorData& data, float deltaTime, bool fleeing) {
    auto& flee = data.state.flee;
    if (fleeing) {
        flee.currentStamina -= DEFAULT_STAMINA_DRAIN * deltaTime;
        flee.currentStamina = std::max(0.0f, flee.currentStamina);
    } else {
        flee.currentStamina += DEFAULT_STAMINA_RECOVERY * deltaTime;
        flee.currentStamina = std::min(DEFAULT_MAX_STAMINA, flee.currentStamina);
    }
}

bool tryFollowPathToGoal(BehaviorContext& ctx, const Vector2D& goal, float speed) {
    if (!ctx.behaviorData || !ctx.pathData) return false;

    const auto& flee = ctx.behaviorData->state.flee;
    auto& pathData = *ctx.pathData;
    Vector2D currentPos = ctx.transform.position;

    constexpr float pathTTL = 3.5f;
    constexpr float noProgressWindow = 0.4f;
    constexpr float GOAL_CHANGE_THRESH_SQUARED = 180.0f * 180.0f;

    const bool skipRefresh = (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
                              pathData.progressTimer < noProgressWindow);
    auto& edm = EntityDataManager::Instance();
    bool needRefresh = !pathData.hasPath || pathData.navIndex >= pathData.pathLength;

    if (!skipRefresh && !needRefresh && pathData.isFollowingPath()) {
        float d = (edm.getWaypoint(ctx.edmIndex, pathData.navIndex) - currentPos).length();
        if (d + 1.0f < pathData.lastNodeDistance) {
            pathData.lastNodeDistance = d;
            pathData.progressTimer = 0.0f;
        } else if (pathData.progressTimer > noProgressWindow) {
            needRefresh = true;
        }
    }

    if (!skipRefresh && pathData.pathUpdateTimer > pathTTL) needRefresh = true;

    if (needRefresh && pathData.pathRequestCooldown <= 0.0f) {
        bool goalChanged = true;
        if (!skipRefresh && pathData.hasPath && pathData.pathLength > 0) {
            Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
            goalChanged = ((goal - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
        }

        if (goalChanged) {
            auto& pf = PathfinderManager::Instance();
            pf.requestPathToEDM(ctx.edmIndex, pf.clampToWorldBounds(currentPos, 100.0f), goal,
                               PathfinderManager::Priority::High);
            pathData.pathRequestCooldown = 0.8f;
        }
    }

    if (pathData.isFollowingPath()) {
        Vector2D node = ctx.pathData->currentWaypoint;
        Vector2D dir = node - currentPos;
        float len = dir.length();

        if (len > 0.01f) {
            dir = dir * (1.0f / len);
            ctx.transform.velocity = dir * speed;
        }

        if (len <= flee.navRadius) {
            edm.advanceWaypointWithCache(ctx.edmIndex);
        }
        return true;
    }

    return false;
}

void updatePanicFlee(BehaviorContext& ctx, const Vector2D& threatPos,
                     const HammerEngine::FleeBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& flee = data.state.flee;
    Vector2D currentPos = ctx.transform.position;

    if (flee.directionChangeTimer > 0.2f || flee.fleeDirection.length() < 0.001f) {
        flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data, config.worldPadding);
        float randomAngle = s_angleVariation(s_rng) * 0.8f;
        float cos_a = std::cos(randomAngle);
        float sin_a = std::sin(randomAngle);
        Vector2D rotated(flee.fleeDirection.getX() * cos_a - flee.fleeDirection.getY() * sin_a,
                        flee.fleeDirection.getX() * sin_a + flee.fleeDirection.getY() * cos_a);
        flee.fleeDirection = rotated;
        flee.directionChangeTimer = 0.0f;
    }

    float speedModifier = calculateFleeSpeedModifier(data, false);
    ctx.transform.velocity = flee.fleeDirection * config.fleeSpeed * speedModifier;
}

void updateStrategicRetreat(BehaviorContext& ctx, const Vector2D& threatPos,
                            const HammerEngine::FleeBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& flee = data.state.flee;
    Vector2D currentPos = ctx.transform.position;

    if (flee.directionChangeTimer > 1.0f || flee.fleeDirection.length() < 0.001f) {
        flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data, config.worldPadding);
        flee.directionChangeTimer = 0.0f;
    }

    float baseRetreatDistance = 800.0f;
    int nearbyCount = AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 100.0f);

    float retreatDistance = baseRetreatDistance;
    if (nearbyCount > 2) {
        retreatDistance = baseRetreatDistance * 1.8f;
        Vector2D lateral(-flee.fleeDirection.getY(), flee.fleeDirection.getX());
        float lateralBias = ((float)(ctx.entityId % 5) - 2.0f) * 0.3f;
        flee.fleeDirection = (flee.fleeDirection + lateral * lateralBias).normalized();
    } else if (nearbyCount > 0) {
        retreatDistance = baseRetreatDistance * 1.3f;
    }

    Vector2D dest = PathfinderManager::Instance().clampToWorldBounds(
        currentPos + flee.fleeDirection * retreatDistance, 100.0f);

    float speedModifier = calculateFleeSpeedModifier(data, false);
    if (!tryFollowPathToGoal(ctx, dest, config.fleeSpeed * speedModifier)) {
        ctx.transform.velocity = flee.fleeDirection * config.fleeSpeed * speedModifier;
    }
}

void updateEvasiveManeuver(BehaviorContext& ctx, const Vector2D& threatPos,
                           const HammerEngine::FleeBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& flee = data.state.flee;
    Vector2D currentPos = ctx.transform.position;

    if (flee.zigzagTimer > DEFAULT_ZIGZAG_INTERVAL) {
        flee.zigzagDirection *= -1;
        flee.zigzagTimer = 0.0f;
    }

    Vector2D baseFleeDir = calculateFleeDirection(currentPos, threatPos, data, config.worldPadding);

    float zigzagAngleRad = (DEFAULT_ZIGZAG_ANGLE * static_cast<float>(M_PI) / 180.0f) * flee.zigzagDirection;
    float cos_z = std::cos(zigzagAngleRad);
    float sin_z = std::sin(zigzagAngleRad);

    Vector2D zigzagDir(baseFleeDir.getX() * cos_z - baseFleeDir.getY() * sin_z,
                      baseFleeDir.getX() * sin_z + baseFleeDir.getY() * cos_z);

    flee.fleeDirection = normalizeVector(zigzagDir);

    float speedModifier = calculateFleeSpeedModifier(data, false);
    ctx.transform.velocity = flee.fleeDirection * config.fleeSpeed * speedModifier;
}

void updateSeekCover(BehaviorContext& ctx, const Vector2D& threatPos,
                     const HammerEngine::FleeBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& flee = data.state.flee;
    Vector2D currentPos = ctx.transform.position;

    float baseCoverDistance = 720.0f;
    int nearbyCount = AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 90.0f);

    float coverDistance = baseCoverDistance;
    if (nearbyCount > 2) {
        coverDistance = baseCoverDistance * 1.6f;
    } else if (nearbyCount > 0) {
        coverDistance = baseCoverDistance * 1.2f;
    }

    flee.fleeDirection = calculateFleeDirection(currentPos, threatPos, data, config.worldPadding);
    Vector2D dest = PathfinderManager::Instance().clampToWorldBounds(
        currentPos + flee.fleeDirection * coverDistance, 100.0f);

    float speedModifier = calculateFleeSpeedModifier(data, false);
    if (!tryFollowPathToGoal(ctx, dest, config.fleeSpeed * speedModifier)) {
        ctx.transform.velocity = flee.fleeDirection * config.fleeSpeed * speedModifier;
    }
}

} // anonymous namespace

namespace Behaviors {

void initFlee(size_t edmIndex, const HammerEngine::FleeBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Flee);
    auto& data = edm.getBehaviorData(edmIndex);
    auto& flee = data.state.flee;
    auto& hotData = edm.getHotDataByIndex(edmIndex);

    flee.lastThreatPosition = hotData.transform.position;
    flee.fleeDirection = Vector2D(0, 0);
    flee.lastKnownSafeDirection = Vector2D(0, 0);
    flee.fleeTimer = 0.0f;
    flee.directionChangeTimer = 0.0f;
    flee.panicTimer = 0.0f;
    flee.currentStamina = DEFAULT_MAX_STAMINA;
    flee.zigzagTimer = 0.0f;
    flee.navRadius = 18.0f;
    flee.backoffTimer = 0.0f;
    flee.zigzagDirection = 1;
    flee.isFleeing = false;
    flee.isInPanic = false;
    flee.hasValidThreat = false;
    flee.fearBoost = 0.0f;

    data.setInitialized(true);
    (void)config;
}

void executeFlee(BehaviorContext& ctx, const HammerEngine::FleeBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid() || !ctx.pathData) return;

    auto& data = *ctx.behaviorData;
    auto& flee = data.state.flee;
    auto& pathData = *ctx.pathData;

    // Update timers
    flee.fleeTimer += ctx.deltaTime;
    flee.directionChangeTimer += ctx.deltaTime;
    if (flee.panicTimer > 0.0f) flee.panicTimer -= ctx.deltaTime;
    flee.zigzagTimer += ctx.deltaTime;
    if (flee.backoffTimer > 0.0f) flee.backoffTimer -= ctx.deltaTime;
    data.lastCrowdAnalysis += ctx.deltaTime;

    // Cache fear from emotions
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        float braveryFactor = ctx.memoryData->personality.bravery;
        float baseFear = ctx.memoryData->emotions.fear;
        flee.fearBoost = baseFear * (1.5f - braveryFactor);
    } else {
        flee.fearBoost = 0.0f;
    }

    // Update path timers
    pathData.pathUpdateTimer += ctx.deltaTime;
    pathData.progressTimer += ctx.deltaTime;
    if (pathData.pathRequestCooldown > 0.0f) pathData.pathRequestCooldown -= ctx.deltaTime;

    // Determine threat
    Vector2D threatPos;
    bool threatValid = false;

    if (ctx.memoryData && ctx.memoryData->lastAttacker.isValid()) {
        auto& edm = EntityDataManager::Instance();
        size_t attackerIdx = edm.getIndex(ctx.memoryData->lastAttacker);
        if (attackerIdx != SIZE_MAX && edm.getHotDataByIndex(attackerIdx).isAlive()) {
            threatPos = edm.getHotDataByIndex(attackerIdx).transform.position;
            threatValid = true;
        }
    }

    if (!threatValid) {
        if (flee.isFleeing) {
            flee.isFleeing = false;
            flee.isInPanic = false;
            flee.hasValidThreat = false;
        }
        updateStamina(data, ctx.deltaTime, false);
        return;
    }

    float distanceToThreatSquared = (ctx.transform.position - threatPos).lengthSquared();
    float detectionRangeSquared = config.safeDistance * config.safeDistance;
    bool threatInRange = (distanceToThreatSquared <= detectionRangeSquared);

    if (threatInRange) {
        if (!flee.isFleeing) {
            flee.isFleeing = true;
            flee.fleeTimer = 0.0f;
            flee.lastThreatPosition = threatPos;
            flee.isInPanic = true;
            flee.panicTimer = DEFAULT_PANIC_DURATION * s_panicVariation(s_rng);
        }
        flee.hasValidThreat = true;
        flee.lastThreatPosition = threatPos;
    } else if (flee.isFleeing) {
        float safeDistanceSquared = config.safeDistance * config.safeDistance;
        if (distanceToThreatSquared >= safeDistanceSquared) {
            flee.isFleeing = false;
            flee.isInPanic = false;
            flee.hasValidThreat = false;
        }
    }

    if (flee.isInPanic && flee.panicTimer <= 0.0f) {
        flee.isInPanic = false;
    }

    if (flee.isFleeing) {
        // Default to panic flee (most common mode)
        if (flee.isInPanic) {
            updatePanicFlee(ctx, threatPos, config);
        } else {
            updateStrategicRetreat(ctx, threatPos, config);
        }
        updateStamina(data, ctx.deltaTime, true);
    }
}

} // namespace Behaviors
