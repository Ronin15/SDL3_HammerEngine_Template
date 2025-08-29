/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef COLLISION_MANAGER_HPP
#define COLLISION_MANAGER_HPP

#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstddef>

#include "entities/Entity.hpp" // EntityID
#include "collisions/CollisionBody.hpp"
#include "collisions/CollisionInfo.hpp"
#include "collisions/SpatialHash.hpp"
#include "collisions/TriggerTag.hpp"
#include "managers/EventManager.hpp"
#include <unordered_set>
#include <unordered_map>
#include <chrono>

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

    // World helpers: build triggers for water tiles (returns count created)
    size_t createTriggersForWaterTiles(HammerEngine::TriggerTag tag = HammerEngine::TriggerTag::Water);
    void resizeBody(EntityID id, float halfWidth, float halfHeight);

    // Queries
    bool overlaps(EntityID a, EntityID b) const;
    void queryArea(const AABB& area, std::vector<EntityID>& out) const;

    // World coupling
    void rebuildStaticFromWorld();                // build colliders from WorldManager grid
    void onTileChanged(int x, int y);             // update a specific cell
    void setWorldBounds(float minX, float minY, float maxX, float maxY);

    // Callbacks
    using CollisionCB = std::function<void(const CollisionInfo&)>;
    void addCollisionCallback(CollisionCB cb);
    void onCollision(CollisionCB cb) { addCollisionCallback(std::move(cb)); }

    // Metrics
    size_t getBodyCount() const { return m_bodies.size(); }
    bool isSyncing() const { return m_isSyncing; }

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
    SpatialHash m_hash{32.0f};
    std::vector<CollisionCB> m_callbacks;
    std::vector<EventManager::HandlerToken> m_handlerTokens;
    std::unordered_map<uint64_t, std::pair<EntityID,EntityID>> m_activeTriggerPairs; // OnEnter/Exit filtering
    std::unordered_map<EntityID, std::chrono::steady_clock::time_point> m_triggerCooldownUntil;
    float m_defaultTriggerCooldownSec{0.0f};

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
    };

public:
    PerfStats getPerfStats() const { return m_perf; }
    void resetPerfStats() { m_perf = PerfStats{}; }

private:
    PerfStats m_perf{};

    // Helpers
    static inline bool isStatic(const CollisionBody& b) {
        return b.type == BodyType::STATIC;
    }

    // Guard to avoid feedback when syncing entity transforms
    bool m_isSyncing{false};
};

#endif // COLLISION_MANAGER_HPP
