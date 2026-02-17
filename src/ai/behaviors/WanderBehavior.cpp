/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleDistribution{0.0f, 2.0f * static_cast<float>(M_PI)};
thread_local std::uniform_int_distribution<uint64_t> s_delayDistribution{0, 5000};

void updateTimers(BehaviorData& data, float deltaTime, PathData* pathData) {
    auto& wander = data.state.wander;
    wander.directionChangeTimer += deltaTime;
    wander.lastDirectionFlip += deltaTime;
    wander.stallTimer += deltaTime;
    wander.unstickTimer += deltaTime;

    if (pathData) {
        pathData->pathUpdateTimer += deltaTime;
        pathData->progressTimer += deltaTime;
        if (pathData->pathRequestCooldown > 0.0f) {
            pathData->pathRequestCooldown -= deltaTime;
        }
    }
}

bool handleStartDelay(BehaviorContext& ctx) {
    if (!ctx.behaviorData) return false;
    auto& data = *ctx.behaviorData;
    auto& wander = data.state.wander;
    if (wander.movementStarted) return true;

    if (wander.directionChangeTimer < wander.startDelay) return false;

    wander.movementStarted = true;
    ctx.transform.velocity = wander.currentDirection * data.moveSpeed;
    return true;
}

float calculateMoveDistance(BehaviorData& data, const Vector2D& position,
                           float baseDistance, const HammerEngine::WanderBehaviorConfig& config) {
    int nearbyCount = data.cachedNearbyCount;
    auto& wander = data.state.wander;
    float moveDistance = baseDistance;

    if (nearbyCount > config.crowdEscapeThreshold) {
        moveDistance = baseDistance * config.crowdEscapeDistanceMultiplier;
        if (nearbyCount > 0) {
            Vector2D escapeDirection = (position - data.cachedClusterCenter).normalized();
            float randomOffset = (nearbyCount % 60 - 30) * 0.01f;
            escapeDirection.setX(escapeDirection.getX() + randomOffset);
            escapeDirection.setY(escapeDirection.getY() + randomOffset);
            escapeDirection.normalize();
            wander.currentDirection = escapeDirection;
        }
    } else if (nearbyCount > 5) {
        moveDistance = baseDistance * 2.0f;
        wander.currentDirection = wander.currentDirection.normalized();
    } else if (nearbyCount > 2) {
        moveDistance = baseDistance * 1.3f;
    }

    return moveDistance;
}

void applyBoundaryAvoidance(BehaviorData& data, const Vector2D& position,
                            const HammerEngine::WanderBehaviorConfig& config,
                            const BehaviorContext& ctx) {
    if (!ctx.worldBoundsValid) {
        return;
    }

    auto& wander = data.state.wander;
    const float EDGE_THRESHOLD = config.edgeThreshold;
    Vector2D boundaryForce(0, 0);

    if (position.getX() < ctx.worldMinX + EDGE_THRESHOLD) {
        float strength = 1.0f - ((position.getX() - ctx.worldMinX) / EDGE_THRESHOLD);
        boundaryForce = boundaryForce + Vector2D(strength, 0);
    } else if (position.getX() > ctx.worldMaxX - EDGE_THRESHOLD) {
        float strength = 1.0f - ((ctx.worldMaxX - position.getX()) / EDGE_THRESHOLD);
        boundaryForce = boundaryForce + Vector2D(-strength, 0);
    }

    if (position.getY() < ctx.worldMinY + EDGE_THRESHOLD) {
        float strength = 1.0f - ((position.getY() - ctx.worldMinY) / EDGE_THRESHOLD);
        boundaryForce = boundaryForce + Vector2D(0, strength);
    } else if (position.getY() > ctx.worldMaxY - EDGE_THRESHOLD) {
        float strength = 1.0f - ((ctx.worldMaxY - position.getY()) / EDGE_THRESHOLD);
        boundaryForce = boundaryForce + Vector2D(0, -strength);
    }

    if (boundaryForce.lengthSquared() > 0.01f) {
        wander.currentDirection = (wander.currentDirection * 0.4f + boundaryForce.normalized() * 0.6f).normalized();
    }
}

