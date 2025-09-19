/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/CollisionManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "events/WorldEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/UniqueID.hpp"
#include "world/WorldData.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <unordered_map>
#include <unordered_set>

using ::EventManager;
using ::EventTypeId;
using ::TileChangedEvent;
using ::WorldGeneratedEvent;
using ::WorldLoadedEvent;
using ::WorldManager;
using ::WorldUnloadedEvent;
using ::HammerEngine::ObstacleType;

bool CollisionManager::init() {
  if (m_initialized)
    return true;
  m_bodies.clear();
  m_staticHash.clear();
  m_dynamicHash.clear();
  subscribeWorldEvents();
  COLLISION_INFO("Initialized: cleared bodies and spatial hash");
  // Forward collision notifications to EventManager
  addCollisionCallback([](const HammerEngine::CollisionInfo &info) {
    EventManager::Instance().triggerCollision(
        info, EventManager::DispatchMode::Deferred);
  });
  m_initialized = true;
  m_isShutdown = false;
  m_lastWasThreaded.store(false, std::memory_order_relaxed);
  m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
  m_lastOptimalWorkerCount.store(0, std::memory_order_relaxed);
  m_lastAvailableWorkers.store(0, std::memory_order_relaxed);
  m_lastCollisionBudget.store(0, std::memory_order_relaxed);
  return true;
}

void CollisionManager::clean() {
  if (!m_initialized || m_isShutdown)
    return;
  m_isShutdown = true;

  // Clean legacy storage
  m_bodies.clear();

  // Clean new SOA storage
  m_storage.clear();

  m_staticHash.clear();
  m_dynamicHash.clear();
  m_callbacks.clear();
  m_initialized = false;
  COLLISION_INFO("Cleaned and shut down (both legacy and SOA storage)");
}

void CollisionManager::prepareForStateTransition() {
  COLLISION_INFO("Preparing CollisionManager for state transition...");

  if (!m_initialized || m_isShutdown) {
    COLLISION_WARN("CollisionManager not initialized or already shutdown "
                   "during state transition");
    return;
  }

  // Clear all collision bodies (both dynamic and static)
  size_t legacyBodyCount = m_bodies.size();
  size_t soaBodyCount = m_storage.size();
  m_bodies.clear();
  m_storage.clear();

  // Clear spatial hash completely
  m_staticHash.clear();
  m_dynamicHash.clear();

  // Clear caches to prevent dangling references to deleted bodies
  m_broadphaseCache.invalidateStaticCache(); // Clear static cache
  m_broadphaseCache.resetFrame();            // Clear frame cache
  m_collisionPool.resetFrame();              // Clear collision buffers

  // Clear trigger tracking state completely
  m_activeTriggerPairs.clear();
  m_triggerCooldownUntil.clear();

  // Reset trigger cooldown settings
  m_defaultTriggerCooldownSec = 0.0f;

  // Clear all collision callbacks (these should be re-registered by new states)
  m_callbacks.clear();

  // Reset performance stats for clean slate
  m_perf = PerfStats{};
  m_lastWasThreaded.store(false, std::memory_order_relaxed);
  m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
  m_lastOptimalWorkerCount.store(0, std::memory_order_relaxed);
  m_lastAvailableWorkers.store(0, std::memory_order_relaxed);
  m_lastCollisionBudget.store(0, std::memory_order_relaxed);

  // Reset world bounds to default
  m_worldBounds = AABB(0, 0, 100000.0f, 100000.0f);

  // Reset syncing state
  m_isSyncing = false;

  // Reset verbose logging to default
  m_verboseLogs = false;

  COLLISION_INFO("CollisionManager state transition complete - removed " +
                 std::to_string(legacyBodyCount) + " legacy bodies and " +
                 std::to_string(soaBodyCount) + " SOA bodies, cleared all state");
}

void CollisionManager::setWorldBounds(float minX, float minY, float maxX,
                                      float maxY) {
  float cx = (minX + maxX) * 0.5f;
  float cy = (minY + maxY) * 0.5f;
  float hw = (maxX - minX) * 0.5f;
  float hh = (maxY - minY) * 0.5f;
  m_worldBounds = AABB(cx, cy, hw, hh);
  COLLISION_DEBUG("World bounds set: [" + std::to_string(minX) + "," +
                  std::to_string(minY) + "] - [" + std::to_string(maxX) + "," +
                  std::to_string(maxY) + "]");
}

void CollisionManager::addBody(EntityID id, const AABB &aabb, BodyType type,
                                uint32_t layer, uint32_t collidesWith) {
  auto body = std::make_shared<CollisionBody>();
  body->id = id;
  body->aabb = aabb;
  body->type = type;
  body->layer = layer;
  body->collidesWith = collidesWith;
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
        EventManager::DispatchMode::Immediate);
  }

  if (type == BodyType::KINEMATIC) {
    COLLISION_DEBUG("Added KINEMATIC body - ID: " + std::to_string(id) +
                    ", Total bodies: " + std::to_string(m_bodies.size()) +
                    ", Kinematic count should now be: " +
                    std::to_string(getKinematicBodyCount()));
  }
}

void CollisionManager::addBody(EntityPtr entity, const AABB &aabb,
                               BodyType type) {
  if (!entity)
    return;
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
      bodyRadius = std::max(it->second->aabb.halfSize.getX(),
                            it->second->aabb.halfSize.getY()) +
                   32.0f;
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
        bodyCenter, bodyRadius,
        "Static obstacle removed (ID: " + std::to_string(id) + ")",
        EventManager::DispatchMode::Immediate);
  }

  if (m_verboseLogs) {
    COLLISION_DEBUG("removeBody id=" + std::to_string(id));
  }
}

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
  auto it = m_bodies.find(id);
  if (it != m_bodies.end())
    it->second->enabled = enabled;
  if (m_verboseLogs) {
    COLLISION_DEBUG("setBodyEnabled id=" + std::to_string(id) + " -> " +
                    (enabled ? std::string("true") : std::string("false")));
  }
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask,
                                    uint32_t collideMask) {
  auto it = m_bodies.find(id);
  if (it != m_bodies.end()) {
    it->second->layer = layerMask;
    it->second->collidesWith = collideMask;
  }
  if (m_verboseLogs) {
    COLLISION_DEBUG("setBodyLayer id=" + std::to_string(id) +
                    ", layer=" + std::to_string(layerMask) +
                    ", mask=" + std::to_string(collideMask));
  }
}

void CollisionManager::setKinematicPose(EntityID id, const Vector2D &center) {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return;
  it->second->aabb.center = center;

  // Update the appropriate spatial hash based on body type
  if (it->second->type == BodyType::STATIC) {
    m_staticHash.update(id, it->second->aabb);
  } else {
    m_dynamicHash.update(id, it->second->aabb);
  }
  if (m_verboseLogs) {
    COLLISION_DEBUG("setKinematicPose id=" + std::to_string(id) + ", center=(" +
                    std::to_string(center.getX()) + "," +
                    std::to_string(center.getY()) + ")");
  }
}

void CollisionManager::setVelocity(EntityID id, const Vector2D &v) {
  auto it = m_bodies.find(id);
  if (it != m_bodies.end())
    it->second->velocity = v;
  if (m_verboseLogs) {
    COLLISION_DEBUG("setVelocity id=" + std::to_string(id) + ", v=(" +
                    std::to_string(v.getX()) + "," + std::to_string(v.getY()) +
                    ")");
  }
}

void CollisionManager::updateKinematicBatch(
    const std::vector<KinematicUpdate> &updates) {
  if (updates.empty())
    return;

  // PERFORMANCE OPTIMIZATION: Batch process all kinematic updates
  // This reduces hash table lookups and spatial hash updates from O(n) to O(1)
  // operations
  //
  // CRITICAL FIX: Update both AABB and spatial hash atomically to prevent
  // race conditions between broadphase (spatial hash) and narrowphase (AABB)

  size_t validUpdates = 0;
  for (const auto &kinematicUpdate : updates) {
    auto it = m_bodies.find(kinematicUpdate.id);
    if (it != m_bodies.end() && it->second->type == BodyType::KINEMATIC) {
      // ATOMIC UPDATE: Update position, velocity, and spatial hash together
      // This prevents race condition where broadphase sees old position
      // but narrowphase sees new position (or vice versa)

      // Update position and velocity in collision body
      it->second->aabb.center = kinematicUpdate.position;
      it->second->velocity = kinematicUpdate.velocity;

      // Ensure body stays enabled - prevent corruption
      if (!it->second->enabled) {
        it->second->enabled = true;
      }

      // Immediately update spatial hash with new AABB - CRITICAL for consistency
      // Note: updateKinematicBatch only updates KINEMATIC bodies, which are always in dynamic hash
      m_dynamicHash.update(kinematicUpdate.id, it->second->aabb);

      validUpdates++;
    }
  }

  // Debug logging (only when verbose and with summary to reduce spam)
  if (m_verboseLogs && !updates.empty()) {
    COLLISION_DEBUG("updateKinematicBatch: processed " +
                    std::to_string(validUpdates) + "/" +
                    std::to_string(updates.size()) + " kinematic updates");
  }
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
  auto it = m_bodies.find(id);
  if (it != m_bodies.end())
    it->second->isTrigger = isTrigger;
  // Reduced debug spam - only log for non-world triggers
  if (id <
      (1ull << 61)) { // Only log for non-world objects (player, NPCs, etc.)
    COLLISION_DEBUG("setBodyTrigger id=" + std::to_string(id) + " -> " +
                    (isTrigger ? std::string("true") : std::string("false")));
  }
}

void CollisionManager::setBodyTriggerTag(EntityID id,
                                         HammerEngine::TriggerTag tag) {
  auto it = m_bodies.find(id);
  if (it != m_bodies.end())
    it->second->triggerTag = tag;
  // Removed debug spam - too many trigger tags created during world build
}

EntityID CollisionManager::createTriggerArea(const AABB &aabb,
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

EntityID CollisionManager::createTriggerAreaAt(float cx, float cy, float halfW,
                                               float halfH,
                                               HammerEngine::TriggerTag tag,
                                               uint32_t layerMask,
                                               uint32_t collideMask) {
  return createTriggerArea(AABB(cx, cy, halfW, halfH), tag, layerMask,
                           collideMask);
}

void CollisionManager::setTriggerCooldown(EntityID triggerId, float seconds) {
  using clock = std::chrono::steady_clock;
  auto now = clock::now();
  m_triggerCooldownUntil[triggerId] =
      now + std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(seconds));
}

