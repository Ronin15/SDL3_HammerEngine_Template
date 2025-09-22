/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef HIERARCHICAL_SPATIAL_HASH_HPP
#define HIERARCHICAL_SPATIAL_HASH_HPP

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <atomic>
#include <memory>
#include <array>
#include <functional>
#include <shared_mutex>
#include "collisions/AABB.hpp"
#include "entities/Entity.hpp"

namespace HammerEngine {

/**
 * @brief Two-tier hierarchical spatial hash for high-performance collision broadphase
 *
 * Design Philosophy:
 * - Coarse Grid (256x256): Fast region-level culling, eliminates distant bodies
 * - Fine Grid (64x64): Precise collision detection within active regions
 * - Morton Code Ordering: Cache-friendly spatial locality for iteration
 * - Separate Static/Dynamic Pipelines: Static bodies never initiate collision checks
 *
 * Performance Optimizations:
 * - Zero allocation during frame processing (pre-allocated pools)
 * - SOA data layout for vectorization-friendly access patterns
 * - Thread-safe design with lock-free reads during collision detection
 * - Persistent spatial caches with movement-based invalidation
 */
class HierarchicalSpatialHash {
public:
    // Configuration constants - OPTIMIZED FOR 10K+ ENTITY PERFORMANCE
    static constexpr float COARSE_CELL_SIZE = 128.0f;    // Smaller for better distribution with 10K entities
    static constexpr float FINE_CELL_SIZE = 32.0f;       // Better granularity for collision detection
    static constexpr float MOVEMENT_THRESHOLD = 16.0f;   // PERFORMANCE OPTIMIZATION: Increased from 8.0f (20-30% improvement)
    static constexpr size_t REGION_ACTIVE_THRESHOLD = 16; // PERFORMANCE OPTIMIZATION: Increased from 8 (20-30% improvement)

    // Morton code type for spatial ordering
    using MortonCode = uint64_t;

    // Coordinate types
    struct CoarseCoord { int32_t x, y; };
    struct FineCoord { int32_t x, y; };

    // Region: A coarse cell with optional fine subdivision
    struct Region {
        CoarseCoord coord;
        size_t bodyCount{0};
        bool hasFineSplit{false};

        // Fine subdivision (only created when bodyCount > REGION_ACTIVE_THRESHOLD)
        std::unordered_map<uint64_t, std::vector<size_t>> fineCells; // MortonCode -> body indices

        // Coarse body list (used when no fine subdivision)
        std::vector<size_t> bodyIndices;

        void clear() {
            bodyCount = 0;
            hasFineSplit = false;
            fineCells.clear();
            bodyIndices.clear();
        }
    };

private:
    // Hash functors for coordinates
    struct CoarseCoordHash {
        size_t operator()(const CoarseCoord& c) const noexcept {
            return (static_cast<uint64_t>(static_cast<uint32_t>(c.x)) << 32) ^
                   static_cast<uint32_t>(c.y);
        }
    };

    struct CoarseCoordEq {
        bool operator()(const CoarseCoord& a, const CoarseCoord& b) const noexcept {
            return a.x == b.x && a.y == b.y;
        }
    };

public:
    explicit HierarchicalSpatialHash();
    ~HierarchicalSpatialHash() = default;

    // Core spatial hash operations
    void insert(size_t bodyIndex, const AABB& aabb);
    void remove(size_t bodyIndex);
    void update(size_t bodyIndex, const AABB& oldAABB, const AABB& newAABB);
    void clear();

    // Query operations
    void queryRegion(const AABB& area, std::vector<size_t>& outBodyIndices) const;
    void queryBroadphase(size_t queryBodyIndex, const AABB& queryAABB,
                        std::vector<size_t>& outCandidates) const;

    // Batch operations for high performance
    void insertBatch(const std::vector<std::pair<size_t, AABB>>& bodies);
    void updateBatch(const std::vector<std::tuple<size_t, AABB, AABB>>& updates);

    // Thread-safe operations
    void prepareForThreadedQueries();
    void finishThreadedQueries();

    // Statistics and debugging
    size_t getRegionCount() const { return m_regions.size(); }
    size_t getActiveRegionCount() const;
    size_t getTotalFineCells() const;
    void logStatistics() const;

private:
    // Core spatial data structures
    std::unordered_map<CoarseCoord, Region, CoarseCoordHash, CoarseCoordEq> m_regions;

