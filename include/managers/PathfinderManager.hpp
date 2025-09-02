/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PATHFINDER_MANAGER_HPP
#define PATHFINDER_MANAGER_HPP

/**
 * @file PathfinderManager.hpp
 * @brief High-performance pathfinding system manager optimized for speed and efficiency
 *
 * Ultra-high-performance PathfinderManager with ThreadSystem integration:
 * - Centralized pathfinding service for all AI entities
 * - Cache-friendly batch processing for optimal performance
 * - Thread-safe parallel path computation with WorkerBudget priorities
 * - Intelligent path caching and request scheduling
 * - Dynamic obstacle integration from CollisionManager
 * - Scales to 10K+ entities while maintaining 60+ FPS
 * - Lock-free request queuing with minimal contention
 */

#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp"
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

// Forward declarations
namespace HammerEngine {
    class PathfindingGrid;
    enum class PathfindingResult;
}

namespace AIInternal {
    class PathCache;
    class PathfindingScheduler;
    class SpatialPriority;
    struct PathRequest;
    struct PathResult;
    enum class PathPriority;
}

class CollisionManager;
class WorldManager;

/**
 * @brief Centralized pathfinding manager following singleton pattern
 */
class PathfinderManager {
public:
    /**
     * @brief Gets the singleton instance of PathfinderManager
     * @return Reference to the PathfinderManager instance
     */
    static PathfinderManager& Instance();

    /**
     * @brief Initializes the PathfinderManager and its subsystems
     * @return true if initialization successful, false otherwise
     */
    bool init();

    /**
     * @brief Checks if PathfinderManager has been initialized
     * @return true if initialized, false otherwise
     */
    bool isInitialized() const;

    /**
     * @brief Updates pathfinding systems and processes pending requests
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Cleans up all pathfinding resources
     */
    void clean();

    /**
     * @brief Shuts down the PathfinderManager
     */
    static void shutdown();

    /**
     * @brief Checks if PathfinderManager has been shut down
     * @return true if manager is shut down, false otherwise
     */
    bool isShutdown() const;

    // ===== Pathfinding Request Interface =====

    /**
     * @brief Request a path for an entity with optional callback
     * @param entityId The entity requesting the path
     * @param start Starting position in world coordinates
     * @param goal Goal position in world coordinates
     * @param priority Priority level for request scheduling
     * @param callback Optional callback when path is ready
     * @return Request ID for tracking (0 if failed)
     */
    uint64_t requestPath(
        EntityID entityId,
        const Vector2D& start,
        const Vector2D& goal,
        AIInternal::PathPriority priority,
        std::function<void(EntityID, const std::vector<Vector2D>&)> callback = nullptr
    );

    /**
     * @brief Get a path synchronously (blocking)
     * @param start Starting position in world coordinates
     * @param goal Goal position in world coordinates
     * @param outPath Vector to store the resulting path
     * @return PathfindingResult indicating success or failure
     */
    HammerEngine::PathfindingResult findPathImmediate(
        const Vector2D& start,
        const Vector2D& goal,
        std::vector<Vector2D>& outPath
    );

    /**
     * @brief Cancel a pending path request
     * @param requestId The ID of the request to cancel
     */
    void cancelRequest(uint64_t requestId);

    /**
     * @brief Cancel all pending requests for an entity
     * @param entityId The entity whose requests should be cancelled
     */
    void cancelEntityRequests(EntityID entityId);

    // ===== Grid Management =====

    /**
     * @brief Rebuild the pathfinding grid from world data
     */
    void rebuildGrid();

    /**
     * @brief Update dynamic obstacles from CollisionManager
     */
    void updateDynamicObstacles();

    /**
     * @brief Add a temporary weight field (for avoidance)
     * @param center Center of the weight field in world coordinates
     * @param radius Radius of the weight field
     * @param weight Weight multiplier (higher = more expensive to traverse)
     */
    void addTemporaryWeightField(const Vector2D& center, float radius, float weight);

    /**
     * @brief Clear all temporary weight fields
     */
    void clearWeightFields();

    // ===== Configuration =====

    /**
     * @brief Set maximum number of paths to process per frame
     * @param maxPaths Maximum paths per frame (default: 5)
     */
    void setMaxPathsPerFrame(int maxPaths);

    /**
     * @brief Set cache expiration time
     * @param seconds Time in seconds before cached paths expire (default: 5.0)
     */
    void setCacheExpirationTime(float seconds);

    /**
     * @brief Enable/disable diagonal movement in pathfinding
     * @param allow true to allow diagonal movement
     */
    void setAllowDiagonal(bool allow);

    /**
     * @brief Set maximum iterations for pathfinding algorithm
     * @param maxIterations Maximum A* iterations (default: 20000)
     */
    void setMaxIterations(int maxIterations);

    // ===== Statistics =====

    struct PathfinderStats {
        uint64_t totalRequests{0};
        uint64_t completedRequests{0};
        uint64_t cancelledRequests{0};
        uint64_t timedOutRequests{0};
        uint64_t cacheHits{0};
        uint64_t cacheMisses{0};
        float averagePathLength{0.0f};
        float averageComputeTime{0.0f};
        uint32_t pendingRequests{0};
        uint32_t activeThreads{0};
        float cacheHitRate{0.0f};
    };

    /**
     * @brief Get current pathfinding statistics
     * @return Current statistics snapshot
     */
    PathfinderStats getStats() const;

    /**
     * @brief Reset all statistics
     */
    void resetStats();

    // Destructor needs to be public for unique_ptr
    ~PathfinderManager();

private:
    // Singleton implementation
    PathfinderManager() = default;
    PathfinderManager(const PathfinderManager&) = delete;
    PathfinderManager& operator=(const PathfinderManager&) = delete;

    // Core components
    std::unique_ptr<HammerEngine::PathfindingGrid> m_grid;
    // PathCache is now managed by PathfindingScheduler
    std::unique_ptr<AIInternal::PathfindingScheduler> m_scheduler;
    std::unique_ptr<AIInternal::SpatialPriority> m_spatialPriority;

    // Request management
    std::atomic<uint64_t> m_nextRequestId{1};
    std::unordered_map<uint64_t, AIInternal::PathRequest> m_pendingRequests;
    std::priority_queue<AIInternal::PathRequest> m_requestQueue;
    std::mutex m_requestMutex;

    // Thread management
    std::vector<std::future<void>> m_activeTasks;
    std::atomic<uint32_t> m_activeThreadCount{0};
    
    // Configuration
    int m_maxPathsPerFrame{5};
    float m_cacheExpirationTime{5.0f};
    bool m_allowDiagonal{true};
    int m_maxIterations{20000};

    // State management
    std::atomic<bool> m_initialized{false};
    bool m_isShutdown{false};
    
    // Statistics
    mutable PathfinderStats m_stats;
    mutable std::mutex m_statsMutex;

    // Update timers
    float m_gridUpdateTimer{0.0f};
    float m_cacheCleanupTimer{0.0f};
    static constexpr float GRID_UPDATE_INTERVAL = 1.0f;  // seconds
    static constexpr float CACHE_CLEANUP_INTERVAL = 2.0f; // seconds

    // Internal methods
    void processSchedulerRequests(const std::vector<AIInternal::PathRequest>& requests);
    void processRequestBatch(std::vector<AIInternal::PathRequest>& batch); // Legacy compatibility
    void updateStatistics();
    void cleanupCache();
    void integrateCollisionData();
    void integrateWorldData();
};

#endif // PATHFINDER_MANAGER_HPP