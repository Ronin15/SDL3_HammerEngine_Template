/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/BehaviorExecutors.hpp"
#include "managers/AIManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <cmath>
#include <random>

namespace {

// ============================================================================
// THREAD-LOCAL STATE
// ============================================================================

thread_local std::mt19937 s_rng{std::random_device{}()};
thread_local std::uniform_real_distribution<float> s_angleDistribution{0.0f, 2.0f * static_cast<float>(M_PI)};
thread_local std::uniform_real_distribution<float> s_radiusDistribution{0.3f, 1.0f};

// ============================================================================
// CONSTANTS
// ============================================================================

constexpr float SUSPICIOUS_THRESHOLD = 2.0f;
constexpr float INVESTIGATING_THRESHOLD = 4.0f;
constexpr float DEFAULT_ROAM_INTERVAL = 6.0f;
constexpr float DEFAULT_THREAT_DETECTION_RANGE = 150.0f;
constexpr float DEFAULT_ATTACK_ENGAGE_RANGE = 80.0f;
constexpr float PATH_TTL = 5.0f;
constexpr float NAV_RADIUS = 18.0f;
constexpr float THREAT_DETECTION_INTERVAL = 0.25f;  // Only check for threats 4x per second
constexpr float CALM_POLL_INTERVAL = 2.0f;  // Calm guards poll less frequently (every 2 seconds)

// Speed multipliers for urgent guard movement
constexpr float GUARD_ALERT_SPEED_MULT = 1.2f;   // Investigating suspicious activity
constexpr float GUARD_PURSUIT_SPEED_MULT = 1.5f; // Pursuing hostile threat (alert level >= 4)

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * @brief Get mode-specific speed based on guard mode
 * @param baseSpeed Entity's base movement speed (from CharacterData)
 */
float getModeSpeed(uint8_t currentMode, float baseSpeed, const HammerEngine::GuardBehaviorConfig& config) {
    switch (currentMode) {
        case 0: return baseSpeed * config.staticSpeedMultiplier;   // STATIC_GUARD
        case 1: return baseSpeed * config.patrolSpeedMultiplier;   // PATROL_GUARD
        case 2: return baseSpeed * config.areaSpeedMultiplier;     // AREA_GUARD
        case 3: return baseSpeed * config.roamingSpeedMultiplier;  // ROAMING_GUARD
        case 4: return baseSpeed * config.alertSpeedMultiplier;    // ALERT_GUARD
        default: return baseSpeed;
    }
}

/**
 * @brief Get mode-specific detection range multiplier
 */
float getModeAlertRadius(uint8_t currentMode, const HammerEngine::GuardBehaviorConfig& config) {
    switch (currentMode) {
        case 0: return config.staticAlertRadiusMultiplier;
        case 1: return config.patrolAlertRadiusMultiplier;
        case 2: return config.areaAlertRadiusMultiplier;
        case 3: return config.roamingAlertRadiusMultiplier;
        case 4: return config.alertModeAlertRadiusMultiplier;
        default: return 1.0f;
    }
}

float getAttackEngageRange(const CharacterData* charData) {
    if (charData && charData->attackRange > 0.0f) {
        return charData->attackRange;
    }

    return DEFAULT_ATTACK_ENGAGE_RANGE;
}

// Process pending messages for Guard behavior
void processGuardMessages(BehaviorData& data, const HammerEngine::GuardBehaviorConfig& config) {
    auto& guard = data.state.guard;

    for (uint8_t i = 0; i < data.pendingMessageCount; ++i) {
        uint8_t msgId = data.pendingMessages[i].messageId;

        switch (msgId) {
            case BehaviorMessage::RAISE_ALERT:
                // Force HOSTILE alert level (3)
                guard.currentAlertLevel = 3;
                guard.alertTimer = 0.0f;
                guard.alertDecayTimer = 0.0f;
                break;

            case BehaviorMessage::CALM_DOWN:
                // Decay alert level by one step (guards remain vigilant)
                if (guard.currentAlertLevel > 0)
                {
                    guard.currentAlertLevel--;
                    guard.alertDecayTimer = 0.0f;
                }
                break;

            case BehaviorMessage::PANIC:
                // Guards resist panic but escalate to HOSTILE alert
                guard.currentAlertLevel = 3;
                guard.alertTimer = 0.0f;
                guard.alertDecayTimer = 0.0f;
                break;

            case BehaviorMessage::DISTRESS:
                // Victim/fleeing entity calling for help — escalate to SUSPICIOUS
                // Triggers faster threat polling (0.25s vs 2s calm rate)
                if (guard.currentAlertLevel < 1)
                {
                    guard.currentAlertLevel = 1;
                    guard.alertTimer = 0.0f;
                    guard.alertDecayTimer = 0.0f;
                }
                break;

            default:
                break;
        }
    }
    data.pendingMessageCount = 0;
    (void)config;
}

float normalizeAngle(float angle) {
    while (angle > static_cast<float>(M_PI)) angle -= 2.0f * static_cast<float>(M_PI);
    while (angle < -static_cast<float>(M_PI)) angle += 2.0f * static_cast<float>(M_PI);
    return angle;
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

/**
 * @brief Get the next patrol waypoint, handling ping-pong vs loop mode
 */
Vector2D getNextPatrolWaypoint(BehaviorData& data) {
    auto& guard = data.state.guard;

    if (guard.patrolWaypointCount == 0) {
        return guard.assignedPosition;
    }

    return guard.patrolWaypoints[guard.currentPatrolWaypointIndex];
}

/**
 * @brief Advance to the next patrol waypoint
 */
void advancePatrolWaypoint(BehaviorData& data) {
    auto& guard = data.state.guard;

    if (guard.patrolWaypointCount == 0) return;

    if (guard.reversePatrol) {
        // Ping-pong mode: go forward then backward
        if (guard.patrolForward) {
            if (guard.currentPatrolWaypointIndex + 1 >= guard.patrolWaypointCount) {
                guard.patrolForward = false;
                if (guard.currentPatrolWaypointIndex > 0) {
                    guard.currentPatrolWaypointIndex--;
                }
            } else {
                guard.currentPatrolWaypointIndex++;
            }
        } else {
            if (guard.currentPatrolWaypointIndex == 0) {
                guard.patrolForward = true;
                if (guard.patrolWaypointCount > 1) {
                    guard.currentPatrolWaypointIndex++;
                }
            } else {
                guard.currentPatrolWaypointIndex--;
            }
        }
    } else {
        // Loop mode: wrap around
        guard.currentPatrolWaypointIndex = (guard.currentPatrolWaypointIndex + 1) % guard.patrolWaypointCount;
    }
}

// Move entity using pathfinding or direct movement - EDM passed to avoid Instance() calls
void moveToPosition(BehaviorContext& ctx, EntityDataManager& edm, const Vector2D& targetPos, float speed) {
    if (speed <= 0.0f || !ctx.pathData) return;

    auto& pathData = *ctx.pathData;
    Vector2D currentPos = ctx.transform.position;

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

// Detect threats using memory data (O(1) instead of O(N))
// Returns threat handle and sets isEnemyFaction to avoid redundant lookup in updateAlertLevel
// Detect threats using memory data and also check for witness-based alert escalation
// witnessAlertLevel is set to the highest alert level warranted by witnessed memories
// witnessLocation is set to the location of the most relevant witness memory
EntityHandle detectThreat(BehaviorContext& ctx, EntityDataManager& edm, bool& isEnemyFaction,
                          uint8_t& witnessAlertLevel, Vector2D& witnessLocation) {
    isEnemyFaction = false;
    witnessAlertLevel = 0;

    uint8_t myFaction = ctx.characterData.faction;

    // 1. Memory: lastAttacker — someone who attacked this guard
    if (ctx.memoryData.lastAttacker.isValid()) {
        size_t idx = edm.getIndex(ctx.memoryData.lastAttacker);
        if (idx != SIZE_MAX && edm.getHotDataByIndex(idx).isAlive()) {
            isEnemyFaction = (edm.getCharacterDataByIndex(idx).faction != myFaction);
            return ctx.memoryData.lastAttacker;
        }
    }

    // 2. Memory: lastTarget — previous known threat
    if (ctx.memoryData.lastTarget.isValid()) {
        size_t idx = edm.getIndex(ctx.memoryData.lastTarget);
        if (idx != SIZE_MAX && edm.getHotDataByIndex(idx).isAlive()) {
            isEnemyFaction = (edm.getCharacterDataByIndex(idx).faction != myFaction);
            return ctx.memoryData.lastTarget;
        }
    }

    // 3. Recent witnessed combat/death memories — single pass for both threat detection
    //    and witness-based alert escalation (avoids duplicate memory iteration)
    if (ctx.memoryData.isValid()) {
        EntityHandle recentThreat{};
        float recentTimestamp = -1.0f;

        for (size_t i = 0; i < NPCMemoryData::INLINE_MEMORY_COUNT; ++i) {
            const auto& mem = ctx.memoryData.memories[i];
            if (!mem.isValid()) continue;
            if (mem.type != MemoryType::WitnessedCombat &&
                mem.type != MemoryType::WitnessedDeath) continue;

            const float memAge = ctx.gameTime - mem.timestamp;
            if (memAge > 10.0f) continue;

            // Track witness alert level (used when no direct threat found)
            if (mem.type == MemoryType::WitnessedDeath && witnessAlertLevel < 2) {
                witnessAlertLevel = 2;  // INVESTIGATING
                witnessLocation = mem.location;
            } else if (witnessAlertLevel < 1) {
                witnessAlertLevel = 1;  // SUSPICIOUS
                witnessLocation = mem.location;
            }

            // Track direct threat from witnessed memories
            if (!mem.subject.isValid()) continue;
            size_t idx = edm.getIndex(mem.subject);
            if (idx == SIZE_MAX || !edm.getHotDataByIndex(idx).isAlive()) continue;

            if (mem.timestamp >= recentTimestamp) {
                recentTimestamp = mem.timestamp;
                recentThreat = mem.subject;
            }
        }

        if (recentThreat.isValid()) {
            size_t idx = edm.getIndex(recentThreat);
            isEnemyFaction = (edm.getCharacterDataByIndex(idx).faction != myFaction);
            return recentThreat;
        }
    }

    // 4. Player proximity check for enemy-faction guards
    if (ctx.playerValid && ctx.characterData.faction == 1) {
        float detectionRange = ctx.behaviorData.state.guard.cachedDetectionRange;
        float distSq = Vector2D::distanceSquared(ctx.transform.position, ctx.playerPosition);
        if (distSq <= detectionRange * detectionRange) {
            isEnemyFaction = true;
            return ctx.playerHandle;
        }
    }

    return EntityHandle{};
}

// Update alert level based on threat presence
// isEnemyFaction is pre-computed by detectThreat to avoid redundant EDM lookup
void updateAlertLevel(BehaviorData& data, bool threatPresent, bool isEnemyFaction, float escalationMultiplier) {
    auto& guard = data.state.guard;

    if (threatPresent) {
        guard.threatSightingTimer = 0.0f;
        guard.hasActiveThreat = true;

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

    // Cache moveSpeed from CharacterData (one-time cost)
    data.moveSpeed = edm.getCharacterDataByIndex(edmIndex).moveSpeed;

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
    guard.hostileTimer = 0.0f;

    data.setInitialized(true);
    (void)config;
}

void executeGuard(BehaviorContext& ctx, const HammerEngine::GuardBehaviorConfig& config) {
    if (!ctx.behaviorData.isValid()) return;

    auto& data = ctx.behaviorData;
    auto& guard = data.state.guard;

    // Get EDM reference once - avoid multiple Instance() calls
    auto& edm = EntityDataManager::Instance();

    // Cache engage range squared once for all distance comparisons this frame
    float engageRange = getAttackEngageRange(&ctx.characterData);
    float engageRangeSq = engageRange * engageRange;

    // Process any pending messages before main logic
    processGuardMessages(data, config);

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
    if (ctx.memoryData.isValid()) {
        float suspicion = ctx.memoryData.emotions.suspicion;
        float loyalty = ctx.memoryData.personality.loyalty;
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

    // Cache detection range - only recompute when mode changes
    if (guard.currentMode != guard.lastCachedMode) {
        guard.cachedDetectionRange = DEFAULT_THREAT_DETECTION_RANGE * getModeAlertRadius(guard.currentMode, config);
        guard.lastCachedMode = guard.currentMode;
    }

    // Detect threats - reduced-rate polling for calm guards, frequent polling when alerted
    // Calm guards (level 0) poll every 2 seconds for ambient awareness
    // Alerted guards (level > 0) poll every 0.25s to track active threats
    EntityHandle threat{};
    bool threatPresent = false;
    bool isEnemyFaction = false;
    bool shouldPoll = (guard.currentAlertLevel == 0)
        ? (guard.threatSightingTimer >= CALM_POLL_INTERVAL)
        : (guard.threatSightingTimer >= THREAT_DETECTION_INTERVAL);

    if (shouldPoll) {
        uint8_t witnessAlertLevel = 0;
        Vector2D witnessLocation{};
        threat = detectThreat(ctx, edm, isEnemyFaction, witnessAlertLevel, witnessLocation);
        threatPresent = threat.isValid();
        guard.threatSightingTimer = 0.0f;

        // Persist threat handle in memory for Attack behavior handoff
        if (threatPresent) {
            ctx.memoryData.lastTarget = threat;
        }

        // Apply witness-based alert escalation (merged from separate memory scan)
        if (!threatPresent && guard.currentAlertLevel == 0 && witnessAlertLevel > 0) {
            guard.currentAlertLevel = witnessAlertLevel;
            guard.lastKnownThreatPosition = witnessLocation;
            guard.alertTimer = 0.0f;
            guard.alertDecayTimer = 0.0f;
        }
    }

    // Update alert level (isEnemyFaction pre-computed by detectThreat)
    updateAlertLevel(data, threatPresent, isEnemyFaction, guard.escalationMultiplier);

    // Overwhelmed guards with very low bravery can flee (+0.1 guard training bonus)
    if (guard.currentAlertLevel >= 3 && ctx.memoryData.isValid())
    {
        float fear = ctx.memoryData.emotions.fear;
        float effectiveBravery = ctx.memoryData.personality.bravery + 0.1f;
        if (fear > 0.7f && effectiveBravery < 0.3f)
        {
            switchBehavior(ctx.edmIndex, BehaviorType::Flee);
            return;
        }
    }

    // Call for help when first reaching HOSTILE (level 3 only) — ALARM handles its own wider call
    if (guard.currentAlertLevel == 3 && !guard.helpCalled && config.canCallForHelp) {
        guard.helpCalled = true;
        thread_local std::vector<size_t> s_helpBuffer;
        uint8_t myFaction = ctx.characterData.faction;
        AIManager::Instance().scanFactionInRadius(
            myFaction, ctx.transform.position, config.helpCallRadius, s_helpBuffer, true);
        for (size_t idx : s_helpBuffer) {
            if (idx == ctx.edmIndex) continue;
            Behaviors::deferBehaviorMessage(idx, BehaviorMessage::RAISE_ALERT);
        }
    }

    if (threatPresent) {
        // Handle threat detection
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
                    moveToPosition(ctx, edm, threatPos, data.moveSpeed);
                    break;

                case 3: { // HOSTILE
                    // Track time spent at HOSTILE for ALARM escalation
                    guard.hostileTimer += ctx.deltaTime;
                    if (guard.hostileTimer >= config.alarmEscalationTime) {
                        guard.currentAlertLevel = 4; // Escalate to ALARM
                        guard.hostileTimer = 0.0f;
                    }

                    float distSq3 = (ctx.transform.position - threatPos).lengthSquared();
                    if (distSq3 <= engageRangeSq) {
                        switchBehavior(ctx.edmIndex, BehaviorType::Attack);
                        return;
                    } else {
                        moveToPosition(ctx, edm, threatPos, data.moveSpeed * GUARD_ALERT_SPEED_MULT);
                    }
                    break;
                }
                case 4: { // ALARM - like HOSTILE but wider help call and no return-to-post
                    float distSq4 = (ctx.transform.position - threatPos).lengthSquared();
                    if (distSq4 <= engageRangeSq) {
                        switchBehavior(ctx.edmIndex, BehaviorType::Attack);
                        return;
                    } else {
                        moveToPosition(ctx, edm, threatPos, data.moveSpeed * GUARD_PURSUIT_SPEED_MULT);
                    }

                    // Widen help call radius for ALARM state
                    if (!guard.helpCalled && config.canCallForHelp) {
                        guard.helpCalled = true;
                        thread_local std::vector<size_t> s_alarmBuffer;
                        uint8_t myFaction = ctx.characterData.faction;
                        AIManager::Instance().scanFactionInRadius(
                            myFaction, ctx.transform.position, config.alarmHelpCallRadius, s_alarmBuffer, true);
                        for (size_t idx : s_alarmBuffer) {
                            if (idx == ctx.edmIndex) continue;
                            Behaviors::deferBehaviorMessage(idx, BehaviorMessage::RAISE_ALERT);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }
    } else if (guard.isInvestigating) {
        // Continue investigation
        // ALARM state: do not return to post until level decays below ALARM
        if (guard.investigationTimer > config.investigationTime && guard.currentAlertLevel < 3) {
            guard.isInvestigating = false;
            guard.returningToPost = true;
        } else {
            // At HOSTILE level, check last known threat position for attack opportunity
            // (Main threat detection above handles new threat discovery - no need for second scan)
                if (guard.currentAlertLevel >= 3) {
                    float distToThreatSq = (ctx.transform.position - guard.lastKnownThreatPosition).lengthSquared();
                    if (distToThreatSq <= engageRangeSq) {
                        switchBehavior(ctx.edmIndex, BehaviorType::Attack);
                        return;
                    }
                }

            if (!isAtPosition(ctx.transform.position, guard.investigationTarget)) {
                float speed = (guard.currentAlertLevel >= 3) ? data.moveSpeed * GUARD_PURSUIT_SPEED_MULT : data.moveSpeed;
                moveToPosition(ctx, edm, guard.investigationTarget, speed);
            }
        }
    } else if (guard.returningToPost) {
        // Return to assigned position
        if (!isAtPosition(ctx.transform.position, guard.assignedPosition)) {
            moveToPosition(ctx, edm, guard.assignedPosition, data.moveSpeed);
        } else {
            guard.returningToPost = false;
            guard.currentAlertLevel = 0; // CALM
        }
    } else {
        // Normal guard behavior based on mode - use mode-specific speeds
        float modeSpeed = getModeSpeed(guard.currentMode, data.moveSpeed, config);

        switch (guard.currentMode) {
            case 0: // STATIC_GUARD
                if (!isAtPosition(ctx.transform.position, guard.assignedPosition, 10.0f)) {
                    moveToPosition(ctx, edm, guard.assignedPosition, modeSpeed);
                }
                if (guard.positionCheckTimer > 2.0f) {
                    guard.currentHeading += 0.5f;
                    guard.currentHeading = normalizeAngle(guard.currentHeading);
                    guard.positionCheckTimer = 0.0f;
                }
                break;

            case 1: // PATROL_GUARD
                // If waypoints are set, follow them; otherwise fall back to roaming
                if (guard.patrolWaypointCount > 0) {
                    Vector2D waypointTarget = getNextPatrolWaypoint(data);
                    if (isAtPosition(ctx.transform.position, waypointTarget)) {
                        advancePatrolWaypoint(data);
                        waypointTarget = getNextPatrolWaypoint(data);
                    }
                    moveToPosition(ctx, edm, waypointTarget, modeSpeed);
                } else {
                    // Fallback to roaming if no waypoints set
                    if (guard.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, guard.roamTarget)) {
                        guard.roamTarget = generateRoamTarget(guard.assignedPosition, config.guardRadius);
                        guard.roamTimer = DEFAULT_ROAM_INTERVAL;
                    }
                    moveToPosition(ctx, edm, guard.roamTarget, modeSpeed);
                }
                break;

            case 2: // AREA_GUARD
            case 3: // ROAMING_GUARD
                if (guard.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, guard.roamTarget)) {
                    guard.roamTarget = generateRoamTarget(guard.assignedPosition, config.guardRadius);
                    guard.roamTimer = DEFAULT_ROAM_INTERVAL;
                }
                moveToPosition(ctx, edm, guard.roamTarget, modeSpeed);
                break;

            case 4: // ALERT_GUARD
                if (guard.currentAlertLevel >= 2) {
                    if (guard.lastKnownThreatPosition.lengthSquared() > 0.0f) {
                        moveToPosition(ctx, edm, guard.lastKnownThreatPosition, modeSpeed);
                    }
                } else if (guard.roamTimer <= 0.0f || isAtPosition(ctx.transform.position, guard.roamTarget)) {
                    guard.roamTarget = generateRoamTarget(guard.assignedPosition, config.guardRadius);
                    guard.roamTimer = DEFAULT_ROAM_INTERVAL;
                    moveToPosition(ctx, edm, guard.roamTarget, modeSpeed);
                }
                break;

            default:
                break;
        }
    }

    // Handle alert decay
    if (guard.currentAlertLevel > 0 && guard.alertDecayTimer > config.alertDecayTime) {
        uint8_t prevLevel = guard.currentAlertLevel;
        guard.currentAlertLevel--;
        guard.alertDecayTimer = 0.0f;

        // ALARM (4) decays to HOSTILE (3) — reset hostileTimer so escalation restarts
        if (prevLevel == 4) {
            guard.hostileTimer = 0.0f;
            guard.helpCalled = false; // Allow re-calling for help at new radius
        }

        // When returning to CALM, signal nearby allies to calm down
        if (guard.currentAlertLevel == 0) {
            guard.helpCalled = false;
            thread_local std::vector<size_t> s_calmBuffer;
            uint8_t myFaction = ctx.characterData.faction;
            AIManager::Instance().scanFactionInRadius(
                myFaction, ctx.transform.position, config.helpCallRadius, s_calmBuffer, true);
            for (size_t idx : s_calmBuffer) {
                if (idx == ctx.edmIndex) continue;
                Behaviors::deferBehaviorMessage(idx, BehaviorMessage::CALM_DOWN);
            }
        }
    }
}

} // namespace Behaviors