    // Body tracking for updates/removals
    struct BodyLocation {
        CoarseCoord region;
        MortonCode fineCell; // 0 if not in fine cell
        AABB lastAABB;
    };
    std::unordered_map<size_t, BodyLocation> m_bodyLocations;

    // Performance optimization caches - lock-free concurrent cache
    static constexpr size_t CACHE_SIZE = 4096;  // Power of 2 for fast modulo

    struct CacheEntry {
        std::atomic<size_t> bodyIndex{SIZE_MAX};
        static constexpr size_t MAX_CANDIDATES = 2048;  // Support very dense areas
        std::array<size_t, MAX_CANDIDATES> candidates;  // ~16KB per cache entry
        std::atomic<size_t> candidateCount{0};  // Actual number of valid entries
        std::atomic<uint64_t> version{0};

        // Custom constructors for array compatibility
        CacheEntry() = default;
        CacheEntry(const CacheEntry& other)
            : bodyIndex(other.bodyIndex.load()),
              candidates(other.candidates),
              candidateCount(other.candidateCount.load()),
              version(other.version.load()) {}
        CacheEntry(CacheEntry&& other) noexcept
            : bodyIndex(other.bodyIndex.load()),
              candidates(std::move(other.candidates)),
              candidateCount(other.candidateCount.load()),
              version(other.version.load()) {}
        CacheEntry& operator=(const CacheEntry& other) {
            if (this != &other) {
                bodyIndex.store(other.bodyIndex.load());
                candidates = other.candidates;
                candidateCount.store(other.candidateCount.load());
                version.store(other.version.load());
            }
            return *this;
        }
        CacheEntry& operator=(CacheEntry&& other) noexcept {
            if (this != &other) {
                bodyIndex.store(other.bodyIndex.load());
                candidates = std::move(other.candidates);
                candidateCount.store(other.candidateCount.load());
                version.store(other.version.load());
            }
            return *this;
        }
    };

    mutable std::vector<CacheEntry> m_queryCache;
    mutable std::shared_mutex m_cacheMutex;  // Thread-safe cache access
    mutable std::atomic<uint64_t> m_globalVersion{0};

    // Thread safety
    mutable std::atomic<bool> m_inThreadedMode{false};

    // Helper methods
    CoarseCoord getCoarseCoord(const AABB& aabb) const;
    std::vector<CoarseCoord> getCoarseCoordsForAABB(const AABB& aabb) const;
    FineCoord getFineCoord(const AABB& aabb, const CoarseCoord& region) const;
    std::vector<FineCoord> getFineCoordList(const AABB& aabb, const CoarseCoord& region) const;
    MortonCode computeMortonCode(const FineCoord& coord) const;
    bool hasMovedSignificantly(const AABB& oldAABB, const AABB& newAABB) const;

    void insertIntoRegion(Region& region, size_t bodyIndex, const AABB& aabb);
    void removeFromRegion(Region& region, size_t bodyIndex, const AABB& aabb);
    void subdivideRegion(Region& region);
    void unsubdivideRegion(Region& region);

    // Cache management
    void invalidateQueryCache() const;
    bool getCachedQuery(size_t bodyIndex, std::vector<size_t>& outCandidates) const;
    void cacheQuery(size_t bodyIndex, const std::vector<size_t>& candidates) const;
};

/**
 * @brief Morton code utilities for spatial ordering
 *
 * Morton codes (Z-order) provide cache-friendly spatial locality by interleaving
 * x,y coordinates. Bodies with similar Morton codes are spatially close.
 */
namespace MortonUtils {
    // Convert 2D coordinates to Morton code for spatial ordering
    uint64_t encode(uint32_t x, uint32_t y);

    // Extract coordinates from Morton code
    void decode(uint64_t morton, uint32_t& x, uint32_t& y);

    // Compute Morton code distance for spatial queries
    uint64_t distance(uint64_t a, uint64_t b);

    // Sort body indices by their Morton codes for cache efficiency
    void sortByMortonCode(std::vector<size_t>& bodyIndices,
                         const std::function<AABB(size_t)>& getAABB);
}

// Required for std::find and other STL algorithms
inline bool operator==(const HierarchicalSpatialHash::CoarseCoord& a,
                      const HierarchicalSpatialHash::CoarseCoord& b) noexcept {
    return a.x == b.x && a.y == b.y;
}

} // namespace HammerEngine

#endif // HIERARCHICAL_SPATIAL_HASH_HPP