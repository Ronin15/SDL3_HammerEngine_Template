#include "PathfindingScheduler.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>

#include "../../core/ThreadSystem.hpp"
#include "../../managers/CollisionManager.hpp"
#include "../../managers/AIManager.hpp"
#include "../pathfinding/PathfindingGrid.hpp"
#include "../../core/Logger.hpp"
#include "PathCache.hpp"

namespace AIInternal {

PathfindingScheduler::PathfindingScheduler()
    : m_lastPlayerPos(0.0f, 0.0f)
    , m_pathCache(std::make_unique<PathCache>())
{
    AI_INFO("PathfindingScheduler initialized with PathCache");
}

PathfindingScheduler::~PathfindingScheduler()
{
    shutdown();
}

void PathfindingScheduler::requestPath(EntityID entityId, const Vector2D& start, const Vector2D& goal,
                                      PathPriority priority, 
                                      std::function<void(EntityID, const std::vector<Vector2D>&)> callback)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        AI_WARN("PathfindingScheduler::requestPath called after shutdown");
        if (callback) {
            callback(entityId, std::vector<Vector2D>{}); // Empty path on failure
        }
        return;
    }

    uint64_t currentTime = SDL_GetTicks();
    
    // First, check PathCache for similar paths
    if (auto cachedPath = m_pathCache->findSimilarPath(start, goal, 64.0f)) {
        // Store cached result and call callback immediately
        storePathResult(entityId, cachedPath.value());
        if (callback) {
            callback(entityId, cachedPath.value());
        }
        // Track cache hit
        m_pathsFromCache.fetch_add(1, std::memory_order_relaxed);
        m_totalRequestsProcessed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    
    // Check if we already have a recent path for this entity
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        auto it = m_pathResults.find(entityId);
        if (it != m_pathResults.end() && 
            (currentTime - it->second.computeTime) < 500 && // 500ms cache
            it->second.isValid) 
        {
            // Return cached path immediately
            if (callback) {
                callback(entityId, it->second.path);
            }
            return;
        }
    }

    // Create new request with adjusted priority based on distance to player
    PathRequest request(entityId, start, goal, priority, callback);
    request.requestTime = currentTime;
    
    // Adjust priority based on distance to player if we have player position
    request.priority = adjustPriorityByDistance(request, m_lastPlayerPos);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_requestQueue.push(request);
    }

    // Pathfinding request queued (stats tracked in periodic summary)
}

bool PathfindingScheduler::hasPath(EntityID entityId) const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_resultsMutex);
    auto it = m_pathResults.find(entityId);
    return it != m_pathResults.end() && it->second.isValid;
}

std::vector<Vector2D> PathfindingScheduler::getPath(EntityID entityId) const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return std::vector<Vector2D>{};
    }

    std::lock_guard<std::mutex> lock(m_resultsMutex);
    auto it = m_pathResults.find(entityId);
    if (it != m_pathResults.end() && it->second.isValid) {
        return it->second.path;
    }
    return std::vector<Vector2D>{};
}

void PathfindingScheduler::clearPath(EntityID entityId)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_resultsMutex);
    m_pathResults.erase(entityId);
}

void PathfindingScheduler::update(float /*deltaTime*/, const Vector2D& playerPos)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    m_lastPlayerPos = playerPos;
    m_requestsThisFrame.store(0, std::memory_order_relaxed);
    
    // Clean up expired requests and results
    cleanupExpiredRequests();
    
    // Perform PathCache maintenance
    if (m_pathCache) {
        // Evict paths in congested areas around player
        m_pathCache->evictPathsInCrowdedAreas(playerPos);
        
        // Periodic cleanup of expired paths
        static uint64_t lastCleanupTime = 0;
        uint64_t currentTime = SDL_GetTicks();
        if ((currentTime - lastCleanupTime) > 5000) { // Cleanup every 5 seconds
            m_pathCache->cleanup();
            lastCleanupTime = currentTime;
        }
    }
    
    // Process batch of requests if we have any
    processRequestBatch();
}

size_t PathfindingScheduler::getQueueSize() const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_requestQueue.size();
}

size_t PathfindingScheduler::getActiveRequestCount() const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_resultsMutex);
    return m_pathResults.size();
}

