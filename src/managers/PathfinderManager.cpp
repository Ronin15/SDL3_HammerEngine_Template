/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp" // Must include for HandlerToken definition
#include "events/CollisionObstacleChangedEvent.hpp"
#include "events/WorldEvent.hpp"
#include <string_view>
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>

PathfinderManager& PathfinderManager::Instance() {
    static PathfinderManager instance;
    return instance;
}

PathfinderManager::~PathfinderManager() {
    if (!m_isShutdown) {
        clean();
    }
}

// Internal priority mapping helpers (implementation-only)
namespace {
    inline HammerEngine::TaskPriority mapEnumToTaskPriority(PathfinderManager::Priority p) {
        switch (p) {
            case PathfinderManager::Priority::Critical: return HammerEngine::TaskPriority::Critical;
            case PathfinderManager::Priority::High:     return HammerEngine::TaskPriority::High;
            case PathfinderManager::Priority::Normal:   return HammerEngine::TaskPriority::Normal;
            case PathfinderManager::Priority::Low:      return HammerEngine::TaskPriority::Low;
            default:                                    return HammerEngine::TaskPriority::Normal;
        }
    }

    inline std::string_view priorityLabel(PathfinderManager::Priority p) {
        using namespace std::literals;
        switch (p) {
            case PathfinderManager::Priority::Critical: return "Critical"sv;
            case PathfinderManager::Priority::High:     return "High"sv;
            case PathfinderManager::Priority::Normal:   return "Normal"sv;
            case PathfinderManager::Priority::Low:      return "Low"sv;
            default:                                    return "Normal"sv;
        }
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

        // ThreadSystem initialization is managed by the application/tests
        
        // Subscribe to collision obstacle change events for cache invalidation
        subscribeToEvents();

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

void PathfinderManager::update() {
    if (!m_initialized.load() || m_isShutdown) {
        return;
    }

    // Event-driven architecture: Grid updates happen via World events (no polling needed)
    // Only report statistics periodically for monitoring

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
    
    // Unsubscribe from events
    unsubscribeFromEvents();

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

void PathfinderManager::prepareForStateTransition() {
    PATHFIND_INFO("Preparing PathfinderManager for state transition...");

    if (!m_initialized.load() || m_isShutdown) {
        PATHFIND_WARN("PathfinderManager not initialized or already shutdown during state transition");
        return;
    }

    // Clear path cache completely for fresh state
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        size_t cacheSize = m_pathCache.size();
        m_pathCache.clear();
        if (cacheSize > 0) {
            PATHFIND_DEBUG("Cleared " + std::to_string(cacheSize) + " cached paths");
        }
    }
    
    // Clear pending requests to avoid callbacks to old game state
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        size_t pendingSize = m_pending.size();
        m_pending.clear();
        if (pendingSize > 0) {
            PATHFIND_DEBUG("Cleared " + std::to_string(pendingSize) + " pending requests");
        }
    }
    
    // Reset statistics for clean slate
    m_enqueuedRequests.store(0);
    m_enqueueFailures.store(0);
    m_completedRequests.store(0);
    m_failedRequests.store(0);
    m_cacheHits.store(0);
    m_cacheMisses.store(0);
    m_processedCount.store(0);
    m_lastRequestsPerSecond = 0.0;
    m_lastTotalRequests = 0;

    // Reset statistics frame counter
    m_statsFrameCounter = 0;

    // Reset collision version tracking
    m_lastCollisionVersion.store(0);
    
    // Keep grid instance but invalidate any cached data within it
    // Grid will be rebuilt when needed by new state
    if (m_grid) {
        // Clear any temporary weight fields that might be state-specific
        clearWeightFields();
    }
    
    // Re-subscribe to events to ensure fresh event handlers for new state
    unsubscribeFromEvents();
    subscribeToEvents();
    
    PATHFIND_INFO("PathfinderManager state transition complete - cleared transient data, kept manager initialized");
}


bool PathfinderManager::isShutdown() const {
    return m_isShutdown;
}

uint64_t PathfinderManager::requestPath(
    EntityID entityId,
    const Vector2D& start,
    const Vector2D& goal,
    Priority priority,
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback
) {
    if (!m_initialized.load() || m_isShutdown) {
        return 0;
    }

    // Normalize endpoints (clamp/snap/quantize)
    Vector2D nStart = start;
    Vector2D nGoal = goal;
    normalizeEndpoints(nStart, nGoal);

    uint64_t cacheKey = computeCacheKey(nStart, nGoal);

    // Generate unique request ID
    const uint64_t requestId = m_nextRequestId.fetch_add(1);
    m_enqueuedRequests.fetch_add(1, std::memory_order_relaxed);

    // Coalesce in-flight requests
    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        auto it = m_pending.find(cacheKey);
        if (it != m_pending.end()) {
            if (callback) it->second.callbacks.push_back(callback);
            return requestId; // Will be fulfilled when the in-flight request completes
        } else {
            PendingCallbacks pc;
            if (callback) pc.callbacks.push_back(callback);
            m_pending.emplace(cacheKey, std::move(pc));
        }
    }

