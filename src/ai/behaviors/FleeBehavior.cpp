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
#include "managers/EntityDataManager.hpp"  // For TransformData
#include "collisions/AABB.hpp"
#include <cmath>
#include <algorithm>

FleeBehavior::FleeBehavior(float fleeSpeed, float detectionRange, float safeDistance)
    : m_fleeSpeed(fleeSpeed)
    , m_detectionRange(detectionRange)
    , m_safeDistance(safeDistance)
{
  // Pre-reserve for large entity counts to avoid reallocations during gameplay
  m_entityStatesByIndex.reserve(16384);
}

FleeBehavior::FleeBehavior(FleeMode mode, float fleeSpeed, float detectionRange)
    : m_fleeMode(mode)
    , m_fleeSpeed(fleeSpeed)
    , m_detectionRange(detectionRange)
{
    // Pre-reserve for large entity counts to avoid reallocations during gameplay
    m_entityStatesByIndex.reserve(16384);
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

FleeBehavior::FleeBehavior(const HammerEngine::FleeBehaviorConfig& config, FleeMode mode)
    : m_config(config)
    , m_fleeMode(mode)
    , m_fleeSpeed(config.fleeSpeed)
    , m_detectionRange(config.safeDistance) // Use safeDistance as detection trigger
    , m_safeDistance(config.safeDistance)
    , m_boundaryPadding(config.worldPadding)
{
    // Pre-reserve for large entity counts to avoid reallocations during gameplay
    m_entityStatesByIndex.reserve(16384);
    // Mode-specific adjustments using config values
    switch (mode) {
        case FleeMode::PANIC_FLEE:
            m_fleeSpeed = config.fleeSpeed * 1.2f; // Faster in panic
            m_panicDuration = 2.0f; // 2 seconds of panic
            break;
        case FleeMode::STRATEGIC_RETREAT:
            m_fleeSpeed = config.fleeSpeed * 0.8f; // Slower, more calculated
            m_safeDistance = config.safeDistance * 1.5f;
            break;
        case FleeMode::EVASIVE_MANEUVER:
            m_zigzagInterval = 0.3f; // More frequent direction changes
            break;
        case FleeMode::SEEK_COVER:
            m_safeDistance = config.safeDistance * 1.2f;
            break;
    }
}

void FleeBehavior::init(EntityHandle handle) {
    if (!handle.isValid()) return;

    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx == SIZE_MAX) return;

    // Ensure vector is large enough for this index
    if (idx >= m_entityStatesByIndex.size()) {
      m_entityStatesByIndex.resize(idx + 1);
    }

    auto& hotData = edm.getHotDataByIndex(idx);
    auto& state = m_entityStatesByIndex[idx];
    state = EntityState(); // Reset to default state
    state.valid = true;
    state.handle = handle;  // Store handle for EDM lookups
    state.currentStamina = m_maxStamina;
    state.lastThreatPosition = hotData.transform.position;
}

