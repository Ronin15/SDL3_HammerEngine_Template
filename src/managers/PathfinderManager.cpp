/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "../ai/internal/PathCache.hpp"
#include "../ai/internal/SpatialPriority.hpp" // for AIInternal::PathPriority enum
#include "managers/WorldManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <algorithm>

// Static instance for singleton
static std::unique_ptr<PathfinderManager> s_instance;
static std::mutex s_instanceMutex;

PathfinderManager& PathfinderManager::Instance() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (!s_instance) {
        s_instance.reset(new PathfinderManager());
    }
    return *s_instance;
}

PathfinderManager::~PathfinderManager() {
    if (!m_isShutdown) {
        clean();
    }
}

bool PathfinderManager::init() {
    if (m_initialized.load()) {
        return true;
    }

    try {
        PATHFIND_INFO("Initializing PathfinderManager");

        // Don't initialize grid immediately - wait for world to be loaded
        // Grid will be created lazily when first needed and world is available
        float cellSize = 64.0f; // Optimized cell size for 4x performance improvement
        m_cellSize = cellSize;

        // Initialize PathCache directly
        m_cache = std::make_unique<AIInternal::PathCache>();

        PATHFIND_INFO("PathfinderManager: PathCache initialized");

        // Grid configuration will be applied when grid is created

        // Skip initial grid rebuild - will happen when world is loaded
        // rebuildGrid();

        m_initialized.store(true);
        PATHFIND_INFO("PathfinderManager initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        PATHFIND_ERROR("Failed to initialize PathfinderManager: " + std::string(e.what()));
        return false;
    }
}

bool PathfinderManager::isInitialized() const {
    return m_initialized.load();
}

void PathfinderManager::update(float deltaTime) {
    if (!m_initialized.load() || m_isShutdown) {
        return;
    }

    // Update timers
    m_gridUpdateTimer += deltaTime;
    m_cacheCleanupTimer += deltaTime;

    // Periodic grid update from world/collision data
    if (m_gridUpdateTimer >= GRID_UPDATE_INTERVAL) {
        updateDynamicObstacles();
        m_gridUpdateTimer = 0.0f;
    }

    // Periodic cache maintenance (cleanup + congestion-aware eviction)
    if (m_cacheCleanupTimer >= CACHE_CLEANUP_INTERVAL) {
        cleanupCache();
        if (m_cache) {
            Vector2D playerPos(0, 0); // TODO: replace with actual player position
            m_cache->evictPathsInCrowdedAreas(playerPos);
        }
        m_cacheCleanupTimer = 0.0f;
    }

    // Update statistics
    updateStatistics();
}

void PathfinderManager::clean() {
    if (m_isShutdown) {
        return;
    }

    PATHFIND_INFO("Cleaning up PathfinderManager");

    // Clear cache
    if (m_cache) {
        m_cache->clear();
    }
    
    // Clear any remaining local request data - no queues in direct async mode

    // Clean up components
    if (m_cache) {
        m_cache->shutdown();
    }

    m_cache.reset();
    m_grid.reset();

    m_initialized.store(false);
    PATHFIND_INFO("PathfinderManager cleaned up");
}

void PathfinderManager::shutdown() {
    std::lock_guard<std::mutex> lock(s_instanceMutex);
    if (s_instance) {
        s_instance->clean();
        s_instance->m_isShutdown = true;
        s_instance.reset();
    }
}

bool PathfinderManager::isShutdown() const {
    return m_isShutdown;
}

// Quantize start/goal by 'quant' and mix via FNV-1a style
uint64_t PathfinderManager::computeKey(const Vector2D& start, const Vector2D& goal, float quant) const {
    const float q = (quant > 0.0f) ? quant : 64.0f;
    auto qcoord = [q](float v) -> uint32_t {
        return static_cast<uint32_t>(std::floor(v / q + 0.5f));
    };
    uint32_t sx = qcoord(start.getX());
    uint32_t sy = qcoord(start.getY());
    uint32_t gx = qcoord(goal.getX());
    uint32_t gy = qcoord(goal.getY());
    uint64_t h = 14695981039346656037ULL;
    h ^= sx; h *= 1099511628211ULL;
    h ^= sy; h *= 1099511628211ULL;
    h ^= gx; h *= 1099511628211ULL;
    h ^= gy; h *= 1099511628211ULL;
    return h;
}

uint64_t PathfinderManager::computeCorridorKey(const Vector2D& start, const Vector2D& goal) const {
    // Use coarse grid world size (cellSize * 4) for corridor grouping
    float coarse = std::max(32.0f, m_cellSize * 4.0f);
    auto qcoord = [coarse](float v) -> int64_t {
        return static_cast<int64_t>(std::floor(v / coarse + 0.5f));
    };
    uint64_t sx = static_cast<uint64_t>(qcoord(start.getX()));
    uint64_t sy = static_cast<uint64_t>(qcoord(start.getY()));
    uint64_t gx = static_cast<uint64_t>(qcoord(goal.getX()));
    uint64_t gy = static_cast<uint64_t>(qcoord(goal.getY()));
    uint64_t a = (sx << 32) ^ sy;
    uint64_t b = (gx << 32) ^ gy;
    // Order-independent mix to maximize reuse
    uint64_t lo = std::min(a, b);
    uint64_t hi = std::max(a, b);
    return (lo * 0x9E3779B185EBCA87ULL) ^ (hi + 0x85EBCA6B);
}

std::vector<Vector2D> PathfinderManager::adjustPathEndpoints(const std::vector<Vector2D>& path,
                                                             const Vector2D& reqStart,
                                                             const Vector2D& reqGoal) {
    if (path.empty()) return path;
    std::vector<Vector2D> out = path;
    out.front() = reqStart;
    out.back() = reqGoal;
    return out;
}


// ASYNC PATHFINDING: Direct ThreadSystem submission for true async processing  
uint64_t PathfinderManager::requestPathAsync(
    EntityID entityId,
    const Vector2D& start,
    const Vector2D& goal,
    AIInternal::PathPriority priority,
    int aiManagerPriority,
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback
) {
    if (!m_initialized.load() || m_isShutdown) {
        return 0;
    }

    // Ensure grid is initialized before pathfinding
    if (!ensureGridInitialized()) {
        // Return a valid request ID but callback with empty path
        if (callback) {
            callback(entityId, std::vector<Vector2D>());
        }
        return m_nextRequestId.fetch_add(1);
    }

    // Pre-check cache and negative cache to avoid scheduling work when possible
    const float dist2 = (goal - start).dot(goal - start);
    float tol;
    if (dist2 > (2000.0f * 2000.0f)) {
        tol = 192.0f; // Ultra-long routes: be lenient to boost reuse
    } else if (dist2 > (1200.0f * 1200.0f)) {
        tol = 144.0f; // Long routes: moderate tolerance
    } else {
        tol = 96.0f;  // Local routes: tighter reuse
    }
    if (m_cache) {
        if (auto cached = m_cache->findSimilarPath(start, goal, tol)) {
            if (callback) callback(entityId, *cached);
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.totalRequests++;
                m_stats.completedRequests++;
                m_stats.cacheHits++;
            }
            return m_nextRequestId.fetch_add(1);
        }
        if (m_cache->hasNegativeCached(start, goal, tol)) {
            if (callback) callback(entityId, {});
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.totalRequests++;
                m_stats.cacheHits++;
            }
            return m_nextRequestId.fetch_add(1);
        }
    }

    // In-flight coalescing: prefer corridor-level coalescing for long routes
    const uint64_t key = computeKey(start, goal, tol);
    const bool isLongRoute = (dist2 > (1200.0f * 1200.0f));
    uint64_t corridorKey = 0;
    bool scheduledByCorridor = false;
    {
        std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
        if (isLongRoute) {
            corridorKey = computeCorridorKey(start, goal);
            auto itc = m_inflightCorridor.find(corridorKey);
            if (itc != m_inflightCorridor.end()) {
                // Attach to existing corridor job
                m_inflightCorridor[corridorKey].push_back({entityId, start, goal, callback});
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.totalRequests++;
                }
                return m_nextRequestId.fetch_add(1);
            } else {
                // Create new corridor job
                m_inflightCorridor[corridorKey].push_back({entityId, start, goal, callback});
                scheduledByCorridor = true;
            }
        } else {
            // Fine-grained deduplication for short routes
            auto it = m_inflight.find(key);
            if (it != m_inflight.end()) {
                if (callback) it->second.emplace_back(entityId, callback);
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.totalRequests++;
                }
                return m_nextRequestId.fetch_add(1);
            } else {
                if (callback) m_inflight[key].emplace_back(entityId, callback);
                else m_inflight[key];
            }
        }
    }

    // Lightweight backpressure: avoid flooding workers with low-priority work
    {
        size_t inflightCount = 0;
        {
            std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
            inflightCount = m_inflight.size() + m_inflightCorridor.size();
        }
        const size_t queueSize = HammerEngine::ThreadSystem::Instance().getQueueSize();
        const bool pressureHigh = (queueSize > 1024) || (inflightCount > 512);
        const bool notUrgent = (priority == AIInternal::PathPriority::Low) ||
                               (priority == AIInternal::PathPriority::Normal && aiManagerPriority <= 5);
        if (pressureHigh && notUrgent) {
            // Defer non-urgent work under pressure; behaviors have TTL/backoff
            if (callback) {
                callback(entityId, std::vector<Vector2D>());
            }
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.totalRequests++;
            }
            return m_nextRequestId.fetch_add(1);
        }
    }

    // TRUE ASYNC: Submit directly to ThreadSystem without queueing
    if (m_grid) {
        // TRUE ASYNC: Submit directly to ThreadSystem  
        auto work = [this, start, goal, tol, key, dist2, scheduledByCorridor, corridorKey]() {
            // FIXED: Check cache first, even in async pathfinding
            std::vector<Vector2D> path;
            bool cacheHit = false;
            
            // Check cache first
            if (m_cache) {
                if (auto cachedPath = m_cache->findSimilarPath(start, goal, tol)) {
                    path = *cachedPath;
                    cacheHit = true;
                    
                    // Update cache hit statistics
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_stats.cacheHits++;
                        m_stats.completedRequests++;
                    }
                }
            }
            
            // If no cache hit, consider negative cache then perform pathfinding
            if (!cacheHit) {
                // Negative cache check to avoid repeated expensive searches
                if (m_cache && m_cache->hasNegativeCached(start, goal, tol)) {
                    // Immediately respond with empty path (negative cache hit)
                    // fan-out to inflight callbacks
                    std::vector<std::pair<EntityID, PathCallback>> callbacks;
                    {
                        std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
                        auto it = m_inflight.find(key);
                        if (it != m_inflight.end()) {
                            callbacks.swap(it->second);
                            m_inflight.erase(it);
                        }
                    }
                    for (auto &cb : callbacks) {
                        if (cb.second) cb.second(cb.first, path);
                    }
                    return;
                }

                float distance2 = dist2;
                HammerEngine::PathfindingResult result;
                
                if (distance2 > (1200.0f * 1200.0f)) {
                    // Prefer hierarchical for long distances
                    result = m_grid->findPathHierarchical(start, goal, path);

                    // Fallback: if hierarchical fails or times out, try direct
                    if (result != HammerEngine::PathfindingResult::SUCCESS || path.empty()) {
                        std::vector<Vector2D> directPath;
                        auto directRes = m_grid->findPath(start, goal, directPath);
                        if (directRes == HammerEngine::PathfindingResult::SUCCESS && !directPath.empty()) {
                            path.swap(directPath);
                            result = directRes;
                        }
                    }

                    // Update hierarchical/direct statistics
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        if (result == HammerEngine::PathfindingResult::SUCCESS) {
                            m_stats.hierarchicalRequests++;
                        } else {
                            // Count toward direct attempts when fallback used
                            m_stats.hierarchicalRequests++;
                        }
                    }
                } else {
                    // Direct pathfinding for short distances
                    result = m_grid->findPath(start, goal, path);

                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.directRequests++;
                }
                
                // Cache successful paths for future reuse
                if (m_cache) {
                    if (result == HammerEngine::PathfindingResult::SUCCESS && !path.empty()) {
                        m_cache->cachePath(start, goal, path);
                    } else if (result == HammerEngine::PathfindingResult::INVALID_GOAL ||
                               result == HammerEngine::PathfindingResult::NO_PATH_FOUND) {
                        // Cache only definitive negatives; avoid caching timeouts
                        m_cache->cacheNegative(start, goal);
                    }
                }
                
                // Update cache miss and timeout statistics
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.cacheMisses++;
                    if (result == HammerEngine::PathfindingResult::TIMEOUT) {
                        m_stats.timedOutRequests++;
                    }
                }
            }
            
            if (scheduledByCorridor) {
                // Fan-out with per-request endpoint adjustment
                std::vector<CorridorCallback> items;
                {
                    std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
                    auto itc = m_inflightCorridor.find(corridorKey);
                    if (itc != m_inflightCorridor.end()) {
                        items.swap(itc->second);
                        m_inflightCorridor.erase(itc);
                    }
                }
                for (auto &it : items) {
                    if (it.cb) {
                        auto adj = adjustPathEndpoints(path, it.start, it.goal);
                        // Opportunistic cache insert for adjusted endpoints to lift reuse
                        if (m_cache && !adj.empty()) {
                            m_cache->cachePath(it.start, it.goal, adj);
                        }
                        it.cb(it.id, adj);
                    }
                }
                // Count successes per-callback to keep success rate accurate
                if (!path.empty()) {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.completedRequests += static_cast<uint64_t>(items.size());
                }
            } else {
                // Fan-out original coalesced requests
                std::vector<std::pair<EntityID, PathCallback>> callbacks;
                {
                    std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
                    auto it = m_inflight.find(key);
                    if (it != m_inflight.end()) {
                        callbacks.swap(it->second);
                        m_inflight.erase(it);
                    }
                }
                for (auto &cb : callbacks) {
                    if (cb.second) cb.second(cb.first, path);
                }
                // Count successes per-callback to keep success rate accurate
                if (!path.empty()) {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.completedRequests += static_cast<uint64_t>(callbacks.size());
                }
            }
        };
        
        // Convert AI path priority and AI manager priority to ThreadSystem priority (take higher priority)
        auto mapPathPrio = [](AIInternal::PathPriority p) {
            switch (p) {
                case AIInternal::PathPriority::Critical:
                case AIInternal::PathPriority::High:   return HammerEngine::TaskPriority::High;
                case AIInternal::PathPriority::Normal: return HammerEngine::TaskPriority::Normal;
                case AIInternal::PathPriority::Low:    return HammerEngine::TaskPriority::Low;
            }
            return HammerEngine::TaskPriority::Normal;
        };
        auto mapAIMgrPrio = [](int p) {
            if (p >= 8) return HammerEngine::TaskPriority::High;
            if (p >= 6) return HammerEngine::TaskPriority::Normal;
            return HammerEngine::TaskPriority::Low;
        };
        auto pickHigher = [](HammerEngine::TaskPriority a, HammerEngine::TaskPriority b) {
            // Lower enum value means higher priority
            return (static_cast<int>(a) < static_cast<int>(b)) ? a : b;
        };
        HammerEngine::TaskPriority taskPriority = pickHigher(mapPathPrio(priority), mapAIMgrPrio(aiManagerPriority));

        HammerEngine::ThreadSystem::Instance().enqueueTask(work, taskPriority, "PathfindingAsync");
        
        // Update stats only
        {
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_stats.totalRequests++;
        }
        
        return m_nextRequestId.fetch_add(1);
    }

    return 0;
}

