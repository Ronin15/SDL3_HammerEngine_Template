/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/CollisionManager.hpp"
#include "core/Logger.hpp"
#include "managers/WorldManager.hpp"
#include "world/WorldData.hpp"
#include "managers/EventManager.hpp"
#include "events/WorldEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "managers/WorldManager.hpp"
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <map>
#include <chrono>
#include "utils/UniqueID.hpp"

using ::WorldManager;
using ::EventManager;
using ::HammerEngine::ObstacleType;
using ::WorldLoadedEvent;
using ::WorldGeneratedEvent;
using ::WorldUnloadedEvent;
using ::TileChangedEvent;
using ::EventTypeId;

bool CollisionManager::init() {
    if (m_initialized) return true;
    m_bodies.clear();
    m_staticHash.clear();
    m_dynamicHash.clear();
    subscribeWorldEvents();
    COLLISION_INFO("Initialized: cleared bodies and spatial hash");
    // Forward collision notifications to EventManager
    addCollisionCallback([](const HammerEngine::CollisionInfo &info){
        EventManager::Instance().triggerCollision(info, EventManager::DispatchMode::Deferred);
    });
    m_initialized = true;
    m_isShutdown = false;
    return true;
}

void CollisionManager::clean() {
    if (!m_initialized || m_isShutdown) return;
    m_isShutdown = true;
    m_bodies.clear();
    m_staticHash.clear();
    m_dynamicHash.clear();
    m_callbacks.clear();
    m_initialized = false;
    COLLISION_INFO("Cleaned and shut down");
}

void CollisionManager::prepareForStateTransition() {
    COLLISION_INFO("Preparing CollisionManager for state transition...");
    
    if (!m_initialized || m_isShutdown) {
        COLLISION_WARN("CollisionManager not initialized or already shutdown during state transition");
        return;
    }

    // Clear all collision bodies (both dynamic and static)
    size_t bodyCount = m_bodies.size();
    m_bodies.clear();
    
    // Clear spatial hash completely 
    m_staticHash.clear();
    m_dynamicHash.clear();
    
    // Clear caches to prevent dangling references to deleted bodies
    m_broadphaseCache.invalidateStaticCache();  // Clear static cache
    m_broadphaseCache.resetFrame();              // Clear frame cache  
    m_collisionPool.resetFrame();                // Clear collision buffers
    
    // Clear trigger tracking state completely
    m_activeTriggerPairs.clear();
    m_triggerCooldownUntil.clear();
    
    // Reset trigger cooldown settings
    m_defaultTriggerCooldownSec = 0.0f;
    
    // Clear all collision callbacks (these should be re-registered by new states)
    m_callbacks.clear();
    
    // Reset performance stats for clean slate
    m_perf = PerfStats{};
    
    // Reset world bounds to default
    m_worldBounds = AABB(0, 0, 100000.0f, 100000.0f);
    
    // Reset syncing state
    m_isSyncing = false;
    
    // Reset verbose logging to default
    m_verboseLogs = false;
    
    COLLISION_INFO("CollisionManager state transition complete - removed " + std::to_string(bodyCount) + " bodies, cleared all state");
}

void CollisionManager::setWorldBounds(float minX, float minY, float maxX, float maxY) {
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float hw = (maxX - minX) * 0.5f;
    float hh = (maxY - minY) * 0.5f;
    m_worldBounds = AABB(cx, cy, hw, hh);
    COLLISION_DEBUG("World bounds set: [" + std::to_string(minX) + "," + std::to_string(minY) +
                    "] - [" + std::to_string(maxX) + "," + std::to_string(maxY) + "]");
}

void CollisionManager::addBody(EntityID id, const AABB& aabb, BodyType type) {
    auto body = std::make_shared<CollisionBody>();
    body->id = id;
    body->aabb = aabb;
    body->type = type;
    m_bodies[id] = body;
    
    // Insert into appropriate spatial hash based on body type
    if (type == BodyType::STATIC) {
        m_staticHash.insert(id, aabb);
    } else {
        m_dynamicHash.insert(id, aabb);
    }
    
    // Invalidate static cache if adding a static body
    if (type == BodyType::STATIC) {
        invalidateStaticCache();
        
        // Notify PathfinderManager of static obstacle changes
        // Use immediate mode for reliable event delivery
        EventManager::Instance().triggerCollisionObstacleChanged(
            aabb.center, 
            std::max(aabb.halfSize.getX(), aabb.halfSize.getY()) + 32.0f,
            "Static obstacle added (ID: " + std::to_string(id) + ")",
            EventManager::DispatchMode::Immediate
        );
    }
    
    if (type == BodyType::KINEMATIC) {
        COLLISION_DEBUG("Added KINEMATIC body - ID: " + std::to_string(id) + 
                       ", Total bodies: " + std::to_string(m_bodies.size()) +
                       ", Kinematic count should now be: " + std::to_string(getKinematicBodyCount()));
    }
}

