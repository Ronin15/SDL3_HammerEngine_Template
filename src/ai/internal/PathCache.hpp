#ifndef PATH_CACHE_HPP
#define PATH_CACHE_HPP

#include <vector>
#include <unordered_map>
#include <queue>
#include <optional>
#include <atomic>
#include <mutex>
#include <cstdint>

#include "../../utils/Vector2D.hpp"
#include "../../entities/Entity.hpp" // For EntityID type

// Forward declarations to minimize dependencies
class CollisionManager;

namespace AIInternal {

/**
 * Cached path segment with metadata for efficient lookup and reuse.
 * Stores successful pathfinding results that can be shared between similar requests.
 */
struct CachedPath {
    Vector2D start;
    Vector2D goal;
    std::vector<Vector2D> waypoints;
    uint64_t creationTime;
    uint64_t lastUsedTime;
    uint32_t useCount;
    bool isValid;
    
    CachedPath() : creationTime(0), lastUsedTime(0), useCount(0), isValid(false) {}
    
    CachedPath(const Vector2D& s, const Vector2D& g, const std::vector<Vector2D>& path, uint64_t time)
        : start(s), goal(g), waypoints(path), creationTime(time), 
          lastUsedTime(time), useCount(1), isValid(!path.empty()) {}
};

/**
 * Statistics for monitoring PathCache performance and hit rate.
 * Used by PathfindingScheduler to report cache effectiveness.
 */
struct PathCacheStats {
    size_t totalPaths = 0;
    size_t totalQueries = 0;
    size_t totalHits = 0;
    size_t totalMisses = 0;
    size_t evictedPaths = 0;
    size_t congestionEvictions = 0;
    float hitRate = 0.0f;
    
    void updateHitRate() {
        hitRate = (totalQueries > 0) ? (static_cast<float>(totalHits) / static_cast<float>(totalQueries)) : 0.0f;
    }
};

/**
 * PathCache - High-performance path caching system for pathfinding optimization.
 * 
 * Provides LRU-based caching of successful pathfinding results with spatial tolerance matching.
 * Integrates with CollisionManager for congestion-aware cache eviction to maintain cache relevance.
 * Designed to reduce pathfinding timeout rates by avoiding redundant computations.
 * 
 * Key Features:
 * - Spatial tolerance matching (default 64px) for similar path reuse
 * - LRU eviction with configurable cache size limits
 * - CollisionManager integration for detecting congested areas
 * - Thread-safe access patterns for concurrent pathfinding
 * - Performance statistics for monitoring effectiveness
 * 
 * Thread Safety:
 * - All public methods are thread-safe using mutex protection
 * - Lock-free atomic counters for statistics
 * - Designed for concurrent access from PathfindingScheduler
 */
class PathCache {
public:
    PathCache();
    ~PathCache();

    /**
     * Search for a cached path similar to the requested start/goal within tolerance.
     * Returns cached path if found, adjusting first/last waypoints to match exact request.
     * 
     * @param start Starting position for pathfinding request
     * @param goal Target position for pathfinding request  
     * @param tolerance Maximum distance for similarity matching (default 64px)
     * @return Optional cached path if similar route found, nullopt otherwise
     */
    std::optional<std::vector<Vector2D>> findSimilarPath(const Vector2D& start, 
                                                        const Vector2D& goal, 
                                                        float tolerance = 64.0f);

    /**
     * Check if a failed (negative) path result is cached for similar start/goal.
     * When true, callers may skip expensive searches and treat as no-path-found.
     */
    bool hasNegativeCached(const Vector2D& start,
                           const Vector2D& goal,
                           float tolerance = 64.0f);

    /**
     * Cache a successful pathfinding result for future reuse.
     * Uses LRU eviction if cache exceeds maximum size limit.
     * 
     * @param start Starting position of computed path
     * @param goal Target position of computed path
     * @param path Vector of waypoints forming the computed path
     */
    void cachePath(const Vector2D& start, const Vector2D& goal, const std::vector<Vector2D>& path);

    /**
     * Cache a negative result (no path found / invalid) for short-term suppression of retries.
     * Stored with isValid=false and cleared by normal cleanup policies.
     */
    void cacheNegative(const Vector2D& start, const Vector2D& goal);