void PathfindingScheduler::shutdown()
{
    bool expected = false;
    if (m_isShutdown.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        AI_INFO("PathfindingScheduler shutting down");
        
        // Shutdown PathCache first
        if (m_pathCache) {
            m_pathCache->shutdown();
        }
        
        // Clear all queues and results
        {
            std::lock_guard<std::mutex> queueLock(m_queueMutex);
            // Clear priority queue by creating new empty one
            std::priority_queue<PathRequest> empty;
            m_requestQueue.swap(empty);
        }
        
        {
            std::lock_guard<std::mutex> resultsLock(m_resultsMutex);
            m_pathResults.clear();
        }
        
        AI_INFO("PathfindingScheduler shutdown complete");
    }
}

void PathfindingScheduler::processRequestBatch()
{
    // This method now just manages the request queue
    // Actual processing is done by AIManager calling extractPendingRequests()
    
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    // Clean up expired requests
    cleanupExpiredRequests();
}

std::vector<PathRequest> PathfindingScheduler::extractPendingRequests(size_t maxRequests)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return std::vector<PathRequest>{};
    }

    std::vector<PathRequest> batchToProcess;
    
    // Extract requests from queue up to the limit
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        
        size_t requestsToTake = std::min(maxRequests, m_requestQueue.size());
        if (requestsToTake == 0) {
            return batchToProcess; // No requests to process
        }
        
        batchToProcess.reserve(requestsToTake);
        
        // Extract requests from priority queue
        for (size_t i = 0; i < requestsToTake && !m_requestQueue.empty(); ++i) {
            batchToProcess.push_back(m_requestQueue.top());
            m_requestQueue.pop();
        }
    }
    
    if (!batchToProcess.empty()) {
        // Sort by spatial locality for better cache performance
        std::sort(batchToProcess.begin(), batchToProcess.end(), spatialComparator);
        
        m_requestsThisFrame.store(batchToProcess.size(), std::memory_order_relaxed);
        
        // Pathfinding batch extracted (stats tracked in periodic summary)
    }
    
    return batchToProcess;
}

void PathfindingScheduler::storePathResult(EntityID entityId, const std::vector<Vector2D>& path)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    uint64_t computeTime = SDL_GetTicks();
    
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_pathResults[entityId] = PathResult(path, computeTime);
    }
    
    // Path result stored (stats tracked in periodic summary)
}

void PathfindingScheduler::processPathBatch(std::vector<PathRequest> batch)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    // This will be called from AIManager context, so we'll modify AIManager to 
    // call this method with the pathfinding grid as a parameter
    // For now, store requests and let AIManager handle the actual pathfinding
    
    for (const auto& request : batch) {
        if (m_isShutdown.load(std::memory_order_relaxed)) {
            break;
        }
        
        // Store empty result for now - AIManager integration will handle actual pathfinding
        uint64_t computeTime = SDL_GetTicks();
        
        {
            std::lock_guard<std::mutex> lock(m_resultsMutex);
            m_pathResults[request.entityId] = PathResult({}, computeTime);
        }
        
        // Call callback with empty path for now
        if (request.callback) {
            request.callback(request.entityId, std::vector<Vector2D>{});
        }
    }
    
    // Pathfinding batch processed (stats tracked in periodic summary)
}