void CollisionManager::addBody(EntityPtr entity, const AABB& aabb, BodyType type) {
    if (!entity) return;
    EntityID id = entity->getID();
    auto body = std::make_shared<CollisionBody>();
    body->id = id;
    body->aabb = aabb;
    body->type = type;
    body->entityWeak = entity;
    m_bodies[id] = body;
    
    // Insert into appropriate spatial hash based on body type
    if (type == BodyType::STATIC) {
        m_staticHash.insert(id, aabb);
    } else {
        m_dynamicHash.insert(id, aabb);
    }
    
    // Invalidate static cache if adding a static body
    if (type == BodyType::STATIC) {
        invalidateStaticCache();
    }
}

void CollisionManager::attachEntity(EntityID id, EntityPtr entity) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) {
        it->second->entityWeak = entity;
    }
}

void CollisionManager::removeBody(EntityID id) {
    // Check if this was a static body before removing it
    bool wasStatic = false;
    Vector2D bodyCenter;
    float bodyRadius = 32.0f;
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) {
        if (it->second->type == BodyType::STATIC) {
            wasStatic = true;
            bodyCenter = it->second->aabb.center;
            bodyRadius = std::max(it->second->aabb.halfSize.getX(), it->second->aabb.halfSize.getY()) + 32.0f;
            m_staticHash.remove(id);
        } else {
            m_dynamicHash.remove(id);
        }
    }
    
    m_bodies.erase(id);
    
    // Invalidate static cache if removing a static body
    if (wasStatic) {
        invalidateStaticCache();
        
        // Notify PathfinderManager of static obstacle removal
        // Use immediate mode for reliable event delivery
        EventManager::Instance().triggerCollisionObstacleChanged(
            bodyCenter, 
            bodyRadius,
            "Static obstacle removed (ID: " + std::to_string(id) + ")",
            EventManager::DispatchMode::Immediate
        );
    }
    
    if (m_verboseLogs) { COLLISION_DEBUG("removeBody id=" + std::to_string(id)); }
}

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->enabled = enabled;
    if (m_verboseLogs) { COLLISION_DEBUG("setBodyEnabled id=" + std::to_string(id) + " -> " + (enabled ? std::string("true") : std::string("false"))); }
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) { it->second->layer = layerMask; it->second->collidesWith = collideMask; }
    if (m_verboseLogs) {
        COLLISION_DEBUG("setBodyLayer id=" + std::to_string(id) +
                        ", layer=" + std::to_string(layerMask) + ", mask=" + std::to_string(collideMask));
    }
}

void CollisionManager::setKinematicPose(EntityID id, const Vector2D& center) {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return;
    it->second->aabb.center = center;
    
    // Update the appropriate spatial hash based on body type
    if (it->second->type == BodyType::STATIC) {
        m_staticHash.update(id, it->second->aabb);
    } else {
        m_dynamicHash.update(id, it->second->aabb);
    }
    if (m_verboseLogs) {
        COLLISION_DEBUG("setKinematicPose id=" + std::to_string(id) +
                        ", center=(" + std::to_string(center.getX()) + "," + std::to_string(center.getY()) + ")");
    }
}

void CollisionManager::setVelocity(EntityID id, const Vector2D& v) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->velocity = v;
    if (m_verboseLogs) {
        COLLISION_DEBUG("setVelocity id=" + std::to_string(id) +
                        ", v=(" + std::to_string(v.getX()) + "," + std::to_string(v.getY()) + ")");
    }
}

void CollisionManager::updateKinematicBatch(const std::vector<KinematicUpdate>& updates) {
    if (updates.empty()) return;
    
    // PERFORMANCE OPTIMIZATION: Batch process all kinematic updates
    // This reduces hash table lookups and spatial hash updates from O(n) to O(1) operations
    
    // Phase 1: Update positions and velocities in batch (minimize hash lookups)
    std::vector<std::pair<EntityID, AABB>> spatialUpdates;
    spatialUpdates.reserve(updates.size());
    
    size_t validUpdates = 0;
    for (const auto& kinematicUpdate : updates) {
        auto it = m_bodies.find(kinematicUpdate.id);
        if (it != m_bodies.end() && it->second->type == BodyType::KINEMATIC) {
            // Update position and velocity
            it->second->aabb.center = kinematicUpdate.position;
            it->second->velocity = kinematicUpdate.velocity;
            
            // Queue for spatial hash update
            spatialUpdates.emplace_back(kinematicUpdate.id, it->second->aabb);
            validUpdates++;
        }
    }
    
    // Phase 2: Batch update spatial hash (single pass through hash structure)
    if (!spatialUpdates.empty()) {
        // Update all entities in spatial hash at once - much more cache-friendly
        for (const auto& [entityId, aabb] : spatialUpdates) {
            auto bodyIt = m_bodies.find(entityId);
            if (bodyIt != m_bodies.end()) {
                // Update the appropriate spatial hash based on body type
                if (bodyIt->second->type == BodyType::STATIC) {
                    m_staticHash.update(entityId, aabb);
                } else {
                    m_dynamicHash.update(entityId, aabb);
                }
            }
        }
    }
    
    // Debug logging (only when verbose and with summary to reduce spam)
    if (m_verboseLogs && !updates.empty()) {
        COLLISION_DEBUG("updateKinematicBatch: processed " + std::to_string(validUpdates) + 
                       "/" + std::to_string(updates.size()) + " kinematic updates");
    }
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->isTrigger = isTrigger;
    // Reduced debug spam - only log for non-world triggers  
    if (id < (1ull << 61)) { // Only log for non-world objects (player, NPCs, etc.)
        COLLISION_DEBUG("setBodyTrigger id=" + std::to_string(id) + " -> " + (isTrigger ? std::string("true") : std::string("false")));
    }
}

