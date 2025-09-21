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
#include "utils/Camera.hpp"
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
  m_storage.clear();
  subscribeWorldEvents();
  COLLISION_INFO("STORAGE LIFECYCLE: init() cleared SOA storage and spatial hash");
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

  COLLISION_INFO("STORAGE LIFECYCLE: clean() clearing " +
                 std::to_string(m_storage.size()) + " SOA bodies");

  // Legacy storage removed - using SOA only

  // Clean SOA storage
  m_storage.clear();
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

  // Check if world is active - if so, let world unloading handle static collision cleanup
  const auto& worldManager = WorldManager::Instance();
  bool hasActiveWorld = worldManager.isInitialized() && worldManager.hasActiveWorld();

  if (hasActiveWorld) {
    // Only clear dynamic collision bodies - static bodies will be cleared by world unloading
    std::vector<EntityID> dynamicBodies;
    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto& hot = m_storage.hotData[i];
      if (hot.active && static_cast<BodyType>(hot.bodyType) != BodyType::STATIC) {
        dynamicBodies.push_back(m_storage.entityIds[i]);
      }
    }

    for (EntityID id : dynamicBodies) {
      removeCollisionBodySOA(id);
    }

    COLLISION_INFO("STORAGE LIFECYCLE: prepareForStateTransition() clearing " +
                   std::to_string(dynamicBodies.size()) + " dynamic bodies (keeping static for world unloading)");

    // Only clear dynamic spatial hash
    m_dynamicSpatialHash.clear();
  } else {
    // No active world - clear all collision bodies
    size_t soaBodyCount = m_storage.size();
    COLLISION_INFO("STORAGE LIFECYCLE: prepareForStateTransition() clearing " +
                   std::to_string(soaBodyCount) + " SOA bodies");
    m_storage.clear();

    // Clear spatial hashes completely
    m_staticSpatialHash.clear();
    m_dynamicSpatialHash.clear();
  }

  // Clear caches to prevent dangling references to deleted bodies
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

  size_t finalBodyCount = m_storage.size();
  COLLISION_INFO("CollisionManager state transition complete - " + std::to_string(finalBodyCount) + " bodies remaining");
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












EntityID CollisionManager::createTriggerArea(const AABB &aabb,
                                             HammerEngine::TriggerTag tag,
                                             uint32_t layerMask,
                                             uint32_t collideMask) {
  EntityID id = HammerEngine::UniqueID::generate();
  Vector2D center(aabb.center.getX(), aabb.center.getY());
  Vector2D halfSize(aabb.halfSize.getX(), aabb.halfSize.getY());
  size_t index = addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                                   layerMask, collideMask);
  // Set trigger properties directly in SOA storage
  m_storage.hotData[index].isTrigger = true;
  m_storage.hotData[index].triggerTag = static_cast<uint8_t>(tag);
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
      // Check if this water trigger already exists in SOA storage
      if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
        Vector2D center(aabb.center.getX(), aabb.center.getY());
        Vector2D halfSize(aabb.halfSize.getX(), aabb.halfSize.getY());
        size_t index = addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                                         CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
        // Set trigger properties directly in SOA storage
        m_storage.hotData[index].isTrigger = true;
        m_storage.hotData[index].triggerTag = static_cast<uint8_t>(tag);
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

          // Check if this static body already exists in SOA storage
          if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
            Vector2D center(aabb.center.getX(), aabb.center.getY());
            Vector2D halfSize(aabb.halfSize.getX(), aabb.halfSize.getY());
            addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                               CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
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


bool CollisionManager::overlaps(EntityID a, EntityID b) const {
  size_t indexA, indexB;
  if (!getCollisionBodySOA(a, indexA) || !getCollisionBodySOA(b, indexB))
    return false;

  AABB aabbA = m_storage.computeAABB(indexA);
  AABB aabbB = m_storage.computeAABB(indexB);
  return aabbA.intersects(aabbB);
}

