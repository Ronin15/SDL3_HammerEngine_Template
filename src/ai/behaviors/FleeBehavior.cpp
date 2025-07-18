/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/FleeBehavior.hpp"
#include "managers/AIManager.hpp"
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

std::string FleeBehavior::getName() const {
    return "Flee";
}

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

void FleeBehavior::setScreenBounds(float width, float height) {
    m_screenWidth = width;
    m_screenHeight = height;
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
    clone->m_screenWidth = m_screenWidth;
    clone->m_screenHeight = m_screenHeight;
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

bool FleeBehavior::isPositionSafe(const Vector2D& position) const {
    EntityPtr threat = getThreat();
    if (!threat) return true;
    
    float distanceToThreat = (position - threat->getPosition()).length();
    return distanceToThreat >= m_safeDistance;
}

bool FleeBehavior::isNearBoundary(const Vector2D& position) const {
    return (position.getX() < m_boundaryPadding || 
            position.getX() > m_screenWidth - m_boundaryPadding ||
            position.getY() < m_boundaryPadding || 
            position.getY() > m_screenHeight - m_boundaryPadding);
}

Vector2D FleeBehavior::avoidBoundaries(const Vector2D& position, const Vector2D& direction) const {
    Vector2D adjustedDir = direction;
    
    // Check boundaries and adjust direction
    if (position.getX() < m_boundaryPadding && direction.getX() < 0) {
        adjustedDir.setX(std::abs(direction.getX())); // Force rightward
    } else if (position.getX() > m_screenWidth - m_boundaryPadding && direction.getX() > 0) {
        adjustedDir.setX(-std::abs(direction.getX())); // Force leftward
    }
    
    if (position.getY() < m_boundaryPadding && direction.getY() < 0) {
        adjustedDir.setY(std::abs(direction.getY())); // Force downward
    } else if (position.getY() > m_screenHeight - m_boundaryPadding && direction.getY() > 0) {
        adjustedDir.setY(-std::abs(direction.getY())); // Force upward
    }
    
    return adjustedDir;
}

void FleeBehavior::updatePanicFlee(EntityPtr entity, EntityState& state) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Uint64 currentTime = SDL_GetTicks();
    
    // In panic mode, change direction more frequently
    if (currentTime - state.lastDirectionChange > 200 || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(entity, threat, state);
        
        // Add some randomness to panic movement
        float randomAngle = m_angleVariation(m_rng);
        float cos_a = std::cos(randomAngle);
        float sin_a = std::sin(randomAngle);
        
        Vector2D rotated(
            state.fleeDirection.getX() * cos_a - state.fleeDirection.getY() * sin_a,
            state.fleeDirection.getX() * sin_a + state.fleeDirection.getY() * cos_a
        );
        
        state.fleeDirection = rotated;
        state.lastDirectionChange = currentTime;
    }
    
    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D velocity = state.fleeDirection * m_fleeSpeed * speedModifier;
    entity->setVelocity(velocity);
}

void FleeBehavior::updateStrategicRetreat(EntityPtr entity, EntityState& state) {
    EntityPtr threat = getThreat();
    if (!threat) return;
    
    Vector2D currentPos = entity->getPosition();
    Uint64 currentTime = SDL_GetTicks();
    
    // Strategic retreat: plan a good escape route
    if (currentTime - state.lastDirectionChange > 1000 || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(entity, threat, state);
        
        // Look for safe zones
        Vector2D safeZoneDirection = findNearestSafeZone(currentPos);
        if (safeZoneDirection.length() > 0.001f) {
            // Blend flee direction with safe zone direction
            Vector2D blended = (state.fleeDirection * 0.6f + normalizeVector(safeZoneDirection) * 0.4f);
            state.fleeDirection = normalizeVector(blended);
        }
        
        state.lastDirectionChange = currentTime;
    }
    
    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D velocity = state.fleeDirection * m_fleeSpeed * speedModifier;
    entity->setVelocity(velocity);
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
    Vector2D velocity = state.fleeDirection * m_fleeSpeed * speedModifier;
    entity->setVelocity(velocity);
}

void FleeBehavior::updateSeekCover(EntityPtr entity, EntityState& state) {
    // Return to guard post  
    Vector2D currentPos = entity->getPosition();
    Vector2D safeZoneDirection = findNearestSafeZone(currentPos);
    
    if (safeZoneDirection.length() > 0.001f) {
        state.fleeDirection = normalizeVector(safeZoneDirection);
    } else {
        // No safe zones, use regular flee behavior
        EntityPtr threat = getThreat();
        if (threat) {
            state.fleeDirection = calculateFleeDirection(entity, threat, state);
        }
    }
    float speedModifier = calculateFleeSpeedModifier(state);
    Vector2D velocity = state.fleeDirection * m_fleeSpeed * speedModifier;
    entity->setVelocity(velocity);
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