HammerEngine::PathfindingResult PathfinderManager::findPathImmediate(
    const Vector2D& start,
    const Vector2D& goal,
    std::vector<Vector2D>& outPath
) {
    if (!m_initialized.load() || m_isShutdown) {
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Ensure grid is initialized before pathfinding
    if (!ensureGridInitialized()) {
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Track request statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalRequests++;
    }

    // CRITICAL FIX: Restore cache functionality for immediate requests
    // Check cache first before expensive A* computation
    if (m_cache) {
        const float dist2 = (goal - start).dot(goal - start);
        float tol;
        if (dist2 > (2000.0f * 2000.0f)) {
            tol = 192.0f;
        } else if (dist2 > (1200.0f * 1200.0f)) {
            tol = 144.0f;
        } else if (dist2 > (512.0f * 512.0f)) {
            tol = 128.0f;
        } else {
            tol = 64.0f;
        }
        if (auto cachedPath = m_cache->findSimilarPath(start, goal, tol)) {
            outPath = *cachedPath;
            
            // Update statistics for cache hit
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.completedRequests++;
                m_stats.cacheHits++;
            }
            
            return HammerEngine::PathfindingResult::SUCCESS;
        } else if (m_cache->hasNegativeCached(start, goal, tol)) {
            // Negative cache hit: return gracefully without recomputing
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.cacheHits++;
            }
            return HammerEngine::PathfindingResult::NO_PATH_FOUND;
        }
    }
    
    // Cache miss - compute path using intelligent hierarchical pathfinding
    float distance2 = (goal - start).dot(goal - start);
    HammerEngine::PathfindingResult result;
    
    if (distance2 > (1200.0f * 1200.0f)) {
        // Prefer hierarchical for long distances
        result = m_grid->findPathHierarchical(start, goal, outPath);

        // Fallback: if hierarchical fails or times out, try direct
        if (result != HammerEngine::PathfindingResult::SUCCESS || outPath.empty()) {
            std::vector<Vector2D> directPath;
            auto directRes = m_grid->findPath(start, goal, directPath);
            if (directRes == HammerEngine::PathfindingResult::SUCCESS && !directPath.empty()) {
                outPath.swap(directPath);
                result = directRes;
            }
        }

        // Stats
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.hierarchicalRequests++;
    } else {
        // Direct pathfinding for short distances
        result = m_grid->findPath(start, goal, outPath);

        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.directRequests++;
    }
    
    // Cache result for future reuse
    if (m_cache) {
        if (result == HammerEngine::PathfindingResult::SUCCESS && !outPath.empty()) {
            m_cache->cachePath(start, goal, outPath);
        } else if (result == HammerEngine::PathfindingResult::INVALID_GOAL ||
                   result == HammerEngine::PathfindingResult::NO_PATH_FOUND) {
            // Cache only definitive negatives; avoid caching timeouts
            m_cache->cacheNegative(start, goal);
        }
    }
    
    // Update cache miss statistics (since we didn't hit cache and had to compute)
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.cacheMisses++;
        if (result == HammerEngine::PathfindingResult::SUCCESS) {
            m_stats.completedRequests++;
        } else if (result == HammerEngine::PathfindingResult::TIMEOUT) {
            m_stats.timedOutRequests++;
        }
    }

    return result;
}