void CollisionManager::queryArea(const AABB &area,
                                 std::vector<EntityID> &out) const {
  // Query SOA storage for bodies that intersect with the area
  out.clear();

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    AABB bodyAABB = m_storage.computeAABB(i);
    if (bodyAABB.intersects(area)) {
      out.push_back(m_storage.entityIds[i]);
    }
  }
}

bool CollisionManager::getBodyCenter(EntityID id, Vector2D &outCenter) const {
  size_t index;
  if (!getCollisionBodySOA(id, index))
    return false;
  outCenter = m_storage.hotData[index].position;
  return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
  size_t index;
  if (!getCollisionBodySOA(id, index))
    return false;
  return static_cast<BodyType>(m_storage.hotData[index].bodyType) == BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
  size_t index;
  if (!getCollisionBodySOA(id, index))
    return false;
  return static_cast<BodyType>(m_storage.hotData[index].bodyType) == BodyType::KINEMATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
  size_t index;
  if (!getCollisionBodySOA(id, index))
    return false;
  return m_storage.hotData[index].isTrigger;
}




void CollisionManager::update(float dt) {
  (void)dt;
  if (!m_initialized || m_isShutdown)
    return;

  // SOA collision system only
  updateSOA(dt);
  return;

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

  // Count bodies by layer using SOA storage
  std::map<uint32_t, size_t> layerCounts;
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto &hot = m_storage.hotData[i];
    if (hot.active) {
      layerCounts[hot.layers]++;
    }
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
  return std::count_if(m_storage.hotData.begin(), m_storage.hotData.end(), [](const auto &hot) {
    return hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC;
  });
}

size_t CollisionManager::getKinematicBodyCount() const {
  return std::count_if(m_storage.hotData.begin(), m_storage.hotData.end(), [](const auto &hot) {
    return hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC;
  });
}

void CollisionManager::rebuildStaticFromWorld() {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return;
  // Remove any existing STATIC world bodies from SOA storage
  std::vector<EntityID> toRemove;
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      toRemove.push_back(m_storage.entityIds[i]);
    }
  }
  for (auto id : toRemove) {
    removeCollisionBodySOA(id);
  }

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
    // Rebuild static spatial hash since world changed
    rebuildStaticSpatialHash();
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

    // Update water trigger for this tile using SOA system
    EntityID trigId = (static_cast<EntityID>(1ull) << 61) |
                      (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
                      static_cast<EntityID>(static_cast<uint32_t>(x));
    removeCollisionBodySOA(trigId);
    if (tile.isWater) {
      float cx = x * tileSize + tileSize * 0.5f;
      float cy = y * tileSize + tileSize * 0.5f;
      Vector2D center(cx, cy);
      Vector2D halfSize(tileSize * 0.5f, tileSize * 0.5f);
      size_t index = addCollisionBodySOA(trigId, center, halfSize, BodyType::STATIC,
                                       CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
      // Set trigger properties directly in SOA storage
      m_storage.hotData[index].isTrigger = true;
      m_storage.hotData[index].triggerTag = static_cast<uint8_t>(HammerEngine::TriggerTag::Water);
    }

    // Update solid obstacle collision body for this tile (BUILDING only)
    // Remove old per-tile collision body (legacy)
    EntityID oldObstacleId =
        (static_cast<EntityID>(2ull) << 61) |
        (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
        static_cast<EntityID>(static_cast<uint32_t>(x));
    removeCollisionBodySOA(oldObstacleId);

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
        removeCollisionBodySOA(
            buildingId); // Remove existing building collision body if any

        float cx = x * tileSize + tileSize;    // Center at 1 tile offset
        float cy = y * tileSize + tileSize;    // Center at 1 tile offset
        Vector2D center(cx, cy);
        Vector2D halfSize(tileSize, tileSize);
        addCollisionBodySOA(buildingId, center, halfSize, BodyType::STATIC,
                           CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
      }
    } else if (tile.obstacleType != ObstacleType::BUILDING &&
               tile.buildingId > 0) {
      // Tile was a building but no longer is - remove the building collision
      // body
      EntityID buildingId =
          (static_cast<EntityID>(3ull) << 61) |
          static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
      removeCollisionBodySOA(buildingId);
    }

    // ROCK and TREE movement penalties are handled by pathfinding system
    // No collision triggers needed for these obstacle types

    // Rebuild static spatial hash since tile changed
    rebuildStaticSpatialHash();
  }
}

