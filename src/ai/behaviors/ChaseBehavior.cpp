/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "ai/BehaviorExecutors.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <chrono>
#include <numeric>
#include <random>

namespace {

thread_local std::mt19937 s_rng{static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count())};

constexpr float DEFAULT_NAV_RADIUS = 64.0f;
constexpr float DEFAULT_MAX_RANGE = 1000.0f;
constexpr float DEFAULT_MIN_RANGE = 50.0f;
constexpr float CHASE_SPEED_MULT = 1.1f;  // Urgent movement multiplier

void updateCooldowns(BehaviorData& data, float deltaTime) {
    auto& chase = data.state.chase;
    if (chase.pathRequestCooldown > 0.0f) chase.pathRequestCooldown -= deltaTime;
    if (chase.stallRecoveryCooldown > 0.0f) chase.stallRecoveryCooldown -= deltaTime;
    if (chase.behaviorChangeCooldown > 0.0f) chase.behaviorChangeCooldown -= deltaTime;
}

bool canRequestPath(const BehaviorData& data) {
    const auto& chase = data.state.chase;
    return chase.pathRequestCooldown <= 0.0f && chase.stallRecoveryCooldown <= 0.0f;
}

void applyPathCooldown(BehaviorData& data, float cooldownSeconds) {
    data.state.chase.pathRequestCooldown = cooldownSeconds;
}

} // anonymous namespace

namespace Behaviors {

void initChase(size_t edmIndex, const HammerEngine::ChaseBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Chase);
    auto& data = edm.getBehaviorData(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    auto& chase = data.state.chase;

    chase.isChasing = false;
    chase.hasLineOfSight = false;
    chase.timeWithoutSight = 0.0f;
    chase.pathRequestCooldown = 0.0f;
    chase.stallRecoveryCooldown = 0.0f;
    chase.behaviorChangeCooldown = 0.0f;
    chase.crowdCheckTimer = 0.0f;
    chase.cachedChaserCount = 0;
    chase.recalcCounter = 0;
    chase.stallPositionVariance = 0.0f;
    chase.unstickTimer = 0.0f;

    const auto& hotData = edm.getHotDataByIndex(edmIndex);
    chase.lastKnownTargetPos = hotData.transform.position;
    chase.currentDirection = Vector2D{0, 0};
    chase.lastStallPosition = Vector2D{0, 0};
    chase.hasExplicitTarget = false;
    chase.explicitTarget = EntityHandle{};

    data.setInitialized(true);
    (void)config;
}

void executeChase(BehaviorContext& ctx, const HammerEngine::ChaseBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid()) return;

    auto& data = *ctx.behaviorData;
    auto& chase = data.state.chase;

