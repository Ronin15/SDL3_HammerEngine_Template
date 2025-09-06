/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "../ai/internal/SpatialPriority.hpp" // for AIInternal::PathPriority enum
#include "managers/WorldManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>

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
        PATHFIND_INFO("Initializing PathfinderManager with clean architecture");

        // No queue needed - requests processed directly on ThreadSystem
        
        // Grid will be created lazily when first needed and world is available
        m_cellSize = 64.0f; // Optimized cell size for 4x performance improvement

        // Cache initialized in header with default expiration time
        
        PATHFIND_INFO("PathfinderManager initialized successfully");

        m_initialized.store(true);
        PATHFIND_INFO("PathfinderManager initialized successfully with clean architecture");
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

    // Check for grid updates and report statistics - reduced frequency to eliminate per-frame overhead
    
    // Grid updates every 5 seconds (300 frames at 60 FPS)
    if (++m_gridUpdateCounter >= 300) {
        m_gridUpdateCounter = 0;
        checkForGridUpdates(GRID_UPDATE_INTERVAL); // Pass fixed interval since we control timing
    }
    
    // Statistics every 10 seconds (600 frames at 60 FPS)
    if (++m_statsFrameCounter >= 600) {
        m_statsFrameCounter = 0;
        reportStatistics();
    }
}