void handlePathfinding(const BehaviorContext& ctx, const Vector2D& dest,
                       const HammerEngine::WanderBehaviorConfig& config) {
    Vector2D position = ctx.transform.position;
    float distanceToGoal = (dest - position).length();
    if (distanceToGoal < 64.0f || !ctx.pathData) return;

    auto& pathData = *ctx.pathData;
    const bool skipRefresh = (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
                              pathData.progressTimer < 0.8f);
    bool needsNewPath = false;
    if (!skipRefresh) {
        needsNewPath = !pathData.hasPath || pathData.navIndex >= pathData.pathLength ||
                       pathData.pathUpdateTimer > config.pathRefreshInterval;
    }

    bool stuckOnObstacle = pathData.progressTimer > 0.8f;
    if (stuckOnObstacle) pathData.clear();

    if ((needsNewPath || stuckOnObstacle) && pathData.pathRequestCooldown <= 0.0f) {
        const float MIN_GOAL_CHANGE = config.minGoalChangeDistance;
        bool goalChanged = true;
        auto& edm = EntityDataManager::Instance();
        if (!skipRefresh && pathData.hasPath && pathData.pathLength > 0) {
            Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
            float goalDistanceSquared = (dest - lastGoal).lengthSquared();
            goalChanged = (goalDistanceSquared >= MIN_GOAL_CHANGE * MIN_GOAL_CHANGE);
        }

        if (goalChanged) {
            PathfinderManager::Instance().requestPathToEDM(ctx.edmIndex, position, dest,
                                                           PathfinderManager::Priority::Normal);
            pathData.pathRequestCooldown = config.pathRequestCooldown;
        }
    }
}

void chooseNewDirection(BehaviorContext& ctx, const HammerEngine::WanderBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& wander = data.state.wander;
    float angle = s_angleDistribution(s_rng);
    wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    if (wander.movementStarted) {
        ctx.transform.velocity = wander.currentDirection * data.moveSpeed;
    }
    (void)config;  // Config no longer used for speed
}

void handleMovement(BehaviorContext& ctx, const HammerEngine::WanderBehaviorConfig& config) {
    if (!ctx.behaviorData) return;
    auto& data = *ctx.behaviorData;
    auto& wander = data.state.wander;
    float baseDistance = 600.0f;
    Vector2D position = ctx.transform.position;

    float moveDistance = calculateMoveDistance(data, position, baseDistance, config);
    applyBoundaryAvoidance(data, position, config, ctx);

    Vector2D dest = position + wander.currentDirection * moveDistance;

    if (ctx.worldBoundsValid) {
        const float MARGIN = config.worldPaddingMargin;
        dest.setX(std::clamp(dest.getX(), ctx.worldMinX + MARGIN, ctx.worldMaxX - MARGIN));
        dest.setY(std::clamp(dest.getY(), ctx.worldMinY + MARGIN, ctx.worldMaxY - MARGIN));
    }

    handlePathfinding(ctx, dest, config);

    if (!ctx.pathData) {
        ctx.transform.velocity = wander.currentDirection * data.moveSpeed;
        return;
    }
    auto& pathData = *ctx.pathData;

    if (pathData.isFollowingPath()) {
        Vector2D waypoint = ctx.pathData->currentWaypoint;
        Vector2D toWaypoint = waypoint - position;
        float dist = toWaypoint.length();

        if (dist < 64.0f) {
            auto& edm = EntityDataManager::Instance();
            edm.advanceWaypointWithCache(ctx.edmIndex);
            if (pathData.isFollowingPath()) {
                waypoint = ctx.pathData->currentWaypoint;
                toWaypoint = waypoint - position;
                dist = toWaypoint.length();
            }
        }

        if (dist > 0.001f) {
            Vector2D direction = toWaypoint / dist;
            ctx.transform.velocity = direction * data.moveSpeed;
        }
    } else {
        ctx.transform.velocity = wander.currentDirection * data.moveSpeed;
    }

    float speedSq = ctx.transform.velocity.lengthSquared();
    const float stallSpeed = std::max(config.stallSpeed, data.moveSpeed * 0.5f);
    const float stallSpeedSq = stallSpeed * stallSpeed;
    const float stallSeconds = config.stallTimeout;

    if (speedSq < stallSpeedSq) {
        if (wander.stallTimer >= stallSeconds) {
            pathData.clear();
            chooseNewDirection(ctx, config);
            pathData.pathRequestCooldown = 0.6f;
            wander.stallTimer = 0.0f;
            return;
        }
    } else {
        wander.stallTimer = 0.0f;
    }

    float changeIntervalSeconds = config.changeDirectionIntervalMin / 1000.0f;
    if (wander.directionChangeTimer >= changeIntervalSeconds) {
        chooseNewDirection(ctx, config);
        wander.directionChangeTimer = 0.0f;
    }

    const float jitterThresholdSq = (data.moveSpeed * 1.5f) * (data.moveSpeed * 1.5f);
    if (speedSq < jitterThresholdSq && speedSq >= stallSpeedSq) {
        float jitter = (s_angleDistribution(s_rng) - static_cast<float>(M_PI)) * 0.1f;
        Vector2D dir = wander.currentDirection;
        float c = std::cos(jitter), s = std::sin(jitter);
        Vector2D rotated(dir.getX() * c - dir.getY() * s, dir.getX() * s + dir.getY() * c);
        if (rotated.lengthSquared() > 0.000001f) {
            rotated.normalize();
            wander.currentDirection = rotated;
            ctx.transform.velocity = wander.currentDirection * data.moveSpeed;
        }
    }

    int nearbyCount = data.cachedNearbyCount;
    if (nearbyCount > 5) {
        float slowdownMultiplier = (nearbyCount > 10) ? 0.5f : 0.7f;
        ctx.transform.velocity = ctx.transform.velocity * slowdownMultiplier;
    }

    wander.previousVelocity = ctx.transform.velocity;
}

} // anonymous namespace

