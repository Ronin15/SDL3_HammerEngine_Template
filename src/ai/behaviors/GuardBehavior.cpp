/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "ai/BehaviorExecutors.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace {

// ============================================================================
// THREAD-LOCAL STATE
// ============================================================================

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleDistribution{0.0f, 2.0f * static_cast<float>(M_PI)};
thread_local std::uniform_real_distribution<float> s_radiusDistribution{0.3f, 1.0f};
thread_local std::vector<EntityHandle> s_nearbyBuffer;

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr float SUSPICIOUS_THRESHOLD = 2.0f;
constexpr float INVESTIGATING_THRESHOLD = 4.0f;
constexpr float DEFAULT_INVESTIGATION_TIME = 10.0f;
constexpr float DEFAULT_RETURN_TO_POST_TIME = 5.0f;
constexpr float DEFAULT_ALERT_DECAY_TIME = 15.0f;
constexpr float DEFAULT_ROAM_INTERVAL = 6.0f;
constexpr float DEFAULT_THREAT_DETECTION_RANGE = 150.0f;
constexpr float DEFAULT_FIELD_OF_VIEW = 360.0f;
constexpr float DEFAULT_ATTACK_ENGAGE_RANGE = 80.0f;
constexpr float DEFAULT_HELP_CALL_RADIUS = 300.0f;
constexpr float PATH_TTL = 5.0f;
constexpr float NAV_RADIUS = 18.0f;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

float normalizeAngle(float angle) {
    while (angle > static_cast<float>(M_PI)) angle -= 2.0f * static_cast<float>(M_PI);
    while (angle < -static_cast<float>(M_PI)) angle += 2.0f * static_cast<float>(M_PI);
    return angle;
}

Vector2D normalizeDir(const Vector2D& v) {
    float len = v.length();
    if (len < 0.0001f) return Vector2D(0, 0);
    return v * (1.0f / len);
}

float calculateAngle(const Vector2D& from, const Vector2D& to) {
    Vector2D diff = to - from;
    return std::atan2(diff.getY(), diff.getX());
}

bool isAtPosition(const Vector2D& currentPos, const Vector2D& targetPos, float threshold = 20.0f) {
    return (currentPos - targetPos).lengthSquared() <= threshold * threshold;
}

Vector2D generateRoamTarget(const Vector2D& center, float radius) {
    float angle = s_angleDistribution(s_rng);
    float dist = s_radiusDistribution(s_rng) * radius;
    return center + Vector2D(dist * std::cos(angle), dist * std::sin(angle));
}

