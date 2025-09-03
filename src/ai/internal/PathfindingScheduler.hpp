#ifndef PATHFINDING_SCHEDULER_HPP
#define PATHFINDING_SCHEDULER_HPP

#include <queue>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <optional>

#include "../../utils/Vector2D.hpp"
#include "../../entities/Entity.hpp" // For EntityID type

// Forward declarations to minimize dependencies
namespace HammerEngine { struct AABB; }
class CollisionManager;

namespace AIInternal { 
    class PathCache; 
    class SpatialPriority;
    struct PathCacheStats; 
}

namespace AIInternal {

enum class PathPriority {
    Critical = 0, // Player, combat situations
    High = 1,     // Close NPCs, important behaviors  
    Normal = 2,   // Regular NPC navigation
    Low = 3       // Background/distant NPCs
};

struct PathRequest {
    EntityID entityId;
    Vector2D start;
    Vector2D goal;
    PathPriority priority;
    uint64_t requestTime;
    std::function<void(EntityID, const std::vector<Vector2D>&)> callback;
    
    PathRequest(EntityID id, const Vector2D& s, const Vector2D& g, 
                PathPriority p = PathPriority::Normal,
                std::function<void(EntityID, const std::vector<Vector2D>&)> cb = nullptr)
        : entityId(id), start(s), goal(g), priority(p), 
          requestTime(0), callback(cb) {}

    // Priority comparison for priority_queue (lower priority value = higher importance)
    bool operator<(const PathRequest& other) const {
        // Higher priority (lower number) comes first
        if (priority != other.priority) {
            return priority > other.priority;
        }
        // Within same priority, older requests come first
        return requestTime > other.requestTime;
    }
};

struct PathResult {
    std::vector<Vector2D> path;
    uint64_t computeTime;
    bool isValid;
    
    PathResult() : computeTime(0), isValid(false) {}
    PathResult(const std::vector<Vector2D>& p, uint64_t time) 
        : path(p), computeTime(time), isValid(!p.empty()) {}
};

/**
 * Internal pathfinding scheduler that replaces the current batch processing hack.
 * Manages pathfinding requests with priority queue and spatial sorting for optimal performance.
 * Integrates with CollisionManager for spatial queries and ThreadSystem for background processing.
 * 
 * This class follows the engine's established patterns:
 * - Uses CollisionManager for spatial operations
 * - Integrates with ThreadSystem for background processing
 * - Thread-safe design with proper mutex usage
 * - RAII principles with proper cleanup
 */
class PathfindingScheduler {
public:
    PathfindingScheduler();
    ~PathfindingScheduler();

    // Core request management
    void requestPath(EntityID entityId, const Vector2D& start, const Vector2D& goal, 
                    PathPriority priority = PathPriority::Normal,
                    std::function<void(EntityID, const std::vector<Vector2D>&)> callback = nullptr);
    
    bool hasPath(EntityID entityId) const;
    std::vector<Vector2D> getPath(EntityID entityId) const;
    void clearPath(EntityID entityId);

    // Main update method called from AIManager
    void update(float deltaTime, const Vector2D& playerPos);
    
    // Extract pending requests for processing by AIManager
    std::vector<PathRequest> extractPendingRequests(size_t maxRequests = MAX_REQUESTS_PER_FRAME);
    
    // Store pathfinding results computed by AIManager
    void storePathResult(EntityID entityId, const std::vector<Vector2D>& path);
    
    // Cache a successful path for reuse (NEW METHOD)
    void cacheSuccessfulPath(const Vector2D& start, const Vector2D& goal, const std::vector<Vector2D>& path);
    
    // Get cached path for batch processing
    std::optional<std::vector<Vector2D>> getCachedPath(const Vector2D& start, const Vector2D& goal, float tolerance) const;
    
    // Performance monitoring
    size_t getQueueSize() const;
    size_t getActiveRequestCount() const;
    
    // Path cache statistics for performance monitoring
    PathCacheStats getPathCacheStats() const;
    
    // Log aggregated pathfinding statistics (similar to ParticleManager pattern)
    void logPathfindingStats() const;
    
    // Shutdown method following engine patterns
    void shutdown();
    
    // SpatialPriority integration (Phase 2)
    void setSpatialPriority(SpatialPriority* spatialPriority);

private:
    // Request management
    std::priority_queue<PathRequest> m_requestQueue;
    std::unordered_map<EntityID, PathResult> m_pathResults;
    std::unordered_map<EntityID, int> m_pendingEntityRequests; // Track pending requests per entity
    mutable std::mutex m_queueMutex;
    mutable std::mutex m_resultsMutex;
    
    // Spatial priority system
    Vector2D m_lastPlayerPos;
    SpatialPriority* m_spatialPriority; // Non-owning pointer to AIManager's SpatialPriority system
    
    // Path caching system
    std::unique_ptr<PathCache> m_pathCache;
    
    // Performance controls - CRITICAL FIX: Increase from 8 to handle overflow
    static constexpr size_t MAX_REQUESTS_PER_FRAME = 32;
    static constexpr size_t MAX_BATCH_SIZE = 32;
    static constexpr uint64_t REQUEST_TIMEOUT_MS = 5000; // 5 second timeout
    std::atomic<bool> m_isShutdown{false};
    
    // Frame tracking for performance metrics
    std::atomic<size_t> m_requestsThisFrame{0};
    std::atomic<uint64_t> m_lastUpdateFrame{0};
    
    // Statistics tracking (atomic for thread-safe access)
    mutable std::atomic<size_t> m_totalRequestsProcessed{0};
    mutable std::atomic<size_t> m_pathsCompleted{0};
    mutable std::atomic<size_t> m_pathsFromCache{0};
    mutable std::atomic<size_t> m_timedOutRequests{0};
    mutable std::atomic<uint64_t> m_totalComputeTimeMs{0};
    
    // Internal processing methods
    void processRequestBatch();
    void processPathBatch(std::vector<PathRequest> batch);
    void processPathBatchWithGrid(std::vector<PathRequest> batch, 
                                 std::function<std::vector<Vector2D>(const Vector2D&, const Vector2D&)> pathfinder);
    void cleanupExpiredRequests();
    
    // Spatial priority helpers
    PathPriority adjustPriorityByDistance(const PathRequest& request, const Vector2D& playerPos) const;
    float calculateDistanceToPlayer(const Vector2D& position, const Vector2D& playerPos) const;
    
    // CollisionManager integration
    int getAreaCongestion(const Vector2D& center, float radius) const;
    bool hasRealtimeObstacles(const Vector2D& start, const Vector2D& goal) const;
    
    // Spatial sorting for cache efficiency
    static bool spatialComparator(const PathRequest& a, const PathRequest& b);
    
    // Prevent copying
    PathfindingScheduler(const PathfindingScheduler&) = delete;
    PathfindingScheduler& operator=(const PathfindingScheduler&) = delete;
};

} // namespace AIInternal

#endif // PATHFINDING_SCHEDULER_HPP