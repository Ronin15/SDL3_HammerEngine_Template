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
  return true;
}

void CollisionManager::clean() {
  if (!m_initialized || m_isShutdown)
    return;
  m_isShutdown = true;

  COLLISION_INFO("STORAGE LIFECYCLE: clean() clearing " +
                 std::to_string(m_storage.size()) + " SOA bodies");


  // Clean SOA storage
  m_storage.clear();
  m_callbacks.clear();
  m_initialized = false;
  COLLISION_INFO("Cleaned and shut down SOA storage");
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
  // Log removed for performance
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

  // Log removed for performance
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
  size_t dynamicBodies = getDynamicBodyCount();

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
  return std::count_if(m_storage.hotData.begin(), m_storage.hotData.end(),
    [](const auto &hot) { return hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC; });
}

size_t CollisionManager::getKinematicBodyCount() const {
  return std::count_if(m_storage.hotData.begin(), m_storage.hotData.end(),
    [](const auto &hot) { return hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC; });
}

size_t CollisionManager::getDynamicBodyCount() const {
  return std::count_if(m_storage.hotData.begin(), m_storage.hotData.end(),
    [](const auto &hot) { return hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::DYNAMIC; });
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
    // Force immediate static spatial hash rebuild for world changes
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

    // PERFORMANCE: Selectively invalidate only the coarse cell containing this tile
    // This prevents invalidating the entire cache when only one tile changes
    float worldX = x * tileSize + tileSize * 0.5f;
    float worldY = y * tileSize + tileSize * 0.5f;
    AABB tileAABB(worldX, worldY, tileSize * 0.5f, tileSize * 0.5f);
    auto changedCoarseCell = m_staticSpatialHash.getCoarseCoord(tileAABB);

    // Invalidate only this specific coarse region cache
    auto cacheIt = m_coarseRegionStaticCache.find(changedCoarseCell);
    if (cacheIt != m_coarseRegionStaticCache.end()) {
      cacheIt->second.valid = false;
    }

    // Mark static hash as needing rebuild since tile changed
    m_staticHashDirty = true;
  }
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
          // Clear static spatial hash since all static bodies were removed
          m_staticSpatialHash.clear();
          m_staticHashDirty = false;
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

    // Mark static hash as needing rebuild (batched for performance)
    m_staticHashDirty = true;
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

  // Mark static hash as needing rebuild when static objects are removed
  if (wasStatic) {
    m_staticHashDirty = true;
  }

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
    // Log removed for performance
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
void CollisionManager::buildActiveIndicesSOA(const CullingArea& cullingArea) const {
  // Build indices of active bodies within culling area
  auto& pools = m_collisionPool;

  // Store current culling area for use in broadphase queries
  m_currentCullingArea = cullingArea;
  pools.activeIndices.clear();
  pools.movableIndices.clear();
  pools.staticIndices.clear();

  // Linear pass - spatial hash handles proximity efficiently

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
      // movableIndices contains both DYNAMIC and KINEMATIC bodies
      // They are grouped together for broadphase collision detection
      pools.movableIndices.push_back(i);
    }
  }
}

// Internal helper methods moved to private section

// ========== NEW SOA-BASED BROADPHASE IMPLEMENTATION ==========