size_t
CollisionManager::createTriggersForWaterTiles(HammerEngine::TriggerTag tag) {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return 0;
  size_t created = 0;
  const float tileSize = 32.0f;
  const int h = static_cast<int>(world->grid.size());
  for (int y = 0; y < h; ++y) {
    const int w = static_cast<int>(world->grid[y].size());
    for (int x = 0; x < w; ++x) {
      const auto &tile = world->grid[y][x];
      if (!tile.isWater)
        continue;
      float cx = x * tileSize + tileSize * 0.5f;
      float cy = y * tileSize + tileSize * 0.5f;
      AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
      // Use a distinct prefix for triggers to avoid id collisions with static
      // colliders
      EntityID id = (static_cast<EntityID>(1ull) << 61) |
                    (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
                    static_cast<EntityID>(static_cast<uint32_t>(x));
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
  // This avoids creating thousands of trigger bodies that cause performance
  // issues
  return 0;
}

size_t CollisionManager::createStaticObstacleBodies() {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return 0;

  size_t created = 0;
  const float tileSize = 32.0f;
  const int h = static_cast<int>(world->grid.size());

  for (int y = 0; y < h; ++y) {
    const int w = static_cast<int>(world->grid[y].size());
    for (int x = 0; x < w; ++x) {
      const auto &tile = world->grid[y][x];

      // Create solid collision bodies for BUILDING obstacles only (64x64 per
      // building) ROCK, TREE, WATER are handled as triggers with movement
      // penalties
      if (tile.obstacleType == ObstacleType::BUILDING) {
        // Only create collision body from the top-left tile of each building
        bool isTopLeft = true;
        if (x > 0 && world->grid[y][x - 1].buildingId == tile.buildingId)
          isTopLeft = false;
        if (y > 0 && world->grid[y - 1][x].buildingId == tile.buildingId)
          isTopLeft = false;

        if (isTopLeft) {
          // Create 64x64 collision body for the entire building (2x2 tiles)
          float cx =
              x * tileSize + tileSize; // Center at 1 tile offset (64px / 2)
          float cy =
              y * tileSize + tileSize; // Center at 1 tile offset (64px / 2)
          AABB aabb(
              cx, cy, tileSize,
              tileSize); // 64x64 collision box (tileSize * 2 / 2 = tileSize)

          // Use building ID for collision body to ensure uniqueness per
          // building
          EntityID id = (static_cast<EntityID>(3ull) << 61) |
                        static_cast<EntityID>(tile.buildingId);

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
  }

  if (created > 0) {
    COLLISION_INFO("Created obstacle bodies: count=" + std::to_string(created));
  }
  return created;
}

void CollisionManager::resizeBody(EntityID id, float halfWidth,
                                  float halfHeight) {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return;
  auto &body = *it->second;
  body.aabb.halfSize = Vector2D(halfWidth, halfHeight);

  // Update the appropriate spatial hash based on body type
  if (body.type == BodyType::STATIC) {
    m_staticHash.update(id, body.aabb);
  } else {
    m_dynamicHash.update(id, body.aabb);
  }
  COLLISION_DEBUG("resizeBody id=" + std::to_string(id) +
                  ", halfW=" + std::to_string(halfWidth) +
                  ", halfH=" + std::to_string(halfHeight));
}

bool CollisionManager::overlaps(EntityID a, EntityID b) const {
  auto ita = m_bodies.find(a);
  auto itb = m_bodies.find(b);
  if (ita == m_bodies.end() || itb == m_bodies.end())
    return false;
  return ita->second->aabb.intersects(itb->second->aabb);
}

void CollisionManager::queryArea(const AABB &area,
                                 std::vector<EntityID> &out) const {
  // Query both static and dynamic hashes, combining results
  std::vector<EntityID> staticResults;
  std::vector<EntityID> dynamicResults;

  m_staticHash.query(area, staticResults);
  m_dynamicHash.query(area, dynamicResults);

  // Combine results efficiently
  out.clear();
  out.reserve(staticResults.size() + dynamicResults.size());
  out.insert(out.end(), staticResults.begin(), staticResults.end());
  out.insert(out.end(), dynamicResults.begin(), dynamicResults.end());
}

bool CollisionManager::getBodyCenter(EntityID id, Vector2D &outCenter) const {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return false;
  outCenter = it->second->aabb.center;
  return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return false;
  return it->second->type == BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return false;
  return it->second->type == BodyType::KINEMATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
  auto it = m_bodies.find(id);
  if (it == m_bodies.end())
    return false;
  return it->second->isTrigger;
}

void CollisionManager::broadphase(
    std::vector<std::pair<EntityID, EntityID>> &pairs) const {
  pairs.clear();
  pairs.reserve(m_bodies.size() * 2);

  // OPTIMIZATION: Use member containers to avoid repeated allocations
  // Access broadphase cache containers that are properly reset each frame
  auto &dynamicCandidates = m_collisionPool.dynamicCandidates;
  auto &staticCandidates = m_collisionPool.staticCandidates;
  auto &seenPairs = m_broadphaseCache.seenPairs;

  // Containers are already cleared by resetFrame() calls
  // Reserve space based on expected sizes
  const size_t bodyCount = m_bodies.size();
  seenPairs.reserve(bodyCount * 2);

  // CRITICAL OPTIMIZATION: Process only dynamic/kinematic bodies (static don't
  // initiate collisions)
  for (const auto &kv : m_bodies) {
    const CollisionBody &body = *kv.second;
    if (!body.enabled || body.type == BodyType::STATIC)
      continue;

    // PERFORMANCE BOOST: Separate queries for dynamic vs static bodies
    // This prevents dynamic bodies from being checked against every static tile

    // 1. Query dynamic hash for dynamic-vs-dynamic collisions
    dynamicCandidates.clear();
    m_dynamicHash.query(body.aabb, dynamicCandidates);

    // Process dynamic candidates
    for (EntityID candidateId : dynamicCandidates) {
      if (candidateId == body.id)
        continue;

      auto candidateIt = m_bodies.find(candidateId);
      if (candidateIt == m_bodies.end())
        continue;

      const CollisionBody &candidate = *candidateIt->second;
      if (!candidate.enabled)
        continue;

      // Quick collision mask check
      if ((body.collidesWith & candidate.layer) == 0)
        continue;

      // Create canonical pair key (smaller ID first)
      uint64_t pairKey;
      if (body.id < candidateId) {
        pairKey = (static_cast<uint64_t>(body.id) << 32) | candidateId;
      } else {
        pairKey = (static_cast<uint64_t>(candidateId) << 32) | body.id;
      }

      // Add unique pair
      if (seenPairs.emplace(pairKey).second) {
        pairs.emplace_back(std::min(body.id, candidateId),
                           std::max(body.id, candidateId));

      }
    }

    // 2. Query static hash for dynamic-vs-static collisions (with caching)
    staticCandidates.clear();

    // Check if we have cached static bodies for this entity
    auto cacheIt = m_broadphaseCache.staticCache.find(body.id);
    bool useCache = (cacheIt != m_broadphaseCache.staticCache.end());

    if (useCache) {
      // Use cached static bodies if entity hasn't moved significantly
      auto &cachedQuery = cacheIt->second;
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
      auto &cachedQuery = m_broadphaseCache.staticCache[body.id];
      cachedQuery.staticBodies = staticCandidates;
      cachedQuery.lastQueryCenter = body.aabb.center;
      cachedQuery.maxQueryDistance =
          std::max(body.aabb.halfSize.getX(), body.aabb.halfSize.getY()) * 2.0f;
    }

    // Process static candidates (dynamic body vs static body only)
    for (EntityID staticId : staticCandidates) {
      auto staticIt = m_bodies.find(staticId);
      if (staticIt == m_bodies.end())
        continue;

      const CollisionBody &staticBody = *staticIt->second;
      if (!staticBody.enabled)
        continue;

      // Quick collision mask check
      if ((body.collidesWith & staticBody.layer) == 0)
        continue;

      // No need for pair deduplication with static bodies since we don't
      // reverse check
      pairs.emplace_back(body.id, staticId);

    }
  }

  // PERFORMANCE OPTIMIZATIONS IMPLEMENTED:
  // 1. Separate static and dynamic spatial hash queries
  // 2. Prevents N×M collision checks between dynamic and static bodies
  // 3. Static bodies never initiate collision checks (only receive them)
  // 4. Dynamic-vs-dynamic pairs are deduplicated, dynamic-vs-static are not (no
  // reverse check)
  // 5. Persistent cache for static body queries with movement-based
  // invalidation
  // 6. Member containers with proper per-frame reset (no thread_local memory
  // leak)
  // 7. Expected 20x performance improvement for world with many static tiles
}

bool CollisionManager::broadphaseThreaded(
    std::vector<std::pair<EntityID, EntityID>> &pairs,
    ThreadingStats &stats) {
  pairs.clear();

  if (!HammerEngine::ThreadSystem::Exists()) {
    broadphase(pairs);
    return false;
  }

  std::vector<EntityID> dynamicIds;
  dynamicIds.reserve(m_bodies.size());
  for (const auto &kv : m_bodies) {
    const CollisionBody &body = *kv.second;
    if (body.enabled && body.type != BodyType::STATIC) {
      dynamicIds.push_back(body.id);
    }
  }

  if (dynamicIds.empty()) {
    return false;
  }

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  if (queueCapacity == 0) {
    queueCapacity = 1;
  }

  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t collisionBudget = std::max<size_t>(1, budget.remaining);

  size_t threadingThresholdValue =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  size_t optimalWorkers = budget.getOptimalWorkerCount(
      collisionBudget, dynamicIds.size(), threadingThresholdValue);
  optimalWorkers = std::max<size_t>(1, optimalWorkers);
  if (m_maxThreads > 0) {
    optimalWorkers =
        std::min(optimalWorkers, static_cast<size_t>(m_maxThreads));
  }

  double queuePressure = static_cast<double>(queueSize) / queueCapacity;
  size_t baseBatchSize =
      std::max(threadingThresholdValue / 2, static_cast<size_t>(64));
  size_t minBodiesPerBatch = baseBatchSize;
  size_t maxBatches = std::max<size_t>(2, optimalWorkers);

  if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
    minBodiesPerBatch =
        std::min(baseBatchSize * 2, threadingThresholdValue * 2);
    size_t highPressureMax = std::max<size_t>(2, optimalWorkers / 2);
    maxBatches = std::max<size_t>(1, highPressureMax);
  } else if (queuePressure < (1.0 - HammerEngine::QUEUE_PRESSURE_WARNING)) {
    size_t minLowPressure =
        std::max(threadingThresholdValue / 4, static_cast<size_t>(32));
    minBodiesPerBatch =
        std::max({baseBatchSize / 2, minLowPressure, static_cast<size_t>(32)});
    maxBatches = std::max<size_t>(4, optimalWorkers);
  }

  size_t desiredBatchCount =
      (dynamicIds.size() + minBodiesPerBatch - 1) / minBodiesPerBatch;
  size_t batchCount = std::min(optimalWorkers, desiredBatchCount);
  batchCount = std::min(batchCount, maxBatches);
  batchCount = std::max<size_t>(1, batchCount);

  if (batchCount <= 1) {
    broadphase(pairs);
    return false;
  }

  size_t bodiesPerBatch = dynamicIds.size() / batchCount;
  size_t remainingBodies = dynamicIds.size() % batchCount;

  std::vector<std::future<std::vector<std::pair<EntityID, EntityID>>>> futures;
  futures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t start = i * bodiesPerBatch;
    size_t end = start + bodiesPerBatch;
    if (i == batchCount - 1) {
      end += remainingBodies;
    }

    futures.push_back(threadSystem.enqueueTaskWithResult(
        [this, start, end, &dynamicIds]() {
          std::vector<std::pair<EntityID, EntityID>> localPairs;
          localPairs.reserve((end > start ? end - start : 0) * 4);
          std::unordered_set<uint64_t> localSeen;
          localSeen.reserve((end > start ? end - start : 0) * 4);

          std::vector<EntityID> dynamicCandidates;
          dynamicCandidates.reserve(32);
          std::vector<EntityID> staticCandidates;
          staticCandidates.reserve(64);

          for (size_t idx = start; idx < end; ++idx) {
            EntityID bodyId = dynamicIds[idx];
            auto bodyIt = m_bodies.find(bodyId);
            if (bodyIt == m_bodies.end()) {
              continue;
            }
            const CollisionBody &body = *bodyIt->second;
            if (!body.enabled) {
              continue;
            }

            dynamicCandidates.clear();
            m_dynamicHash.query(body.aabb, dynamicCandidates);
            for (EntityID candidateId : dynamicCandidates) {
              if (candidateId == body.id) {
                continue;
              }
              auto candidateIt = m_bodies.find(candidateId);
              if (candidateIt == m_bodies.end()) {
                continue;
              }
              const CollisionBody &candidate = *candidateIt->second;
              if (!candidate.enabled) {
                continue;
              }
              if ((body.collidesWith & candidate.layer) == 0) {
                continue;
              }

              EntityID a = std::min(body.id, candidateId);
              EntityID b = std::max(body.id, candidateId);
              uint64_t key = (static_cast<uint64_t>(a) << 32) |
                             static_cast<uint64_t>(b);
              if (localSeen.emplace(key).second) {
                localPairs.emplace_back(a, b);
              }
            }

            staticCandidates.clear();
            m_staticHash.query(body.aabb, staticCandidates);
            for (EntityID staticId : staticCandidates) {
              auto staticIt = m_bodies.find(staticId);
              if (staticIt == m_bodies.end()) {
                continue;
              }
              const CollisionBody &staticBody = *staticIt->second;
              if (!staticBody.enabled) {
                continue;
              }
              if ((body.collidesWith & staticBody.layer) == 0) {
                continue;
              }
              localPairs.emplace_back(body.id, staticId);
            }
          }

          return localPairs;
        },
        HammerEngine::TaskPriority::High, "Collision_BroadphaseBatch"));
  }

  std::unordered_set<uint64_t> globalSeen;
  globalSeen.reserve(dynamicIds.size() * 4);
  for (auto &future : futures) {
    auto localPairs = future.get();
    for (auto &pair : localPairs) {
      EntityID a = pair.first;
      EntityID b = pair.second;
      uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) |
                     static_cast<uint64_t>(std::max(a, b));
      if (globalSeen.emplace(key).second) {
        pairs.emplace_back(a, b);
      }
    }
  }

  stats.optimalWorkers = optimalWorkers;
  stats.availableWorkers = availableWorkers;
  stats.budget = collisionBudget;
  stats.batchCount = batchCount;

  return true;
}

void CollisionManager::narrowphase(
    const std::vector<std::pair<EntityID, EntityID>> &pairs,
    std::vector<CollisionInfo> &collisions) const {
  collisions.clear();
  for (auto [aId, bId] : pairs) {
    const auto ita = m_bodies.find(aId);
    const auto itb = m_bodies.find(bId);
    if (ita == m_bodies.end() || itb == m_bodies.end())
      continue;
    const CollisionBody &A = *ita->second;
    const CollisionBody &B = *itb->second;

    if (!A.aabb.intersects(B.aabb))
      continue;
    float dxLeft = B.aabb.right() - A.aabb.left();
    float dxRight = A.aabb.right() - B.aabb.left();
    float dyTop = B.aabb.bottom() - A.aabb.top();
    float dyBottom = A.aabb.bottom() - B.aabb.top();
    float minPen = dxLeft;
    Vector2D normal(-1, 0);
    if (dxRight < minPen) {
      minPen = dxRight;
      normal = Vector2D(1, 0);
    }
    if (dyTop < minPen) {
      minPen = dyTop;
      normal = Vector2D(0, -1);
    }
    if (dyBottom < minPen) {
      minPen = dyBottom;
      normal = Vector2D(0, 1);
    }
    collisions.push_back(
        CollisionInfo{aId, bId, normal, minPen, (A.isTrigger || B.isTrigger)});
  }
}

bool CollisionManager::narrowphaseThreaded(
    const std::vector<std::pair<EntityID, EntityID>> &pairs,
    std::vector<CollisionInfo> &collisions, ThreadingStats &stats) {
  collisions.clear();

  if (pairs.empty()) {
    return false;
  }

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  if (queueCapacity == 0) {
    queueCapacity = 1;
  }

  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t collisionBudget = std::max<size_t>(1, budget.remaining);

  size_t threadingThreshold =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  size_t optimalWorkers = budget.getOptimalWorkerCount(
      collisionBudget, pairs.size(), threadingThreshold);

  optimalWorkers = std::max<size_t>(1, optimalWorkers);
  if (m_maxThreads > 0) {
    optimalWorkers = std::min(optimalWorkers, static_cast<size_t>(m_maxThreads));
  }

  double queuePressure = static_cast<double>(queueSize) / queueCapacity;
  size_t baseBatchSize =
      std::max(threadingThreshold / 2, static_cast<size_t>(64));
  size_t minPairsPerBatch = baseBatchSize;
  size_t maxBatches = std::max<size_t>(2, optimalWorkers);

  if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
    minPairsPerBatch = std::min(baseBatchSize * 2, threadingThreshold * 2);
    size_t highPressureMax = std::max<size_t>(2, optimalWorkers / 2);
    maxBatches = std::max<size_t>(1, highPressureMax);
  } else if (queuePressure < (1.0 - HammerEngine::QUEUE_PRESSURE_WARNING)) {
    size_t minLowPressure =
        std::max(threadingThreshold / 4, static_cast<size_t>(32));
    minPairsPerBatch =
        std::max({baseBatchSize / 2, minLowPressure, static_cast<size_t>(32)});
    maxBatches = std::max<size_t>(4, optimalWorkers);
  }

  size_t desiredBatchCount =
      (pairs.size() + minPairsPerBatch - 1) / minPairsPerBatch;
  size_t batchCount = std::min(optimalWorkers, desiredBatchCount);
  batchCount = std::min(batchCount, maxBatches);
  batchCount = std::max<size_t>(1, batchCount);

  if (batchCount == 1) {
    return false;
  }

  size_t pairsPerBatch = pairs.size() / batchCount;
  size_t remainingPairs = pairs.size() % batchCount;

  std::vector<std::future<std::vector<CollisionInfo>>> futures;
  futures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t start = i * pairsPerBatch;
    size_t end = start + pairsPerBatch;
    if (i == batchCount - 1) {
      end += remainingPairs;
    }

    futures.push_back(threadSystem.enqueueTaskWithResult(
        [this, &pairs, start, end]() {
          std::vector<CollisionInfo> localCollisions;
          localCollisions.reserve(end > start ? end - start : 0);
          for (size_t idx = start; idx < end; ++idx) {
            auto [aId, bId] = pairs[idx];
            const auto ita = m_bodies.find(aId);
            const auto itb = m_bodies.find(bId);
            if (ita == m_bodies.end() || itb == m_bodies.end())
              continue;
            const CollisionBody &A = *ita->second;
            const CollisionBody &B = *itb->second;
            if (!A.aabb.intersects(B.aabb))
              continue;

            float dxLeft = B.aabb.right() - A.aabb.left();
            float dxRight = A.aabb.right() - B.aabb.left();
            float dyTop = B.aabb.bottom() - A.aabb.top();
            float dyBottom = A.aabb.bottom() - B.aabb.top();
            float minPen = dxLeft;
            Vector2D normal(-1, 0);
            if (dxRight < minPen) {
              minPen = dxRight;
              normal = Vector2D(1, 0);
            }
            if (dyTop < minPen) {
              minPen = dyTop;
              normal = Vector2D(0, -1);
            }
            if (dyBottom < minPen) {
              minPen = dyBottom;
              normal = Vector2D(0, 1);
            }

            localCollisions.push_back(CollisionInfo{
                aId, bId, normal, minPen, (A.isTrigger || B.isTrigger)});
          }
          return localCollisions;
        },
        HammerEngine::TaskPriority::High, "Collision_NarrowphaseBatch"));
  }

  for (auto &future : futures) {
    auto localCollisions = future.get();
    collisions.insert(collisions.end(), localCollisions.begin(),
                      localCollisions.end());
  }

  stats.optimalWorkers = optimalWorkers;
  stats.availableWorkers = availableWorkers;
  stats.budget = collisionBudget;
  stats.batchCount = batchCount;

  return true;
}

void CollisionManager::resolve(const CollisionInfo &info) {
  if (info.trigger)
    return;
  auto ita = m_bodies.find(info.a);
  auto itb = m_bodies.find(info.b);
  if (ita == m_bodies.end() || itb == m_bodies.end())
    return;
  CollisionBody &A = *ita->second;
  CollisionBody &B = *itb->second;
  const float push = info.penetration * 0.5f;

  // Apply position corrections (including to kinematic bodies)
  if (A.type != BodyType::STATIC && B.type != BodyType::STATIC) {
    A.aabb.center += info.normal * (-push);
    B.aabb.center += info.normal * (push);
  } else if (A.type != BodyType::STATIC) {
    A.aabb.center += info.normal * (-info.penetration);
  } else if (B.type != BodyType::STATIC) {
    B.aabb.center += info.normal * (info.penetration);
  }

  auto dampen = [&](CollisionBody &body) {
    float nx = info.normal.getX();
    float ny = info.normal.getY();
    float vdotn = body.velocity.getX() * nx + body.velocity.getY() * ny;
    if (vdotn > 0)
      return;
    Vector2D vn(nx * vdotn, ny * vdotn);
    body.velocity -= vn * (1.0f + body.restitution);
  };
  dampen(A);
  dampen(B);
  // Small tangential slide to reduce clumping for NPC-vs-NPC only (skip Player)
  auto isPlayer = [](const CollisionBody &b) {
    return (b.layer & CollisionLayer::Layer_Player) != 0;
  };
  if (A.type == BodyType::DYNAMIC && B.type == BodyType::DYNAMIC &&
      !isPlayer(A) && !isPlayer(B)) {
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
  auto clampSpeed = [](CollisionBody &body) {
    const float maxSpeed = 300.0f;
    float lx = body.velocity.length();
    if (lx > maxSpeed && lx > 0.0f) {
      Vector2D dir = body.velocity;
      dir.normalize();
      body.velocity = dir * maxSpeed;
    }
  };
  clampSpeed(A);
  clampSpeed(B);

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
  if (!m_initialized || m_isShutdown)
    return;

  // NEW SOA UPDATE PATH: Use SOA storage if available, fallback to legacy
  bool useSOAPath = !m_storage.empty();
  size_t totalBodies = useSOAPath ? m_storage.size() : m_bodies.size();

  if (useSOAPath) {
    updateSOA(dt);
    return;
  }

  // LEGACY UPDATE PATH: Will be removed after migration
  // Initialize object pools and broadphase cache for this frame
  m_collisionPool.ensureCapacity(m_bodies.size());
  m_collisionPool.resetFrame();
  m_broadphaseCache
      .resetFrame(); // Clear per-frame tracking (keeps static cache)

  ThreadingStats summaryStats{};
  bool summaryThreaded = false;

  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  size_t threadingThresholdValue =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));

  bool threadingEnabled =
      m_useThreading.load(std::memory_order_acquire) &&
      HammerEngine::ThreadSystem::Exists();

  bool broadphaseUsedThreading = false;
  bool attemptedBroadphaseThreading = false;
  if (threadingEnabled) {
    size_t dynamicBodyCount = 0;
    for (const auto &kv : m_bodies) {
      const CollisionBody &body = *kv.second;
      if (body.enabled && body.type != BodyType::STATIC) {
        ++dynamicBodyCount;
      }
    }

    if (dynamicBodyCount >= threadingThresholdValue) {
      attemptedBroadphaseThreading = true;
      ThreadingStats stats;
      broadphaseUsedThreading = broadphaseThreaded(m_collisionPool.pairBuffer, stats);
      if (broadphaseUsedThreading) {
        summaryThreaded = true;
        summaryStats = stats;
      }
    } else {
      broadphase(m_collisionPool.pairBuffer);
    }
  }
  if (!threadingEnabled) {
    broadphase(m_collisionPool.pairBuffer);
  } else if (!attemptedBroadphaseThreading) {
    // Already executed sequential broadphase in the branch above
  } else if (!broadphaseUsedThreading && m_collisionPool.pairBuffer.empty()) {
    // Threading was attempted but skipped due to insufficient work; ensure pairs populated
    broadphase(m_collisionPool.pairBuffer);
  }
  auto t1 = clock::now();

  const size_t pairCount = m_collisionPool.pairBuffer.size();
  if (threadingEnabled && pairCount >= threadingThresholdValue) {
    ThreadingStats narrowStats;
    bool narrowphaseUsedThreading = narrowphaseThreaded(m_collisionPool.pairBuffer, m_collisionPool.collisionBuffer, narrowStats);
    if (narrowphaseUsedThreading) {
      summaryThreaded = true;
      summaryStats = narrowStats; // Use narrowphase stats if it was threaded
    } else {
      narrowphase(m_collisionPool.pairBuffer, m_collisionPool.collisionBuffer);
    }
  } else {
    narrowphase(m_collisionPool.pairBuffer, m_collisionPool.collisionBuffer);
  }
  auto t2 = clock::now();

  for (const auto &c : m_collisionPool.collisionBuffer) {
    resolve(c);
    for (const auto &cb : m_callbacks) {
      cb(c);
    }
  }
  auto t3 = clock::now();
  if (m_verboseLogs && !m_collisionPool.collisionBuffer.empty()) {
    COLLISION_DEBUG("Resolved collisions: count=" +
                    std::to_string(m_collisionPool.collisionBuffer.size()));
  }
  // Reflect resolved poses back to entities so callers see corrected transforms
  // Synchronize collision results back to entities
  // Skip kinematic bodies since they manage their own positions through
  // AI/input
  m_isSyncing = true;
  for (auto &kv : m_bodies) {
    auto &b = *kv.second;
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
    if (x > y)
      std::swap(x, y);
    // Simple mix (not cryptographic); sufficient for set keys
    return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
  };

  auto now = clock::now();
  std::unordered_set<uint64_t> currentPairs;
  currentPairs.reserve(m_collisionPool.collisionBuffer.size());
  for (const auto &c : m_collisionPool.collisionBuffer) {
    auto ita = m_bodies.find(c.a);
    auto itb = m_bodies.find(c.b);
    if (ita == m_bodies.end() || itb == m_bodies.end())
      continue;
    const CollisionBody &A = *ita->second;
    const CollisionBody &B = *itb->second;

    auto isPlayer = [](const CollisionBody &b) {
      return (b.layer & CollisionLayer::Layer_Player) != 0;
    };
    const CollisionBody *playerBody = nullptr;
    const CollisionBody *triggerBody = nullptr;

    if (isPlayer(A) && B.isTrigger) {
      playerBody = &A;
      triggerBody = &B;
    } else if (isPlayer(B) && A.isTrigger) {
      playerBody = &B;
      triggerBody = &A;
    } else {
      continue;
    }

    uint64_t key = makeKey(playerBody->id, triggerBody->id);
    currentPairs.insert(key);
    if (!m_activeTriggerPairs.count(key)) {
      // Cooldown check per trigger
      auto cdIt = m_triggerCooldownUntil.find(triggerBody->id);
      bool cooled =
          (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);
      if (cooled) {
        WorldTriggerEvent evt(playerBody->id, triggerBody->id,
                              triggerBody->triggerTag, playerBody->aabb.center,
                              TriggerPhase::Enter);
        EventManager::Instance().triggerWorldTrigger(
            evt, EventManager::DispatchMode::Deferred);
        COLLISION_INFO(
            "Trigger Enter: player=" + std::to_string(playerBody->id) +
            ", trigger=" + std::to_string(triggerBody->id) + ", tag=" +
            std::to_string(static_cast<int>(triggerBody->triggerTag)));
        if (m_defaultTriggerCooldownSec > 0.0f) {
          m_triggerCooldownUntil[triggerBody->id] =
              now +
              std::chrono::duration_cast<clock::duration>(
                  std::chrono::duration<double>(m_defaultTriggerCooldownSec));
        }
      }
      m_activeTriggerPairs.emplace(
          key, std::make_pair(playerBody->id, triggerBody->id));
    }
  }
  // Remove stale pairs (exited triggers) and dispatch Exit events
  for (auto it = m_activeTriggerPairs.begin();
       it != m_activeTriggerPairs.end();) {
    if (!currentPairs.count(it->first)) {
      // OnExit
      EntityID playerId = it->second.first;
      EntityID triggerId = it->second.second;
      auto bt = m_bodies.find(triggerId);
      if (bt != m_bodies.end()) {
        WorldTriggerEvent evt(playerId, triggerId, bt->second->triggerTag,
                              bt->second->aabb.center, TriggerPhase::Exit);
        EventManager::Instance().triggerWorldTrigger(
            evt, EventManager::DispatchMode::Deferred);
        COLLISION_INFO(
            "Trigger Exit: player=" + std::to_string(playerId) +
            ", trigger=" + std::to_string(triggerId) + ", tag=" +
            std::to_string(static_cast<int>(bt->second->triggerTag)));
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

  if (summaryThreaded) {
    m_lastWasThreaded.store(true, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(summaryStats.batchCount,
                                 std::memory_order_relaxed);
    m_lastOptimalWorkerCount.store(summaryStats.optimalWorkers,
                                   std::memory_order_relaxed);
    m_lastAvailableWorkers.store(summaryStats.availableWorkers,
                                 std::memory_order_relaxed);
    m_lastCollisionBudget.store(summaryStats.budget,
                                std::memory_order_relaxed);
  } else {
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
    m_lastOptimalWorkerCount.store(0, std::memory_order_relaxed);
    m_lastAvailableWorkers.store(0, std::memory_order_relaxed);
    m_lastCollisionBudget.store(0, std::memory_order_relaxed);
  }

  // Periodic collision statistics (every 300 frames like AIManager)
  if (m_perf.frames % 300 == 0 && m_perf.bodyCount > 0) {
    bool wasThreaded = m_lastWasThreaded.load(std::memory_order_relaxed);
    if (wasThreaded) {
      size_t optimalWorkers =
          m_lastOptimalWorkerCount.load(std::memory_order_relaxed);
      size_t availableWorkers =
          m_lastAvailableWorkers.load(std::memory_order_relaxed);
      size_t collisionBudget =
          m_lastCollisionBudget.load(std::memory_order_relaxed);
      size_t batchCount =
          std::max<size_t>(1, m_lastThreadBatchCount.load(std::memory_order_relaxed));

      COLLISION_DEBUG(
          "Collision Summary - Bodies: " + std::to_string(m_perf.bodyCount) +
          ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
          ", Broadphase: " + std::to_string(m_perf.lastBroadphaseMs) + "ms" +
          ", Narrowphase: " + std::to_string(m_perf.lastNarrowphaseMs) +
          "ms" + ", Last Pairs: " + std::to_string(m_perf.lastPairs) +
          ", Last Collisions: " + std::to_string(m_perf.lastCollisions) +
          " [Threaded: " + std::to_string(optimalWorkers) + "/" +
          std::to_string(availableWorkers) + " workers, Budget: " +
          std::to_string(collisionBudget) + ", Batches: " +
          std::to_string(batchCount) + "]");
    } else {
      COLLISION_DEBUG(
          "Collision Summary - Bodies: " + std::to_string(m_perf.bodyCount) +
          ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
          ", Broadphase: " + std::to_string(m_perf.lastBroadphaseMs) + "ms" +
          ", Narrowphase: " + std::to_string(m_perf.lastNarrowphaseMs) + "ms" +
          ", Last Pairs: " + std::to_string(m_perf.lastPairs) +
          ", Last Collisions: " + std::to_string(m_perf.lastCollisions));
    }
  }
}

void CollisionManager::addCollisionCallback(CollisionCB cb) {
  m_callbacks.push_back(std::move(cb));
}

void CollisionManager::logCollisionStatistics() const {
  size_t staticBodies = getStaticBodyCount();
  size_t kinematicBodies = getKinematicBodyCount();
  size_t dynamicBodies = getBodyCount() - staticBodies - kinematicBodies;

  COLLISION_INFO("Collision Statistics:");
  COLLISION_INFO("  Total Bodies: " + std::to_string(getBodyCount()));
  COLLISION_INFO("  Static Bodies: " + std::to_string(staticBodies) +
                 " (obstacles + triggers)");
  COLLISION_INFO("  Kinematic Bodies: " + std::to_string(kinematicBodies) +
                 " (NPCs)");
  COLLISION_INFO("  Dynamic Bodies: " + std::to_string(dynamicBodies) +
                 " (player, projectiles)");

  // Count bodies by layer
  std::map<uint32_t, size_t> layerCounts;
  for (const auto &kv : m_bodies) {
    const auto &body = *kv.second;
    layerCounts[body.layer]++;
  }

  COLLISION_INFO("  Layer Distribution:");
  for (const auto &layerCount : layerCounts) {
    std::string layerName;
    switch (layerCount.first) {
    case CollisionLayer::Layer_Default:
      layerName = "Default";
      break;
    case CollisionLayer::Layer_Player:
      layerName = "Player";
      break;
    case CollisionLayer::Layer_Enemy:
      layerName = "Enemy";
      break;
    case CollisionLayer::Layer_Environment:
      layerName = "Environment";
      break;
    case CollisionLayer::Layer_Projectile:
      layerName = "Projectile";
      break;
    case CollisionLayer::Layer_Trigger:
      layerName = "Trigger";
      break;
    default:
      layerName = "Unknown";
      break;
    }
    COLLISION_INFO("    " + layerName + ": " +
                   std::to_string(layerCount.second));
  }
}

size_t CollisionManager::getStaticBodyCount() const {
  return std::count_if(m_bodies.begin(), m_bodies.end(), [](const auto &kv) {
    return kv.second->type == BodyType::STATIC;
  });
}

size_t CollisionManager::getKinematicBodyCount() const {
  return std::count_if(m_bodies.begin(), m_bodies.end(), [](const auto &kv) {
    return kv.second->type == BodyType::KINEMATIC;
  });
}

void CollisionManager::rebuildStaticFromWorld() {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return;
  // Remove any existing STATIC world bodies
  std::vector<EntityID> toRemove;
  for (const auto &kv : m_bodies) {
    if (isStatic(*kv.second))
      toRemove.push_back(kv.first);
  }
  for (auto id : toRemove)
    removeBody(id);

  // Create solid collision bodies for obstacles and triggers for movement
  // penalties
  size_t solidBodies = createStaticObstacleBodies();
  size_t waterTriggers =
      createTriggersForWaterTiles(HammerEngine::TriggerTag::Water);
  size_t obstacleTriggers =
      createTriggersForObstacles(); // Always returns 0 - obstacle penalties
                                    // handled by pathfinding

  if (solidBodies > 0 || waterTriggers > 0) {
    COLLISION_INFO(
        "World colliders built: solid=" + std::to_string(solidBodies) +
        ", water triggers=" + std::to_string(waterTriggers) +
        ", obstacle triggers=" + std::to_string(obstacleTriggers));
    // Log detailed statistics for debugging
    logCollisionStatistics();
    // Invalidate static body cache since world changed
    invalidateStaticCache();
  }
}

void CollisionManager::onTileChanged(int x, int y) {
  const auto &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return;
  const float tileSize = 32.0f;

  if (y >= 0 && y < static_cast<int>(world->grid.size()) && x >= 0 &&
      x < static_cast<int>(world->grid[y].size())) {
    const auto &tile = world->grid[y][x];

    // Update water trigger for this tile
    EntityID trigId = (static_cast<EntityID>(1ull) << 61) |
                      (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
                      static_cast<EntityID>(static_cast<uint32_t>(x));
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
    // Remove old per-tile collision body (legacy)
    EntityID oldObstacleId =
        (static_cast<EntityID>(2ull) << 61) |
        (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
        static_cast<EntityID>(static_cast<uint32_t>(x));
    removeBody(oldObstacleId);

    if (tile.obstacleType == ObstacleType::BUILDING && tile.buildingId > 0) {
      // Only create collision body from the top-left tile of each building
      bool isTopLeft = true;
      if (x > 0 && world->grid[y][x - 1].buildingId == tile.buildingId)
        isTopLeft = false;
      if (y > 0 && world->grid[y - 1][x].buildingId == tile.buildingId)
        isTopLeft = false;

      if (isTopLeft) {
        // Create 64x64 collision body for the entire building using building ID
        EntityID buildingId =
            (static_cast<EntityID>(3ull) << 61) |
            static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
        removeBody(
            buildingId); // Remove existing building collision body if any

        float cx = x * tileSize + tileSize;    // Center at 1 tile offset
        float cy = y * tileSize + tileSize;    // Center at 1 tile offset
        AABB aabb(cx, cy, tileSize, tileSize); // 64x64 collision box
        addBody(buildingId, aabb, BodyType::STATIC);
        setBodyLayer(buildingId, CollisionLayer::Layer_Environment,
                     0xFFFFFFFFu);
        setBodyTrigger(buildingId, false);
      }
    } else if (tile.obstacleType != ObstacleType::BUILDING &&
               tile.buildingId > 0) {
      // Tile was a building but no longer is - remove the building collision
      // body
      EntityID buildingId =
          (static_cast<EntityID>(3ull) << 61) |
          static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
      removeBody(buildingId);
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
  auto &em = EventManager::Instance();
  auto token = em.registerHandlerWithToken(
      EventTypeId::World, [this](const EventData &data) {
        auto base = data.event;
        if (!base)
          return;
        if (auto loaded = std::dynamic_pointer_cast<WorldLoadedEvent>(base)) {
          (void)loaded;
          const auto &worldManager = WorldManager::Instance();
          float minX, minY, maxX, maxY;
          if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
            this->setWorldBounds(minX, minY, maxX, maxY);
          }
          COLLISION_INFO("World loaded - rebuilding static colliders");
          this->rebuildStaticFromWorld();
          return;
        }
        if (auto generated =
                std::dynamic_pointer_cast<WorldGeneratedEvent>(base)) {
          (void)generated;
          const auto &worldManager = WorldManager::Instance();
          float minX, minY, maxX, maxY;
          if (worldManager.getWorldBounds(minX, minY, maxX, maxY)) {
            this->setWorldBounds(minX, minY, maxX, maxY);
          }
          COLLISION_INFO("World generated - rebuilding static colliders");
          this->rebuildStaticFromWorld();
          return;
        }
        if (auto unloaded =
                std::dynamic_pointer_cast<WorldUnloadedEvent>(base)) {
          (void)unloaded;
          std::vector<EntityID> toRemove;
          for (const auto &kv : m_bodies)
            if (isStatic(*kv.second))
              toRemove.push_back(kv.first);
          for (auto id : toRemove)
            removeBody(id);
          COLLISION_INFO("World unloaded - removed static colliders: " +
                         std::to_string(toRemove.size()));
          // Invalidate static cache since static bodies were removed
          invalidateStaticCache();
          return;
        }
        if (auto tileChanged =
                std::dynamic_pointer_cast<TileChangedEvent>(base)) {
          this->onTileChanged(tileChanged->getX(), tileChanged->getY());
          return;
        }
      });
  m_handlerTokens.push_back(token);
}

void CollisionManager::configureThreading(bool useThreading,
                                          unsigned int maxThreads) {
  m_useThreading.store(useThreading, std::memory_order_release);
  m_maxThreads = maxThreads;
  COLLISION_INFO("Collision threading " +
                 std::string(useThreading ? "enabled" : "disabled") +
                 " with max threads: " + std::to_string(m_maxThreads));
}

void CollisionManager::setThreadingThreshold(size_t threshold) {
  threshold = std::max(static_cast<size_t>(1), threshold);
  m_threadingThreshold.store(threshold, std::memory_order_release);
  COLLISION_INFO("Collision threading threshold set to " + std::to_string(threshold) +
          " bodies");
}

size_t CollisionManager::getThreadingThreshold() const {
  return m_threadingThreshold.load(std::memory_order_acquire);
}

// ========== NEW SOA STORAGE MANAGEMENT METHODS ==========

size_t CollisionManager::addCollisionBodySOA(EntityID id, const Vector2D& position,
                                              const Vector2D& halfSize, BodyType type,
                                              uint32_t layer, uint32_t collidesWith) {
  // Check if entity already exists
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end()) {
    // Update existing entity
    size_t index = it->second;
    if (index < m_storage.hotData.size()) {
      auto& hot = m_storage.hotData[index];
      hot.position = position;
      hot.halfSize = halfSize;
      hot.layers = layer;
      hot.collidesWith = collidesWith;
      hot.bodyType = static_cast<uint8_t>(type);
      hot.active = true;

      // Update cold data
      if (index < m_storage.coldData.size()) {
        m_storage.coldData[index].fullAABB = AABB(position.getX(), position.getY(),
                                                  halfSize.getX(), halfSize.getY());
      }

      COLLISION_DEBUG("Updated existing SOA body for entity " + std::to_string(id) +
                      " at index " + std::to_string(index));
      return index;
    }
  }

  // Add new entity
  size_t newIndex = m_storage.size();

  // Initialize hot data
  CollisionStorage::HotData hotData{};
  hotData.position = position;
  hotData.velocity = Vector2D(0, 0);
  hotData.halfSize = halfSize;
  hotData.layers = layer;
  hotData.collidesWith = collidesWith;
  hotData.bodyType = static_cast<uint8_t>(type);
  hotData.triggerTag = static_cast<uint8_t>(HammerEngine::TriggerTag::None);
  hotData.active = true;
  hotData.isTrigger = false;
  hotData.mass = 1.0f;
  hotData.friction = 0.8f;
  hotData.restitution = 0.0f;

  // Initialize cold data
  CollisionStorage::ColdData coldData{};
  coldData.acceleration = Vector2D(0, 0);
  coldData.lastPosition = position;
  coldData.fullAABB = AABB(position.getX(), position.getY(), halfSize.getX(), halfSize.getY());

  // Add to storage
  m_storage.hotData.push_back(hotData);
  m_storage.coldData.push_back(coldData);
  m_storage.entityIds.push_back(id);
  m_storage.entityToIndex[id] = newIndex;

  COLLISION_DEBUG("Added new SOA body for entity " + std::to_string(id) +
                  " at index " + std::to_string(newIndex) +
                  ", type: " + std::to_string(static_cast<int>(type)));

  return newIndex;
}

void CollisionManager::removeCollisionBodySOA(EntityID id) {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end()) {
    return; // Entity not found
  }

  size_t indexToRemove = it->second;
  size_t lastIndex = m_storage.size() - 1;

  if (indexToRemove < m_storage.size()) {
    // Swap with last element and pop (to maintain contiguous arrays)
    if (indexToRemove != lastIndex) {
      // Swap hot data
      m_storage.hotData[indexToRemove] = m_storage.hotData[lastIndex];
      // Swap cold data
      m_storage.coldData[indexToRemove] = m_storage.coldData[lastIndex];
      // Swap entity IDs
      m_storage.entityIds[indexToRemove] = m_storage.entityIds[lastIndex];

      // Update index mapping for the moved entity
      EntityID movedEntityId = m_storage.entityIds[indexToRemove];
      m_storage.entityToIndex[movedEntityId] = indexToRemove;
    }

    // Remove last elements
    m_storage.hotData.pop_back();
    m_storage.coldData.pop_back();
    m_storage.entityIds.pop_back();
  }

  // Remove from index mapping
  m_storage.entityToIndex.erase(it);

  COLLISION_DEBUG("Removed SOA body for entity " + std::to_string(id) +
                  " from index " + std::to_string(indexToRemove));
}

bool CollisionManager::getCollisionBodySOA(EntityID id, size_t& outIndex) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
    outIndex = it->second;
    return true;
  }
  return false;
}

void CollisionManager::updateCollisionBodyPositionSOA(EntityID id, const Vector2D& newPosition) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    auto& hot = m_storage.hotData[index];
    hot.position = newPosition;

    // Update full AABB in cold data
    if (index < m_storage.coldData.size()) {
      m_storage.coldData[index].fullAABB = AABB(newPosition.getX(), newPosition.getY(),
                                                hot.halfSize.getX(), hot.halfSize.getY());
    }
  }
}

void CollisionManager::updateCollisionBodyVelocitySOA(EntityID id, const Vector2D& newVelocity) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].velocity = newVelocity;
  }
}

