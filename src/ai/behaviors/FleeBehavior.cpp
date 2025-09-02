/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/PathfindingCompat.hpp"
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

void FleeBehavior::executeLogic(EntityPtr entity) {
    if (!entity || !isActive()) return;

    auto it = m_entityStates.find(entity);
    if (it == m_entityStates.end()) {
        init(entity);
        it = m_entityStates.find(entity);
        if (it == m_entityStates.end()) return;
    }

    EntityState& state = it->second;
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
    Uint64 currentTime = SDL_GetTicks();
    
    if (threatInRange) {
        // Start fleeing if not already
        if (!state.isFleeing) {
            state.isFleeing = true;
            state.fleeStartTime = currentTime;
            state.lastThreatPosition = threat->getPosition();
            
            // Determine if this should trigger panic
            if (m_fleeMode == FleeMode::PANIC_FLEE) {
                state.isInPanic = true;
                state.panicEndTime = currentTime + static_cast<Uint64>(m_panicDuration * m_panicVariation(m_rng));
            }
        }
        
        state.hasValidThreat = true;
        state.lastThreatPosition = threat->getPosition();
    } else if (state.isFleeing) {
        // Check if we're at safe distance
        float distanceToThreat = (entity->getPosition() - threat->getPosition()).length();
        if (distanceToThreat >= m_safeDistance) {
            state.isFleeing = false;
            state.isInPanic = false;
            state.hasValidThreat = false;
        }
    }

    // Update panic state
    if (state.isInPanic && currentTime >= state.panicEndTime) {
        state.isInPanic = false;
    }

    // Execute appropriate flee behavior
    if (state.isFleeing) {
        switch (m_fleeMode) {
            case FleeMode::PANIC_FLEE:
                updatePanicFlee(entity, state);
                break;
            case FleeMode::STRATEGIC_RETREAT:
                updateStrategicRetreat(entity, state);
                break;
            case FleeMode::EVASIVE_MANEUVER:
                updateEvasiveManeuver(entity, state);
                break;
            case FleeMode::SEEK_COVER:
                updateSeekCover(entity, state);
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
        state.panicEndTime = SDL_GetTicks() + static_cast<Uint64>(m_panicDuration);
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
    return (it != m_entityStates.end()) ? 
           (it->first->getPosition() - threat->getPosition()).length() : -1.0f;
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
    
    float distance = (entity->getPosition() - threat->getPosition()).length();
    return distance <= m_detectionRange;
}

Vector2D FleeBehavior::calculateFleeDirection(EntityPtr entity, EntityPtr threat, const EntityState& state) {
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
        float distance = (position - zone.center).length();
        if (distance < minDistance) {
            minDistance = distance;
            nearest = &zone;
        }
    }
    
    return nearest ? (nearest->center - position) : Vector2D(0, 0);
}

[[maybe_unused]] bool FleeBehavior::isPositionSafe(const Vector2D& position) const {
    EntityPtr threat = getThreat();
    if (!threat) return true;
    
    float distanceToThreat = (position - threat->getPosition()).length();
    return distanceToThreat >= m_safeDistance;
}

[[maybe_unused]] bool FleeBehavior::isNearBoundary(const Vector2D& position) const {
    // Use world bounds for boundary detection
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE = 32.0f;
        float worldMinX = minX * TILE + m_boundaryPadding;
        float worldMinY = minY * TILE + m_boundaryPadding;
        float worldMaxX = maxX * TILE - m_boundaryPadding;
        float worldMaxY = maxY * TILE - m_boundaryPadding;
        
        return (position.getX() < worldMinX || 
                position.getX() > worldMaxX ||
                position.getY() < worldMinY || 
                position.getY() > worldMaxY);
    } else {
        // Fallback: assume a large world if bounds unavailable
        const float defaultWorldSize = 3200.0f;
        return (position.getX() < m_boundaryPadding || 
                position.getX() > defaultWorldSize - m_boundaryPadding ||
                position.getY() < m_boundaryPadding || 
                position.getY() > defaultWorldSize - m_boundaryPadding);
    }
}