void CollisionManager::broadphaseSOA(std::vector<std::pair<size_t, size_t>>& indexPairs) {
  indexPairs.clear();

  const auto& pools = m_collisionPool;
  const auto& movableIndices = pools.movableIndices;

  // Reserve space for pairs
  indexPairs.reserve(movableIndices.size() * 4);

  // Process only movable bodies (static never initiate collisions)
  for (size_t dynamicIdx : movableIndices) {
    const auto& dynamicHot = m_storage.hotData[dynamicIdx];
    if (!dynamicHot.active) continue;

    // PERFORMANCE: Use cached AABB bounds directly instead of recomputing
    // Cached bounds are already updated in updateCachedAABB()
    m_storage.updateCachedAABB(dynamicIdx); // Ensure cache is fresh
    AABB dynamicAABB(
      (dynamicHot.aabbMinX + dynamicHot.aabbMaxX) * 0.5f,  // centerX
      (dynamicHot.aabbMinY + dynamicHot.aabbMaxY) * 0.5f,  // centerY
      (dynamicHot.aabbMaxX - dynamicHot.aabbMinX) * 0.5f,  // halfWidth
      (dynamicHot.aabbMaxY - dynamicHot.aabbMinY) * 0.5f   // halfHeight
    );

    // In-place epsilon expansion - most efficient approach
    dynamicAABB.halfSize += Vector2D(SPATIAL_QUERY_EPSILON, SPATIAL_QUERY_EPSILON);

    // 1. Movable-vs-movable collisions
    auto& dynamicCandidates = getPooledVector();
    m_dynamicSpatialHash.queryRegion(dynamicAABB, dynamicCandidates);

    for (size_t candidateIdx : dynamicCandidates) {
      if (candidateIdx >= m_storage.hotData.size() || candidateIdx == dynamicIdx) continue;

      const auto& candidateHot = m_storage.hotData[candidateIdx];
      if (!candidateHot.active || (dynamicHot.collidesWith & candidateHot.layers) == 0) continue;

      // Ensure consistent ordering
      size_t a = std::min(dynamicIdx, candidateIdx);
      size_t b = std::max(dynamicIdx, candidateIdx);
      indexPairs.emplace_back(a, b);
    }

    // 2. Use pre-populated static collision cache for dynamic-vs-static collisions
    auto cacheIt = m_staticCollisionCache.find(dynamicIdx);
    if (cacheIt != m_staticCollisionCache.end() && cacheIt->second.valid) {
      const auto& staticCandidates = cacheIt->second.cachedStaticIndices;

      for (size_t staticIdx : staticCandidates) {
        if (staticIdx >= m_storage.hotData.size()) continue;

        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active || (dynamicHot.collidesWith & staticHot.layers) == 0) continue;

        indexPairs.emplace_back(dynamicIdx, staticIdx);
      }
    }

    // Return pooled vector
    returnPooledVector(dynamicCandidates);
  }
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
        entityA, entityB, normal, minPen, isTrigger, aIdx, bIdx
    });
  }

  // Removed per-frame logging - collision count is included in periodic summary
}

// ========== NEW SOA UPDATE METHOD ==========