// Removed invalidateStaticCache() - replaced with rebuildStaticSpatialHash()

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
          for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
            const auto& hot = m_storage.hotData[i];
            if (hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
              toRemove.push_back(m_storage.entityIds[i]);
            }
          }
          for (auto id : toRemove) {
            removeCollisionBodySOA(id);
          }
          COLLISION_INFO("World unloaded - removed static colliders: " +
                         std::to_string(toRemove.size()));
          // Rebuild static spatial hash since static bodies were removed
          rebuildStaticSpatialHash();
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
      hot.aabbDirty = 1;
      hot.layers = layer;
      hot.collidesWith = collidesWith;
      hot.bodyType = static_cast<uint8_t>(type);
      hot.active = true;

      // Update cold data
      if (index < m_storage.coldData.size()) {
        m_storage.coldData[index].fullAABB = AABB(position.getX(), position.getY(),
                                                  halfSize.getX(), halfSize.getY());
      }

      // Removed per-entity logging to reduce spam
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
  hotData.aabbDirty = 1;
  hotData.layers = layer;
  hotData.collidesWith = collidesWith;
  hotData.bodyType = static_cast<uint8_t>(type);
  hotData.triggerTag = static_cast<uint8_t>(HammerEngine::TriggerTag::None);
  hotData.active = true;
  hotData.isTrigger = false;
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

  // Removed per-entity logging to reduce spam - entity count is included in periodic summary

  // Fire collision obstacle changed event for static bodies
  if (type == BodyType::STATIC) {
    float radius = std::max(halfSize.getX(), halfSize.getY()) + 16.0f; // Add safety margin
    std::string description = "Static obstacle added at (" +
                              std::to_string(position.getX()) + ", " +
                              std::to_string(position.getY()) + ")";
    EventManager::Instance().triggerCollisionObstacleChanged(position, radius, description,
                                                            EventManager::DispatchMode::Deferred);

    // OPTIMIZATION: Only rebuild static spatial hash when static objects change
    rebuildStaticSpatialHash();
  }

  return newIndex;
}

void CollisionManager::removeCollisionBodySOA(EntityID id) {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end()) {
    return; // Entity not found
  }

  size_t indexToRemove = it->second;
  size_t lastIndex = m_storage.size() - 1;

  // Fire collision obstacle changed event for static bodies before removal
  bool wasStatic = false;
  if (indexToRemove < m_storage.size()) {
    const auto& hot = m_storage.hotData[indexToRemove];
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      wasStatic = true;
      float radius = std::max(hot.halfSize.getX(), hot.halfSize.getY()) + 16.0f;
      std::string description = "Static obstacle removed from (" +
                                std::to_string(hot.position.getX()) + ", " +
                                std::to_string(hot.position.getY()) + ")";
      EventManager::Instance().triggerCollisionObstacleChanged(hot.position, radius, description,
                                                              EventManager::DispatchMode::Deferred);
    }
  }

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

  // OPTIMIZATION: Only rebuild static spatial hash when static objects are removed
  if (wasStatic) {
    rebuildStaticSpatialHash();
  }

  // Removed per-entity logging to reduce spam
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
    hot.aabbDirty = 1;

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

void CollisionManager::updateCollisionBodySizeSOA(EntityID id, const Vector2D& newHalfSize) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].halfSize = newHalfSize;
    m_storage.hotData[index].aabbDirty = 1;
    COLLISION_DEBUG("Updated SOA body size for entity " + std::to_string(id) +
                    " to (" + std::to_string(newHalfSize.getX()) + ", " +
                    std::to_string(newHalfSize.getY()) + ")");
  }
}

