#include "PathCache.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cassert>

#include "../../managers/CollisionManager.hpp"
#include "../../core/Logger.hpp"

namespace AIInternal {

PathCache::PathCache()
{
    AI_INFO("PathCache initialized");
}

PathCache::~PathCache()
{
    shutdown();
}

std::optional<std::vector<Vector2D>> PathCache::findSimilarPath(const Vector2D& start, 
                                                               const Vector2D& goal, 
                                                               float tolerance)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return std::nullopt;
    }

    m_totalQueries.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    // Search through cached paths for similar routes
    for (auto& [pathKey, cachedPath] : m_cachedPaths) {
        if (!cachedPath.isValid) {
            continue;
        }
        
        if (isPathSimilar(cachedPath, start, goal, tolerance)) {
            // Update usage statistics
            updatePathUsage(pathKey, cachedPath);
            
            // Return adjusted path matching exact request
            auto adjustedPath = adjustPathToRequest(cachedPath.waypoints, start, goal);
            
            m_totalHits.fetch_add(1, std::memory_order_relaxed);
            
            // Cache hit (stats tracked in PathfindingScheduler)
            
            return adjustedPath;
        }
    }
    
    m_totalMisses.fetch_add(1, std::memory_order_relaxed);
    
    // Cache miss (stats tracked in PathfindingScheduler)
    return std::nullopt;
}

void PathCache::cachePath(const Vector2D& start, const Vector2D& goal, const std::vector<Vector2D>& path)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }
    
    if (path.empty()) {
        // Empty path not cached (tracked in eviction stats)
        return;
    }

    uint64_t currentTime = SDL_GetTicks();
    uint64_t pathKey = hashPath(start, goal);
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    // Check if we need to evict before adding new path
    if (m_cachedPaths.size() >= MAX_CACHED_PATHS) {
        evictLRU();
    }
    
    // Create new cached path entry
    CachedPath newPath(start, goal, path, currentTime);
    m_cachedPaths[pathKey] = std::move(newPath);
    m_lruQueue.push(pathKey);
    
    // Path successfully cached (stats tracked in PathfindingScheduler)
}

void PathCache::evictPathsInCrowdedAreas(const Vector2D& playerPos, 
                                        float congestionRadius, 
                                        int maxCongestion)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    std::vector<uint64_t> pathsToEvict;
    
    for (const auto& [pathKey, cachedPath] : m_cachedPaths) {
        if (!cachedPath.isValid) {
            continue;
        }
        
        // Check if path intersects with congested area around player
        if (pathIntersectsCongestion(cachedPath.waypoints, playerPos, congestionRadius, maxCongestion)) {
            pathsToEvict.push_back(pathKey);
        }
    }
    
    // Remove congested paths
    for (uint64_t pathKey : pathsToEvict) {
        m_cachedPaths.erase(pathKey);
        m_congestionEvictions.fetch_add(1, std::memory_order_relaxed);
    }
    
    if (!pathsToEvict.empty()) {
        // Congestion evictions completed (count tracked in stats)
    }
}

void PathCache::cleanup(uint64_t maxAgeMs, uint32_t minUseCount)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    uint64_t currentTime = SDL_GetTicks();
    
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    std::vector<uint64_t> pathsToRemove;
    
    for (const auto& [pathKey, cachedPath] : m_cachedPaths) {
        bool shouldRemove = false;
        
        // Remove if invalid
        if (!cachedPath.isValid) {
            shouldRemove = true;
        }
        // Remove if too old and not frequently used
        else if ((currentTime - cachedPath.creationTime) > maxAgeMs && 
                 cachedPath.useCount < minUseCount) {
            shouldRemove = true;
        }
        
        if (shouldRemove) {
            pathsToRemove.push_back(pathKey);
        }
    }
    
    // Remove expired paths
    for (uint64_t pathKey : pathsToRemove) {
        m_cachedPaths.erase(pathKey);
        m_evictedPaths.fetch_add(1, std::memory_order_relaxed);
    }
    
    if (!pathsToRemove.empty()) {
        // Cleanup completed (eviction count tracked in stats)
    }
}

PathCacheStats PathCache::getStats() const
{
    PathCacheStats stats;
    
    // Get atomic counters
    stats.totalQueries = m_totalQueries.load(std::memory_order_relaxed);
    stats.totalHits = m_totalHits.load(std::memory_order_relaxed);
    stats.totalMisses = m_totalMisses.load(std::memory_order_relaxed);
    stats.evictedPaths = m_evictedPaths.load(std::memory_order_relaxed);
    stats.congestionEvictions = m_congestionEvictions.load(std::memory_order_relaxed);
    
    // Get cache size with lock
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        stats.totalPaths = m_cachedPaths.size();
    }
    
    // Calculate hit rate
    stats.updateHitRate();
    
    return stats;
}

void PathCache::clear()
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    m_cachedPaths.clear();
    
    // Clear LRU queue by creating new empty one
    std::queue<uint64_t> emptyQueue;
    m_lruQueue.swap(emptyQueue);
    
    // Reset statistics
    m_totalQueries.store(0, std::memory_order_relaxed);
    m_totalHits.store(0, std::memory_order_relaxed);
    m_totalMisses.store(0, std::memory_order_relaxed);
    m_evictedPaths.store(0, std::memory_order_relaxed);
    m_congestionEvictions.store(0, std::memory_order_relaxed);
    
    AI_INFO("PathCache: Cleared all cached paths and reset statistics");
}

size_t PathCache::size() const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_cachedPaths.size();
}