    // Process directly on ThreadSystem - no queue overhead
    auto work = [this, entityId, nStart, nGoal, cacheKey]() {
        std::vector<Vector2D> path;
        bool cacheHit = false;
        
        // Fast cache lookup - primary cache only for maximum performance
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            auto it = m_pathCache.find(cacheKey);
            if (it != m_pathCache.end()) {
                // Cache hit - update usage tracking
                path = it->second.path;
                it->second.lastUsed = std::chrono::steady_clock::now();
                it->second.useCount++;
                cacheHit = true;
                m_cacheHits.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Cache miss - will need full pathfinding
                m_cacheMisses.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        // Compute path if cache miss
        if (!cacheHit) {
            findPathImmediate(nStart, nGoal, path);
        }
        
        // Cache path and update statistics
        if (!path.empty()) {
            // Cache the successful path
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                
                // Smart cache management with LRU eviction
                if (m_pathCache.size() >= MAX_CACHE_ENTRIES) {
                    evictOldestCacheEntry();
                }
                
                // Cache the full path
                PathCacheEntry entry;
                entry.path = path;
                entry.lastUsed = std::chrono::steady_clock::now();
                entry.useCount = 1;
                m_pathCache[cacheKey] = std::move(entry);
            }
            
            // Update success statistics
            m_completedRequests.fetch_add(1, std::memory_order_relaxed);
        } else {
            // Update failure statistics
            m_failedRequests.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Update basic processing count (no timing overhead)
        m_processedCount.fetch_add(1, std::memory_order_relaxed);
        
        // Fan out to all pending callbacks for this key
        std::vector<PathCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto it = m_pending.find(cacheKey);
            if (it != m_pending.end()) {
                callbacks.swap(it->second.callbacks);
                m_pending.erase(it);
            }
        }
        for (const auto &cb : callbacks) {
            if (cb) cb(entityId, path);
        }
    };

    // Submit pathfinding work to ThreadSystem with mapped priority
    const auto taskPri = mapEnumToTaskPriority(priority);
    const auto priLabel = priorityLabel(priority);
    std::string taskDesc;
    taskDesc.reserve(24 + priLabel.size());
    taskDesc = "PathfindingComputation/";
    taskDesc.append(priLabel);
    HammerEngine::ThreadSystem::Instance().enqueueTask(
        work,
        taskPri,
        taskDesc
    );
    
    return requestId;
}

// (Backward-compat overload removed to keep public API minimal during development)