// Move entity using pathfinding or direct movement
void moveToPosition(BehaviorContext& ctx, const Vector2D& targetPos, float speed) {
    if (speed <= 0.0f || !ctx.pathData) return;

    auto& pathData = *ctx.pathData;
    Vector2D currentPos = ctx.transform.position;
    auto& edm = EntityDataManager::Instance();

    // Check if we need a new path
    const bool skipRefresh = (pathData.pathRequestCooldown > 0.0f && pathData.isFollowingPath() &&
                              pathData.progressTimer < 0.8f);
    bool needsPath = false;
    if (!skipRefresh) {
        needsPath = !pathData.hasPath || pathData.navIndex >= pathData.pathLength ||
                    pathData.pathUpdateTimer > PATH_TTL;
    }

    // Check if goal changed significantly
    if (!skipRefresh && !needsPath && pathData.hasPath && pathData.pathLength > 0) {
        constexpr float GOAL_CHANGE_THRESH_SQ = 100.0f * 100.0f;
        Vector2D lastGoal = edm.getPathGoal(ctx.edmIndex);
        if ((targetPos - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQ) {
            needsPath = true;
        }
    }

    // Request new path if needed
    if (needsPath && pathData.pathRequestCooldown <= 0.0f) {
        PathfinderManager::Instance().requestPathToEDM(ctx.edmIndex, currentPos, targetPos,
                                                        PathfinderManager::Priority::Normal);
        pathData.pathRequestCooldown = 0.3f + (ctx.entityId % 200) * 0.001f;
    }

    // Follow path if available
    if (pathData.isFollowingPath()) {
        Vector2D waypoint = ctx.pathData->currentWaypoint;
        Vector2D toWaypoint = waypoint - currentPos;
        float dist = toWaypoint.length();

        if (dist < NAV_RADIUS) {
            edm.advanceWaypointWithCache(ctx.edmIndex);
            if (pathData.isFollowingPath()) {
                waypoint = ctx.pathData->currentWaypoint;
                toWaypoint = waypoint - currentPos;
                dist = toWaypoint.length();
            }
        }

        if (dist > 0.001f) {
            Vector2D direction = toWaypoint / dist;
            ctx.transform.velocity = direction * speed;
            pathData.progressTimer = 0.0f;
        }
    } else {
        // Direct movement fallback
        Vector2D toTarget = targetPos - currentPos;
        float dist = toTarget.length();
        if (dist > NAV_RADIUS && dist > 0.001f) {
            Vector2D direction = toTarget / dist;
            ctx.transform.velocity = direction * speed;
        } else {
            ctx.transform.velocity = Vector2D(0, 0);
        }
    }
}

// Detect threats in range
EntityHandle detectThreat(BehaviorContext& ctx, float detectionRange, float fieldOfView) {
    if (!ctx.behaviorData) return EntityHandle{};

    auto& edm = EntityDataManager::Instance();
    const auto& guard = ctx.behaviorData->state.guard;

    // Query nearby entities
    s_nearbyBuffer.clear();
    if (s_nearbyBuffer.capacity() < 32) s_nearbyBuffer.reserve(32);
    AIManager::Instance().queryHandlesInRadius(ctx.transform.position, detectionRange, s_nearbyBuffer, true);

    // Check for enemy faction NPCs
    for (const auto& handle : s_nearbyBuffer) {
        if (!handle.isValid()) continue;
        size_t idx = edm.getIndex(handle);
        if (idx == SIZE_MAX) continue;

        const auto& charData = edm.getCharacterDataByIndex(idx);
        if (charData.faction == 1) { // Enemy
            Vector2D threatPos = edm.getHotDataByIndex(idx).transform.position;

            // Check FOV if limited
            if (fieldOfView < 360.0f) {
                float angleToThreat = calculateAngle(ctx.transform.position, threatPos);
                float angleDiff = std::abs(normalizeAngle(angleToThreat - guard.currentHeading));
                float halfFOV = (fieldOfView * static_cast<float>(M_PI) / 180.0f) * 0.5f;
                if (angleDiff > halfFOV) continue;
            }
            return handle;
        }
    }

    // Check friendly NPCs' attackers
    for (const auto& handle : s_nearbyBuffer) {
        if (!handle.isValid()) continue;
        size_t idx = edm.getIndex(handle);
        if (idx == SIZE_MAX) continue;

        const auto& charData = edm.getCharacterDataByIndex(idx);
        if (charData.faction == 0) { // Friendly
            const auto& memData = edm.getMemoryData(idx);
            if (memData.lastAttacker.isValid()) {
                return memData.lastAttacker;
            }
        }
    }

    // Check player for enemy guards
    if (ctx.playerValid && ctx.edmIndex != SIZE_MAX) {
        const auto& guardCharData = edm.getCharacterDataByIndex(ctx.edmIndex);
        if (guardCharData.faction == 1) {
            Vector2D threatPos = ctx.playerPosition;
            float distance = (ctx.transform.position - threatPos).length();
            if (distance <= detectionRange) {
                if (fieldOfView < 360.0f) {
                    float angleToThreat = calculateAngle(ctx.transform.position, threatPos);
                    float angleDiff = std::abs(normalizeAngle(angleToThreat - guard.currentHeading));
                    float halfFOV = (fieldOfView * static_cast<float>(M_PI) / 180.0f) * 0.5f;
                    if (angleDiff > halfFOV) return EntityHandle{};
                }
                return ctx.playerHandle;
            }
        }
    }

    return EntityHandle{};
}

// Update alert level based on threat presence
void updateAlertLevel(BehaviorData& data, bool threatPresent, EntityHandle threat, float escalationMultiplier) {
    auto& guard = data.state.guard;

    if (threatPresent) {
        guard.threatSightingTimer = 0.0f;
        guard.hasActiveThreat = true;

        // Check if threat is enemy faction - immediate hostile response
        bool isEnemyFaction = false;
        if (threat.isValid()) {
            auto& edm = EntityDataManager::Instance();
            size_t threatIdx = edm.getIndex(threat);
            if (threatIdx != SIZE_MAX) {
                const auto& charData = edm.getCharacterDataByIndex(threatIdx);
                isEnemyFaction = (charData.faction == 1);
            }
        }

        if (isEnemyFaction) {
            guard.currentAlertLevel = 3; // HOSTILE
            guard.alertTimer = 0.0f;
        } else {
            float threatDuration = guard.alertTimer;
            if (guard.currentAlertLevel == 0) {
                guard.currentAlertLevel = 1; // SUSPICIOUS
                guard.alertTimer = 0.0f;
            } else if (guard.currentAlertLevel == 1 && threatDuration > SUSPICIOUS_THRESHOLD * escalationMultiplier) {
                guard.currentAlertLevel = 2; // INVESTIGATING
            } else if (guard.currentAlertLevel == 2 && threatDuration > INVESTIGATING_THRESHOLD * escalationMultiplier) {
                guard.currentAlertLevel = 3; // HOSTILE
            }
        }
    } else {
        guard.hasActiveThreat = false;
    }
}

} // anonymous namespace