// ========== DOUBLE BUFFERING SYSTEM METHODS ==========

// Buffer management methods moved to private section for implementation

// ========== OBJECT POOL MANAGEMENT METHODS ==========

void CollisionManager::prepareCollisionBuffers(size_t bodyCount) {
  // Ensure main collision pool has adequate capacity
  m_collisionPool.ensureCapacity(bodyCount);

  // Reset all pools for this frame
  m_collisionPool.resetFrame();

  COLLISION_DEBUG("Prepared collision buffers for " + std::to_string(bodyCount) + " bodies");
}

void CollisionManager::buildActiveIndicesSOA() {
  // Build indices of active bodies for efficient processing
  auto& pools = m_collisionPool;
  pools.activeIndices.clear();
  pools.dynamicIndices.clear();
  pools.staticIndices.clear();

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (hot.active) {
      pools.activeIndices.push_back(i);

      // Categorize by body type for optimized processing
      BodyType bodyType = static_cast<BodyType>(hot.bodyType);
      if (bodyType == BodyType::STATIC) {
        pools.staticIndices.push_back(i);
      } else {
        pools.dynamicIndices.push_back(i);
      }
    }
  }

  COLLISION_DEBUG("Built active indices: " + std::to_string(pools.activeIndices.size()) +
                  " total (" + std::to_string(pools.dynamicIndices.size()) + " dynamic, " +
                  std::to_string(pools.staticIndices.size()) + " static)");
}

