/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_MANAGER_HPP
#define COLLISION_MANAGER_HPP

#include <memory>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstddef>
#include <chrono>
#include <array>
#include <optional>

#include "entities/Entity.hpp" // EntityID
#include "collisions/CollisionBody.hpp"
#include "collisions/CollisionInfo.hpp"
#include "collisions/HierarchicalSpatialHash.hpp"
#include "collisions/TriggerTag.hpp"
#include "managers/EventManager.hpp"

// Forward declarations
namespace HammerEngine {
    class Camera;
}

using HammerEngine::AABB;
using HammerEngine::BodyType;
using HammerEngine::CollisionInfo;
using HammerEngine::CollisionLayer;

class CollisionManager {
private:
    // Forward declarations for structures used in public interface
    struct CollisionStorage;
    struct CollisionPool;

public:
    static CollisionManager& Instance() {
        static CollisionManager s_instance; return s_instance;
    }

    bool init();
    void clean();

    /**
     * @brief Prepares CollisionManager for state transition by clearing all collision bodies
     *
     * CRITICAL ARCHITECTURAL REQUIREMENT:
     * This method MUST clear ALL collision bodies (both dynamic and static) during state transitions.
     *
     * WHY THIS IS NECESSARY:
     * prepareForStateTransition() is called BEFORE state exit, which unregisters event handlers.
     * This means WorldUnloadedEvent handlers will NOT fire after state transition begins.
     *
     * Previous "smart" logic tried to keep static bodies when a world was active, expecting
     * WorldUnloadedEvent to clean them up. This was BROKEN because:
     * 1. prepareForStateTransition() unregisters event handlers first
     * 2. WorldUnloadedEvent handler never fires
     * 3. Static bodies from old world persist into new world
     *
     * CONSEQUENCES OF NOT CLEARING ALL BODIES:
     * - Duplicate/stale collision bodies across state transitions
     * - Spatial hash corruption (bodies from multiple worlds in same hash)
     * - Collision detection failures (entities colliding with phantom geometry)
     * - Memory leaks (bodies never cleaned up)
     *
     * CORRECT BEHAVIOR:
     * Always clear ALL bodies. The world will be unloaded immediately after state transition,
     * and the new state will rebuild static bodies when it loads its world via WorldLoadedEvent.
     *
     * @note This is called automatically by GameStateManager before state transitions
     * @note Event handlers are unregistered AFTER this method completes
     * @see GameStateManager::changeState()
     */
    void prepareForStateTransition();

    bool isInitialized() const { return m_initialized; }
    bool isShutdown() const { return m_isShutdown; }

    /**
     * @brief Sets global pause state for collision detection
     * @param paused true to pause collision updates, false to resume
     */
    void setGlobalPause(bool paused);

    /**
     * @brief Gets the current global pause state
     * @return true if collision updates are globally paused
     */
    bool isGloballyPaused() const;

    // Tick: run collision detection/resolution only (no movement integration)
    void update(float dt);


    // NEW SOA UPDATE PATH: High-performance collision detection using SOA storage
    void updateSOA(float dt);


    // Batch updates for performance optimization (AI entities)
    struct KinematicUpdate {
        EntityID id;
        Vector2D position;
        Vector2D velocity;

        KinematicUpdate(EntityID entityId, const Vector2D& pos, const Vector2D& vel = Vector2D(0, 0))
            : id(entityId), position(pos), velocity(vel) {}
    };
    void updateKinematicBatchSOA(const std::vector<KinematicUpdate>& updates);

    // Per-batch collision updates (zero contention - each AI batch has its own buffer)
    void applyBatchedKinematicUpdates(const std::vector<std::vector<KinematicUpdate>>& batchUpdates);

    // Single-vector overload for non-batched updates (convenience wrapper)
    void applyKinematicUpdates(std::vector<KinematicUpdate>& updates);

    // Convenience methods for triggers
    EntityID createTriggerArea(const AABB& aabb,
                               HammerEngine::TriggerTag tag,
                               uint32_t layerMask = CollisionLayer::Layer_Environment,
                               uint32_t collideMask = 0xFFFFFFFFu);
    EntityID createTriggerAreaAt(float cx, float cy, float halfW, float halfH,
                                 HammerEngine::TriggerTag tag,
                                 uint32_t layerMask = CollisionLayer::Layer_Environment,
                                 uint32_t collideMask = 0xFFFFFFFFu);
    void setTriggerCooldown(EntityID triggerId, float seconds);
    void setDefaultTriggerCooldown(float seconds) { m_defaultTriggerCooldownSec = seconds; }

