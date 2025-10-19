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
#include <numeric>
#include <queue>
#include <set>
#include <shared_mutex>
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

  /* IMPORTANT: Clear ALL collision bodies during state transitions
   *
   * Previous logic tried to be "smart" by keeping static bodies when a world
   * was active, expecting WorldUnloadedEvent to clean them up. This was BROKEN
   * because prepareForStateTransition() unregisters event handlers (line 138),
   * so the WorldUnloadedEvent handler never fires!
   *
   * Result: Static bodies from old world persisted into new world, causing:
   * - Duplicate/stale collision bodies
   * - Spatial hash corruption
   * - Collision detection failures
   *
   * Solution: Always clear ALL bodies. The world will be unloaded immediately
   * after state transition anyway, and new state will rebuild static bodies
   * when it loads its world.
   */
  [[maybe_unused]] size_t soaBodyCount = m_storage.size();
  COLLISION_INFO("STORAGE LIFECYCLE: prepareForStateTransition() clearing " +
                 std::to_string(soaBodyCount) + " SOA bodies (dynamic + static)");

  // Acquire exclusive write lock before clearing storage
  // Prevents AI threads from reading during modifications
  {
    std::unique_lock<std::shared_mutex> storageLock(m_storageMutex);

    // Clear all collision bodies and spatial hashes
    m_storage.clear();
    m_staticSpatialHash.clear();
    m_dynamicSpatialHash.clear();
  }

  // Process any pending commands before clearing caches to ensure clean state
  processPendingCommands();

  // Clear pending command queue to prevent stale commands in new state
  {
    std::lock_guard<std::mutex> lock(m_commandQueueMutex);
    m_pendingCommands.clear();
  }

  // Clear all caches to prevent dangling references to deleted bodies
  m_collisionPool.resetFrame();              // Clear collision buffers
  m_coarseRegionStaticCache.clear();         // Clear region-based static cache
  m_bodyCoarseCell.clear();                  // Clear body cell tracking
  m_cacheHits = 0;                           // Reset cache statistics
  m_cacheMisses = 0;

  // Clear vector pool to release temporary allocations
  m_vectorPool.clear();
  m_nextPoolIndex = 0;

  // Clear trigger tracking state completely
  m_activeTriggerPairs.clear();
  m_triggerCooldownUntil.clear();

  // Reset trigger cooldown settings
  m_defaultTriggerCooldownSec = 0.0f;

  // Unregister all event handlers before clearing tokens
  auto& em = EventManager::Instance();
  for (const auto& token : m_handlerTokens) {
    em.removeHandler(token);
  }
  m_handlerTokens.clear();

  // Re-subscribe to world events (WorldLoaded, WorldUnloaded, TileChanged)
  // These are manager-level handlers that must persist across state transitions
  subscribeWorldEvents();

  // Clear all collision callbacks (these should be re-registered by new states)
  m_callbacks.clear();

  // Reset performance stats for clean slate
  m_perf = PerfStats{};

  // Reset world bounds to minimal (will be set by WorldLoadedEvent/WorldGeneratedEvent)
  m_worldBounds = AABB(0.0f, 0.0f, 0.0f, 0.0f);

  // Reset syncing state
  m_isSyncing = false;

  // Reset verbose logging to default
  m_verboseLogs = false;

  [[maybe_unused]] size_t finalBodyCount = m_storage.size();
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
  // Pass trigger properties through deferred command queue (thread-safe)
  addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                      layerMask, collideMask, true, static_cast<uint8_t>(tag));
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
  constexpr float tileSize = HammerEngine::TILE_SIZE;
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
        addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                           CollisionLayer::Layer_Environment, 0xFFFFFFFFu,
                           true, static_cast<uint8_t>(tag));
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
  constexpr float tileSize = HammerEngine::TILE_SIZE;
  const int h = static_cast<int>(world->grid.size());

  // Track which tiles we've already processed
  std::set<std::pair<int, int>> processedTiles;

  for (int y = 0; y < h; ++y) {
    const int w = static_cast<int>(world->grid[y].size());
    for (int x = 0; x < w; ++x) {
      const auto &tile = world->grid[y][x];

      if (tile.obstacleType != ObstacleType::BUILDING || tile.buildingId == 0)
        continue;

      // Skip if we've already processed this tile as part of another building
      if (processedTiles.find({x, y}) != processedTiles.end())
        continue;

      // Flood fill to find all connected building tiles with same buildingId
      std::set<std::pair<int, int>> visited;
      std::queue<std::pair<int, int>> toVisit;
      std::vector<std::pair<int, int>> buildingTiles;

      toVisit.push({x, y});
      visited.insert({x, y});

      while (!toVisit.empty()) {
        auto [cx, cy] = toVisit.front();
        toVisit.pop();
        buildingTiles.push_back({cx, cy});
        processedTiles.insert({cx, cy});

        const int dx[] = {-1, 1, 0, 0};
        const int dy[] = {0, 0, -1, 1};

        for (int i = 0; i < 4; ++i) {
          int nx = cx + dx[i];
          int ny = cy + dy[i];

          if (nx < 0 || ny < 0 || ny >= h || nx >= w)
            continue;
          if (visited.find({nx, ny}) != visited.end())
            continue;

          const auto& neighbor = world->grid[ny][nx];
          if (neighbor.obstacleType == ObstacleType::BUILDING &&
              neighbor.buildingId == tile.buildingId) {
            visited.insert({nx, ny});
            toVisit.push({nx, ny});
          }
        }
      }

      // SIMPLE RECTANGLE DETECTION: Most buildings are solid rectangles
      // Only use row decomposition for complex non-rectangular shapes

      // Find bounding box
      const auto& firstTile = buildingTiles[0];
      int minX = firstTile.first;
      int maxX = firstTile.first;
      int minY = firstTile.second;
      int maxY = firstTile.second;

      for (const auto& [tx, ty] : buildingTiles) {
        minX = std::min(minX, tx);
        maxX = std::max(maxX, tx);
        minY = std::min(minY, ty);
        maxY = std::max(maxY, ty);
      }

      // Check if building is a solid rectangle (all tiles present)
      int expectedTiles = (maxX - minX + 1) * (maxY - minY + 1);
      bool isRectangle = (static_cast<int>(buildingTiles.size()) == expectedTiles);

      // DEBUG: Log rectangle detection for troubleshooting
      COLLISION_DEBUG("Building " + std::to_string(tile.buildingId) +
                     ": bounds (" + std::to_string(minX) + "," + std::to_string(minY) +
                     ") to (" + std::to_string(maxX) + "," + std::to_string(maxY) +
                     "), tiles=" + std::to_string(buildingTiles.size()) +
                     ", expected=" + std::to_string(expectedTiles) +
                     ", isRectangle=" + (isRectangle ? "YES" : "NO"));

      if (isRectangle) {
        // SIMPLE CASE: Single collision body for entire rectangular building
        float worldMinX = minX * tileSize;
        float worldMinY = minY * tileSize;
        float worldMaxX = (maxX + 1) * tileSize;
        float worldMaxY = (maxY + 1) * tileSize;

        float cx = (worldMinX + worldMaxX) * 0.5f;
        float cy = (worldMinY + worldMaxY) * 0.5f;
        float halfWidth = (worldMaxX - worldMinX) * 0.5f;
        float halfHeight = (worldMaxY - worldMinY) * 0.5f;

        Vector2D center(cx, cy);
        Vector2D halfSize(halfWidth, halfHeight);

        // Single body: subBodyIndex = 0
        EntityID id = (static_cast<EntityID>(3ull) << 61) |
                      (static_cast<EntityID>(tile.buildingId) << 16);

        if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
          addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                             CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
          ++created;

          COLLISION_INFO("Building " + std::to_string(tile.buildingId) +
                        ": created 1 collision body (rectangle " +
                        std::to_string(maxX - minX + 1) + "x" + std::to_string(maxY - minY + 1) +
                        " tiles) at center(" + std::to_string(cx) + "," + std::to_string(cy) +
                        ") halfSize(" + std::to_string(halfWidth) + "," + std::to_string(halfHeight) +
                        ") AABB[" + std::to_string(worldMinX) + "," + std::to_string(worldMinY) +
                        " to " + std::to_string(worldMaxX) + "," + std::to_string(worldMaxY) + "]");
        }
      } else {
        // COMPLEX CASE: Non-rectangular building - use row decomposition
        std::map<int, std::vector<int>> rowToColumns;
        for (const auto& [tx, ty] : buildingTiles) {
          rowToColumns[ty].push_back(tx);
        }

        for (auto& [row, columns] : rowToColumns) {
          std::sort(columns.begin(), columns.end());
        }

        uint16_t subBodyIndex = 0;
        for (const auto& [row, columns] : rowToColumns) {
          size_t i = 0;
          while (i < columns.size()) {
            int spanStart = columns[i];
            int spanEnd = spanStart;

            while (i + 1 < columns.size() && columns[i + 1] == columns[i] + 1) {
              ++i;
              spanEnd = columns[i];
            }

            float worldMinX = spanStart * tileSize;
            float worldMinY = row * tileSize;
            float worldMaxX = (spanEnd + 1) * tileSize;
            float worldMaxY = (row + 1) * tileSize;

            float cx = (worldMinX + worldMaxX) * 0.5f;
            float cy = (worldMinY + worldMaxY) * 0.5f;
            float halfWidth = (worldMaxX - worldMinX) * 0.5f;
            float halfHeight = (worldMaxY - worldMinY) * 0.5f;

            Vector2D center(cx, cy);
            Vector2D halfSize(halfWidth, halfHeight);

            EntityID id = (static_cast<EntityID>(3ull) << 61) |
                          (static_cast<EntityID>(tile.buildingId) << 16) |
                          static_cast<EntityID>(subBodyIndex);

            if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
              addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                                 CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
              ++created;
              ++subBodyIndex;
            }

            ++i;
          }
        }

        COLLISION_INFO("Building " + std::to_string(tile.buildingId) +
                      ": created " + std::to_string(subBodyIndex) +
                      " collision bodies (non-rectangular)");
      }
    }
  }

  return created;
}

