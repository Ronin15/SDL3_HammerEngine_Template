/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_PATHFINDING_COMPAT_HPP
#define AI_PATHFINDING_COMPAT_HPP

/**
 * @file PathfindingCompat.hpp
 * @brief Temporary compatibility layer for legacy pathfinding functions
 * 
 * This file provides minimal compatibility functions to support existing AI behaviors
 * during the transition to PathfinderManager. This is a temporary solution and will
 * be removed once all behaviors are properly refactored.
 */

#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/PathfindingScheduler.hpp"
#include <vector>
#include <cstdint>

namespace AIInternal {

/**
 * @brief Simple pathfinding policy structure for compatibility
 */
struct PathPolicy {
    uint64_t pathTTL = 2000;           // Path time-to-live in ms
    uint64_t noProgressWindow = 1500;  // No progress detection window in ms
    float nodeRadius = 32.0f;          // Node radius for pathfinding
    bool allowDetours = true;          // Allow detours around obstacles
    float detourRadii[2] = {64.0f, 128.0f}; // Detour search radii
    float lateralBias = 0.0f;          // Lateral movement bias
};

/**
 * @brief Compatibility function for clamping positions to world bounds
 * @param position Position to clamp
 * @param margin Safety margin from world edges
 * @return Clamped position within world bounds
 */
Vector2D ClampToWorld(const Vector2D& position, float margin = 100.0f);

/**
 * @brief Compatibility function for following a path step
 * @param entity Entity to move
 * @param currentPos Current entity position
 * @param path Path to follow
 * @param pathIndex Current path index (will be modified)
 * @param speed Movement speed
 * @param nodeRadius Radius for reaching path nodes
 * @param lateralBias Lateral movement bias (unused in compatibility mode)
 * @return true if successfully following path, false otherwise
 */
bool FollowPathStepWithPolicy(EntityPtr entity, const Vector2D& currentPos,
                             std::vector<Vector2D>& path, size_t& pathIndex,
                             float speed, float nodeRadius, float lateralBias = 0.0f);

/**
 * @brief Compatibility function for async path refresh
 * @param entity Entity requesting path
 * @param currentPos Current entity position  
 * @param goalPos Goal position for pathfinding
 * @param path Path storage (will be updated when path is found)
 * @param pathIndex Path index storage
 * @param lastPathUpdate Last path update time storage
 * @param lastProgressTime Last progress time storage
 * @param lastNodeDistance Last node distance storage (unused)
 * @param policy Pathfinding policy
 * @param priority Path priority (0=high, 1=normal, 2=low)
 * @return true if path request was initiated, false otherwise
 */
bool RefreshPathWithPolicyAsync(EntityPtr entity, const Vector2D& currentPos, const Vector2D& goalPos,
                               std::vector<Vector2D>& path, size_t& pathIndex, uint64_t& lastPathUpdate,
                               uint64_t& lastProgressTime, float& lastNodeDistance,
                               const PathPolicy& policy, int priority = 1);

/**
 * @brief Simple entity unstick mechanism for compatibility
 * @param entity Entity to unstick
 */
void ForceUnstickEntity(EntityPtr entity);

/**
 * @brief Compatibility function for simple path refresh (synchronous version)
 * @param entity Entity requesting path
 * @param currentPos Current entity position  
 * @param goalPos Goal position for pathfinding
 * @param path Path storage (will be updated when path is found)
 * @param pathIndex Path index storage
 * @param lastPathUpdate Last path update time storage
 * @param lastProgressTime Last progress time storage
 * @param lastNodeDistance Last node distance storage (unused)
 * @param policy Pathfinding policy
 * @param priority Path priority (0=high, 1=normal, 2=low)
 * @return true if path was updated, false otherwise
 */
bool RefreshPathWithPolicy(EntityPtr entity, const Vector2D& currentPos, const Vector2D& goalPos,
                          std::vector<Vector2D>& path, size_t& pathIndex, uint64_t& lastPathUpdate,
                          uint64_t& lastProgressTime, float& lastNodeDistance,
                          const PathPolicy& policy, int priority = 1);

/**
 * @brief World bounds structure for collision system compatibility
 */
struct WorldBounds {
    bool valid;
    float minX;
    float minY;
    float maxX;
    float maxY;
    
    WorldBounds() : valid(false), minX(0), minY(0), maxX(0), maxY(0) {}
    WorldBounds(float minX_, float minY_, float maxX_, float maxY_) 
        : valid(true), minX(minX_), minY(minY_), maxX(maxX_), maxY(maxY_) {}
};

/**
 * @brief Get world bounds in pixel coordinates for collision system
 * @return WorldBounds structure with world dimensions
 */
WorldBounds GetWorldBoundsInPixels();

/**
 * @brief Compatibility functions for legacy AIManager pathfinding calls
 */
namespace CompatAIManager {
    void RequestPathAsync(EntityPtr entity, const Vector2D& start, const Vector2D& goal, AIInternal::PathPriority priority);
    bool HasAsyncPath(EntityPtr entity);
    std::vector<Vector2D> GetAsyncPath(EntityPtr entity);
}

} // namespace AIInternal

#endif // AI_PATHFINDING_COMPAT_HPP