void PathfindingScheduler::processPathBatchWithGrid(std::vector<PathRequest> batch,
                                                   std::function<std::vector<Vector2D>(const Vector2D&, const Vector2D&)> pathfinder)
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    for (const auto& request : batch) {
        if (m_isShutdown.load(std::memory_order_relaxed)) {
            break;
        }
        
        uint64_t requestStartTime = SDL_GetTicks();
        std::vector<Vector2D> path;
        
        try {
            // Use provided pathfinder function
            path = pathfinder(request.start, request.goal);
            
            uint64_t computeTime = SDL_GetTicks();
            
            // Store result in local cache
            {
                std::lock_guard<std::mutex> lock(m_resultsMutex);
                m_pathResults[request.entityId] = PathResult(path, computeTime);
            }
            
            // Cache successful paths for reuse
            if (!path.empty() && m_pathCache) {
                m_pathCache->cachePath(request.start, request.goal, path);
            }
            
            // Call callback if provided
            if (request.callback) {
                request.callback(request.entityId, path);
            }
            
            // Track successful completion and compute time
            uint64_t pathComputeTime = computeTime - requestStartTime;
            m_pathsCompleted.fetch_add(1, std::memory_order_relaxed);
            m_totalRequestsProcessed.fetch_add(1, std::memory_order_relaxed);
            m_totalComputeTimeMs.fetch_add(pathComputeTime, std::memory_order_relaxed);
                    
        } catch (const std::exception& e) {
            AI_ERROR("PathfindingScheduler: Exception during pathfinding for entity " + 
                     std::to_string(request.entityId) + ": " + e.what());
            
            // Store empty result
            {
                std::lock_guard<std::mutex> lock(m_resultsMutex);
                m_pathResults[request.entityId] = PathResult();
            }
            
            // Call callback with empty path
            if (request.callback) {
                request.callback(request.entityId, std::vector<Vector2D>{});
            }
            
            // Track failed request
            m_totalRequestsProcessed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    // Pathfinding batch with grid completed (stats tracked in periodic summary)
}

void PathfindingScheduler::cleanupExpiredRequests()
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }

    uint64_t currentTime = SDL_GetTicks();
    
    // Clean up old path results (older than 10 seconds)
    {
        std::lock_guard<std::mutex> lock(m_resultsMutex);
        auto it = m_pathResults.begin();
        while (it != m_pathResults.end()) {
            if ((currentTime - it->second.computeTime) > 10000) { // 10 second cleanup
                it = m_pathResults.erase(it);
            } else {
                ++it;
            }
        }
    }
}

PathPriority PathfindingScheduler::adjustPriorityByDistance(const PathRequest& request, 
                                                           const Vector2D& playerPos) const
{
    float distanceToPlayer = calculateDistanceToPlayer(request.start, playerPos);
    
    // Spatial priority zones based on distance from player
    if (distanceToPlayer < 800.0f) { // Near zone
        return std::min(request.priority, PathPriority::High);
    } else if (distanceToPlayer < 1600.0f) { // Medium zone
        return request.priority; // Keep original priority
    } else if (distanceToPlayer < 3200.0f) { // Far zone
        return std::max(request.priority, PathPriority::Low);
    } else { // Very far zone - heavily deprioritize
        return PathPriority::Low;
    }
}

float PathfindingScheduler::calculateDistanceToPlayer(const Vector2D& position, 
                                                     const Vector2D& playerPos) const
{
    float dx = position.getX() - playerPos.getX();
    float dy = position.getY() - playerPos.getY();
    return std::sqrt(dx * dx + dy * dy);
}

int PathfindingScheduler::getAreaCongestion(const Vector2D& center, float radius) const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return 0;
    }

    try {
        auto& cm = CollisionManager::Instance();
        std::vector<EntityID> queryResults;
        
        HammerEngine::AABB area(center.getX() - radius, center.getY() - radius, 
                               radius * 2.0f, radius * 2.0f);
        cm.queryArea(area, queryResults);
        
        // Count dynamic/kinematic bodies (entities that need pathfinding)
        int congestion = 0;
        for (EntityID id : queryResults) {
            if (cm.isDynamic(id) || cm.isKinematic(id)) {
                if (!cm.isTrigger(id)) {
                    congestion++;
                }
            }
        }
        
        return congestion;
        
    } catch (const std::exception& e) {
        AI_ERROR("PathfindingScheduler::getAreaCongestion: Exception - " + std::string(e.what()));
        return 0;
    }
}

