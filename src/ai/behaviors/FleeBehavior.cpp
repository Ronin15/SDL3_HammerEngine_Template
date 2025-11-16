/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"  // For PathPriority enum
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/CollisionManager.hpp"
#include "collisions/AABB.hpp"
#include <cmath>
#include <algorithm>

FleeBehavior::FleeBehavior(float fleeSpeed, float detectionRange, float safeDistance)
    : m_fleeSpeed(fleeSpeed)
    , m_detectionRange(detectionRange)
    , m_safeDistance(safeDistance)
{
}

FleeBehavior::FleeBehavior(FleeMode mode, float fleeSpeed, float detectionRange)
    : m_fleeMode(mode)
    , m_fleeSpeed(fleeSpeed)
    , m_detectionRange(detectionRange)
{
    // Adjust parameters based on mode
    switch (mode) {
        case FleeMode::PANIC_FLEE:
            m_fleeSpeed = fleeSpeed * 1.2f; // Faster in panic
            m_panicDuration = 2000.0f;      // 2 seconds of panic
            break;
        case FleeMode::STRATEGIC_RETREAT:
            m_fleeSpeed = fleeSpeed * 0.8f; // Slower, more calculated
            m_safeDistance = detectionRange * 1.8f;
            break;
        case FleeMode::EVASIVE_MANEUVER:
            m_zigzagInterval = 300;         // More frequent direction changes
            break;
        case FleeMode::SEEK_COVER:
            m_safeDistance = detectionRange * 1.5f;
            break;
    }
}

void FleeBehavior::init(EntityPtr entity) {
    if (!entity) return;

    auto& state = m_entityStates[entity];
    state = EntityState(); // Reset to default state
    state.currentStamina = m_maxStamina;
    state.lastThreatPosition = entity->getPosition();
}

void FleeBehavior::executeLogic(EntityPtr entity, float deltaTime) {
    if (!entity || !isActive()) return;

    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) {
        init(entity);
        it = m_entityStates.find(entity);
        if (it == m_entityStates.end()) return;
    }

    EntityState& state = it->second;

    // Update all timers
    state.fleeTimer += deltaTime;
    state.directionChangeTimer += deltaTime;
    if (state.panicTimer > 0.0f) state.panicTimer -= deltaTime;
    state.zigzagTimer += deltaTime;
    state.pathUpdateTimer += deltaTime;
    state.progressTimer += deltaTime;
    if (state.pathCooldown > 0.0f) state.pathCooldown -= deltaTime;
    if (state.backoffTimer > 0.0f) state.backoffTimer -= deltaTime;
    state.lastCrowdAnalysis += deltaTime;

    // PERFORMANCE OPTIMIZATION: Cache crowd analysis results every 3-5 seconds
    // Reduces CollisionManager queries significantly for fleeing entities
    float crowdCacheInterval = 3.0f + (entity->getID() % 200) * 0.01f; // 3-5 seconds
    if (state.lastCrowdAnalysis >= crowdCacheInterval) {
      Vector2D position = entity->getPosition();
      float queryRadius = 100.0f; // Smaller radius for flee (lighter separation)
      state.cachedNearbyCount = AIInternal::GetNearbyEntitiesWithPositions(
          entity, position, queryRadius, state.cachedNearbyPositions);
      state.lastCrowdAnalysis = 0.0f; // Reset timer
    }

    EntityPtr threat = getThreat();

    if (!threat) {
        // No threat detected, stop fleeing and recover stamina
        if (state.isFleeing) {
            state.isFleeing = false;
            state.isInPanic = false;
            state.hasValidThreat = false;
        }
        
        if (m_useStamina) {
            updateStamina(state, 0.016f, false); // Assume ~60 FPS
        }
        return;
    }

    // Check if threat is in detection range
    bool threatInRange = isThreatInRange(entity, threat);
    
    
    if (threatInRange) {
        // Start fleeing if not already
        if (!state.isFleeing) {
            state.isFleeing = true;
            state.fleeTimer = 0.0f;
            state.lastThreatPosition = threat->getPosition();
            
            // Determine if this should trigger panic
            if (m_fleeMode == FleeMode::PANIC_FLEE) {
                state.isInPanic = true;
                state.panicTimer = m_panicDuration * m_panicVariation(m_rng);
            }
        }
        
        state.hasValidThreat = true;
        state.lastThreatPosition = threat->getPosition();
    } else if (state.isFleeing) {
        // PERFORMANCE: Use squared distance for comparison
        float distanceToThreatSquared = (entity->getPosition() - threat->getPosition()).lengthSquared();
        float safeDistanceSquared = m_safeDistance * m_safeDistance;
        if (distanceToThreatSquared >= safeDistanceSquared) {
            state.isFleeing = false;
            state.isInPanic = false;
            state.hasValidThreat = false;
        }
    }

    // Update panic state
    if (state.isInPanic && state.panicTimer <= 0.0f) {
        state.isInPanic = false;
    }

    // Execute appropriate flee behavior
    if (state.isFleeing) {
        switch (m_fleeMode) {
            case FleeMode::PANIC_FLEE:
                updatePanicFlee(entity, state, deltaTime);
                break;
            case FleeMode::STRATEGIC_RETREAT:
                updateStrategicRetreat(entity, state, deltaTime);
                break;
            case FleeMode::EVASIVE_MANEUVER:
                updateEvasiveManeuver(entity, state, deltaTime);
                break;
            case FleeMode::SEEK_COVER:
                updateSeekCover(entity, state, deltaTime);
                break;
        }
        
        if (m_useStamina) {
            updateStamina(state, 0.016f, true);
        }
    }
}