    // World helpers: build collision bodies and triggers from world data
    size_t createTriggersForWaterTiles(HammerEngine::TriggerTag tag = HammerEngine::TriggerTag::Water);
    size_t createTriggersForObstacles(); // Create triggers for ROCK, TREE with movement penalties
    size_t createStaticObstacleBodies();

    // Queries
    bool overlaps(EntityID a, EntityID b) const;
    void queryArea(const AABB& area, std::vector<EntityID>& out) const;
    // Query a body's center by id; returns true if found
    bool getBodyCenter(EntityID id, Vector2D& outCenter) const;
    // Type/flags helpers for filtering
    bool isDynamic(EntityID id) const;
    bool isKinematic(EntityID id) const;
    bool isStatic(EntityID id) const;
    bool isTrigger(EntityID id) const;

    // World coupling
    void rebuildStaticFromWorld();                // build colliders from WorldManager grid
    void populateStaticCache();                   // populate static collision cache after world load
    void onTileChanged(int x, int y);             // update a specific cell
    void setWorldBounds(float minX, float minY, float maxX, float maxY);

    // Callbacks
    using CollisionCB = std::function<void(const CollisionInfo&)>;
    void addCollisionCallback(CollisionCB cb);
    void onCollision(CollisionCB cb) { addCollisionCallback(std::move(cb)); }

    // Metrics
    size_t getBodyCount() const { return m_storage.size(); }
    bool isSyncing() const { return m_isSyncing; }

    // NEW SOA STORAGE MANAGEMENT METHODS
    size_t addCollisionBodySOA(EntityID id, const Vector2D& position, const Vector2D& halfSize,
                               BodyType type, uint32_t layer = CollisionLayer::Layer_Default,
                               uint32_t collidesWith = 0xFFFFFFFFu,
                               bool isTrigger = false, uint8_t triggerTag = 0);
    void removeCollisionBodySOA(EntityID id);
    bool getCollisionBodySOA(EntityID id, size_t& outIndex) const;
    void updateCollisionBodyPositionSOA(EntityID id, const Vector2D& newPosition);
    void updateCollisionBodyVelocitySOA(EntityID id, const Vector2D& newVelocity);
    Vector2D getCollisionBodyVelocitySOA(EntityID id) const;
    void updateCollisionBodySizeSOA(EntityID id, const Vector2D& newHalfSize);
    void attachEntity(EntityID id, EntityPtr entity);
    void processPendingCommands(); // Process queued collision body commands (for tests/immediate processing)

    // SOA Body Management Methods
    void setBodyEnabled(EntityID id, bool enabled);

    // Configuration setters for collision culling (runtime adjustable)
    void setCullingBuffer(float buffer) { m_cullingBuffer = buffer; }
    void setCacheEvictionMultiplier(float multiplier) { m_cacheEvictionMultiplier = multiplier; }
    void setCacheEvictionInterval(size_t interval) { m_cacheEvictionInterval = interval; }
    void setCacheStaleThreshold(uint8_t threshold) { m_cacheStaleThreshold = threshold; }