// Internal helper methods moved to private section

// ========== NEW SOA-BASED BROADPHASE IMPLEMENTATION ==========

void CollisionManager::broadphaseSOA(std::vector<std::pair<size_t, size_t>>& indexPairs) const {
  indexPairs.clear();

  // Build active indices for this frame
  const_cast<CollisionManager*>(this)->buildActiveIndicesSOA();

  const auto& pools = m_collisionPool;
  const auto& dynamicIndices = pools.dynamicIndices;
  const auto& staticIndices = pools.staticIndices;

  // Reserve space for expected pairs
  size_t expectedPairs = dynamicIndices.size() * 8; // Conservative estimate
  indexPairs.reserve(expectedPairs);

  std::unordered_set<uint64_t> seenPairs;
  seenPairs.reserve(expectedPairs);

  // OPTIMIZATION: Process only dynamic bodies (static never initiate collisions)
  for (size_t dynamicIdx : dynamicIndices) {
    const auto& dynamicHot = m_storage.hotData[dynamicIdx];
    if (!dynamicHot.active) continue;

    AABB dynamicAABB = m_storage.computeAABB(dynamicIdx);

    // Query hierarchical spatial hash for candidates
    std::vector<size_t> candidates;

    // 1. Query dynamic spatial hash for dynamic-vs-dynamic collisions
    m_dynamicSpatialHash.queryBroadphase(dynamicIdx, dynamicAABB, candidates);
    for (size_t candidateIdx : candidates) {
      if (candidateIdx == dynamicIdx || candidateIdx >= m_storage.hotData.size()) continue;

      const auto& candidateHot = m_storage.hotData[candidateIdx];
      if (!candidateHot.active) continue;

      // Check collision masks
      if ((dynamicHot.collidesWith & candidateHot.layers) == 0) continue;

      // Create canonical pair key (smaller index first for deduplication)
      size_t a = std::min(dynamicIdx, candidateIdx);
      size_t b = std::max(dynamicIdx, candidateIdx);
      uint64_t pairKey = (static_cast<uint64_t>(a) << 32) | b;

      if (seenPairs.emplace(pairKey).second) {
        indexPairs.emplace_back(a, b);
      }
    }

    // 2. Query static spatial hash for dynamic-vs-static collisions
    candidates.clear();
    m_staticSpatialHash.queryBroadphase(dynamicIdx, dynamicAABB, candidates);
    for (size_t staticIdx : candidates) {
      if (staticIdx >= m_storage.hotData.size()) continue;

      const auto& staticHot = m_storage.hotData[staticIdx];
      if (!staticHot.active) continue;

      // Check collision masks
      if ((dynamicHot.collidesWith & staticHot.layers) == 0) continue;

      // No deduplication needed for dynamic-vs-static (static never initiates)
      indexPairs.emplace_back(dynamicIdx, staticIdx);
    }
  }

  COLLISION_DEBUG("SOA Broadphase generated " + std::to_string(indexPairs.size()) +
                  " pairs from " + std::to_string(dynamicIndices.size()) + " dynamic and " +
                  std::to_string(staticIndices.size()) + " static bodies");
}