void FleeBehavior::executeLogic(BehaviorContext& ctx) {
    if (!isActive()) return;

    // Get EDM reference early for path state and threat lookups
    auto& edm = EntityDataManager::Instance();

    // Get state by EDM index (contention-free vector access)
    if (ctx.edmIndex >= m_entityStatesByIndex.size()) {
        return;
    }
    EntityState& state = m_entityStatesByIndex[ctx.edmIndex];
    if (!state.valid) {
        return;
    }

    // Update all timers
    state.fleeTimer += ctx.deltaTime;
    state.directionChangeTimer += ctx.deltaTime;
    if (state.panicTimer > 0.0f) state.panicTimer -= ctx.deltaTime;
    state.zigzagTimer += ctx.deltaTime;
    if (state.backoffTimer > 0.0f) state.backoffTimer -= ctx.deltaTime;
    state.lastCrowdAnalysis += ctx.deltaTime;

    // Update EDM path timers (single source of truth for path state)
    auto& pathData = edm.getPathData(ctx.edmIndex);
    pathData.pathUpdateTimer += ctx.deltaTime;
    pathData.progressTimer += ctx.deltaTime;
    if (pathData.pathRequestCooldown > 0.0f) pathData.pathRequestCooldown -= ctx.deltaTime;

    // PERFORMANCE OPTIMIZATION: Cache crowd analysis results every 3-5 seconds
    // Reduces CollisionManager queries significantly for fleeing entities
    float crowdCacheInterval = 3.0f + (ctx.entityId % 200) * 0.01f; // 3-5 seconds
    if (state.lastCrowdAnalysis >= crowdCacheInterval) {
      Vector2D position = ctx.transform.position;
      float queryRadius = 100.0f; // Smaller radius for flee (lighter separation)
      state.cachedNearbyCount = AIInternal::GetNearbyEntitiesWithPositions(
          ctx.entityId, position, queryRadius, state.cachedNearbyPositions);
      state.lastCrowdAnalysis = 0.0f; // Reset timer
    }
    EntityHandle threatHandle = getThreatHandle();

    if (!threatHandle.isValid()) {
        // No threat detected, stop fleeing and recover stamina
        if (state.isFleeing) {
            state.isFleeing = false;
            state.isInPanic = false;
            state.hasValidThreat = false;
        }

        if (m_useStamina) {
            updateStamina(state, ctx.deltaTime, false);
        }
        return;
    }

    size_t threatIdx = edm.getIndex(threatHandle);
    if (threatIdx == SIZE_MAX) {
        if (state.isFleeing) {
            state.isFleeing = false;
            state.isInPanic = false;
            state.hasValidThreat = false;
        }
        return;
    }

    auto& threatHotData = edm.getHotDataByIndex(threatIdx);
    // Check if threat is in detection range
    Vector2D threatPos = threatHotData.transform.position;
    float distanceToThreatSquared = (ctx.transform.position - threatPos).lengthSquared();
    float const detectionRangeSquared = m_detectionRange * m_detectionRange;
    bool threatInRange = (distanceToThreatSquared <= detectionRangeSquared);


    if (threatInRange) {
        // Start fleeing if not already
        if (!state.isFleeing) {
            state.isFleeing = true;
            state.fleeTimer = 0.0f;
            state.lastThreatPosition = threatPos;

            // Determine if this should trigger panic
            if (m_fleeMode == FleeMode::PANIC_FLEE) {
                state.isInPanic = true;
                state.panicTimer = m_panicDuration * m_panicVariation(m_rng);
            }
        }

        state.hasValidThreat = true;
        state.lastThreatPosition = threatPos;
    } else if (state.isFleeing) {
        // PERFORMANCE: Use squared distance for comparison
        float const safeDistanceSquared = m_safeDistance * m_safeDistance;
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
                updatePanicFlee(ctx, state, threatPos);
                break;
            case FleeMode::STRATEGIC_RETREAT:
                updateStrategicRetreat(ctx, state, threatPos);
                break;
            case FleeMode::EVASIVE_MANEUVER:
                updateEvasiveManeuver(ctx, state, threatPos);
                break;
            case FleeMode::SEEK_COVER:
                updateSeekCover(ctx, state, threatPos);
                break;
        }

        if (m_useStamina) {
            updateStamina(state, ctx.deltaTime, true);
        }
    }
}

void FleeBehavior::clean(EntityHandle handle) {
    if (handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        size_t idx = edm.getIndex(handle);
        if (idx != SIZE_MAX) {
            // Clear EDM path state
            edm.clearPathData(idx);

            if (idx < m_entityStatesByIndex.size()) {
                m_entityStatesByIndex[idx].valid = false;
            }
        }
    } else {
        // Clear all states
        for (auto& state : m_entityStatesByIndex) {
            state.valid = false;
        }
    }
}

void FleeBehavior::onMessage(EntityHandle handle, const std::string& message) {
    if (!handle.isValid()) return;

    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx == SIZE_MAX || idx >= m_entityStatesByIndex.size() || !m_entityStatesByIndex[idx].valid)
        return;

    EntityState& state = m_entityStatesByIndex[idx];

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
    return std::any_of(m_entityStatesByIndex.begin(), m_entityStatesByIndex.end(),
                      [](const auto& state) { return state.valid && state.isFleeing; });
}

bool FleeBehavior::isInPanic() const {
    return std::any_of(m_entityStatesByIndex.begin(), m_entityStatesByIndex.end(),
                      [](const auto& state) { return state.valid && state.isInPanic; });
}