Vector2D FleeBehavior::avoidBoundaries(const Vector2D& position, const Vector2D& direction) const {
    Vector2D adjustedDir = direction;
    
    // Use world bounds for boundary avoidance with world-scale padding
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE = 32.0f;
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
    } else {
        // Fallback: assume a large world if bounds unavailable
        const float defaultWorldSize = 3200.0f;
        if (position.getX() < m_boundaryPadding && direction.getX() < 0) {
            adjustedDir.setX(std::abs(direction.getX())); // Force rightward
        } else if (position.getX() > defaultWorldSize - m_boundaryPadding && direction.getX() > 0) {
            adjustedDir.setX(-std::abs(direction.getX())); // Force leftward
        }
        
        if (position.getY() < m_boundaryPadding && direction.getY() < 0) {
            adjustedDir.setY(std::abs(direction.getY())); // Force downward
        } else if (position.getY() > defaultWorldSize - m_boundaryPadding && direction.getY() > 0) {
            adjustedDir.setY(-std::abs(direction.getY())); // Force upward
        }
    }
    
    return adjustedDir;
}

void FleeBehavior::updatePanicFlee(EntityPtr entity, EntityState& state) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Uint64 currentTime = SDL_GetTicks();
    
    // In panic mode, change direction more frequently and use longer distances
    if (currentTime - state.lastDirectionChange > 200 || state.fleeDirection.length() < 0.001f) {
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
        state.lastDirectionChange = currentTime;
        
        // In panic, try to flee much further to break out of small areas
        Vector2D currentPos = entity->getPosition();
        Vector2D panicDest = currentPos + state.fleeDirection * 1000.0f; // Much longer panic flee distance
        
        // Clamp to world bounds with larger margin for panic mode
        panicDest = AIInternal::ClampToWorld(panicDest, 100.0f);
    }
    
    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D intended = state.fleeDirection * m_fleeSpeed * speedModifier;
    Vector2D adjusted = AIInternal::ApplySeparation(entity, entity->getPosition(),
                          intended, m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4);
    entity->setVelocity(adjusted);
}

void FleeBehavior::updateStrategicRetreat(EntityPtr entity, EntityState& state) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Vector2D currentPos = entity->getPosition();
    Uint64 currentTime = SDL_GetTicks();

    // Strategic retreat: aim for a point away from threat (or toward nearest safe zone)
    if (currentTime - state.lastDirectionChange > 1000 || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(entity, threat, state);

        // Blend with nearest safe zone direction if exists
        Vector2D safeZoneDirection = findNearestSafeZone(currentPos);
        if (safeZoneDirection.length() > 0.001f) {
            Vector2D blended = (state.fleeDirection * 0.6f + normalizeVector(safeZoneDirection) * 0.4f);
            state.fleeDirection = normalizeVector(blended);
        }
        state.lastDirectionChange = currentTime;
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
    Vector2D dest = AIInternal::ClampToWorld(currentPos + state.fleeDirection * retreatDistance, 100.0f);

    // Try to path toward the retreat destination with TTL and no-progress checks
    auto tryFollowPath = [&](const Vector2D &goal, float speed)->bool {
        Uint64 now = SDL_GetTicks();
        const Uint64 pathTTL = 1500; const Uint64 noProgressWindow = 300;
        bool needRefresh = state.pathPoints.empty() || state.currentPathIndex >= state.pathPoints.size();
        if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
            float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
            if (d + 1.0f < state.lastNodeDistance) { state.lastNodeDistance = d; state.lastProgressTime = now; }
            else if (state.lastProgressTime == 0) { state.lastProgressTime = now; }
            else if (now - state.lastProgressTime > noProgressWindow) { needRefresh = true; }
        }
        if (now - state.lastPathUpdate > pathTTL) needRefresh = true;
        if (needRefresh && now >= state.nextPathAllowed) {
            // Use PathfinderManager for pathfinding requests
            auto& pathfinder = PathfinderManager::Instance();
            pathfinder.requestPath(entity->getID(), AIInternal::ClampToWorld(currentPos, 100.0f), goal, AIInternal::PathPriority::High,
                [&state](EntityID id, const std::vector<Vector2D>& path) {
                    state.pathPoints = path;
                    state.currentPathIndex = 0;
                });
            // Note: Path will be set via callback when ready
            if (!state.pathPoints.empty()) {
                state.currentPathIndex = 0;
                state.lastPathUpdate = now;
                state.lastNodeDistance = std::numeric_limits<float>::infinity();
                state.lastProgressTime = now;
                state.nextPathAllowed = now + 800; // cooldown
            } else {
                // Async path not ready, apply cooldown to prevent spam
                state.nextPathAllowed = now + 600; // Shorter cooldown for flee (more urgent)
            }
        }
        if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
            Vector2D node = state.pathPoints[state.currentPathIndex];
            Vector2D dir = node - currentPos; float len = dir.length();
            if (len > 0.01f) { dir = dir * (1.0f/len); entity->setVelocity(dir * speed); }
            if ((node - currentPos).length() <= state.navRadius) {
                ++state.currentPathIndex; state.lastNodeDistance = std::numeric_limits<float>::infinity(); state.lastProgressTime = now;
            }
            return true;
        }
        return false;
    };

    float speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPath(dest, m_fleeSpeed * speedModifier)) {
        // Fallback to direct flee when no path available
        Vector2D intended = state.fleeDirection * m_fleeSpeed * speedModifier;
        Vector2D adjusted = AIInternal::ApplySeparation(entity, entity->getPosition(),
                              intended, m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4);
        entity->setVelocity(adjusted);
    }
}

