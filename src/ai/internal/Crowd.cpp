/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/internal/Crowd.hpp"
#include "managers/CollisionManager.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace AIInternal {

// PERFORMANCE OPTIMIZATION: Spatial query cache to reduce CollisionManager load
// Caches queryArea results within the same frame to eliminate redundant spatial queries
// Key insight: Many nearby entities query the same spatial regions each frame
//
// MEMORY MANAGEMENT: Uses buffer reuse pattern to avoid per-frame allocations
// - Pre-allocated fixed-size array (no dynamic allocation per frame)
// - Marks entries as stale instead of clearing
// - Reuses vector capacity across frames (CLAUDE.md requirement)
struct SpatialQueryCache {
  struct CacheEntry {
    uint64_t frameNumber;
    uint64_t queryKey;  // Store hash for fast validation (cheap integer compare)
    std::vector<EntityID> results;
  };

  uint64_t currentFrame = 0;
  static constexpr size_t CACHE_SIZE = 64;
  std::array<CacheEntry, CACHE_SIZE> entries; // Fixed-size, no heap allocations

  SpatialQueryCache() {
    // Pre-allocate capacity for all vectors to avoid per-frame reallocations
    for (auto& entry : entries) {
      entry.results.reserve(32); // Typical query returns ~10-30 entities
      entry.frameNumber = 0;
      entry.queryKey = 0;
    }
  }

  // Simple hash for position+radius (quantize to reduce unique keys)
  static uint64_t hashQuery(const Vector2D& center, float radius) {
    // Quantize position to 8-pixel grid to increase cache hits
    int32_t const qx = static_cast<int32_t>(center.getX() / 8.0f);
    int32_t const qy = static_cast<int32_t>(center.getY() / 8.0f);
    int32_t const qr = static_cast<int32_t>(radius / 8.0f);
    // Combine into hash
    uint64_t hash = static_cast<uint64_t>(qx);
    hash ^= (static_cast<uint64_t>(qy) << 16);
    hash ^= (static_cast<uint64_t>(qr) << 32);
    return hash;
  }

  bool lookup(const Vector2D& center, float radius, std::vector<EntityID>& outResults) {
    uint64_t key = hashQuery(center, radius);
    size_t index = key % CACHE_SIZE;

    const CacheEntry& entry = entries[index];
    // Frame-based validation: entry is valid only if frame matches
    // No need to check 'valid' flag - frame comparison is sufficient
    if (entry.frameNumber == currentFrame && entry.queryKey == key) {
      outResults = entry.results;
      return true;
    }
    return false;
  }

  void store(const Vector2D& center, float radius, const std::vector<EntityID>& results) {
    uint64_t key = hashQuery(center, radius);
    size_t index = key % CACHE_SIZE;

    CacheEntry& entry = entries[index];
    entry.frameNumber = currentFrame;
    entry.queryKey = key;
    entry.results = results; // Reuses existing capacity when possible
    // No need to set 'valid' flag - frameNumber is sufficient
  }

  void newFrame(uint64_t frameNumber) {
    // ZERO-COST INVALIDATION: Just update frame counter
    // Old entries auto-invalidate when frameNumber doesn't match
    // No loop, no writes, no cache thrashing across threads
    currentFrame = frameNumber;
  }
};

// Thread-local cache instance (one per worker thread)
static thread_local SpatialQueryCache g_spatialCache;