void PathfinderManager::cancelRequest(uint64_t /* requestId */) {
    // Cancellation is now handled by PathfindingScheduler
    // This method is kept for interface compatibility
    
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.cancelledRequests++;
    }
}

void PathfinderManager::cancelEntityRequests(EntityID /* entityId */) {
    // Cancellation is now handled by PathfindingScheduler
    // This method is kept for interface compatibility
    
    // Scheduler handles the actual cancellation
    // Just update stats for compatibility
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.cancelledRequests++; // Approximate for interface compatibility
    }
}

void PathfinderManager::rebuildGrid() {
    // Ensure grid is initialized (this also checks for active world)
    if (!ensureGridInitialized()) {
        return;
    }

    m_grid->rebuildFromWorld();
    
    // Grid changed; clear cache to avoid stale paths
    if (m_cache) {
        m_cache->clear();
    }
    GAMEENGINE_INFO("Grid rebuilt manually and cache invalidated");
}

void PathfinderManager::updateDynamicObstacles() {
    // Ensure grid is initialized (this also checks for active world)
    if (!ensureGridInitialized()) {
        return;
    }

    // Smart grid rebuilding: Only rebuild when world has changed or a safety interval elapsed
    static uint64_t lastWorldVersion = 0;
    static float secondsSinceLastRebuild = 0.0f;
    secondsSinceLastRebuild += GRID_UPDATE_INTERVAL; // Called on a fixed cadence (see update())
    
    // Check if world has changed by getting version from WorldManager
    auto& worldManager = WorldManager::Instance();
    uint64_t currentWorldVersion = worldManager.getWorldVersion();
    
    // Rebuild when version changed or every 30 seconds as a fallback
    bool worldChanged = (currentWorldVersion != lastWorldVersion);
    bool shouldRebuild = worldChanged || (secondsSinceLastRebuild >= 60.0f);
    
    if (shouldRebuild) {
        // PERFORMANCE OPTIMIZATION: Move expensive grid rebuild to background thread
        // This prevents blocking the main AI update loop
        auto rebuildTask = [this]() {
            if (m_grid) {
                m_grid->rebuildFromWorld();
                
                // Do not clear the cache on rebuild; rely on pathfinding corrections
                
                GAMEENGINE_INFO("Async grid rebuild completed");
            }
        };
        
        // Submit to ThreadSystem with normal priority (not blocking critical AI tasks)
        HammerEngine::ThreadSystem::Instance().enqueueTask(rebuildTask, HammerEngine::TaskPriority::Normal);
        
        // Update tracking variables immediately to prevent duplicate rebuilds
        lastWorldVersion = currentWorldVersion;
        secondsSinceLastRebuild = 0.0f;
    }

    // Always integrate dynamic collision data (entities moving around)
    integrateCollisionData();
}