bool CollisionManager::broadphaseSOAThreaded(std::vector<std::pair<size_t, size_t>>& indexPairs,
                                             ThreadingStats& stats) {
  indexPairs.clear();

  if (!HammerEngine::ThreadSystem::Exists()) {
    broadphaseSOA(indexPairs);
    return false;
  }

  // Build active indices
  const_cast<CollisionManager*>(this)->buildActiveIndicesSOA();

  const auto& pools = m_collisionPool;
  const auto& dynamicIndices = pools.dynamicIndices;

  if (dynamicIndices.empty()) {
    return false;
  }

  auto& threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
  size_t queueSize = threadSystem.getQueueSize();
  size_t queueCapacity = threadSystem.getQueueCapacity();
  if (queueCapacity == 0) queueCapacity = 1;

  // Calculate worker budget using proper WorkerBudget system
  HammerEngine::WorkerBudget budget =
      HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t collisionBudget = budget.collisionAllocated;
  if (collisionBudget == 0) {
    broadphaseSOA(indexPairs);
    return false;
  }

  size_t threadingThresholdValue =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  size_t optimalWorkers = budget.getOptimalWorkerCount(
      collisionBudget, dynamicIndices.size(), threadingThresholdValue);
  optimalWorkers = std::max<size_t>(1, optimalWorkers);

  // Dynamic batch sizing based on queue pressure
  double queuePressure = static_cast<double>(queueSize) / queueCapacity;
  size_t baseBatchSize = std::max(threadingThresholdValue / 2, static_cast<size_t>(64));
  size_t minBodiesPerBatch = baseBatchSize;
  size_t maxBatches = std::max<size_t>(2, optimalWorkers);

  if (queuePressure > HammerEngine::QUEUE_PRESSURE_WARNING) {
    minBodiesPerBatch = std::min(baseBatchSize * 2, threadingThresholdValue * 2);
    maxBatches = std::max<size_t>(2, optimalWorkers / 2);
  } else if (queuePressure < (1.0 - HammerEngine::QUEUE_PRESSURE_WARNING)) {
    minBodiesPerBatch = std::max({baseBatchSize / 2, threadingThresholdValue / 4, static_cast<size_t>(32)});
    maxBatches = std::max<size_t>(4, optimalWorkers);
  }

  size_t desiredBatchCount = (dynamicIndices.size() + minBodiesPerBatch - 1) / minBodiesPerBatch;
  size_t batchCount = std::min(optimalWorkers, desiredBatchCount);
  batchCount = std::min(batchCount, maxBatches);
  batchCount = std::max<size_t>(1, batchCount);

  if (batchCount <= 1) {
    broadphaseSOA(indexPairs);
    return false;
  }

  // Prepare thread pools
  const_cast<CollisionManager*>(this)->prepareCollisionBuffers(m_storage.size());

  // Distribute work across threads
  size_t bodiesPerBatch = dynamicIndices.size() / batchCount;
  size_t remainingBodies = dynamicIndices.size() % batchCount;

  std::vector<std::future<std::vector<std::pair<size_t, size_t>>>> futures;
  futures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t start = i * bodiesPerBatch;
    size_t end = start + bodiesPerBatch;
    if (i == batchCount - 1) {
      end += remainingBodies;
    }

    futures.push_back(threadSystem.enqueueTaskWithResult(
        [this, start, end, &dynamicIndices]() -> std::vector<std::pair<size_t, size_t>> {
          std::vector<std::pair<size_t, size_t>> localPairs;
          localPairs.reserve((end - start) * 8);
          std::unordered_set<uint64_t> localSeenPairs;
          localSeenPairs.reserve((end - start) * 8);

          for (size_t i = start; i < end; ++i) {
            size_t dynamicIdx = dynamicIndices[i];
            const auto& dynamicHot = m_storage.hotData[dynamicIdx];
            if (!dynamicHot.active) continue;

            AABB dynamicAABB = m_storage.computeAABB(dynamicIdx);

            // Query candidates from both spatial hashes
            std::vector<size_t> candidates;

            // Dynamic-vs-dynamic
            m_dynamicSpatialHash.queryBroadphase(dynamicIdx, dynamicAABB, candidates);
            for (size_t candidateIdx : candidates) {
              if (candidateIdx == dynamicIdx || candidateIdx >= m_storage.hotData.size()) continue;

              const auto& candidateHot = m_storage.hotData[candidateIdx];
              if (!candidateHot.active) continue;
              if ((dynamicHot.collidesWith & candidateHot.layers) == 0) continue;

              size_t a = std::min(dynamicIdx, candidateIdx);
              size_t b = std::max(dynamicIdx, candidateIdx);
              uint64_t pairKey = (static_cast<uint64_t>(a) << 32) | b;

              if (localSeenPairs.emplace(pairKey).second) {
                localPairs.emplace_back(a, b);
              }
            }

            // Dynamic-vs-static
            candidates.clear();
            m_staticSpatialHash.queryBroadphase(dynamicIdx, dynamicAABB, candidates);
            for (size_t staticIdx : candidates) {
              if (staticIdx >= m_storage.hotData.size()) continue;

              const auto& staticHot = m_storage.hotData[staticIdx];
              if (!staticHot.active) continue;
              if ((dynamicHot.collidesWith & staticHot.layers) == 0) continue;

              localPairs.emplace_back(dynamicIdx, staticIdx);
            }
          }

          return localPairs;
        },
        HammerEngine::TaskPriority::High, "CollisionSOA_BroadphaseBatch"));
  }

  // Merge results from all threads
  std::unordered_set<uint64_t> globalSeenPairs;
  globalSeenPairs.reserve(dynamicIndices.size() * 8);

  for (auto& future : futures) {
    auto localPairs = future.get();
    for (const auto& pair : localPairs) {
      // For dynamic-vs-static, no deduplication needed
      // For dynamic-vs-dynamic, deduplicate using global set
      if (pair.first < pair.second) { // Dynamic-vs-dynamic (already deduplicated locally)
        uint64_t pairKey = (static_cast<uint64_t>(pair.first) << 32) | pair.second;
        if (globalSeenPairs.emplace(pairKey).second) {
          indexPairs.push_back(pair);
        }
      } else {
        indexPairs.push_back(pair); // Dynamic-vs-static, no global deduplication needed
      }
    }
  }

  stats.optimalWorkers = optimalWorkers;
  stats.availableWorkers = availableWorkers;
  stats.budget = collisionBudget;
  stats.batchCount = batchCount;

  return true;
}

