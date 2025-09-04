/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "../ai/internal/PathCache.hpp"
#include "../ai/internal/SpatialPriority.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/WorldManager.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <future>

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

        // Initialize spatial priority system
        m_spatialPriority = std::make_unique<AIInternal::SpatialPriority>();

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

    // Periodic cache cleanup
    if (m_cacheCleanupTimer >= CACHE_CLEANUP_INTERVAL) {
        cleanupCache();
        m_cacheCleanupTimer = 0.0f;
    }

    // Periodic cache cleanup
    if (m_cache) {
        Vector2D playerPos(0, 0); // TODO: Get actual player position
        m_cache->evictPathsInCrowdedAreas(playerPos);
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

    // Wait for active threads to complete
    for (auto& task : m_activeTasks) {
        if (task.valid()) {
            task.wait();
        }
    }
    m_activeTasks.clear();

    // Clean up components
    if (m_cache) {
        m_cache->shutdown();
    }

    m_cache.reset();
    m_spatialPriority.reset();
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


// ASYNC PATHFINDING: Direct ThreadSystem submission for true async processing  
uint64_t PathfinderManager::requestPathAsync(
    EntityID entityId,
    const Vector2D& start,
    const Vector2D& goal,
    AIInternal::PathPriority /* priority */,
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

    // TRUE ASYNC: Submit directly to ThreadSystem without queueing
    if (m_grid) {
        // TRUE ASYNC: Submit directly to ThreadSystem  
        auto work = [this, entityId, start, goal, callback]() {
            // FIXED: Check cache first, even in async pathfinding
            std::vector<Vector2D> path;
            bool cacheHit = false;
            
            // Check cache first
            if (m_cache) {
                if (auto cachedPath = m_cache->findSimilarPath(start, goal, 64.0f)) {
                    path = *cachedPath;
                    cacheHit = true;
                    
                    // Update cache hit statistics
                    {
                        std::lock_guard<std::mutex> lock(m_statsMutex);
                        m_stats.cacheHits++;
                    }
                }
            }
            
            // If no cache hit, perform pathfinding
            if (!cacheHit) {
                float distance = (goal - start).length();
                HammerEngine::PathfindingResult result;
                
                if (distance > 512.0f) {
                    // Use hierarchical pathfinding for long distances
                    result = m_grid->findPathHierarchical(start, goal, path);
                    
                    // Update hierarchical statistics
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.hierarchicalRequests++;
                }
                else {
                    // Use direct pathfinding for short distances
                    result = m_grid->findPath(start, goal, path);
                    
                    // Update direct statistics
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.directRequests++;
                }
                
                // Cache successful paths for future reuse
                if (result == HammerEngine::PathfindingResult::SUCCESS && !path.empty() && m_cache) {
                    m_cache->cachePath(start, goal, path);
                }
                
                // Update cache miss statistics
                {
                    std::lock_guard<std::mutex> lock(m_statsMutex);
                    m_stats.cacheMisses++;
                }
            }
            
            // Execute callback with result
            callback(entityId, path);
        };
        
        // Convert aiManagerPriority to TaskPriority
        HammerEngine::TaskPriority taskPriority = HammerEngine::TaskPriority::Normal;
        if (aiManagerPriority >= 8) taskPriority = HammerEngine::TaskPriority::High;
        else if (aiManagerPriority >= 6) taskPriority = HammerEngine::TaskPriority::Normal;
        else taskPriority = HammerEngine::TaskPriority::Low;
        
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

    // CRITICAL FIX: Restore cache functionality for immediate requests
    // Check cache first before expensive A* computation
    if (m_cache) {
        if (auto cachedPath = m_cache->findSimilarPath(start, goal, 64.0f)) {
            outPath = *cachedPath;
            
            // Update statistics for cache hit
            {
                std::lock_guard<std::mutex> lock(m_statsMutex);
                m_stats.completedRequests++;
                m_stats.cacheHits++;
            }
            
            return HammerEngine::PathfindingResult::SUCCESS;
        }
    }
    
    // Cache miss - compute path using intelligent hierarchical pathfinding
    float distance = (goal - start).length();
    HammerEngine::PathfindingResult result;
    
    if (distance > 512.0f) {
        // Long distance: Use hierarchical pathfinding for 10x speedup
        result = m_grid->findPathHierarchical(start, goal, outPath);
        
        // Log hierarchical pathfinding usage for monitoring
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.hierarchicalRequests++;
    }
    else {
        // Short distance: Use direct pathfinding for precision
        result = m_grid->findPath(start, goal, outPath);
        
        // Log direct pathfinding usage
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.directRequests++;
    }
    
    // Cache successful paths for future reuse
    if (result == HammerEngine::PathfindingResult::SUCCESS && !outPath.empty() && m_cache) {
        m_cache->cachePath(start, goal, outPath);
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
    
    // PathfindingScheduler handles its own cache clearing when needed
    GAMEENGINE_INFO("Grid rebuilt manually, PathfindingScheduler will handle cache invalidation");
}

void PathfinderManager::updateDynamicObstacles() {
    // Ensure grid is initialized (this also checks for active world)
    if (!ensureGridInitialized()) {
        return;
    }

    // CRITICAL FIX: Smart grid rebuilding to minimize cache invalidation
    // Only rebuild grid when world has actually changed
    static uint64_t lastWorldVersion = 0;
    static float lastRebuildTime = 0.0f;
    static float rebuildTimer = 0.0f;
    rebuildTimer += (1.0f/60.0f); // Approximate deltaTime
    
    // Check if world has changed by getting version from WorldManager
    auto& worldManager = WorldManager::Instance();
    uint64_t currentWorldVersion = worldManager.getWorldVersion();
    
    // Only rebuild if:
    // 1. World version changed (world actually modified), OR
    // 2. More than 30 seconds have passed (safety fallback)
    bool shouldRebuild = false;
    if (currentWorldVersion != lastWorldVersion) {
        shouldRebuild = true;
        GAMEENGINE_INFO("Grid rebuilt (world changed)");
    } else if ((rebuildTimer - lastRebuildTime) >= 30.0f) {
        shouldRebuild = true;
        GAMEENGINE_INFO("Grid rebuilt (30s safety fallback)");
    }
    
    if (shouldRebuild) {
        m_grid->rebuildFromWorld();
        lastWorldVersion = currentWorldVersion;
        lastRebuildTime = rebuildTimer;
        
        // Clear cache since grid changed
        if (m_cache) {
            m_cache->clear();
        }
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
    
    m_stats.activeThreads = m_activeThreadCount.load();
    
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
    // Statistics are now primarily managed by PathfindingScheduler
    // This method updates local manager stats
    
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.pendingRequests = 0; // No pending requests in direct async mode
    
    // Get grid statistics if available and update consolidated stats
    uint32_t totalRequests = 0;
    uint32_t successfulPaths = 0;
    uint32_t timeouts = 0;
    uint32_t invalidGoals = 0;
    float avgPathLength = 0.0f;
    float successRate = 0.0f;
    float timeoutRate = 0.0f;
    
    if (m_grid) {
        auto gridStats = m_grid->getStats();
        totalRequests = gridStats.totalRequests;
        successfulPaths = gridStats.successfulPaths;
        timeouts = gridStats.timeouts;
        invalidGoals = gridStats.invalidGoals;
        
        if (gridStats.successfulPaths > 0) {
            avgPathLength = static_cast<float>(gridStats.avgPathLength);
            m_stats.averagePathLength = avgPathLength;
        }
        
        if (totalRequests > 0) {
            successRate = (static_cast<float>(successfulPaths) / static_cast<float>(totalRequests)) * 100.0f;
            timeoutRate = (static_cast<float>(timeouts) / static_cast<float>(totalRequests)) * 100.0f;
        }
    }
    
    // Periodic consolidated status reporting (every 5 seconds, like AIManager)
    static auto lastDebugTime = std::chrono::steady_clock::now();
    auto currentTime = std::chrono::steady_clock::now();
    auto timeSinceLastDebug = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastDebugTime);
    
    if (timeSinceLastDebug.count() >= 5 && totalRequests > 0) {
        lastDebugTime = currentTime;
        
        // Get fresh cache statistics directly from cache before displaying
        if (m_cache) {
            auto cacheStats = m_cache->getStats();
            m_stats.cacheHits = static_cast<uint64_t>(cacheStats.totalHits);
            m_stats.cacheMisses = static_cast<uint64_t>(cacheStats.totalMisses);
            m_stats.cacheHitRate = cacheStats.hitRate;
        }
        
        // Get cache statistics  
        float cacheHitRate = m_stats.cacheHitRate * 100.0f;
        uint32_t cacheHits = m_stats.cacheHits;
        uint32_t cacheMisses = m_stats.cacheMisses;
        uint32_t activeThreads = m_stats.activeThreads;
        uint32_t pendingRequests = static_cast<uint32_t>(m_stats.pendingRequests);
        
        // Calculate hierarchical pathfinding ratio
        uint64_t hierarchicalRequests = m_stats.hierarchicalRequests;
        uint64_t directRequests = m_stats.directRequests;
        uint64_t totalPathRequests = hierarchicalRequests + directRequests;
        float hierarchicalRatio = (totalPathRequests > 0) ? 
            (100.0f * hierarchicalRequests) / totalPathRequests : 0.0f;
        
        PATHFIND_INFO("PathfinderManager Status - Requests: " + std::to_string(totalRequests) +
                        ", SUCCESS RATE: " + std::to_string(static_cast<int>(successRate)) + "%" +
                        ", TIMEOUT RATE: " + std::to_string(static_cast<int>(timeoutRate)) + "%" +
                        " (" + std::to_string(timeouts) + " timeouts)" +
                        ", Invalid: " + std::to_string(invalidGoals) +
                        ", Pending: " + std::to_string(pendingRequests) +
                        ", Cache Hit Rate: " + std::to_string(static_cast<int>(cacheHitRate)) + "%" +
                        " (" + std::to_string(cacheHits) + "/" + std::to_string(cacheHits + cacheMisses) + ")" +
                        ", Avg Path Length: " + std::to_string(static_cast<int>(avgPathLength)) + " nodes" +
                        ", Hierarchical: " + std::to_string(static_cast<int>(hierarchicalRatio)) + "%" +
                        " (" + std::to_string(hierarchicalRequests) + "/" + std::to_string(totalPathRequests) + ")" +
                        ", Active Threads: " + std::to_string(activeThreads));
    }
}

void PathfinderManager::cleanupCache() {
    // Cache cleanup is now handled by PathfindingScheduler
    // This method is kept for interface compatibility
    if (m_cache) {
        // Perform cache cleanup using PathCache directly
        m_cache->cleanup();
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