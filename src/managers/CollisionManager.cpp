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
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include "utils/UniqueID.hpp"

using ::WorldManager;
using ::EventManager;
using ::WorldLoadedEvent;
using ::WorldGeneratedEvent;
using ::WorldUnloadedEvent;
using ::TileChangedEvent;
using ::EventTypeId;

bool CollisionManager::init() {
    if (m_initialized) return true;
    m_bodies.clear();
    m_hash.clear();
    subscribeWorldEvents();
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
    m_hash.clear();
    m_callbacks.clear();
    m_initialized = false;
}

void CollisionManager::setWorldBounds(float minX, float minY, float maxX, float maxY) {
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float hw = (maxX - minX) * 0.5f;
    float hh = (maxY - minY) * 0.5f;
    m_worldBounds = AABB(cx, cy, hw, hh);
}

void CollisionManager::addBody(EntityID id, const AABB& aabb, BodyType type) {
    auto body = std::make_shared<CollisionBody>();
    body->id = id;
    body->aabb = aabb;
    body->type = type;
    m_bodies[id] = body;
    m_hash.insert(id, aabb);
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
    m_hash.insert(id, aabb);
}

void CollisionManager::attachEntity(EntityID id, EntityPtr entity) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) {
        it->second->entityWeak = entity;
    }
}

void CollisionManager::removeBody(EntityID id) {
    m_bodies.erase(id);
    m_hash.remove(id);
}

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->enabled = enabled;
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) { it->second->layer = layerMask; it->second->collidesWith = collideMask; }
}

void CollisionManager::setKinematicPose(EntityID id, const Vector2D& center) {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return;
    it->second->aabb.center = center;
    m_hash.update(id, it->second->aabb);
}

void CollisionManager::setVelocity(EntityID id, const Vector2D& v) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->velocity = v;
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->isTrigger = isTrigger;
}

void CollisionManager::setBodyTriggerTag(EntityID id, HammerEngine::TriggerTag tag) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->triggerTag = tag;
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

void CollisionManager::resizeBody(EntityID id, float halfWidth, float halfHeight) {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return;
    auto &body = *it->second;
    body.aabb.halfSize = Vector2D(halfWidth, halfHeight);
    m_hash.update(id, body.aabb);
}

bool CollisionManager::overlaps(EntityID a, EntityID b) const {
    auto ita = m_bodies.find(a); auto itb = m_bodies.find(b);
    if (ita == m_bodies.end() || itb == m_bodies.end()) return false;
    return ita->second->aabb.intersects(itb->second->aabb);
}

void CollisionManager::queryArea(const AABB& area, std::vector<EntityID>& out) const {
    m_hash.query(area, out);
}

void CollisionManager::broadphase(std::vector<std::pair<EntityID,EntityID>>& pairs) const {
    pairs.clear();
    std::vector<EntityID> candidates;
    for (const auto& kv : m_bodies) {
        const CollisionBody& b = *kv.second;
        if (!b.enabled) continue;
        candidates.clear();
        m_hash.query(b.aabb, candidates);
        for (EntityID otherId : candidates) {
            if (otherId <= b.id) continue;
            auto it = m_bodies.find(otherId);
            if (it == m_bodies.end()) continue;
            const CollisionBody& o = *it->second;
            if (!o.enabled) continue;
            if (!b.shouldCollideWith(o)) continue;
            pairs.emplace_back(b.id, otherId);
        }
    }
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
    if (A.type != BodyType::STATIC && B.type != BodyType::STATIC) {
        A.aabb.center += info.normal * (-push);
        B.aabb.center += info.normal * ( push);
    } else if (A.type != BodyType::STATIC) {
        A.aabb.center += info.normal * (-info.penetration);
    } else if (B.type != BodyType::STATIC) {
        B.aabb.center += info.normal * ( info.penetration);
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
    m_hash.update(A.id, A.aabb);
    m_hash.update(B.id, B.aabb);
}

void CollisionManager::update(float dt) {
    (void)dt;
    if (!m_initialized || m_isShutdown) return;
    std::vector<std::pair<EntityID,EntityID>> pairs;
    broadphase(pairs);
    std::vector<CollisionInfo> collisions;
    narrowphase(pairs, collisions);
    for (const auto& c : collisions) {
        resolve(c);
        for (auto& cb : m_callbacks) { cb(c); }
    }
    // Reflect resolved poses back to entities so callers see corrected transforms
    m_isSyncing = true;
    for (auto& kv : m_bodies) {
        auto& b = *kv.second;
        if (auto ent = b.entityWeak.lock()) {
            ent->setPosition(b.aabb.center);
            ent->setVelocity(b.velocity);
        }
    }
    m_isSyncing = false;

    // Trigger-only world events: Player vs Trigger OnEnter
    auto makeKey = [](EntityID a, EntityID b) -> uint64_t {
        uint64_t x = static_cast<uint64_t>(a);
        uint64_t y = static_cast<uint64_t>(b);
        if (x > y) std::swap(x, y);
        // Simple mix (not cryptographic); sufficient for set keys
        return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
    };

    using clock = std::chrono::steady_clock;
    auto now = clock::now();
    std::unordered_set<uint64_t> currentPairs;
    currentPairs.reserve(collisions.size());
    for (const auto &c : collisions) {
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
            }
            it = m_activeTriggerPairs.erase(it);
        } else {
            ++it;
        }
    }
}