void FleeBehavior::clean(EntityPtr entity) {
    if (entity) {
        m_entityStates.erase(entity);
    }
}

void FleeBehavior::onMessage(EntityPtr entity, const std::string& message) {
    if (!entity) return;

    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) return;

    EntityState& state = it->second;

    if (message == "panic") {
        state.isInPanic = true;
        state.panicTimer = m_panicDuration;
    } else if (message == "calm_down") {
        state.isInPanic = false;
    } else if (message == "stop_fleeing") {
        state.isFleeing = false;
        state.isInPanic = false;
        state.hasValidThreat = false;
    } else if (message == "recover_stamina") {
        state.currentStamina = m_maxStamina;
    }
}

std::string FleeBehavior::getName() const { return "Flee"; }

void FleeBehavior::setFleeSpeed(float speed) {
    m_fleeSpeed = std::max(0.1f, speed);
}

void FleeBehavior::setDetectionRange(float range) {
    m_detectionRange = std::max(0.0f, range);
}

void FleeBehavior::setSafeDistance(float distance) {
    m_safeDistance = std::max(0.0f, distance);
}

void FleeBehavior::setFleeMode(FleeMode mode) {
    m_fleeMode = mode;
}

void FleeBehavior::setPanicDuration(float duration) {
    m_panicDuration = std::max(0.0f, duration);
}

void FleeBehavior::setStaminaSystem(bool enabled, float maxStamina, float staminaDrain) {
    m_useStamina = enabled;
    m_maxStamina = std::max(1.0f, maxStamina);
    m_staminaDrain = std::max(0.0f, staminaDrain);
}

void FleeBehavior::addSafeZone(const Vector2D& center, float radius) {
    m_safeZones.emplace_back(center, radius);
}

void FleeBehavior::clearSafeZones() {
    m_safeZones.clear();
}

bool FleeBehavior::isFleeing() const {
    return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                      [](const auto& pair) { return pair.second.isFleeing; });
}

bool FleeBehavior::isInPanic() const {
    return std::any_of(m_entityStates.begin(), m_entityStates.end(),
                      [](const auto& pair) { return pair.second.isInPanic; });
}

float FleeBehavior::getDistanceToThreat() const {
    EntityPtr threat = getThreat();
    if (!threat) return -1.0f;
    auto it = std::find_if(m_entityStates.begin(), m_entityStates.end(),
                          [](const auto& pair) { return pair.second.isFleeing; });
    if (it != m_entityStates.end()) {
        // PERFORMANCE: Only compute sqrt when actually returning distance
        float distSquared = (it->first->getPosition() - threat->getPosition()).lengthSquared();
        return std::sqrt(distSquared);
    }
    return -1.0f;
}

FleeBehavior::FleeMode FleeBehavior::getFleeMode() const {
    return m_fleeMode;
}