    // Configuration getters
    float getCullingBuffer() const { return m_cullingBuffer; }
    float getCacheEvictionMultiplier() const { return m_cacheEvictionMultiplier; }
    size_t getCacheEvictionInterval() const { return m_cacheEvictionInterval; }
    uint8_t getCacheStaleThreshold() const { return m_cacheStaleThreshold; }
    void setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask);
    void setVelocity(EntityID id, const Vector2D& velocity);
    void setBodyTrigger(EntityID id, bool isTrigger);

    // Internal buffer management (simplified public interface)
    void prepareCollisionBuffers(size_t bodyCount);

    // SOA UPDATE HELPER METHODS
    void syncSpatialHashesWithActiveIndices();
    void resolveSOA(const CollisionInfo& collision);
    void syncEntitiesToSOA();
    void processTriggerEventsSOA();

    // PERFORMANCE: Vector pooling for temporary allocations
    std::vector<size_t>& getPooledVector();
    void returnPooledVector(std::vector<size_t>& vec);

    /* Body Type Distinctions:
     * - STATIC: World obstacles, buildings, triggers (never move, handled separately)
     * - KINEMATIC: NPCs, script-controlled entities (move via script, not physics)
     * - DYNAMIC: Player, projectiles (physics-simulated, respond to forces)
     *
     * Note: The collision system groups KINEMATIC + DYNAMIC as "movable" bodies
     * for broadphase optimization, since both require collision detection against
     * static geometry and each other.
     */
    void updatePerformanceMetricsSOA(
        std::chrono::steady_clock::time_point t0,
        std::chrono::steady_clock::time_point t1,
        std::chrono::steady_clock::time_point t2,
        std::chrono::steady_clock::time_point t3,
        std::chrono::steady_clock::time_point t4,
        std::chrono::steady_clock::time_point t5,
        std::chrono::steady_clock::time_point t6,
        size_t bodyCount,
        size_t activeMovableBodies,
        size_t pairCount,
        size_t collisionCount,
        size_t activeBodies,
        size_t dynamicBodiesCulled,
        size_t staticBodiesCulled,
        double cullingMs,
        size_t totalStaticBodies,
        size_t totalMovableBodies);

    // Debug utilities
    void logCollisionStatistics() const;
    size_t getStaticBodyCount() const;
    size_t getKinematicBodyCount() const;
    size_t getDynamicBodyCount() const;

