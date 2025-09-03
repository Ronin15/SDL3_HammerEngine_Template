/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "../ai/internal/PathCache.hpp"
#include "../ai/internal/PathfindingScheduler.hpp"
#include "../ai/internal/SpatialPriority.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/WorldManager.hpp"
#include "core/ThreadSystem.hpp"
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
        GAMEENGINE_INFO("Initializing PathfinderManager");

        // Don't initialize grid immediately - wait for world to be loaded
        // Grid will be created lazily when first needed and world is available
        float cellSize = 32.0f; // Default cell size, could be configurable
        m_cellSize = cellSize;

        // PathCache is now managed by PathfindingScheduler
        // m_cache = std::make_unique<AIInternal::PathCache>();

        // Initialize spatial priority system
        m_spatialPriority = std::make_unique<AIInternal::SpatialPriority>();

        // Initialize pathfinding scheduler
        m_scheduler = std::make_unique<AIInternal::PathfindingScheduler>();

        // Grid configuration will be applied when grid is created

        // Skip initial grid rebuild - will happen when world is loaded
        // rebuildGrid();

        m_initialized.store(true);
        GAMEENGINE_INFO("PathfinderManager initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        GAMEENGINE_ERROR("Failed to initialize PathfinderManager: " + std::string(e.what()));
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

    // Update scheduler (handles request distribution and processing)
    // Need player position - for now use origin, later get from game state
    if (m_scheduler) {
        Vector2D playerPos(0, 0); // TODO: Get actual player position
        m_scheduler->update(deltaTime, playerPos);
        
        // Process requests through scheduler using our grid
        auto requests = m_scheduler->extractPendingRequests(m_maxPathsPerFrame);
        if (!requests.empty()) {
            processSchedulerRequests(requests);
        }
    }

    // Update statistics
    updateStatistics();
}

void PathfinderManager::clean() {
    if (m_isShutdown) {
        return;
    }

    GAMEENGINE_INFO("Cleaning up PathfinderManager");

    // Cancel all pending requests (now handled by scheduler)
    if (m_scheduler) {
        // PathfindingScheduler will handle cleanup in its shutdown method
    }
    
    // Clear any remaining local request data
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        while (!m_requestQueue.empty()) {
            m_requestQueue.pop();
        }
        m_pendingRequests.clear();
    }

    // Wait for active threads to complete
    for (auto& task : m_activeTasks) {
        if (task.valid()) {
            task.wait();
        }
    }
    m_activeTasks.clear();

    // Clean up components
    if (m_scheduler) {
        m_scheduler->shutdown();
    }

    m_scheduler.reset();
    m_spatialPriority.reset();
    // m_cache is now managed by PathfindingScheduler
    m_grid.reset();

    m_initialized.store(false);
    GAMEENGINE_INFO("PathfinderManager cleaned up");
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

uint64_t PathfinderManager::requestPath(
    EntityID entityId,
    const Vector2D& start,
    const Vector2D& goal,
    AIInternal::PathPriority priority,
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback
) {
    if (!m_initialized.load() || m_isShutdown) {
        return 0;
    }

    uint64_t requestId = m_nextRequestId.fetch_add(1);
    
    AIInternal::PathRequest request(entityId, start, goal, priority, callback);
    request.requestTime = std::chrono::steady_clock::now().time_since_epoch().count();

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pendingRequests.insert(std::make_pair(requestId, request));
        m_requestQueue.push(request);
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        m_stats.totalRequests++;
        m_stats.pendingRequests = m_requestQueue.size();
    }

    return requestId;
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

    // PathfindingScheduler handles caching - this is immediate/blocking path computation
    // For immediate requests, we bypass caching and go direct to grid for reliability
    
    // Compute path directly using grid
    auto result = m_grid->findPath(start, goal, outPath);
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(m_statsMutex);
        if (result == HammerEngine::PathfindingResult::SUCCESS) {
            m_stats.completedRequests++;
        } else if (result == HammerEngine::PathfindingResult::TIMEOUT) {
            m_stats.timedOutRequests++;
        }
    }

    return result;
}

void PathfinderManager::cancelRequest(uint64_t requestId) {
    // Most cancellation is now handled by PathfindingScheduler
    // This method provides interface compatibility
    
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_pendingRequests.erase(requestId);
    }
    
    {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.cancelledRequests++;
    }
}