float FleeBehavior::getDistanceToThreat() const {
    // Phase 2 EDM Migration: Use handle-based lookup
    EntityHandle threatHandle = getThreatHandle();
    if (!threatHandle.isValid()) return -1.0f;

    auto& edm = EntityDataManager::Instance();
    size_t threatIdx = edm.getIndex(threatHandle);
    if (threatIdx == SIZE_MAX) return -1.0f;

    Vector2D threatPos = edm.getHotDataByIndex(threatIdx).transform.position;

    // Find a fleeing entity and get its position from EDM
    for (const auto& state : m_entityStatesByIndex) {
        if (!state.valid || !state.isFleeing) continue;
        if (!state.handle.isValid()) continue;

        size_t entityIdx = edm.getIndex(state.handle);
        if (entityIdx == SIZE_MAX) continue;

        Vector2D entityPos = edm.getHotDataByIndex(entityIdx).transform.position;
        float distSquared = (entityPos - threatPos).lengthSquared();
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

EntityHandle FleeBehavior::getThreatHandle() const {
    return AIManager::Instance().getPlayerHandle();
}

Vector2D FleeBehavior::getThreatPosition() const {
    return AIManager::Instance().getPlayerPosition();
}

bool FleeBehavior::isThreatInRange(const Vector2D& entityPos, const Vector2D& threatPos) const {
    // PERFORMANCE: Use squared distance
    float distanceSquared = (entityPos - threatPos).lengthSquared();
    float const detectionRangeSquared = m_detectionRange * m_detectionRange;
    return distanceSquared <= detectionRangeSquared;
}

Vector2D FleeBehavior::calculateFleeDirection(const Vector2D& entityPos, const Vector2D& threatPos, const EntityState& state) const {
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
    float minDistanceSquared = std::numeric_limits<float>::max();

    for (const auto& zone : m_safeZones) {
        // PERFORMANCE: Use squared distance throughout - avoid sqrt in loop
        float distanceSquared = (position - zone.center).lengthSquared();
        if (distanceSquared < minDistanceSquared) {
            minDistanceSquared = distanceSquared;
            nearest = &zone;
        }
    }

    return nearest ? (nearest->center - position) : Vector2D(0, 0);
}

[[maybe_unused]] bool FleeBehavior::isPositionSafe(const Vector2D& position) const {
    // Phase 2 EDM Migration: Use handle-based lookup
    EntityHandle threatHandle = getThreatHandle();
    if (!threatHandle.isValid()) return true;

    auto& edm = EntityDataManager::Instance();
    size_t threatIdx = edm.getIndex(threatHandle);
    if (threatIdx == SIZE_MAX) return true;

    Vector2D threatPos = edm.getHotDataByIndex(threatIdx).transform.position;

    // PERFORMANCE: Use squared distance
    float distanceToThreatSquared = (position - threatPos).lengthSquared();
    float const safeDistanceSquared = m_safeDistance * m_safeDistance;
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
    float const worldMinX = minX * TILE + m_boundaryPadding;
    float const worldMinY = minY * TILE + m_boundaryPadding;
    float const worldMaxX = maxX * TILE - m_boundaryPadding;
    float const worldMaxY = maxY * TILE - m_boundaryPadding;

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
    float const worldMinX = minX * TILE + worldPadding;
    float const worldMinY = minY * TILE + worldPadding;
    float const worldMaxX = maxX * TILE - worldPadding;
    float const worldMaxY = maxY * TILE - worldPadding;

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

void FleeBehavior::updatePanicFlee(BehaviorContext& ctx, EntityState& state, const Vector2D& threatPos) {
    Vector2D currentPos = ctx.transform.position;

    // In panic mode, change direction more frequently and use longer distances
    if (state.directionChangeTimer > 0.2f || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(currentPos, threatPos, state);

        // Add more randomness to panic movement for world-scale escape
        float randomAngle = m_angleVariation(m_rng) * 0.8f; // Increased randomness
        float cos_a = std::cos(randomAngle);
        float sin_a = std::sin(randomAngle);

        Vector2D const rotated(
            state.fleeDirection.getX() * cos_a - state.fleeDirection.getY() * sin_a,
            state.fleeDirection.getX() * sin_a + state.fleeDirection.getY() * cos_a
        );

        state.fleeDirection = rotated;
        state.directionChangeTimer = 0.0f;

        // In panic, try to flee much further to break out of small areas
        Vector2D panicDest = currentPos + state.fleeDirection * 1000.0f; // Much longer panic flee distance

        // Clamp to world bounds with larger margin for panic mode
        panicDest = pathfinder().clampToWorldBounds(panicDest, 100.0f);
    }

    float const speedModifier = calculateFleeSpeedModifier(state);
    Vector2D const intended = state.fleeDirection * m_fleeSpeed * speedModifier;
    // Set velocity directly - CollisionManager handles overlap resolution
    ctx.transform.velocity = intended;
}

void FleeBehavior::updateStrategicRetreat(BehaviorContext& ctx, EntityState& state, const Vector2D& threatPos) {
    Vector2D currentPos = ctx.transform.position;

    // Strategic retreat: aim for a point away from threat (or toward nearest safe zone)
    if (state.directionChangeTimer > 1.0f || state.fleeDirection.length() < 0.001f) {
        state.fleeDirection = calculateFleeDirection(currentPos, threatPos, state);

        // Blend with nearest safe zone direction if exists
        Vector2D const safeZoneDirection = findNearestSafeZone(currentPos);
        if (safeZoneDirection.length() > 0.001f) {
            Vector2D const blended = (state.fleeDirection * 0.6f + normalizeVector(safeZoneDirection) * 0.4f);
            state.fleeDirection = normalizeVector(blended);
        }
        state.directionChangeTimer = 0.0f;
    }

    // Dynamic retreat distance based on local entity density
    float const baseRetreatDistance = 800.0f; // Increased base for world scale
    int nearbyCount = 0;

    // Check for other fleeing entities to avoid clustering in same escape routes
    nearbyCount = AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 100.0f);

    float retreatDistance = baseRetreatDistance;
    if (nearbyCount > 2) {
        // High density: encourage longer retreat to spread fleeing entities
        retreatDistance = baseRetreatDistance * 1.8f; // Up to 1440px retreat

        // Also add lateral bias to prevent all entities fleeing in same direction
        Vector2D const lateral(-state.fleeDirection.getY(), state.fleeDirection.getX());
        float lateralBias = ((float)(ctx.entityId % 5) - 2.0f) * 0.3f; // -0.6 to +0.6
        state.fleeDirection = (state.fleeDirection + lateral * lateralBias).normalized();
    } else if (nearbyCount > 0) {
        // Medium density: moderate expansion
        retreatDistance = baseRetreatDistance * 1.3f;
    }

    // Compute a retreat destination further ahead and clamp to world bounds
    Vector2D dest = pathfinder().clampToWorldBounds(currentPos + state.fleeDirection * retreatDistance, 100.0f);

    // OPTIMIZATION: Use extracted method instead of lambda for better compiler optimization
    float const speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPathToGoal(ctx, state, dest, m_fleeSpeed * speedModifier)) {
        // Fallback to direct flee when no path available
        Vector2D const intended2 = state.fleeDirection * m_fleeSpeed * speedModifier;
        // Set velocity directly - CollisionManager handles overlap resolution
        ctx.transform.velocity = intended2;
    }
}

void FleeBehavior::updateEvasiveManeuver(BehaviorContext& ctx, EntityState& state, const Vector2D& threatPos) {
    Vector2D currentPos = ctx.transform.position;

    // Zigzag pattern
    if (state.zigzagTimer > m_zigzagInterval) {
        state.zigzagDirection *= -1; // Flip direction
        state.zigzagTimer = 0.0f;
    }

    // Base flee direction
    Vector2D baseFleeDir = calculateFleeDirection(currentPos, threatPos, state);

    // Apply zigzag
    float zigzagAngleRad = (m_zigzagAngle * M_PI / 180.0f) * state.zigzagDirection;
    float cos_z = std::cos(zigzagAngleRad);
    float sin_z = std::sin(zigzagAngleRad);

    Vector2D const zigzagDir(
        baseFleeDir.getX() * cos_z - baseFleeDir.getY() * sin_z,
        baseFleeDir.getX() * sin_z + baseFleeDir.getY() * cos_z
    );

    state.fleeDirection = normalizeVector(zigzagDir);

    float const speedModifier = calculateFleeSpeedModifier(state);
    Vector2D const intended3 = state.fleeDirection * m_fleeSpeed * speedModifier;
    // Set velocity directly - CollisionManager handles overlap resolution
    ctx.transform.velocity = intended3;
}

void FleeBehavior::updateSeekCover(BehaviorContext& ctx, EntityState& state, const Vector2D& threatPos) {
    // Move toward nearest safe zone using pathfinding when possible
    Vector2D currentPos = ctx.transform.position;
    Vector2D const safeZoneDirection = findNearestSafeZone(currentPos);

    // Dynamic cover seeking distance based on entity density
    float const baseCoverDistance = 720.0f; // Increased base for world scale
    int nearbyCount = 0;

    // Check for clustering of other cover-seekers
    nearbyCount = AIInternal::CountNearbyEntities(ctx.entityId, currentPos, 90.0f);

    float coverDistance = baseCoverDistance;
    if (nearbyCount > 2) {
        // High density: seek cover further away to spread entities
        coverDistance = baseCoverDistance * 1.6f; // Up to 1152px
    } else if (nearbyCount > 0) {
        // Medium density: moderate expansion
        coverDistance = baseCoverDistance * 1.2f;
    }

    Vector2D dest;
    if (safeZoneDirection.length() > 0.001f) {
        state.fleeDirection = normalizeVector(safeZoneDirection);
        dest = currentPos + state.fleeDirection * coverDistance;
    } else {
        // No safe zones, move away from threat with larger distance
        state.fleeDirection = calculateFleeDirection(currentPos, threatPos, state);
        dest = currentPos + state.fleeDirection * coverDistance;
    }

    // Clamp destination within world bounds
    dest = pathfinder().clampToWorldBounds(dest, 100.0f);

    // OPTIMIZATION: Use extracted method instead of lambda for better compiler optimization
    float const speedModifier = calculateFleeSpeedModifier(state);
    if (!tryFollowPathToGoal(ctx, state, dest, m_fleeSpeed * speedModifier)) {
        // Fallback to straight-line movement
        Vector2D const intended4 = state.fleeDirection * m_fleeSpeed * speedModifier;
        // Set velocity directly - CollisionManager handles overlap resolution
        ctx.transform.velocity = intended4;
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
    float const magnitude = direction.length();
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
        float const staminaRatio = state.currentStamina / m_maxStamina;
        modifier *= (0.3f + 0.7f * staminaRatio); // Speed ranges from 30% to 100%
    }

    return modifier;
}

// OPTIMIZATION: Uses EDM path state for per-entity isolation and no shared_from_this() overhead
// This method handles path-following logic with TTL and no-progress checks
bool FleeBehavior::tryFollowPathToGoal(BehaviorContext& ctx, EntityState& state, const Vector2D& goal, float speed) {
    // PERFORMANCE: Increase TTL to reduce pathfinding frequency
    constexpr float pathTTL = 2.5f;
    constexpr float noProgressWindow = 0.4f;
    constexpr float GOAL_CHANGE_THRESH_SQUARED = 180.0f * 180.0f; // Increased from 96px

    Vector2D currentPos = ctx.transform.position;

    // Read path state from EDM (single source of truth, per-entity isolation)
    auto& edm = EntityDataManager::Instance();
    auto& pathData = edm.getPathData(ctx.edmIndex);

    // Check if path needs refresh
    bool needRefresh = !pathData.hasPath || pathData.navIndex >= pathData.navPath.size();

    // Check for progress towards current waypoint
    if (!needRefresh && pathData.isFollowingPath()) {
        float d = (pathData.navPath[pathData.navIndex] - currentPos).length();
        if (d + 1.0f < pathData.lastNodeDistance) {
            pathData.lastNodeDistance = d;
            pathData.progressTimer = 0.0f;
        } else if (pathData.progressTimer > noProgressWindow) {
            needRefresh = true;
        }
    }

    // Check if path is stale
    if (pathData.pathUpdateTimer > pathTTL) {
        needRefresh = true;
    }

    // Request new path if needed and cooldown allows
    if (needRefresh && pathData.pathRequestCooldown <= 0.0f) {
        // Gate refresh on significant goal change to avoid thrash
        bool goalChanged = true;
        if (pathData.hasPath && !pathData.navPath.empty()) {
            Vector2D lastGoal = pathData.navPath.back();
            goalChanged = ((goal - lastGoal).lengthSquared() > GOAL_CHANGE_THRESH_SQUARED);
        }

        // Only request new path if goal changed significantly
        if (goalChanged) {
            // EDM-integrated async pathfinding - result written directly to EDM
            auto& pf = this->pathfinder();
            pf.requestPathToEDM(
                ctx.edmIndex,
                pf.clampToWorldBounds(currentPos, 100.0f),
                goal,
                PathfinderManager::Priority::High);

            // Apply cooldown to prevent spam
            pathData.pathRequestCooldown = 0.6f; // Shorter cooldown for flee (more urgent)
        }
    }

    // Follow existing path if available (using EDM path state)
    if (pathData.isFollowingPath()) {
        Vector2D node = pathData.getCurrentWaypoint();
        Vector2D dir = node - currentPos;
        float const len = dir.length();

        if (len > 0.01f) {
            dir = dir * (1.0f / len);
            ctx.transform.velocity = dir * speed;
        }

        // Check if reached current waypoint
        if (len <= state.navRadius) {
            pathData.advanceWaypoint();
        }

        return true;
    }

    return false;
}