private:
    CollisionManager() = default;
    ~CollisionManager() { if (!m_isShutdown) clean(); }
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    // NEW SOA-BASED BROADPHASE: High-performance hierarchical collision detection
    void broadphaseSOA(std::vector<std::pair<size_t, size_t>>& indexPairs);
    void narrowphaseSOA(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                        std::vector<CollisionInfo>& collisions) const;

    // Multi-threading support for narrowphase (WorkerBudget integrated)
    void narrowphaseSingleThreaded(
        const std::vector<std::pair<size_t, size_t>>& indexPairs,
        std::vector<CollisionInfo>& collisions) const;

    void narrowphaseMultiThreaded(
        const std::vector<std::pair<size_t, size_t>>& indexPairs,
        std::vector<CollisionInfo>& collisions,
        size_t batchCount,
        size_t batchSize) const;

    void narrowphaseBatch(
        const std::vector<std::pair<size_t, size_t>>& indexPairs,
        size_t startIdx,
        size_t endIdx,
        std::vector<CollisionInfo>& outCollisions) const;

    void processNarrowphasePairScalar(
        const std::pair<size_t, size_t>& pair,
        std::vector<CollisionInfo>& outCollisions) const;

    // Multi-threading support for broadphase (WorkerBudget integrated)
    void broadphaseSingleThreaded(std::vector<std::pair<size_t, size_t>>& indexPairs);

    void broadphaseMultiThreaded(
        std::vector<std::pair<size_t, size_t>>& indexPairs,
        size_t batchCount,
        size_t batchSize);

    void broadphaseBatch(
        std::vector<std::pair<size_t, size_t>>& outIndexPairs,
        size_t startIdx,
        size_t endIdx);

    // Internal helper methods for SOA buffer management
    void swapCollisionBuffers();
    void copyHotDataToWorkingBuffer();
    void buildActiveIndicesSOA();
    void prepareCollisionPools(size_t bodyCount, size_t threadCount);
    void mergeThreadResults();

    // Apply pending kinematic updates from async AI threads (called at start of update)
    void applyPendingKinematicUpdates();

    // Spatial hash optimization methods
    void rebuildStaticSpatialHash();
    void updateStaticCollisionCacheForMovableBodies();

    // Building collision validation

    void subscribeWorldEvents(); // hook to world events

    // Thread-safe command queue system for collision body management
    enum class CommandType {
        Add,
        Remove,
        Modify
    };

    struct PendingCommand {
        CommandType type = CommandType::Add;
        EntityID id = 0;
        Vector2D position{};
        Vector2D halfSize{};
        BodyType bodyType = BodyType::DYNAMIC;
        uint32_t layer = 0;
        uint32_t collideMask = 0;
        bool isTrigger = false;
        uint8_t triggerTag = 0;
    };

    // Collision culling configuration - adjustable constants
    static constexpr float COLLISION_CULLING_BUFFER = 1000.0f;      // Buffer around culling area (1200x1200 total area)
    static constexpr float SPATIAL_QUERY_EPSILON = 0.5f;            // AABB expansion for cell boundary overlap protection (reduced from 2.0f)
    static constexpr float CACHE_EVICTION_MULTIPLIER = 3.0f;        // Cache entries beyond 3x culling buffer are marked stale
    static constexpr size_t CACHE_EVICTION_INTERVAL = 300;          // Check for stale cache entries every 300 frames (5 seconds at 60 FPS)
    static constexpr uint8_t CACHE_STALE_THRESHOLD = 3;             // Remove cache entries after 3 consecutive eviction cycles without access

    // Collision prediction configuration - prevents diagonal tunneling through corners
    static constexpr float VELOCITY_PREDICTION_FACTOR = 1.15f;      // Expand AABBs by velocity*dt*factor to predict collisions
    static constexpr float FAST_VELOCITY_THRESHOLD = 250.0f;        // Velocity threshold for AABB expansion (pixels/frame)

    // Spatial culling support (area-based, not camera-based)
    struct CullingArea {
        float minX, minY, maxX, maxY;
        float bufferSize{COLLISION_CULLING_BUFFER}; // Buffer around specified culling area

        bool contains(float x, float y) const {
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        }
    };

    // Returns body type counts: {totalStatic, totalDynamic, totalKinematic}
    std::tuple<size_t, size_t, size_t> buildActiveIndicesSOA(const CullingArea& cullingArea) const;
    CullingArea createDefaultCullingArea() const;
    void evictStaleCacheEntries(const CullingArea& cullingArea);

    // OPTIMIZATION HELPERS: Static spatial grid and frame decimation
    void rebuildStaticSpatialGrid();
    void queryStaticGridCells(const CullingArea& area, std::vector<size_t>& outIndices) const;


    // Configurable collision culling parameters (runtime adjustable)
    float m_cullingBuffer{COLLISION_CULLING_BUFFER};
    float m_cacheEvictionMultiplier{CACHE_EVICTION_MULTIPLIER};
    size_t m_cacheEvictionInterval{CACHE_EVICTION_INTERVAL};
    uint8_t m_cacheStaleThreshold{CACHE_STALE_THRESHOLD};

    bool m_initialized{false};
    bool m_isShutdown{false};
    std::atomic<bool> m_globallyPaused{false}; // Global pause state for update() early exit
    AABB m_worldBounds{0,0, 100000.0f, 100000.0f}; // large default box (centered at 0,0)

    // NEW SOA STORAGE SYSTEM: Following AIManager pattern for better cache performance
    struct CollisionStorage {
        // Hot data: Accessed every frame during collision detection
        // OPTIMIZED: Reduced from 64 bytes with wasted space to efficient 64-byte layout
        struct HotData {
            Vector2D position;           // 8 bytes: Current position (center of AABB)
            Vector2D velocity;           // 8 bytes: Current velocity
            Vector2D halfSize;           // 8 bytes: Half-width and half-height
            
            // Cached AABB for performance - exactly 16 bytes (4 floats)
            mutable float aabbMinX, aabbMinY, aabbMaxX, aabbMaxY;
            
            uint16_t layers;             // 2 bytes: Layer mask (supports 16 layers, 7 currently defined)
            uint16_t collidesWith;       // 2 bytes: Collision mask
            uint8_t bodyType;            // 1 byte: BodyType enum (STATIC, KINEMATIC, DYNAMIC)
            uint8_t triggerTag;          // 1 byte: TriggerTag enum for triggers
            uint8_t active;              // 1 byte: Whether this body participates in collision detection
            uint8_t isTrigger;           // 1 byte: Whether this is a trigger body
            mutable uint8_t aabbDirty;   // 1 byte: Whether cached AABB needs updating
            
            // OPTIMIZATION: Cached coarse grid coords (eliminates m_bodyCoarseCell map lookup)
            int16_t coarseCellX;         // 2 bytes: Cached coarse grid X coordinate
            int16_t coarseCellY;         // 2 bytes: Cached coarse grid Y coordinate
            
            // Reserved for future optimizations (e.g., collision flags, frame counters)
            uint8_t _reserved[5];        // 5 bytes: Future expansion space
            
            // Padding to exactly 64 bytes (current size: 62, need 2 more)
            uint8_t _padding[2];

        };
        static_assert(sizeof(HotData) == 64, "HotData should be exactly 64 bytes for cache alignment");

        // Cold data: Rarely accessed, separated to avoid cache pollution
        struct ColdData {
            EntityWeakPtr entityWeak;    // Back-reference to entity
            Vector2D acceleration;       // Acceleration (rarely used)
            Vector2D lastPosition;       // Previous position for optimization
            AABB fullAABB;              // Full AABB (computed from position + halfSize)
            float restitution;           // Bounce coefficient (0.0-1.0) - moved from HotData for cache optimization
            float friction;              // Surface friction (0.0-1.0) - for future physics implementation
            float mass;                  // Mass (kg) - for future physics implementation
        };

        // Primary storage arrays (SOA layout)
        std::vector<HotData> hotData;
        std::vector<ColdData> coldData;
        std::vector<EntityID> entityIds;

        // Index mapping for fast entity lookup
        std::unordered_map<EntityID, size_t> entityToIndex;

        // Convenience methods
        size_t size() const { return hotData.size(); }
        bool empty() const { return hotData.empty(); }

        void clear() {
            hotData.clear();
            coldData.clear();
            entityIds.clear();
            entityToIndex.clear();
        }

        void ensureCapacity(size_t capacity) {
            if (hotData.capacity() < capacity) {
                hotData.reserve(capacity);
                coldData.reserve(capacity);
                entityIds.reserve(capacity);
                entityToIndex.reserve(capacity);
            }
        }

        // Get current hot data for a given index
        const HotData& getHotData(size_t index) const {
            return hotData[index];
        }

        HotData& getHotData(size_t index) {
            return hotData[index];
        }

        // Update cached AABB bounds if dirty
        void updateCachedAABB(size_t index) const {
            auto& hot = hotData[index];
            if (hot.aabbDirty) {
                hot.aabbMinX = hot.position.getX() - hot.halfSize.getX();
                hot.aabbMinY = hot.position.getY() - hot.halfSize.getY();
                hot.aabbMaxX = hot.position.getX() + hot.halfSize.getX();
                hot.aabbMaxY = hot.position.getY() + hot.halfSize.getY();
                hot.aabbDirty = 0;
            }
        }

        // Get cached AABB bounds directly (more efficient for simple tests)
        void getCachedAABBBounds(size_t index, float& minX, float& minY, float& maxX, float& maxY) const {
            updateCachedAABB(index);
            const auto& hot = hotData[index];
            minX = hot.aabbMinX;
            minY = hot.aabbMinY;
            maxX = hot.aabbMaxX;
            maxY = hot.aabbMaxY;
        }

        // Compute AABB from hot data (with caching)
        AABB computeAABB(size_t index) const {
            updateCachedAABB(index);
            const auto& hot = hotData[index];
            // Use cached values to construct AABB - center and half-size from cached bounds
            float centerX = (hot.aabbMinX + hot.aabbMaxX) * 0.5f;
            float centerY = (hot.aabbMinY + hot.aabbMaxY) * 0.5f;
            float halfWidth = (hot.aabbMaxX - hot.aabbMinX) * 0.5f;
            float halfHeight = (hot.aabbMaxY - hot.aabbMinY) * 0.5f;
            return AABB(centerX, centerY, halfWidth, halfHeight);
        }
    };

    CollisionStorage m_storage;

    /* ========== DUAL SPATIAL HASH ARCHITECTURE ==========
     *
     * The collision system uses TWO separate spatial hashes for optimal performance:
     *
     * 1. STATIC SPATIAL HASH (m_staticSpatialHash):
     *    - Contains: World geometry (buildings, obstacles, water triggers)
     *    - Rebuilt: Only when world changes (tile edits, building placement)
     *    - Queried: By dynamic/kinematic bodies during broadphase
     *    - Optimization: Coarse-grid region cache (128×128 cells) reduces redundant queries
     *    - Benefit: Static world geometry doesn't need to be re-hashed every frame
     *
     * 2. DYNAMIC SPATIAL HASH (m_dynamicSpatialHash):
     *    - Contains: Moving entities (player, NPCs, projectiles)
     *    - Rebuilt: Every frame from active culled bodies
     *    - Queried: For dynamic-vs-dynamic collision detection
     *    - Optimization: Only includes bodies within culling area (player-centered)
     *    - Benefit: Fast dynamic collision detection without static world overhead
     *
     * WHY SEPARATION:
     * - Avoids rebuilding thousands of static tiles every frame
     * - Static bodies never initiate collision checks (optimization)
     * - Cache remains valid across frames for static geometry
     * - Culling only applies to dynamic hash, not static (prevents missing collisions)
     *
     * BROADPHASE FLOW:
     * 1. Rebuild dynamic hash with active movable bodies (line ~1180)
     * 2. For each movable body:
     *    a. Query dynamic hash → movable-vs-movable pairs
     *    b. Query static cache → movable-vs-static pairs
     * 3. Narrowphase filters pairs and computes collision details
     * ===================================================== */
    HammerEngine::HierarchicalSpatialHash m_staticSpatialHash;   // Static world geometry
    HammerEngine::HierarchicalSpatialHash m_dynamicSpatialHash;  // Moving entities

    // STATIC COLLISION CACHE: Coarse-grid region cache for static bodies
    // Bodies in the same 128×128 coarse cell share the same cached static query results
    // This replaces the old per-body cache for better memory efficiency
    struct CoarseRegionStaticCache {
        std::vector<size_t> staticIndices;
        bool valid{false};
        uint64_t lastAccessFrame{0};  // Frame number when this cache entry was last accessed
        uint8_t staleCount{0};         // Number of consecutive eviction cycles without access
    };
    std::unordered_map<HammerEngine::HierarchicalSpatialHash::CoarseCoord,
                       CoarseRegionStaticCache,
                       HammerEngine::HierarchicalSpatialHash::CoarseCoordHash,
                       HammerEngine::HierarchicalSpatialHash::CoarseCoordEq> m_coarseRegionStaticCache;

    // Pre-populate cache for a specific coarse region (thread-safe: single-threaded population)
    void populateCacheForRegion(const HammerEngine::HierarchicalSpatialHash::CoarseCoord& region,
                                CoarseRegionStaticCache& cache);

    // Cache statistics
    mutable size_t m_cacheHits{0};
    mutable size_t m_cacheMisses{0};

    // Current culling area for spatial queries
    mutable CullingArea m_currentCullingArea{0.0f, 0.0f, 0.0f, 0.0f};

    // OPTIMIZATION: Static Spatial Grid for efficient culling queries
    // Grid (128×128 cells) to quickly filter static bodies by culling area
    // Grid is rebuilt only on world events (statics added/removed), not every frame
    static constexpr float STATIC_GRID_CELL_SIZE = 128.0f;
    struct StaticGridCell {
        int32_t x;
        int32_t y;
        bool operator==(const StaticGridCell& other) const {
            return x == other.x && y == other.y;
        }
    };
    struct StaticGridCellHash {
        size_t operator()(const StaticGridCell& cell) const {
            // Simple hash combining x and y
            return std::hash<int32_t>()(cell.x) ^ (std::hash<int32_t>()(cell.y) << 1);
        }
    };
    std::unordered_map<StaticGridCell, std::vector<size_t>, StaticGridCellHash> m_staticSpatialGrid;
    bool m_staticGridDirty{true};  // Rebuild grid when statics added/removed

    // Tolerance-based static index cache - avoids requerying when camera moves small distances
    static constexpr float STATIC_CACHE_TOLERANCE = STATIC_GRID_CELL_SIZE;  // 128px
    mutable std::vector<size_t> m_cachedStaticIndices;
    mutable CullingArea m_cachedStaticCullingArea{0.0f, 0.0f, 0.0f, 0.0f};
    mutable bool m_staticIndexCacheValid{false};

    std::vector<CollisionCB> m_callbacks;
    std::vector<EventManager::HandlerToken> m_handlerTokens;
    std::unordered_map<uint64_t, std::pair<EntityID,EntityID>> m_activeTriggerPairs; // OnEnter/Exit filtering
    std::unordered_map<EntityID, std::chrono::steady_clock::time_point> m_triggerCooldownUntil;
    float m_defaultTriggerCooldownSec{0.0f};
    
    // ENHANCED OBJECT POOLS: Zero-allocation collision processing
    struct CollisionPool {
        // Primary collision processing buffers
        std::vector<std::pair<EntityID, EntityID>> pairBuffer;
        std::vector<EntityID> candidateBuffer;
        std::vector<CollisionInfo> collisionBuffer;
        std::vector<EntityID> dynamicCandidates;  // For broadphase dynamic queries
        std::vector<EntityID> staticCandidates;   // For broadphase static queries

        // NEW SOA-specific pools
        std::vector<size_t> activeIndices;        // Indices of active bodies for processing
        std::vector<size_t> movableIndices;      // Indices of non-static bodies (dynamic + kinematic)
        std::vector<size_t> staticIndices;       // Indices of static bodies only

        void ensureCapacity(size_t bodyCount) {
            // OPTIMIZED ESTIMATES: Based on actual benchmark results
            // 10k bodies → ~1.4k pairs → ~760 collisions
            // More realistic estimates reduce memory waste and improve cache performance

            size_t expectedPairs;
            if (bodyCount < 1000) {
                expectedPairs = bodyCount; // Small body counts have fewer pairs
            } else if (bodyCount < 5000) {
                expectedPairs = bodyCount / 2; // Medium body counts scale sub-linearly
            } else {
                expectedPairs = bodyCount / 8; // Large body counts benefit from spatial culling
            }

            if (pairBuffer.capacity() < expectedPairs) {
                size_t expectedCollisions = expectedPairs / 2; // About 50% pair→collision ratio observed
                pairBuffer.reserve(expectedPairs);
                candidateBuffer.reserve(bodyCount / 2);
                collisionBuffer.reserve(expectedCollisions);

                // Spatial hash query results scale with local density, not total body count
                dynamicCandidates.reserve(std::min(static_cast<size_t>(64), bodyCount / 10));
                staticCandidates.reserve(std::min(static_cast<size_t>(256), bodyCount / 5));

                // SOA-specific capacity
                activeIndices.reserve(bodyCount);
                movableIndices.reserve(bodyCount / 4);  // Estimate 25% movable (dynamic + kinematic)
                staticIndices.reserve(bodyCount);
            }
        }

        void resetFrame() {
            pairBuffer.clear();
            candidateBuffer.clear();
            collisionBuffer.clear();
            dynamicCandidates.clear();
            staticCandidates.clear();

            // SOA-specific resets
            activeIndices.clear();
            movableIndices.clear();
            staticIndices.clear();
            // Vectors retain capacity
        }
    };

    mutable CollisionPool m_collisionPool;

    // PERFORMANCE: Vector pool for temporary allocations in hot paths
    mutable std::vector<std::vector<size_t>> m_vectorPool;
    mutable std::atomic<size_t> m_nextPoolIndex{0};

    // PERFORMANCE: Reusable containers to avoid per-frame allocations
    // These are cleared each frame but capacity is retained to eliminate heap churn
    mutable std::unordered_set<EntityID> m_collidedEntitiesBuffer;      // For syncEntitiesToSOA()
    mutable std::unordered_set<uint64_t> m_currentTriggerPairsBuffer;   // For processTriggerEventsSOA()
    // Note: buildActiveIndicesSOA() uses pools.staticIndices directly (already a reusable buffer)

    // OPTIMIZATION: Persistent index tracking (avoids O(n) iteration per frame)
    // Regression fix for commit 768ad87 - movable body iteration was O(18K) instead of O(3)
    std::vector<size_t> m_movableBodyIndices;       // Indices of DYNAMIC + KINEMATIC bodies
    std::unordered_set<size_t> m_movableIndexSet;   // For O(1) removal lookup
    std::optional<size_t> m_playerBodyIndex;        // Cached player body index (Layer_Player)

    // Performance metrics
    struct PerfStats {
        double lastBroadphaseMs{0.0};
        double lastNarrowphaseMs{0.0};
        double lastResolveMs{0.0};
        double lastSyncMs{0.0};
        double lastTotalMs{0.0};
        double avgTotalMs{0.0};
        uint64_t frames{0};
        size_t lastPairs{0};
        size_t lastCollisions{0};
        size_t bodyCount{0};

        // PERFORMANCE OPTIMIZATION METRICS: Track optimization effectiveness
        size_t lastActiveBodies{0};           // Bodies after culling optimizations
        size_t lastDynamicBodiesCulled{0};    // Dynamic bodies culled by distance
        size_t lastStaticBodiesCulled{0};     // Static bodies culled by area
        size_t totalStaticBodies{0};          // Total static bodies before culling
        size_t totalMovableBodies{0};         // Total dynamic+kinematic bodies before culling
        double lastCullingMs{0.0};            // Time spent on culling operations
        double avgBroadphaseMs{0.0};          // Average broadphase time

        // CACHE PERFORMANCE METRICS: Track coarse-grid static cache effectiveness
        size_t cacheEntriesActive{0};         // Number of active cache entries
        size_t cacheEntriesEvicted{0};        // Cache entries evicted this frame
        size_t totalCacheEvictions{0};        // Total evictions since start

        // High-performance exponential moving average (no loops, O(1))
        static constexpr double ALPHA = 0.01; // ~100 frame average, much faster than windowing

        void updateAverage(double newTotalMs) {
            if (frames == 0) {
                avgTotalMs = newTotalMs; // Initialize with first value
            } else {
                // Exponential moving average: O(1) operation, no memory overhead
                avgTotalMs = ALPHA * newTotalMs + (1.0 - ALPHA) * avgTotalMs;
            }
        }

        void updateBroadphaseAverage(double newBroadphaseMs) {
            if (frames == 0) {
                avgBroadphaseMs = newBroadphaseMs;
            } else {
                avgBroadphaseMs = ALPHA * newBroadphaseMs + (1.0 - ALPHA) * avgBroadphaseMs;
            }
        }

        // Calculate culling effectiveness percentages
        double getDynamicCullingRate() const {
            return totalMovableBodies > 0 ? (100.0 * lastDynamicBodiesCulled) / totalMovableBodies : 0.0;
        }

        double getStaticCullingRate() const {
            return totalStaticBodies > 0 ? (100.0 * lastStaticBodiesCulled) / totalStaticBodies : 0.0;
        }

        double getActiveBodiesRate() const {
            return bodyCount > 0 ? (100.0 * lastActiveBodies) / bodyCount : 0.0;
        }
    };

