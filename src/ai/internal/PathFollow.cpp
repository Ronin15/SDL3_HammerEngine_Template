#include "ai/internal/PathFollow.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/CollisionManager.hpp"
#include "collisions/AABB.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace AIInternal {

// Cache for expensive world bounds calculations
struct WorldBoundsCache {
    bool valid = false;
    float minX, minY, maxX, maxY;
    float worldMinX, worldMinY, worldMaxX, worldMaxY;
    static constexpr float TILE = 32.0f;
    
    void update() {
        if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
            worldMinX = minX * TILE;
            worldMinY = minY * TILE; 
            worldMaxX = maxX * TILE;
            worldMaxY = maxY * TILE;
            valid = true;
        } else {
            valid = false;
        }
    }
    
    Vector2D clamp(const Vector2D& p, float margin) const {
        if (!valid) return p;
        return Vector2D(
            std::clamp(p.getX(), worldMinX + margin, worldMaxX - margin),
            std::clamp(p.getY(), worldMinY + margin, worldMaxY - margin)
        );
    }
};

static thread_local WorldBoundsCache g_boundsCache;

Vector2D ClampToWorld(const Vector2D &p, float margin) {
    if (!g_boundsCache.valid) {
        g_boundsCache.update();
    }
    return g_boundsCache.clamp(p, margin);
}

WorldBoundsPixels GetWorldBoundsInPixels() {
    WorldBoundsPixels result{0, 0, 0, 0, false};
    float minX, minY, maxX, maxY;
    if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE = 32.0f;
        result.minX = minX * TILE;
        result.minY = minY * TILE;
        result.maxX = maxX * TILE;
        result.maxY = maxY * TILE;
        result.valid = true;
    }
    return result;
}

static void requestTo(EntityPtr entity,
                      const Vector2D &from,
                      const Vector2D &goal,
                      std::vector<Vector2D> &outPath,
                      size_t &idx,
                      Uint64 &lastUpdate,
                      Uint64 now,
                      Uint64 &lastProgress,
                      float &lastNodeDist) {
    // PATHFINDING CONSOLIDATION: Route all requests through async PathfindingScheduler 
    // to utilize PathCache and improve timeout rates
    AIManager::Instance().requestPathAsync(entity, from, goal, AIManager::PathPriority::Normal);
    outPath = AIManager::Instance().getAsyncPath(entity);
    idx = 0;
    lastUpdate = now;
    lastNodeDist = std::numeric_limits<float>::infinity();
    lastProgress = now;
}

