#ifndef SPATIAL_PRIORITY_HPP
#define SPATIAL_PRIORITY_HPP

/**
 * @file SpatialPriority.hpp
 * @brief Spatial priority system for pathfinding optimization (Phase 2 of pathfinding redesign)
 *
 * This system implements zone-based priority scheduling for pathfinding requests based on
 * distance from the player. It integrates with the existing PathfindingScheduler to provide
 * efficient spatial culling and frame rate management for large numbers of AI entities.
 *
 * Priority Zones (using PathPriority enum from PathfindingScheduler):
 * - Near (0-800px): Critical/High priority, every frame updates
 * - Medium (800-1600px): Normal priority, every 2-3 frames
 * - Far (1600-3200px): Low priority, every 5-10 frames
 * - Culled (3200px+): Low priority, simple movement patterns
 */

#include <unordered_map>
#include <cstdint>
#include <atomic>
#include "utils/Vector2D.hpp"
#include "entities/Entity.hpp" // For EntityID type
#include "ai/internal/PathPriority.hpp" // For PathPriority enum

namespace AIInternal {

/**
 * @brief Spatial priority system for managing pathfinding requests based on distance
 *
 * This class provides zone-based priority classification and frame rate management
 * for pathfinding operations. It follows the engine's established patterns:
 * - Thread-safe design using atomic operations
 * - Integration with existing CollisionManager spatial queries
 * - Performance-oriented with minimal overhead
 * - RAII principles with proper cleanup
 */
class SpatialPriority {
public:
    SpatialPriority();
    ~SpatialPriority() = default;

    /**
     * @brief Determines the priority of an entity based on its distance from the player
     * @param entityId The entity requesting pathfinding
     * @param entityPos Current position of the entity
     * @param playerPos Current position of the player
     * @return Priority level based on distance zones
     */
    PathPriority getEntityPriority(EntityID entityId, const Vector2D& entityPos, const Vector2D& playerPos);

    /**
     * @brief Checks if an entity should be processed this frame based on its priority
     * @param entityId The entity to check
     * @param priority The entity's current priority level
     * @param currentFrame Current game frame number
     * @return true if entity should be processed this frame, false to skip
     */
    bool shouldProcessThisFrame(EntityID entityId, PathPriority priority, uint64_t currentFrame);

    /**
     * @brief Updates internal frame tracking for skipping logic
     * @param currentFrame Current game frame number
     * Called once per frame from AIManager::update()
     */
    void updateFrameSkipping(uint64_t currentFrame);

    /**
     * @brief Gets the distance threshold for a specific priority zone
     * @param priority The priority level to query
     * @return Maximum distance for this priority zone in pixels
     */
    static float getDistanceThreshold(PathPriority priority);

    /**
     * @brief Calculates the frame skip interval for a given priority
     * @param priority The priority level to query
     * @return Number of frames to skip between updates (0 = every frame)
     */
    static uint32_t getFrameSkipInterval(PathPriority priority);

    /**
     * @brief Gets performance statistics for monitoring
     * @return String containing current statistics
     */
    std::string getPerformanceStats() const;

    /**
     * @brief Resets all frame tracking data (useful for state transitions)
     */
    void resetFrameTracking();

private:
    // Priority zone distance thresholds (in pixels)
    static constexpr float NEAR_DISTANCE = 800.0f;      // High priority zone
    static constexpr float MEDIUM_DISTANCE = 1600.0f;   // Normal priority zone
    static constexpr float FAR_DISTANCE = 3200.0f;      // Low priority zone
    // Beyond FAR_DISTANCE = Culled zone

    // Frame skipping intervals for each priority level
    static constexpr uint32_t HIGH_PRIORITY_SKIP = 0;    // Every frame (no skip)
    static constexpr uint32_t NORMAL_PRIORITY_SKIP = 2;  // Every 2-3 frames
    static constexpr uint32_t LOW_PRIORITY_SKIP = 7;     // Every 5-10 frames for far zones

    // Entity tracking limits and cleanup intervals
    static constexpr uint32_t ENTITY_CLEANUP_FRAME_INTERVAL = 600;  // Normal cleanup every 600 frames
    static constexpr size_t MAX_TRACKED_ENTITIES = 10000;           // Hard limit to prevent memory exhaustion
    static constexpr size_t AGGRESSIVE_CLEANUP_THRESHOLD = 8000;    // 80% of max - trigger aggressive cleanup
    static constexpr uint32_t AGGRESSIVE_CLEANUP_INTERVAL = 300;    // More frequent cleanup when approaching limits

    // Frame tracking for skip logic
    struct EntityFrameState {
        uint64_t lastProcessedFrame = 0;
        int lastPriority = 2; // Default to Normal priority (value 2)
        uint32_t consecutiveSkips = 0;

        EntityFrameState() = default;
    };

    // Entity-specific frame tracking
    std::unordered_map<EntityID, EntityFrameState> m_entityFrameStates;

    // Current frame counter
    std::atomic<uint64_t> m_currentFrame{0};

    // Performance tracking (atomic for thread safety)
    mutable std::atomic<uint64_t> m_totalEntitiesProcessed{0};
    mutable std::atomic<uint64_t> m_entitiesSkipped{0};
    mutable std::atomic<uint64_t> m_highPriorityCount{0};
    mutable std::atomic<uint64_t> m_normalPriorityCount{0};
    mutable std::atomic<uint64_t> m_lowPriorityCount{0};

    // Entity cleanup tracking
    mutable std::atomic<uint64_t> m_normalCleanupCount{0};
    mutable std::atomic<uint64_t> m_aggressiveCleanupCount{0};
    mutable std::atomic<uint64_t> m_emergencyCleanupCount{0};
    mutable std::atomic<uint64_t> m_entitiesRejected{0};

    // Helper methods
    PathPriority classifyByDistance(float distanceSquared) const;
    bool shouldSkipBasedOnFrames(const EntityFrameState& state, PathPriority priority, uint64_t currentFrame) const;
    void updateEntityFrameState(EntityID entityId, PathPriority priority, uint64_t currentFrame, bool wasProcessed);
    void performEntityCleanup(uint64_t currentFrame, bool forceAggressive = false);
    bool shouldTrackNewEntity() const;

    // Prevent copying
    SpatialPriority(const SpatialPriority&) = delete;
    SpatialPriority& operator=(const SpatialPriority&) = delete;
};

} // namespace AIInternal

#endif // SPATIAL_PRIORITY_HPP