void CollisionManager::setBodyTriggerTag(EntityID id, HammerEngine::TriggerTag tag) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->triggerTag = tag;
    // Removed debug spam - too many trigger tags created during world build
}

EntityID CollisionManager::createTriggerArea(const AABB& aabb,
                               HammerEngine::TriggerTag tag,
                               uint32_t layerMask,
                               uint32_t collideMask) {
    EntityID id = HammerEngine::UniqueID::generate();
    addBody(id, aabb, BodyType::STATIC);
    setBodyLayer(id, layerMask, collideMask);
    setBodyTrigger(id, true);
    setBodyTriggerTag(id, tag);
    return id;
}

EntityID CollisionManager::createTriggerAreaAt(float cx, float cy, float halfW, float halfH,
                                 HammerEngine::TriggerTag tag,
                                 uint32_t layerMask,
                                 uint32_t collideMask) {
    return createTriggerArea(AABB(cx, cy, halfW, halfH), tag, layerMask, collideMask);
}

void CollisionManager::setTriggerCooldown(EntityID triggerId, float seconds) {
    using clock = std::chrono::steady_clock;
    auto now = clock::now();
    m_triggerCooldownUntil[triggerId] = now + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(seconds));
}

size_t CollisionManager::createTriggersForWaterTiles(HammerEngine::TriggerTag tag) {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return 0;
    size_t created = 0;
    const float tileSize = 32.0f;
    const int h = static_cast<int>(world->grid.size());
    for (int y = 0; y < h; ++y) {
        const int w = static_cast<int>(world->grid[y].size());
        for (int x = 0; x < w; ++x) {
            const auto& tile = world->grid[y][x];
            if (!tile.isWater) continue;
            float cx = x * tileSize + tileSize * 0.5f;
            float cy = y * tileSize + tileSize * 0.5f;
            AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
            // Use a distinct prefix for triggers to avoid id collisions with static colliders
            EntityID id = (static_cast<EntityID>(1ull) << 61) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
            if (m_bodies.find(id) == m_bodies.end()) {
                addBody(id, aabb, BodyType::STATIC);
                setBodyLayer(id, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
                setBodyTrigger(id, true);
                setBodyTriggerTag(id, tag);
                ++created;
            }
        }
    }
    if (created > 0) {
        COLLISION_INFO("Created water triggers: count=" + std::to_string(created));
    }
    return created;
}

size_t CollisionManager::createTriggersForObstacles() {
    // ROCK and TREE movement penalties are now handled by pathfinding system
    // This avoids creating thousands of trigger bodies that cause performance issues
    return 0;
}

size_t CollisionManager::createStaticObstacleBodies() {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return 0;
    
    size_t created = 0;
    const float tileSize = 32.0f;
    const int h = static_cast<int>(world->grid.size());
    
    for (int y = 0; y < h; ++y) {
        const int w = static_cast<int>(world->grid[y].size());
        for (int x = 0; x < w; ++x) {
            const auto& tile = world->grid[y][x];
            
            // Create solid collision bodies for BUILDING obstacles only
            // ROCK, TREE, WATER are handled as triggers with movement penalties
            if (tile.obstacleType == ObstacleType::BUILDING) {
                
                float cx = x * tileSize + tileSize * 0.5f;
                float cy = y * tileSize + tileSize * 0.5f;
                AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
                
                // Use a different prefix for obstacle bodies to avoid conflicts with triggers
                EntityID id = (static_cast<EntityID>(2ull) << 61) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
                
                if (m_bodies.find(id) == m_bodies.end()) {
                    addBody(id, aabb, BodyType::STATIC);
                    setBodyLayer(id, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
                    // These are solid bodies, not triggers
                    setBodyTrigger(id, false);
                    ++created;
                }
            }
        }
    }
    
    if (created > 0) {
        COLLISION_INFO("Created obstacle bodies: count=" + std::to_string(created));
    }
    return created;
}

void CollisionManager::resizeBody(EntityID id, float halfWidth, float halfHeight) {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return;
    auto &body = *it->second;
    body.aabb.halfSize = Vector2D(halfWidth, halfHeight);
    
    // Update the appropriate spatial hash based on body type
    if (body.type == BodyType::STATIC) {
        m_staticHash.update(id, body.aabb);
    } else {
        m_dynamicHash.update(id, body.aabb);
    }
    COLLISION_DEBUG("resizeBody id=" + std::to_string(id) +
                    ", halfW=" + std::to_string(halfWidth) + ", halfH=" + std::to_string(halfHeight));
}

bool CollisionManager::overlaps(EntityID a, EntityID b) const {
    auto ita = m_bodies.find(a); auto itb = m_bodies.find(b);
    if (ita == m_bodies.end() || itb == m_bodies.end()) return false;
    return ita->second->aabb.intersects(itb->second->aabb);
}

void CollisionManager::queryArea(const AABB& area, std::vector<EntityID>& out) const {
    // Query both static and dynamic hashes, combining results
    thread_local std::vector<EntityID> staticResults;
    thread_local std::vector<EntityID> dynamicResults;
    
    staticResults.clear();
    dynamicResults.clear();
    
    m_staticHash.query(area, staticResults);
    m_dynamicHash.query(area, dynamicResults);
    
    // Combine results efficiently
    out.clear();
    out.reserve(staticResults.size() + dynamicResults.size());
    out.insert(out.end(), staticResults.begin(), staticResults.end());
    out.insert(out.end(), dynamicResults.begin(), dynamicResults.end());
}

bool CollisionManager::getBodyCenter(EntityID id, Vector2D& outCenter) const {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return false;
    outCenter = it->second->aabb.center;
    return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return false;
    return it->second->type == BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return false;
    return it->second->type == BodyType::KINEMATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return false;
    return it->second->isTrigger;
}

void CollisionManager::broadphase(std::vector<std::pair<EntityID,EntityID>>& pairs) const {
    pairs.clear();
    pairs.reserve(m_bodies.size() * 2);
    
    // OPTIMIZATION: Use member containers to avoid repeated allocations
    // Access broadphase cache containers that are properly reset each frame
    auto& dynamicCandidates = m_collisionPool.dynamicCandidates;
    auto& staticCandidates = m_collisionPool.staticCandidates;
    auto& seenPairs = m_broadphaseCache.seenPairs;
    
    // Containers are already cleared by resetFrame() calls
    // Reserve space based on expected sizes
    const size_t bodyCount = m_bodies.size();
    seenPairs.reserve(bodyCount * 2);
    
    // CRITICAL OPTIMIZATION: Process only dynamic/kinematic bodies (static don't initiate collisions)
    for (const auto& kv : m_bodies) {
        const CollisionBody& body = *kv.second;
        if (!body.enabled || body.type == BodyType::STATIC) continue;
        
        // PERFORMANCE BOOST: Separate queries for dynamic vs static bodies
        // This prevents dynamic bodies from being checked against every static tile
        
        // 1. Query dynamic hash for dynamic-vs-dynamic collisions
        dynamicCandidates.clear();
        m_dynamicHash.query(body.aabb, dynamicCandidates);
        
        // Process dynamic candidates
        for (EntityID candidateId : dynamicCandidates) {
            if (candidateId == body.id) continue;
            
            auto candidateIt = m_bodies.find(candidateId);
            if (candidateIt == m_bodies.end()) continue;
            
            const CollisionBody& candidate = *candidateIt->second;
            if (!candidate.enabled) continue;
            
            // Quick collision mask check
            if ((body.collidesWith & candidate.layer) == 0) continue;
            
            // Create canonical pair key (smaller ID first)
            uint64_t pairKey;
            if (body.id < candidateId) {
                pairKey = (static_cast<uint64_t>(body.id) << 32) | candidateId;
            } else {
                pairKey = (static_cast<uint64_t>(candidateId) << 32) | body.id;
            }
            
            // Add unique pair
            if (seenPairs.emplace(pairKey).second) {
                pairs.emplace_back(std::min(body.id, candidateId), std::max(body.id, candidateId));
            }
        }
        
        // 2. Query static hash for dynamic-vs-static collisions (with caching)
        staticCandidates.clear();
        
        // Check if we have cached static bodies for this entity
        auto cacheIt = m_broadphaseCache.staticCache.find(body.id);
        bool useCache = (cacheIt != m_broadphaseCache.staticCache.end());
        
        if (useCache) {
            // Use cached static bodies if entity hasn't moved significantly
            auto& cachedQuery = cacheIt->second;
            Vector2D centerDelta = body.aabb.center - cachedQuery.lastQueryCenter;
            float distMoved = centerDelta.length();
            
            if (distMoved <= cachedQuery.maxQueryDistance * 0.25f) {
                // Entity moved less than 25% of query radius, use cache
                staticCandidates = cachedQuery.staticBodies;
            } else {
                useCache = false; // Cache invalid, need fresh query
            }
        }
        
        if (!useCache) {
            // Query spatial hash and update cache
            m_staticHash.query(body.aabb, staticCandidates);
            
            // Update cache for next frame
            auto& cachedQuery = m_broadphaseCache.staticCache[body.id];
            cachedQuery.staticBodies = staticCandidates;
            cachedQuery.lastQueryCenter = body.aabb.center;
            cachedQuery.maxQueryDistance = std::max(body.aabb.halfSize.getX(), body.aabb.halfSize.getY()) * 2.0f;
        }
        
        // Process static candidates (dynamic body vs static body only)
        for (EntityID staticId : staticCandidates) {
            auto staticIt = m_bodies.find(staticId);
            if (staticIt == m_bodies.end()) continue;
            
            const CollisionBody& staticBody = *staticIt->second;
            if (!staticBody.enabled) continue;
            
            // Quick collision mask check
            if ((body.collidesWith & staticBody.layer) == 0) continue;
            
            // No need for pair deduplication with static bodies since we don't reverse check
            pairs.emplace_back(body.id, staticId);
        }
    }
    
    // PERFORMANCE OPTIMIZATIONS IMPLEMENTED:
    // 1. Separate static and dynamic spatial hash queries
    // 2. Prevents N×M collision checks between dynamic and static bodies
    // 3. Static bodies never initiate collision checks (only receive them)
    // 4. Dynamic-vs-dynamic pairs are deduplicated, dynamic-vs-static are not (no reverse check)
    // 5. Persistent cache for static body queries with movement-based invalidation
    // 6. Member containers with proper per-frame reset (no thread_local memory leak)
    // 7. Expected 20x performance improvement for world with many static tiles
}

void CollisionManager::narrowphase(const std::vector<std::pair<EntityID,EntityID>>& pairs,
                                 std::vector<CollisionInfo>& collisions) const {
    collisions.clear();
    for (auto [aId, bId] : pairs) {
        const auto ita = m_bodies.find(aId);
        const auto itb = m_bodies.find(bId);
        if (ita == m_bodies.end() || itb == m_bodies.end()) continue;
        const CollisionBody& A = *ita->second;
        const CollisionBody& B = *itb->second;
        if (!A.aabb.intersects(B.aabb)) continue;
        float dxLeft = B.aabb.right() - A.aabb.left();
        float dxRight = A.aabb.right() - B.aabb.left();
        float dyTop = B.aabb.bottom() - A.aabb.top();
        float dyBottom = A.aabb.bottom() - B.aabb.top();
        float minPen = dxLeft; Vector2D normal(-1,0);
        if (dxRight < minPen) { minPen = dxRight; normal = Vector2D(1,0); }
        if (dyTop < minPen) { minPen = dyTop; normal = Vector2D(0,-1); }
        if (dyBottom < minPen) { minPen = dyBottom; normal = Vector2D(0,1); }
        collisions.push_back(CollisionInfo{aId, bId, normal, minPen, (A.isTrigger || B.isTrigger)});
    }
}

void CollisionManager::resolve(const CollisionInfo& info) {
    if (info.trigger) return;
    auto ita = m_bodies.find(info.a);
    auto itb = m_bodies.find(info.b);
    if (ita == m_bodies.end() || itb == m_bodies.end()) return;
    CollisionBody& A = *ita->second;
    CollisionBody& B = *itb->second;
    const float push = info.penetration * 0.5f;
    
    // Apply position corrections (including to kinematic bodies)
    if (A.type != BodyType::STATIC && B.type != BodyType::STATIC) {
        A.aabb.center += info.normal * (-push);
        B.aabb.center += info.normal * ( push);
    } else if (A.type != BodyType::STATIC) {
        A.aabb.center += info.normal * (-info.penetration);
    } else if (B.type != BodyType::STATIC) {
        B.aabb.center += info.normal * ( info.penetration);
    }
    
    // Immediately sync position corrections back to kinematic entities
    // This ensures AI system sees the corrected positions
    if (A.type == BodyType::KINEMATIC) {
        if (auto ent = A.entityWeak.lock()) {
            ent->setPosition(A.aabb.center);
        }
    }
    if (B.type == BodyType::KINEMATIC) {
        if (auto ent = B.entityWeak.lock()) {
            ent->setPosition(B.aabb.center);
        }
    }
    auto dampen = [&](CollisionBody& body) {
        float nx = info.normal.getX();
        float ny = info.normal.getY();
        float vdotn = body.velocity.getX()*nx + body.velocity.getY()*ny;
        if (vdotn > 0) return;
        Vector2D vn(nx*vdotn, ny*vdotn);
        body.velocity -= vn * (1.0f + body.restitution);
    };
    dampen(A); dampen(B);
    // Small tangential slide to reduce clumping for NPC-vs-NPC only (skip Player)
    auto isPlayer = [](const CollisionBody& b){ return (b.layer & CollisionLayer::Layer_Player) != 0; };
    if (A.type == BodyType::DYNAMIC && B.type == BodyType::DYNAMIC && !isPlayer(A) && !isPlayer(B)) {
        Vector2D tangent(-info.normal.getY(), info.normal.getX());
        // Scale slide by penetration, clamp to safe range
        float slideBoost = std::min(5.0f, std::max(0.5f, info.penetration * 5.0f));
        if (A.id < B.id) {
            A.velocity += tangent * slideBoost;
            B.velocity -= tangent * slideBoost;
        } else {
            A.velocity -= tangent * slideBoost;
            B.velocity += tangent * slideBoost;
        }
    }
    auto clampSpeed = [](CollisionBody& body){
        const float maxSpeed = 300.0f;
        float lx = body.velocity.length();
        if (lx > maxSpeed && lx > 0.0f) {
            Vector2D dir = body.velocity; dir.normalize();
            body.velocity = dir * maxSpeed;
        }
    };
    clampSpeed(A); clampSpeed(B);
    
    // Update the appropriate spatial hash for each body based on type
    if (A.type == BodyType::STATIC) {
        m_staticHash.update(A.id, A.aabb);
    } else {
        m_dynamicHash.update(A.id, A.aabb);
    }
    
    if (B.type == BodyType::STATIC) {
        m_staticHash.update(B.id, B.aabb);
    } else {
        m_dynamicHash.update(B.id, B.aabb);
    }
}

void CollisionManager::update(float dt) {
    (void)dt;
    if (!m_initialized || m_isShutdown) return;
    
    // Initialize collision detection for this frame
    
    // Initialize object pools and broadphase cache for this frame
    m_collisionPool.ensureCapacity(m_bodies.size());
    m_collisionPool.resetFrame();
    m_broadphaseCache.resetFrame();  // Clear per-frame tracking (keeps static cache)
    
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    broadphase(m_collisionPool.pairBuffer);
    auto t1 = clock::now();

    narrowphase(m_collisionPool.pairBuffer, m_collisionPool.collisionBuffer);
    auto t2 = clock::now();

    for (const auto& c : m_collisionPool.collisionBuffer) {
        resolve(c);
        for (const auto& cb : m_callbacks) { cb(c); }
    }
    auto t3 = clock::now();
    if (m_verboseLogs && !m_collisionPool.collisionBuffer.empty()) {
        COLLISION_DEBUG("Resolved collisions: count=" + std::to_string(m_collisionPool.collisionBuffer.size()));
    }
    // Reflect resolved poses back to entities so callers see corrected transforms
    // Synchronize collision results back to entities
    // Skip kinematic bodies since they manage their own positions through AI/input
    m_isSyncing = true;
    for (auto& kv : m_bodies) {
        auto& b = *kv.second;
        if (b.type != HammerEngine::BodyType::KINEMATIC) {
            if (auto ent = b.entityWeak.lock()) {
                ent->setPosition(b.aabb.center);
                ent->setVelocity(b.velocity);
            }
        }
    }
    m_isSyncing = false;
    auto t4 = clock::now();

    // Trigger-only world events: Player vs Trigger OnEnter
    auto makeKey = [](EntityID a, EntityID b) -> uint64_t {
        uint64_t x = static_cast<uint64_t>(a);
        uint64_t y = static_cast<uint64_t>(b);
        if (x > y) std::swap(x, y);
        // Simple mix (not cryptographic); sufficient for set keys
        return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
    };

    auto now = clock::now();
    std::unordered_set<uint64_t> currentPairs;
    currentPairs.reserve(m_collisionPool.collisionBuffer.size());
    for (const auto &c : m_collisionPool.collisionBuffer) {
        auto ita = m_bodies.find(c.a);
        auto itb = m_bodies.find(c.b);
        if (ita == m_bodies.end() || itb == m_bodies.end()) continue;
        const CollisionBody &A = *ita->second;
        const CollisionBody &B = *itb->second;

        auto isPlayer = [](const CollisionBody &b){ return (b.layer & CollisionLayer::Layer_Player) != 0; };
        const CollisionBody *playerBody = nullptr;
        const CollisionBody *triggerBody = nullptr;

        if (isPlayer(A) && B.isTrigger) { playerBody = &A; triggerBody = &B; }
        else if (isPlayer(B) && A.isTrigger) { playerBody = &B; triggerBody = &A; }
        else { continue; }

        uint64_t key = makeKey(playerBody->id, triggerBody->id);
        currentPairs.insert(key);
        if (!m_activeTriggerPairs.count(key)) {
            // Cooldown check per trigger
            auto cdIt = m_triggerCooldownUntil.find(triggerBody->id);
            bool cooled = (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);
            if (cooled) {
                WorldTriggerEvent evt(playerBody->id, triggerBody->id, triggerBody->triggerTag,
                                      playerBody->aabb.center, TriggerPhase::Enter);
                EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);
                COLLISION_INFO("Trigger Enter: player=" + std::to_string(playerBody->id) +
                               ", trigger=" + std::to_string(triggerBody->id) +
                               ", tag=" + std::to_string(static_cast<int>(triggerBody->triggerTag)));
                if (m_defaultTriggerCooldownSec > 0.0f) {
                    m_triggerCooldownUntil[triggerBody->id] = now + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(m_defaultTriggerCooldownSec));
                }
            }
            m_activeTriggerPairs.emplace(key, std::make_pair(playerBody->id, triggerBody->id));
        }
    }
    // Remove stale pairs (exited triggers) and dispatch Exit events
    for (auto it = m_activeTriggerPairs.begin(); it != m_activeTriggerPairs.end(); ) {
        if (!currentPairs.count(it->first)) {
            // OnExit
            EntityID playerId = it->second.first;
            EntityID triggerId = it->second.second;
            auto bt = m_bodies.find(triggerId);
            if (bt != m_bodies.end()) {
                WorldTriggerEvent evt(playerId, triggerId, bt->second->triggerTag,
                                      bt->second->aabb.center, TriggerPhase::Exit);
                EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);
                COLLISION_INFO("Trigger Exit: player=" + std::to_string(playerId) +
                               ", trigger=" + std::to_string(triggerId) +
                               ", tag=" + std::to_string(static_cast<int>(bt->second->triggerTag)));
            }
            it = m_activeTriggerPairs.erase(it);
        } else {
            ++it;
        }
    }

    // Perf metrics
    auto d01 = std::chrono::duration<double, std::milli>(t1 - t0).count();
    auto d12 = std::chrono::duration<double, std::milli>(t2 - t1).count();
    auto d23 = std::chrono::duration<double, std::milli>(t3 - t2).count();
    auto d34 = std::chrono::duration<double, std::milli>(t4 - t3).count();
    auto d04 = std::chrono::duration<double, std::milli>(t4 - t0).count();
    m_perf.lastBroadphaseMs = d01;
    m_perf.lastNarrowphaseMs = d12;
    m_perf.lastResolveMs = d23;
    m_perf.lastSyncMs = d34;
    m_perf.lastTotalMs = d04;
    m_perf.lastPairs = m_collisionPool.pairBuffer.size();
    m_perf.lastCollisions = m_collisionPool.collisionBuffer.size();
    m_perf.bodyCount = m_bodies.size();
    
    // Use moving window average instead of cumulative average
    m_perf.updateAverage(m_perf.lastTotalMs);
    m_perf.frames += 1;
    if (m_perf.lastTotalMs > 5.0) {
        COLLISION_WARN("Slow frame: totalMs=" + std::to_string(m_perf.lastTotalMs) +
                       ", pairs=" + std::to_string(m_perf.lastPairs) +
                       ", collisions=" + std::to_string(m_perf.lastCollisions));
    }

    // Periodic collision statistics (every 300 frames like AIManager)
    if (m_perf.frames % 300 == 0 && m_perf.bodyCount > 0) {
        COLLISION_DEBUG("Collision Summary - Bodies: " + std::to_string(m_perf.bodyCount) +
                       ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                       ", Broadphase: " + std::to_string(m_perf.lastBroadphaseMs) + "ms" +
                       ", Narrowphase: " + std::to_string(m_perf.lastNarrowphaseMs) + "ms" +
                       ", Last Pairs: " + std::to_string(m_perf.lastPairs) +
                       ", Last Collisions: " + std::to_string(m_perf.lastCollisions));
    }
}