void PathfinderManager::addTemporaryWeightField(const Vector2D& center, float radius, float weight) {
    if (!ensureGridInitialized()) {
        return;
    }

    m_grid->addWeightCircle(center, radius, weight);
}

void PathfinderManager::clearWeightFields() {
    if (!ensureGridInitialized()) {
        return;
    }

    m_grid->resetWeights(1.0f);
}

void PathfinderManager::setMaxPathsPerFrame(int maxPaths) {
    m_maxPathsPerFrame = std::max(1, maxPaths);
}

void PathfinderManager::setCacheExpirationTime(float seconds) {
    m_cacheExpirationTime = std::max(0.1f, seconds);
    // Cache expiration time is used in cleanup method
}

void PathfinderManager::setAllowDiagonal(bool allow) {
    m_allowDiagonal = allow;
    if (m_grid) {
        m_grid->setAllowDiagonal(allow);
    }
}

void PathfinderManager::setMaxIterations(int maxIterations) {
    m_maxIterations = std::max(100, maxIterations);
    if (m_grid) {
        m_grid->setMaxIterations(m_maxIterations);
    }
}

PathfinderManager::PathfinderStats PathfinderManager::getStats() const {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    
    // Get cache statistics directly from cache
    if (m_cache) {
        auto cacheStats = m_cache->getStats();
        
        // Update our stats with the cache stats
        m_stats.cacheHits = static_cast<uint64_t>(cacheStats.totalHits);
        m_stats.cacheMisses = static_cast<uint64_t>(cacheStats.totalMisses);
        m_stats.cacheHitRate = cacheStats.hitRate;
        
    } else {
        // No cache available - calculate from local stats
        if ((m_stats.cacheHits + m_stats.cacheMisses) > 0) {
            m_stats.cacheHitRate = static_cast<float>(m_stats.cacheHits) / 
                                   static_cast<float>(m_stats.cacheHits + m_stats.cacheMisses);
        } else {
            m_stats.cacheHitRate = 0.0f;
        }
    }
    
    // Report in-flight requests as 'active threads' to reflect real activity
    {
        std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
        m_stats.activeThreads = static_cast<uint32_t>(m_inflight.size());
    }
    
    return m_stats;
}