void PathCache::shutdown()
{
    bool expected = false;
    if (m_isShutdown.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        AI_INFO("PathCache shutting down");
        
        // Clear all data
        clear();
        
        AI_INFO("PathCache shutdown complete");
    }
}

// Private helper methods

uint64_t PathCache::hashPath(const Vector2D& start, const Vector2D& goal) const
{
    // CRITICAL FIX: Align quantization with spatial tolerance (64px) to prevent cache misses
    // Old 32px quantization was too fine, causing cache misses for similar paths
    static constexpr float QUANTIZATION_SIZE = 64.0f;  // Match DEFAULT_SPATIAL_TOLERANCE
    
    // Use more robust quantization to handle clustered entities
    uint32_t startX = static_cast<uint32_t>(std::floor(start.getX() / QUANTIZATION_SIZE + 0.5f));
    uint32_t startY = static_cast<uint32_t>(std::floor(start.getY() / QUANTIZATION_SIZE + 0.5f));
    uint32_t goalX = static_cast<uint32_t>(std::floor(goal.getX() / QUANTIZATION_SIZE + 0.5f));
    uint32_t goalY = static_cast<uint32_t>(std::floor(goal.getY() / QUANTIZATION_SIZE + 0.5f));
    
    // Improved hash combining to reduce collisions in clustered scenarios
    // Use FNV-1a-like hash mixing for better distribution
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    
    hash ^= startX; hash *= 1099511628211ULL; // FNV prime
    hash ^= startY; hash *= 1099511628211ULL;
    hash ^= goalX; hash *= 1099511628211ULL;
    hash ^= goalY; hash *= 1099511628211ULL;
    
    return hash;
}

float PathCache::calculateDistance(const Vector2D& a, const Vector2D& b) const
{
    float dx = a.getX() - b.getX();
    float dy = a.getY() - b.getY();
    return std::sqrt(dx * dx + dy * dy);
}

bool PathCache::isPathSimilar(const CachedPath& cached, 
                             const Vector2D& requestStart, 
                             const Vector2D& requestGoal, 
                             float tolerance) const
{
    // Check if start and goal positions are within tolerance
    float startDistance = calculateDistance(cached.start, requestStart);
    float goalDistance = calculateDistance(cached.goal, requestGoal);
    
    return (startDistance <= tolerance) && (goalDistance <= tolerance);
}

std::vector<Vector2D> PathCache::adjustPathToRequest(const std::vector<Vector2D>& cachedPath,
                                                    const Vector2D& requestStart,
                                                    const Vector2D& requestGoal) const
{
    if (cachedPath.empty()) {
        return cachedPath;
    }
    
    std::vector<Vector2D> adjustedPath = cachedPath;
    
    // Adjust first waypoint to exact start position
    if (!adjustedPath.empty()) {
        adjustedPath[0] = requestStart;
    }
    
    // Adjust last waypoint to exact goal position
    if (adjustedPath.size() > 1) {
        adjustedPath[adjustedPath.size() - 1] = requestGoal;
    }
    
    return adjustedPath;
}

void PathCache::evictLRU()
{
    // Remove oldest entries until we're under the size limit
    while (m_cachedPaths.size() >= MAX_CACHED_PATHS && !m_lruQueue.empty()) {
        uint64_t oldestKey = m_lruQueue.front();
        m_lruQueue.pop();
        
        auto it = m_cachedPaths.find(oldestKey);
        if (it != m_cachedPaths.end()) {
            m_cachedPaths.erase(it);
            m_evictedPaths.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool PathCache::pathIntersectsCongestion(const std::vector<Vector2D>& path,
                                        const Vector2D& congestionCenter,
                                        float congestionRadius,
                                        int maxCongestion) const
{
    if (path.empty()) {
        return false;
    }
    
    try {
        auto& cm = CollisionManager::Instance();
        
        // Check congestion at several points along the path
        size_t checkPoints = std::min(path.size(), static_cast<size_t>(8)); // Max 8 check points
        size_t step = std::max(static_cast<size_t>(1), path.size() / checkPoints);
        
        for (size_t i = 0; i < path.size(); i += step) {
            const Vector2D& waypoint = path[i];
            
            // Skip waypoints too far from congestion center
            if (calculateDistance(waypoint, congestionCenter) > congestionRadius * 2.0f) {
                continue;
            }
            
            // Check congestion around this waypoint
            std::vector<EntityID> queryResults;
            HammerEngine::AABB area(waypoint.getX() - 64.0f, waypoint.getY() - 64.0f, 128.0f, 128.0f);
            cm.queryArea(area, queryResults);
            
            // Count dynamic/kinematic entities (potential obstacles)
            int congestion = 0;
            for (EntityID id : queryResults) {
                if ((cm.isDynamic(id) || cm.isKinematic(id)) && !cm.isTrigger(id)) {
                    congestion++;
                }
            }
            
            // If this section of path has high congestion, consider it blocked
            if (congestion >= maxCongestion) {
                return true;
            }
        }
        
        return false;
        
    } catch (const std::exception& e) {
        AI_ERROR("PathCache::pathIntersectsCongestion: Exception - " + std::string(e.what()));
        return false;
    }
}

void PathCache::updatePathUsage(uint64_t pathKey, CachedPath& path)
{
    // Update last used time and increment use count
    path.lastUsedTime = SDL_GetTicks();
    path.useCount++;
    
    // CRITICAL FIX: Don't keep adding to LRU queue - it causes unbounded growth
    // The LRU queue should only track insertion order, not every access
    // Usage frequency is tracked by useCount and lastUsedTime
}

} // namespace AIInternal