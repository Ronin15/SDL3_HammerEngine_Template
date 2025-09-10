/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/PathfinderManager.hpp"
#include "ai/pathfinding/PathfindingGrid.hpp"
#include "managers/WorldManager.hpp"
#include "managers/EventManager.hpp" // Must include for HandlerToken definition
#include "events/CollisionObstacleChangedEvent.hpp"
#include "ai/internal/PathPriority.hpp"
#include <string_view>
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

// Internal priority mapping helpers (implementation-only)
namespace {
    inline AIInternal::PathPriority mapPriorityIntToEnum(int p) {
        if (p <= 0) return AIInternal::PathPriority::Critical;
        if (p == 1) return AIInternal::PathPriority::High;
        if (p == 2) return AIInternal::PathPriority::Normal;
        return AIInternal::PathPriority::Low; // p >= 3
    }

    inline HammerEngine::TaskPriority mapEnumToTaskPriority(AIInternal::PathPriority p) {
        switch (p) {
            case AIInternal::PathPriority::Critical: return HammerEngine::TaskPriority::Critical;
            case AIInternal::PathPriority::High:     return HammerEngine::TaskPriority::High;
            case AIInternal::PathPriority::Normal:   return HammerEngine::TaskPriority::Normal;
            case AIInternal::PathPriority::Low:      return HammerEngine::TaskPriority::Low;
            default:                                 return HammerEngine::TaskPriority::Normal;
        }
    }

    inline std::string_view priorityLabel(AIInternal::PathPriority p) {
        using namespace std::literals;
        switch (p) {
            case AIInternal::PathPriority::Critical: return "Critical"sv;
            case AIInternal::PathPriority::High:     return "High"sv;
            case AIInternal::PathPriority::Normal:   return "Normal"sv;
            case AIInternal::PathPriority::Low:      return "Low"sv;
            default:                                 return "Normal"sv;
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
    int priority,
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
        
        // Cache path (whether from cache hit or new computation)
        if (!path.empty()) {
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
        
        // Update statistics - simplified success check
        if (!path.empty()) {
            m_completedRequests.fetch_add(1, std::memory_order_relaxed);
        } else {
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
        for (auto &cb : callbacks) {
            if (cb) cb(entityId, path);
        }
    };

    // Submit pathfinding work to ThreadSystem with mapped priority
    const AIInternal::PathPriority priEnum = mapPriorityIntToEnum(priority);
    const auto taskPri = mapEnumToTaskPriority(priEnum);
    const auto priLabel = priorityLabel(priEnum);
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

// Backward-compatible overload (enum class → int mapping)
uint64_t PathfinderManager::requestPath(
    EntityID entityId,
    const Vector2D& start,
    const Vector2D& goal,
    AIInternal::PathPriority priority,
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback
) {
    return requestPath(entityId, start, goal, static_cast<int>(priority), std::move(callback));
}

HammerEngine::PathfindingResult PathfinderManager::findPathImmediate(
    const Vector2D& start,
    const Vector2D& goal,
    std::vector<Vector2D>& outPath
) {
    if (!m_initialized.load() || m_isShutdown) {
        return HammerEngine::PathfindingResult::NO_PATH_FOUND;
    }

    // Normalize endpoints for safe and cache-friendly pathfinding
    Vector2D nStart = start;
    Vector2D nGoal = goal;
    normalizeEndpoints(nStart, nGoal);

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
    float distance2 = (nGoal - nStart).dot(nGoal - nStart);
    HammerEngine::PathfindingResult result;
    
    if (distance2 > (1200.0f * 1200.0f)) {
        // Long distance - try hierarchical first
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
        // Short to medium distance - use direct pathfinding
        result = gridSnapshot->findPath(nStart, nGoal, outPath);
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
        PATHFIND_DEBUG("Cannot rebuild grid - no active world");
        return;
    }

    int worldWidth = 0, worldHeight = 0;
    if (!worldManager.getWorldDimensions(worldWidth, worldHeight) || worldWidth <= 0 || worldHeight <= 0) {
        PATHFIND_WARN("Invalid world dimensions during rebuild");
        return;
    }

    const float TILE_SIZE = 32.0f;
    float worldPixelWidth = worldWidth * TILE_SIZE;
    float worldPixelHeight = worldHeight * TILE_SIZE;
    int gridWidth = static_cast<int>(worldPixelWidth / m_cellSize);
    int gridHeight = static_cast<int>(worldPixelHeight / m_cellSize);
    if (gridWidth <= 0 || gridHeight <= 0) {
        PATHFIND_WARN("Calculated grid too small during rebuild");
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

        PATHFIND_INFO("Grid rebuilt successfully");
    } catch (const std::exception& e) {
        PATHFIND_ERROR("Grid rebuild failed: " + std::string(e.what()));
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
        stats.segmentCacheSize = 0; // Segment cache removed for performance
    }
    stats.negativeCacheSize = 0;
    
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
    
    // Fast fallback bounds - avoid calling WorldManager during AI updates
    const float fallbackMargin = std::max(margin, 256.0f);
    return Vector2D(
        std::clamp(position.getX(), 0.0f + fallbackMargin, 3200.0f - fallbackMargin),
        std::clamp(position.getY(), 0.0f + fallbackMargin, 3200.0f - fallbackMargin)
    );
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
    // Fallback
    float fallbackW = 3200.0f;
    float fallbackH = 3200.0f;
    return Vector2D(
        std::clamp(position.getX(), halfW, fallbackW - halfW),
        std::clamp(position.getY(), halfH, fallbackH - halfH)
    );
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
    auto &wm = WorldManager::Instance();
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

    // Quantize to improve cache hits - use 256-pixel quantization
    start = Vector2D(std::round(start.getX() / 256.0f) * 256.0f,
                     std::round(start.getY() / 256.0f) * 256.0f);
    goal = Vector2D(std::round(goal.getX() / 256.0f) * 256.0f,
                    std::round(goal.getY() / 256.0f) * 256.0f);
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
    const float TILE_SIZE = 32.0f; // Match WorldManager::TILE_SIZE
    float worldPixelWidth = worldWidth * TILE_SIZE;
    float worldPixelHeight = worldHeight * TILE_SIZE;
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
        
        return true;
    }
    catch (const std::exception& e) {
        PATHFIND_ERROR("Failed to create pathfinding grid: " + std::string(e.what()));
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
    // Coarser quantization: 256-pixel grid for better cache hit rates
    // Trade some spatial precision for much higher cache reuse
    int sx = static_cast<int>(start.getX() / 256.0f);
    int sy = static_cast<int>(start.getY() / 256.0f);
    int gx = static_cast<int>(goal.getX() / 256.0f);
    int gy = static_cast<int>(goal.getY() / 256.0f);
    
    // Pack into 64-bit key: sx(16) | sy(16) | gx(16) | gy(16)
    return (static_cast<uint64_t>(sx & 0xFFFF) << 48) |
           (static_cast<uint64_t>(sy & 0xFFFF) << 32) |
           (static_cast<uint64_t>(gx & 0xFFFF) << 16) |
           static_cast<uint64_t>(gy & 0xFFFF);
}


void PathfinderManager::evictOldestCacheEntry() {
    if (m_pathCache.empty()) return;
    
    auto oldest = m_pathCache.begin();
    for (auto it = m_pathCache.begin(); it != m_pathCache.end(); ++it) {
        if (it->second.lastUsed < oldest->second.lastUsed) {
            oldest = it;
        }
    }
    m_pathCache.erase(oldest);
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

// End of file - optimized single-tier cache implementation with collision integration