void PathfinderManager::resetStats() {
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = PathfinderStats{};
    
    if (m_grid) {
        m_grid->resetStats();
    }
}


void PathfinderManager::updateStatistics() {
    // Consolidate manager-level statistics (includes hierarchical + direct)
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.pendingRequests = 0; // No internal queue

    // Use manager-tracked totals for success/timeout rates (more accurate across strategies)
    uint64_t totalReqs = m_stats.totalRequests;
    uint64_t totalCompleted = m_stats.completedRequests;
    uint64_t totalTimeouts = m_stats.timedOutRequests;
    float successRate = 0.0f;
    float timeoutRate = 0.0f;
    if (totalReqs > 0) {
        successRate = (static_cast<float>(totalCompleted) / static_cast<float>(totalReqs)) * 100.0f;
        timeoutRate = (static_cast<float>(totalTimeouts) / static_cast<float>(totalReqs)) * 100.0f;
    }

    // Keep avg path length from fine grid when available (informational)
    float avgPathLength = m_stats.averagePathLength;
    if (m_grid) {
        auto gridStats = m_grid->getStats();
        if (gridStats.successfulPaths > 0) {
            avgPathLength = static_cast<float>(gridStats.avgPathLength);
            m_stats.averagePathLength = avgPathLength;
        }
    }
    
    // Periodic consolidated status reporting (every 5 seconds, like AIManager)
    static auto lastDebugTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastDebug = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastDebugTime);
    
    if (timeSinceLastDebug.count() >= 5 && totalReqs > 0) {
        lastDebugTime = currentTime;
        
        // Get fresh cache statistics directly from cache before displaying
        if (m_cache) {
            auto cacheStats = m_cache->getStats();
            m_stats.cacheHits = static_cast<uint64_t>(cacheStats.totalHits);
            m_stats.cacheMisses = static_cast<uint64_t>(cacheStats.totalMisses);
            m_stats.cacheHitRate = cacheStats.hitRate;
        }
        // Refresh activeThreads from in-flight map to reflect real activity
        {
            std::lock_guard<std::mutex> inflightLock(m_inflightMutex);
            m_stats.activeThreads = static_cast<uint32_t>(m_inflight.size() + m_inflightCorridor.size());
        }
        
        // Get cache statistics
        float cacheHitRate = m_stats.cacheHitRate * 100.0f;
        uint32_t cacheHits = m_stats.cacheHits;
        uint32_t cacheMisses = m_stats.cacheMisses;
        size_t cacheSize = 0;
        size_t cacheEvicted = 0;
        size_t cacheCongestionEvictions = 0;
        if (m_cache) {
            auto cacheStats = m_cache->getStats();
            cacheSize = cacheStats.totalPaths;
            cacheEvicted = cacheStats.evictedPaths;
            cacheCongestionEvictions = cacheStats.congestionEvictions;
        }
        uint32_t activeThreads = m_stats.activeThreads;
        uint32_t pendingRequests = static_cast<uint32_t>(m_stats.pendingRequests);
        uint64_t totalNoPath = (totalReqs > (totalCompleted + totalTimeouts)) ?
                               (totalReqs - (totalCompleted + totalTimeouts)) : 0;
        float failRate = (totalReqs > 0) ? (100.0f * static_cast<float>(totalNoPath) / static_cast<float>(totalReqs)) : 0.0f;
        
        // Calculate hierarchical pathfinding ratio
        uint64_t hierarchicalRequests = m_stats.hierarchicalRequests;
        uint64_t directRequests = m_stats.directRequests;
        uint64_t totalPathRequests = hierarchicalRequests + directRequests;
        float hierarchicalRatio = (totalPathRequests > 0) ? 
            (100.0f * hierarchicalRequests) / totalPathRequests : 0.0f;
        
        PATHFIND_INFO("PathfinderManager Status - Requests: " + std::to_string(totalReqs) +
                        ", SUCCESS RATE: " + std::to_string(static_cast<int>(successRate)) + "%" +
                        ", TIMEOUT RATE: " + std::to_string(static_cast<int>(timeoutRate)) + "%" +
                        " (" + std::to_string(totalTimeouts) + " timeouts)" +
                        ", FAIL RATE: " + std::to_string(static_cast<int>(failRate)) + "%" +
                        " (" + std::to_string(totalNoPath) + " no-path)" +
                        ", Pending: " + std::to_string(pendingRequests) +
                        ", Cache Hit Rate: " + std::to_string(static_cast<int>(cacheHitRate)) + "%" +
                        " (" + std::to_string(cacheHits) + "/" + std::to_string(cacheHits + cacheMisses) + ")" +
                        ", Cache Size: " + std::to_string(cacheSize) +
                        ", Evicted: " + std::to_string(cacheEvicted) + 
                        ", Congestion Evictions: " + std::to_string(cacheCongestionEvictions) +
                        ", Avg Path Length: " + std::to_string(static_cast<int>(avgPathLength)) + " nodes" +
                        ", Hierarchical: " + std::to_string(static_cast<int>(hierarchicalRatio)) + "%" +
                        " (" + std::to_string(hierarchicalRequests) + "/" + std::to_string(totalPathRequests) + ")" +
                        ", Active Threads: " + std::to_string(activeThreads) +
                        ", InFlight: " + std::to_string(m_stats.activeThreads));
    }
}