void CollisionManager::addCollisionCallback(CollisionCB cb) { m_callbacks.push_back(std::move(cb)); }

void CollisionManager::logCollisionStatistics() const {
    size_t staticBodies = getStaticBodyCount();
    size_t kinematicBodies = getKinematicBodyCount();
    size_t dynamicBodies = getBodyCount() - staticBodies - kinematicBodies;
    
    COLLISION_INFO("Collision Statistics:");
    COLLISION_INFO("  Total Bodies: " + std::to_string(getBodyCount()));
    COLLISION_INFO("  Static Bodies: " + std::to_string(staticBodies) + " (obstacles + triggers)");
    COLLISION_INFO("  Kinematic Bodies: " + std::to_string(kinematicBodies) + " (NPCs)");
    COLLISION_INFO("  Dynamic Bodies: " + std::to_string(dynamicBodies) + " (player, projectiles)");
    
    // Count bodies by layer
    std::map<uint32_t, size_t> layerCounts;
    for (const auto& kv : m_bodies) {
        const auto& body = *kv.second;
        layerCounts[body.layer]++;
    }
    
    COLLISION_INFO("  Layer Distribution:");
    for (const auto& layerCount : layerCounts) {
        std::string layerName;
        switch (layerCount.first) {
            case CollisionLayer::Layer_Default: layerName = "Default"; break;
            case CollisionLayer::Layer_Player: layerName = "Player"; break;
            case CollisionLayer::Layer_Enemy: layerName = "Enemy"; break;
            case CollisionLayer::Layer_Environment: layerName = "Environment"; break;
            case CollisionLayer::Layer_Projectile: layerName = "Projectile"; break;
            case CollisionLayer::Layer_Trigger: layerName = "Trigger"; break;
            default: layerName = "Unknown"; break;
        }
        COLLISION_INFO("    " + layerName + ": " + std::to_string(layerCount.second));
    }
}