public:
    const PerfStats& getPerfStats() const { return m_perf; }
    void resetPerfStats() { m_perf = PerfStats{}; }
    void setVerboseLogging(bool enabled) { m_verboseLogs = enabled; }

private:
    PerfStats m_perf{};
    bool m_verboseLogs{false};

    // Guard to avoid feedback when syncing entity transforms
    bool m_isSyncing{false};

    // Optimization: Track when static spatial hash needs rebuilding
    bool m_staticHashDirty{false};

    // Cache eviction: Track frame count for periodic eviction
    size_t m_framesSinceLastEviction{0};

    // Thread-safe command queue for deferred collision body operations
    std::vector<PendingCommand> m_pendingCommands;
    mutable std::mutex m_commandQueueMutex;

    // Multi-threading support for narrowphase (WorkerBudget integrated)
    mutable std::vector<std::future<void>> m_narrowphaseFutures;
    mutable std::shared_ptr<std::vector<std::vector<CollisionInfo>>> m_batchCollisionBuffers;
    mutable std::mutex m_narrowphaseFuturesMutex;

    // Multi-threading support for broadphase (WorkerBudget integrated)
    mutable std::vector<std::future<void>> m_broadphaseFutures;
    mutable std::shared_ptr<std::vector<std::vector<std::pair<size_t, size_t>>>> m_broadphasePairBuffers;
    mutable std::mutex m_broadphaseFuturesMutex;

    // Threading config and metrics
    static constexpr size_t MIN_PAIRS_FOR_THREADING = 100;
    static constexpr size_t MIN_MOVABLE_FOR_BROADPHASE_THREADING = 500;
    mutable bool m_lastNarrowphaseWasThreaded{false};
    mutable size_t m_lastNarrowphaseBatchCount{1};
    mutable bool m_lastBroadphaseWasThreaded{false};
    mutable size_t m_lastBroadphaseBatchCount{1};

    // Thread-local buffers for parallel broadphase (stack-allocated per batch, zero contention)
    // Each worker thread creates its own instance to avoid data races on spatial hash queries
    struct BroadphaseThreadBuffers {
        std::vector<size_t> dynamicCandidates;
        std::vector<size_t> staticCandidates;
        HammerEngine::HierarchicalSpatialHash::QueryBuffers queryBuffers;

        void reserve() {
            dynamicCandidates.reserve(256);
            staticCandidates.reserve(256);
            queryBuffers.reserve();
        }

        void clear() {
            dynamicCandidates.clear();
            staticCandidates.clear();
            queryBuffers.clear();
        }
    };

    // Thread-safe access to collision storage (entityToIndex map and storage arrays)
    // shared_lock for reads (AI threads), unique_lock for writes (update thread)
    mutable std::shared_mutex m_storageMutex;
};

#endif // COLLISION_MANAGER_HPP
