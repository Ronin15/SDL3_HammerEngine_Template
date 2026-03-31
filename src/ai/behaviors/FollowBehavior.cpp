/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"

namespace Behaviors {

void initFollow(size_t edmIndex, const HammerEngine::FollowBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Follow);
    auto& data = edm.getBehaviorData(edmIndex);

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

    auto& follow = data.state.follow;
    auto& hotData = edm.getHotDataByIndex(edmIndex);

    follow.lastTargetPosition = hotData.transform.position;
    follow.currentVelocity = Vector2D(0, 0);
    follow.desiredPosition = hotData.transform.position;
    follow.formationOffset = Vector2D(0, 0);
    follow.lastSepForce = Vector2D(0, 0);
    follow.currentSpeed = 0.0f;
    follow.currentHeading = 0.0f;
    follow.backoffTimer = 0.0f;
    follow.formationSlot = -1;
    follow.isFollowing = false;
    follow.targetMoving = false;
    follow.inFormation = false;
    follow.isStopped = true;

    data.setInitialized(true);
    (void)config;
}

void executeFollow(BehaviorContext& ctx, const HammerEngine::FollowBehaviorConfig& config) {
    if (!ctx.behaviorData.isValid()) return;

    auto& data = ctx.behaviorData;
    auto& follow = data.state.follow;

    // Process pending behavior messages
    for (uint8_t i = 0; i < data.pendingMessageCount; ++i)
    {
        switch (data.pendingMessages[i].messageId)
        {
            case BehaviorMessage::PANIC:
                data.pendingMessageCount = 0;
                switchBehavior(ctx.edmIndex, BehaviorType::Flee);
                return;
            case BehaviorMessage::CALM_DOWN:
                if (ctx.memoryData.isValid())
                {
                    ctx.memoryData.emotions.fear = std::max(0.0f, ctx.memoryData.emotions.fear - 0.5f);
                }
                break;
            case BehaviorMessage::RAISE_ALERT:
                if (ctx.memoryData.personality.bravery < 0.4f)
                {
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

    // Throttle follow movement logic — responsive but not every frame
    follow.backoffTimer += ctx.deltaTime;  // Reused as throttle timer (unused by Follow)
    if (follow.backoffTimer < config.updateInterval) return;
    follow.backoffTimer = 0.0f;

    // Get target (leader) position from memory
    Vector2D targetPos;
    bool targetValid = false;

    if (ctx.memoryData.lastTarget.isValid()) {
        auto& edm = EntityDataManager::Instance();
        size_t targetIdx = edm.getIndex(ctx.memoryData.lastTarget);
        if (targetIdx != SIZE_MAX) {
            const auto& targetHot = edm.getHotDataByIndex(targetIdx);
            if (targetHot.isAlive()) {
                targetPos = targetHot.transform.position;
                targetValid = true;
            }
        }
    }

    if (!targetValid) {
        follow.isFollowing = false;
        follow.isStopped = true;
        ctx.transform.velocity = Vector2D(0, 0);
        return;
    }

    Vector2D currentPos = ctx.transform.position;
    float distanceToTarget = (targetPos - currentPos).length();

    // Check if target is moving
    Vector2D targetMovement = targetPos - follow.lastTargetPosition;
    follow.targetMoving = targetMovement.lengthSquared() > 1.0f;
    follow.lastTargetPosition = targetPos;

    // Check if arrived
    if (distanceToTarget < config.arrivalRadius) {
        follow.isFollowing = false;
        follow.isStopped = true;
        ctx.transform.velocity = Vector2D(0, 0);
        return;
    }

    // Check if within follow distance
    if (distanceToTarget < config.followDistance) {
        if (!follow.targetMoving) {
            follow.isStopped = true;
            ctx.transform.velocity = Vector2D(0, 0);
            return;
        }
    }

    follow.isFollowing = true;
    follow.isStopped = false;

    // Calculate desired position (behind target)
    Vector2D toTarget = (targetPos - currentPos).normalized();
    Vector2D desiredPos = targetPos - toTarget * config.followDistance;
    follow.desiredPosition = desiredPos;

    // Use pathfinding for navigation
    if (ctx.pathData) {
        auto& pathData = *ctx.pathData;
        pathData.pathUpdateTimer += config.updateInterval;
        if (pathData.pathRequestCooldown > 0.0f) {
            pathData.pathRequestCooldown -= config.updateInterval;
        }

        bool needsPath = !pathData.hasPath || pathData.navIndex >= pathData.pathLength ||
                         pathData.pathUpdateTimer > config.pathTTL;

        // Check if target moved significantly
        if (!needsPath && pathData.hasPath) {
            auto& edm = EntityDataManager::Instance();
            Vector2D pathGoal = edm.getPathGoal(ctx.edmIndex);
            float goalChangeSq = (desiredPos - pathGoal).lengthSquared();
            needsPath = goalChangeSq > config.goalChangeThreshold * config.goalChangeThreshold;
        }

        if (needsPath && pathData.pathRequestCooldown <= 0.0f) {
            PathfinderManager::Instance().requestPathToEDM(ctx.edmIndex, currentPos, desiredPos,
                                                           PathfinderManager::Priority::Normal);
            pathData.pathRequestCooldown = config.pathCooldown;
        }

        if (pathData.isFollowingPath()) {
            Vector2D waypoint = ctx.pathData->currentWaypoint;
            Vector2D toWaypoint = waypoint - currentPos;
            float dist = toWaypoint.length();

            if (dist < config.nodeRadius) {
                auto& edm = EntityDataManager::Instance();
                edm.advanceWaypointWithCache(ctx.edmIndex);
                if (pathData.isFollowingPath()) {
                    waypoint = ctx.pathData->currentWaypoint;
                    toWaypoint = waypoint - currentPos;
                    dist = toWaypoint.length();
                }
            }

            if (dist > 0.001f) {
                Vector2D direction = toWaypoint / dist;
                float speed = data.moveSpeed;

                // Speed adjustment based on distance
                if (distanceToTarget > config.catchupRange) {
                    speed *= config.catchupSpeedMultiplier;  // Speed up to catch up
                } else if (distanceToTarget < config.followDistance * 0.5f) {
                    speed *= config.slowdownSpeedMultiplier;  // Slow down when too close
                }

                ctx.transform.velocity = direction * speed;
                follow.currentSpeed = speed;
            }
        } else {
            // Direct movement fallback
            Vector2D direction = (desiredPos - currentPos).normalized();
            ctx.transform.velocity = direction * data.moveSpeed;
            follow.currentSpeed = data.moveSpeed;
        }
    } else {
        // No pathData - direct movement
        Vector2D direction = (desiredPos - currentPos).normalized();
        ctx.transform.velocity = direction * data.moveSpeed;
        follow.currentSpeed = data.moveSpeed;
    }

    // Stall detection
    float speedSq = ctx.transform.velocity.lengthSquared();
    float stallThreshold = data.moveSpeed * config.stallSpeedMultiplier;
    if (speedSq < stallThreshold * stallThreshold) {
        data.separationTimer += config.updateInterval;
        if (data.separationTimer > config.stallTimeout) {
            // Try to unstick
            if (ctx.pathData) ctx.pathData->clear();
            data.separationTimer = 0.0f;
        }
    } else {
        data.separationTimer = 0.0f;
    }

    follow.currentVelocity = ctx.transform.velocity;
}

} // namespace Behaviors