bool PathfindingScheduler::hasRealtimeObstacles(const Vector2D& start, const Vector2D& goal) const
{
    // Simple line-of-sight check using CollisionManager
    // This is a basic implementation - could be enhanced with more sophisticated checks
    try {
        auto& cm = CollisionManager::Instance();
        
        // Sample points along the path
        float dx = goal.getX() - start.getX();
        float dy = goal.getY() - start.getY();
        float distance = std::sqrt(dx * dx + dy * dy);
        
        if (distance < 1.0f) {
            return false; // Too short to have obstacles
        }
        
        int samples = std::min(static_cast<int>(distance / 32.0f), 16); // Sample every 32 pixels, max 16 samples
        
        for (int i = 1; i < samples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(samples);
            Vector2D samplePoint(start.getX() + dx * t, start.getY() + dy * t);
            
            // Check for obstacles in small area around sample point
            std::vector<EntityID> obstacles;
            HammerEngine::AABB area(samplePoint.getX() - 16.0f, samplePoint.getY() - 16.0f, 32.0f, 32.0f);
            cm.queryArea(area, obstacles);
            
            for (EntityID id : obstacles) {
                if (!cm.isDynamic(id) && !cm.isKinematic(id) && !cm.isTrigger(id)) {
                    return true; // Found static obstacle
                }
            }
        }
        
        return false;
        
    } catch (const std::exception& e) {
        AI_ERROR("PathfindingScheduler::hasRealtimeObstacles: Exception - " + std::string(e.what()));
        return false;
    }
}

bool PathfindingScheduler::spatialComparator(const PathRequest& a, const PathRequest& b)
{
    // Sort by spatial locality using Morton order approximation
    // This improves cache performance when processing pathfinding requests
    
    // Convert coordinates to unsigned for bit operations
    uint32_t ax = static_cast<uint32_t>(std::max(0.0f, a.start.getX()));
    uint32_t ay = static_cast<uint32_t>(std::max(0.0f, a.start.getY()));
    uint32_t bx = static_cast<uint32_t>(std::max(0.0f, b.start.getX()));
    uint32_t by = static_cast<uint32_t>(std::max(0.0f, b.start.getY()));
    
    // Simple Morton-like ordering: interleave bits of x and y coordinates
    uint64_t aCode = 0, bCode = 0;
    for (int i = 0; i < 16; ++i) {
        aCode |= (uint64_t)((ax >> i) & 1) << (2 * i);
        aCode |= (uint64_t)((ay >> i) & 1) << (2 * i + 1);
        bCode |= (uint64_t)((bx >> i) & 1) << (2 * i);
        bCode |= (uint64_t)((by >> i) & 1) << (2 * i + 1);
    }
    
    return aCode < bCode;
}

PathCacheStats PathfindingScheduler::getPathCacheStats() const
{
    if (m_isShutdown.load(std::memory_order_relaxed) || !m_pathCache) {
        return PathCacheStats{};
    }
    
    return m_pathCache->getStats();
}

void PathfindingScheduler::logPathfindingStats() const
{
    if (m_isShutdown.load(std::memory_order_relaxed)) {
        return;
    }
    
    // Get current statistics
    size_t queueSize = getQueueSize();
    size_t activeRequests = getActiveRequestCount();
    size_t totalProcessed = m_totalRequestsProcessed.load(std::memory_order_relaxed);
    size_t pathsCompleted = m_pathsCompleted.load(std::memory_order_relaxed);
    size_t pathsFromCache = m_pathsFromCache.load(std::memory_order_relaxed);
    uint64_t totalComputeTime = m_totalComputeTimeMs.load(std::memory_order_relaxed);
    
    // Calculate success rate and average compute time
    float successRate = (totalProcessed > 0) ? 
        (static_cast<float>(pathsCompleted) / static_cast<float>(totalProcessed) * 100.0f) : 0.0f;
    float avgComputeTime = (pathsCompleted > 0) ? 
        (static_cast<float>(totalComputeTime) / static_cast<float>(pathsCompleted)) : 0.0f;
    
    // Get cache stats if available
    std::string cacheInfo;
    if (m_pathCache) {
        auto cacheStats = m_pathCache->getStats();
        cacheInfo = " Cache: " + std::to_string(cacheStats.totalPaths) + " paths, " + 
                    std::to_string(static_cast<int>(cacheStats.hitRate * 100.0f)) + "% hit rate";
    }
    
    // Log aggregated statistics in ParticleManager style
    AI_INFO("Pathfinding: Queue: " + std::to_string(queueSize) + 
            ", Active: " + std::to_string(activeRequests) + 
            ", Success: " + std::to_string(static_cast<int>(successRate)) + "%" +
            ", Cached: " + std::to_string(pathsFromCache) + 
            ", Avg: " + std::to_string(static_cast<int>(avgComputeTime)) + "ms" + cacheInfo);
}

} // namespace AIInternal