    // Emotional modulation: fearful NPCs break off chase
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        float fear = ctx.memoryData->emotions.fear;
        float bravery = ctx.memoryData->personality.bravery;
        if (fear > 0.7f && bravery < 0.3f) {
            switchBehavior(ctx.edmIndex, BehaviorType::Flee);
            return;
        }
    }

    // Aggression speed modifier (up to +20%)
    float emotionalSpeedMod = 1.0f;
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        emotionalSpeedMod = 1.0f + ctx.memoryData->emotions.aggression * 0.2f;
    }
    float chaseSpeed = CHASE_SPEED_MULT * emotionalSpeedMod;

    // Crowd analysis cache
    data.lastCrowdAnalysis += ctx.deltaTime;
    float crowdCacheInterval = 3.0f + (static_cast<float>(ctx.entityId % 200) * 0.01f);
    if (data.lastCrowdAnalysis >= crowdCacheInterval) {
        constexpr float kCrowdQueryRadius = 80.0f;
        auto& nearbyPositions = AIInternal::GetNearbyPositionBuffer();
        nearbyPositions.clear();
        data.cachedNearbyCount = AIInternal::GetNearbyEntitiesWithPositions(
            ctx.entityId, ctx.transform.position, kCrowdQueryRadius, nearbyPositions);

        if (!nearbyPositions.empty()) {
            Vector2D sum = std::accumulate(nearbyPositions.begin(), nearbyPositions.end(), Vector2D{0, 0});
            data.cachedClusterCenter = sum * (1.0f / static_cast<float>(nearbyPositions.size()));
        }
        data.lastCrowdAnalysis = 0.0f;
    }

    // Target priority: explicit > memory lastTarget > memory lastAttacker
    Vector2D entityPos = ctx.transform.position;
    Vector2D targetPos;
    bool targetValid = false;
    auto& edm = EntityDataManager::Instance();

    if (chase.hasExplicitTarget && chase.explicitTarget.isValid()) {
        size_t targetIdx = edm.getIndex(chase.explicitTarget);
        if (targetIdx != SIZE_MAX) {
            const auto& targetHot = edm.getHotDataByIndex(targetIdx);
            if (targetHot.isAlive()) {
                targetPos = targetHot.transform.position;
                targetValid = true;
            }
        }
        if (!targetValid) {
            chase.hasExplicitTarget = false;
            chase.explicitTarget = EntityHandle{};
        }
    }

    if (!targetValid && ctx.memoryData && ctx.memoryData->lastTarget.isValid()) {
        size_t targetIdx = edm.getIndex(ctx.memoryData->lastTarget);
        if (targetIdx != SIZE_MAX) {
            const auto& targetHot = edm.getHotDataByIndex(targetIdx);
            if (targetHot.isAlive()) {
                targetPos = targetHot.transform.position;
                targetValid = true;
            }
        }
    }

    if (!targetValid && ctx.memoryData && ctx.memoryData->lastAttacker.isValid()) {
        size_t attackerIdx = edm.getIndex(ctx.memoryData->lastAttacker);
        if (attackerIdx != SIZE_MAX) {
            const auto& attackerHot = edm.getHotDataByIndex(attackerIdx);
            if (attackerHot.isAlive()) {
                targetPos = attackerHot.transform.position;
                targetValid = true;
            }
        }
    }

    if (!targetValid) {
        if (chase.isChasing) {
            ctx.transform.velocity = Vector2D(0, 0);
            chase.isChasing = false;
            chase.hasLineOfSight = false;
        }
        return;
    }

    float distanceSquared = (targetPos - entityPos).lengthSquared();
    float maxRangeSquared = DEFAULT_MAX_RANGE * DEFAULT_MAX_RANGE;
    float minRangeSquared = DEFAULT_MIN_RANGE * DEFAULT_MIN_RANGE;

    updateCooldowns(data, ctx.deltaTime);

    if (distanceSquared <= maxRangeSquared) {
        chase.hasLineOfSight = true;
        chase.lastKnownTargetPos = targetPos;

        if (distanceSquared > minRangeSquared && ctx.pathData) {
            auto& pathData = *ctx.pathData;
            pathData.pathUpdateTimer += ctx.deltaTime;

            const bool skipRefresh = (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
                                      pathData.progressTimer < 0.8f);
            bool needsNewPath = false;

            if (!skipRefresh) {
                if (!pathData.hasPath || pathData.navIndex >= pathData.pathLength) {
                    needsNewPath = true;
                } else if (pathData.pathUpdateTimer > config.pathRefreshInterval) {
                    needsNewPath = true;
                } else {
                    auto& edm = EntityDataManager::Instance();
                    Vector2D pathGoal = edm.getPathGoal(ctx.edmIndex);
                    float targetMovementSquared = (targetPos - pathGoal).lengthSquared();
                    needsNewPath = (targetMovementSquared >
                                   config.pathInvalidationDistance * config.pathInvalidationDistance);
                }
            }

            bool stuckOnObstacle = (pathData.progressTimer > 3.0f);
            if (stuckOnObstacle) pathData.clear();

            if ((needsNewPath || stuckOnObstacle) && canRequestPath(data)) {
                float minRangeCheckSquared = (DEFAULT_MIN_RANGE * 1.5f) * (DEFAULT_MIN_RANGE * 1.5f);
                if (distanceSquared < minRangeCheckSquared) {
                    ctx.transform.velocity = Vector2D(0, 0);
                    return;
                }

                PathfinderManager::Instance().requestPathToEDM(ctx.edmIndex, entityPos, targetPos,
                                                               PathfinderManager::Priority::High);
                applyPathCooldown(data, config.pathRequestCooldown);
            }

            if (pathData.isFollowingPath()) {
                Vector2D waypoint = ctx.pathData->currentWaypoint;
                Vector2D toWaypoint = waypoint - entityPos;
                float dist = toWaypoint.length();

                if (dist < DEFAULT_NAV_RADIUS) {
                    auto& edm = EntityDataManager::Instance();
                    edm.advanceWaypointWithCache(ctx.edmIndex);
                    if (pathData.isFollowingPath()) {
                        waypoint = ctx.pathData->currentWaypoint;
                        toWaypoint = waypoint - entityPos;
                        dist = toWaypoint.length();
                    }
                }

                bool following = (pathData.isFollowingPath() && dist > 0.001f);

                if (following) {
                    Vector2D direction = toWaypoint / dist;
                    ctx.transform.velocity = direction * data.moveSpeed * chaseSpeed;
                    pathData.progressTimer = 0.0f;

                    chase.crowdCheckTimer += ctx.deltaTime;
                    if (chase.crowdCheckTimer >= config.crowdCheckInterval) {
                        chase.crowdCheckTimer = 0.0f;
                    }

                    if (chase.cachedChaserCount > 3) {
                        Vector2D toTarget = (targetPos - entityPos).normalized();
                        Vector2D lateral(-toTarget.getY(), toTarget.getX());
                        float lateralBias = ((float)(ctx.entityId % 3) - 1.0f) * 15.0f;
                        Vector2D adjustedTarget = targetPos + lateral * lateralBias;
                        Vector2D newDir = (adjustedTarget - entityPos).normalized();
                        ctx.transform.velocity = newDir * data.moveSpeed * chaseSpeed;
                    }
                } else {
                    Vector2D direction = (targetPos - entityPos).normalized();
                    ctx.transform.velocity = direction * data.moveSpeed * chaseSpeed;
                    pathData.progressTimer = 0.0f;
                }
            } else {
                Vector2D direction = (targetPos - entityPos).normalized();
                int nearbyCount = data.cachedNearbyCount;

                if (nearbyCount > 1) {
                    Vector2D lateral(-direction.getY(), direction.getX());
                    float offset = ((float)(ctx.entityId % 3) - 1.0f) * 20.0f;
                    direction = direction + lateral * (offset / 400.0f);
                    direction.normalize();
                }

                ctx.transform.velocity = direction * data.moveSpeed * chaseSpeed;
                pathData.progressTimer = 0.0f;
            }

            chase.isChasing = true;

            // Stall detection
            float currentSpeedSq = ctx.transform.velocity.lengthSquared();
            float stallThreshold = std::max(1.0f, data.moveSpeed * chaseSpeed * config.stallSpeedMultiplier);
            float stallThresholdSq = stallThreshold * stallThreshold;
            float stallTimeLimit = config.stallTimeout;

            if (currentSpeedSq < stallThresholdSq) {
                pathData.stallTimer += ctx.deltaTime;
                if (pathData.stallTimer >= stallTimeLimit) {
                    pathData.clear();
                    pathData.stallTimer = 0.0f;

                    std::uniform_real_distribution<float> jitterDist(-0.1f, 0.1f);
                    float jitter = jitterDist(s_rng);
                    Vector2D dir = (targetPos - entityPos).normalized();
                    float c = std::cos(jitter), s = std::sin(jitter);
                    Vector2D rotated(dir.getX() * c - dir.getY() * s, dir.getX() * s + dir.getY() * c);
                    ctx.transform.velocity = rotated * data.moveSpeed * chaseSpeed;
                }
            } else {
                pathData.stallTimer = 0.0f;
            }
        } else {
            if (chase.isChasing) {
                ctx.transform.velocity = Vector2D(0, 0);
                chase.isChasing = false;
            }
        }
    } else {
        if (chase.isChasing) {
            chase.isChasing = false;
            chase.hasLineOfSight = false;
            ctx.transform.velocity = Vector2D(0, 0);
            if (ctx.pathData) ctx.pathData->clear();
        }
    }
}

} // namespace Behaviors