// PERFORMANCE: Consolidated core separation logic used by both overloads
// This eliminates code duplication and ensures consistent behavior
static Vector2D ComputeSeparationForce(
    const Vector2D &currentPos,
    const Vector2D &intendedVel,
    float speed,
    float baseRadius,
    float queryRadius,
    float strength,
    size_t maxNeighbors,
    const std::vector<Vector2D> &neighborPositions) {

  Vector2D sep(0, 0);
  Vector2D avoidance(0, 0);
  float closest = queryRadius;
  size_t counted = 0;
  size_t criticalNeighbors = 0;

  // Process neighbor positions
  for (const Vector2D &other : neighborPositions) {
    Vector2D d = currentPos - other;

    // OPTIMIZATION: Manhattan distance fast-reject before expensive sqrt calculation
    float manhattanDist = std::abs(d.getX()) + std::abs(d.getY());
    if (manhattanDist > queryRadius * 1.5f) continue;

    float dist = d.length();

    if (dist < 0.5f) {
      // Handle extreme overlap with emergency push
      d = Vector2D((rand() % 200 - 100) / 100.0f, (rand() % 200 - 100) / 100.0f);
      dist = 16.0f;
      criticalNeighbors++;
    }
    if (dist > queryRadius) continue;

    closest = std::min(closest, dist);

    // Multi-layer separation with world-scale awareness
    Vector2D dir = d * (1.0f / dist);

    if (dist < baseRadius * 0.5f) {
      // Critical range - strong repulsion
      float criticalWeight = (baseRadius * 0.5f - dist) / (baseRadius * 0.5f);
      avoidance = avoidance + dir * (criticalWeight * criticalWeight * 3.0f);
      criticalNeighbors++;
    } else if (dist < baseRadius) {
      // Normal separation range
      float normalWeight = (baseRadius - dist) / baseRadius;
      sep = sep + dir * normalWeight;
    }

    // Limit neighbor processing for performance
    if (++counted >= std::min(maxNeighbors, static_cast<size_t>(6))) break;
  }

  Vector2D out = intendedVel;
  float const il = out.length();

  if ((sep.length() > 0.001f || avoidance.length() > 0.001f) && il > 0.001f) {
    Vector2D const intendedDir = out * (1.0f / il);

    // Emergency avoidance with pathfinding preservation
    if (criticalNeighbors > 0 && avoidance.length() > 0.001f) {
      Vector2D const avoidDir = avoidance.normalized();
      Vector2D perpendicular(-intendedDir.getY(), intendedDir.getX());

      if (avoidDir.dot(perpendicular) < 0) {
        perpendicular = Vector2D(intendedDir.getY(), -intendedDir.getX());
      }

      Vector2D const emergencyVel = intendedDir * 0.6f + perpendicular * 0.8f;
      float const emLen = emergencyVel.length();
      if (emLen > 0.01f) {
        out = emergencyVel * (speed / emLen);
      }
    } else {
      // Pathfinding-aware separation
      float adaptiveStrength = strength;

      if (counted >= maxNeighbors) {
        adaptiveStrength = std::min(adaptiveStrength * 1.5f, 0.6f);
      }
      if (closest < baseRadius * 0.7f) {
        adaptiveStrength = std::min(adaptiveStrength * 1.3f, 0.5f);
      }

      if (sep.length() > 0.001f) {
        Vector2D const sepDir = sep.normalized();
        float const directionConflict = -sepDir.dot(intendedDir);

        if (directionConflict > 0.7f) {
          // High conflict: lateral redirection
          Vector2D lateral(-intendedDir.getY(), intendedDir.getX());
          if (sepDir.dot(lateral) < 0) {
            lateral = Vector2D(intendedDir.getY(), -intendedDir.getX());
          }

          Vector2D const redirected = intendedDir * 0.85f + lateral * adaptiveStrength * 1.2f;
          float const redirLen = redirected.length();
          if (redirLen > 0.01f) {
            out = redirected * (speed / redirLen);
          }
        } else {
          // Low conflict: gentle separation with forward bias
          Vector2D const forwardBias = out * (1.0f - adaptiveStrength * 0.35f);
          Vector2D const separationForce = sep * adaptiveStrength * speed * 0.5f;
          Vector2D const blended = forwardBias + separationForce;

          float const bl = blended.length();
          if (bl > 0.01f) {
            out = blended * (speed / bl);
          }
        }
      }
    }
  }

  return out;
}

Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors) {
  if (!entity || speed <= 0.0f) return intendedVel;

  const auto &cm = CollisionManager::Instance();

  // Use thread-local vector to avoid repeated allocations
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();

  // Much larger query area for world-scale separation
  // Scale separation radius based on speed for dynamic behavior
  // Tighter, capped query radius to reduce broad-phase load
  // Tight separation window to cut cost and preserve forward motion
  float baseRadius = std::max(radius, 24.0f);
  float speedMultiplier = std::clamp(speed / 120.0f, 1.0f, 1.5f);
  float queryRadius = std::min(baseRadius * speedMultiplier, 96.0f);

  // PERFORMANCE: Check spatial cache before expensive queryArea call
  if (!g_spatialCache.lookup(currentPos, queryRadius, queryResults)) {
    // Cache miss - perform actual collision query
    HammerEngine::AABB area(currentPos.getX() - queryRadius, currentPos.getY() - queryRadius,
                            queryRadius * 2.0f, queryRadius * 2.0f);
    cm.queryArea(area, queryResults);

    // Store result in cache for subsequent queries in same frame
    g_spatialCache.store(currentPos, queryRadius, queryResults);
  }

  // Extract positions from query results
  static thread_local std::vector<Vector2D> neighborPositions;
  neighborPositions.clear();

  for (EntityID id : queryResults) {
    if (id == entity->getID()) continue;
    if ((!cm.isDynamic(id) && !cm.isKinematic(id)) || cm.isTrigger(id)) continue;
    Vector2D other;
    if (cm.getBodyCenter(id, other)) {
      neighborPositions.push_back(other);
    }
  }

  // Use consolidated separation logic
  return ComputeSeparationForce(currentPos, intendedVel, speed, baseRadius,
                                queryRadius, strength, maxNeighbors, neighborPositions);
}