void CollisionManager::attachEntity(EntityID id, EntityPtr entity) {
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.coldData.size()) {
      m_storage.coldData[index].entityWeak = entity;
    }
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

  // Prepared collision buffers
}


// Optimized version of buildActiveIndicesSOA - O(N) instead of O(N²)
void CollisionManager::buildActiveIndicesSOA(const CullingArea& cullingArea) {
  // Build indices of active bodies within culling area
  auto& pools = m_collisionPool;
  pools.activeIndices.clear();
  pools.dynamicIndices.clear();
  pools.staticIndices.clear();

  // OPTIMIZATION: Simple linear pass - let spatial hash handle proximity efficiently
  // The previous O(N²) "smart culling" was slower than just processing all active bodies

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    // Optional: Apply culling area bounds if specified
    if (cullingArea.minX != cullingArea.maxX || cullingArea.minY != cullingArea.maxY) {
      if (!cullingArea.contains(hot.position.getX(), hot.position.getY())) {
        continue; // Skip bodies outside culling area
      }
    }

    BodyType bodyType = static_cast<BodyType>(hot.bodyType);

    pools.activeIndices.push_back(i);

    // Categorize by body type for optimized processing
    if (bodyType == BodyType::STATIC) {
      pools.staticIndices.push_back(i);
    } else {
      pools.dynamicIndices.push_back(i);
    }
  }
}

// Internal helper methods moved to private section

// ========== NEW SOA-BASED BROADPHASE IMPLEMENTATION ==========

void CollisionManager::broadphaseSOA(std::vector<std::pair<size_t, size_t>>& indexPairs) const {
  indexPairs.clear();

  // Build active indices for this frame (with configurable culling)
  CullingArea cullingArea = createDefaultCullingArea();
  const_cast<CollisionManager*>(this)->buildActiveIndicesSOA(cullingArea);

  const auto& pools = m_collisionPool;
  const auto& dynamicIndices = pools.dynamicIndices;

  // MASSIVE OPTIMIZATION: Use sorted vector instead of unordered_set for pair deduplication
  // This is much faster for 10K+ entities
  std::vector<uint64_t> dynamicPairs;
  dynamicPairs.reserve(dynamicIndices.size() * 4); // More conservative estimate

  // Reserve space for final pairs
  indexPairs.reserve(dynamicIndices.size() * 6);

  // OPTIMIZATION: Process only dynamic bodies (static never initiate collisions)
  for (size_t i = 0; i < dynamicIndices.size(); ++i) {
    size_t dynamicIdx = dynamicIndices[i];
    const auto& dynamicHot = m_storage.hotData[dynamicIdx];
    if (!dynamicHot.active) continue;

    AABB dynamicAABB = m_storage.computeAABB(dynamicIdx);

    // 1. Dynamic-vs-dynamic collisions using spatial hash (eliminates N² complexity)
    std::vector<size_t> dynamicCandidates;
    m_dynamicSpatialHash.queryBroadphase(dynamicIdx, dynamicAABB, dynamicCandidates);

    for (size_t candidateIdx : dynamicCandidates) {
      if (candidateIdx >= m_storage.hotData.size()) continue;
      if (candidateIdx == dynamicIdx) continue; // Skip self
      if (candidateIdx <= dynamicIdx) continue; // Avoid duplicate pairs (only check higher indices)

      const auto& candidateHot = m_storage.hotData[candidateIdx];
      if (!candidateHot.active) continue;

      // Check collision masks
      if ((dynamicHot.collidesWith & candidateHot.layers) == 0) continue;

      // Add pair directly (spatial hash already filters by proximity)
      indexPairs.emplace_back(dynamicIdx, candidateIdx);
    }

    // 2. Query static spatial hash for dynamic-vs-static collisions
    std::vector<size_t> staticCandidates;
    m_staticSpatialHash.queryRegion(dynamicAABB, staticCandidates);

    for (size_t staticIdx : staticCandidates) {
      if (staticIdx >= m_storage.hotData.size()) continue;

      const auto& staticHot = m_storage.hotData[staticIdx];
      if (!staticHot.active) continue;

      // Static objects already filtered by culling system - no additional distance check needed

      // Check collision masks
      if ((dynamicHot.collidesWith & staticHot.layers) == 0) continue;

      indexPairs.emplace_back(dynamicIdx, staticIdx);
    }
  }

  // Removed per-frame logging - pair count is included in periodic summary
}