void CollisionManager::narrowphaseSOA(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                                      std::vector<CollisionInfo>& collisions) const {
  collisions.clear();
  collisions.reserve(indexPairs.size() / 4); // Conservative estimate

  for (const auto& [aIdx, bIdx] : indexPairs) {
    if (aIdx >= m_storage.hotData.size() || bIdx >= m_storage.hotData.size()) continue;

    const auto& hotA = m_storage.hotData[aIdx];
    const auto& hotB = m_storage.hotData[bIdx];

    if (!hotA.active || !hotB.active) continue;

    // Compute AABBs from hot data
    AABB aabbA = m_storage.computeAABB(aIdx);
    AABB aabbB = m_storage.computeAABB(bIdx);

    // AABB intersection test
    if (!aabbA.intersects(aabbB)) continue;

    // Compute collision details
    float dxLeft = aabbB.right() - aabbA.left();
    float dxRight = aabbA.right() - aabbB.left();
    float dyTop = aabbB.bottom() - aabbA.top();
    float dyBottom = aabbA.bottom() - aabbB.top();

    float minPen = dxLeft;
    Vector2D normal(-1, 0);
    if (dxRight < minPen) {
      minPen = dxRight;
      normal = Vector2D(1, 0);
    }
    if (dyTop < minPen) {
      minPen = dyTop;
      normal = Vector2D(0, -1);
    }
    if (dyBottom < minPen) {
      minPen = dyBottom;
      normal = Vector2D(0, 1);
    }

    // Create collision info using EntityIDs
    EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
    EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;

    bool isTrigger = hotA.isTrigger || hotB.isTrigger;

    collisions.push_back(CollisionInfo{
        entityA, entityB, normal, minPen, isTrigger
    });
  }

  COLLISION_DEBUG("SOA Narrowphase processed " + std::to_string(indexPairs.size()) +
                  " pairs, found " + std::to_string(collisions.size()) + " collisions");
}