std::shared_ptr<AIBehavior> FleeBehavior::clone() const {
    auto clone = std::make_shared<FleeBehavior>(m_fleeMode, m_fleeSpeed, m_detectionRange);
    clone->m_safeDistance = m_safeDistance;
    clone->m_panicDuration = m_panicDuration;
    clone->m_useStamina = m_useStamina;
    clone->m_maxStamina = m_maxStamina;
    clone->m_staminaDrain = m_staminaDrain;
    clone->m_safeZones = m_safeZones;
    return clone;
}

EntityPtr FleeBehavior::getThreat() const {
    return AIManager::Instance().getPlayerReference();
}

bool FleeBehavior::isThreatInRange(EntityPtr entity, EntityPtr threat) const {
    if (!entity || !threat) return false;
    
    // PERFORMANCE: Use squared distance
    float distanceSquared = (entity->getPosition() - threat->getPosition()).lengthSquared();
    float detectionRangeSquared = m_detectionRange * m_detectionRange;
    return distanceSquared <= detectionRangeSquared;
}

Vector2D FleeBehavior::calculateFleeDirection(EntityPtr entity, EntityPtr threat, const EntityState& state) const {
    if (!entity || !threat) return Vector2D(0, 0);
    
    Vector2D entityPos = entity->getPosition();
    Vector2D threatPos = threat->getPosition();
    
    // Basic flee direction (away from threat)
    Vector2D fleeDir = entityPos - threatPos;
    
    if (fleeDir.length() < 0.001f) {
        // If too close, use last known direction or random
        if (state.fleeDirection.length() > 0.001f) {
            fleeDir = state.fleeDirection;
        } else {
            // Random direction
            float angle = m_angleVariation(m_rng) * 2.0f * M_PI;
            fleeDir = Vector2D(std::cos(angle), std::sin(angle));
        }
    }
    
    // Avoid boundaries
    fleeDir = avoidBoundaries(entityPos, fleeDir);
    
    return normalizeVector(fleeDir);
}

Vector2D FleeBehavior::findNearestSafeZone(const Vector2D& position) const {
    if (m_safeZones.empty()) return Vector2D(0, 0);
    
    const SafeZone* nearest = nullptr;
    float minDistance = std::numeric_limits<float>::max();
    
    for (const auto& zone : m_safeZones) {
        // PERFORMANCE: Use squared distance for comparison
        float distanceSquared = (position - zone.center).lengthSquared();
        if (distanceSquared < minDistance * minDistance) {
            minDistance = std::sqrt(distanceSquared); // Only compute sqrt when updating minimum
            nearest = &zone;
        }
    }
    
    return nearest ? (nearest->center - position) : Vector2D(0, 0);
}

[[maybe_unused]] bool FleeBehavior::isPositionSafe(const Vector2D& position) const {
    EntityPtr threat = getThreat();
    if (!threat) return true;
    
    // PERFORMANCE: Use squared distance
    float distanceToThreatSquared = (position - threat->getPosition()).lengthSquared();
    float safeDistanceSquared = m_safeDistance * m_safeDistance;
    return distanceToThreatSquared >= safeDistanceSquared;
}

[[maybe_unused]] bool FleeBehavior::isNearBoundary(const Vector2D& position) const {
    // Use world bounds for boundary detection
    float minX, minY, maxX, maxY;
    if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        // No world loaded - can't determine boundaries
        return false;
    }

    constexpr float TILE = HammerEngine::TILE_SIZE;
    float worldMinX = minX * TILE + m_boundaryPadding;
    float worldMinY = minY * TILE + m_boundaryPadding;
    float worldMaxX = maxX * TILE - m_boundaryPadding;
    float worldMaxY = maxY * TILE - m_boundaryPadding;

    return (position.getX() < worldMinX ||
            position.getX() > worldMaxX ||
            position.getY() < worldMinY ||
            position.getY() > worldMaxY);
}