void FleeBehavior::updateEvasiveManeuver(EntityPtr entity, EntityState& state) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Uint64 currentTime = SDL_GetTicks();
    
    // Zigzag pattern
    if (currentTime - state.lastZigzagTime > m_zigzagInterval) {
        state.zigzagDirection *= -1; // Flip direction
        state.lastZigzagTime = currentTime;
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
    Vector2D intended = state.fleeDirection * m_fleeSpeed * speedModifier;
    Vector2D adjusted = AIInternal::ApplySeparation(entity, entity->getPosition(),
                          intended, m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4);
    entity->setVelocity(adjusted);
}

void FleeBehavior::updateSeekCover(EntityPtr entity, EntityState& state) {
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
    dest = AIInternal::ClampToWorld(dest, 100.0f);

    auto tryFollowPath = [&](const Vector2D &goal, float speed)->bool {
        Uint64 now = SDL_GetTicks();
        const Uint64 pathTTL = 1500; const Uint64 noProgressWindow = 300;
        bool needRefresh = state.pathPoints.empty() || state.currentPathIndex >= state.pathPoints.size();
        if (!needRefresh && state.currentPathIndex < state.pathPoints.size()) {
            float d = (state.pathPoints[state.currentPathIndex] - currentPos).length();
            if (d + 1.0f < state.lastNodeDistance) { state.lastNodeDistance = d; state.lastProgressTime = now; }
            else if (state.lastProgressTime == 0) { state.lastProgressTime = now; }
            else if (now - state.lastProgressTime > noProgressWindow) { needRefresh = true; }
        }
        if (now - state.lastPathUpdate > pathTTL) needRefresh = true;
        if (needRefresh && now >= state.nextPathAllowed) {
            // PATHFINDING CONSOLIDATION: All requests now use PathfinderManager
            PathfinderManager::Instance().requestPath(
                entity->getID(), AIInternal::ClampToWorld(currentPos, 100.0f), goal, AIInternal::PathPriority::High,
                [this, entity](EntityID, const std::vector<Vector2D>& path) {
                  if (!path.empty()) {
                    // Find the behavior state for this entity
                    auto it = m_entityStates.find(entity);
                    if (it != m_entityStates.end()) {
                      it->second.pathPoints = path;
                      it->second.currentPathIndex = 0;
                      it->second.lastPathUpdate = SDL_GetTicks();
                      it->second.lastNodeDistance = std::numeric_limits<float>::infinity();
                      it->second.lastProgressTime = SDL_GetTicks();
                      it->second.nextPathAllowed = SDL_GetTicks() + 800; // cooldown
                    }
                  }
                });
            // Remove the synchronous path check since we're using callback-based async path
            {
                // Async path not ready, apply cooldown to prevent spam
                state.nextPathAllowed = now + 600; // Shorter cooldown for flee (more urgent)
            }
        }
        if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
            Vector2D node = state.pathPoints[state.currentPathIndex];
            Vector2D dir = node - currentPos; float len = dir.length();
            if (len > 0.01f) { dir = dir * (1.0f/len); entity->setVelocity(dir * speed); }
            if ((node - currentPos).length() <= state.navRadius) {
                ++state.currentPathIndex; state.lastNodeDistance = std::numeric_limits<float>::infinity(); state.lastProgressTime = now;
            }
            return true;
        }
        return false;
    };

    float speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPath(dest, m_fleeSpeed * speedModifier)) {
        // Fallback to straight-line movement
        Vector2D intended = state.fleeDirection * m_fleeSpeed * speedModifier;
        Vector2D adjusted = AIInternal::ApplySeparation(entity, entity->getPosition(),
                              intended, m_fleeSpeed * speedModifier, 26.0f, 0.25f, 4);
        entity->setVelocity(adjusted);
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