void PathfinderManager::cleanupCache() {
    if (m_cache) {
        const uint64_t maxAgeMs = static_cast<uint64_t>(m_cacheExpirationTime * 1000.0f);
        m_cache->cleanup(maxAgeMs);
    }
}

void PathfinderManager::integrateCollisionData() {
    // This would integrate dynamic collision data into the pathfinding grid
    // For now, we'll rely on the grid's rebuildFromWorld() which gets static obstacles
    // In the future, this could query CollisionManager for dynamic obstacles
}

void PathfinderManager::integrateWorldData() {
    // Already handled by rebuildGrid() which calls grid->rebuildFromWorld()
    // This method is here for future expansion if needed
}

bool PathfinderManager::ensureGridInitialized() {
    if (m_grid) {
        return true; // Grid already exists
    }

    // Check if we have an active world to get dimensions from
    auto& worldManager = WorldManager::Instance();
    if (!worldManager.hasActiveWorld()) {
        GAMEENGINE_DEBUG("Cannot initialize pathfinding grid - no active world");
        return false;
    }

    // Get world dimensions
    int worldWidth = 0;
    int worldHeight = 0;
    if (!worldManager.getWorldDimensions(worldWidth, worldHeight)) {
        GAMEENGINE_WARN("Cannot get world dimensions for pathfinding grid initialization");
        return false;
    }

    // Validate dimensions to prevent 0x0 grids
    if (worldWidth <= 0 || worldHeight <= 0) {
        GAMEENGINE_WARN("Invalid world dimensions for pathfinding grid: " + 
                        std::to_string(worldWidth) + "x" + std::to_string(worldHeight));
        return false;
    }

    // Calculate grid dimensions in cells
    // NOTE: worldWidth/Height from WorldManager are in tile count, not pixels
    // Convert to pixels first: tileCount * TILE_SIZE = pixelDimensions
    const float TILE_SIZE = 32.0f; // Match WorldManager::TILE_SIZE
    float worldPixelWidth = worldWidth * TILE_SIZE;
    float worldPixelHeight = worldHeight * TILE_SIZE;
    int gridWidth = static_cast<int>(worldPixelWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldPixelHeight / m_cellSize);

    // Ensure minimum grid size
    if (gridWidth <= 0 || gridHeight <= 0) {
        GAMEENGINE_WARN("Calculated grid dimensions too small: " + 
                        std::to_string(gridWidth) + "x" + std::to_string(gridHeight) + 
                        " (world: " + std::to_string(worldWidth) + "x" + std::to_string(worldHeight) + 
                        ", cellSize: " + std::to_string(m_cellSize) + ")");
        return false;
    }

    try {
        // Create the pathfinding grid
        m_grid = std::make_unique<HammerEngine::PathfindingGrid>(
            gridWidth,
            gridHeight,
            m_cellSize,
            Vector2D(0, 0)
        );

        // Apply configuration settings
        m_grid->setAllowDiagonal(m_allowDiagonal);
        m_grid->setMaxIterations(m_maxIterations);

        // Build the grid from world data
        m_grid->rebuildFromWorld();

        GAMEENGINE_INFO("Pathfinding grid initialized: " + 
                        std::to_string(gridWidth) + "x" + std::to_string(gridHeight) + 
                        " cells (world: " + std::to_string(worldWidth) + "x" + std::to_string(worldHeight) + 
                        ", cellSize: " + std::to_string(m_cellSize) + ")");
        
        return true;
    }
    catch (const std::exception& e) {
        GAMEENGINE_ERROR("Failed to create pathfinding grid: " + std::string(e.what()));
        m_grid.reset();
        return false;
    }
}

Vector2D PathfinderManager::clampToWorldBounds(const Vector2D& position, float margin) const {
    // Get world bounds from WorldManager
    auto& worldManager = WorldManager::Instance();
    float minX, minY, maxX, maxY;
    
    if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
        // getWorldBounds() already returns pixel coordinates
        return Vector2D(
            std::clamp(position.getX(), minX + margin, maxX - margin),
            std::clamp(position.getY(), minY + margin, maxY - margin)
        );
    }
    
    // Fallback bounds if WorldManager fails - use generous margins
    const float fallbackMargin = std::max(margin, 256.0f);
    return Vector2D(
        std::clamp(position.getX(), 0.0f + fallbackMargin, 3200.0f - fallbackMargin),
        std::clamp(position.getY(), 0.0f + fallbackMargin, 3200.0f - fallbackMargin)
    );
}

bool PathfinderManager::followPathStep(EntityPtr entity, const Vector2D& currentPos,
                                      std::vector<Vector2D>& path, size_t& pathIndex,
                                      float speed, float nodeRadius) const {
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