void CollisionManager::validateBuildingCollisionCoverage() {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return;

  COLLISION_DEBUG("Validating building collision coverage...");
  std::set<uint32_t> uniqueBuildings;
  std::map<uint32_t, int> buildingTileCounts;
  std::map<uint32_t, std::vector<std::pair<int, int>>> buildingTilePositions;

  const int h = static_cast<int>(world->grid.size());

  // Count tiles per building
  for (int y = 0; y < h; ++y) {
    const int w = static_cast<int>(world->grid[y].size());
    for (int x = 0; x < w; ++x) {
      const auto &tile = world->grid[y][x];
      if (tile.obstacleType == ObstacleType::BUILDING && tile.buildingId > 0) {
        uniqueBuildings.insert(tile.buildingId);
        buildingTileCounts[tile.buildingId]++;
        buildingTilePositions[tile.buildingId].push_back({x, y});
      }
    }
  }

  if (uniqueBuildings.empty()) {
    COLLISION_DEBUG("No buildings found in world - skipping validation");
    return;
  }

  // Validate each building
  int buildingsWithCollision = 0;
  int buildingsMissingCollision = 0;

  for (uint32_t buildingId : uniqueBuildings) {
    int tileCount = buildingTileCounts[buildingId];
    const auto& positions = buildingTilePositions[buildingId];

    // Check if any collision bodies exist for this building
    bool hasCollisionBodies = false;
    int collisionBodyCount = 0;
    for (uint16_t subIdx = 0; subIdx < 100; ++subIdx) { // Check up to 100 sub-bodies
      EntityID id = (static_cast<EntityID>(3ull) << 61) |
                    (static_cast<EntityID>(buildingId) << 16) |
                    static_cast<EntityID>(subIdx);
      if (m_storage.entityToIndex.find(id) != m_storage.entityToIndex.end()) {
        hasCollisionBodies = true;
        collisionBodyCount++;
      }
    }

    if (!hasCollisionBodies) {
      COLLISION_ERROR("Building " + std::to_string(buildingId) +
                     " has " + std::to_string(tileCount) +
                     " tiles but NO collision bodies! Positions: " +
                     "(" + std::to_string(positions[0].first) + ", " +
                     std::to_string(positions[0].second) + ") to (" +
                     std::to_string(positions.back().first) + ", " +
                     std::to_string(positions.back().second) + ")");
      buildingsMissingCollision++;
    } else {
      COLLISION_DEBUG("Building " + std::to_string(buildingId) +
                     " validated: " + std::to_string(tileCount) + " tiles, " +
                     std::to_string(collisionBodyCount) + " collision bodies");
      buildingsWithCollision++;
    }
  }

  COLLISION_INFO("Building validation complete: " +
                std::to_string(buildingsWithCollision) + " buildings OK, " +
                std::to_string(buildingsMissingCollision) + " buildings MISSING collision bodies");
}

bool CollisionManager::overlaps(EntityID a, EntityID b) const {
  // Thread-safe read access - single lock for entire operation
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  auto itA = m_storage.entityToIndex.find(a);
  auto itB = m_storage.entityToIndex.find(b);
  if (itA == m_storage.entityToIndex.end() || itB == m_storage.entityToIndex.end())
    return false;

  size_t indexA = itA->second;
  size_t indexB = itB->second;

  AABB aabbA = m_storage.computeAABB(indexA);
  AABB aabbB = m_storage.computeAABB(indexB);
  return aabbA.intersects(aabbB);
}