bool CollisionManager::broadphaseSOAThreaded(std::vector<std::pair<size_t, size_t>>& indexPairs,
                                             ThreadingStats& stats) {
  indexPairs.clear();

  if (!HammerEngine::ThreadSystem::Exists()) {
    broadphaseSOA(indexPairs);
    return false;
  }

  // Build active indices (with configurable culling)
  CullingArea cullingArea = createDefaultCullingArea();
  const_cast<CollisionManager*>(this)->buildActiveIndicesSOA(cullingArea);

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

    // Get cached AABB bounds directly (more efficient than creating AABB objects)
    float minXA, minYA, maxXA, maxYA;
    float minXB, minYB, maxXB, maxYB;
    m_storage.getCachedAABBBounds(aIdx, minXA, minYA, maxXA, maxYA);
    m_storage.getCachedAABBBounds(bIdx, minXB, minYB, maxXB, maxYB);

    // AABB intersection test using cached bounds (avoids object creation)
    if (maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA) continue;

    // Compute collision details using cached bounds
    float dxLeft = maxXB - minXA;   // B.right - A.left
    float dxRight = maxXA - minXB;  // A.right - B.left
    float dyTop = maxYB - minYA;    // B.bottom - A.top
    float dyBottom = maxYA - minYB; // A.bottom - B.top

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

  // Removed per-frame logging - collision count is included in periodic summary
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

            // Get cached AABB bounds directly (more efficient than creating AABB objects)
            float minXA, minYA, maxXA, maxYA;
            float minXB, minYB, maxXB, maxYB;
            m_storage.getCachedAABBBounds(aIdx, minXA, minYA, maxXA, maxYA);
            m_storage.getCachedAABBBounds(bIdx, minXB, minYB, maxXB, maxYB);

            // AABB intersection test using cached bounds
            if (maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA) continue;

            // Compute penetration and normal using cached bounds
            float dxLeft = maxXB - minXA;   // B.right - A.left
            float dxRight = maxXA - minXB;  // A.right - B.left
            float dyTop = maxYB - minYA;    // B.bottom - A.top
            float dyBottom = maxYA - minYB; // A.bottom - B.top

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

  // Pure SOA system - no legacy compatibility

  // Check storage state at start of update
  size_t bodyCount = m_storage.size();
  // Removed per-frame logging - details are included in periodic summary at end of update

  // Prepare collision processing for this frame
  prepareCollisionBuffers(bodyCount); // Prepare collision buffers

  // Determine threading strategy using WorkerBudget system
  size_t threadingThresholdValue =
      std::max<size_t>(1, m_threadingThreshold.load(std::memory_order_acquire));
  bool threadingEnabled = m_useThreading.load(std::memory_order_acquire) &&
                          HammerEngine::ThreadSystem::Exists();

  ThreadingStats summaryStats{};
  bool summaryThreaded = false;

  // Count active dynamic bodies for threading decisions (with configurable culling)
  CullingArea cullingArea = createDefaultCullingArea();

  // PERFORMANCE OPTIMIZATION: Track culling metrics
  auto cullingStart = clock::now();
  size_t totalBodiesBefore = bodyCount;
  buildActiveIndicesSOA(cullingArea);
  auto cullingEnd = clock::now();

  // CRITICAL FIX: Sync spatial hashes AFTER culling, only for active bodies
  syncSpatialHashesWithActiveIndices();

  double cullingMs = std::chrono::duration<double, std::milli>(cullingEnd - cullingStart).count();

  size_t activeDynamicBodies = m_collisionPool.dynamicIndices.size();
  size_t activeBodies = m_collisionPool.activeIndices.size();
  size_t dynamicBodiesCulled = 0;
  size_t staticBodiesCulled = 0;

  // Calculate culling effectiveness
  if (totalBodiesBefore > activeBodies) {
    // Estimate dynamic vs static culling based on body type distribution
    size_t totalCulled = totalBodiesBefore - activeBodies;
    size_t totalDynamic = activeDynamicBodies;

    // Rough estimate: assume proportional culling
    if (totalBodiesBefore > 0 && activeBodies > 0) {
      double dynamicRatio = static_cast<double>(totalDynamic) / activeBodies;
      dynamicBodiesCulled = static_cast<size_t>(totalCulled * dynamicRatio);
      staticBodiesCulled = totalCulled - dynamicBodiesCulled;
    }
  }

  // Object pool for SOA collision processing
  std::vector<std::pair<size_t, size_t>> indexPairs;

  // BROADPHASE: Generate collision pairs using hierarchical spatial hash
  auto t1 = clock::now();
  bool broadphaseUsedThreading = false;
  // Use active dynamic bodies for threading decision - they drive the workload
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

  // PERFORMANCE METRICS: Track timing and threading stats with optimization metrics
  size_t threadCount = summaryThreaded ? summaryStats.optimalWorkers : 0;
  updatePerformanceMetricsSOA(t0, t1, t2, t3, t4, t5, t6, summaryThreaded,
                               summaryStats.optimalWorkers, summaryStats.availableWorkers,
                               summaryStats.budget, summaryStats.batchCount,
                               bodyCount, activeDynamicBodies, pairCount, m_collisionPool.collisionBuffer.size(),
                               activeBodies, dynamicBodiesCulled, staticBodiesCulled, threadCount, cullingMs);

  // SOA Update complete (reduced logging)
}