void PathfinderManager::clean() {
    if (m_isShutdown) {
        return;
    }

    PATHFIND_INFO("Cleaning up PathfinderManager");

    // Clear cache (shutdown - can clear all at once since frame timing doesn't matter)
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_pathCache.clear();
    }

    // No queue to clear - using direct ThreadSystem processing

    // Clear grid
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

    // Generate unique request ID
    const uint64_t requestId = m_nextRequestId.fetch_add(1);
    
    m_enqueuedRequests.fetch_add(1, std::memory_order_relaxed);

    // Process directly on ThreadSystem - no queue overhead
    auto work = [this, entityId, start, goal, callback]() {
        std::vector<Vector2D> path;
        bool cacheHit = false;
        
        // Ultra-fast cache: check and swap in single operation
        uint64_t cacheKey = computeCacheKey(start, goal);
        
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_pathCache.find(cacheKey);
            if (it != m_pathCache.end()) {
                // Move from cache - cache entry becomes empty but stays for reuse
                path = std::move(it->second.path);
                cacheHit = true;
                m_cacheHits.fetch_add(1, std::memory_order_relaxed);
            } else {
                m_cacheMisses.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        // Compute path if cache miss
        if (!cacheHit) {
            HammerEngine::PathfindingResult result = findPathImmediate(start, goal, path);
        }
        
        // Always re-cache the path (whether from cache hit or new computation)
        if (!path.empty()) {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            
            // Simple cache size management - clear when full (better than expensive FIFO)
            if (m_pathCache.size() >= MAX_CACHE_ENTRIES) {
                m_pathCache.clear(); // O(1) operation, starts fresh
            }
            
            PathCacheEntry entry;
            entry.path = path; // Copy for cache
            m_pathCache[cacheKey] = std::move(entry);
        }
        
        // Update statistics - simplified success check
        if (!path.empty()) {
            m_completedRequests.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_failedRequests.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Update basic processing count (no timing overhead)
        m_processedCount.fetch_add(1, std::memory_order_relaxed);
        
        if (callback) {
            callback(entityId, path);
        }
    };

    // Submit pathfinding work directly to ThreadSystem
    HammerEngine::ThreadSystem::Instance().enqueueTask(
        work, 
        HammerEngine::TaskPriority::Normal, 
        "PathfindingComputation"
    );
    
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

    // Take a snapshot of the grid to avoid races with background rebuilds
    auto gridSnapshot = std::atomic_load(&m_grid);
    if (!gridSnapshot) {
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Determine which pathfinding algorithm to use based on distance
    float distance2 = (goal - start).dot(goal - start);
    HammerEngine::PathfindingResult result;
    
    if (distance2 > (1200.0f * 1200.0f)) {
        // Long distance - try hierarchical first
        result = gridSnapshot->findPathHierarchical(start, goal, outPath);
        
        // Fallback to direct if hierarchical fails
        if (result != HammerEngine::PathfindingResult::SUCCESS || outPath.empty()) {
            std::vector<Vector2D> directPath;
            auto directResult = gridSnapshot->findPath(start, goal, directPath);
            if (directResult == HammerEngine::PathfindingResult::SUCCESS && !directPath.empty()) {
                outPath = std::move(directPath);
                result = directResult;
            }
        }
    } else {
        // Short to medium distance - use direct pathfinding
        result = gridSnapshot->findPath(start, goal, outPath);
    }

    return result;
}

size_t PathfinderManager::getQueueSize() const {
    return 0; // No queue - direct ThreadSystem processing
}

bool PathfinderManager::hasPendingWork() const {
    return false; // No queue - work submitted directly to ThreadSystem
}

void PathfinderManager::rebuildGrid() {
    // Build a new grid from current world data and swap atomically
    auto& worldManager = WorldManager::Instance();
    if (!worldManager.hasActiveWorld()) {
        GAMEENGINE_DEBUG("Cannot rebuild grid - no active world");
        return;
    }

    int worldWidth = 0, worldHeight = 0;
    if (!worldManager.getWorldDimensions(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
        GAMEENGINE_WARN("Invalid world dimensions during rebuild");
        return;
    }

    const float TILE_SIZE = 32.0f;
    float worldPixelWidth = worldWidth * TILE_SIZE;
    float worldPixelHeight = worldHeight * TILE_SIZE;
    int gridWidth = static_cast<int>(worldPixelWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldPixelHeight / m_cellSize);
    if (gridWidth <= 0 || gridHeight <= 0) {
        GAMEENGINE_WARN("Calculated grid too small during rebuild");
        return;
    }

    try {
        auto newGrid = std::make_shared<HammerEngine::PathfindingGrid>(
            gridWidth, gridHeight, m_cellSize, Vector2D(0, 0)
        );
        newGrid->setAllowDiagonal(m_allowDiagonal);
        newGrid->setMaxIterations(m_maxIterations);
        newGrid->rebuildFromWorld();

        // Publish the new grid atomically
        std::atomic_store(&m_grid, newGrid);

        // Clear cache since grid changed
        {
            std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
            m_pathCache.clear();
        }

        GAMEENGINE_INFO("Grid rebuilt successfully");
    } catch (const std::exception& e) {
        GAMEENGINE_ERROR("Grid rebuild failed: " + std::string(e.what()));
    }
}

void PathfinderManager::updateDynamicObstacles() {
    // This method is called by GameEngine for periodic grid updates
    // We just check if a rebuild is needed and schedule it
    checkForGridUpdates(GRID_UPDATE_INTERVAL);
}

void PathfinderManager::addTemporaryWeightField(const Vector2D& center, float radius, float weight) {
    if (!ensureGridInitialized()) {
        return;
    }

    if (auto grid = std::atomic_load(&m_grid)) {
        grid->addWeightCircle(center, radius, weight);
    }
}

void PathfinderManager::clearWeightFields() {
    if (!ensureGridInitialized()) {
        return;
    }

    if (auto grid = std::atomic_load(&m_grid)) {
        grid->resetWeights(1.0f);
    }
}

void PathfinderManager::setAllowDiagonal(bool allow) {
    m_allowDiagonal = allow;
    auto grid = std::atomic_load(&m_grid);
    if (grid) {
        grid->setAllowDiagonal(allow);
    }
}

void PathfinderManager::setMaxIterations(int maxIterations) {
    m_maxIterations = std::max(100, maxIterations);
    auto grid = std::atomic_load(&m_grid);
    if (grid) {
        grid->setMaxIterations(m_maxIterations);
    }
}

void PathfinderManager::setMaxPathsPerFrame(int maxPaths) {
    m_maxRequestsPerUpdate = std::max(static_cast<size_t>(1), static_cast<size_t>(maxPaths));
}

void PathfinderManager::setCacheExpirationTime(float seconds) {
    m_cacheExpirationTime = std::max(1.0f, seconds);
}

PathfinderManager::PathfinderStats PathfinderManager::getStats() const {
    PathfinderStats stats{};
    
    // Manager-level statistics
    stats.totalRequests = m_enqueuedRequests.load(std::memory_order_relaxed);
    stats.queueSize = 0; // No queue - direct ThreadSystem processing
    stats.queueCapacity = 0; // No queue - direct ThreadSystem processing  
    stats.processorActive = true; // ThreadSystem based processing
    
    // Real statistics from pathfinding processing
    stats.completedRequests = m_completedRequests.load(std::memory_order_relaxed);
    stats.failedRequests = m_failedRequests.load(std::memory_order_relaxed);
    
    // No timing statistics to eliminate overhead
    stats.averageProcessingTimeMs = 0.0;
    stats.requestsPerSecond = 0.0;
    
    // Cache statistics
    stats.cacheHits = m_cacheHits.load(std::memory_order_relaxed);
    stats.cacheMisses = m_cacheMisses.load(std::memory_order_relaxed);
    stats.negativeHits = 0; // Not implemented
    
    // Cache size statistics
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        stats.cacheSize = m_pathCache.size();
    }
    stats.negativeCacheSize = 0;
    
    // Calculate approximate memory usage
    size_t gridMemory = 0;
    if (m_grid) {
        // Approximate grid memory: width * height * sizeof(cell data)
        gridMemory = m_grid->getWidth() * m_grid->getHeight() * 8; // ~8 bytes per cell
    }
    
    // Cache memory usage (approximate)
    size_t cacheMemory = stats.cacheSize * (sizeof(PathCacheEntry) + 50); // ~50 bytes per path estimate
    
    stats.memoryUsageKB = (gridMemory + cacheMemory) / 1024.0;
    
    // Calculate cache hit rate
    uint64_t totalCacheChecks = stats.cacheHits + stats.cacheMisses;
    if (totalCacheChecks > 0) {
        stats.cacheHitRate = static_cast<float>(stats.cacheHits) / static_cast<float>(totalCacheChecks);
    } else {
        stats.cacheHitRate = 0.0f;
    }
    
    return stats;
}

void PathfinderManager::resetStats() {
    m_enqueuedRequests.store(0, std::memory_order_relaxed);
    m_enqueueFailures.store(0, std::memory_order_relaxed);
    m_completedRequests.store(0, std::memory_order_relaxed);
    m_failedRequests.store(0, std::memory_order_relaxed);
    m_cacheHits.store(0, std::memory_order_relaxed);
    m_cacheMisses.store(0, std::memory_order_relaxed);
    m_processedCount.store(0, std::memory_order_relaxed);
    
    // Fast cache clear - unordered_map::clear() is O(1) for small caches
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_pathCache.clear(); // Fast operation for 512 entries
    }
    
    // No queue statistics to reset
}

Vector2D PathfinderManager::clampToWorldBounds(const Vector2D& position, float margin) const {
    // PERFORMANCE FIX: Use cached bounds instead of calling WorldManager every time
    // Cache the bounds during grid initialization and use fallback if not available
    
    // Use cached bounds from grid initialization if available
    if (m_grid) {
        // Get bounds from the pathfinding grid which are cached
        const float gridCellSize = 64.0f; // Match m_cellSize
        const float worldWidth = m_grid->getWidth() * gridCellSize;
        const float worldHeight = m_grid->getHeight() * gridCellSize;
        
        return Vector2D(
            std::clamp(position.getX(), margin, worldWidth - margin),
            std::clamp(position.getY(), margin, worldHeight - margin)
        );
    }
    
    // Fast fallback bounds - avoid calling WorldManager during AI updates
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
    const float radius2 = nodeRadius * nodeRadius;
    float dist2 = toNode.dot(toNode);

    if (dist2 <= radius2) {
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
        dist2 = toNode.dot(toNode);
    }

    if (dist2 > 0.01f) { // equivalent to previous 0.1f threshold squared
        // Move toward current path node
        float invLen = 1.0f / std::sqrt(dist2);
        Vector2D direction = toNode * invLen; // normalized
        entity->setVelocity(direction * speed);
        return true;
    }

    return false;
}

void PathfinderManager::reportStatistics() {
    auto stats = getStats();
    
    if (stats.totalRequests > 0) {
        PATHFIND_INFO("PathfinderManager Status - Total Requests: " + std::to_string(stats.totalRequests) +
                     ", Completed: " + std::to_string(stats.completedRequests) +
                     ", Failed: " + std::to_string(stats.failedRequests) +
                     ", Cache Hits: " + std::to_string(stats.cacheHits) +
                     ", Cache Misses: " + std::to_string(stats.cacheMisses) +
                     ", Hit Rate: " + std::to_string(static_cast<int>(stats.cacheHitRate * 100)) + "%" +
                     ", Cache Size: " + std::to_string(stats.cacheSize) +
                     ", Avg Time: " + std::to_string(stats.averageProcessingTimeMs) + "ms" +
                     ", RPS: " + std::to_string(static_cast<int>(stats.requestsPerSecond)) +
                     ", Memory: " + std::to_string(stats.memoryUsageKB) + " KB" +
                     ", ThreadSystem: " + (stats.processorActive ? "Active" : "Inactive"));
    }
}

bool PathfinderManager::ensureGridInitialized() {
    if (std::atomic_load(&m_grid)) {
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
        auto newGrid = std::make_shared<HammerEngine::PathfindingGrid>(
            gridWidth,
            gridHeight,
            m_cellSize,
            Vector2D(0, 0)
        );

        // Apply configuration settings
        newGrid->setAllowDiagonal(m_allowDiagonal);
        newGrid->setMaxIterations(m_maxIterations);

        // Build the grid from world data
        newGrid->rebuildFromWorld();

        std::atomic_store(&m_grid, newGrid);

        GAMEENGINE_INFO("Pathfinding grid initialized: " + 
                        std::to_string(gridWidth) + "x" + std::to_string(gridHeight) + 
                        " cells (world: " + std::to_string(worldWidth) + "x" + std::to_string(worldHeight) + 
                        ", cellSize: " + std::to_string(m_cellSize) + ")");
        
        return true;
    }
    catch (const std::exception& e) {
        GAMEENGINE_ERROR("Failed to create pathfinding grid: " + std::string(e.what()));
        std::atomic_store(&m_grid, std::shared_ptr<HammerEngine::PathfindingGrid>());
        return false;
    }
}

void PathfinderManager::checkForGridUpdates(float deltaTime) {
    m_timeSinceLastRebuild += deltaTime;
    
    // Only check for updates every GRID_UPDATE_INTERVAL seconds  
    if (m_timeSinceLastRebuild < GRID_UPDATE_INTERVAL) {
        return;
    }
    
    // Check if world has changed by getting version from WorldManager
    auto& worldManager = WorldManager::Instance();
    uint64_t currentWorldVersion = worldManager.getWorldVersion();
    
    // Rebuild when version changed or every 30 seconds as a fallback
    bool worldChanged = (currentWorldVersion != m_lastWorldVersion);
    bool shouldRebuild = worldChanged || (m_timeSinceLastRebuild >= 30.0f);
    
    if (shouldRebuild) {
        rebuildGrid();
        m_lastWorldVersion = currentWorldVersion;
        m_timeSinceLastRebuild = 0.0f;
    }
}

uint64_t PathfinderManager::computeCacheKey(const Vector2D& start, const Vector2D& goal) const {
    // Quantize positions to 128-pixel grid for better cache coherence
    // Larger quantization = more cache hits for nearby positions
    int sx = static_cast<int>(start.getX() / 128.0f);
    int sy = static_cast<int>(start.getY() / 128.0f);
    int gx = static_cast<int>(goal.getX() / 128.0f);
    int gy = static_cast<int>(goal.getY() / 128.0f);
    
    // Pack into 64-bit key: sx(16) | sy(16) | gx(16) | gy(16)
    return (static_cast<uint64_t>(sx & 0xFFFF) << 48) |
           (static_cast<uint64_t>(sy & 0xFFFF) << 32) |
           (static_cast<uint64_t>(gx & 0xFFFF) << 16) |
           static_cast<uint64_t>(gy & 0xFFFF);
}

// End of file - no legacy methods remain