Vector2D FleeBehavior::avoidBoundaries(const Vector2D& position, const Vector2D& direction) const {
    // Use world bounds for boundary avoidance with world-scale padding
    float minX, minY, maxX, maxY;
    if (!WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        // No world loaded - return direction unchanged
        return direction;
    }

    Vector2D adjustedDir = direction;
    constexpr float TILE = HammerEngine::TILE_SIZE;
    const float worldPadding = 80.0f; // Increased padding for world-scale movement
    float worldMinX = minX * TILE + worldPadding;
    float worldMinY = minY * TILE + worldPadding;
    float worldMaxX = maxX * TILE - worldPadding;
    float worldMaxY = maxY * TILE - worldPadding;

    // Check world boundaries and adjust direction
    if (position.getX() < worldMinX && direction.getX() < 0) {
        adjustedDir.setX(std::abs(direction.getX())); // Force rightward
    } else if (position.getX() > worldMaxX && direction.getX() > 0) {
        adjustedDir.setX(-std::abs(direction.getX())); // Force leftward
    }

    if (position.getY() < worldMinY && direction.getY() < 0) {
        adjustedDir.setY(std::abs(direction.getY())); // Force downward
    } else if (position.getY() > worldMaxY && direction.getY() > 0) {
        adjustedDir.setY(-std::abs(direction.getY())); // Force upward
    }

    return adjustedDir;
}

void FleeBehavior::updatePanicFlee(EntityPtr entity, EntityState& state, float deltaTime) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    


    // In panic mode, change direction more frequently and use longer distances
    if (state.directionChangeTimer > 0.2f || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(entity, threat, state);

        // Add more randomness to panic movement for world-scale escape
        float randomAngle = m_angleVariation(m_rng) * 0.8f; // Increased randomness
        float cos_a = std::cos(randomAngle);
        float sin_a = std::sin(randomAngle);

        Vector2D rotated(
            state.fleeDirection.getX() * cos_a - state.fleeDirection.getY() * sin_a,
            state.fleeDirection.getX() * sin_a + state.fleeDirection.getY() * cos_a
        );

        state.fleeDirection = rotated;
        state.directionChangeTimer = 0.0f;
        
        // In panic, try to flee much further to break out of small areas
        Vector2D currentPos = entity->getPosition();
        Vector2D panicDest = currentPos + state.fleeDirection * 1000.0f; // Much longer panic flee distance
        
        // Clamp to world bounds with larger margin for panic mode
        panicDest = pathfinder().clampToWorldBounds(panicDest, 100.0f);
    }
    
    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D intended = state.fleeDirection * m_fleeSpeed * speedModifier;
    // PERFORMANCE OPTIMIZATION: Use cached collision data to avoid redundant queries
    applySeparationWithCache(entity, entity->getPosition(), intended,
                             m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4,
                             state.separationTimer, state.lastSepVelocity, deltaTime,
                             state.cachedNearbyPositions);
}

void FleeBehavior::updateStrategicRetreat(EntityPtr entity, EntityState& state, float deltaTime) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Vector2D currentPos = entity->getPosition();



    // Strategic retreat: aim for a point away from threat (or toward nearest safe zone)
    if (state.directionChangeTimer > 1.0f || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(entity, threat, state);

        // Blend with nearest safe zone direction if exists
        Vector2D safeZoneDirection = findNearestSafeZone(currentPos);
        if (safeZoneDirection.length() > 0.001f) {
            Vector2D blended = (state.fleeDirection * 0.6f + normalizeVector(safeZoneDirection) * 0.4f);
            state.fleeDirection = normalizeVector(blended);
        }
        state.directionChangeTimer = 0.0f;
    }

    // Dynamic retreat distance based on local entity density
    float baseRetreatDistance = 800.0f; // Increased base for world scale
    int nearbyCount = 0;
    
    // Check for other fleeing entities to avoid clustering in same escape routes
    nearbyCount = AIInternal::CountNearbyEntities(entity, currentPos, 100.0f);
    
    float retreatDistance = baseRetreatDistance;
    if (nearbyCount > 2) {
        // High density: encourage longer retreat to spread fleeing entities
        retreatDistance = baseRetreatDistance * 1.8f; // Up to 1440px retreat
        
        // Also add lateral bias to prevent all entities fleeing in same direction
        Vector2D lateral(-state.fleeDirection.getY(), state.fleeDirection.getX());
        float lateralBias = ((float)(entity->getID() % 5) - 2.0f) * 0.3f; // -0.6 to +0.6
        state.fleeDirection = (state.fleeDirection + lateral * lateralBias).normalized();
    } else if (nearbyCount > 0) {
        // Medium density: moderate expansion
        retreatDistance = baseRetreatDistance * 1.3f;
    }

    // Compute a retreat destination further ahead and clamp to world bounds
    Vector2D dest = pathfinder().clampToWorldBounds(currentPos + state.fleeDirection * retreatDistance, 100.0f);

    // OPTIMIZATION: Use extracted method instead of lambda for better compiler optimization
    float speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPathToGoal(entity, currentPos, state, dest, m_fleeSpeed * speedModifier)) {
        // Fallback to direct flee when no path available
        Vector2D intended2 = state.fleeDirection * m_fleeSpeed * speedModifier;
        // PERFORMANCE OPTIMIZATION: Use cached collision data
        applySeparationWithCache(entity, entity->getPosition(), intended2,
                                 m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4,
                                 state.separationTimer, state.lastSepVelocity, deltaTime,
                                 state.cachedNearbyPositions);
    }
}