// ========== SOA UPDATE HELPER METHODS ==========

// PERFORMANCE CRITICAL: Only sync spatial hash for active bodies after culling
void CollisionManager::syncSpatialHashesWithActiveIndices() {
  const auto& pools = m_collisionPool;

  // Clear and rebuild dynamic spatial hash with only active dynamic bodies
  m_dynamicSpatialHash.clear();

  for (size_t idx : pools.dynamicIndices) {
    if (idx >= m_storage.hotData.size()) continue;

    const auto& hot = m_storage.hotData[idx];
    if (!hot.active) continue;

    AABB aabb = m_storage.computeAABB(idx);
    m_dynamicSpatialHash.insert(idx, aabb);
  }

  // Static hash is managed separately via rebuildStaticSpatialHash()
}

// Legacy function - kept for compatibility but should not be used in hot path
void CollisionManager::syncSpatialHashesWithSOA() {
  // OPTIMIZED SPATIAL HASH UPDATE: Update positions incrementally instead of full rebuild

  static size_t lastBodyCount = 0;
  size_t currentBodyCount = m_storage.hotData.size();

  // Check if we need a full rebuild (body count changed)
  bool needsFullRebuild = (currentBodyCount != lastBodyCount);

  if (needsFullRebuild) {
    // Full rebuild when bodies are added/removed
    m_dynamicSpatialHash.clear();

    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto& hot = m_storage.hotData[i];
      if (!hot.active) continue;

      BodyType bodyType = static_cast<BodyType>(hot.bodyType);
      if (bodyType == BodyType::DYNAMIC || bodyType == BodyType::KINEMATIC) {
        AABB aabb = m_storage.computeAABB(i);
        m_dynamicSpatialHash.insert(i, aabb);
      }
    }

    lastBodyCount = currentBodyCount;
  } else {
    // Incremental update: Only update bodies with dirty AABBs (moved positions)
    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto& hot = m_storage.hotData[i];
      if (!hot.active || !hot.aabbDirty) continue;

      BodyType bodyType = static_cast<BodyType>(hot.bodyType);
      if (bodyType == BodyType::DYNAMIC || bodyType == BodyType::KINEMATIC) {
        // Remove old position and insert new position
        m_dynamicSpatialHash.remove(i);
        AABB aabb = m_storage.computeAABB(i);
        m_dynamicSpatialHash.insert(i, aabb);
      }
    }
  }

  // Static hash is managed separately via rebuildStaticSpatialHash()
}

