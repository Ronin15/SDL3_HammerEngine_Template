/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "PathfindingCompat.hpp"
#include "managers/WorldManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include <algorithm>
#include <unordered_map>
#include <mutex>

namespace AIInternal {

// Thread-safe entity path storage for compatibility
static std::unordered_map<EntityID, std::vector<Vector2D>> s_entityPaths;
static std::mutex s_pathsMutex;

Vector2D ClampToWorld(const Vector2D& position, float margin) {
    // Get world bounds from WorldManager
    auto& worldManager = WorldManager::Instance();
    float minX, minY, maxX, maxY;
    
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE_SIZE = 32.0f;
        float worldMinX = minX * TILE_SIZE;
        float worldMinY = minY * TILE_SIZE;  
        float worldMaxX = maxX * TILE_SIZE;
        float worldMaxY = maxY * TILE_SIZE;
        
        return Vector2D(
            std::clamp(position.getX(), worldMinX + margin, worldMaxX - margin),
            std::clamp(position.getY(), worldMinY + margin, worldMaxY - margin)
        );
    }
    
    // Fallback bounds if WorldManager fails
    return Vector2D(
        std::clamp(position.getX(), 0.0f + margin, 32000.0f - margin),
        std::clamp(position.getY(), 0.0f + margin, 32000.0f - margin)
    );
}

bool FollowPathStepWithPolicy(EntityPtr entity, const Vector2D& currentPos,
                             std::vector<Vector2D>& path, size_t& pathIndex,
                             float speed, float nodeRadius, float /* lateralBias */) {
    if (!entity || path.empty() || pathIndex >= path.size()) {
        return false;
    }
    
    Vector2D targetNode = path[pathIndex];
    Vector2D toNode = targetNode - currentPos;
    float distToNode = toNode.length();
    
    if (distToNode <= nodeRadius) {
        // Reached current node, advance to next
        pathIndex++;
        if (pathIndex >= path.size()) {
            // Path complete
            path.clear();
            pathIndex = 0;
            return false;
        }
        // Continue to next node
        targetNode = path[pathIndex];
        toNode = targetNode - currentPos;
        distToNode = toNode.length();
    }
    
    if (distToNode > 0.1f) {
        // Move toward current path node
        Vector2D direction = toNode / distToNode; // normalized
        entity->setVelocity(direction * speed);
        return true;
    }
    
    return false;
}

bool RefreshPathWithPolicyAsync(EntityPtr entity, const Vector2D& currentPos, const Vector2D& goalPos,
                               std::vector<Vector2D>& path, size_t& pathIndex, uint64_t& lastPathUpdate,
                               uint64_t& lastProgressTime, float& /* lastNodeDistance */,
                               const PathPolicy& policy, int priority) {
    if (!entity) {
        return false;
    }
    
    uint64_t currentTime = SDL_GetTicks();
    
    // Check if we need to refresh the path
    if (!path.empty() && (currentTime - lastPathUpdate) < policy.pathTTL) {
        return true; // Current path is still valid
    }
    
    // Convert priority to PathfinderManager priority
    AIInternal::PathPriority pathPriority = AIInternal::PathPriority::Normal;
    if (priority == 0) pathPriority = AIInternal::PathPriority::High;
    else if (priority == 2) pathPriority = AIInternal::PathPriority::Low;
    
    // Request new path from PathfinderManager
    auto& pathfinder = PathfinderManager::Instance();
    EntityID entityId = entity->getID();
    
    pathfinder.requestPath(entityId, currentPos, goalPos, pathPriority,
        [entityId, &path, &pathIndex, &lastPathUpdate](EntityID id, const std::vector<Vector2D>& newPath) {
            if (id == entityId) {
                std::lock_guard<std::mutex> lock(s_pathsMutex);
                s_entityPaths[entityId] = newPath;
                // Note: We can't safely update the reference parameters from this callback
                // as they may be stack variables that no longer exist
            }
        });
    
    // Check if we have a path ready
    {
        std::lock_guard<std::mutex> lock(s_pathsMutex);
        auto it = s_entityPaths.find(entityId);
        if (it != s_entityPaths.end() && !it->second.empty()) {
            path = it->second;
            pathIndex = 0;
            lastPathUpdate = currentTime;
            lastProgressTime = currentTime;
            s_entityPaths.erase(it); // Remove after using
            return true;
        }
    }
    
    // Path request initiated but not ready yet
    return false;
}

void ForceUnstickEntity(EntityPtr entity) {
    if (!entity) return;
    
    // Simple unstick mechanism: apply random velocity nudge
    EntityID entityId = entity->getID();
    float angle = ((entityId * 17) % 360) * M_PI / 180.0f;
    Vector2D nudgeVel(cosf(angle) * 50.0f, sinf(angle) * 50.0f);
    entity->setVelocity(nudgeVel);
    
    // Cancel any pending pathfinding requests
    PathfinderManager::Instance().cancelEntityRequests(entityId);
}

bool RefreshPathWithPolicy(EntityPtr entity, const Vector2D& currentPos, const Vector2D& goalPos,
                          std::vector<Vector2D>& path, size_t& pathIndex, uint64_t& lastPathUpdate,
                          uint64_t& lastProgressTime, float& /* lastNodeDistance */,
                          const PathPolicy& policy, int priority) {
    // This is a simplified synchronous version - just tries immediate pathfinding
    if (!entity) {
        return false;
    }
    
    uint64_t currentTime = SDL_GetTicks();
    
    // Check if we need to refresh the path
    if (!path.empty() && (currentTime - lastPathUpdate) < policy.pathTTL) {
        return true; // Current path is still valid
    }
    
    // Try immediate pathfinding
    auto& pathfinder = PathfinderManager::Instance();
    std::vector<Vector2D> newPath;
    auto result = pathfinder.findPathImmediate(currentPos, goalPos, newPath);
    
    if (result == HammerEngine::PathfindingResult::SUCCESS && !newPath.empty()) {
        path = newPath;
        pathIndex = 0;
        lastPathUpdate = currentTime;
        lastProgressTime = currentTime;
        return true;
    }
    
    return false;
}

WorldBounds GetWorldBoundsInPixels() {
    // Get world bounds from WorldManager
    auto& worldManager = WorldManager::Instance();
    
    float minX, minY, maxX, maxY;
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
        // Convert tile coordinates to pixel coordinates (assuming 32 pixel tiles)
        constexpr float TILE_SIZE = 32.0f;
        return WorldBounds(
            minX * TILE_SIZE,
            minY * TILE_SIZE,
            maxX * TILE_SIZE,
            maxY * TILE_SIZE
        );
    }
    
    // Return invalid bounds if world bounds not available
    return WorldBounds();
}

// Compatibility functions for legacy AIManager calls
namespace CompatAIManager {
    void RequestPathAsync(EntityPtr entity, const Vector2D& start, const Vector2D& goal, AIInternal::PathPriority priority) {
        if (!entity) return;
        auto& pathfinder = PathfinderManager::Instance();
        pathfinder.requestPath(entity->getID(), start, goal, priority, nullptr);
    }
    
    bool HasAsyncPath(EntityPtr entity) {
        if (!entity) return false;
        // Simplified heuristic - in real implementation, would track entity paths
        return false;
    }
    
    std::vector<Vector2D> GetAsyncPath(EntityPtr entity) {
        if (!entity) return std::vector<Vector2D>{};
        // Simplified - in real implementation, would return stored path
        return std::vector<Vector2D>{};
    }
}

} // namespace AIInternal