/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleDist{0.0f, 2.0f * static_cast<float>(M_PI)};
thread_local std::uniform_real_distribution<float> s_cooldownVariation{0.0f, 1.0f};

// Patrol uses guard state since they share similar patrol mechanics
// guard.currentPatrolIndex = current waypoint index
// guard.currentPatrolTarget = current target position
// guard.patrolMoveTimer = movement timer

Vector2D generateRandomWaypoint(const Vector2D& currentPos, float boundaryPadding) {
    float minX, minY, maxX, maxY;
    if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        float angle = s_angleDist(s_rng);
        return currentPos + Vector2D(std::cos(angle), std::sin(angle)) * 200.0f;
    }

    constexpr float TILE = HammerEngine::TILE_SIZE;
    float worldMinX = minX * TILE + boundaryPadding;
    float worldMinY = minY * TILE + boundaryPadding;
    float worldMaxX = maxX * TILE - boundaryPadding;
    float worldMaxY = maxY * TILE - boundaryPadding;

    std::uniform_real_distribution<float> xDist(worldMinX, worldMaxX);
    std::uniform_real_distribution<float> yDist(worldMinY, worldMaxY);

    return Vector2D(xDist(s_rng), yDist(s_rng));
}

bool isAtWaypoint(const Vector2D& position, const Vector2D& waypoint, float radius) {
    return (position - waypoint).lengthSquared() < radius * radius;
}

} // anonymous namespace

namespace Behaviors {

void initPatrol(size_t edmIndex, const HammerEngine::PatrolBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Patrol);
    auto& data = edm.getBehaviorData(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    auto& guard = data.state.guard;  // Patrol uses guard state
    auto& hotData = edm.getHotDataByIndex(edmIndex);

    guard.currentPatrolIndex = 0;
    guard.currentPatrolTarget = hotData.transform.position;
    guard.patrolMoveTimer = 0.0f;
    guard.assignedPosition = hotData.transform.position;

    // Generate initial waypoints in the waypoint slot
    auto* waypointSlot = edm.getWaypointSlot(edmIndex);
    if (waypointSlot) {
        for (size_t i = 0; i < 4; ++i) {
            waypointSlot[i] = generateRandomWaypoint(hotData.transform.position, config.boundaryPadding);
        }
    }

    data.setInitialized(true);
}

void executePatrol(BehaviorContext& ctx, const HammerEngine::PatrolBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid()) return;

    auto& data = *ctx.behaviorData;
    auto& guard = data.state.guard;

    // Combat reaction: brave+aggressive NPCs fight back, others flee
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

    Vector2D currentPos = ctx.transform.position;
    auto& edm = EntityDataManager::Instance();

    // Get waypoints from slot
    auto* waypointSlot = edm.getWaypointSlot(ctx.edmIndex);
    if (!waypointSlot) {
        ctx.transform.velocity = Vector2D(0, 0);
        return;
    }

    // Check if at current waypoint
    Vector2D currentWaypoint = waypointSlot[guard.currentPatrolIndex % 4];
    if (isAtWaypoint(currentPos, currentWaypoint, config.waypointReachedRadius)) {
        guard.patrolMoveTimer += ctx.deltaTime;
        if (guard.patrolMoveTimer >= config.waypointCooldown) {
            guard.currentPatrolIndex = (guard.currentPatrolIndex + 1) % 4;
            guard.patrolMoveTimer = 0.0f;
            guard.currentPatrolTarget = waypointSlot[guard.currentPatrolIndex];
        }
        ctx.transform.velocity = Vector2D(0, 0);
        return;
    }

    // Move toward waypoint using pathfinding
    if (ctx.pathData) {
        auto& pathData = *ctx.pathData;
        pathData.pathUpdateTimer += ctx.deltaTime;
        if (pathData.pathRequestCooldown > 0.0f) {
            pathData.pathRequestCooldown -= ctx.deltaTime;
        }

        bool needsPath = !pathData.hasPath || pathData.navIndex >= pathData.pathLength ||
                         pathData.pathUpdateTimer > config.pathRequestCooldown;

        if (needsPath && pathData.pathRequestCooldown <= 0.0f) {
            PathfinderManager::Instance().requestPathToEDM(ctx.edmIndex, currentPos, currentWaypoint,
                                                           PathfinderManager::Priority::Normal);
            pathData.pathRequestCooldown = config.pathRequestCooldown +
                                           s_cooldownVariation(s_rng) * config.pathRequestCooldownVariation;
        }

        if (pathData.isFollowingPath()) {
            Vector2D waypoint = ctx.pathData->currentWaypoint;
            Vector2D toWaypoint = waypoint - currentPos;
            float dist = toWaypoint.length();

            if (dist < config.waypointReachedRadius) {
                edm.advanceWaypointWithCache(ctx.edmIndex);
                if (pathData.isFollowingPath()) {
                    waypoint = ctx.pathData->currentWaypoint;
                    toWaypoint = waypoint - currentPos;
                    dist = toWaypoint.length();
                }
            }

            if (dist > 0.001f) {
                Vector2D direction = toWaypoint / dist;
                ctx.transform.velocity = direction * data.moveSpeed;
            }
        } else {
            // Direct movement fallback
            Vector2D direction = (currentWaypoint - currentPos).normalized();
            ctx.transform.velocity = direction * data.moveSpeed;
        }
    } else {
        // No pathData - direct movement
        Vector2D direction = (currentWaypoint - currentPos).normalized();
        ctx.transform.velocity = direction * data.moveSpeed;
    }

    // Cautious movement when suspicious
    if (isOnAlert(ctx)) {
        ctx.transform.velocity = ctx.transform.velocity * 0.7f;
    }

    // Stall detection
    float speedSq = ctx.transform.velocity.lengthSquared();
    float stallThreshold = data.moveSpeed * config.stallSpeedMultiplier;
    if (speedSq < stallThreshold * stallThreshold) {
        data.separationTimer += ctx.deltaTime;
        if (data.separationTimer > config.advanceWaypointDelay) {
            // Skip to next waypoint
            guard.currentPatrolIndex = (guard.currentPatrolIndex + 1) % 4;
            guard.currentPatrolTarget = waypointSlot[guard.currentPatrolIndex];
            data.separationTimer = 0.0f;
            if (ctx.pathData) ctx.pathData->clear();
        }
    } else {
        data.separationTimer = 0.0f;
    }
}

} // namespace Behaviors