HammerEngine::PathfindingResult PathfinderManager::findPathImmediate(
    const Vector2D& start,
    const Vector2D& goal,
    std::vector<Vector2D>& outPath
) {
    if (!m_initialized.load() || m_isShutdown) {
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Start timing for performance statistics
    auto startTime = std::chrono::steady_clock::now();

    // Normalize endpoints for safe and cache-friendly pathfinding
    Vector2D nStart = start;
    Vector2D nGoal = goal;
    normalizeEndpoints(nStart, nGoal);

    // Ensure grid is initialized before pathfinding
    if (!ensureGridInitialized()) {
        // Record timing even for failed requests
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        m_totalProcessingTimeMs.fetch_add(duration.count() / 1000.0, std::memory_order_relaxed);
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Take a snapshot of the grid to avoid races with background rebuilds
    auto gridSnapshot = std::atomic_load(&m_grid);
    if (!gridSnapshot) {
        // Record timing even for failed requests
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        m_totalProcessingTimeMs.fetch_add(duration.count() / 1000.0, std::memory_order_relaxed);
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Determine which pathfinding algorithm to use based on sophisticated heuristics
    HammerEngine::PathfindingResult result;
    
    if (gridSnapshot->shouldUseHierarchicalPathfinding(nStart, nGoal)) {
        // Use hierarchical pathfinding - try hierarchical first
        result = gridSnapshot->findPathHierarchical(nStart, nGoal, outPath);
        
        // Fallback to direct if hierarchical fails
        if (result != HammerEngine::PathfindingResult::SUCCESS || outPath.empty()) {
            std::vector<Vector2D> directPath;
            auto directResult = gridSnapshot->findPath(nStart, nGoal, directPath);
            if (directResult == HammerEngine::PathfindingResult::SUCCESS && !directPath.empty()) {
                outPath = std::move(directPath);
                result = directResult;
            }
        }
    } else {
        // Use direct pathfinding for optimal performance
        result = gridSnapshot->findPath(nStart, nGoal, outPath);
    }

    // Record timing for all completed requests
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    m_totalProcessingTimeMs.fetch_add(duration.count() / 1000.0, std::memory_order_relaxed);
    
    return result;
}

size_t PathfinderManager::getQueueSize() const {
    return 0; // No queue - direct ThreadSystem processing
}

bool PathfinderManager::hasPendingWork() const {
    return false; // No queue - work submitted directly to ThreadSystem
}

void PathfinderManager::rebuildGrid() {
    // ASYNC OPTIMIZATION: Submit grid rebuild to background thread to eliminate main thread spikes
    // The expensive rebuildFromWorld() operation (2-3ms for 500x500 grid) runs on ThreadSystem
    // Grid is swapped atomically when complete, so pathfinding continues with old grid briefly

    const auto& worldManager = WorldManager::Instance();
    if (!worldManager.hasActiveWorld()) {
        PATHFIND_DEBUG("Cannot rebuild grid - no active world");
        return;
    }

    int worldWidth = 0, worldHeight = 0;
    if (!worldManager.getWorldDimensions(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
        PATHFIND_WARN("Invalid world dimensions during rebuild");
        return;
    }

    float worldPixelWidth = worldWidth * HammerEngine::TILE_SIZE;
    float worldPixelHeight = worldHeight * HammerEngine::TILE_SIZE;
    int gridWidth = static_cast<int>(worldPixelWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldPixelHeight / m_cellSize);
    if (gridWidth <= 0 || gridHeight <= 0) {
        PATHFIND_WARN("Calculated grid too small during rebuild");
        return;
    }

    // Capture rebuild parameters for async execution
    float cellSize = m_cellSize;
    bool allowDiagonal = m_allowDiagonal;
    int maxIterations = m_maxIterations;

    // Submit rebuild task to ThreadSystem (background thread)
    HammerEngine::ThreadSystem::Instance().enqueueTask(
        [gridWidth, gridHeight, cellSize, allowDiagonal, maxIterations, this]() {
            try {
                // Create and build new grid on background thread
                auto newGrid = std::make_shared<HammerEngine::PathfindingGrid>(
                    gridWidth, gridHeight, cellSize, Vector2D(0, 0)
                );
                newGrid->setAllowDiagonal(allowDiagonal);
                newGrid->setMaxIterations(maxIterations);
                newGrid->rebuildFromWorld(); // EXPENSIVE: 2-3ms, now off main thread!

                // Atomically swap the grid (thread-safe)
                std::atomic_store(&m_grid, newGrid);

                // Smart cache invalidation: Clear only 50% oldest entries (mutex-protected)
                // SAFETY: Check if manager still initialized before touching cache
                // This prevents race with state transition or shutdown
                if (m_initialized.load(std::memory_order_acquire)) {
                    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
                    clearOldestCacheEntries(0.5f); // Remove 50% oldest paths
                    PATHFIND_INFO("Grid rebuilt successfully (async, partial cache clear)");

                    // Now that grid is ready, calculate optimal cache settings and pre-warm
                    // These must happen AFTER grid rebuild completes
                    calculateOptimalCacheSettings();
                    prewarmPathCache();
                } else {
                    PATHFIND_DEBUG("Grid rebuilt but manager shutting down, skipped cache clear");
                }
            } catch (const std::exception& e) {
                PATHFIND_ERROR("Async grid rebuild failed: " + std::string(e.what()));
            }
        },
        HammerEngine::TaskPriority::Low, // Low priority - doesn't block gameplay
        "PathfindingGridRebuild"
    );

    PATHFIND_DEBUG("Grid rebuild submitted to background thread");
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
    
    // Calculate average processing time and requests per second
    uint64_t totalRequests = stats.completedRequests + stats.failedRequests;
    if (totalRequests > 0) {
        double totalTimeMs = m_totalProcessingTimeMs.load(std::memory_order_relaxed);
        stats.averageProcessingTimeMs = totalTimeMs / totalRequests;
    } else {
        stats.averageProcessingTimeMs = 0.0;
    }
    
    // Calculate requests per second using time since last stats update
    auto now = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatsUpdate);
    if (timeDiff.count() > 1000) { // Update RPS every second
        double secondsSinceLastUpdate = timeDiff.count() / 1000.0;
        uint64_t currentTotalRequests = m_enqueuedRequests.load(std::memory_order_relaxed);

        if (m_lastStatsUpdate != std::chrono::steady_clock::time_point{}) {
            // Calculate RPS based on request DELTA since last update (not total)
            // Handle counter resets: if currentTotal < lastTotal, counters were reset
            int64_t deltaRequests = 0;
            if (currentTotalRequests >= m_lastTotalRequests) {
                deltaRequests = static_cast<int64_t>(currentTotalRequests - m_lastTotalRequests);
            } else {
                // Counter was reset, use current value as delta
                deltaRequests = static_cast<int64_t>(currentTotalRequests);
            }
            m_lastRequestsPerSecond = (deltaRequests > 0) ? (deltaRequests / secondsSinceLastUpdate) : 0.0;
        }

        m_lastTotalRequests = currentTotalRequests;
        m_lastStatsUpdate = now;
    }
    stats.requestsPerSecond = m_lastRequestsPerSecond;
    
    // Cache statistics
    stats.cacheHits = m_cacheHits.load(std::memory_order_relaxed);
    stats.cacheMisses = m_cacheMisses.load(std::memory_order_relaxed);

    // Cache size statistics
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        stats.cacheSize = m_pathCache.size();
        stats.segmentCacheSize = 0; // Segment cache removed for performance
    }

    // Calculate approximate memory usage
    size_t gridMemory = 0;
    if (m_grid) {
        // Approximate grid memory: width * height * sizeof(cell data)
        gridMemory = m_grid->getWidth() * m_grid->getHeight() * 8; // ~8 bytes per cell
    }
    
    // Cache memory usage (approximate)
    size_t cacheMemory = stats.cacheSize * (sizeof(PathCacheEntry) + 50) + 
                        0; // Segment cache removed for performance
    
    stats.memoryUsageKB = (gridMemory + cacheMemory) / 1024.0;
    
    // Calculate cache hit rates
    uint64_t totalCacheChecks = stats.cacheHits + stats.cacheMisses;
    if (totalCacheChecks > 0) {
        stats.cacheHitRate = static_cast<float>(stats.cacheHits) / static_cast<float>(totalCacheChecks);
        stats.totalHitRate = stats.cacheHitRate;
    } else {
        stats.cacheHitRate = 0.0f;
        stats.totalHitRate = 0.0f;
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
    m_lastRequestsPerSecond = 0.0;
    m_lastTotalRequests = 0;
    
    // Fast cache clear - unordered_map::clear() is O(1) for small caches
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_pathCache.clear(); // Fast operation for cache entries
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
    
    // No grid available - world not loaded yet, return position as-is
    return position;
}

bool PathfinderManager::getCachedWorldBounds(float& outWidth, float& outHeight) const {
    auto grid = std::atomic_load(&m_grid);
    if (grid) {
        const float gridCellSize = 64.0f;
        outWidth = grid->getWidth() * gridCellSize;
        outHeight = grid->getHeight() * gridCellSize;
        return true;
    }
    // No grid available - world not loaded yet
    return false;
}

Vector2D PathfinderManager::clampInsideExtents(const Vector2D& position, float halfW, float halfH, float extraMargin) const {
    auto grid = std::atomic_load(&m_grid);
    if (grid) {
        const float gridCellSize = 64.0f;
        const float worldWidth = grid->getWidth() * gridCellSize;
        const float worldHeight = grid->getHeight() * gridCellSize;
        float minX = halfW + extraMargin;
        float minY = halfH + extraMargin;
        float maxX = worldWidth - halfW - extraMargin;
        float maxY = worldHeight - halfH - extraMargin;
        return Vector2D(
            std::clamp(position.getX(), minX, maxX),
            std::clamp(position.getY(), minY, maxY)
        );
    }
    // No grid available - world not loaded yet, return position as-is
    return position;
}

Vector2D PathfinderManager::adjustSpawnToNavigable(const Vector2D& desired, float halfW, float halfH, float interiorMargin) const {
    Vector2D pos = clampInsideExtents(desired, halfW, halfH, interiorMargin);
    auto grid = std::atomic_load(&m_grid);
    if (grid) {
        // Snap within ~2 cells
        Vector2D snapped = grid->snapToNearestOpenWorld(pos, grid->getCellSize() * 2.0f);
        // Prefer interior if snapped remains blocked (defensive)
        if (!grid->isWorldBlocked(snapped)) return snapped;
    }
    // Fallback: pull to center a bit
    const auto &wm = WorldManager::Instance();
    float minX=0, minY=0, maxX=0, maxY=0;
    if (wm.getWorldBounds(minX, minY, maxX, maxY)) {
        Vector2D center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
        Vector2D dir = center - pos;
        if (dir.length() > 0.001f) {
            dir.normalize();
            return clampInsideExtents(pos + dir * 256.0f, halfW, halfH, interiorMargin);
        }
    }
    return pos;
}

[[maybe_unused]] static inline bool pointInRect(const Vector2D& p, float minX, float minY, float maxX, float maxY) {
    return p.getX() >= minX && p.getX() <= maxX && p.getY() >= minY && p.getY() <= maxY;
}

Vector2D PathfinderManager::adjustSpawnToNavigableInRect(const Vector2D& desired,
                                                         float halfW, float halfH,
                                                         float interiorMargin,
                                                         float minX, float minY,
                                                         float maxX, float maxY) const {
    // Clamp area by extents + interior margin
    float aminX = minX + halfW + interiorMargin;
    float aminY = minY + halfH + interiorMargin;
    float amaxX = maxX - halfW - interiorMargin;
    float amaxY = maxY - halfH - interiorMargin;

    Vector2D pos = clampInsideExtents(desired, halfW, halfH, interiorMargin);
    // Clamp to area rect
    pos.setX(std::clamp(pos.getX(), aminX, amaxX));
    pos.setY(std::clamp(pos.getY(), aminY, amaxY));

    if (auto grid = std::atomic_load(&m_grid)) {
        // Try snap within area (rings of ~cell size)
        float cell = grid->getCellSize();
        for (int r = 0; r <= 2; ++r) {
            float rad = (r+1) * cell;
            for (int i = 0; i < 16; ++i) {
                float ang = static_cast<float>(i) * (static_cast<float>(M_PI) * 2.0f / 16.0f);
                Vector2D cand = Vector2D(pos.getX() + std::cos(ang) * rad,
                                         pos.getY() + std::sin(ang) * rad);
                // Keep inside area
                cand.setX(std::clamp(cand.getX(), aminX, amaxX));
                cand.setY(std::clamp(cand.getY(), aminY, amaxY));
                if (!grid->isWorldBlocked(cand)) return cand;
            }
        }
    }
    return pos;
}

Vector2D PathfinderManager::adjustSpawnToNavigableInCircle(const Vector2D& desired,
                                                           float halfW, float halfH,
                                                           float interiorMargin,
                                                           const Vector2D& center,
                                                           float radius) const {
    float effectiveR = std::max(0.0f, radius - std::max(halfW, halfH) - interiorMargin);
    Vector2D pos = clampInsideExtents(desired, halfW, halfH, interiorMargin);
    Vector2D to = pos - center;
    float d = to.length();
    if (d > effectiveR && d > 0.001f) {
        to = to * (effectiveR / d);
        pos = center + to;
    }
    if (auto grid = std::atomic_load(&m_grid)) {
        float cell = grid->getCellSize();
        for (int r = 0; r <= 2; ++r) {
            float rad = (r+1) * cell;
            for (int i = 0; i < 16; ++i) {
                float ang = static_cast<float>(i) * (static_cast<float>(M_PI) * 2.0f / 16.0f);
                Vector2D cand = Vector2D(pos.getX() + std::cos(ang) * rad,
                                         pos.getY() + std::sin(ang) * rad);
                // Project back to circle if outside
                Vector2D tc = cand - center;
                float cd = tc.length();
                if (cd > effectiveR && cd > 0.001f) {
                    tc = tc * (effectiveR / cd);
                    cand = center + tc;
                }
                if (!grid->isWorldBlocked(cand)) return cand;
            }
        }
    }
    return pos;
}

void PathfinderManager::normalizeEndpoints(Vector2D& start, Vector2D& goal) const {
    // Clamp to world bounds with a modest interior margin
    const float margin = 96.0f;
    start = clampToWorldBounds(start, margin);
    goal = clampToWorldBounds(goal, margin);

    // Snap to nearest open cells if grid available
    if (auto grid = std::atomic_load(&m_grid)) {
        float r = grid->getCellSize() * 2.0f;
        start = grid->snapToNearestOpenWorld(start, r);
        goal = grid->snapToNearestOpenWorld(goal, r);
    }

    // Quantize to improve cache hits - use dynamic quantization scaled to world size
    start = Vector2D(std::round(start.getX() / m_endpointQuantization) * m_endpointQuantization,
                     std::round(start.getY() / m_endpointQuantization) * m_endpointQuantization);
    goal = Vector2D(std::round(goal.getX() / m_endpointQuantization) * m_endpointQuantization,
                    std::round(goal.getY() / m_endpointQuantization) * m_endpointQuantization);
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

void PathfinderManager::reportStatistics() const {
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

    // Reset per-cycle counters for next reporting window (every 600 frames / 10 seconds)
    m_enqueuedRequests.store(0, std::memory_order_relaxed);
    m_completedRequests.store(0, std::memory_order_relaxed);
    m_failedRequests.store(0, std::memory_order_relaxed);
    m_cacheHits.store(0, std::memory_order_relaxed);
    m_cacheMisses.store(0, std::memory_order_relaxed);
    m_totalProcessingTimeMs.store(0.0, std::memory_order_relaxed);
}

bool PathfinderManager::ensureGridInitialized() {
    if (std::atomic_load(&m_grid)) {
        return true; // Grid already exists
    }

    // Check if we have an active world to get dimensions from
    const auto& worldManager = WorldManager::Instance();
    if (!worldManager.hasActiveWorld()) {
        PATHFIND_DEBUG("Cannot initialize pathfinding grid - no active world");
        return false;
    }

    // Get world dimensions
    int worldWidth = 0;
    int worldHeight = 0;
    if (!worldManager.getWorldDimensions(worldWidth, worldHeight)) {
        PATHFIND_WARN("Cannot get world dimensions for pathfinding grid initialization");
        return false;
    }

    // Validate dimensions to prevent 0x0 grids
    if (worldWidth <= 0 || worldHeight <= 0) {
        PATHFIND_WARN("Invalid world dimensions for pathfinding grid: " + 
                        std::to_string(worldWidth) + "x" + std::to_string(worldHeight));
        return false;
    }

    // Calculate grid dimensions in cells
    float worldPixelWidth = worldWidth * HammerEngine::TILE_SIZE;
    float worldPixelHeight = worldHeight * HammerEngine::TILE_SIZE;
    int gridWidth = static_cast<int>(worldPixelWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldPixelHeight / m_cellSize);

    // Ensure minimum grid size
    if (gridWidth <= 0 || gridHeight <= 0) {
        PATHFIND_WARN("Calculated grid dimensions too small: " + 
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

        PATHFIND_INFO("Pathfinding grid initialized: " +
                        std::to_string(gridWidth) + "x" + std::to_string(gridHeight) +
                        " cells (world: " + std::to_string(worldWidth) + "x" + std::to_string(worldHeight) +
                        ", cellSize: " + std::to_string(m_cellSize) + ")");

        // Auto-calculate optimal cache settings for this world size
        calculateOptimalCacheSettings();

        return true;
    }
    catch (const std::exception& e) {
        PATHFIND_ERROR("Failed to create pathfinding grid: " + std::string(e.what()));
        std::atomic_store(&m_grid, std::shared_ptr<HammerEngine::PathfindingGrid>());
        return false;
    }
}

uint64_t PathfinderManager::computeCacheKey(const Vector2D& start, const Vector2D& goal) const {
    // Dynamic quantization scaled to world size for optimal cache coverage
    // Automatically calculated to ensure cache buckets ≈ sqrt(MAX_CACHE_ENTRIES)
    int sx = static_cast<int>(start.getX() / m_cacheKeyQuantization);
    int sy = static_cast<int>(start.getY() / m_cacheKeyQuantization);
    int gx = static_cast<int>(goal.getX() / m_cacheKeyQuantization);
    int gy = static_cast<int>(goal.getY() / m_cacheKeyQuantization);

    // Pack into 64-bit key: sx(16) | sy(16) | gx(16) | gy(16)
    return (static_cast<uint64_t>(sx & 0xFFFF) << 48) |
           (static_cast<uint64_t>(sy & 0xFFFF) << 32) |
           (static_cast<uint64_t>(gx & 0xFFFF) << 16) |
           static_cast<uint64_t>(gy & 0xFFFF);
}


void PathfinderManager::evictOldestCacheEntry() {
    if (m_pathCache.empty()) return;

    auto oldest = std::min_element(m_pathCache.begin(), m_pathCache.end(),
                                   [](const auto& a, const auto& b) {
                                       return a.second.lastUsed < b.second.lastUsed;
                                   });
    m_pathCache.erase(oldest);
}

void PathfinderManager::clearOldestCacheEntries(float percentage) {
    if (m_pathCache.empty() || percentage <= 0.0f) return;

    // Clamp percentage to [0, 1]
    percentage = std::min(1.0f, std::max(0.0f, percentage));

    size_t numToRemove = static_cast<size_t>(m_pathCache.size() * percentage);
    if (numToRemove == 0) return;

    // Build vector of entries sorted by lastUsed (oldest first)
    std::vector<std::pair<uint64_t, std::chrono::steady_clock::time_point>> entries;
    entries.reserve(m_pathCache.size());

    for (const auto& [key, entry] : m_pathCache) {
        entries.emplace_back(key, entry.lastUsed);
    }

    // Partial sort to find the oldest N entries (faster than full sort)
    std::partial_sort(entries.begin(), entries.begin() + numToRemove, entries.end(),
                     [](const auto& a, const auto& b) {
                         return a.second < b.second; // Oldest first
                     });

    // Remove the oldest entries
    for (size_t i = 0; i < numToRemove; ++i) {
        m_pathCache.erase(entries[i].first);
    }

    PATHFIND_DEBUG("Cleared " + std::to_string(numToRemove) + " oldest cache entries (" +
                   std::to_string(static_cast<int>(percentage * 100)) + "%)");
}

void PathfinderManager::clearAllCache() {
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    size_t clearedCount = m_pathCache.size();
    m_pathCache.clear();
    PATHFIND_INFO("Cleared all cache entries: " + std::to_string(clearedCount) + " paths removed");
}

void PathfinderManager::calculateOptimalCacheSettings() {
    if (!m_grid) {
        PATHFIND_WARN("Cannot calculate cache settings - no grid available");
        return;
    }

    float worldW = m_grid->getWidth() * m_cellSize;
    float worldH = m_grid->getHeight() * m_cellSize;
    float diagonal = std::sqrt(worldW * worldW + worldH * worldH);

    // ADAPTIVE QUANTIZATION: Single unified quantization for correctness

    // Endpoint quantization: Conservative scaling for ACCURACY (minimize path failures)
    // 0.5% of world size with strict 256px cap to keep quantization fine-grained
    // This prevents entities from snapping to blocked cells
    m_endpointQuantization = std::clamp(worldW / 200.0f, 128.0f, 256.0f);

    // BUGFIX: Cache key quantization MUST match endpoint quantization to prevent
    // coalescing requests with different normalized goals. Previously, cache key was
    // 4-8x coarser than endpoint quantization, causing coalesced entities to receive
    // paths to wrong destinations (off by 160-512px from their actual normalized goal).
    // Example bug scenario with old values (endpoint=160px, cache=1280px):
    //   Entity A requests path to (5920, 6720) [after endpoint quantization]
    //   Entity B requests path to (6080, 6720) [after endpoint quantization]
    //   Both get same cache key due to coarse cache quantization
    //   Entity B receives path to (5920, 6720) instead of (6080, 6720) - 160px error!
    m_cacheKeyQuantization = m_endpointQuantization;

    // ADAPTIVE THRESHOLDS
    m_hierarchicalThreshold = diagonal * 0.05f;      // 5% of world diagonal
    m_connectivityThreshold = worldW * 0.25f;        // 25% of world width

    // ADAPTIVE PRE-WARMING (8-connected sector graph)
    // Small worlds (< 16K): 4×4 sectors = 56 paths
    // Medium worlds (32K): 8×8 sectors = 210 paths
    // Large worlds (64K+): 16×16 sectors = 930 paths
    if (worldW < 16000.0f) {
        m_prewarmSectorCount = 4;
    } else if (worldW < 48000.0f) {
        m_prewarmSectorCount = 8;
    } else {
        m_prewarmSectorCount = 16;
    }
    // Calculate actual path count for 8-connected grid: 2×N×(N-1) + 2×(N-1)²
    // For N=8: 2×8×7 + 2×7² = 112 + 98 = 210 paths
    int N = m_prewarmSectorCount;
    m_prewarmPathCount = 2 * N * (N - 1) + 2 * (N - 1) * (N - 1);

    // Calculate expected cache bucket count for logging
    int bucketsX = static_cast<int>(worldW / m_cacheKeyQuantization);
    int bucketsY = static_cast<int>(worldH / m_cacheKeyQuantization);
    int totalBuckets = bucketsX * bucketsY;
    float cacheEfficiency = (static_cast<float>(MAX_CACHE_ENTRIES) / static_cast<float>(totalBuckets)) * 100.0f;

    PATHFIND_INFO("Auto-tuned cache settings for " +
                  std::to_string(static_cast<int>(worldW)) + "×" +
                  std::to_string(static_cast<int>(worldH)) + "px world:");
    PATHFIND_INFO("  Endpoint quantization: " +
                  std::to_string(static_cast<int>(m_endpointQuantization)) + "px (" +
                  std::to_string(static_cast<int>((m_endpointQuantization / worldW) * 100.0f * 10.0f) / 10.0f) + "% world)");
    PATHFIND_INFO("  Cache key quantization: " +
                  std::to_string(static_cast<int>(m_cacheKeyQuantization)) + "px");
    PATHFIND_INFO("  Expected cache buckets: " +
                  std::to_string(bucketsX) + "×" + std::to_string(bucketsY) +
                  " = " + std::to_string(totalBuckets) + " total");
    PATHFIND_INFO("  Cache efficiency: " +
                  std::to_string(static_cast<int>(cacheEfficiency)) + "% coverage");
    PATHFIND_INFO("  Hierarchical threshold: " +
                  std::to_string(static_cast<int>(m_hierarchicalThreshold)) + "px");
    PATHFIND_INFO("  Pre-warm sectors: " +
                  std::to_string(m_prewarmSectorCount) + "×" +
                  std::to_string(m_prewarmSectorCount) +
                  " = " + std::to_string(m_prewarmPathCount) + " paths");
}

void PathfinderManager::prewarmPathCache() {
    // Use compare_exchange to ensure only one thread executes pre-warming
    bool expected = false;
    if (!m_prewarming.compare_exchange_strong(expected, true) || !m_grid) {
        return;
    }

    float worldW = m_grid->getWidth() * m_cellSize;
    float worldH = m_grid->getHeight() * m_cellSize;
    int sectors = m_prewarmSectorCount;
    float sectorW = worldW / static_cast<float>(sectors);
    float sectorH = worldH / static_cast<float>(sectors);

    PATHFIND_INFO("Pre-warming cache with " +
                  std::to_string(m_prewarmPathCount) + " sector-based paths (world: " +
                  std::to_string(static_cast<int>(worldW)) + "×" +
                  std::to_string(static_cast<int>(worldH)) + "px, sectors: " +
                  std::to_string(sectors) + "×" + std::to_string(sectors) + ")...");

    // Generate paths between sector centers
    std::vector<std::pair<Vector2D, Vector2D>> seedPaths;
    seedPaths.reserve(m_prewarmPathCount);

    for (int sy = 0; sy < sectors; sy++) {
        for (int sx = 0; sx < sectors; sx++) {
            Vector2D sectorCenter(
                (static_cast<float>(sx) + 0.5f) * sectorW,
                (static_cast<float>(sy) + 0.5f) * sectorH
            );

            // Connect to adjacent and diagonal sectors (8-connectivity)
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue; // Skip self

                    int nx = sx + dx;
                    int ny = sy + dy;

                    // Only connect to valid neighbors within bounds
                    if (nx >= 0 && nx < sectors && ny >= 0 && ny < sectors) {
                        // Only add each connection once (avoid duplicates by only connecting forward)
                        if (ny > sy || (ny == sy && nx > sx)) {
                            Vector2D neighborCenter(
                                (static_cast<float>(nx) + 0.5f) * sectorW,
                                (static_cast<float>(ny) + 0.5f) * sectorH
                            );
                            seedPaths.emplace_back(sectorCenter, neighborCenter);
                        }
                    }
                }
            }
        }
    }

    // Submit pre-warming paths with Low priority (background processing)
    // These will be distributed automatically by ThreadSystem
    int pathCount = 0;
    for (const auto& [start, goal] : seedPaths) {
        // Use Low priority and no callback for pre-warming
        requestPath(0, start, goal, Priority::Low, nullptr);
        pathCount++;
    }

    PATHFIND_INFO("Submitted " + std::to_string(pathCount) +
                  " pre-warming paths to background ThreadSystem");

    m_prewarming.store(false);
}

void PathfinderManager::subscribeToEvents() {
    if (!EventManager::Instance().isInitialized()) {
        PATHFIND_WARN("EventManager not initialized, delaying event subscription");
        return;
    }
    
    try {
        auto& eventMgr = EventManager::Instance();
        
        // Subscribe to collision obstacle changed events
        auto token = eventMgr.registerHandlerWithToken(EventTypeId::CollisionObstacleChanged, 
            [this](const EventData& data) {
                if (data.isActive() && data.event) {
                    auto obstacleEvent = std::dynamic_pointer_cast<CollisionObstacleChangedEvent>(data.event);
                    if (obstacleEvent) {
                        onCollisionObstacleChanged(obstacleEvent->getPosition(), 
                                                  obstacleEvent->getRadius(),
                                                  obstacleEvent->getDescription());
                    }
                }
            });
        
        m_eventHandlerTokens.push_back(token);
        PATHFIND_DEBUG("PathfinderManager subscribed to CollisionObstacleChanged events");

        // Subscribe to world events (WorldLoaded, WorldUnloaded, TileChanged)
        auto worldToken = eventMgr.registerHandlerWithToken(EventTypeId::World,
            [this](const EventData& data) {
                auto baseEvent = data.event;
                if (!baseEvent) return;

                // Handle WorldLoadedEvent
                if (auto loadedEvent = std::dynamic_pointer_cast<WorldLoadedEvent>(baseEvent)) {
                    onWorldLoaded(loadedEvent->getWidth(), loadedEvent->getHeight());
                    return;
                }

                // Handle WorldUnloadedEvent
                if (auto unloadedEvent = std::dynamic_pointer_cast<WorldUnloadedEvent>(baseEvent)) {
                    (void)unloadedEvent; // Acknowledge event
                    onWorldUnloaded();
                    return;
                }

                // Handle TileChangedEvent
                if (auto tileEvent = std::dynamic_pointer_cast<TileChangedEvent>(baseEvent)) {
                    onTileChanged(tileEvent->getX(), tileEvent->getY());
                    return;
                }
            });

        m_eventHandlerTokens.push_back(worldToken);
        PATHFIND_DEBUG("PathfinderManager subscribed to World events (WorldLoaded, WorldUnloaded, TileChanged)");

    } catch (const std::exception& e) {
        PATHFIND_ERROR("Failed to subscribe to events: " + std::string(e.what()));
    }
}

void PathfinderManager::unsubscribeFromEvents() {
    if (!EventManager::Instance().isInitialized()) {
        return;
    }
    
    try {
        auto& eventMgr = EventManager::Instance();
        for (const auto& token : m_eventHandlerTokens) {
            eventMgr.removeHandler(token);
        }
        m_eventHandlerTokens.clear();
        PATHFIND_DEBUG("PathfinderManager unsubscribed from all events");
        
    } catch (const std::exception& e) {
        PATHFIND_ERROR("Failed to unsubscribe from events: " + std::string(e.what()));
    }
}

void PathfinderManager::onCollisionObstacleChanged(const Vector2D& position, float radius, const std::string& description) {
    // Increment collision version to trigger cache invalidation
    m_lastCollisionVersion.fetch_add(1, std::memory_order_release);
    
    // Selective cache invalidation: remove paths that pass through the affected area
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        
        size_t removedCount = 0;
        for (auto it = m_pathCache.begin(); it != m_pathCache.end(); ) {
            bool pathIntersectsArea = false;
            
            // Check if any point in the cached path is within the affected radius
            for (const auto& pathPoint : it->second.path) {
                float distance2 = (pathPoint - position).dot(pathPoint - position);
                if (distance2 <= (radius * radius)) {
                    pathIntersectsArea = true;
                    break;
                }
            }
            
            if (pathIntersectsArea) {
                it = m_pathCache.erase(it);
                removedCount++;
            } else {
                ++it;
            }
        }
        
        if (removedCount > 0) {
            PATHFIND_DEBUG("Invalidated " + std::to_string(removedCount) + 
                          " cached paths due to obstacle change: " + description);
        }
    }
}

void PathfinderManager::onWorldLoaded(int worldWidth, int worldHeight) {
    PATHFIND_INFO("World loaded (" + std::to_string(worldWidth) + "x" + std::to_string(worldHeight) +
                  ") - rebuilding pathfinding grid and clearing cache");

    // Clear all cached paths - old world paths are completely invalid
    clearAllCache();

    // Rebuild pathfinding grid from new world data
    // Note: calculateOptimalCacheSettings() and prewarmPathCache() are called
    // automatically when the async rebuild completes (see rebuildGrid() implementation)
    rebuildGrid();

    PATHFIND_INFO("Pathfinding grid rebuild initiated (async)");
}

void PathfinderManager::onWorldUnloaded() {
    PATHFIND_INFO("World unloaded - clearing all pathfinding cache and pending requests");

    // Clear all cached paths
    clearAllCache();

    // Clear pending callbacks since entities may no longer exist
    {
        std::lock_guard<std::mutex> pendingLock(m_pendingMutex);
        m_pending.clear();
    }

    PATHFIND_INFO("Pathfinding cache and pending requests cleared");
}

void PathfinderManager::onTileChanged(int x, int y) {
    // Convert tile coordinates to world position (assuming 64px tiles)
    constexpr float TILE_SIZE = 64.0f;
    Vector2D tileWorldPos(x * TILE_SIZE + TILE_SIZE * 0.5f, y * TILE_SIZE + TILE_SIZE * 0.5f);

    // Invalidate paths that pass through or near the changed tile
    // Use slightly larger radius than tile size to catch paths that pass nearby
    constexpr float INVALIDATION_RADIUS = TILE_SIZE * 1.5f;

    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    size_t removedCount = 0;

    for (auto it = m_pathCache.begin(); it != m_pathCache.end(); ) {
        bool pathIntersectsTile = false;

        // Check if any point in the cached path is within the tile's influence radius
        for (const auto& pathPoint : it->second.path) {
            float distance2 = (pathPoint - tileWorldPos).dot(pathPoint - tileWorldPos);
            if (distance2 <= (INVALIDATION_RADIUS * INVALIDATION_RADIUS)) {
                pathIntersectsTile = true;
                break;
            }
        }

        if (pathIntersectsTile) {
            it = m_pathCache.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }

    if (removedCount > 0) {
        PATHFIND_DEBUG("Tile changed at (" + std::to_string(x) + ", " + std::to_string(y) +
                      "), invalidated " + std::to_string(removedCount) + " cached paths");
    }
}

// End of file - event-driven pathfinding with collision and world integration