size_t CollisionManager::getStaticBodyCount() const {
    return std::count_if(m_bodies.begin(), m_bodies.end(),
                         [](const auto& kv) { return kv.second->type == BodyType::STATIC; });
}

size_t CollisionManager::getKinematicBodyCount() const {
    return std::count_if(m_bodies.begin(), m_bodies.end(),
                         [](const auto& kv) { return kv.second->type == BodyType::KINEMATIC; });
}

void CollisionManager::rebuildStaticFromWorld() {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    // Remove any existing STATIC world bodies
    std::vector<EntityID> toRemove;
    for (const auto& kv : m_bodies) {
        if (isStatic(*kv.second)) toRemove.push_back(kv.first);
    }
    for (auto id : toRemove) removeBody(id);

    // Create solid collision bodies for obstacles and triggers for movement penalties
    size_t solidBodies = createStaticObstacleBodies();
    size_t waterTriggers = createTriggersForWaterTiles(HammerEngine::TriggerTag::Water);
    size_t obstacleTriggers = createTriggersForObstacles(); // Always returns 0 - obstacle penalties handled by pathfinding
    
    if (solidBodies > 0 || waterTriggers > 0) {
        COLLISION_INFO("World colliders built: solid=" + std::to_string(solidBodies) + 
                      ", water triggers=" + std::to_string(waterTriggers) +
                      ", obstacle triggers=" + std::to_string(obstacleTriggers));
        // Log detailed statistics for debugging
        logCollisionStatistics();
        // Invalidate static body cache since world changed
        invalidateStaticCache();
    }
}