bool RefreshPathWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &desiredGoal,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    Uint64 &lastPathUpdate,
    Uint64 &lastProgressTime,
    float &lastNodeDistance,
    const PathPolicy &policy) {

    Uint64 now = SDL_GetTicks();
    bool needRefresh = pathPoints.empty() || currentPathIndex >= pathPoints.size();
    if (!needRefresh && currentPathIndex < pathPoints.size()) {
        float d = (pathPoints[currentPathIndex] - currentPos).length();
        // More lenient progress detection - require meaningful distance reduction
        if (d + 8.0f < lastNodeDistance) { // World-scale movement requires larger progress threshold
            lastNodeDistance = d; 
            lastProgressTime = now; 
        } else if (lastProgressTime == 0) { 
            lastProgressTime = now; 
        } else if (now - lastProgressTime > policy.noProgressWindow) { 
            // Additional check: only refresh if we're not very close to the current node
            // This prevents constant refreshing when entity is near but can't reach the exact node
            if (d > policy.nodeRadius * 3.0f) { // Increased from 2.0f to 3.0f for more stability
                needRefresh = true; 
            }
        }
    }
    if (now - lastPathUpdate > policy.pathTTL) needRefresh = true;
    if (!needRefresh) return false;

    // PATHFINDING CONSOLIDATION: All requests now go through PathfindingScheduler via async pathway
    // Clamp both current position and desired goal to world bounds (use 100px margin to match pathfinding boundary requirements)
    Vector2D clampedCurrentPos = ClampToWorld(currentPos, 100.0f);
    Vector2D clampedGoal = ClampToWorld(desiredGoal, 100.0f);
    requestTo(entity, clampedCurrentPos, clampedGoal, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
    if (!pathPoints.empty() || !policy.allowDetours) return true;

    // Try detours around the goal, but only if we haven't tried too many times recently
    static thread_local std::unordered_map<EntityID, Uint64> lastDetourAttempt;
    EntityID entityId = entity->getID();
    
    if (now - lastDetourAttempt[entityId] > 4000) { // Increased cooldown to reduce spam
        lastDetourAttempt[entityId] = now;
        
        // Check if we're in a crowded area - if so, try alternative targets instead of detours
        AABB crowdCheck(currentPos.getX() - 50.0f, currentPos.getY() - 50.0f, 100.0f, 100.0f);
        std::vector<EntityID> nearbyEntities;
        CollisionManager::Instance().queryArea(crowdCheck, nearbyEntities);
        
        bool inCrowdedArea = false;
        int neighborCount = 0;
        for (EntityID id : nearbyEntities) {
            if (id != entityId) neighborCount++;
        }
        inCrowdedArea = (neighborCount >= 4); // Crowded if 4+ nearby NPCs
        
        if (inCrowdedArea) {
            // Try alternative targets at increasing distances from original goal
            for (float distance : {150.0f, 250.0f, 400.0f}) {
                for (float angle : {0.0f, 1.57f, 3.14f, 4.71f, 0.78f, 2.35f, 3.92f, 5.49f}) {
                    Vector2D offset(distance * cosf(angle), distance * sinf(angle));
                    Vector2D alternativeGoal = ClampToWorld(clampedGoal + offset, 100.0f);
                    
                    // Check if alternative goal is less crowded
                    AABB altCheck(alternativeGoal.getX() - 40.0f, alternativeGoal.getY() - 40.0f, 80.0f, 80.0f);
                    std::vector<EntityID> altNearby;
                    CollisionManager::Instance().queryArea(altCheck, altNearby);
                    
                    if (altNearby.size() < nearbyEntities.size() / 2) { // Less crowded area
                        requestTo(entity, currentPos, alternativeGoal, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
                        if (!pathPoints.empty()) return true;
                    }
                }
            }
        } else {
            // Standard detour behavior for non-crowded areas
            for (float r : policy.detourRadii) {
                for (float a : policy.detourAngles) {
                    Vector2D offset(std::cos(a) * r, std::sin(a) * r);
                    Vector2D alt = ClampToWorld(clampedGoal + offset, 100.0f);
                    requestTo(entity, currentPos, alt, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
                    if (!pathPoints.empty()) return true;
                }
            }
        }
    }
    
    // If we still have no path, set a temporary fallback goal in the general direction
    if (pathPoints.empty()) {
        Vector2D direction = clampedGoal - currentPos;
        if (direction.length() > 0.1f) {
            direction.normalize();
            Vector2D fallbackGoal = currentPos + direction * 100.0f; // Move 100px in the right direction
            fallbackGoal = ClampToWorld(fallbackGoal, 100.0f);
            requestTo(entity, currentPos, fallbackGoal, pathPoints, currentPathIndex, lastPathUpdate, now, lastProgressTime, lastNodeDistance);
        }
    }
    
    return true; // refreshed, but path may still be empty
}

bool FollowPathStepWithPolicy(
    EntityPtr entity,
    const Vector2D &currentPos,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    float speed,
    float nodeRadius,
    float lateralBias) {
    if (pathPoints.empty() || currentPathIndex >= pathPoints.size()) return false;
    Vector2D node = pathPoints[currentPathIndex];
    Vector2D dir = node - currentPos;
    float len = dir.length();
    if (len > 0.01f) {
        dir = dir * (1.0f / len);
        if (lateralBias > 0.0f) {
            Vector2D perp(-dir.getY(), dir.getX());
            float side = ((entity->getID() & 1) ? 1.0f : -1.0f);
            Vector2D biased = dir * 1.0f + perp * (lateralBias * side);
            float bl = biased.length(); if (bl > 0.001f) biased = biased * (1.0f / bl);
            entity->setVelocity(biased * speed);
        } else {
            entity->setVelocity(dir * speed);
        }
    }
    if ((node - currentPos).length() <= nodeRadius) {
        ++currentPathIndex;
    }
    return true;
}

// Optimized async request tracking with per-entity state management
struct AsyncRequestState {
    Uint64 lastRequestTime = 0;
    bool hasValidPath = false;
    static constexpr Uint64 MIN_REQUEST_INTERVAL = 2500;
};

static std::unordered_map<EntityID, AsyncRequestState> g_asyncStates;

static void requestToAsync(EntityPtr entity,
                           const Vector2D &from,
                           const Vector2D &goal,
                           std::vector<Vector2D> &outPath,
                           size_t &idx,
                           Uint64 &lastUpdate,
                           Uint64 now,
                           Uint64 &lastProgress,
                           float &lastNodeDist,
                           int priority) {
    EntityID entityId = entity->getID();
    AsyncRequestState& state = g_asyncStates[entityId];
    
    // Check if we recently made an async request for this entity
    if (state.lastRequestTime != 0 && (now - state.lastRequestTime) < AsyncRequestState::MIN_REQUEST_INTERVAL) {
        // Still waiting for previous request, just check if result is ready
        if (AIManager::Instance().hasAsyncPath(entity)) {
            outPath = AIManager::Instance().getAsyncPath(entity);
            idx = 0;
            lastUpdate = now;
            lastNodeDist = std::numeric_limits<float>::infinity();
            lastProgress = now;
            state.lastRequestTime = 0; // Clear tracking when path received
            state.hasValidPath = true;
        }
        return;
    }
    
    AIManager::PathPriority asyncPriority = static_cast<AIManager::PathPriority>(priority);
    AIManager::Instance().requestPathAsync(entity, from, goal, asyncPriority);
    state.lastRequestTime = now; // Track when we made the request
    
    // Check if async path is ready immediately (from previous request)
    if (AIManager::Instance().hasAsyncPath(entity)) {
        outPath = AIManager::Instance().getAsyncPath(entity);
        idx = 0;
        lastUpdate = now;
        lastNodeDist = std::numeric_limits<float>::infinity();
        lastProgress = now;
        state.lastRequestTime = 0; // Clear tracking when path received
        state.hasValidPath = true;
    }
}

bool RefreshPathWithPolicyAsync(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &desiredGoal,
    std::vector<Vector2D> &pathPoints,
    size_t &currentPathIndex,
    Uint64 &lastPathUpdate,
    Uint64 &lastProgressTime,
    float &lastNodeDistance,
    const PathPolicy &policy,
    int priority) {

    // Distance-based path segmentation for long journeys (use 100px margin to match pathfinding boundary requirements)
    Vector2D clampedCurrentPos = ClampToWorld(currentPos, 100.0f);
    Vector2D clampedGoal = ClampToWorld(desiredGoal, 100.0f);
    float directDistance = (clampedGoal - clampedCurrentPos).length();
    const float MAX_PATH_DISTANCE = 1200.0f; // ~37 tiles at 32px/tile
    
    Vector2D effectiveGoal = clampedGoal;
    if (directDistance > MAX_PATH_DISTANCE) {
        // Create intermediate waypoint toward goal
        Vector2D direction = (clampedGoal - clampedCurrentPos).normalized();
        effectiveGoal = clampedCurrentPos + direction * MAX_PATH_DISTANCE;
        effectiveGoal = ClampToWorld(effectiveGoal, 100.0f);
    }

    Uint64 now = SDL_GetTicks();
    bool needRefresh = pathPoints.empty() || currentPathIndex >= pathPoints.size();
    
    if (!needRefresh && currentPathIndex < pathPoints.size()) {
        float d = (pathPoints[currentPathIndex] - currentPos).length();
        // More lenient progress detection - require meaningful distance reduction
        if (d + 8.0f < lastNodeDistance) { // World-scale movement requires larger progress threshold
            lastNodeDistance = d; 
            lastProgressTime = now; 
        } else if (lastProgressTime == 0) { 
            lastProgressTime = now; 
        } else if (now - lastProgressTime > policy.noProgressWindow) { 
            // Additional check: only refresh if we're not very close to the current node
            // This prevents constant refreshing when entity is near but can't reach the exact node
            if (d > policy.nodeRadius * 3.0f) { // Increased from 2.0f to 3.0f for more stability
                needRefresh = true; 
            }
        }
    }
    // Longer TTL for async paths to reduce refresh frequency
    Uint64 pathTTL = policy.pathTTL * 2; // Double the TTL for async paths
    if (now - lastPathUpdate > pathTTL) needRefresh = true;
    
    // If we need a refresh, request async path
    if (needRefresh) {
        requestToAsync(entity, clampedCurrentPos, effectiveGoal, pathPoints, currentPathIndex, 
                      lastPathUpdate, now, lastProgressTime, lastNodeDistance, priority);
        
        // If no async path is ready yet, try detours if allowed
        if (pathPoints.empty() && policy.allowDetours) {
            // Optimized detour tracking with reduced memory overhead
            static thread_local std::unordered_map<EntityID, std::pair<Uint64, uint8_t>> detourTracking;
            
            EntityID entityId = entity->getID();
            auto& [lastDetourTime, detourCount] = detourTracking[entityId];
            
            if (now - lastDetourTime > 5000) {
                detourCount = 0;
                lastDetourTime = now;
            }
            
            if (detourCount < 4) {
                // Try most promising detour angles first (cardinal and diagonal directions)
                static const float priorityAngles[] = {0.0f, 1.57f, 3.14f, 4.71f, 0.78f, 2.35f, 3.92f, 5.49f};
                
                for (float angle : priorityAngles) {
                    if (detourCount >= 4) break;
                    
                    for (float radius : policy.detourRadii) {
                        Vector2D offset(radius * cosf(angle), radius * sinf(angle));
                        Vector2D detourGoal = ClampToWorld(effectiveGoal + offset, 100.0f);
                        requestToAsync(entity, currentPos, detourGoal, pathPoints, currentPathIndex,
                                     lastPathUpdate, now, lastProgressTime, lastNodeDistance, priority);
                        if (!pathPoints.empty()) {
                            detourCount++;
                            return true;
                        }
                    }
                }
            }
        }
        return !pathPoints.empty();
    }
    
    // Check if an async path became ready, but only if we don't have a recent valid path
    // This prevents rapid path switching when paths are working fine
    if (AIManager::Instance().hasAsyncPath(entity) && 
        (pathPoints.empty() || (now - lastPathUpdate) > 3000)) {
        pathPoints = AIManager::Instance().getAsyncPath(entity);
        currentPathIndex = 0;
        lastPathUpdate = now;
        lastNodeDistance = std::numeric_limits<float>::infinity();
        lastProgressTime = now;
        return true;
    }
    
    return !pathPoints.empty();
}

AIInternal::YieldResult CheckYieldAndRedirect(
    EntityPtr entity,
    const Vector2D &currentPos,
    const Vector2D &intendedDirection,
    float intendedSpeed) {
    
    AIInternal::YieldResult result;
    if (!entity || intendedDirection.length() < 0.01f) return result;
    
    Vector2D normalizedDir = intendedDirection.normalized();
    
    // Query entities in front of this entity
    float queryRadius = std::max(64.0f, intendedSpeed * 2.0f);
    Vector2D frontCenter = currentPos + normalizedDir * queryRadius * 0.5f;
    HammerEngine::AABB queryArea(frontCenter.getX() - queryRadius, frontCenter.getY() - queryRadius,
                   queryRadius * 2.0f, queryRadius * 2.0f);
    
    std::vector<EntityID> nearbyEntities;
    CollisionManager::Instance().queryArea(queryArea, nearbyEntities);
    
    int entitiesInPath = 0;
    int slowMovingInPath = 0;
    Vector2D crowdCenter(0, 0);
    
    for (EntityID id : nearbyEntities) {
        if (id == entity->getID()) continue;
        
        Vector2D entityPos;
        if (!CollisionManager::Instance().getBodyCenter(id, entityPos)) continue;
        
        Vector2D toEntity = entityPos - currentPos;
        float distToEntity = toEntity.length();
        if (distToEntity > queryRadius) continue;
        
        // Check if entity is in our intended path (within 30 degree cone)
        float alignment = toEntity.normalized().dot(normalizedDir);
        if (alignment > 0.8f) { // ~36 degree cone
            entitiesInPath++;
            crowdCenter = crowdCenter + entityPos;
            
            // Check if the blocking entity is moving slowly
            // (This would require getting entity velocity, simplified for now)
            if (distToEntity < 48.0f) {
                slowMovingInPath++;
            }
        }
    }
    
    if (entitiesInPath > 0) {
        crowdCenter = crowdCenter / static_cast<float>(entitiesInPath);
        
        // Determine yield vs redirect strategy
        if (slowMovingInPath >= 2 || entitiesInPath >= 3) {
            // Multiple slow entities ahead - yield briefly then redirect
            result.shouldYield = true;
            result.yieldDuration = 200 + (entity->getID() % 300); // Staggered yield times
            
            // Calculate redirect direction - perpendicular to intended direction
            Vector2D perpendicular(-normalizedDir.getY(), normalizedDir.getX());
            Vector2D awayFromCrowd = (currentPos - crowdCenter).normalized();
            
            // Choose perpendicular direction that moves away from crowd
            if (perpendicular.dot(awayFromCrowd) < 0) {
                perpendicular = Vector2D(normalizedDir.getY(), -normalizedDir.getX());
            }
            
            result.shouldRedirect = true;
            result.redirectDirection = normalizedDir * 0.6f + perpendicular * 0.8f;
            result.redirectDirection.normalize();
        } else if (entitiesInPath == 1) {
            // Single entity ahead - brief yield to let them pass
            result.shouldYield = true;
            result.yieldDuration = 150 + (entity->getID() % 200);
        }
    }
    
    return result;
}

bool ApplyYieldBehavior(
    EntityPtr entity,
    const AIInternal::YieldResult &yieldResult,
    Uint64 &yieldStartTime,
    Uint64 currentTime) {
    
    if (!yieldResult.shouldYield) {
        yieldStartTime = 0;
        return false;
    }
    
    // Start yielding if not already
    if (yieldStartTime == 0) {
        yieldStartTime = currentTime;
    }
    
    // Check if yield period is over
    if (currentTime - yieldStartTime >= yieldResult.yieldDuration) {
        yieldStartTime = 0;
        return false;
    }
    
    // Apply yielding behavior - slow down or stop
    Vector2D currentVel = entity->getVelocity();
    Vector2D reducedVel = currentVel * 0.2f; // Slow to 20% speed
    entity->setVelocity(reducedVel);
    
    return true; // Currently yielding
}

bool HandleDynamicStuckDetection(
    EntityPtr entity,
    StuckDetectionState &stuckState,
    Uint64 currentTime) {
    
    if (!entity) return false;
    
    Vector2D currentPos = entity->getPosition();
    stuckState.updatePosition(currentPos, currentTime);
    
    // Check if stuck and needs immediate escape
    if (stuckState.checkIfStuck(entity, currentTime) && stuckState.needsEscape(currentTime)) {
        // Find immediate escape direction
        AABB queryArea(currentPos.getX() - 80.0f, currentPos.getY() - 80.0f, 160.0f, 160.0f);
        std::vector<EntityID> nearbyEntities;
        CollisionManager::Instance().queryArea(queryArea, nearbyEntities);
        
        // Calculate crowd center to escape from
        Vector2D crowdCenter = currentPos; // Default to current position
        int entityCount = 0;
        
        for (EntityID id : nearbyEntities) {
            if (id == entity->getID()) continue;
            Vector2D entityPos;
            if (CollisionManager::Instance().getBodyCenter(id, entityPos)) {
                crowdCenter = crowdCenter + entityPos;
                entityCount++;
            }
        }
        
        if (entityCount > 0) {
            crowdCenter = crowdCenter / static_cast<float>(entityCount + 1); // +1 for self
        }
        
        // Calculate escape direction away from crowd
        Vector2D escapeDir = (currentPos - crowdCenter).normalized();
        
        // Add randomization based on entity ID and escape attempts to prevent sync
        float randomAngle = ((entity->getID() * 7 + stuckState.escapeAttempts * 3) % 180 - 90) * M_PI / 180.0f;
        float cosAngle = cosf(randomAngle);
        float sinAngle = sinf(randomAngle);
        Vector2D rotatedEscape(
            escapeDir.getX() * cosAngle - escapeDir.getY() * sinAngle,
            escapeDir.getX() * sinAngle + escapeDir.getY() * cosAngle
        );
        
        // Apply immediate escape velocity
        float escapeSpeed = 80.0f + stuckState.escapeAttempts * 20.0f; // Increase speed with attempts
        entity->setVelocity(rotatedEscape * escapeSpeed);
        
        stuckState.escapeAttempts++;
        stuckState.stuckStartTime = currentTime; // Reset timer for next escape attempt
        
        return true; // Applied escape behavior
    }
    
    return false; // No escape needed
}

} // namespace AIInternal