    /**
     * Remove cached paths that pass through high-congestion areas.
     * Uses CollisionManager to detect crowded regions around player position.
     * Proactively invalidates paths likely to become blocked.
     * 
     * @param playerPos Current player position for congestion analysis
     * @param congestionRadius Radius around player to check for crowding (default 400px)
     * @param maxCongestion Maximum entity count before path eviction (default 8)
     */
    void evictPathsInCrowdedAreas(const Vector2D& playerPos, 
                                 float congestionRadius = 400.0f, 
                                 int maxCongestion = 8);

    /**
     * Cleanup expired cached paths based on age and usage patterns.
     * Called periodically to maintain cache relevance and prevent memory growth.
     * 
     * @param maxAgeMs Maximum age for cached paths in milliseconds (default 30000ms)
     * @param minUseCount Minimum usage count to preserve old paths (default 2)
     */
    void cleanup(uint64_t maxAgeMs = 30000, uint32_t minUseCount = 2);

    /**
     * Get current cache statistics for performance monitoring.
     * Thread-safe access to hit rates, cache size, eviction counts.
     * 
     * @return PathCacheStats structure with current metrics
     */
    PathCacheStats getStats() const;

    /**
     * Clear all cached paths and reset statistics.
     * Used for testing and debugging scenarios.
     */
    void clear();

    /**
     * Get current number of cached paths.
     * Thread-safe accessor for cache size monitoring.
     * 
     * @return Current cache size
     */
    size_t size() const;

    /**
     * Engine shutdown method following established patterns.
     * Clears all cached data and marks instance as shut down.
     */
    void shutdown();

private:
    // Cache storage and management
    std::unordered_map<uint64_t, CachedPath> m_cachedPaths;
    std::queue<uint64_t> m_lruQueue; // For LRU eviction order
    mutable std::mutex m_cacheMutex;
    
    // Configuration constants
    static constexpr size_t MAX_CACHED_PATHS = 1024;
    static constexpr float DEFAULT_SPATIAL_TOLERANCE = 64.0f;
    static constexpr uint64_t DEFAULT_MAX_AGE_MS = 30000; // 30 seconds
    static constexpr uint32_t DEFAULT_MIN_USE_COUNT = 2;
    
    // Performance statistics (atomic for lock-free access)
    mutable std::atomic<size_t> m_totalQueries{0};
    mutable std::atomic<size_t> m_totalHits{0};
    mutable std::atomic<size_t> m_totalMisses{0};
    mutable std::atomic<size_t> m_evictedPaths{0};
    mutable std::atomic<size_t> m_congestionEvictions{0};
    
    // Shutdown guard following engine patterns
    std::atomic<bool> m_isShutdown{false};
    
    // Internal helper methods
    
    /**
     * Generate hash key for path lookup based on start and goal positions.
     * Uses spatial quantization to group nearby requests.
     */
    uint64_t hashPath(const Vector2D& start, const Vector2D& goal) const;
    
    /**
     * Calculate distance between two 2D points.
     * Used for spatial tolerance matching.
     */
    float calculateDistanceSquared(const Vector2D& a, const Vector2D& b) const;
    
    /**
     * Check if two paths are similar within spatial tolerance.
     * Compares start/goal positions and validates path compatibility.
     */
    bool isPathSimilar(const CachedPath& cached, 
                      const Vector2D& requestStart, 
                      const Vector2D& requestGoal, 
                      float tolerance) const;
    
    /**
     * Adjust cached path waypoints to match exact start/goal positions.
     * Modifies first and last waypoints while preserving path shape.
     */
    std::vector<Vector2D> adjustPathToRequest(const std::vector<Vector2D>& cachedPath,
                                            const Vector2D& requestStart,
                                            const Vector2D& requestGoal) const;
    
    /**
     * Perform LRU eviction when cache exceeds size limits.
     * Removes oldest least-recently-used paths.
     */
    void evictLRU();
    
    /**
     * Check if a path intersects with a high-congestion area.
     * Uses CollisionManager for spatial queries.
     */
    bool pathIntersectsCongestion(const std::vector<Vector2D>& path,
                                 const Vector2D& congestionCenter,
                                 float congestionRadius,
                                 int maxCongestion) const;
    
    /**
     * Update path usage statistics and LRU ordering.
     * Called when cached path is successfully reused.
     */
    void updatePathUsage(uint64_t pathKey, CachedPath& path);
    
    // Prevent copying
    PathCache(const PathCache&) = delete;
    PathCache& operator=(const PathCache&) = delete;
};

} // namespace AIInternal

#endif // PATH_CACHE_HPP