void CollisionManager::onTileChanged(int x, int y) {
    const auto& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    const float tileSize = 32.0f;
    
    if (y >= 0 && y < static_cast<int>(world->grid.size()) && x >= 0 && x < static_cast<int>(world->grid[y].size())) {
        const auto& tile = world->grid[y][x];
        
        // Update water trigger for this tile
        EntityID trigId = (static_cast<EntityID>(1ull) << 61) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
        removeBody(trigId);
        if (tile.isWater) {
            float cx = x * tileSize + tileSize * 0.5f;
            float cy = y * tileSize + tileSize * 0.5f;
            AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
            addBody(trigId, aabb, BodyType::STATIC);
            setBodyLayer(trigId, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
            setBodyTrigger(trigId, true);
            setBodyTriggerTag(trigId, HammerEngine::TriggerTag::Water);
        }
        
        // Update solid obstacle collision body for this tile (BUILDING only)
        EntityID obstacleId = (static_cast<EntityID>(2ull) << 61) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
        removeBody(obstacleId);
        if (tile.obstacleType == ObstacleType::BUILDING) {
            float cx = x * tileSize + tileSize * 0.5f;
            float cy = y * tileSize + tileSize * 0.5f;
            AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
            addBody(obstacleId, aabb, BodyType::STATIC);
            setBodyLayer(obstacleId, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
            setBodyTrigger(obstacleId, false);
        }
        
        // ROCK and TREE movement penalties are handled by pathfinding system
        // No collision triggers needed for these obstacle types
        
        // Invalidate static cache since tile changed
        invalidateStaticCache();
    }
}

void CollisionManager::invalidateStaticCache() {
    m_broadphaseCache.invalidateStaticCache();
}

void CollisionManager::subscribeWorldEvents() {
    auto& em = EventManager::Instance();
    auto token = em.registerHandlerWithToken(EventTypeId::World, [this](const EventData& data){
        auto base = data.event;
        if (!base) return;
        if (auto loaded = std::dynamic_pointer_cast<WorldLoadedEvent>(base)) {
            (void)loaded;
            const auto& worldManager = WorldManager::Instance();
            float minX, minY, maxX, maxY;
            if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
                this->setWorldBounds(minX, minY, maxX, maxY);
            }
            COLLISION_INFO("World loaded - rebuilding static colliders");
            this->rebuildStaticFromWorld();
            return;
        }
        if (auto generated = std::dynamic_pointer_cast<WorldGeneratedEvent>(base)) {
            (void)generated;
            const auto& worldManager = WorldManager::Instance();
            float minX, minY, maxX, maxY;
            if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
                this->setWorldBounds(minX, minY, maxX, maxY);
            }
            COLLISION_INFO("World generated - rebuilding static colliders");
            this->rebuildStaticFromWorld();
            return;
        }
        if (auto unloaded = std::dynamic_pointer_cast<WorldUnloadedEvent>(base)) {
            (void)unloaded;
            std::vector<EntityID> toRemove;
            for (const auto& kv : m_bodies) if (isStatic(*kv.second)) toRemove.push_back(kv.first);
            for (auto id : toRemove) removeBody(id);
            COLLISION_INFO("World unloaded - removed static colliders: " + std::to_string(toRemove.size()));
            // Invalidate static cache since static bodies were removed
            invalidateStaticCache();
            return;
        }
        if (auto tileChanged = std::dynamic_pointer_cast<TileChangedEvent>(base)) {
            this->onTileChanged(tileChanged->getX(), tileChanged->getY());
            return;
        }
    });
    m_handlerTokens.push_back(token);
}