void CollisionManager::rebuildStaticSpatialHash() {
  // OPTIMIZATION: Only called when static objects are added/removed
  m_staticSpatialHash.clear();

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    BodyType bodyType = static_cast<BodyType>(hot.bodyType);
    if (bodyType == BodyType::STATIC) {
      AABB aabb = m_storage.computeAABB(i);
      m_staticSpatialHash.insert(i, aabb);
    }
  }
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

    // Allow all body types to sync collision-resolved positions

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
    size_t collisionCount,
    size_t activeBodies,
    size_t dynamicBodiesCulled,
    size_t staticBodiesCulled,
    size_t threadCount,
    double cullingMs) {

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

  // PERFORMANCE OPTIMIZATION METRICS: Track optimization effectiveness
  m_perf.lastActiveBodies = activeBodies > 0 ? activeBodies : bodyCount;
  m_perf.lastDynamicBodiesCulled = dynamicBodiesCulled;
  m_perf.lastStaticBodiesCulled = staticBodiesCulled;
  m_perf.lastFrameWasThreaded = wasThreaded;
  m_perf.lastThreadCount = threadCount > 0 ? threadCount : (wasThreaded ? optimalWorkers : 0);
  m_perf.lastCullingMs = cullingMs;

  m_perf.updateAverage(m_perf.lastTotalMs);
  m_perf.updateBroadphaseAverage(d12);
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

  // Performance warnings (throttled to reduce spam during benchmarks)
  if (m_perf.lastTotalMs > 5.0 && m_perf.frames % 60 == 0) { // Only log every 60 frames for slow performance
    COLLISION_WARN("SOA Slow frame: totalMs=" + std::to_string(m_perf.lastTotalMs) +
                   ", syncMs=" + std::to_string(d01) +
                   ", broadphaseMs=" + std::to_string(d12) +
                   ", narrowphaseMs=" + std::to_string(d23) +
                   ", pairs=" + std::to_string(pairCount) +
                   ", collisions=" + std::to_string(collisionCount));
  }

  // Periodic statistics (every 300 frames like AIManager)
  if (m_perf.frames % 300 == 0 && bodyCount > 0) {
    // PERFORMANCE OPTIMIZATION REPORTING: Show optimization effectiveness
    std::string optimizationStats = " [Optimizations: Active=" + std::to_string(m_perf.getActiveBodiesRate()) + "%";
    if (dynamicBodiesCulled > 0) {
      optimizationStats += ", DynCulled=" + std::to_string(m_perf.getDynamicCullingRate()) + "%";
    }
    if (staticBodiesCulled > 0) {
      optimizationStats += ", StaticCulled=" + std::to_string(m_perf.getStaticCullingRate()) + "%";
    }
    if (cullingMs > 0.0) {
      optimizationStats += ", CullingMs=" + std::to_string(cullingMs);
    }
    optimizationStats += "]";

    if (wasThreaded) {
      COLLISION_DEBUG("SOA Collision Summary - Bodies: " + std::to_string(bodyCount) +
                      " (" + std::to_string(activeDynamicBodies) + " dynamic)" +
                      ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                      ", Avg Broadphase: " + std::to_string(m_perf.avgBroadphaseMs) + "ms" +
                      ", Current Broadphase: " + std::to_string(d12) + "ms" +
                      ", Narrowphase: " + std::to_string(d23) + "ms" +
                      ", Last Pairs: " + std::to_string(pairCount) +
                      ", Last Collisions: " + std::to_string(collisionCount) +
                      " [Threaded: " + std::to_string(optimalWorkers) + "/" +
                      std::to_string(availableWorkers) + " workers, Budget: " +
                      std::to_string(budget) + ", Batches: " +
                      std::to_string(batchCount) + "]" + optimizationStats);
    } else {
      COLLISION_DEBUG("SOA Collision Summary - Bodies: " + std::to_string(bodyCount) +
                      " (" + std::to_string(activeDynamicBodies) + " dynamic)" +
                      ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                      ", Avg Broadphase: " + std::to_string(m_perf.avgBroadphaseMs) + "ms" +
                      ", Current Broadphase: " + std::to_string(d12) + "ms" +
                      ", Narrowphase: " + std::to_string(d23) + "ms" +
                      ", Last Pairs: " + std::to_string(pairCount) +
                      ", Last Collisions: " + std::to_string(collisionCount) +
                      " [Single-threaded]" + optimizationStats);
    }
  }
}