void CollisionManager::updateSOA(float dt) {
  (void)dt;

  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();

  // Pure SOA system - no legacy compatibility

  // Check storage state at start of update
  size_t bodyCount = m_storage.size();

  // Prepare collision processing for this frame
  prepareCollisionBuffers(bodyCount); // Prepare collision buffers

  // Count active dynamic bodies (with configurable culling)
  CullingArea cullingArea = createDefaultCullingArea();

  // Rebuild static spatial hash if needed (batched from add/remove operations)
  if (m_staticHashDirty) {
    rebuildStaticSpatialHash();
    m_staticHashDirty = false;
  }

  // Track culling metrics
  auto cullingStart = clock::now();
  size_t totalBodiesBefore = bodyCount;
  buildActiveIndicesSOA(cullingArea);
  auto cullingEnd = clock::now();

  // Sync spatial hashes after culling, only for active bodies
  syncSpatialHashesWithActiveIndices();

  // Update static collision cache for all movable bodies
  updateStaticCollisionCacheForMovableBodies();

  double cullingMs = std::chrono::duration<double, std::milli>(cullingEnd - cullingStart).count();

  size_t activeMovableBodies = m_collisionPool.movableIndices.size();
  size_t activeBodies = m_collisionPool.activeIndices.size();
  size_t dynamicBodiesCulled = 0;
  size_t staticBodiesCulled = 0;
  if (totalBodiesBefore > activeBodies) {
    // Estimate dynamic vs static culling based on body type distribution
    size_t totalCulled = totalBodiesBefore - activeBodies;
    size_t totalMovable = activeMovableBodies;

    // Rough estimate: assume proportional culling
    if (totalBodiesBefore > 0 && activeBodies > 0) {
      double movableRatio = static_cast<double>(totalMovable) / activeBodies;
      dynamicBodiesCulled = static_cast<size_t>(totalCulled * movableRatio);
      staticBodiesCulled = totalCulled - dynamicBodiesCulled;
    }
  }

  // Object pool for SOA collision processing
  std::vector<std::pair<size_t, size_t>> indexPairs;

  // BROADPHASE: Generate collision pairs using spatial hash
  auto t1 = clock::now();
  broadphaseSOA(indexPairs);
  auto t2 = clock::now();

  // NARROWPHASE: Detailed collision detection and response calculation
  const size_t pairCount = indexPairs.size();
  narrowphaseSOA(indexPairs, m_collisionPool.collisionBuffer);
  auto t3 = clock::now();


  // RESOLUTION: Apply collision responses and update positions
  for (const auto& collision : m_collisionPool.collisionBuffer) {
    resolveSOA(collision);
    for (const auto& cb : m_callbacks) {
      cb(collision);
    }
  }
  auto t4 = clock::now();

  // Verbose logging removed for performance

  // SYNCHRONIZATION: Update entity positions and velocities from SOA storage
  syncEntitiesToSOA();
  auto t5 = clock::now();

  // TRIGGER PROCESSING: Handle trigger enter/exit events
  processTriggerEventsSOA();
  auto t6 = clock::now();

  // Track performance metrics
  updatePerformanceMetricsSOA(t0, t1, t2, t3, t4, t5, t6,
                               bodyCount, activeMovableBodies, pairCount, m_collisionPool.collisionBuffer.size(),
                               activeBodies, dynamicBodiesCulled, staticBodiesCulled, cullingMs);

}


// ========== SOA UPDATE HELPER METHODS ==========

// Sync spatial hash for active bodies after culling
void CollisionManager::syncSpatialHashesWithActiveIndices() {
  const auto& pools = m_collisionPool;

  // Clear and rebuild dynamic spatial hash with only active movable bodies
  m_dynamicSpatialHash.clear();

  for (size_t idx : pools.movableIndices) {
    if (idx >= m_storage.hotData.size()) continue;

    const auto& hot = m_storage.hotData[idx];
    if (!hot.active) continue;

    AABB aabb = m_storage.computeAABB(idx);
    m_dynamicSpatialHash.insert(idx, aabb);
  }

  // Static hash is managed separately via rebuildStaticSpatialHash()
}


