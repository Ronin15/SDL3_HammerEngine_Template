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
    m_hash.clear();
    m_callbacks.clear();
    m_initialized = false;
    COLLISION_INFO("Cleaned and shut down");
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
    m_hash.update(id, it->second->aabb);
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

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->isTrigger = isTrigger;
    COLLISION_DEBUG("setBodyTrigger id=" + std::to_string(id) + " -> " + (isTrigger ? std::string("true") : std::string("false")));
}

void CollisionManager::setBodyTriggerTag(EntityID id, HammerEngine::TriggerTag tag) {
    auto it = m_bodies.find(id);
    if (it != m_bodies.end()) it->second->triggerTag = tag;
    COLLISION_DEBUG("setBodyTriggerTag id=" + std::to_string(id) + ", tag=" + std::to_string(static_cast<int>(tag)));
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

void CollisionManager::resizeBody(EntityID id, float halfWidth, float halfHeight) {
    auto it = m_bodies.find(id);
    if (it == m_bodies.end()) return;
    auto &body = *it->second;
    body.aabb.halfSize = Vector2D(halfWidth, halfHeight);
    m_hash.update(id, body.aabb);
    COLLISION_DEBUG("resizeBody id=" + std::to_string(id) +
                    ", halfW=" + std::to_string(halfWidth) + ", halfH=" + std::to_string(halfHeight));
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
    auto keyOf = [](EntityID a, EntityID b){
        uint64_t x = static_cast<uint64_t>(a);
        uint64_t y = static_cast<uint64_t>(b);
        if (x > y) std::swap(x, y);
        return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
    };
    std::unordered_set<uint64_t> seen;
    seen.reserve(m_bodies.size());
    for (const auto& kv : m_bodies) {
        const CollisionBody& b = *kv.second;
        if (!b.enabled) continue;
        if (b.type == BodyType::STATIC) continue; // only moving bodies generate queries
        candidates.clear();
        m_hash.query(b.aabb, candidates);
        for (EntityID otherId : candidates) {
            if (otherId == b.id) continue;
            auto it = m_bodies.find(otherId);
            if (it == m_bodies.end()) continue;
            const CollisionBody& o = *it->second;
            if (!o.enabled) continue;
            if (!b.shouldCollideWith(o)) continue;
            // Ensure one canonical ordering to avoid duplicates
            EntityID aId = b.id;
            EntityID bId = otherId;
            if (aId > bId) std::swap(aId, bId);
            uint64_t k = keyOf(aId, bId);
            if (seen.insert(k).second) {
                pairs.emplace_back(aId, bId);
            }
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
    m_hash.update(A.id, A.aabb);
    m_hash.update(B.id, B.aabb);
}

void CollisionManager::update(float dt) {
    (void)dt;
    if (!m_initialized || m_isShutdown) return;
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    std::vector<std::pair<EntityID,EntityID>> pairs;
    broadphase(pairs);
    auto t1 = clock::now();

    std::vector<CollisionInfo> collisions;
    narrowphase(pairs, collisions);
    auto t2 = clock::now();

    for (const auto& c : collisions) {
        resolve(c);
        for (auto& cb : m_callbacks) { cb(c); }
    }
    auto t3 = clock::now();
    if (m_verboseLogs && !collisions.empty()) {
        COLLISION_DEBUG("Resolved collisions: count=" + std::to_string(collisions.size()));
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
    m_perf.lastPairs = pairs.size();
    m_perf.lastCollisions = collisions.size();
    m_perf.bodyCount = m_bodies.size();
    m_perf.avgTotalMs = (m_perf.avgTotalMs * m_perf.frames + m_perf.lastTotalMs) / (m_perf.frames + 1);
    m_perf.frames += 1;
    if (m_perf.lastTotalMs > 5.0) {
        COLLISION_WARN("Slow frame: totalMs=" + std::to_string(m_perf.lastTotalMs) +
                       ", pairs=" + std::to_string(m_perf.lastPairs) +
                       ", collisions=" + std::to_string(m_perf.lastCollisions));
    }
}

void CollisionManager::addCollisionCallback(CollisionCB cb) { m_callbacks.push_back(std::move(cb)); }

void CollisionManager::rebuildStaticFromWorld() {
    const WorldManager& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    // Remove any existing STATIC world bodies (we do not process world tile collisions unless trigger)
    std::vector<EntityID> toRemove;
    for (const auto& kv : m_bodies) {
        if (isStatic(*kv.second)) toRemove.push_back(kv.first);
    }
    for (auto id : toRemove) removeBody(id);

    // Only create trigger volumes (e.g., water) — no solid tiles
    size_t created = createTriggersForWaterTiles(HammerEngine::TriggerTag::Water);
    if (created > 0) {
        COLLISION_INFO("World triggers built: water count=" + std::to_string(created));
    }
}

void CollisionManager::onTileChanged(int x, int y) {
    const auto& wm = WorldManager::Instance();
    const auto* world = wm.getWorldData();
    if (!world) return;
    const float tileSize = 32.0f;
    // Update only trigger for this tile; no solid world body
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
            COLLISION_INFO("World loaded - rebuilding static colliders");
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
            return;
        }
        if (auto tileChanged = std::dynamic_pointer_cast<TileChangedEvent>(base)) {
            this->onTileChanged(tileChanged->getX(), tileChanged->getY());
            return;
        }
    });
    m_handlerTokens.push_back(token);
}