void CollisionManager::updateKinematicBatchSOA(const std::vector<KinematicUpdate>& updates) {
  if (updates.empty()) return;

  // Batch update all kinematic bodies in SOA storage
  size_t validUpdates = 0;
  for (const auto& update : updates) {
    size_t index;
    if (getCollisionBodySOA(update.id, index)) {
      auto& hot = m_storage.hotData[index];
      if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC) {
        hot.position = update.position;
        hot.velocity = update.velocity;
        hot.aabbDirty = 1;
        hot.active = true; // Ensure body stays enabled
        validUpdates++;
      }
    }
  }

  if (m_verboseLogs && !updates.empty()) {
    COLLISION_DEBUG("updateKinematicBatchSOA: processed " +
                    std::to_string(validUpdates) + "/" +
                    std::to_string(updates.size()) + " kinematic updates");
  }
}

// ========== SOA BODY MANAGEMENT METHODS ==========

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].active = enabled ? 1 : 0;
    // Removed per-entity logging to reduce spam
  }
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    auto& hot = m_storage.hotData[index];
    hot.layers = layerMask;
    hot.collidesWith = collideMask;
    // Removed per-entity logging to reduce spam
  }
}

void CollisionManager::setVelocity(EntityID id, const Vector2D& velocity) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].velocity = velocity;
    // Removed per-entity logging to reduce spam
  }
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].isTrigger = isTrigger ? 1 : 0;
    // Removed per-entity logging to reduce spam
  }
}

CollisionManager::CullingArea CollisionManager::createDefaultCullingArea() const {
  // BENCHMARK DETECTION: If we have a lot of bodies (>1000), this is likely a benchmark
  // For benchmarks, disable culling entirely to test full system performance
  if (m_storage.size() > 1000) {
    CullingArea area;
    area.minX = 0.0f;
    area.minY = 0.0f;
    area.maxX = 0.0f;
    area.maxY = 0.0f;  // Signals buildActiveIndicesSOA to skip culling
    return area;
  }

  // Normal gameplay: Find the player position (EntityID 1 by convention)
  Vector2D playerPos(0.0f, 0.0f);
  bool playerFound = false;

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    if (m_storage.entityIds[i] == 1) { // Player is typically EntityID 1
      const auto& hot = m_storage.hotData[i];
      if (hot.active) {
        playerPos = hot.position;
        playerFound = true;
        break;
      }
    }
  }

  // If no player found, fall back to first dynamic entity
  if (!playerFound) {
    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto& hot = m_storage.hotData[i];
      if (!hot.active) continue;

      BodyType bodyType = static_cast<BodyType>(hot.bodyType);
      if (bodyType == BodyType::DYNAMIC || bodyType == BodyType::KINEMATIC) {
        playerPos = hot.position;
        playerFound = true;
        break;
      }
    }
  }

  // Create player-centered culling area for normal gameplay
  CullingArea area;
  if (playerFound) {
    area.minX = playerPos.getX() - COLLISION_CULLING_BUFFER;
    area.minY = playerPos.getY() - COLLISION_CULLING_BUFFER;
    area.maxX = playerPos.getX() + COLLISION_CULLING_BUFFER;
    area.maxY = playerPos.getY() + COLLISION_CULLING_BUFFER;
  } else {
    // Fallback: no player found, disable culling entirely
    area.minX = 0.0f;
    area.minY = 0.0f;
    area.maxX = 0.0f;
    area.maxY = 0.0f;
  }

  return area;
}