void CollisionManager::rebuildStaticSpatialHash() {
  // Only called when static objects are added/removed
  m_staticSpatialHash.clear();

  // Clear static collision caches since static bodies have changed
  m_staticCollisionCache.clear();
  m_coarseRegionStaticCache.clear();  // Invalidate all coarse region caches
  m_bodyCoarseCell.clear();           // Reset body cell tracking

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

void CollisionManager::updateStaticCollisionCacheForMovableBodies() {
  // PERFORMANCE: Coarse-grid region-based caching instead of per-body caching
  // Bodies in the same 128×128 coarse cell share the same cached static query results

  const auto& movableIndices = m_collisionPool.movableIndices;

  for (size_t dynamicIdx : movableIndices) {
    if (dynamicIdx >= m_storage.hotData.size()) continue;

    const auto& hot = m_storage.hotData[dynamicIdx];
    if (!hot.active) continue;

    // Compute AABB and current coarse cell for this body
    AABB dynamicAABB = m_storage.computeAABB(dynamicIdx);
    auto currentCoarseCell = m_staticSpatialHash.getCoarseCoord(dynamicAABB);

    // Check if body has moved to a different coarse cell
    auto bodyCoarseCellIt = m_bodyCoarseCell.find(dynamicIdx);
    bool crossedBoundary = (bodyCoarseCellIt == m_bodyCoarseCell.end() ||
                            bodyCoarseCellIt->second.x != currentCoarseCell.x ||
                            bodyCoarseCellIt->second.y != currentCoarseCell.y);

    // Get or create cache entry for this region
    auto& regionCache = m_coarseRegionStaticCache[currentCoarseCell];

    // If body crossed coarse cell boundary OR region cache is invalid, query static hash
    if (crossedBoundary || !regionCache.valid) {
      if (!regionCache.valid) {
        m_cacheMisses++;

        // Expand AABB to cover entire coarse cell + margin for border cases
        AABB regionAABB = dynamicAABB;
        regionAABB.halfSize += Vector2D(SPATIAL_QUERY_EPSILON + 64.0f, SPATIAL_QUERY_EPSILON + 64.0f);

        // Query static spatial hash for this entire coarse region
        auto& staticCandidates = getPooledVector();
        m_staticSpatialHash.queryRegion(regionAABB, staticCandidates);

        // Cache result for entire region (all bodies in this cell will share it)
        regionCache.staticIndices = staticCandidates;
        regionCache.valid = true;
        returnPooledVector(staticCandidates);
      } else {
        m_cacheHits++;
      }

      // Update body's tracked coarse cell
      m_bodyCoarseCell[dynamicIdx] = currentCoarseCell;
    } else {
      // Body still in same coarse cell, cache is valid
      m_cacheHits++;
    }

    // Update old per-body cache for backward compatibility with broadphaseSOA()
    // This will be removed in phase 4 when we integrate directly
    auto& oldCache = m_staticCollisionCache[dynamicIdx];
    oldCache.cachedStaticIndices = regionCache.staticIndices;
    oldCache.valid = true;
    oldCache.lastPosition = hot.position;
  }
}

void CollisionManager::resolveSOA(const CollisionInfo& collision) {
  if (collision.trigger) return; // Triggers don't need position resolution

  // Use stored indices - NO MORE LINEAR LOOKUP!
  size_t indexA = collision.indexA;
  size_t indexB = collision.indexB;

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

      EntityID entityA = collision.a;  // Use already-known entity IDs
      EntityID entityB = collision.b;

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
    // Use stored indices - NO MORE LINEAR LOOKUP!
    size_t indexA = collision.indexA;
    size_t indexB = collision.indexB;

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

        // Log removed for performance

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

      // Find trigger hot data for position - use hash lookup instead of linear search
      Vector2D triggerPos(0, 0);
      HammerEngine::TriggerTag triggerTag = HammerEngine::TriggerTag::None;
      auto triggerIt = m_storage.entityToIndex.find(triggerId);
      if (triggerIt != m_storage.entityToIndex.end()) {
        size_t triggerIndex = triggerIt->second;
        if (triggerIndex < m_storage.hotData.size()) {
          const auto& hot = m_storage.hotData[triggerIndex];
          triggerPos = hot.position;
          triggerTag = static_cast<HammerEngine::TriggerTag>(hot.triggerTag);
        }
      }

      WorldTriggerEvent evt(playerId, triggerId, triggerTag, triggerPos, TriggerPhase::Exit);
      EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);

      // Log removed for performance

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
    size_t bodyCount,
    size_t activeMovableBodies,
    size_t pairCount,
    size_t collisionCount,
    size_t activeBodies,
    size_t dynamicBodiesCulled,
    size_t staticBodiesCulled,
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
  m_perf.lastCullingMs = cullingMs;

  m_perf.updateAverage(m_perf.lastTotalMs);
  m_perf.updateBroadphaseAverage(d12);
  m_perf.frames += 1;

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

    // Coarse region cache statistics
    size_t totalCacheAccesses = m_cacheHits + m_cacheMisses;
    float cacheHitRate = totalCacheAccesses > 0 ? (static_cast<float>(m_cacheHits) / totalCacheAccesses) * 100.0f : 0.0f;
    size_t activeRegions = m_coarseRegionStaticCache.size();
    std::string cacheStatsStr = " [RegionCache: Active=" + std::to_string(activeRegions) +
                                ", Hits=" + std::to_string(m_cacheHits) +
                                ", Misses=" + std::to_string(m_cacheMisses) +
                                ", HitRate=" + std::to_string(static_cast<int>(cacheHitRate)) + "%]";

    COLLISION_DEBUG("SOA Collision Summary - Bodies: " + std::to_string(bodyCount) +
                    " (" + std::to_string(activeMovableBodies) + " movable)" +
                    ", Avg Total: " + std::to_string(m_perf.avgTotalMs) + "ms" +
                    ", Avg Broadphase: " + std::to_string(m_perf.avgBroadphaseMs) + "ms" +
                    ", Current Broadphase: " + std::to_string(d12) + "ms" +
                    ", Narrowphase: " + std::to_string(d23) + "ms" +
                    ", Last Pairs: " + std::to_string(pairCount) +
                    ", Last Collisions: " + std::to_string(collisionCount) +
                    optimizationStats + cacheStatsStr);

    // Reset cache counters for next reporting window (every 300 frames)
    m_cacheHits = 0;
    m_cacheMisses = 0;
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

  // Verbose logging removed for performance
}

