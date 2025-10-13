/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_MANAGER_HPP
#define COLLISION_MANAGER_HPP

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstddef>
#include <chrono>
#include <array>

#include "entities/Entity.hpp" // EntityID
#include "collisions/CollisionBody.hpp"
#include "collisions/CollisionInfo.hpp"
#include "collisions/HierarchicalSpatialHash.hpp"
#include "collisions/TriggerTag.hpp"
#include "managers/EventManager.hpp"

// SIMD support detection (following ParticleManager pattern)
#if defined(__SSE2__) || \
    (defined(_MSC_VER) && \
     (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)))
#define COLLISION_SIMD_SSE2 1
#include <emmintrin.h>
#endif

#if defined(__SSE4_1__) || (defined(_MSC_VER) && defined(__AVX__))
#define COLLISION_SIMD_SSE4 1
#include <smmintrin.h>
#endif

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
    void prepareForStateTransition();
    bool isInitialized() const { return m_initialized; }
    bool isShutdown() const { return m_isShutdown; }

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
        double cullingMs);

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
    void validateBuildingCollisionCoverage();

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

    // Collision prediction configuration - prevents diagonal tunneling through corners
    static constexpr float VELOCITY_PREDICTION_FACTOR = 1.15f;      // Expand AABBs by velocity*dt*factor to predict collisions
    static constexpr float FAST_VELOCITY_THRESHOLD = 250.0f;        // Velocity threshold for AABB expansion (pixels/frame)

    // Camera culling support
    struct CullingArea {
        float minX, minY, maxX, maxY;
        float bufferSize{COLLISION_CULLING_BUFFER}; // Buffer around camera view

        bool contains(float x, float y) const {
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        }
    };

    void buildActiveIndicesSOA(const CullingArea& cullingArea) const;
    CullingArea createDefaultCullingArea() const;


    bool m_initialized{false};
    bool m_isShutdown{false};
    AABB m_worldBounds{0,0, 100000.0f, 100000.0f}; // large default box (centered at 0,0)

    // NEW SOA STORAGE SYSTEM: Following AIManager pattern for better cache performance
    struct CollisionStorage {
        // Hot data: Accessed every frame during collision detection
        struct HotData {
            Vector2D position;           // 8 bytes: Current position (center of AABB)
            Vector2D velocity;           // 8 bytes: Current velocity
            Vector2D halfSize;           // 8 bytes: Half-width and half-height
            uint32_t layers;             // 4 bytes: Layer mask (what layer this body is on) - REVERTED from uint16_t
            uint32_t collidesWith;       // 4 bytes: Collision mask (what layers this body collides with) - REVERTED from uint16_t
            float restitution;           // 4 bytes: Bounce/restitution coefficient (moved mass/friction to cold data)
            uint8_t bodyType;            // 1 byte: BodyType enum (STATIC, KINEMATIC, DYNAMIC)
            uint8_t triggerTag;          // 1 byte: TriggerTag enum for triggers
            uint8_t active;              // 1 byte: Whether this body participates in collision detection
            uint8_t isTrigger;           // 1 byte: Whether this is a trigger body
            mutable uint8_t aabbDirty;   // 1 byte: Whether cached AABB needs updating

            // Cached AABB for performance - exactly 16 bytes (4 floats)
            mutable float aabbMinX, aabbMinY, aabbMaxX, aabbMaxY;

            // Padding to exactly 64 bytes: we're at 60, need 4 more bytes
            uint8_t _padding[4];

        };
        static_assert(sizeof(HotData) == 64, "HotData should be exactly 64 bytes for cache alignment");

        // Cold data: Rarely accessed, separated to avoid cache pollution
        struct ColdData {
            EntityWeakPtr entityWeak;    // Back-reference to entity
            Vector2D acceleration;       // Acceleration (rarely used)
            Vector2D lastPosition;       // Previous position for optimization
            AABB fullAABB;              // Full AABB (computed from position + halfSize)
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
    };
    std::unordered_map<HammerEngine::HierarchicalSpatialHash::CoarseCoord,
                       CoarseRegionStaticCache,
                       HammerEngine::HierarchicalSpatialHash::CoarseCoordHash,
                       HammerEngine::HierarchicalSpatialHash::CoarseCoordEq> m_coarseRegionStaticCache;

    // Track which coarse cell each dynamic body currently occupies
    std::unordered_map<size_t, HammerEngine::HierarchicalSpatialHash::CoarseCoord> m_bodyCoarseCell;

    // Cache statistics
    mutable size_t m_cacheHits{0};
    mutable size_t m_cacheMisses{0};

    // Current culling area for spatial queries
    mutable CullingArea m_currentCullingArea{0.0f, 0.0f, 0.0f, 0.0f};

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

            size_t expectedCollisions = expectedPairs / 2; // About 50% pair→collision ratio observed

            if (pairBuffer.capacity() < expectedPairs) {
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
    mutable size_t m_nextPoolIndex{0};

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
        double lastCullingMs{0.0};            // Time spent on culling operations
        double avgBroadphaseMs{0.0};          // Average broadphase time

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
            return bodyCount > 0 ? (100.0 * lastDynamicBodiesCulled) / bodyCount : 0.0;
        }

        double getStaticCullingRate() const {
            return bodyCount > 0 ? (100.0 * lastStaticBodiesCulled) / bodyCount : 0.0;
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

    // Thread-safe command queue for deferred collision body operations
    std::vector<PendingCommand> m_pendingCommands;
    mutable std::mutex m_commandQueueMutex;

    // Thread-safe access to collision storage (entityToIndex map and storage arrays)
    // shared_lock for reads (AI threads), unique_lock for writes (update thread)
    mutable std::shared_mutex m_storageMutex;
};

#endif // COLLISION_MANAGER_HPP