void CollisionManager::addCollisionCallback(CollisionCB cb) { m_callbacks.push_back(std::move(cb)); }

void CollisionManager::rebuildStaticFromWorld() {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    std::vector<EntityID> toRemove;
    for (const auto& kv : m_bodies) {
        if (isStatic(*kv.second)) toRemove.push_back(kv.first);
    }
    for (auto id : toRemove) removeBody(id);

    const float tileSize = 32.0f;
    const int h = static_cast<int>(world->grid.size());
    for (int y = 0; y < h; ++y) {
        const int w = static_cast<int>(world->grid[y].size());
        for (int x = 0; x < w; ++x) {
            const auto& tile = world->grid[y][x];
            bool blocked = tile.obstacleType != HammerEngine::ObstacleType::NONE || tile.isWater;
            if (!blocked) continue;
            float cx = x * tileSize + tileSize * 0.5f;
            float cy = y * tileSize + tileSize * 0.5f;
            AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
            EntityID id = (static_cast<EntityID>(1ull) << 62) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
            addBody(id, aabb, BodyType::STATIC);
            setBodyLayer(id, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
        }
    }
}

void CollisionManager::onTileChanged(int x, int y) {
    const auto& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    const float tileSize = 32.0f;
    EntityID id = (static_cast<EntityID>(1ull) << 62) | (static_cast<EntityID>(y) << 31) | static_cast<EntityID>(x);
    removeBody(id);
    if (y >= 0 && y < static_cast<int>(world->grid.size()) && x >= 0 && x < static_cast<int>(world->grid[y].size())) {
        const auto& tile = world->grid[y][x];
        bool blocked = tile.obstacleType != HammerEngine::ObstacleType::NONE || tile.isWater;
        if (blocked) {
            float cx = x * tileSize + tileSize * 0.5f;
            float cy = y * tileSize + tileSize * 0.5f;
            AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
            addBody(id, aabb, BodyType::STATIC);
            setBodyLayer(id, CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
        }
    }
}

void CollisionManager::subscribeWorldEvents() {
    auto& em = EventManager::Instance();
    auto token = em.registerHandlerWithToken(EventTypeId::World, [this](const EventData& data){
        auto base = data.event;
        if (!base) return;
        if (auto loaded = std::dynamic_pointer_cast<WorldLoadedEvent>(base)) {
            (void)loaded;
            float minX, minY, maxX, maxY;
            if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
                const float TILE = 32.0f;
                this->setWorldBounds(minX * TILE, minY * TILE, maxX * TILE, maxY * TILE);
            }
            this->rebuildStaticFromWorld();
            return;
        }
        if (auto generated = std::dynamic_pointer_cast<WorldGeneratedEvent>(base)) {
            (void)generated;
            float minX, minY, maxX, maxY;
            if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
                const float TILE = 32.0f;
                this->setWorldBounds(minX * TILE, minY * TILE, maxX * TILE, maxY * TILE);
            }
            this->rebuildStaticFromWorld();
            return;
        }
        if (auto unloaded = std::dynamic_pointer_cast<WorldUnloadedEvent>(base)) {
            (void)unloaded;
            std::vector<EntityID> toRemove;
            for (const auto& kv : m_bodies) if (isStatic(*kv.second)) toRemove.push_back(kv.first);
            for (auto id : toRemove) removeBody(id);
            return;
        }
        if (auto tileChanged = std::dynamic_pointer_cast<TileChangedEvent>(base)) {
            this->onTileChanged(tileChanged->getX(), tileChanged->getY());
            return;
        }
    });
    m_handlerTokens.push_back(token);
}