bool CollisionManager::narrowphaseSOAThreaded(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                                              std::vector<CollisionInfo>& collisions,
                                              ThreadingStats& stats) {
  collisions.clear();

  if (!HammerEngine::ThreadSystem::Exists() || indexPairs.empty()) {
    narrowphaseSOA(indexPairs, collisions);
    return false;
  }

  auto& threadSystem = HammerEngine::ThreadSystem::Instance();
  size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());

  // Use same threading strategy as broadphase
  HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(availableWorkers);
  size_t collisionBudget = budget.collisionAllocated;
  size_t threadingThresholdValue = std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));

  if (indexPairs.size() < threadingThresholdValue || collisionBudget == 0) {
    narrowphaseSOA(indexPairs, collisions);
    return false;
  }

  size_t optimalWorkers = budget.getOptimalWorkerCount(collisionBudget, indexPairs.size(), threadingThresholdValue);
  size_t batchCount = std::min(optimalWorkers, indexPairs.size() / 64);
  batchCount = std::max<size_t>(1, batchCount);

  if (batchCount <= 1) {
    narrowphaseSOA(indexPairs, collisions);
    return false;
  }

  size_t pairsPerBatch = indexPairs.size() / batchCount;
  size_t remainingPairs = indexPairs.size() % batchCount;

  std::vector<std::future<std::vector<CollisionInfo>>> futures;
  futures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t start = i * pairsPerBatch;
    size_t end = start + pairsPerBatch;
    if (i == batchCount - 1) {
      end += remainingPairs;
    }

    futures.push_back(threadSystem.enqueueTaskWithResult(
        [this, start, end, &indexPairs]() -> std::vector<CollisionInfo> {
          std::vector<CollisionInfo> localCollisions;
          localCollisions.reserve((end - start) / 4);

          for (size_t i = start; i < end; ++i) {
            const auto& [aIdx, bIdx] = indexPairs[i];
            if (aIdx >= m_storage.hotData.size() || bIdx >= m_storage.hotData.size()) continue;

            const auto& hotA = m_storage.hotData[aIdx];
            const auto& hotB = m_storage.hotData[bIdx];

            if (!hotA.active || !hotB.active) continue;

            AABB aabbA = m_storage.computeAABB(aIdx);
            AABB aabbB = m_storage.computeAABB(bIdx);

            if (!aabbA.intersects(aabbB)) continue;

            // Compute penetration and normal
            float dxLeft = aabbB.right() - aabbA.left();
            float dxRight = aabbA.right() - aabbB.left();
            float dyTop = aabbB.bottom() - aabbA.top();
            float dyBottom = aabbA.bottom() - aabbB.top();

            float minPen = dxLeft;
            Vector2D normal(-1, 0);
            if (dxRight < minPen) {
              minPen = dxRight;
              normal = Vector2D(1, 0);
            }
            if (dyTop < minPen) {
              minPen = dyTop;
              normal = Vector2D(0, -1);
            }
            if (dyBottom < minPen) {
              minPen = dyBottom;
              normal = Vector2D(0, 1);
            }

            EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
            EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;

            bool isTrigger = hotA.isTrigger || hotB.isTrigger;

            localCollisions.push_back(CollisionInfo{
                entityA, entityB, normal, minPen, isTrigger
            });
          }

          return localCollisions;
        },
        HammerEngine::TaskPriority::High, "CollisionSOA_NarrowphaseBatch"));
  }

  // Merge results
  for (auto& future : futures) {
    auto localCollisions = future.get();
    collisions.insert(collisions.end(), localCollisions.begin(), localCollisions.end());
  }

  stats.optimalWorkers = optimalWorkers;
  stats.availableWorkers = availableWorkers;
  stats.budget = collisionBudget;
  stats.batchCount = batchCount;

  return true;
}

// ========== NEW SOA UPDATE METHOD ==========

void CollisionManager::updateSOA(float dt) {
  (void)dt;

  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();

  // Prepare collision processing for this frame
  size_t bodyCount = m_storage.size();
  prepareCollisionBuffers(bodyCount); // Prepare collision buffers

  // Sync spatial hashes with SOA storage
  syncSpatialHashesWithSOA();

  // Determine threading strategy using WorkerBudget system
  size_t threadingThresholdValue =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  bool threadingEnabled = m_useThreading.load(std::memory_order_acquire) &&
                          HammerEngine::ThreadSystem::Exists();

  ThreadingStats summaryStats{};
  bool summaryThreaded = false;

  // Count active dynamic bodies for threading decisions
  buildActiveIndicesSOA();
  size_t activeDynamicBodies = m_collisionPool.dynamicIndices.size();

  // Object pool for SOA collision processing
  std::vector<std::pair<size_t, size_t>> indexPairs;

  // BROADPHASE: Generate collision pairs using hierarchical spatial hash
  auto t1 = clock::now();
  bool broadphaseUsedThreading = false;
  if (threadingEnabled && activeDynamicBodies >= threadingThresholdValue) {
    ThreadingStats stats;
    broadphaseUsedThreading = broadphaseSOAThreaded(indexPairs, stats);
    if (broadphaseUsedThreading) {
      summaryThreaded = true;
      summaryStats = stats;
    }
  }

  if (!broadphaseUsedThreading) {
    broadphaseSOA(indexPairs);
  }
  auto t2 = clock::now();

  // NARROWPHASE: Detailed collision detection and response calculation
  const size_t pairCount = indexPairs.size();
  if (threadingEnabled && pairCount >= threadingThresholdValue) {
    ThreadingStats narrowStats;
    bool narrowphaseUsedThreading = narrowphaseSOAThreaded(indexPairs, m_collisionPool.collisionBuffer, narrowStats);
    if (narrowphaseUsedThreading) {
      summaryThreaded = true;
      summaryStats = narrowStats; // Use narrowphase stats if it was threaded
    }
  }

  if (!summaryThreaded || m_collisionPool.collisionBuffer.empty()) {
    narrowphaseSOA(indexPairs, m_collisionPool.collisionBuffer);
  }
  auto t3 = clock::now();

  // RESOLUTION: Apply collision responses and update positions
  for (const auto& collision : m_collisionPool.collisionBuffer) {
    resolveSOA(collision);
    for (const auto& cb : m_callbacks) {
      cb(collision);
    }
  }
  auto t4 = clock::now();

  if (m_verboseLogs && !m_collisionPool.collisionBuffer.empty()) {
    COLLISION_DEBUG("Resolved SOA collisions: count=" +
                    std::to_string(m_collisionPool.collisionBuffer.size()));
  }

  // SYNCHRONIZATION: Update entity positions and velocities from SOA storage
  syncEntitiesToSOA();
  auto t5 = clock::now();

  // TRIGGER PROCESSING: Handle trigger enter/exit events
  processTriggerEventsSOA();
  auto t6 = clock::now();

  // PERFORMANCE METRICS: Track timing and threading stats
  updatePerformanceMetricsSOA(t0, t1, t2, t3, t4, t5, t6, summaryThreaded,
                               summaryStats.optimalWorkers, summaryStats.availableWorkers,
                               summaryStats.budget, summaryStats.batchCount,
                               bodyCount, activeDynamicBodies, pairCount, m_collisionPool.collisionBuffer.size());

  COLLISION_DEBUG("SOA Update complete: " + std::to_string(bodyCount) + " bodies, " +
                  std::to_string(pairCount) + " pairs, " +
                  std::to_string(m_collisionPool.collisionBuffer.size()) + " collisions" +
                  (summaryThreaded ? " [Threaded]" : " [Single-threaded]"));
}

// ========== SOA UPDATE HELPER METHODS ==========

void CollisionManager::syncSpatialHashesWithSOA() {
  // Clear and rebuild spatial hashes from SOA storage
  m_staticSpatialHash.clear();
  m_dynamicSpatialHash.clear();

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    AABB aabb = m_storage.computeAABB(i);
    BodyType bodyType = static_cast<BodyType>(hot.bodyType);

    if (bodyType == BodyType::STATIC) {
      m_staticSpatialHash.insert(i, aabb);
    } else {
      m_dynamicSpatialHash.insert(i, aabb);
    }
  }

  COLLISION_DEBUG("Synced spatial hashes with SOA storage: " +
                  std::to_string(m_storage.hotData.size()) + " bodies");
}

void CollisionManager::resolveSOA(const CollisionInfo& collision) {
  if (collision.trigger) return; // Triggers don't need position resolution

  // Find the indices for the colliding entities
  size_t indexA = SIZE_MAX, indexB = SIZE_MAX;
  for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
    if (m_storage.entityIds[i] == collision.a) indexA = i;
    if (m_storage.entityIds[i] == collision.b) indexB = i;
    if (indexA != SIZE_MAX && indexB != SIZE_MAX) break;
  }

  if (indexA >= m_storage.hotData.size() || indexB >= m_storage.hotData.size()) return;

  auto& hotA = m_storage.hotData[indexA];
  auto& hotB = m_storage.hotData[indexB];

  BodyType typeA = static_cast<BodyType>(hotA.bodyType);
  BodyType typeB = static_cast<BodyType>(hotB.bodyType);

  const float push = collision.penetration * 0.5f;

  // Apply position corrections
  if (typeA != BodyType::STATIC && typeB != BodyType::STATIC) {
    // Both dynamic/kinematic - split the correction
    Vector2D correction = collision.normal * push;
    hotA.position -= correction;
    hotB.position += correction;
  } else if (typeA != BodyType::STATIC) {
    // Only A moves
    hotA.position -= collision.normal * collision.penetration;
  } else if (typeB != BodyType::STATIC) {
    // Only B moves
    hotB.position += collision.normal * collision.penetration;
  }

  // Apply velocity damping for dynamic bodies
  auto dampenVelocity = [&collision](Vector2D& velocity, float restitution) {
    float vdotn = velocity.getX() * collision.normal.getX() +
                  velocity.getY() * collision.normal.getY();
    if (vdotn > 0) return; // Moving away from collision

    Vector2D normalVelocity = collision.normal * vdotn;
    velocity -= normalVelocity * (1.0f + restitution);
  };

  if (typeA == BodyType::DYNAMIC) {
    dampenVelocity(hotA.velocity, hotA.restitution);
  }
  if (typeB == BodyType::DYNAMIC) {
    dampenVelocity(hotB.velocity, hotB.restitution);
  }

  // Add tangential slide for NPC-vs-NPC collisions (but not player)
  if (typeA == BodyType::DYNAMIC && typeB == BodyType::DYNAMIC) {
    bool isPlayerA = (hotA.layers & CollisionLayer::Layer_Player) != 0;
    bool isPlayerB = (hotB.layers & CollisionLayer::Layer_Player) != 0;

    if (!isPlayerA && !isPlayerB) {
      Vector2D tangent(-collision.normal.getY(), collision.normal.getX());
      float slideBoost = std::min(5.0f, std::max(0.5f, collision.penetration * 5.0f));

      EntityID entityA = (indexA < m_storage.entityIds.size()) ? m_storage.entityIds[indexA] : 0;
      EntityID entityB = (indexB < m_storage.entityIds.size()) ? m_storage.entityIds[indexB] : 0;

      if (entityA < entityB) {
        hotA.velocity += tangent * slideBoost;
        hotB.velocity -= tangent * slideBoost;
      } else {
        hotA.velocity -= tangent * slideBoost;
        hotB.velocity += tangent * slideBoost;
      }
    }
  }

  // Clamp velocities to reasonable limits
  auto clampVelocity = [](Vector2D& velocity) {
    const float maxSpeed = 300.0f;
    float speed = velocity.length();
    if (speed > maxSpeed && speed > 0.0f) {
      velocity = velocity * (maxSpeed / speed);
    }
  };

  clampVelocity(hotA.velocity);
  clampVelocity(hotB.velocity);
}