namespace Behaviors {

// ============================================================================
// PUBLIC API
// ============================================================================

void initGuard(size_t edmIndex, const HammerEngine::GuardBehaviorConfig& config) {
    auto& edm = EntityDataManager::Instance();
    edm.initBehaviorData(edmIndex, BehaviorType::Guard);
    auto& data = edm.getBehaviorData(edmIndex);
    auto& guard = data.state.guard;
    auto& hotData = edm.getHotDataByIndex(edmIndex);

    // Initialize guard state
    guard.assignedPosition = hotData.transform.position;
    guard.currentMode = 0; // STATIC_GUARD by default
    guard.currentAlertLevel = 0; // CALM
    guard.onDuty = true;
    guard.hasActiveThreat = false;
    guard.isInvestigating = false;
    guard.returningToPost = false;
    guard.alertRaised = false;
    guard.helpCalled = false;
    guard.threatSightingTimer = 0.0f;
    guard.alertTimer = 0.0f;
    guard.investigationTimer = 0.0f;
    guard.positionCheckTimer = 0.0f;
    guard.patrolMoveTimer = 0.0f;
    guard.alertDecayTimer = 0.0f;
    guard.currentHeading = 0.0f;
    guard.roamTimer = DEFAULT_ROAM_INTERVAL;
    guard.currentPatrolIndex = 0;
    guard.escalationMultiplier = 1.0f;
    guard.roamTarget = hotData.transform.position;
    guard.currentPatrolTarget = hotData.transform.position;
    guard.lastKnownThreatPosition = Vector2D(0, 0);
    guard.investigationTarget = Vector2D(0, 0);

    data.setInitialized(true);
    (void)config;
}

void executeGuard(BehaviorContext& ctx, const HammerEngine::GuardBehaviorConfig& config) {
    if (!ctx.behaviorData || !ctx.behaviorData->isValid()) return;

    auto& data = *ctx.behaviorData;
    auto& guard = data.state.guard;

    if (!guard.onDuty) return;

    // Update timers
    guard.threatSightingTimer += ctx.deltaTime;
    guard.alertTimer += ctx.deltaTime;
    guard.investigationTimer += ctx.deltaTime;
    guard.positionCheckTimer += ctx.deltaTime;
    guard.patrolMoveTimer += ctx.deltaTime;
    guard.alertDecayTimer += ctx.deltaTime;
    guard.roamTimer -= ctx.deltaTime;

    // Update escalation multiplier from personality
    if (ctx.memoryData && ctx.memoryData->isValid()) {
        float suspicion = ctx.memoryData->emotions.suspicion;
        float loyalty = ctx.memoryData->personality.loyalty;
        guard.escalationMultiplier = 1.0f / (1.0f + suspicion * 0.5f + loyalty * 0.25f);
    } else {
        guard.escalationMultiplier = 1.0f;
    }

    // Update path timers
    if (!ctx.pathData) return;
    auto& pathData = *ctx.pathData;
    pathData.pathUpdateTimer += ctx.deltaTime;
    pathData.progressTimer += ctx.deltaTime;
    if (pathData.pathRequestCooldown > 0.0f) {
        pathData.pathRequestCooldown -= ctx.deltaTime;
    }

    // Detect threats
    EntityHandle threat = detectThreat(ctx, DEFAULT_THREAT_DETECTION_RANGE, DEFAULT_FIELD_OF_VIEW);
    bool threatPresent = threat.isValid();

    // Update alert level
    updateAlertLevel(data, threatPresent, threat, guard.escalationMultiplier);

    if (threatPresent) {
        // Handle threat detection
        auto& edm = EntityDataManager::Instance();
        size_t threatIdx = edm.getIndex(threat);
        if (threatIdx != SIZE_MAX) {
            Vector2D threatPos = edm.getHotDataByIndex(threatIdx).transform.position;
            guard.lastKnownThreatPosition = threatPos;

            switch (guard.currentAlertLevel) {
                case 1: // SUSPICIOUS
                    guard.currentHeading = calculateAngle(ctx.transform.position, threatPos);
                    break;

                case 2: // INVESTIGATING
                    guard.isInvestigating = true;
                    guard.investigationTarget = threatPos;
                    guard.investigationTimer = 0.0f;
                    moveToPosition(ctx, threatPos, config.guardSpeed);
                    break;

                case 3: // HOSTILE
                case 4: { // ALARM
                    float distance = (ctx.transform.position - threatPos).length();
                    if (distance <= DEFAULT_ATTACK_ENGAGE_RANGE) {
                        // Switch to Attack behavior
                        switchBehavior(ctx.edmIndex, BehaviorType::Attack);
                        return;
                    } else {
                        float speed = (guard.currentAlertLevel >= 4) ? config.guardSpeed * 1.5f : config.guardSpeed * 1.2f;
                        moveToPosition(ctx, threatPos, speed);
                    }
                    break;
                }
                default:
                    break;
            }
        }
    } else if (guard.isInvestigating) {
        // Continue investigation
        if (guard.investigationTimer > DEFAULT_INVESTIGATION_TIME && guard.currentAlertLevel < 3) {
            guard.isInvestigating = false;
            guard.returningToPost = true;
        } else {
            // At HOSTILE level, scan for threats while investigating
            if (guard.currentAlertLevel >= 3) {
                auto& edm = EntityDataManager::Instance();
                s_nearbyBuffer.clear();
                AIManager::Instance().queryHandlesInRadius(ctx.transform.position, DEFAULT_THREAT_DETECTION_RANGE * 2.0f,
                                                           s_nearbyBuffer, true);

                for (const auto& handle : s_nearbyBuffer) {
                    if (!handle.isValid()) continue;
                    size_t idx = edm.getIndex(handle);
                    if (idx == SIZE_MAX) continue;

                    const auto& charData = edm.getCharacterDataByIndex(idx);
                    if (charData.faction == 1) {
                        float distance = (ctx.transform.position - edm.getHotDataByIndex(idx).transform.position).length();
                        if (distance <= DEFAULT_ATTACK_ENGAGE_RANGE * 2.0f) {
                            switchBehavior(ctx.edmIndex, BehaviorType::Attack);
                            return;
                        }
                    }
                }
            }

            if (!isAtPosition(ctx.transform.position, guard.investigationTarget)) {
                float speed = (guard.currentAlertLevel >= 3) ? config.guardSpeed * 1.5f : config.guardSpeed;
                moveToPosition(ctx, guard.investigationTarget, speed);
            }
        }
    } else if (guard.returningToPost) {
        // Return to assigned position
        if (!isAtPosition(ctx.transform.position, guard.assignedPosition)) {
            moveToPosition(ctx, guard.assignedPosition, config.guardSpeed);
        } else {
            guard.returningToPost = false;
            guard.currentAlertLevel = 0; // CALM
        }
    } else {
        // Normal guard behavior based on mode
        switch (guard.currentMode) {
            case 0: // STATIC_GUARD
                if (!isAtPosition(ctx.transform.position, guard.assignedPosition, 10.0f)) {
                    moveToPosition(ctx, guard.assignedPosition, config.guardSpeed);
                }
                if (guard.positionCheckTimer > 2.0f) {
                    guard.currentHeading += 0.5f;
                    guard.currentHeading = normalizeAngle(guard.currentHeading);
                    guard.positionCheckTimer = 0.0f;
                }
                break;

            case 1: // PATROL_GUARD
            case 2: // AREA_GUARD
            case 3: // ROAMING_GUARD
                if (guard.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, guard.roamTarget)) {
                    guard.roamTarget = generateRoamTarget(guard.assignedPosition, config.guardRadius);
                    guard.roamTimer = DEFAULT_ROAM_INTERVAL;
                }
                moveToPosition(ctx, guard.roamTarget, config.guardSpeed);
                break;

            case 4: // ALERT_GUARD
                if (guard.currentAlertLevel >= 2) {
                    if (guard.lastKnownThreatPosition.length() > 0) {
                        moveToPosition(ctx, guard.lastKnownThreatPosition, config.guardSpeed * 1.5f);
                    }
                } else if (guard.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, guard.roamTarget)) {
                    guard.roamTarget = generateRoamTarget(guard.assignedPosition, config.guardRadius);
                    guard.roamTimer = DEFAULT_ROAM_INTERVAL;
                    moveToPosition(ctx, guard.roamTarget, config.guardSpeed);
                }
                break;

            default:
                break;
        }
    }

    // Handle alert decay
    if (guard.currentAlertLevel > 0 && guard.alertDecayTimer > DEFAULT_ALERT_DECAY_TIME) {
        guard.currentAlertLevel--;
        guard.alertDecayTimer = 0.0f;
    }
}

} // namespace Behaviors