void CollisionManager::queryArea(const AABB &area,
                                 std::vector<EntityID> &out) const {
  // Thread-safe read access - allows concurrent reads from multiple AI threads
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

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
  // Thread-safe read access - single lock for entire operation
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  outCenter = m_storage.hotData[it->second].position;
  return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
  // Thread-safe read access - single lock for entire operation
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) == BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
  // Thread-safe read access - single lock for entire operation
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) == BodyType::KINEMATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
  // Thread-safe read access - single lock for entire operation
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return m_storage.hotData[it->second].isTrigger;
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
  [[maybe_unused]] size_t staticBodies = getStaticBodyCount();
  [[maybe_unused]] size_t kinematicBodies = getKinematicBodyCount();
  [[maybe_unused]] size_t dynamicBodies = getDynamicBodyCount();

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
    [[maybe_unused]] std::string layerName;
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
  [[maybe_unused]] size_t waterTriggers =
      createTriggersForWaterTiles(HammerEngine::TriggerTag::Water);
  [[maybe_unused]] size_t obstacleTriggers =
      createTriggersForObstacles(); // Always returns 0 - obstacle penalties
                                    // handled by pathfinding

  if (solidBodies > 0 || waterTriggers > 0) {
    COLLISION_INFO(
        "World colliders built: solid=" + std::to_string(solidBodies) +
        ", water triggers=" + std::to_string(waterTriggers) +
        ", obstacle triggers=" + std::to_string(obstacleTriggers));

    // CRITICAL: Process pending commands BEFORE rebuilding spatial hash
    // The createStatic*() functions above add bodies via command queue,
    // so we must process them first or spatial hash will be empty!
    processPendingCommands();

    // DEBUG: Count actual building collision bodies to verify cleanup
    int buildingBodyCount = 0;
    for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
      EntityID id = m_storage.entityIds[i];
      if ((id >> 61) == 3) { // Building type
        buildingBodyCount++;
        uint32_t buildingId = (id >> 16) & 0xFFFF;
        uint16_t subBodyIndex = id & 0xFFFF;
        COLLISION_DEBUG("Building collision body found: buildingId=" + std::to_string(buildingId) +
                       ", subBodyIndex=" + std::to_string(subBodyIndex));
      }
    }
    COLLISION_INFO("Total building collision bodies in storage: " + std::to_string(buildingBodyCount));

    // VALIDATION: Check that all buildings have collision coverage (after processing commands)
    validateBuildingCollisionCoverage();

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
  constexpr float tileSize = HammerEngine::TILE_SIZE;

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
      // Pass trigger properties through command queue (isTrigger=true, triggerTag=Water)
      addCollisionBodySOA(trigId, center, halfSize, BodyType::STATIC,
                         CollisionLayer::Layer_Environment, 0xFFFFFFFFu,
                         true, static_cast<uint8_t>(HammerEngine::TriggerTag::Water));
    }

    // Update solid obstacle collision body for this tile (BUILDING only)
    // Remove old per-tile collision body (legacy)
    EntityID oldObstacleId =
        (static_cast<EntityID>(2ull) << 61) |
        (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
        static_cast<EntityID>(static_cast<uint32_t>(x));
    removeCollisionBodySOA(oldObstacleId);

    if (tile.obstacleType == ObstacleType::BUILDING && tile.buildingId > 0) {
      // Find all connected building tiles to create unified collision body
      // This prevents collision seams when buildings are adjacent
      std::set<std::pair<int, int>> visited;
      std::queue<std::pair<int, int>> toVisit;
      std::vector<std::pair<int, int>> buildingTiles;

      toVisit.push({x, y});
      visited.insert({x, y});

      // Flood fill to find all connected building tiles (same buildingId or adjacent buildings)
      while (!toVisit.empty()) {
        auto [cx, cy] = toVisit.front();
        toVisit.pop();
        buildingTiles.push_back({cx, cy});

        // Check all 4 adjacent tiles for building connectivity
        const int dx[] = {-1, 1, 0, 0};
        const int dy[] = {0, 0, -1, 1};

        for (int i = 0; i < 4; ++i) {
          int nx = cx + dx[i];
          int ny = cy + dy[i];

          if (nx < 0 || ny < 0 || ny >= static_cast<int>(world->grid.size()) ||
              nx >= static_cast<int>(world->grid[ny].size()))
            continue;

          if (visited.find({nx, ny}) != visited.end())
            continue;

          const auto& neighbor = world->grid[ny][nx];

          // Only connect tiles with the SAME buildingId (forms one building unit)
          if (neighbor.obstacleType == ObstacleType::BUILDING &&
              neighbor.buildingId == tile.buildingId) {
            visited.insert({nx, ny});
            toVisit.push({nx, ny});
          }
        }
      }

      // Only create collision body if this is the top-left tile of the cluster
      bool isTopLeft = true;
      for (const auto& [tx, ty] : buildingTiles) {
        if (ty < y || (ty == y && tx < x)) {
          isTopLeft = false;
          break;
        }
      }

      if (isTopLeft) {
        // REMOVE OLD COLLISION BODIES: Handle BOTH old and new EntityID formats
        // OLD format (single body): (3ull << 61) | buildingId
        // NEW format (multi-body): (3ull << 61) | (buildingId << 16) | subBodyIndex

        // First, remove OLD format single bounding box (if it exists)
        EntityID oldSingleBodyId = (static_cast<EntityID>(3ull) << 61) |
                                   static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
        removeCollisionBodySOA(oldSingleBodyId);

        // Then, remove all NEW format multi-body collision bodies
        uint16_t subBodyIndex = 0;
        while (subBodyIndex < 1000) { // Safety limit to prevent infinite loop
          EntityID newMultiBodyId = (static_cast<EntityID>(3ull) << 61) |
                                    (static_cast<EntityID>(tile.buildingId) << 16) |
                                    static_cast<EntityID>(subBodyIndex);

          auto it = m_storage.entityToIndex.find(newMultiBodyId);
          if (it == m_storage.entityToIndex.end()) {
            break; // No more sub-bodies for this building
          }

          removeCollisionBodySOA(newMultiBodyId);
          ++subBodyIndex;
        }

        // ROW-BASED RECTANGULAR DECOMPOSITION for accurate collision on non-rectangular buildings
        // Group tiles by row and create one collision body per contiguous horizontal span
        std::map<int, std::vector<int>> rowToColumns;
        for (const auto& [tx, ty] : buildingTiles) {
          rowToColumns[ty].push_back(tx);
        }

        // Sort columns in each row for contiguous span detection
        for (auto& [row, columns] : rowToColumns) {
          std::sort(columns.begin(), columns.end());
        }

        // Create collision bodies for each row's contiguous spans
        subBodyIndex = 0;
        for (const auto& [row, columns] : rowToColumns) {
          size_t i = 0;
          while (i < columns.size()) {
            int spanStart = columns[i];
            int spanEnd = spanStart;

            // Extend span while tiles are contiguous
            while (i + 1 < columns.size() && columns[i + 1] == columns[i] + 1) {
              ++i;
              spanEnd = columns[i];
            }

            // Create collision body for this span with vertical overlap to eliminate seams
            // Small overlap prevents gaps between row bodies due to floating point precision
            constexpr float SEAM_OVERLAP = 0.1f;

            float worldMinX = spanStart * tileSize;
            float worldMinY = row * tileSize - SEAM_OVERLAP;
            float worldMaxX = (spanEnd + 1) * tileSize;
            float worldMaxY = (row + 1) * tileSize + SEAM_OVERLAP;

            float cx = (worldMinX + worldMaxX) * 0.5f;
            float cy = (worldMinY + worldMaxY) * 0.5f;
            float halfWidth = (worldMaxX - worldMinX) * 0.5f;
            float halfHeight = (worldMaxY - worldMinY) * 0.5f;

            Vector2D center(cx, cy);
            Vector2D halfSize(halfWidth, halfHeight);

            // Encode building ID and sub-body index in EntityID
            // Format: (3ull << 61) | (buildingId << 16) | subBodyIndex
            EntityID id = (static_cast<EntityID>(3ull) << 61) |
                          (static_cast<EntityID>(tile.buildingId) << 16) |
                          static_cast<EntityID>(subBodyIndex);

            addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                               CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
            ++subBodyIndex;

            ++i; // Move to next potential span
          }
        }
      }
    } else if (tile.obstacleType != ObstacleType::BUILDING &&
               tile.buildingId > 0) {
      // Tile was a building but no longer is - remove ALL building collision bodies
      // Handle BOTH old and new EntityID formats

      // Remove OLD format single bounding box (if it exists)
      EntityID oldSingleBodyId = (static_cast<EntityID>(3ull) << 61) |
                                 static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
      removeCollisionBodySOA(oldSingleBodyId);

      // Remove all NEW format multi-body collision bodies
      uint16_t subBodyIndex = 0;
      while (subBodyIndex < 1000) { // Safety limit to prevent infinite loop
        EntityID newMultiBodyId = (static_cast<EntityID>(3ull) << 61) |
                                  (static_cast<EntityID>(tile.buildingId) << 16) |
                                  static_cast<EntityID>(subBodyIndex);

        auto it = m_storage.entityToIndex.find(newMultiBodyId);
        if (it == m_storage.entityToIndex.end()) {
          break; // No more sub-bodies for this building
        }

        removeCollisionBodySOA(newMultiBodyId);
        ++subBodyIndex;
      }
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

void CollisionManager::processPendingCommands() {
  // Drain the command queue and apply all pending operations
  // This is called from updateSOA() on the update thread
  std::vector<PendingCommand> commandsToProcess;

  {
    std::lock_guard<std::mutex> lock(m_commandQueueMutex);
    if (m_pendingCommands.empty()) {
      return; // Early exit if no commands
    }
    commandsToProcess.swap(m_pendingCommands); // Move commands out quickly
  }

  // Acquire exclusive write lock - prevents AI threads from reading during modifications
  // This protects entityToIndex map and storage arrays from concurrent access
  std::unique_lock<std::shared_mutex> storageLock(m_storageMutex);

  for (const auto& cmd : commandsToProcess) {
    switch (cmd.type) {
      case CommandType::Add: {
        // Check if entity already exists
        auto it = m_storage.entityToIndex.find(cmd.id);
        if (it != m_storage.entityToIndex.end()) {
          // Update existing entity
          size_t index = it->second;
          if (index < m_storage.hotData.size()) {
            auto& hot = m_storage.hotData[index];
            hot.position = cmd.position;
            hot.halfSize = cmd.halfSize;
            hot.aabbDirty = 1;
            hot.layers = cmd.layer;
            hot.collidesWith = cmd.collideMask;
            hot.bodyType = static_cast<uint8_t>(cmd.bodyType);
            hot.active = true;

            // Update cold data
            if (index < m_storage.coldData.size()) {
              m_storage.coldData[index].fullAABB = AABB(cmd.position.getX(), cmd.position.getY(),
                                                        cmd.halfSize.getX(), cmd.halfSize.getY());
            }
          }
          continue;
        }

        // Add new entity
        size_t newIndex = m_storage.size();

        // Initialize hot data
        CollisionStorage::HotData hotData{};
        hotData.position = cmd.position;
        hotData.velocity = Vector2D(0, 0);
        hotData.halfSize = cmd.halfSize;
        hotData.aabbDirty = 1;
        hotData.layers = cmd.layer;
        hotData.collidesWith = cmd.collideMask;
        hotData.bodyType = static_cast<uint8_t>(cmd.bodyType);
        hotData.triggerTag = cmd.triggerTag;
        hotData.active = true;
        hotData.isTrigger = cmd.isTrigger;
        hotData.restitution = 0.0f;

        // Initialize cold data
        CollisionStorage::ColdData coldData{};
        coldData.acceleration = Vector2D(0, 0);
        coldData.lastPosition = cmd.position;
        coldData.fullAABB = AABB(cmd.position.getX(), cmd.position.getY(),
                                 cmd.halfSize.getX(), cmd.halfSize.getY());

        // Add to storage
        m_storage.hotData.push_back(hotData);
        m_storage.coldData.push_back(coldData);
        m_storage.entityIds.push_back(cmd.id);
        m_storage.entityToIndex[cmd.id] = newIndex;

        // Fire collision obstacle changed event for static bodies
        if (cmd.bodyType == BodyType::STATIC) {
          float radius = std::max(cmd.halfSize.getX(), cmd.halfSize.getY()) + 16.0f;
          std::string description = "Static obstacle added at (" +
                                    std::to_string(cmd.position.getX()) + ", " +
                                    std::to_string(cmd.position.getY()) + ")";
          EventManager::Instance().triggerCollisionObstacleChanged(cmd.position, radius, description,
                                                                  EventManager::DispatchMode::Deferred);
          m_staticHashDirty = true;
        }
        break;
      }

      case CommandType::Remove: {
        auto it = m_storage.entityToIndex.find(cmd.id);
        if (it == m_storage.entityToIndex.end()) {
          continue; // Entity not found
        }

        size_t indexToRemove = it->second;
        size_t lastIndex = m_storage.size() - 1;

        // Fire collision obstacle changed event for static bodies before removal
        if (indexToRemove < m_storage.size()) {
          const auto& hot = m_storage.hotData[indexToRemove];
          if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
            float radius = std::max(hot.halfSize.getX(), hot.halfSize.getY()) + 16.0f;
            std::string description = "Static obstacle removed from (" +
                                      std::to_string(hot.position.getX()) + ", " +
                                      std::to_string(hot.position.getY()) + ")";
            EventManager::Instance().triggerCollisionObstacleChanged(hot.position, radius, description,
                                                                    EventManager::DispatchMode::Deferred);
            m_staticHashDirty = true;
          }
        }

        if (indexToRemove != lastIndex) {
          // Swap with last element
          m_storage.hotData[indexToRemove] = m_storage.hotData[lastIndex];
          m_storage.coldData[indexToRemove] = m_storage.coldData[lastIndex];
          m_storage.entityIds[indexToRemove] = m_storage.entityIds[lastIndex];

          // Update map for swapped entity
          EntityID movedEntity = m_storage.entityIds[indexToRemove];
          m_storage.entityToIndex[movedEntity] = indexToRemove;
        }

        // Remove last element
        m_storage.hotData.pop_back();
        m_storage.coldData.pop_back();
        m_storage.entityIds.pop_back();
        m_storage.entityToIndex.erase(cmd.id);
        break;
      }

      case CommandType::Modify:
        // Handle modify commands if needed in the future
        break;
    }
  }
}

size_t CollisionManager::addCollisionBodySOA(EntityID id, const Vector2D& position,
                                              const Vector2D& halfSize, BodyType type,
                                              uint32_t layer, uint32_t collidesWith,
                                              bool isTrigger, uint8_t triggerTag) {
  // Queue command for deferred processing on update thread (thread-safe)
  PendingCommand cmd;
  cmd.type = CommandType::Add;
  cmd.id = id;
  cmd.position = position;
  cmd.halfSize = halfSize;
  cmd.bodyType = type;
  cmd.layer = layer;
  cmd.collideMask = collidesWith;
  cmd.isTrigger = isTrigger;
  cmd.triggerTag = triggerTag;

  {
    std::lock_guard<std::mutex> lock(m_commandQueueMutex);
    m_pendingCommands.push_back(cmd);
  }

  // Return 0 as placeholder - actual index will be assigned when command is processed
  // This is acceptable since most callers don't use the return value
  return 0;
}

void CollisionManager::removeCollisionBodySOA(EntityID id) {
  // Queue command for deferred processing on update thread (thread-safe)
  PendingCommand cmd;
  cmd.type = CommandType::Remove;
  cmd.id = id;

  {
    std::lock_guard<std::mutex> lock(m_commandQueueMutex);
    m_pendingCommands.push_back(cmd);
  }

  // Clean up trigger-related state for this entity
  for (auto it = m_activeTriggerPairs.begin(); it != m_activeTriggerPairs.end(); ) {
    if (it->second.first == id || it->second.second == id) {
      it = m_activeTriggerPairs.erase(it);
    } else {
      ++it;
    }
  }
  m_triggerCooldownUntil.erase(id);
}

bool CollisionManager::getCollisionBodySOA(EntityID id, size_t& outIndex) const {
  // Thread-safe read access - allows concurrent reads from multiple AI threads
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

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

Vector2D CollisionManager::getCollisionBodyVelocitySOA(EntityID id) const {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    return m_storage.hotData[index].velocity;
  }
  return Vector2D(0, 0);
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
std::tuple<size_t, size_t, size_t> CollisionManager::buildActiveIndicesSOA(const CullingArea& cullingArea) const {
  // Build indices of active bodies within culling area
  // OPTIMIZATION: Count body types during iteration to avoid expensive count_if calls
  auto& pools = m_collisionPool;

  // Store current culling area for use in broadphase queries
  m_currentCullingArea = cullingArea;
  pools.activeIndices.clear();
  pools.movableIndices.clear();
  pools.staticIndices.clear();

  // Track total body counts (before culling)
  size_t totalStatic = 0;
  size_t totalDynamic = 0;
  size_t totalKinematic = 0;

  // Linear pass - spatial hash handles proximity efficiently
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    // CRITICAL: Apply culling FIRST to skip 99% of bodies early
    // This prevents unnecessary work on bodies far from camera
    if (cullingArea.minX != cullingArea.maxX || cullingArea.minY != cullingArea.maxY) {
      if (!cullingArea.contains(hot.position.getX(), hot.position.getY())) {
        continue; // Skip bodies outside culling area
      }
    }

    // Now check body type only for bodies that passed culling (~100 bodies vs 27k)
    BodyType bodyType = static_cast<BodyType>(hot.bodyType);

    // Count active (post-culling) bodies by type
    if (bodyType == BodyType::STATIC) {
      totalStatic++;
    } else if (bodyType == BodyType::DYNAMIC) {
      totalDynamic++;
    } else if (bodyType == BodyType::KINEMATIC) {
      totalKinematic++;
    }

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

  return {totalStatic, totalDynamic, totalKinematic};
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
    // Check active status first (direct indexing to avoid long-lived reference)
    if (!m_storage.hotData[dynamicIdx].active) continue;

    // Copy collision mask to avoid dangling reference if vector reallocates
    const uint32_t dynamicCollidesWith = m_storage.hotData[dynamicIdx].collidesWith;

    // PERFORMANCE: Use cached AABB bounds directly without constructing AABB object
    // Cached bounds are already updated in updateCachedAABB()
    m_storage.updateCachedAABB(dynamicIdx); // Ensure cache is fresh

    // Re-fetch reference after updateCachedAABB (safe since updateCachedAABB doesn't reallocate)
    const auto& dynamicHot = m_storage.hotData[dynamicIdx];

    // Calculate epsilon-expanded bounds directly from cached min/max (NO AABB construction!)
#ifdef COLLISION_SIMD_SSE2
    // SIMD: Compute all 4 bounds in parallel
    // Load: [aabbMinX, aabbMinY, aabbMaxX, aabbMaxY]
    __m128 bounds = _mm_set_ps(dynamicHot.aabbMaxY, dynamicHot.aabbMaxX,
                               dynamicHot.aabbMinY, dynamicHot.aabbMinX);
    // Epsilon: [-eps, -eps, +eps, +eps]
    __m128 epsilon = _mm_set_ps(SPATIAL_QUERY_EPSILON, SPATIAL_QUERY_EPSILON,
                                -SPATIAL_QUERY_EPSILON, -SPATIAL_QUERY_EPSILON);
    __m128 queryBounds = _mm_add_ps(bounds, epsilon);

    // Extract results
    alignas(16) float queryBoundsArray[4];
    _mm_store_ps(queryBoundsArray, queryBounds);
    float queryMinX = queryBoundsArray[0];
    float queryMinY = queryBoundsArray[1];
    float queryMaxX = queryBoundsArray[2];
    float queryMaxY = queryBoundsArray[3];
#else
    // Scalar fallback
    float queryMinX = dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON;
    float queryMinY = dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON;
    float queryMaxX = dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON;
    float queryMaxY = dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON;
#endif

    // 1. Movable-vs-movable collisions (use optimized bounds-based query)
    auto& dynamicCandidates = getPooledVector();
    m_dynamicSpatialHash.queryRegionBounds(queryMinX, queryMinY, queryMaxX, queryMaxY, dynamicCandidates);

#ifdef COLLISION_SIMD_SSE2
    // SIMD: Process candidates in batches of 4 for layer mask filtering
    const __m128i maskVec = _mm_set1_epi32(dynamicCollidesWith);
    size_t i = 0;
    const size_t simdEnd = (dynamicCandidates.size() / 4) * 4;

    for (; i < simdEnd; i += 4) {
      // Bounds check for batch
      if (dynamicCandidates[i] >= m_storage.hotData.size() ||
          dynamicCandidates[i+1] >= m_storage.hotData.size() ||
          dynamicCandidates[i+2] >= m_storage.hotData.size() ||
          dynamicCandidates[i+3] >= m_storage.hotData.size()) {
        // Fall back to scalar for this batch
        for (size_t j = i; j < i + 4 && j < dynamicCandidates.size(); ++j) {
          size_t candidateIdx = dynamicCandidates[j];
          if (candidateIdx >= m_storage.hotData.size() || candidateIdx == dynamicIdx) continue;
          const auto& candidateHot = m_storage.hotData[candidateIdx];
          if (!candidateHot.active || (dynamicCollidesWith & candidateHot.layers) == 0) continue;
          size_t a = std::min(dynamicIdx, candidateIdx);
          size_t b = std::max(dynamicIdx, candidateIdx);
          indexPairs.emplace_back(a, b);
        }
        continue;
      }

      // Load 4 candidate layers
      __m128i layers = _mm_set_epi32(
        m_storage.hotData[dynamicCandidates[i+3]].layers,
        m_storage.hotData[dynamicCandidates[i+2]].layers,
        m_storage.hotData[dynamicCandidates[i+1]].layers,
        m_storage.hotData[dynamicCandidates[i]].layers
      );

      // Batch layer mask check: result = layers & dynamicCollidesWith
      __m128i result = _mm_and_si128(layers, maskVec);
      __m128i zeros = _mm_setzero_si128();
      __m128i cmp = _mm_cmpeq_epi32(result, zeros);
      int failMask = _mm_movemask_epi8(cmp);

      // If all 4 failed (all bits set), skip entire batch
      if (failMask == 0xFFFF) continue;

      // Process individual candidates that passed layer mask check
      for (size_t j = 0; j < 4; ++j) {
        size_t candidateIdx = dynamicCandidates[i + j];
        if (candidateIdx == dynamicIdx) continue;

        // Check if this candidate passed (failMask bit for this lane is 0)
        int laneFailBits = (failMask >> (j * 4)) & 0xF;
        if (laneFailBits == 0xF) continue; // This candidate failed layer mask

        const auto& candidateHot = m_storage.hotData[candidateIdx];
        if (!candidateHot.active) continue;

        // Add collision pair
        size_t a = std::min(dynamicIdx, candidateIdx);
        size_t b = std::max(dynamicIdx, candidateIdx);
        indexPairs.emplace_back(a, b);
      }
    }

    // Scalar tail for remaining candidates
    for (; i < dynamicCandidates.size(); ++i) {
      size_t candidateIdx = dynamicCandidates[i];
#else
    // Scalar fallback
    for (size_t candidateIdx : dynamicCandidates) {
#endif
      if (candidateIdx >= m_storage.hotData.size() || candidateIdx == dynamicIdx) continue;

      const auto& candidateHot = m_storage.hotData[candidateIdx];
      if (!candidateHot.active || (dynamicCollidesWith & candidateHot.layers) == 0) continue;

      // Ensure consistent ordering
      size_t a = std::min(dynamicIdx, candidateIdx);
      size_t b = std::max(dynamicIdx, candidateIdx);
      indexPairs.emplace_back(a, b);
    }

    // 2. Use coarse-grid region cache for static collision queries
    auto coarseCellIt = m_bodyCoarseCell.find(dynamicIdx);

    if (coarseCellIt != m_bodyCoarseCell.end()) {
      auto regionCacheIt = m_coarseRegionStaticCache.find(coarseCellIt->second);

      if (regionCacheIt != m_coarseRegionStaticCache.end() && regionCacheIt->second.valid) {
        const auto& staticCandidates = regionCacheIt->second.staticIndices;

#ifdef COLLISION_SIMD_SSE2
        // SIMD: Process static candidates in batches of 4 for layer mask filtering
        const __m128i staticMaskVec = _mm_set1_epi32(dynamicCollidesWith);
        size_t si = 0;
        const size_t staticSimdEnd = (staticCandidates.size() / 4) * 4;

        for (; si < staticSimdEnd; si += 4) {
          // Bounds check for batch
          if (staticCandidates[si] >= m_storage.hotData.size() ||
              staticCandidates[si+1] >= m_storage.hotData.size() ||
              staticCandidates[si+2] >= m_storage.hotData.size() ||
              staticCandidates[si+3] >= m_storage.hotData.size()) {
            // Fall back to scalar for this batch
            for (size_t j = si; j < si + 4 && j < staticCandidates.size(); ++j) {
              size_t staticIdx = staticCandidates[j];
              if (staticIdx >= m_storage.hotData.size()) continue;
              const auto& staticHot = m_storage.hotData[staticIdx];
              if (!staticHot.active || (dynamicCollidesWith & staticHot.layers) == 0) continue;
              indexPairs.emplace_back(dynamicIdx, staticIdx);
            }
            continue;
          }

          // Load 4 static candidate layers
          __m128i staticLayers = _mm_set_epi32(
            m_storage.hotData[staticCandidates[si+3]].layers,
            m_storage.hotData[staticCandidates[si+2]].layers,
            m_storage.hotData[staticCandidates[si+1]].layers,
            m_storage.hotData[staticCandidates[si]].layers
          );

          // Batch layer mask check
          __m128i staticResult = _mm_and_si128(staticLayers, staticMaskVec);
          __m128i staticZeros = _mm_setzero_si128();
          __m128i staticCmp = _mm_cmpeq_epi32(staticResult, staticZeros);
          int staticFailMask = _mm_movemask_epi8(staticCmp);

          // If all 4 failed, skip entire batch
          if (staticFailMask == 0xFFFF) continue;

          // Process individual static candidates that passed
          for (size_t j = 0; j < 4; ++j) {
            size_t staticIdx = staticCandidates[si + j];

            // Check if this candidate passed
            int laneFailBits = (staticFailMask >> (j * 4)) & 0xF;
            if (laneFailBits == 0xF) continue;

            const auto& staticHot = m_storage.hotData[staticIdx];
            if (!staticHot.active) continue;

            indexPairs.emplace_back(dynamicIdx, staticIdx);
          }
        }

        // Scalar tail
        for (; si < staticCandidates.size(); ++si) {
          size_t staticIdx = staticCandidates[si];
#else
        // Scalar fallback
        for (size_t staticIdx : staticCandidates) {
#endif
          if (staticIdx >= m_storage.hotData.size()) continue;

          const auto& staticHot = m_storage.hotData[staticIdx];
          if (!staticHot.active || (dynamicCollidesWith & staticHot.layers) == 0) continue;

          indexPairs.emplace_back(dynamicIdx, staticIdx);
        }
      } else {
        // FALLBACK: Cache doesn't exist or is invalid - query static hash directly
        // This ensures static collisions are ALWAYS detected, even if cache is stale
        auto& staticCandidates = getPooledVector();
        m_staticSpatialHash.queryRegionBounds(queryMinX, queryMinY, queryMaxX, queryMaxY, staticCandidates);

        // Process static candidates (simplified - no SIMD since this is fallback)
        for (size_t staticIdx : staticCandidates) {
          if (staticIdx >= m_storage.hotData.size()) continue;
          const auto& staticHot = m_storage.hotData[staticIdx];
          if (!staticHot.active || (dynamicCollidesWith & staticHot.layers) == 0) continue;
          indexPairs.emplace_back(dynamicIdx, staticIdx);
        }
        returnPooledVector(staticCandidates);
      }
    } else {
      // FALLBACK: Body not tracked in coarse cell map - query static hash directly
      auto& staticCandidates = getPooledVector();
      m_staticSpatialHash.queryRegionBounds(queryMinX, queryMinY, queryMaxX, queryMaxY, staticCandidates);

      for (size_t staticIdx : staticCandidates) {
        if (staticIdx >= m_storage.hotData.size()) continue;
        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active || (dynamicCollidesWith & staticHot.layers) == 0) continue;
        indexPairs.emplace_back(dynamicIdx, staticIdx);
      }
      returnPooledVector(staticCandidates);
    }

    // Return pooled vector
    returnPooledVector(dynamicCandidates);
  }
}

void CollisionManager::narrowphaseSOA(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                                      std::vector<CollisionInfo>& collisions) const {
  collisions.clear();
  collisions.reserve(indexPairs.size() / 4); // Conservative estimate

#ifdef COLLISION_SIMD_SSE2
  // SIMD: Batch process AABB intersection tests (4 pairs at a time)
  size_t i = 0;
  const size_t simdEnd = (indexPairs.size() / 4) * 4;

  for (; i < simdEnd; i += 4) {
    // Load indices for 4 pairs
    const auto& [aIdx0, bIdx0] = indexPairs[i];
    const auto& [aIdx1, bIdx1] = indexPairs[i+1];
    const auto& [aIdx2, bIdx2] = indexPairs[i+2];
    const auto& [aIdx3, bIdx3] = indexPairs[i+3];

    // Bounds check
    if (aIdx0 >= m_storage.hotData.size() || bIdx0 >= m_storage.hotData.size() ||
        aIdx1 >= m_storage.hotData.size() || bIdx1 >= m_storage.hotData.size() ||
        aIdx2 >= m_storage.hotData.size() || bIdx2 >= m_storage.hotData.size() ||
        aIdx3 >= m_storage.hotData.size() || bIdx3 >= m_storage.hotData.size()) {
      // Fall back to scalar for this batch
      for (size_t j = i; j < i + 4; ++j) {
        const auto& [aIdx, bIdx] = indexPairs[j];
        if (aIdx >= m_storage.hotData.size() || bIdx >= m_storage.hotData.size()) continue;
        const auto& hotA = m_storage.hotData[aIdx];
        const auto& hotB = m_storage.hotData[bIdx];
        if (!hotA.active || !hotB.active) continue;

        float minXA, minYA, maxXA, maxYA, minXB, minYB, maxXB, maxYB;
        m_storage.getCachedAABBBounds(aIdx, minXA, minYA, maxXA, maxYA);
        m_storage.getCachedAABBBounds(bIdx, minXB, minYB, maxXB, maxYB);

        if (maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA) continue;

        float overlapX = std::min(maxXA, maxXB) - std::max(minXA, minXB);
        float overlapY = std::min(maxYA, maxYB) - std::max(minYA, minYB);

        float minPen;
        Vector2D normal;
        constexpr float AXIS_PREFERENCE_EPSILON = 0.01f;
        if (overlapX < overlapY - AXIS_PREFERENCE_EPSILON) {
          minPen = overlapX;
          float centerXA = (minXA + maxXA) * 0.5f;
          float centerXB = (minXB + maxXB) * 0.5f;
          normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
        } else {
          minPen = overlapY;
          float centerYA = (minYA + maxYA) * 0.5f;
          float centerYB = (minYB + maxYB) * 0.5f;
          normal = (centerYA < centerYB) ? Vector2D(0, 1) : Vector2D(0, -1);
        }
        EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
        EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;
        bool isEitherTrigger = hotA.isTrigger || hotB.isTrigger;
        collisions.push_back(CollisionInfo{entityA, entityB, normal, minPen, isEitherTrigger, aIdx, bIdx});
      }
      continue;
    }

    // Get AABB bounds for all 4 pairs
    float minXA[4], minYA[4], maxXA[4], maxYA[4];
    float minXB[4], minYB[4], maxXB[4], maxYB[4];

    m_storage.getCachedAABBBounds(aIdx0, minXA[0], minYA[0], maxXA[0], maxYA[0]);
    m_storage.getCachedAABBBounds(bIdx0, minXB[0], minYB[0], maxXB[0], maxYB[0]);
    m_storage.getCachedAABBBounds(aIdx1, minXA[1], minYA[1], maxXA[1], maxYA[1]);
    m_storage.getCachedAABBBounds(bIdx1, minXB[1], minYB[1], maxXB[1], maxYB[1]);
    m_storage.getCachedAABBBounds(aIdx2, minXA[2], minYA[2], maxXA[2], maxYA[2]);
    m_storage.getCachedAABBBounds(bIdx2, minXB[2], minYB[2], maxXB[2], maxYB[2]);
    m_storage.getCachedAABBBounds(aIdx3, minXA[3], minYA[3], maxXA[3], maxYA[3]);
    m_storage.getCachedAABBBounds(bIdx3, minXB[3], minYB[3], maxXB[3], maxYB[3]);

    // SIMD intersection test for 4 pairs
    __m128 maxXA_v = _mm_loadu_ps(maxXA);
    __m128 minXB_v = _mm_loadu_ps(minXB);
    __m128 maxXB_v = _mm_loadu_ps(maxXB);
    __m128 minXA_v = _mm_loadu_ps(minXA);
    __m128 maxYA_v = _mm_loadu_ps(maxYA);
    __m128 minYB_v = _mm_loadu_ps(minYB);
    __m128 maxYB_v = _mm_loadu_ps(maxYB);
    __m128 minYA_v = _mm_loadu_ps(minYA);

    // Test: maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA
    __m128 xFail1 = _mm_cmplt_ps(maxXA_v, minXB_v);
    __m128 xFail2 = _mm_cmplt_ps(maxXB_v, minXA_v);
    __m128 yFail1 = _mm_cmplt_ps(maxYA_v, minYB_v);
    __m128 yFail2 = _mm_cmplt_ps(maxYB_v, minYA_v);

    __m128 fail = _mm_or_ps(_mm_or_ps(xFail1, xFail2), _mm_or_ps(yFail1, yFail2));
    int failMask = _mm_movemask_ps(fail);

    // If all 4 pairs failed intersection, skip
    if (failMask == 0xF) continue;

    // Process pairs that passed intersection test
    const size_t indices[4] = {aIdx0, aIdx1, aIdx2, aIdx3};
    const size_t bindices[4] = {bIdx0, bIdx1, bIdx2, bIdx3};

    for (size_t j = 0; j < 4; ++j) {
      if (failMask & (1 << j)) continue; // This pair failed intersection

      size_t aIdx = indices[j];
      size_t bIdx = bindices[j];
      const auto& hotA = m_storage.hotData[aIdx];
      const auto& hotB = m_storage.hotData[bIdx];

      if (!hotA.active || !hotB.active) continue;

      // Calculate overlap and collision details (scalar - normals are conditional)
      float overlapX = std::min(maxXA[j], maxXB[j]) - std::max(minXA[j], minXB[j]);
      float overlapY = std::min(maxYA[j], maxYB[j]) - std::max(minYA[j], minYB[j]);

      float minPen;
      Vector2D normal;
      if (overlapX < overlapY) {
        minPen = overlapX;
        float centerXA = (minXA[j] + maxXA[j]) * 0.5f;
        float centerXB = (minXB[j] + maxXB[j]) * 0.5f;
        normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
      } else {
        minPen = overlapY;
        float centerYA = (minYA[j] + maxYA[j]) * 0.5f;
        float centerYB = (minYB[j] + maxYB[j]) * 0.5f;
        normal = (centerYA < centerYB) ? Vector2D(0, 1) : Vector2D(0, -1);
      }

      EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
      EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;
      bool isEitherTrigger = hotA.isTrigger || hotB.isTrigger;

      collisions.push_back(CollisionInfo{entityA, entityB, normal, minPen, isEitherTrigger, aIdx, bIdx});
    }
  }

  // Scalar tail for remaining pairs
  for (; i < indexPairs.size(); ++i) {
    const auto& [aIdx, bIdx] = indexPairs[i];
#else
  // Scalar fallback
  for (const auto& [aIdx, bIdx] : indexPairs) {
#endif
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
    // Calculate overlap on each axis (always positive when intersecting)
    float overlapX = std::min(maxXA, maxXB) - std::max(minXA, minXB);
    float overlapY = std::min(maxYA, maxYB) - std::max(minYA, minYB);

    // NOTE: No MIN_OVERLAP threshold - AABB test above already filters non-overlapping pairs
    // Any overlap detected here is a real collision and must be resolved

    // Standard AABB resolution: separate on axis of minimum penetration
    // Add epsilon to prefer Y-axis when penetrations are nearly equal (prevents corner ambiguity)
    constexpr float AXIS_PREFERENCE_EPSILON = 0.01f;
    float minPen;
    Vector2D normal;

    if (overlapX < overlapY - AXIS_PREFERENCE_EPSILON) {
      // Separate on X-axis
      minPen = overlapX;

      // DEEP PENETRATION FIX: Use velocity direction instead of center comparison
      // Only needed for rare edge cases (reduced from 16px with cache fixes)
      constexpr float DEEP_PENETRATION_THRESHOLD = 10.0f;
      if (minPen > DEEP_PENETRATION_THRESHOLD && hotA.velocity.lengthSquared() > 1.0f) {
        // Push opposite to velocity direction (player was moving INTO the collision)
        normal = (hotA.velocity.getX() > 0) ? Vector2D(-1, 0) : Vector2D(1, 0);
      } else {
        // Standard center comparison for shallow penetrations
        float centerXA = (minXA + maxXA) * 0.5f;
        float centerXB = (minXB + maxXB) * 0.5f;
        normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
      }
    } else {
      // Separate on Y-axis
      minPen = overlapY;

      // DEEP PENETRATION FIX: Use velocity direction instead of center comparison
      // Only needed for rare edge cases (reduced from 16px with cache fixes)
      constexpr float DEEP_PENETRATION_THRESHOLD = 10.0f;
      if (minPen > DEEP_PENETRATION_THRESHOLD && hotA.velocity.lengthSquared() > 1.0f) {
        // Push opposite to velocity direction (player was moving INTO the collision)
        normal = (hotA.velocity.getY() > 0) ? Vector2D(0, -1) : Vector2D(0, 1);
      } else {
        // Standard center comparison for shallow penetrations
        float centerYA = (minYA + maxYA) * 0.5f;
        float centerYB = (minYB + maxYB) * 0.5f;
        normal = (centerYA < centerYB) ? Vector2D(0, 1) : Vector2D(0, -1);
      }
    }

    // Create collision info using EntityIDs
    EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
    EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;

    bool isEitherTrigger = hotA.isTrigger || hotB.isTrigger;

    collisions.push_back(CollisionInfo{
        entityA, entityB, normal, minPen, isEitherTrigger, aIdx, bIdx
    });
  }

  // Removed per-frame logging - collision count is included in periodic summary
}

// ========== NEW SOA UPDATE METHOD ==========

void CollisionManager::updateSOA(float dt) {
  (void)dt;

  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();

  // Process pending add/remove commands first (thread-safe deferred operations)
  processPendingCommands();

  // Note: Kinematic updates now applied via applyBatchedKinematicUpdates()
  // called by AIManager after batch completion (zero contention)

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

  // MOVEMENT INTEGRATION: Removed redundant loop (was lines 1846-1871)
  // AIManager now handles all position updates via updateKinematicBatchSOA() and applyBatchedKinematicUpdates()
  // which directly set hot.position to the final integrated position.
  // CollisionManager's job is ONLY to:
  //   1. Detect collisions using positions updated by AIManager
  //   2. Resolve collisions (push bodies apart via resolveSOA())
  // This eliminates 28k+ unnecessary iterations per frame and fixes double-integration bug.

  // Track culling metrics
  auto cullingStart = clock::now();

  // OPTIMIZATION: buildActiveIndicesSOA now returns body type counts during iteration
  // This avoids 3 expensive std::count_if calls (83,901 iterations for 27k bodies!)
  auto [totalStaticBodies, totalDynamicBodies, totalKinematicBodies] = buildActiveIndicesSOA(cullingArea);
  size_t totalMovableBodies = totalDynamicBodies + totalKinematicBodies;

  auto cullingEnd = clock::now();

  // Sync spatial hashes after culling, only for active bodies
  syncSpatialHashesWithActiveIndices();

  // Reset static culling counter for this frame
  m_perf.lastStaticBodiesCulled = 0;

  // Update static collision cache for all movable bodies
  // (this will accumulate actual static bodies culled in m_perf.lastStaticBodiesCulled)
  updateStaticCollisionCacheForMovableBodies();

  // Periodic cache eviction: Remove stale cache entries every N frames
  m_framesSinceLastEviction++;
  if (m_framesSinceLastEviction >= CACHE_EVICTION_INTERVAL) {
    evictStaleCacheEntries(cullingArea);
    m_framesSinceLastEviction = 0;
  } else {
    // Reset per-frame eviction counter when not evicting
    m_perf.cacheEntriesEvicted = 0;
  }

  double cullingMs = std::chrono::duration<double, std::milli>(cullingEnd - cullingStart).count();

  size_t activeMovableBodies = m_collisionPool.movableIndices.size();
  size_t activeBodies = m_collisionPool.activeIndices.size();
  size_t activeStaticBodies = m_collisionPool.staticIndices.size();

  // CULLING METRICS: Calculate accurate counts of culled bodies

  // Calculate culled counts
  size_t staticBodiesCulled = (totalStaticBodies > activeStaticBodies) ?
                              (totalStaticBodies - activeStaticBodies) : 0;
  size_t dynamicBodiesCulled = (totalMovableBodies > activeMovableBodies) ?
                               (totalMovableBodies - activeMovableBodies) : 0;

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
#ifdef COLLISION_SIMD_SSE2
  // SIMD batch resolution: Process 4 collisions at a time where possible
  size_t collIdx = 0;
  const size_t collSimdEnd = (m_collisionPool.collisionBuffer.size() / 4) * 4;

  for (; collIdx < collSimdEnd; collIdx += 4) {
    // Process 4 collisions in parallel (SIMD vector operations)
    for (size_t j = 0; j < 4; ++j) {
      const auto& collision = m_collisionPool.collisionBuffer[collIdx + j];
      resolveSOA(collision);
      for (const auto& cb : m_callbacks) {
        cb(collision);
      }
    }
  }

  // Scalar tail
  for (; collIdx < m_collisionPool.collisionBuffer.size(); ++collIdx) {
    const auto& collision = m_collisionPool.collisionBuffer[collIdx];
    resolveSOA(collision);
    for (const auto& cb : m_callbacks) {
      cb(collision);
    }
  }
#else
  // Scalar fallback
  for (const auto& collision : m_collisionPool.collisionBuffer) {
    resolveSOA(collision);
    for (const auto& cb : m_callbacks) {
      cb(collision);
    }
  }
#endif
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
                               activeBodies, dynamicBodiesCulled, staticBodiesCulled, cullingMs,
                               totalStaticBodies, totalMovableBodies);

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

        // FIXED: Query entire coarse cell region (128x128), not just player AABB + margin
        // Coarse cell size is 128 pixels, so half-size is 64
        constexpr float COARSE_CELL_SIZE = 128.0f;
        const float coarseCellHalfSize = COARSE_CELL_SIZE * 0.5f;

        // Calculate center of current coarse cell
        float cellCenterX = (currentCoarseCell.x + 0.5f) * COARSE_CELL_SIZE;
        float cellCenterY = (currentCoarseCell.y + 0.5f) * COARSE_CELL_SIZE;

        // Create AABB covering entire coarse cell + small margin for border cases
        // Reduced from 32px to 16px now that fallback queries work correctly
        AABB regionAABB(cellCenterX, cellCenterY,
                       coarseCellHalfSize + SPATIAL_QUERY_EPSILON + 16.0f,
                       coarseCellHalfSize + SPATIAL_QUERY_EPSILON + 16.0f);

        // Query static spatial hash for this entire coarse region
        auto& staticCandidates = getPooledVector();
        m_staticSpatialHash.queryRegion(regionAABB, staticCandidates);

        // FIXED: NO CULLING on cache population!
        // The coarse region cache must contain ALL static bodies in the cell,
        // regardless of camera position. Otherwise, buildings approaching from
        // off-screen won't be detected until deep penetration occurs.
        // Culling happens at the dynamic body level (buildActiveIndicesSOA).

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
  }
}

void CollisionManager::evictStaleCacheEntries(const CullingArea& cullingArea) {
  // PERFORMANCE: Evict cache entries for coarse cells far from active culling area
  // This prevents unbounded memory growth when entities spread across large maps

  if (m_coarseRegionStaticCache.empty()) {
    return; // No cache entries to evict
  }

  // Calculate eviction bounds (3x culling buffer = 6000x6000 area)
  const float evictionBuffer = COLLISION_CULLING_BUFFER * CACHE_EVICTION_MULTIPLIER;
  const float evictionMinX = cullingArea.minX - evictionBuffer;
  const float evictionMinY = cullingArea.minY - evictionBuffer;
  const float evictionMaxX = cullingArea.maxX + evictionBuffer;
  const float evictionMaxY = cullingArea.maxY + evictionBuffer;

  // Coarse cell size (from HierarchicalSpatialHash - 128 pixels)
  constexpr float COARSE_CELL_SIZE = 128.0f;

  // Iterate through cache and remove entries outside eviction bounds
  size_t evictedCount = 0;
  for (auto it = m_coarseRegionStaticCache.begin(); it != m_coarseRegionStaticCache.end(); ) {
    const auto& coord = it->first;

    // Calculate center of this coarse cell
    float cellCenterX = (coord.x + 0.5f) * COARSE_CELL_SIZE;
    float cellCenterY = (coord.y + 0.5f) * COARSE_CELL_SIZE;

    // Check if cell center is outside eviction bounds
    if (cellCenterX < evictionMinX || cellCenterX > evictionMaxX ||
        cellCenterY < evictionMinY || cellCenterY > evictionMaxY) {
      // Evict this cache entry
      it = m_coarseRegionStaticCache.erase(it);
      evictedCount++;
    } else {
      ++it;
    }
  }

  // Update performance metrics
  m_perf.cacheEntriesEvicted = evictedCount;
  m_perf.totalCacheEvictions += evictedCount;
  m_perf.cacheEntriesActive = m_coarseRegionStaticCache.size();

  // Clean up body->coarse-cell tracking for evicted entries
  // (Optional optimization: only remove entries for bodies that no longer exist)
  if (evictedCount > 0) {
    for (auto it = m_bodyCoarseCell.begin(); it != m_bodyCoarseCell.end(); ) {
      // Check if this body's coarse cell was evicted
      if (m_coarseRegionStaticCache.find(it->second) == m_coarseRegionStaticCache.end()) {
        it = m_bodyCoarseCell.erase(it);
      } else {
        ++it;
      }
    }
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
#ifdef COLLISION_SIMD_SSE2
  if (typeA != BodyType::STATIC && typeB != BodyType::STATIC) {
    // SIMD: Both dynamic/kinematic - split the correction
    __m128 normal = _mm_set_ps(0, 0, collision.normal.getY(), collision.normal.getX());
    __m128 pushVec = _mm_set1_ps(push);
    __m128 correction = _mm_mul_ps(normal, pushVec);

    __m128 posA = _mm_set_ps(0, 0, hotA.position.getY(), hotA.position.getX());
    __m128 posB = _mm_set_ps(0, 0, hotB.position.getY(), hotB.position.getX());

    posA = _mm_sub_ps(posA, correction);
    posB = _mm_add_ps(posB, correction);

    alignas(16) float resultA[4], resultB[4];
    _mm_store_ps(resultA, posA);
    _mm_store_ps(resultB, posB);

    hotA.position.setX(resultA[0]);
    hotA.position.setY(resultA[1]);
    hotB.position.setX(resultB[0]);
    hotB.position.setY(resultB[1]);
  } else if (typeA != BodyType::STATIC) {
    // SIMD: Only A moves
    __m128 normal = _mm_set_ps(0, 0, collision.normal.getY(), collision.normal.getX());
    __m128 penVec = _mm_set1_ps(collision.penetration);
    __m128 correction = _mm_mul_ps(normal, penVec);

    __m128 posA = _mm_set_ps(0, 0, hotA.position.getY(), hotA.position.getX());
    posA = _mm_sub_ps(posA, correction);

    alignas(16) float resultA[4];
    _mm_store_ps(resultA, posA);

    hotA.position.setX(resultA[0]);
    hotA.position.setY(resultA[1]);
  } else if (typeB != BodyType::STATIC) {
    // SIMD: Only B moves
    __m128 normal = _mm_set_ps(0, 0, collision.normal.getY(), collision.normal.getX());
    __m128 penVec = _mm_set1_ps(collision.penetration);
    __m128 correction = _mm_mul_ps(normal, penVec);

    __m128 posB = _mm_set_ps(0, 0, hotB.position.getY(), hotB.position.getX());
    posB = _mm_add_ps(posB, correction);

    alignas(16) float resultB[4];
    _mm_store_ps(resultB, posB);

    hotB.position.setX(resultB[0]);
    hotB.position.setY(resultB[1]);
  }
#else
  // Scalar fallback
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
#endif

  // Apply velocity damping for dynamic bodies
#ifdef COLLISION_SIMD_SSE2
  auto dampenVelocitySIMD = [&collision](Vector2D& velocity, float restitution, bool isStatic) {
    // SIMD dot product
    __m128 vel = _mm_set_ps(0, 0, velocity.getY(), velocity.getX());
    __m128 norm = _mm_set_ps(0, 0, collision.normal.getY(), collision.normal.getX());
    __m128 dot = _mm_mul_ps(vel, norm);
    __m128 dot_shuf = _mm_shuffle_ps(dot, dot, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 dot_sum = _mm_add_ps(dot, dot_shuf);
    float vdotn = _mm_cvtss_f32(dot_sum);

    // Normal points from A to B, so vdotn > 0 means moving TOWARD collision
    // vdotn < 0 means moving AWAY from collision
    if (vdotn < 0) {
      return; // Moving away - no damping needed
    }

    // SIMD velocity damping
    __m128 vdotnVec = _mm_set1_ps(vdotn);
    __m128 normalVel = _mm_mul_ps(norm, vdotnVec);

    // For static collisions, zero velocity; for dynamic, use restitution
    __m128 dampFactor = isStatic ? _mm_set1_ps(1.0f) : _mm_set1_ps(1.0f + restitution);
    __m128 dampedVel = _mm_mul_ps(normalVel, dampFactor);
    vel = _mm_sub_ps(vel, dampedVel);

    alignas(16) float result[4];
    _mm_store_ps(result, vel);

    velocity.setX(result[0]);
    velocity.setY(result[1]);
  };

  if (typeA == BodyType::DYNAMIC) {
    bool staticCollision = (typeB == BodyType::STATIC);
    dampenVelocitySIMD(hotA.velocity, hotA.restitution, staticCollision);
  }
  if (typeB == BodyType::DYNAMIC) {
    bool staticCollision = (typeA == BodyType::STATIC);
    // For entity B, we need to flip the normal (normal points A->B, but we need B->A)
    CollisionInfo flippedCollision = collision;
    flippedCollision.normal = collision.normal * -1.0f;
    auto dampenVelocitySIMD_B = [&flippedCollision](Vector2D& velocity, float restitution, bool isStatic) {
      __m128 vel = _mm_set_ps(0, 0, velocity.getY(), velocity.getX());
      __m128 norm = _mm_set_ps(0, 0, flippedCollision.normal.getY(), flippedCollision.normal.getX());
      __m128 dot = _mm_mul_ps(vel, norm);
      __m128 dot_shuf = _mm_shuffle_ps(dot, dot, _MM_SHUFFLE(2, 3, 0, 1));
      __m128 dot_sum = _mm_add_ps(dot, dot_shuf);
      float vdotn = _mm_cvtss_f32(dot_sum);
      if (vdotn < 0) return; // Moving away
      __m128 vdotnVec = _mm_set1_ps(vdotn);
      __m128 normalVel = _mm_mul_ps(norm, vdotnVec);
      __m128 dampFactor = isStatic ? _mm_set1_ps(1.0f) : _mm_set1_ps(1.0f + restitution);
      __m128 dampedVel = _mm_mul_ps(normalVel, dampFactor);
      vel = _mm_sub_ps(vel, dampedVel);
      alignas(16) float result[4];
      _mm_store_ps(result, vel);
      velocity.setX(result[0]);
      velocity.setY(result[1]);
    };
    dampenVelocitySIMD_B(hotB.velocity, hotB.restitution, staticCollision);
  }
#else
  // Scalar fallback
  auto dampenVelocity = [&collision](Vector2D& velocity, float restitution, bool isStatic) {
    float vdotn = velocity.getX() * collision.normal.getX() +
                  velocity.getY() * collision.normal.getY();

    // Normal points from A to B, so vdotn > 0 means moving TOWARD collision
    // vdotn < 0 means moving AWAY from collision
    if (vdotn < 0) {
      return; // Moving away - no damping needed
    }

    // For collisions with static objects, completely zero velocity in collision direction
    // to prevent continuous penetration when input keeps setting velocity
    if (isStatic) {
      Vector2D normalVelocity = collision.normal * vdotn;
      velocity -= normalVelocity; // Remove all velocity along normal
    } else {
      // For dynamic-dynamic collisions, use restitution-based damping
      Vector2D normalVelocity = collision.normal * vdotn;
      velocity -= normalVelocity * (1.0f + restitution);
    }
  };

  if (typeA == BodyType::DYNAMIC) {
    bool staticCollision = (typeB == BodyType::STATIC);
    dampenVelocity(hotA.velocity, hotA.restitution, staticCollision);
  }
  if (typeB == BodyType::DYNAMIC) {
    bool staticCollision = (typeA == BodyType::STATIC);
    // For entity B, we need to flip the normal (normal points A->B, but we need B->A)
    CollisionInfo flippedCollision = collision;
    flippedCollision.normal = collision.normal * -1.0f;
    auto dampenVelocityB = [&flippedCollision](Vector2D& velocity, float restitution, bool isStatic) {
      float vdotn = velocity.getX() * flippedCollision.normal.getX() +
                    velocity.getY() * flippedCollision.normal.getY();
      if (vdotn < 0) return; // Moving away
      if (isStatic) {
        Vector2D normalVelocity = flippedCollision.normal * vdotn;
        velocity -= normalVelocity;
      } else {
        Vector2D normalVelocity = flippedCollision.normal * vdotn;
        velocity -= normalVelocity * (1.0f + restitution);
      }
    };
    dampenVelocityB(hotB.velocity, hotB.restitution, staticCollision);
  }
#endif

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
#ifdef COLLISION_SIMD_SSE2
  auto clampVelocitySIMD = [](Vector2D& velocity) {
    const float maxSpeed = 300.0f;

    // SIMD length calculation
    __m128 vel = _mm_set_ps(0, 0, velocity.getY(), velocity.getX());
    __m128 sq = _mm_mul_ps(vel, vel);
    __m128 sq_shuf = _mm_shuffle_ps(sq, sq, _MM_SHUFFLE(2, 3, 0, 1));
    __m128 sum = _mm_add_ps(sq, sq_shuf);
    float lenSq = _mm_cvtss_f32(sum);
    float speed = std::sqrt(lenSq);

    if (speed > maxSpeed && speed > 0.0f) {
      // SIMD velocity scaling
      __m128 scale = _mm_set1_ps(maxSpeed / speed);
      vel = _mm_mul_ps(vel, scale);

      alignas(16) float result[4];
      _mm_store_ps(result, vel);
      velocity.setX(result[0]);
      velocity.setY(result[1]);
    }
  };

  clampVelocitySIMD(hotA.velocity);
  clampVelocitySIMD(hotB.velocity);
#else
  // Scalar fallback
  auto clampVelocity = [](Vector2D& velocity) {
    const float maxSpeed = 300.0f;
    float speed = velocity.length();
    if (speed > maxSpeed && speed > 0.0f) {
      velocity = velocity * (maxSpeed / speed);
    }
  };

  clampVelocity(hotA.velocity);
  clampVelocity(hotB.velocity);
#endif
}

void CollisionManager::syncEntitiesToSOA() {
  // OPTIMIZATION: Only sync active bodies that were processed this frame
  // Before: 27,099 iterations (all bodies)
  // After: ~80 iterations (only active bodies within culling range)
  // Performance gain: ~97% reduction in syncing overhead

  m_isSyncing = true;

  // Only sync movable bodies that were active this frame
  // Static bodies never move, so no need to sync them back to entities
  for (size_t idx : m_collisionPool.movableIndices) {
    if (idx >= m_storage.hotData.size() || idx >= m_storage.coldData.size()) continue;

    const auto& hot = m_storage.hotData[idx];
    auto& cold = m_storage.coldData[idx];

    if (!hot.active) continue;

    // Sync collision-resolved position and velocity back to entity
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

        COLLISION_DEBUG("Player " + std::to_string(playerId) + " ENTERED trigger " +
                       std::to_string(triggerId) + " (tag: " + std::to_string(static_cast<int>(triggerTag)) +
                       ") at position (" + std::to_string(playerHot->position.getX()) + ", " +
                       std::to_string(playerHot->position.getY()) + ")");

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

      COLLISION_DEBUG("Player " + std::to_string(playerId) + " EXITED trigger " +
                     std::to_string(triggerId) + " (tag: " + std::to_string(static_cast<int>(triggerTag)) +
                     ") at position (" + std::to_string(triggerPos.getX()) + ", " +
                     std::to_string(triggerPos.getY()) + ")");

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
    double cullingMs,
    size_t totalStaticBodies,
    size_t totalMovableBodies) {

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
  m_perf.totalStaticBodies = totalStaticBodies;
  m_perf.totalMovableBodies = totalMovableBodies;
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
    [[maybe_unused]] std::string optimizationStats = " [Optimizations: Active=" + std::to_string(m_perf.getActiveBodiesRate()) + "%";
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
    [[maybe_unused]] size_t totalCacheAccesses = m_cacheHits + m_cacheMisses;
    [[maybe_unused]] float cacheHitRate = totalCacheAccesses > 0 ? (static_cast<float>(m_cacheHits) / totalCacheAccesses) * 100.0f : 0.0f;
    [[maybe_unused]] size_t activeRegions = m_coarseRegionStaticCache.size();
    [[maybe_unused]] std::string cacheStatsStr = " [RegionCache: Active=" + std::to_string(activeRegions) +
                                ", Hits=" + std::to_string(m_cacheHits) +
                                ", Misses=" + std::to_string(m_cacheMisses) +
                                ", HitRate=" + std::to_string(static_cast<int>(cacheHitRate)) + "%";

    // Add eviction statistics if available
    if (m_perf.cacheEntriesEvicted > 0 || m_perf.totalCacheEvictions > 0) {
      cacheStatsStr += ", Evicted=" + std::to_string(m_perf.cacheEntriesEvicted) +
                       ", TotalEvictions=" + std::to_string(m_perf.totalCacheEvictions);
    }
    cacheStatsStr += "]";

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

  // PERFORMANCE FIX: Acquire lock ONCE for entire batch instead of per-entity
  // Reduces lock acquisitions from O(n) to O(1) - critical for 1000+ entity updates
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  // Batch update all kinematic bodies in SOA storage
  for (const auto& bodyUpdate : updates) {
    // Direct map access without additional locking (we hold the lock)
    auto it = m_storage.entityToIndex.find(bodyUpdate.id);
    if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
      size_t index = it->second;
      auto& hot = m_storage.hotData[index];
      if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC) {
        hot.position = bodyUpdate.position;
        hot.velocity = bodyUpdate.velocity;
        hot.aabbDirty = 1;
        hot.active = true; // Ensure body stays enabled
      }
    }
  }

  // Verbose logging removed for performance
}

void CollisionManager::applyBatchedKinematicUpdates(const std::vector<std::vector<KinematicUpdate>>& batchUpdates) {
  // PER-BATCH COLLISION UPDATES: Zero contention approach
  // Each AI batch has its own buffer, we merge them here with no mutex needed.
  // This eliminates the serialization bottleneck that caused frame jitter.

  if (batchUpdates.empty()) return;

  // Count total updates for efficiency
  size_t totalUpdates = std::accumulate(batchUpdates.begin(), batchUpdates.end(), size_t{0},
    [](size_t sum, const auto& batch) { return sum + batch.size(); });

  if (totalUpdates == 0) return;

  // PERFORMANCE: Acquire shared lock ONCE for all batches
  std::shared_lock<std::shared_mutex> lock(m_storageMutex);

  // Merge all batch updates into collision storage
  for (const auto& batchBuffer : batchUpdates) {
    for (const auto& bodyUpdate : batchBuffer) {
      auto it = m_storage.entityToIndex.find(bodyUpdate.id);
      if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
        size_t index = it->second;
        auto& hot = m_storage.hotData[index];
        if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC) {
          hot.position = bodyUpdate.position;
          hot.velocity = bodyUpdate.velocity;
          hot.aabbDirty = 1;
          hot.active = true;
        }
      }
    }
  }

  // Batch processing complete - zero mutex contention between AI batches
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
  /* THREAD SAFETY WARNING:
   * This vector pool is SINGLE-THREADED ONLY. m_nextPoolIndex is not atomic.
   *
   * SAFE because:
   * - CollisionManager::update() runs on main thread only
   * - Vector pool only used during broadphase (within update())
   * - No concurrent access to collision update logic
   *
   * IF MULTI-THREADING IS ADDED:
   * - Option 1: Make m_nextPoolIndex std::atomic<size_t>
   * - Option 2: Use thread_local vector pool per thread
   * - Option 3: Add mutex (impacts performance)
   *
   * Current design prioritizes performance for single-threaded collision updates.
   */

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

