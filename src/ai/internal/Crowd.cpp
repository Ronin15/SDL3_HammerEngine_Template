/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/internal/Crowd.hpp"
#include "managers/CollisionManager.hpp"
#include <algorithm>
#include <array>

namespace AIInternal {

// PERFORMANCE OPTIMIZATION: Spatial query cache to reduce CollisionManager load
// Caches queryArea results within the same frame to eliminate redundant spatial
// queries Key insight: Many nearby entities query the same spatial regions each
// frame
//
// MEMORY MANAGEMENT: Uses buffer reuse pattern to avoid per-frame allocations
// - Pre-allocated fixed-size array (no dynamic allocation per frame)
// - Marks entries as stale instead of clearing
// - Reuses vector capacity across frames (CLAUDE.md requirement)
struct SpatialQueryCache {
  struct CacheEntry {
    uint64_t frameNumber;
    uint64_t queryKey; // Store hash for fast validation (cheap integer compare)
    std::vector<EntityID> results;
  };

  uint64_t currentFrame = 0;
  static constexpr size_t CACHE_SIZE = 64;
  std::array<CacheEntry, CACHE_SIZE> entries; // Fixed-size, no heap allocations

  SpatialQueryCache() {
    // Pre-allocate capacity for all vectors to avoid per-frame reallocations
    for (auto &entry : entries) {
      entry.results.reserve(32); // Typical query returns ~10-30 entities
      entry.frameNumber = 0;
      entry.queryKey = 0;
    }
  }

  // Simple hash for position+radius (quantize to reduce unique keys)
  static uint64_t hashQuery(const Vector2D &center, float radius) {
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

  bool lookup(const Vector2D &center, float radius,
              std::vector<EntityID> &outResults) {
    uint64_t key = hashQuery(center, radius);
    size_t index = key % CACHE_SIZE;

    const CacheEntry &entry = entries[index];
    // Frame-based validation: entry is valid only if frame matches
    // No need to check 'valid' flag - frame comparison is sufficient
    if (entry.frameNumber == currentFrame && entry.queryKey == key) {
      outResults = entry.results;
      return true;
    }
    return false;
  }

  void store(const Vector2D &center, float radius,
             const std::vector<EntityID> &results) {
    uint64_t key = hashQuery(center, radius);
    size_t index = key % CACHE_SIZE;

    CacheEntry &entry = entries[index];
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

// Thread-local position buffer for GetNearbyEntitiesWithPositions callers
// Avoids per-call allocations when callers use GetNearbyPositionBuffer()
static thread_local std::vector<Vector2D> g_nearbyPositionBuffer;

int CountNearbyEntities(EntityID excludeId, const Vector2D &center,
                        float radius) {
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
  return std::count_if(
      queryResults.begin(), queryResults.end(), [excludeId, &cm](auto id) {
        return id != excludeId && (cm.isDynamic(id) || cm.isKinematic(id)) &&
               !cm.isTrigger(id);
      });
}

int GetNearbyEntitiesWithPositions(EntityID excludeId, const Vector2D &center,
                                   float radius,
                                   std::vector<Vector2D> &outPositions) {
  outPositions.clear();

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

  // Collect positions of actual entities (dynamic/kinematic, non-trigger,
  // excluding self)
  for (auto id : queryResults) {
    if (id != excludeId && (cm.isDynamic(id) || cm.isKinematic(id)) &&
        !cm.isTrigger(id)) {
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

std::vector<Vector2D> &GetNearbyPositionBuffer() { return g_nearbyPositionBuffer; }

} // namespace AIInternal