void CollisionManager::syncEntitiesToSOA() {
  // Sync SOA positions and velocities back to entities
  m_isSyncing = true;

  for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
    if (i >= m_storage.hotData.size() || i >= m_storage.coldData.size()) continue;

    const auto& hot = m_storage.hotData[i];
    auto& cold = m_storage.coldData[i];

    // Skip kinematic bodies (they manage their own positions through AI/input)
    BodyType bodyType = static_cast<BodyType>(hot.bodyType);
    if (bodyType == BodyType::KINEMATIC) continue;

    if (auto entity = cold.entityWeak.lock()) {
      entity->setPosition(hot.position);
      entity->setVelocity(hot.velocity);
    }
  }

  m_isSyncing = false;
}

void CollisionManager::processTriggerEventsSOA() {
  // Process trigger enter/exit events using SOA data
  // This is similar to the legacy trigger processing but uses SOA storage

  auto makeKey = [](EntityID a, EntityID b) -> uint64_t {
    uint64_t x = static_cast<uint64_t>(a);
    uint64_t y = static_cast<uint64_t>(b);
    if (x > y) std::swap(x, y);
    return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
  };

  auto now = std::chrono::steady_clock::now();
  std::unordered_set<uint64_t> currentPairs;
  currentPairs.reserve(m_collisionPool.collisionBuffer.size());

  for (const auto& collision : m_collisionPool.collisionBuffer) {
    // Find body indices
    size_t indexA = SIZE_MAX, indexB = SIZE_MAX;
    for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
      if (m_storage.entityIds[i] == collision.a) indexA = i;
      if (m_storage.entityIds[i] == collision.b) indexB = i;
      if (indexA != SIZE_MAX && indexB != SIZE_MAX) break;
    }

    if (indexA >= m_storage.hotData.size() || indexB >= m_storage.hotData.size()) continue;

    const auto& hotA = m_storage.hotData[indexA];
    const auto& hotB = m_storage.hotData[indexB];

    // Check for player-trigger interactions
    bool isPlayerA = (hotA.layers & CollisionLayer::Layer_Player) != 0;
    bool isPlayerB = (hotB.layers & CollisionLayer::Layer_Player) != 0;
    bool isTriggerA = hotA.isTrigger;
    bool isTriggerB = hotB.isTrigger;

    const CollisionStorage::HotData* playerHot = nullptr;
    const CollisionStorage::HotData* triggerHot = nullptr;
    EntityID playerId = 0, triggerId = 0;

    if (isPlayerA && isTriggerB) {
      playerHot = &hotA;
      triggerHot = &hotB;
      playerId = collision.a;
      triggerId = collision.b;
    } else if (isPlayerB && isTriggerA) {
      playerHot = &hotB;
      triggerHot = &hotA;
      playerId = collision.b;
      triggerId = collision.a;
    } else {
      continue; // Not a player-trigger interaction
    }

    uint64_t key = makeKey(playerId, triggerId);
    currentPairs.insert(key);

    if (!m_activeTriggerPairs.count(key)) {
      // Check cooldown
      auto cdIt = m_triggerCooldownUntil.find(triggerId);
      bool cooled = (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);

      if (cooled) {
        HammerEngine::TriggerTag triggerTag = static_cast<HammerEngine::TriggerTag>(triggerHot->triggerTag);
        WorldTriggerEvent evt(playerId, triggerId, triggerTag, playerHot->position, TriggerPhase::Enter);
        EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);

        COLLISION_INFO("SOA Trigger Enter: player=" + std::to_string(playerId) +
                       ", trigger=" + std::to_string(triggerId) + ", tag=" +
                       std::to_string(static_cast<int>(triggerTag)));

        if (m_defaultTriggerCooldownSec > 0.0f) {
          m_triggerCooldownUntil[triggerId] = now +
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double>(m_defaultTriggerCooldownSec));
        }
      }

      m_activeTriggerPairs.emplace(key, std::make_pair(playerId, triggerId));
    }
  }

  // Remove stale pairs (trigger exits)
  for (auto it = m_activeTriggerPairs.begin(); it != m_activeTriggerPairs.end();) {
    if (!currentPairs.count(it->first)) {
      EntityID playerId = it->second.first;
      EntityID triggerId = it->second.second;

      // Find trigger hot data for position
      Vector2D triggerPos(0, 0);
      HammerEngine::TriggerTag triggerTag = HammerEngine::TriggerTag::None;
      for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
        if (m_storage.entityIds[i] == triggerId && i < m_storage.hotData.size()) {
          const auto& hot = m_storage.hotData[i];
          triggerPos = hot.position;
          triggerTag = static_cast<HammerEngine::TriggerTag>(hot.triggerTag);
          break;
        }
      }

      WorldTriggerEvent evt(playerId, triggerId, triggerTag, triggerPos, TriggerPhase::Exit);
      EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);

      COLLISION_INFO("SOA Trigger Exit: player=" + std::to_string(playerId) +
                     ", trigger=" + std::to_string(triggerId) + ", tag=" +
                     std::to_string(static_cast<int>(triggerTag)));

      it = m_activeTriggerPairs.erase(it);
    } else {
      ++it;
    }
  }
}

void CollisionManager::updatePerformanceMetricsSOA(
    std::chrono::steady_clock::time_point t0,
    std::chrono::steady_clock::time_point t1,
    std::chrono::steady_clock::time_point t2,
    std::chrono::steady_clock::time_point t3,
    std::chrono::steady_clock::time_point t4,
    std::chrono::steady_clock::time_point t5,
    std::chrono::steady_clock::time_point t6,
    bool wasThreaded,
    size_t optimalWorkers,
    size_t availableWorkers,
    size_t budget,
    size_t batchCount,
    size_t bodyCount,
    size_t activeDynamicBodies,
    size_t pairCount,
    size_t collisionCount) {

  // Calculate timing metrics
  auto d01 = std::chrono::duration<double, std::milli>(t1 - t0).count(); // Sync spatial hash
  auto d12 = std::chrono::duration<double, std::milli>(t2 - t1).count(); // Broadphase
  auto d23 = std::chrono::duration<double, std::milli>(t3 - t2).count(); // Narrowphase
  auto d34 = std::chrono::duration<double, std::milli>(t4 - t3).count(); // Resolution
  auto d45 = std::chrono::duration<double, std::milli>(t5 - t4).count(); // Sync entities
  auto d56 = std::chrono::duration<double, std::milli>(t6 - t5).count(); // Trigger processing
  auto d06 = std::chrono::duration<double, std::milli>(t6 - t0).count(); // Total

  m_perf.lastBroadphaseMs = d12;
  m_perf.lastNarrowphaseMs = d23;
  m_perf.lastResolveMs = d34;
  m_perf.lastSyncMs = d45 + d56; // Combine sync phases
  m_perf.lastTotalMs = d06;
  m_perf.lastPairs = pairCount;
  m_perf.lastCollisions = collisionCount;
  m_perf.bodyCount = bodyCount;

  m_perf.updateAverage(m_perf.lastTotalMs);
  m_perf.frames += 1;

  // Update threading stats
  if (wasThreaded) {
    m_lastWasThreaded.store(true, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(batchCount, std::memory_order_relaxed);
    m_lastOptimalWorkerCount.store(optimalWorkers, std::memory_order_relaxed);
    m_lastAvailableWorkers.store(availableWorkers, std::memory_order_relaxed);
    m_lastCollisionBudget.store(budget, std::memory_order_relaxed);
  } else {
    m_lastWasThreaded.store(false, std::memory_order_relaxed);
    m_lastThreadBatchCount.store(1, std::memory_order_relaxed);
    m_lastOptimalWorkerCount.store(0, std::memory_order_relaxed);
    m_lastAvailableWorkers.store(0, std::memory_order_relaxed);
    m_lastCollisionBudget.store(0, std::memory_order_relaxed);
  }

  // Performance warnings
  if (m_perf.lastTotalMs > 5.0) {
    COLLISION_WARN("SOA Slow frame: totalMs=" + std::to_string(m_perf.lastTotalMs) +
                   ", syncMs=" + std::to_string(d01) +
                   ", broadphaseMs=" + std::to_string(d12) +
                   ", narrowphaseMs=" + std::to_string(d23) +
                   ", pairs=" + std::to_string(pairCount) +
                   ", collisions=" + std::to_string(collisionCount));
  }

  // Periodic statistics (every 300 frames like AIManager)
  if (m_perf.frames % 300 == 0 && bodyCount > 0) {
    if (wasThreaded) {
      COLLISION_DEBUG("SOA Collision Summary - Bodies: " + std::to_string(bodyCount) +
                      " (" + std::to_string(activeDynamicBodies) + " dynamic)" +
                      ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                      ", Broadphase: " + std::to_string(d12) + "ms" +
                      ", Narrowphase: " + std::to_string(d23) + "ms" +
                      ", Last Pairs: " + std::to_string(pairCount) +
                      ", Last Collisions: " + std::to_string(collisionCount) +
                      " [Threaded: " + std::to_string(optimalWorkers) + "/" +
                      std::to_string(availableWorkers) + " workers, Budget: " +
                      std::to_string(budget) + ", Batches: " +
                      std::to_string(batchCount) + "]");
    } else {
      COLLISION_DEBUG("SOA Collision Summary - Bodies: " + std::to_string(bodyCount) +
                      " (" + std::to_string(activeDynamicBodies) + " dynamic)" +
                      ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                      ", Broadphase: " + std::to_string(d12) + "ms" +
                      ", Narrowphase: " + std::to_string(d23) + "ms" +
                      ", Last Pairs: " + std::to_string(pairCount) +
                      ", Last Collisions: " + std::to_string(collisionCount) +
                      " [Single-threaded]");
    }
  }
}
