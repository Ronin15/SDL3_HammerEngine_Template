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


    // Batch updates for performance optimization (AI entities)
    struct KinematicUpdate {
        EntityID id;
        Vector2D position;
        Vector2D velocity;

        KinematicUpdate(EntityID entityId, const Vector2D& pos, const Vector2D& vel = Vector2D(0, 0))
            : id(entityId), position(pos), velocity(vel) {}
    };
    void updateKinematicBatch(const std::vector<KinematicUpdate>& updates);

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
    void onTileChanged(int x, int y);             // update a specific cell
    void setWorldBounds(float minX, float minY, float maxX, float maxY);

    // Callbacks
    using CollisionCB = std::function<void(const CollisionInfo&)>;
    void addCollisionCallback(CollisionCB cb);
    void onCollision(CollisionCB cb) { addCollisionCallback(std::move(cb)); }

    // Metrics
    size_t getBodyCount() const { return m_storage.size(); }
    bool isSyncing() const { return m_isSyncing; }

    // STATIC BODY MANAGEMENT
    // EDM-CENTRIC: Only static bodies (buildings, triggers, obstacles) go in m_storage
    // Movables (players, NPCs) are managed entirely by EDM - no m_storage entry
    size_t addStaticBody(EntityID id, const Vector2D& position, const Vector2D& halfSize,
                         uint32_t layer = CollisionLayer::Layer_Default,
                         uint32_t collidesWith = 0xFFFFFFFFu,
                         bool isTrigger = false, uint8_t triggerTag = 0);
    void removeCollisionBody(EntityID id);
    bool getCollisionBody(EntityID id, size_t& outIndex) const;
    void updateCollisionBodyPosition(EntityID id, const Vector2D& newPosition);
    void updateCollisionBodyVelocity(EntityID id, const Vector2D& newVelocity);
    Vector2D getCollisionBodyVelocity(EntityID id) const;
    void updateCollisionBodySize(EntityID id, const Vector2D& newHalfSize);
    void attachEntity(EntityID id, EntityPtr entity);

    // Body State Management Methods
    void setBodyEnabled(EntityID id, bool enabled);

    // Configuration setters for collision culling (runtime adjustable)
    void setCullingBuffer(float buffer) { m_cullingBuffer = buffer; }

    // Configuration getters
    float getCullingBuffer() const { return m_cullingBuffer; }
    void setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask);
    void setVelocity(EntityID id, const Vector2D& velocity);
    void setBodyTrigger(EntityID id, bool isTrigger);

    // Internal buffer management (simplified public interface)
    void prepareCollisionBuffers(size_t bodyCount);

    // UPDATE HELPER METHODS
    void syncSpatialHashesWithActiveIndices();
    void resolve(const CollisionInfo& collision);
    void processTriggerEvents();

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
    void updatePerformanceMetrics(
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

    // EDM-CENTRIC BROADPHASE: Uses pools.movableAABBs + pools.movableMovablePairs/movableStaticPairs
    void broadphase();
    void narrowphase(std::vector<CollisionInfo>& collisions) const;

    // Narrowphase uses single-threaded path (SIMD 4-wide)
    void narrowphaseSingleThreaded(std::vector<CollisionInfo>& collisions) const;

    // Multi-threading support for broadphase (WorkerBudget integrated)
    void broadphaseSingleThreaded();
    void broadphaseMultiThreaded(size_t batchCount, size_t batchSize);
    void broadphaseBatch(size_t startIdx, size_t endIdx,
                         std::vector<std::pair<size_t, size_t>>& outMovableMovable,
                         std::vector<std::pair<size_t, size_t>>& outMovableStatic);

    // Internal helper methods for SOA buffer management
    void buildActiveIndices();
    void prepareCollisionPools(size_t bodyCount, size_t threadCount);

    // Apply pending kinematic updates from async AI threads (called at start of update)
    void applyPendingKinematicUpdates();

    // Spatial hash optimization methods
    void rebuildStaticSpatialHash();

    // Building collision validation

    void subscribeWorldEvents(); // hook to world events

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
    std::tuple<size_t, size_t, size_t> buildActiveIndices(const CullingArea& cullingArea) const;
    CullingArea createDefaultCullingArea() const;

    // NOTE: Cache functions removed in Phase 3 - m_staticSpatialHash queried directly


    // Configurable collision culling parameters (runtime adjustable)
    float m_cullingBuffer{COLLISION_CULLING_BUFFER};

    bool m_initialized{false};
    bool m_isShutdown{false};
    std::atomic<bool> m_globallyPaused{false}; // Global pause state for update() early exit
    AABB m_worldBounds{0,0, 100000.0f, 100000.0f}; // large default box (centered at 0,0)

    // NEW SOA STORAGE SYSTEM: Following AIManager pattern for better cache performance
    // REFACTORED: Position/velocity/halfSize removed - accessed via EntityDataManager
    struct CollisionStorage {
        // Hot data: Accessed every frame during collision detection
        // AIManager pattern: Only collision-specific data + EDM index for position access
        struct HotData {
            // Cached AABB for performance - computed from EDM position + halfSize
            mutable float aabbMinX, aabbMinY, aabbMaxX, aabbMaxY;  // 16 bytes

            // EDM reference for accessing position/halfSize (moved from ColdData for cache locality)
            size_t edmIndex;             // 8 bytes: EntityDataManager index

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

            // Padding to 64 bytes (one cache line)
            // Layout: 16 (floats) + 8 (size_t) + 4 (uint16_t) + 5 (uint8_t) + 1 (implicit pad) + 4 (int16_t) = 38
            uint8_t _reserved[26];       // 26 bytes: Future expansion (38 + 26 = 64)
        };
        static_assert(sizeof(HotData) == 64, "HotData should be exactly 64 bytes for cache alignment");

        // Cold data: Rarely accessed, separated to avoid cache pollution
        // NOTE: Position/velocity/halfSize owned by EntityDataManager
        // All collision bodies must have valid EDM entries (statics via createStaticBody)
        struct ColdData {
            EntityWeakPtr entityWeak;    // Back-reference to entity
            AABB fullAABB;              // Full AABB (computed from EDM position + halfSize)
            float restitution;           // Bounce coefficient (0.0-1.0)
            float friction;              // Surface friction (0.0-1.0)
            float mass;                  // Mass (kg) - for future physics
            // NOTE: edmIndex moved to HotData for cache locality during AABB updates
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

        // NOTE: updateCachedAABB moved to CollisionManager (needs EDM access)
        // Call refreshCachedAABBs() at start of broadphase to batch-update all AABBs

        // Get cached AABB bounds directly (assumes AABB already refreshed)
        void getCachedAABBBounds(size_t index, float& minX, float& minY, float& maxX, float& maxY) const {
            const auto& hot = hotData[index];
            minX = hot.aabbMinX;
            minY = hot.aabbMinY;
            maxX = hot.aabbMaxX;
            maxY = hot.aabbMaxY;
        }

        // Compute AABB from cached bounds (assumes AABB already refreshed)
        AABB computeAABB(size_t index) const {
            const auto& hot = hotData[index];
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

    // Current culling area for spatial queries
    mutable CullingArea m_currentCullingArea{0.0f, 0.0f, 0.0f, 0.0f};

    // Static query caching - skip re-query when culling area unchanged
    mutable CullingArea m_lastStaticQueryCullingArea{0.0f, 0.0f, 0.0f, 0.0f};
    mutable bool m_staticQueryCacheDirty{true};

    // NOTE: Redundant caches removed in Phase 3 EDM refactor:
    // - m_coarseRegionStaticCache: Now use m_staticSpatialHash directly
    // - m_staticSpatialGrid: Redundant with m_staticSpatialHash
    // - m_cachedStaticIndices: Tolerance cache adds complexity without benefit

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

        // EDM-CENTRIC: Active tier indices and cached collision data
        std::vector<size_t> movableIndices;       // EDM indices of Active tier entities with collision
        std::vector<size_t> staticIndices;        // m_storage indices of static bodies in culling area
        std::vector<size_t> sortedMovableIndices; // Pool indices sorted by X for Sweep-and-Prune

        // EDM-CENTRIC: Cached AABBs for movables, computed from EDM each frame
        // Parallel to movableIndices: movableAABBs[i] corresponds to movableIndices[i]
        struct MovableAABB {
            float minX, minY, maxX, maxY;
            uint16_t layers;
            uint16_t collidesWith;
        };
        std::vector<MovableAABB> movableAABBs;

        // EDM-CENTRIC: Collision pairs from broadphase
        // movableMovablePairs: (poolIdx_A, poolIdx_B) - both indices into movableIndices/movableAABBs
        // movableStaticPairs: (poolIdx, storageIdx) - poolIdx into movableIndices, storageIdx into m_storage
        std::vector<std::pair<size_t, size_t>> movableMovablePairs;
        std::vector<std::pair<size_t, size_t>> movableStaticPairs;

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

                // EDM-centric capacity
                movableIndices.reserve(bodyCount / 4);  // Estimate 25% Active tier with collision
                movableAABBs.reserve(bodyCount / 4);    // Parallel to movableIndices
                staticIndices.reserve(bodyCount);
                sortedMovableIndices.reserve(bodyCount / 4);
                movableMovablePairs.reserve(expectedPairs / 4);
                movableStaticPairs.reserve(expectedPairs);
            }
        }

        void resetFrame() {
            pairBuffer.clear();
            candidateBuffer.clear();
            collisionBuffer.clear();
            dynamicCandidates.clear();
            staticCandidates.clear();

            // EDM-centric resets
            movableIndices.clear();
            movableAABBs.clear();
            // NOTE: staticIndices is cached and cleared only when culling area changes
            sortedMovableIndices.clear();
            movableMovablePairs.clear();
            movableStaticPairs.clear();
            // Vectors retain capacity
        }
    };

    mutable CollisionPool m_collisionPool;

    // PERFORMANCE: Vector pool for temporary allocations in hot paths
    mutable std::vector<std::vector<size_t>> m_vectorPool;
    mutable std::atomic<size_t> m_nextPoolIndex{0};

    // PERFORMANCE: Reusable containers to avoid per-frame allocations
    // These are cleared each frame but capacity is retained to eliminate heap churn
    mutable std::unordered_set<uint64_t> m_currentTriggerPairsBuffer;   // For processTriggerEvents()
    // Note: buildActiveIndices() uses pools.staticIndices directly (already a reusable buffer)

    // NOTE: Legacy m_movableBodyIndices removed - EDM Active tier is now the source of truth
    // NOTE: Legacy static cell cache removed - m_staticSpatialHash queried directly

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

    // Multi-threading support for narrowphase (WorkerBudget integrated)
    mutable std::vector<std::future<void>> m_narrowphaseFutures;
    mutable std::shared_ptr<std::vector<std::vector<CollisionInfo>>> m_batchCollisionBuffers;
    mutable std::mutex m_narrowphaseFuturesMutex;

    // Multi-threading support for broadphase (WorkerBudget integrated)
    // Matches AIManager pattern: reusable member vectors, no mutex (futures are thread-safe)
    mutable std::vector<std::future<void>> m_broadphaseFutures;
    struct BroadphaseBatchBuffer {
        std::vector<std::pair<size_t, size_t>> movableMovable;
        std::vector<std::pair<size_t, size_t>> movableStatic;
        void clear() { movableMovable.clear(); movableStatic.clear(); }
    };
    mutable std::vector<BroadphaseBatchBuffer> m_broadphaseBatchBuffers;

    // Threading config and metrics
    // Narrowphase: 100+ pairs worth threading (each pair = AABB test + layer check + collision info)
    // Broadphase: With SIMD direct iteration, workload = M×M/2 + M×S AABB checks
    //             150 movables × 150/2 = 11K checks, plus 150 × statics - worth threading
    static constexpr size_t MIN_PAIRS_FOR_THREADING = 100;        // Narrowphase: min pairs before threading
    static constexpr size_t MIN_MOVABLE_FOR_BROADPHASE_THREADING = 150;  // Broadphase: min movable bodies
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

};

#endif // COLLISION_MANAGER_HPP