// PERFORMANCE OPTIMIZATION: ApplySeparation with pre-fetched neighbor data
// This overload skips the expensive collision query when neighbor positions
// are already available from a previous query (e.g., crowd analysis)
Vector2D ApplySeparation(EntityPtr entity,
                         const Vector2D &currentPos,
                         const Vector2D &intendedVel,
                         float speed,
                         float radius,
                         float strength,
                         size_t maxNeighbors,
                         const std::vector<Vector2D> &preFetchedNeighbors) {
  if (!entity || speed <= 0.0f) return intendedVel;

  // Calculate parameters and delegate to consolidated logic
  float baseRadius = std::max(radius, 24.0f);
  float speedMultiplier = std::clamp(speed / 120.0f, 1.0f, 1.5f);
  float queryRadius = std::min(baseRadius * speedMultiplier, 96.0f);

  // Use consolidated separation logic with pre-fetched neighbors
  return ComputeSeparationForce(currentPos, intendedVel, speed, baseRadius,
                                queryRadius, strength, maxNeighbors, preFetchedNeighbors);
}

// LOCK-FREE VARIANT: ApplySeparation without EntityPtr
// Uses EntityID directly for self-exclusion - no mutex acquisition needed.
// This is the hot path for BehaviorContext-based behavior execution.
Vector2D ApplySeparationDirect(EntityID entityId,
                               const Vector2D &currentPos,
                               const Vector2D &intendedVel,
                               float speed,
                               float radius,
                               float strength,
                               size_t maxNeighbors) {
  if (speed <= 0.0f) return intendedVel;

  const auto &cm = CollisionManager::Instance();

  // Use thread-local vector to avoid repeated allocations
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();

  // Calculate query parameters (same as ApplySeparation)
  float baseRadius = std::max(radius, 24.0f);
  float speedMultiplier = std::clamp(speed / 120.0f, 1.0f, 1.5f);
  float queryRadius = std::min(baseRadius * speedMultiplier, 96.0f);

  // PERFORMANCE: Check spatial cache before expensive queryArea call
  if (!g_spatialCache.lookup(currentPos, queryRadius, queryResults)) {
    // Cache miss - perform actual collision query
    HammerEngine::AABB area(currentPos.getX() - queryRadius, currentPos.getY() - queryRadius,
                            queryRadius * 2.0f, queryRadius * 2.0f);
    cm.queryArea(area, queryResults);

    // Store result in cache for subsequent queries in same frame
    g_spatialCache.store(currentPos, queryRadius, queryResults);
  }

  // Extract positions from query results (using EntityID for self-exclusion)
  static thread_local std::vector<Vector2D> neighborPositions;
  neighborPositions.clear();

  for (EntityID id : queryResults) {
    if (id == entityId) continue;  // Self-exclusion using EntityID (no EntityPtr needed)
    if ((!cm.isDynamic(id) && !cm.isKinematic(id)) || cm.isTrigger(id)) continue;
    Vector2D other;
    if (cm.getBodyCenter(id, other)) {
      neighborPositions.push_back(other);
    }
  }

  // Use consolidated separation logic
  return ComputeSeparationForce(currentPos, intendedVel, speed, baseRadius,
                                queryRadius, strength, maxNeighbors, neighborPositions);
}