void FleeBehavior::updateEvasiveManeuver(EntityPtr entity, EntityState& state, float deltaTime) {
    EntityPtr threat = getThreat();
    if (!threat) return;




    // Zigzag pattern
    if (state.zigzagTimer > m_zigzagInterval) {
        state.zigzagDirection *= -1; // Flip direction
        state.zigzagTimer = 0.0f;
    }
    
    // Base flee direction
    Vector2D baseFleeDir = calculateFleeDirection(entity, threat, state);
    
    // Apply zigzag
    float zigzagAngleRad = (m_zigzagAngle * M_PI / 180.0f) * state.zigzagDirection;
    float cos_z = std::cos(zigzagAngleRad);
    float sin_z = std::sin(zigzagAngleRad);
    
    Vector2D zigzagDir(
        baseFleeDir.getX() * cos_z - baseFleeDir.getY() * sin_z,
        baseFleeDir.getX() * sin_z + baseFleeDir.getY() * cos_z
    );
    
    state.fleeDirection = normalizeVector(zigzagDir);

    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D intended3 = state.fleeDirection * m_fleeSpeed * speedModifier;
    // PERFORMANCE OPTIMIZATION: Use cached collision data
    applySeparationWithCache(entity, entity->getPosition(), intended3,
                             m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4,
                             state.separationTimer, state.lastSepVelocity, deltaTime,
                             state.cachedNearbyPositions);
}

void FleeBehavior::updateSeekCover(EntityPtr entity, EntityState& state, float deltaTime) {
    // Move toward nearest safe zone using pathfinding when possible
    Vector2D currentPos = entity->getPosition();
    Vector2D safeZoneDirection = findNearestSafeZone(currentPos);

    // Dynamic cover seeking distance based on entity density
    float baseCoverDistance = 720.0f; // Increased base for world scale
    int nearbyCount = 0;
    
    // Check for clustering of other cover-seekers
    nearbyCount = AIInternal::CountNearbyEntities(entity, currentPos, 90.0f);
    
    float coverDistance = baseCoverDistance;
    if (nearbyCount > 2) {
        // High density: seek cover further away to spread entities
        coverDistance = baseCoverDistance * 1.6f; // Up to 1152px
    } else if (nearbyCount > 0) {
        // Medium density: moderate expansion
        coverDistance = baseCoverDistance * 1.2f;
    }

    Vector2D dest = currentPos;
    if (safeZoneDirection.length() > 0.001f) {
        state.fleeDirection = normalizeVector(safeZoneDirection);
        dest = currentPos + state.fleeDirection * coverDistance;
    } else {
        // No safe zones, move away from threat with larger distance
        EntityPtr threat = getThreat();
        if (threat) {
            state.fleeDirection = calculateFleeDirection(entity, threat, state);
            dest = currentPos + state.fleeDirection * coverDistance;
        }
    }

    // Clamp destination within world bounds
    dest = pathfinder().clampToWorldBounds(dest, 100.0f);

    // OPTIMIZATION: Use extracted method instead of lambda for better compiler optimization
    float speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPathToGoal(entity, currentPos, state, dest, m_fleeSpeed * speedModifier)) {
        // Fallback to straight-line movement
        Vector2D intended4 = state.fleeDirection * m_fleeSpeed * speedModifier;
        // PERFORMANCE OPTIMIZATION: Use cached collision data
        applySeparationWithCache(entity, entity->getPosition(), intended4,
                                 m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4,
                                 state.separationTimer, state.lastSepVelocity, deltaTime,
                                 state.cachedNearbyPositions);
    }
}