void PathfinderManager::cancelEntityRequests(EntityID entityId) {
    // Most cancellation is now handled by PathfindingScheduler
    // This method provides interface compatibility and cleans up local state
    
    int cancelled = 0;
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        
        auto it = m_pendingRequests.begin();
        while (it != m_pendingRequests.end()) {
            if (it->second.entityId == entityId) {
                it = m_pendingRequests.erase(it);
                cancelled++;
            } else {
                ++it;
            }
        }
    }
    
    if (cancelled > 0) {
        std::lock_guard<std::mutex> statsLock(m_statsMutex);
        m_stats.cancelledRequests += cancelled;
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

    // Reduce rebuild frequency - only rebuild every 5 seconds to prevent cache thrashing
    static float lastRebuildTime = 0.0f;
    static float rebuildTimer = 0.0f;
    rebuildTimer += (1.0f/60.0f); // Approximate deltaTime
    
    if (rebuildTimer - lastRebuildTime >= 5.0f) {  // Rebuild every 5 seconds max
        // First rebuild base grid from world
        m_grid->rebuildFromWorld();
        lastRebuildTime = rebuildTimer;
        
        GAMEENGINE_INFO("Grid rebuilt (periodic - every 5s to reduce cache invalidation)");
    }

    // Then integrate dynamic collision data
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
    
    // Get cache statistics from scheduler
    if (m_scheduler) {
        auto cacheStats = m_scheduler->getPathCacheStats();
        // Update our stats with the scheduler's cache stats
        uint64_t schedulerHits = static_cast<uint64_t>(cacheStats.totalHits);
        uint64_t schedulerMisses = static_cast<uint64_t>(cacheStats.totalMisses);
        
        // Use the scheduler's stats as the authoritative cache stats
        m_stats.cacheHits = schedulerHits;
        m_stats.cacheMisses = schedulerMisses;
        m_stats.cacheHitRate = cacheStats.hitRate;
    } else {
        // No scheduler available - calculate from local stats
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

// Method removed - functionality moved to processSchedulerRequests

void PathfinderManager::processSchedulerRequests(const std::vector<AIInternal::PathRequest>& requests) {
    if (requests.empty() || !m_scheduler) {
        return;
    }

    // Ensure grid is initialized before processing requests
    if (!ensureGridInitialized()) {
        return;
    }

    // Process each request directly using our grid
    for (const auto& request : requests) {
        std::vector<Vector2D> path;
        auto result = m_grid->findPath(request.start, request.goal, path);
        
        if (result == HammerEngine::PathfindingResult::SUCCESS) {
            // Store result in scheduler
            m_scheduler->storePathResult(request.entityId, path);
            
            // Call callback if provided
            if (request.callback) {
                request.callback(request.entityId, path);
            }
            
            // Update local stats
            std::lock_guard<std::mutex> lock(m_statsMutex);
            m_stats.completedRequests++;
        } else {
            // Store empty result for failed paths
            m_scheduler->storePathResult(request.entityId, std::vector<Vector2D>{});
            
            // Call callback with empty path
            if (request.callback) {
                request.callback(request.entityId, std::vector<Vector2D>{});
            }
            
            // Update local stats
            std::lock_guard<std::mutex> lock(m_statsMutex);
            if (result == HammerEngine::PathfindingResult::TIMEOUT) {
                m_stats.timedOutRequests++;
            }
        }
    }
}

// This method is no longer used - replaced by processSchedulerRequests
// Kept for potential future use or interface compatibility
void PathfinderManager::processRequestBatch(std::vector<AIInternal::PathRequest>& batch) {
    // Delegate to the new method
    processSchedulerRequests(batch);
}

void PathfinderManager::updateStatistics() {
    // Statistics are now primarily managed by PathfindingScheduler
    // This method updates local manager stats
    
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats.pendingRequests = m_requestQueue.size();
    
    // Get grid statistics if available
    if (m_grid) {
        auto gridStats = m_grid->getStats();
        if (gridStats.successfulPaths > 0) {
            m_stats.averagePathLength = static_cast<float>(gridStats.avgPathLength);
        }
    }
}

void PathfinderManager::cleanupCache() {
    // Cache cleanup is now handled by PathfindingScheduler
    // This method is kept for interface compatibility
    if (m_scheduler) {
        // Scheduler handles cache cleanup in its update() method
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
    int gridWidth = static_cast<int>(worldWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldHeight / m_cellSize);

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