/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_MANAGER_HPP
#define COLLISION_MANAGER_HPP

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstddef>
#include <chrono>

#include "entities/Entity.hpp" // EntityID
#include "collisions/CollisionBody.hpp"
#include "collisions/CollisionInfo.hpp"
#include "collisions/SpatialHash.hpp"
#include "collisions/TriggerTag.hpp"
#include "managers/EventManager.hpp"

using HammerEngine::AABB;
using HammerEngine::BodyType;
using HammerEngine::CollisionInfo;
using HammerEngine::CollisionBody;
using HammerEngine::SpatialHash;
using HammerEngine::CollisionLayer;

class CollisionManager {
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

    // Bodies
    void addBody(EntityID id, const AABB& aabb, BodyType type);
    void addBody(EntityPtr entity, const AABB& aabb, BodyType type);
    void attachEntity(EntityID id, EntityPtr entity);
    void removeBody(EntityID id);
    void setBodyEnabled(EntityID id, bool enabled);
    void setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask);
    void setKinematicPose(EntityID id, const Vector2D& center);
    void setVelocity(EntityID id, const Vector2D& v);
    
    // Batch updates for performance optimization (AI entities)
    struct KinematicUpdate {
        EntityID id;
        Vector2D position;
        Vector2D velocity;
        
        KinematicUpdate(EntityID entityId, const Vector2D& pos, const Vector2D& vel = Vector2D(0, 0))
            : id(entityId), position(pos), velocity(vel) {}
    };
    void updateKinematicBatch(const std::vector<KinematicUpdate>& updates);
    
    void setBodyTrigger(EntityID id, bool isTrigger);
    void setBodyTriggerTag(EntityID id, HammerEngine::TriggerTag tag);
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
    void resizeBody(EntityID id, float halfWidth, float halfHeight);

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
    void invalidateStaticCache();                 // call when world geometry changes

    // Callbacks
    using CollisionCB = std::function<void(const CollisionInfo&)>;
    void addCollisionCallback(CollisionCB cb);
    void onCollision(CollisionCB cb) { addCollisionCallback(std::move(cb)); }

    // Metrics
    size_t getBodyCount() const { return m_bodies.size(); }
    bool isSyncing() const { return m_isSyncing; }
    
    // Debug utilities
    void logCollisionStatistics() const;
    size_t getStaticBodyCount() const;
    size_t getKinematicBodyCount() const;

private:
    CollisionManager() = default;
    ~CollisionManager() { if (!m_isShutdown) clean(); }
    CollisionManager(const CollisionManager&) = delete;
    CollisionManager& operator=(const CollisionManager&) = delete;

    void broadphase(std::vector<std::pair<EntityID,EntityID>>& pairs) const;
    void narrowphase(const std::vector<std::pair<EntityID,EntityID>>& pairs,
                     std::vector<CollisionInfo>& collisions) const;
    void resolve(const CollisionInfo& info);
    void subscribeWorldEvents(); // hook to world events (future)

    bool m_initialized{false};
    bool m_isShutdown{false};
    AABB m_worldBounds{0,0, 100000.0f, 100000.0f}; // large default box (centered at 0,0)

    // storage
    std::unordered_map<EntityID, std::shared_ptr<CollisionBody>> m_bodies;
    
    // PERFORMANCE OPTIMIZATION: Separate spatial hashes for static vs dynamic bodies
    // This prevents dynamic bodies from checking against every static tile
    SpatialHash m_staticHash{64.0f, 2.0f};   // Static bodies (world tiles, buildings)
    SpatialHash m_dynamicHash{64.0f, 2.0f};  // Dynamic/kinematic bodies (NPCs, player)
    std::vector<CollisionCB> m_callbacks;
    std::vector<EventManager::HandlerToken> m_handlerTokens;
    std::unordered_map<uint64_t, std::pair<EntityID,EntityID>> m_activeTriggerPairs; // OnEnter/Exit filtering
    std::unordered_map<EntityID, std::chrono::steady_clock::time_point> m_triggerCooldownUntil;
    float m_defaultTriggerCooldownSec{0.0f};
    
    // Object pools for collision processing
    struct CollisionPool {
        std::vector<std::pair<EntityID, EntityID>> pairBuffer;
        std::vector<EntityID> candidateBuffer;
        std::vector<CollisionInfo> collisionBuffer;
        std::vector<EntityID> dynamicCandidates;  // For broadphase dynamic queries
        std::vector<EntityID> staticCandidates;   // For broadphase static queries
        
        void ensureCapacity(size_t bodyCount) {
            size_t expectedPairs = bodyCount * 4; // Conservative estimate
            if (pairBuffer.capacity() < expectedPairs) {
                pairBuffer.reserve(expectedPairs);
                candidateBuffer.reserve(bodyCount * 2);
                collisionBuffer.reserve(expectedPairs / 4);
                dynamicCandidates.reserve(32);    // Fewer dynamic bodies expected
                staticCandidates.reserve(128);     // More static bodies expected
            }
        }
        
        void resetFrame() {
            pairBuffer.clear();
            candidateBuffer.clear();
            collisionBuffer.clear();
            dynamicCandidates.clear();
            staticCandidates.clear();
            // Vectors retain capacity
        }
    };
    
    mutable CollisionPool m_collisionPool;
    
    // Broadphase optimization: persistent containers to avoid allocations
    struct BroadphaseCache {
        std::unordered_set<uint64_t> seenPairs;
        std::unordered_map<EntityID, const CollisionBody*> fastBodyLookup;
        
        // Static body query cache - cache spatial queries for static bodies
        struct StaticQueryCache {
            std::vector<EntityID> staticBodies;
            Vector2D lastQueryCenter;
            float maxQueryDistance;
        };
        std::unordered_map<EntityID, StaticQueryCache> staticCache;
        uint64_t staticCacheVersion{0}; // Incremented when static bodies change
        
        void ensureCapacity(size_t bodyCount) {
            if (fastBodyLookup.size() == 0) {
                fastBodyLookup.reserve(bodyCount);
                seenPairs.reserve(bodyCount * 2);
                staticCache.reserve(bodyCount / 4); // Estimate dynamic bodies that need static caching
            }
        }
        
        void resetFrame() {
            fastBodyLookup.clear();
            seenPairs.clear();
            // staticCache persists across frames
        }
        
        void invalidateStaticCache() {
            staticCache.clear();
            staticCacheVersion++;
        }
    };
    
    mutable BroadphaseCache m_broadphaseCache;

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
    };

public:
    PerfStats getPerfStats() const { return m_perf; }
    void resetPerfStats() { m_perf = PerfStats{}; }
    void setVerboseLogging(bool enabled) { m_verboseLogs = enabled; }

private:
    PerfStats m_perf{};
    bool m_verboseLogs{false};

    // Helpers
    static inline bool isStatic(const CollisionBody& b) {
        return b.type == BodyType::STATIC;
    }

    // Guard to avoid feedback when syncing entity transforms
    bool m_isSyncing{false};
};

#endif // COLLISION_MANAGER_HPP