Vector2D SmoothVelocityTransition(const Vector2D &currentVel,
                                  const Vector2D &targetVel,
                                  float smoothingFactor,
                                  float maxChange) {
  Vector2D deltaVel = targetVel - currentVel;
  float const deltaLen = deltaVel.length();
  
  if (deltaLen <= 0.01f) {
    return currentVel; // No significant change needed
  }
  
  // Limit maximum change per frame to prevent jitter
  if (deltaLen > maxChange) {
    deltaVel = deltaVel * (maxChange / deltaLen);
  }
  
  // Apply smoothing - smaller factor = more smoothing
  Vector2D const smoothedDelta = deltaVel * smoothingFactor;
  Vector2D result = currentVel + smoothedDelta;
  
  // Ensure the result maintains reasonable bounds
  float const resultLen = result.length();
  if (resultLen > 200.0f) { // Cap maximum velocity
    result = result * (200.0f / resultLen);
  }
  
  return result;
}

int CountNearbyEntities(EntityPtr entity, const Vector2D &center, float radius) {
  if (!entity) return 0;
  
  const auto &cm = CollisionManager::Instance();
  
  // Use thread-local vector to avoid repeated allocations
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();

  // PERFORMANCE: Check spatial cache before expensive queryArea call
  if (!g_spatialCache.lookup(center, radius, queryResults)) {
    // Cache miss - perform actual collision query
    HammerEngine::AABB area(center.getX() - radius, center.getY() - radius,
                            radius * 2.0f, radius * 2.0f);
    cm.queryArea(area, queryResults);

    // Store result in cache for subsequent queries in same frame
    g_spatialCache.store(center, radius, queryResults);
  }

  // Count only actual entities (dynamic/kinematic, non-trigger, excluding self)
  return std::count_if(queryResults.begin(), queryResults.end(),
                       [entity, &cm](auto id) {
                         return id != entity->getID() && (cm.isDynamic(id) || cm.isKinematic(id)) && !cm.isTrigger(id);
                       });
}

int GetNearbyEntitiesWithPositions(EntityPtr entity, const Vector2D &center, float radius, 
                                   std::vector<Vector2D> &outPositions) {
  outPositions.clear();
  if (!entity) return 0;
  
  const auto &cm = CollisionManager::Instance();

  // Use thread-local vector to avoid repeated allocations
  static thread_local std::vector<EntityID> queryResults;
  queryResults.clear();

  // PERFORMANCE: Check spatial cache before expensive queryArea call
  if (!g_spatialCache.lookup(center, radius, queryResults)) {
    // Cache miss - perform actual collision query
    HammerEngine::AABB area(center.getX() - radius, center.getY() - radius,
                            radius * 2.0f, radius * 2.0f);
    cm.queryArea(area, queryResults);

    // Store result in cache for subsequent queries in same frame
    g_spatialCache.store(center, radius, queryResults);
  }

  // Collect positions of actual entities (dynamic/kinematic, non-trigger, excluding self)
  for (auto id : queryResults) {
    if (id != entity->getID() && (cm.isDynamic(id) || cm.isKinematic(id)) && !cm.isTrigger(id)) {
      Vector2D entityPos;
      if (cm.getBodyCenter(id, entityPos)) {
        outPositions.push_back(entityPos);
      }
    }
  }
  
  return static_cast<int>(outPositions.size());
}

void InvalidateSpatialCache(uint64_t frameNumber) {
  g_spatialCache.newFrame(frameNumber);
}

} // namespace AIInternal