void FleeBehavior::updateStamina(EntityState& state, float deltaTime, bool fleeing) {
    if (fleeing) {
        state.currentStamina -= m_staminaDrain * deltaTime;
        state.currentStamina = std::max(0.0f, state.currentStamina);
    } else {
        state.currentStamina += m_staminaRecovery * deltaTime;
        state.currentStamina = std::min(m_maxStamina, state.currentStamina);
    }
}

Vector2D FleeBehavior::normalizeVector(const Vector2D& direction) const {
    float magnitude = direction.length();
    if (magnitude < 0.001f) {
        return Vector2D(1, 0); // Default direction
    }
    return direction / magnitude;
}

float FleeBehavior::calculateFleeSpeedModifier(const EntityState& state) const {
    float modifier = 1.0f;

    // Panic increases speed
    if (state.isInPanic) {
        modifier *= 1.3f;
    }

    // Stamina affects speed
    if (m_useStamina) {
        float staminaRatio = state.currentStamina / m_maxStamina;
        modifier *= (0.3f + 0.7f * staminaRatio); // Speed ranges from 30% to 100%
    }

    return modifier;
}

// OPTIMIZATION: Extracted from lambda for better compiler optimization
// This method handles path-following logic with TTL and no-progress checks
bool FleeBehavior::tryFollowPathToGoal(EntityPtr entity, const Vector2D& currentPos, EntityState& state, const Vector2D& goal, float speed) {
    // PERFORMANCE: Increase TTL to reduce pathfinding frequency
    constexpr float pathTTL = 2.5f;
    constexpr float noProgressWindow = 0.4f;
    constexpr float GOAL_CHANGE_THRESH_SQUARED = 180.0f * 180.0f; // Increased from 96px

    // Check if path needs refresh
    bool needRefresh = state.pathPoints.empty() || state.currentPathIndex >= state.pathPoints.size();

    // Check for progress towards current waypoint
    if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
        float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
        if (d + 1.0f < state.lastNodeDistance) {
            state.lastNodeDistance = d;
            state.progressTimer = 0.0f;
        } else if (state.progressTimer > noProgressWindow) {
            needRefresh = true;
        }
    }

    // Check if path is stale
    if (state.pathUpdateTimer > pathTTL) {
        needRefresh = true;
    }

    // Request new path if needed and cooldown allows
    if (needRefresh && state.pathCooldown <= 0.0f) {
        // Gate refresh on significant goal change to avoid thrash
        bool goalChanged = true;
        if (!state.pathPoints.empty()) {
            Vector2D lastGoal = state.pathPoints.back();
            goalChanged = ((goal - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
        }

        // Only request new path if goal changed significantly
        if (goalChanged) {
            // Use PathfinderManager for pathfinding requests
            auto& pf = this->pathfinder();
            pf.requestPath(
                entity->getID(),
                pf.clampToWorldBounds(currentPos, 100.0f),
                goal,
                PathfinderManager::Priority::High,
                [this, entity](EntityID, const std::vector<Vector2D>& path) {
                    auto it = m_entityStates.find(entity);
                    if (it != m_entityStates.end() && !path.empty()) {
                        it->second.pathPoints = path;
                        it->second.currentPathIndex = 0;
                        it->second.pathUpdateTimer = 0.0f;
                        it->second.lastNodeDistance = std::numeric_limits<float>::infinity();
                        it->second.progressTimer = 0.0f;
                        it->second.pathCooldown = 0.8f;
                    }
                });

            // Apply cooldown to prevent spam (async path not ready yet)
            state.pathCooldown = 0.6f; // Shorter cooldown for flee (more urgent)
        }
    }

    // Follow existing path if available
    if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
        Vector2D node = state.pathPoints[state.currentPathIndex];
        Vector2D dir = node - currentPos;
        float len = dir.length();

        if (len > 0.01f) {
            dir = dir * (1.0f / len);
            entity->setVelocity(dir * speed);
        }

        // Check if reached current waypoint
        if ((node - currentPos).length() <= state.navRadius) {
            ++state.currentPathIndex;
            state.lastNodeDistance = std::numeric_limits<float>::infinity();
            state.progressTimer = 0.0f;
        }

        return true;
    }

    return false;
}