// ========== SOA BODY MANAGEMENT METHODS ==========

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].active = enabled ? 1 : 0;
    }
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    auto& hot = m_storage.hotData[index];
    hot.layers = layerMask;
    hot.collidesWith = collideMask;
    }
}

void CollisionManager::setVelocity(EntityID id, const Vector2D& velocity) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].velocity = velocity;
    }
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    m_storage.hotData[index].isTrigger = isTrigger ? 1 : 0;
    }
}

CollisionManager::CullingArea CollisionManager::createDefaultCullingArea() const {
  CullingArea area;
  Vector2D playerPos(0.0f, 0.0f);
  bool playerFound = false;

  // Search for player entity by collision layer instead of hardcoded EntityID
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    // Check if this entity has Player layer
    if (hot.layers & CollisionLayer::Layer_Player) {
      playerPos = hot.position;
      playerFound = true;
      break; // Found player, use this position
    }
  }

  if (playerFound) {
    // Create player-centered culling area
    area.minX = playerPos.getX() - COLLISION_CULLING_BUFFER;
    area.minY = playerPos.getY() - COLLISION_CULLING_BUFFER;
    area.maxX = playerPos.getX() + COLLISION_CULLING_BUFFER;
    area.maxY = playerPos.getY() + COLLISION_CULLING_BUFFER;
  } else {
    // Player not found - use world center with standard culling area
    // This maintains consistent culling behavior instead of processing entire world
    area.minX = 0.0f - COLLISION_CULLING_BUFFER;
    area.minY = 0.0f - COLLISION_CULLING_BUFFER;
    area.maxX = 0.0f + COLLISION_CULLING_BUFFER;
    area.maxY = 0.0f + COLLISION_CULLING_BUFFER;

    // Log warning only once every 300 frames to avoid spam
    static uint64_t logFrameCounter = 0;
    if (++logFrameCounter % 300 == 1) {
      COLLISION_WARN("Player entity not found for culling area - using world center (logged every 300 frames)");
    }
  }

  return area;
}

// ========== PERFORMANCE: VECTOR POOLING METHODS ==========

std::vector<size_t>& CollisionManager::getPooledVector() {
  // Initialize pool if empty
  if (m_vectorPool.empty()) {
    m_vectorPool.reserve(32); // Reasonable pool size
    for (size_t i = 0; i < 16; ++i) {
      m_vectorPool.emplace_back();
      m_vectorPool.back().reserve(64); // Pre-allocate reasonable capacity
    }
  }

  // Use round-robin allocation
  if (m_nextPoolIndex >= m_vectorPool.size()) {
    m_nextPoolIndex = 0;
  }

  auto& vec = m_vectorPool[m_nextPoolIndex++];
  vec.clear(); // Clear but retain capacity
  return vec;
}

void CollisionManager::returnPooledVector(std::vector<size_t>& vec) {
  // Vector is automatically returned to pool via reference
  // Just clear it to avoid holding onto data
  vec.clear();
}