namespace Behaviors {

void initWander(size_t edmIndex, const HammerEngine::WanderBehaviorConfig& config) {
    // Config used in executeWander(), not needed for state initialization
    (void)config;

    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Wander);
    auto& data = edm.getBehaviorData(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    auto& wander = data.state.wander;

    wander.directionChangeTimer = 0.0f;
    wander.lastDirectionFlip = 0.0f;
    wander.startDelay = s_delayDistribution(s_rng) / 1000.0f;
    wander.movementStarted = false;
    wander.stallTimer = 0.0f;
    wander.lastStallPosition = Vector2D{0, 0};
    wander.stallPositionVariance = 0.0f;
    wander.unstickTimer = 0.0f;

    float angle = s_angleDistribution(s_rng);
    wander.currentDirection = Vector2D(std::cos(angle), std::sin(angle));
    wander.previousVelocity = Vector2D{0, 0};

    data.setInitialized(true);
}

void executeWander(BehaviorContext& ctx, const HammerEngine::WanderBehaviorConfig& config) {
    if (!ctx.behaviorData) return;

    auto& data = *ctx.behaviorData;
    if (!data.isValid()) return;

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
                if (ctx.memoryData && ctx.memoryData->isValid()) {
                    ctx.memoryData->emotions.fear = std::max(0.0f, ctx.memoryData->emotions.fear - 0.5f);
                }
                break;
            case BehaviorMessage::RAISE_ALERT:
                if (ctx.memoryData && ctx.memoryData->personality.bravery < 0.4f) {
                    data.pendingMessageCount = 0;
                    switchBehavior(ctx.edmIndex, BehaviorType::Flee);
                    return;
                }
                break;
            default: break;
        }
    }
    data.pendingMessageCount = 0;

    // Combat reaction: brave+aggressive NPCs fight back, others flee
    // (Ally alerting now handled centrally by damage event handler via RAISE_ALERT)
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

    updateTimers(data, ctx.deltaTime, ctx.pathData);

    if (!handleStartDelay(ctx)) return;

    if (data.state.wander.movementStarted) {
        handleMovement(ctx, config);

        // Cautious movement when suspicious
        if (isOnAlert(ctx)) {
            ctx.transform.velocity = ctx.transform.velocity * 0.7f;
        }
    }
}

} // namespace Behaviors
