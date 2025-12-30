/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/* ARCHITECTURAL NOTE: CollisionManager Threading Strategy
 *
 * CollisionManager uses HYBRID threading:
 * - Broadphase: Single-threaded (complex spatial hash queries)
 * - Narrowphase: Multi-threaded via WorkerBudget (pure computation, 4-wide SIMD)
 *
 * Narrowphase parallelization rationale:
 * - Pure computation (AABB intersection tests, no shared state)
 * - Read-only input (indexPairs from broadphase)
 * - Per-batch output buffers eliminate lock contention
 * - Nested 4-wide SIMD processing preserved within each batch
 * - Threshold (MIN_PAIRS_FOR_THREADING) prevents overhead for small workloads
 *
 * Broadphase remains single-threaded because:
 * - Complex spatial hash synchronization overhead
 * - Already highly optimized (SIMD, spatial hashing, culling, cache hits)
 * - Broadphase is not the bottleneck (narrowphase is computational)
 *
 * Performance: Handles 27K+ bodies @ 60 FPS on Apple Silicon.
 * Narrowphase threading provides 2-4x speedup for large workloads (10K+ bodies).
 */

#include "managers/CollisionManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/WorldEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/UniqueID.hpp"
#include "utils/SIMDMath.hpp"
#include "world/WorldData.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <numeric>
#include <queue>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

// Use SIMD abstraction layer
using namespace HammerEngine::SIMD;

using ::EventManager;
using ::EventTypeId;
using ::TileChangedEvent;
using ::WorldGeneratedEvent;
using ::WorldLoadedEvent;
using ::WorldManager;
using ::WorldUnloadedEvent;
using ::HammerEngine::ObstacleType;

// Building collision body limits
constexpr uint16_t MAX_BUILDING_SUB_BODIES = 1000;

// Fibonacci hash for pair<int,int> - excellent for sequential tile coordinates
// Uses golden ratio multiplier for even distribution of consecutive values
struct PairHash {
  size_t operator()(const std::pair<int, int>& p) const noexcept {
    constexpr uint64_t kFibMult = 11400714819323198485ULL;  // 2^64 / φ (golden ratio)
    uint64_t h1 = static_cast<uint64_t>(static_cast<uint32_t>(p.first)) * kFibMult;
    uint64_t h2 = static_cast<uint64_t>(static_cast<uint32_t>(p.second)) * kFibMult;
    return h1 ^ (h2 >> 1);  // Shift h2 to break symmetry (a,b) != (b,a)
  }
};

bool CollisionManager::init() {
  if (m_initialized)
    return true;
  m_storage.clear();
  subscribeWorldEvents();
  COLLISION_INFO("STORAGE LIFECYCLE: init() cleared SOA storage and spatial hash");

  // PERFORMANCE: Pre-allocate vector pool to prevent FPS dips from reallocations
  // Initialize vector pool (moved from lazy initialization in getPooledVector)
  m_vectorPool.clear();
  m_vectorPool.reserve(32);
  for (size_t i = 0; i < 16; ++i) {
    m_vectorPool.emplace_back();
    m_vectorPool.back().reserve(64); // Pre-allocate reasonable capacity
  }
  m_nextPoolIndex.store(0, std::memory_order_relaxed);

  // Pre-reserve reusable containers to avoid per-frame allocations
  m_currentTriggerPairsBuffer.reserve(1000);    // Typical trigger count
  // Note: pools.staticIndices is reserved by CollisionPool::ensureCapacity()

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

  COLLISION_INFO(std::format("STORAGE LIFECYCLE: clean() clearing {} SOA bodies",
                             m_storage.size()));


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
  size_t soaBodyCount = m_storage.size();
  COLLISION_INFO(std::format("STORAGE LIFECYCLE: prepareForStateTransition() clearing {} SOA bodies (dynamic + static)",
                             soaBodyCount));

  // Clear all collision bodies and spatial hashes
  m_storage.clear();
  m_staticSpatialHash.clear();
  m_dynamicSpatialHash.clear();

  // Clear collision buffers to prevent dangling references to deleted bodies
  m_collisionPool.resetFrame();

  // OPTIMIZATION: Clear persistent index tracking (regression fix for commit 768ad87)
  m_movableBodyIndices.clear();
  m_movableIndexSet.clear();
  m_playerBodyIndex = std::nullopt;

  // Re-initialize vector pool (must not leave it empty to prevent divide-by-zero in getPooledVector)
  m_vectorPool.clear();
  m_vectorPool.reserve(32);
  for (size_t i = 0; i < 16; ++i) {
    m_vectorPool.emplace_back();
    m_vectorPool.back().reserve(64);
  }
  m_nextPoolIndex.store(0, std::memory_order_relaxed);

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

  size_t finalBodyCount = m_storage.size();
  COLLISION_INFO(std::format("CollisionManager state transition complete - {} bodies remaining", finalBodyCount));
}

void CollisionManager::setWorldBounds(float minX, float minY, float maxX,
                                      float maxY) {
  const float cx = (minX + maxX) * 0.5f;
  const float cy = (minY + maxY) * 0.5f;
  const float hw = (maxX - minX) * 0.5f;
  const float hh = (maxY - minY) * 0.5f;
  m_worldBounds = AABB(cx, cy, hw, hh);

  COLLISION_DEBUG(std::format("World bounds set: [{},{}] - [{},{}]",
                              minX, minY, maxX, maxY));
}












EntityID CollisionManager::createTriggerArea(const AABB &aabb,
                                             HammerEngine::TriggerTag tag,
                                             uint32_t layerMask,
                                             uint32_t collideMask) {
  const EntityID id = HammerEngine::UniqueID::generate();
  const Vector2D center(aabb.center.getX(), aabb.center.getY());
  const Vector2D halfSize(aabb.halfSize.getX(), aabb.halfSize.getY());
  // addCollisionBodySOA creates EDM entry for STATIC bodies
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
  size_t skippedInterior = 0;
  constexpr float tileSize = HammerEngine::TILE_SIZE;
  const int h = static_cast<int>(world->grid.size());

  // Helper lambda to check if a tile is water (with bounds checking)
  auto isWaterTile = [&world, h](int tx, int ty) -> bool {
    if (ty < 0 || ty >= h) return false;
    const int rowWidth = static_cast<int>(world->grid[ty].size());
    if (tx < 0 || tx >= rowWidth) return false;
    return world->grid[ty][tx].isWater;
  };

  for (int y = 0; y < h; ++y) {
    const int w = static_cast<int>(world->grid[y].size());
    for (int x = 0; x < w; ++x) {
      const auto &tile = world->grid[y][x];
      if (!tile.isWater)
        continue;

      // OPTIMIZATION: Only create triggers for water tiles on the edge
      // A tile is an "edge" if any of its 4 neighbors is NOT water
      bool const isEdge = !isWaterTile(x - 1, y) ||  // Left neighbor
                    !isWaterTile(x + 1, y) ||  // Right neighbor
                    !isWaterTile(x, y - 1) ||  // Top neighbor
                    !isWaterTile(x, y + 1);    // Bottom neighbor

      if (!isEdge) {
        ++skippedInterior;
        continue;  // Skip interior water tiles - player can't enter from here
      }

      const float cx = x * tileSize + tileSize * 0.5f;
      const float cy = y * tileSize + tileSize * 0.5f;
      const AABB aabb(cx, cy, tileSize * 0.5f, tileSize * 0.5f);
      // Use a distinct prefix for triggers to avoid id collisions with static
      // colliders
      EntityID id = (static_cast<EntityID>(1ull) << 61) |
                    (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
                    static_cast<EntityID>(static_cast<uint32_t>(x));
      // Check if this water trigger already exists in SOA storage
      if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
        const Vector2D center(aabb.center.getX(), aabb.center.getY());
        const Vector2D halfSize(aabb.halfSize.getX(), aabb.halfSize.getY());
        addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                           CollisionLayer::Layer_Environment, 0xFFFFFFFFu,
                           true, static_cast<uint8_t>(tag));
        ++created;
      }
    }
  }

  COLLISION_DEBUG_IF(skippedInterior > 0,
      std::format("Water triggers: {} edge triggers created, {} interior tiles skipped",
                  created, skippedInterior));
  return created;
}


size_t CollisionManager::createStaticObstacleBodies() {
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return 0;

  size_t created = 0;
  constexpr float tileSize = HammerEngine::TILE_SIZE;
  const int h = static_cast<int>(world->grid.size());

  // Track which tiles we've already processed (O(1) lookup with unordered_set)
  std::unordered_set<std::pair<int, int>, PairHash> processedTiles;

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
      std::unordered_set<std::pair<int, int>, PairHash> visited;
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
      const int expectedTiles = (maxX - minX + 1) * (maxY - minY + 1);
      const bool isRectangle = (static_cast<int>(buildingTiles.size()) == expectedTiles);

      COLLISION_DEBUG(std::format("Building {}: bounds ({},{}) to ({},{}), tiles={}, expected={}, isRectangle={}",
                                  tile.buildingId, minX, minY, maxX, maxY,
                                  buildingTiles.size(), expectedTiles, isRectangle ? "YES" : "NO"));

      if (isRectangle) {
        // SIMPLE CASE: Single collision body for entire rectangular building
        const float worldMinX = minX * tileSize;
        const float worldMinY = minY * tileSize;
        const float worldMaxX = (maxX + 1) * tileSize;
        const float worldMaxY = (maxY + 1) * tileSize;

        const float cx = (worldMinX + worldMaxX) * 0.5f;
        const float cy = (worldMinY + worldMaxY) * 0.5f;
        const float halfWidth = (worldMaxX - worldMinX) * 0.5f;
        const float halfHeight = (worldMaxY - worldMinY) * 0.5f;

        const Vector2D center(cx, cy);
        const Vector2D halfSize(halfWidth, halfHeight);

        // Single body: subBodyIndex = 0
        EntityID id = (static_cast<EntityID>(3ull) << 61) |
                      (static_cast<EntityID>(tile.buildingId) << 16);

        if (m_storage.entityToIndex.find(id) == m_storage.entityToIndex.end()) {
          addCollisionBodySOA(id, center, halfSize, BodyType::STATIC,
                             CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
          ++created;

          COLLISION_INFO(std::format("Building {}: created 1 collision body (rectangle {}x{} tiles) "
                                     "at center({},{}) halfSize({},{}) AABB[{},{} to {},{}]",
                                     tile.buildingId, maxX - minX + 1, maxY - minY + 1,
                                     cx, cy, halfWidth, halfHeight,
                                     worldMinX, worldMinY, worldMaxX, worldMaxY));
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

        COLLISION_INFO(std::format("Building {}: created {} collision bodies (non-rectangular)",
                                   tile.buildingId, subBodyIndex));
      }
    }
  }

  return created;
}


bool CollisionManager::overlaps(EntityID a, EntityID b) const {
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
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  // Access position from EDM (single source of truth)
  size_t edmIndex = m_storage.hotData[it->second].edmIndex;
  if (edmIndex == SIZE_MAX) {
    // Fallback: compute center from cached AABB
    const auto& hot = m_storage.hotData[it->second];
    outCenter = Vector2D((hot.aabbMinX + hot.aabbMaxX) * 0.5f,
                         (hot.aabbMinY + hot.aabbMaxY) * 0.5f);
  } else {
    const auto& transform = EntityDataManager::Instance().getTransformByIndex(edmIndex);
    outCenter = transform.position;
  }
  return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) == BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) == BodyType::KINEMATIC;
}

bool CollisionManager::isStatic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) == BodyType::STATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return m_storage.hotData[it->second].isTrigger;
}




void CollisionManager::update(float dt) {
  (void)dt;
  if (!m_initialized || m_isShutdown || m_globallyPaused.load(std::memory_order_acquire))
    return;

  // SOA collision system only
  updateSOA(dt);
  return;

}

void CollisionManager::setGlobalPause(bool paused) {
  m_globallyPaused.store(paused, std::memory_order_release);
}

bool CollisionManager::isGloballyPaused() const {
  return m_globallyPaused.load(std::memory_order_acquire);
}

void CollisionManager::addCollisionCallback(CollisionCB cb) {
  m_callbacks.push_back(std::move(cb));
}

void CollisionManager::logCollisionStatistics() const {
#ifndef NDEBUG
  // Debug-only: expensive iteration for statistics logging
  size_t staticBodies = getStaticBodyCount();
  size_t kinematicBodies = getKinematicBodyCount();
  size_t dynamicBodies = getDynamicBodyCount();

  COLLISION_INFO("Collision Statistics:");
  COLLISION_INFO(std::format("  Total Bodies: {}", getBodyCount()));
  COLLISION_INFO(std::format("  Static Bodies: {} (obstacles + triggers)", staticBodies));
  COLLISION_INFO(std::format("  Kinematic Bodies: {} (NPCs)", kinematicBodies));
  COLLISION_INFO(std::format("  Dynamic Bodies: {} (player, projectiles)", dynamicBodies));

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
    COLLISION_INFO(std::format("    {}: {}", layerName, layerCount.second));
  }
#endif
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

  if (solidBodies > 0 || waterTriggers > 0) {
    COLLISION_INFO(std::format(
        "World colliders built: solid={}, water triggers={}",
        solidBodies, waterTriggers));

#ifndef NDEBUG
    // Debug-only: Count building collision bodies for verification
    int buildingBodyCount = 0;
    for (size_t i = 0; i < m_storage.entityIds.size(); ++i) {
      EntityID id = m_storage.entityIds[i];
      if ((id >> 61) == 3) { // Building type
        buildingBodyCount++;
        uint32_t buildingId = (id >> 16) & 0xFFFF;
        uint16_t subBodyIndex = id & 0xFFFF;
        COLLISION_DEBUG(std::format("Building collision body found: buildingId={}, subBodyIndex={}",
                                    buildingId, subBodyIndex));
      }
    }
    COLLISION_INFO(std::format("Total building collision bodies in storage: {}", buildingBodyCount));
    logCollisionStatistics();
#endif

    // Force immediate static spatial hash rebuild for world changes
    rebuildStaticSpatialHash();
  }
}

// NOTE: populateStaticCache() removed in Phase 3 - m_staticSpatialHash queried directly

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
      float const cx = x * tileSize + tileSize * 0.5f;
      float const cy = y * tileSize + tileSize * 0.5f;
      Vector2D const center(cx, cy);
      Vector2D const halfSize(tileSize * 0.5f, tileSize * 0.5f);
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
      std::unordered_set<std::pair<int, int>, PairHash> visited;
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
        while (subBodyIndex < MAX_BUILDING_SUB_BODIES) { // Safety limit to prevent infinite loop
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
          COLLISION_INFO("Responding to WorldUnloadedEvent");

          // Static bodies already cleared by prepareForStateTransition()
          // This event handler serves as confirmation that world cleanup completed
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

// ========== SOA STORAGE MANAGEMENT METHODS ==========

size_t CollisionManager::addCollisionBodySOA(EntityID id, const Vector2D& position,
                                              const Vector2D& halfSize, BodyType type,
                                              uint32_t layer, uint32_t collidesWith,
                                              bool isTrigger, uint8_t triggerTag) {
  // Check if entity already exists
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end()) {
    // Update existing entity
    size_t index = it->second;
    if (index < m_storage.hotData.size()) {
      auto& hot = m_storage.hotData[index];
      BodyType oldBodyType = static_cast<BodyType>(hot.bodyType);
      uint32_t oldLayers = hot.layers;

      float px = position.getX();
      float py = position.getY();
      float hw = halfSize.getX();
      float hh = halfSize.getY();
      hot.aabbMinX = px - hw;
      hot.aabbMinY = py - hh;
      hot.aabbMaxX = px + hw;
      hot.aabbMaxY = py + hh;
      hot.aabbDirty = 0;
      hot.layers = layer;
      hot.collidesWith = collidesWith;
      hot.bodyType = static_cast<uint8_t>(type);
      hot.active = true;

      // Update movable tracking if body type changed
      bool wasMovable = (oldBodyType != BodyType::STATIC);
      bool isMovable = (type != BodyType::STATIC);
      if (wasMovable && !isMovable) {
        m_movableIndexSet.erase(index);
        auto movableIt = std::find(m_movableBodyIndices.begin(), m_movableBodyIndices.end(), index);
        if (movableIt != m_movableBodyIndices.end()) {
          *movableIt = m_movableBodyIndices.back();
          m_movableBodyIndices.pop_back();
        }
      } else if (!wasMovable && isMovable) {
        m_movableBodyIndices.push_back(index);
        m_movableIndexSet.insert(index);
      }

      // Update player index if layer changed
      bool wasPlayer = (oldLayers & CollisionLayer::Layer_Player) != 0;
      bool isPlayer = (layer & CollisionLayer::Layer_Player) != 0;
      if (!wasPlayer && isPlayer) {
        m_playerBodyIndex = index;
      } else if (wasPlayer && !isPlayer && m_playerBodyIndex == index) {
        m_playerBodyIndex = std::nullopt;
      }

      if (index < m_storage.coldData.size()) {
        m_storage.coldData[index].fullAABB = AABB(px, py, hw, hh);
      }
    }
    return it->second;
  }

  // Add new entity
  size_t newIndex = m_storage.size();
  float px = position.getX();
  float py = position.getY();
  float hw = halfSize.getX();
  float hh = halfSize.getY();

  CollisionStorage::HotData hotData{};
  hotData.aabbMinX = px - hw;
  hotData.aabbMinY = py - hh;
  hotData.aabbMaxX = px + hw;
  hotData.aabbMaxY = py + hh;
  hotData.aabbDirty = 0;
  hotData.layers = layer;
  hotData.collidesWith = collidesWith;
  hotData.bodyType = static_cast<uint8_t>(type);
  hotData.triggerTag = triggerTag;
  hotData.active = true;
  hotData.isTrigger = isTrigger;

  // Look up EDM entry by EntityID - EDM is source of truth
  auto& edm = EntityDataManager::Instance();
  size_t edmIdx = edm.findIndexByEntityId(id);
  if (edmIdx == SIZE_MAX) {
    // Not registered with EDM - create entry for collision body
    EntityHandle handle = edm.createStaticBody(position, hw, hh);
    edmIdx = edm.getIndex(handle);
  }
  hotData.edmIndex = edmIdx;

  // Initialize coarse cell coords for cache optimization
  AABB initialAABB(px, py, hw, hh);
  auto initialCoarseCell = m_staticSpatialHash.getCoarseCoord(initialAABB);
  hotData.coarseCellX = static_cast<int16_t>(initialCoarseCell.x);
  hotData.coarseCellY = static_cast<int16_t>(initialCoarseCell.y);

  CollisionStorage::ColdData coldData{};
  coldData.fullAABB = AABB(px, py, hw, hh);

  m_storage.hotData.push_back(hotData);
  m_storage.coldData.push_back(coldData);
  m_storage.entityIds.push_back(id);
  m_storage.entityToIndex[id] = newIndex;

  if (type != BodyType::STATIC) {
    m_movableBodyIndices.push_back(newIndex);
    m_movableIndexSet.insert(newIndex);
  }

  if (layer & CollisionLayer::Layer_Player) {
    m_playerBodyIndex = newIndex;
  }

  if (type == BodyType::STATIC) {
    float radius = std::max(hw, hh) + 16.0f;
    std::string description = std::format("Static obstacle added at ({}, {})", px, py);
    EventManager::Instance().triggerCollisionObstacleChanged(position, radius, description,
                                                            EventManager::DispatchMode::Deferred);
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
  if (indexToRemove < m_storage.size()) {
    const auto& hot = m_storage.hotData[indexToRemove];
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      Vector2D position;
      float halfW, halfH;
      if (hot.edmIndex != SIZE_MAX) {
        auto& edm = EntityDataManager::Instance();
        position = edm.getTransformByIndex(hot.edmIndex).position;
        const auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);
        halfW = edmHot.halfWidth;
        halfH = edmHot.halfHeight;
      } else {
        position = Vector2D((hot.aabbMinX + hot.aabbMaxX) * 0.5f,
                            (hot.aabbMinY + hot.aabbMaxY) * 0.5f);
        halfW = (hot.aabbMaxX - hot.aabbMinX) * 0.5f;
        halfH = (hot.aabbMaxY - hot.aabbMinY) * 0.5f;
      }
      float radius = std::max(halfW, halfH) + 16.0f;
      std::string description = std::format("Static obstacle removed from ({}, {})",
                                            position.getX(), position.getY());
      EventManager::Instance().triggerCollisionObstacleChanged(position, radius, description,
                                                              EventManager::DispatchMode::Deferred);
      m_staticHashDirty = true;
    }
  }

  // Clear player index if removing player
  if (m_playerBodyIndex && *m_playerBodyIndex == indexToRemove) {
    m_playerBodyIndex = std::nullopt;
  }

  // Remove from movable tracking (swap-and-pop for O(1))
  if (m_movableIndexSet.count(indexToRemove)) {
    m_movableIndexSet.erase(indexToRemove);
    auto movableIt = std::find(m_movableBodyIndices.begin(), m_movableBodyIndices.end(), indexToRemove);
    if (movableIt != m_movableBodyIndices.end()) {
      *movableIt = m_movableBodyIndices.back();
      m_movableBodyIndices.pop_back();
    }
  }

  if (indexToRemove != lastIndex) {
    // Swap with last element
    m_storage.hotData[indexToRemove] = m_storage.hotData[lastIndex];
    m_storage.coldData[indexToRemove] = m_storage.coldData[lastIndex];
    m_storage.entityIds[indexToRemove] = m_storage.entityIds[lastIndex];

    EntityID movedEntity = m_storage.entityIds[indexToRemove];
    m_storage.entityToIndex[movedEntity] = indexToRemove;

    // Update tracking for moved element
    if (m_playerBodyIndex && *m_playerBodyIndex == lastIndex) {
      m_playerBodyIndex = indexToRemove;
    }
    if (m_movableIndexSet.count(lastIndex)) {
      m_movableIndexSet.erase(lastIndex);
      m_movableIndexSet.insert(indexToRemove);
      auto movedIt = std::find(m_movableBodyIndices.begin(), m_movableBodyIndices.end(), lastIndex);
      if (movedIt != m_movableBodyIndices.end()) {
        *movedIt = indexToRemove;
      }
    }
  }

  m_storage.hotData.pop_back();
  m_storage.coldData.pop_back();
  m_storage.entityIds.pop_back();
  m_storage.entityToIndex.erase(id);

  // Clean up trigger-related state
  for (auto triggerIt = m_activeTriggerPairs.begin(); triggerIt != m_activeTriggerPairs.end(); ) {
    if (triggerIt->second.first == id || triggerIt->second.second == id) {
      triggerIt = m_activeTriggerPairs.erase(triggerIt);
    } else {
      ++triggerIt;
    }
  }
  m_triggerCooldownUntil.erase(id);
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

    // Write position to EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      auto& edm = EntityDataManager::Instance();
      edm.getTransformByIndex(hot.edmIndex).position = newPosition;
      const auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);

      // Update cached AABB
      float halfW = edmHot.halfWidth;
      float halfH = edmHot.halfHeight;
      hot.aabbMinX = newPosition.getX() - halfW;
      hot.aabbMinY = newPosition.getY() - halfH;
      hot.aabbMaxX = newPosition.getX() + halfW;
      hot.aabbMaxY = newPosition.getY() + halfH;
      hot.aabbDirty = 0;

      // Update full AABB in cold data
      if (index < m_storage.coldData.size()) {
        m_storage.coldData[index].fullAABB = AABB(newPosition.getX(), newPosition.getY(), halfW, halfH);
      }
    }
  }
}

void CollisionManager::updateCollisionBodyVelocitySOA(EntityID id, const Vector2D& newVelocity) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    auto& hot = m_storage.hotData[index];
    // Write velocity to EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      EntityDataManager::Instance().getTransformByIndex(hot.edmIndex).velocity = newVelocity;
    }
  }
}

Vector2D CollisionManager::getCollisionBodyVelocitySOA(EntityID id) const {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    const auto& hot = m_storage.hotData[index];
    // Read velocity from EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      return EntityDataManager::Instance().getTransformByIndex(hot.edmIndex).velocity;
    }
  }
  return Vector2D(0, 0);
}

void CollisionManager::updateCollisionBodySizeSOA(EntityID id, const Vector2D& newHalfSize) {
  size_t index;
  if (getCollisionBodySOA(id, index)) {
    auto& hot = m_storage.hotData[index];
    // Write halfSize to EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      auto& edm = EntityDataManager::Instance();
      auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);
      edmHot.halfWidth = newHalfSize.getX();
      edmHot.halfHeight = newHalfSize.getY();

      // Update cached AABB
      const auto& transform = edm.getTransformByIndex(hot.edmIndex);
      float px = transform.position.getX();
      float py = transform.position.getY();
      hot.aabbMinX = px - newHalfSize.getX();
      hot.aabbMinY = py - newHalfSize.getY();
      hot.aabbMaxX = px + newHalfSize.getX();
      hot.aabbMaxY = py + newHalfSize.getY();
      hot.aabbDirty = 0;
    }
  }
}

void CollisionManager::attachEntity(EntityID id, EntityPtr entity) {
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end()) {
    size_t index = it->second;
    if (index < m_storage.coldData.size() && index < m_storage.hotData.size()) {
      m_storage.coldData[index].entityWeak = entity;

      // Cache EDM index for direct position access (like AIManager pattern)
      // edmIndex is in HotData for cache locality during AABB updates
      if (entity && entity->hasValidHandle()) {
        m_storage.hotData[index].edmIndex = EntityDataManager::Instance().getIndex(entity->getHandle());
      } else {
        m_storage.hotData[index].edmIndex = SIZE_MAX;
      }
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


// NOTE: rebuildStaticSpatialGrid() and queryStaticGridCells() removed in Phase 3
// Static bodies are now queried directly via m_staticSpatialHash

// Optimized version of buildActiveIndicesSOA - queries m_staticSpatialHash directly
std::tuple<size_t, size_t, size_t> CollisionManager::buildActiveIndicesSOA(const CullingArea& cullingArea) const {
  // Build indices of active bodies within culling area
  // Phase 3 simplified: Query m_staticSpatialHash directly instead of separate grid/cache
  auto& pools = m_collisionPool;

  // Store current culling area for use in broadphase queries
  m_currentCullingArea = cullingArea;
  pools.activeIndices.clear();
  pools.movableIndices.clear();
  pools.staticIndices.clear();

  // Track total body counts
  size_t totalStatic = 0;
  size_t totalDynamic = 0;
  size_t totalKinematic = 0;

  // Query m_staticSpatialHash directly for statics in culling area
  // Creates an AABB from the culling bounds for the spatial hash query
  const float cullCenterX = (cullingArea.minX + cullingArea.maxX) * 0.5f;
  const float cullCenterY = (cullingArea.minY + cullingArea.maxY) * 0.5f;
  const float cullHalfW = (cullingArea.maxX - cullingArea.minX) * 0.5f;
  const float cullHalfH = (cullingArea.maxY - cullingArea.minY) * 0.5f;

  if (cullHalfW > 0.0f && cullHalfH > 0.0f) {
    AABB cullAABB(cullCenterX, cullCenterY, cullHalfW, cullHalfH);
    m_staticSpatialHash.queryRegion(cullAABB, pools.staticIndices);
  }
  totalStatic = pools.staticIndices.size();

  // OPTIMIZATION: Process only tracked movable bodies (O(3) instead of O(18K))
  // Regression fix for commit 768ad87 - was iterating ALL bodies to find 3 movables
  for (size_t i : m_movableBodyIndices) {
    if (i >= m_storage.hotData.size()) continue;
    const auto& hot = m_storage.hotData[i];
    if (!hot.active) continue;

    // Apply culling to movable bodies using cached AABB center
    if (cullingArea.minX != cullingArea.maxX || cullingArea.minY != cullingArea.maxY) {
      float posX = (hot.aabbMinX + hot.aabbMaxX) * 0.5f;
      float posY = (hot.aabbMinY + hot.aabbMaxY) * 0.5f;
      if (!cullingArea.contains(posX, posY)) {
        continue;
      }
    }

    // Count movable bodies by type
    BodyType bodyType = static_cast<BodyType>(hot.bodyType);
    if (bodyType == BodyType::DYNAMIC) {
      totalDynamic++;
    } else if (bodyType == BodyType::KINEMATIC) {
      totalKinematic++;
    }

    pools.movableIndices.push_back(i);
  }

  // Combine cached statics + current movables into activeIndices
  pools.activeIndices.reserve(pools.staticIndices.size() + pools.movableIndices.size());
  pools.activeIndices.insert(pools.activeIndices.end(), pools.staticIndices.begin(), pools.staticIndices.end());
  pools.activeIndices.insert(pools.activeIndices.end(), pools.movableIndices.begin(), pools.movableIndices.end());

  return {totalStatic, totalDynamic, totalKinematic};
}

// Internal helper methods moved to private section

// ========== NEW SOA-BASED BROADPHASE IMPLEMENTATION ==========

void CollisionManager::broadphaseSOA(std::vector<std::pair<size_t, size_t>>& indexPairs) {
  // Dispatcher: Choose single-threaded or multi-threaded path based on WorkerBudget

  const auto& pools = m_collisionPool;
  const auto& movableIndices = pools.movableIndices;

  if (movableIndices.empty()) {
    indexPairs.clear();
    m_lastBroadphaseWasThreaded = false;
    return;
  }

  // Query WorkerBudget for optimal configuration
  auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
  size_t optimalWorkers = budgetMgr.getOptimalWorkers(
      HammerEngine::SystemType::Collision, movableIndices.size());

  auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
      HammerEngine::SystemType::Collision,
      movableIndices.size(),
      optimalWorkers);

  // Threshold: multi-thread only if WorkerBudget recommends it
  if (batchCount <= 1 || movableIndices.size() < MIN_MOVABLE_FOR_BROADPHASE_THREADING) {
    m_lastBroadphaseWasThreaded = false;
    m_lastBroadphaseBatchCount = 1;
    broadphaseSingleThreaded(indexPairs);
  } else {
    m_lastBroadphaseWasThreaded = true;
    m_lastBroadphaseBatchCount = batchCount;
    broadphaseMultiThreaded(indexPairs, batchCount, batchSize);
  }
}

void CollisionManager::broadphaseSingleThreaded(std::vector<std::pair<size_t, size_t>>& indexPairs) {
  indexPairs.clear();

  const auto& pools = m_collisionPool;
  const auto& movableIndices = pools.movableIndices;
  const auto& staticIndices = pools.staticIndices;

  // Reserve space for pairs (movable×movable/2 + movable×static)
  indexPairs.reserve(movableIndices.size() * (movableIndices.size() / 2 + staticIndices.size()) / 4);

  // ========================================================================
  // 1. MOVABLE-VS-MOVABLE: Sweep-and-Prune (SAP) algorithm
  //    Sort by X, only check pairs with overlapping X ranges - O(M log M + M*k)
  //    where k is average number of X-overlapping neighbors (typically small)
  // ========================================================================
  if (movableIndices.size() > 1) {
    // Copy and sort movables by minX for sweep-and-prune
    auto& sorted = m_collisionPool.sortedMovableIndices;
    sorted.assign(movableIndices.begin(), movableIndices.end());
    std::sort(sorted.begin(), sorted.end(), [this](size_t a, size_t b) {
      return m_storage.hotData[a].aabbMinX < m_storage.hotData[b].aabbMinX;
    });

    // Sweep: for each body, only check bodies that start before this one ends (X overlap)
    for (size_t i = 0; i < sorted.size(); ++i) {
      size_t idxA = sorted[i];
      const auto& hotA = m_storage.hotData[idxA];
      if (!hotA.active) continue;

      const float maxXA = hotA.aabbMaxX + SPATIAL_QUERY_EPSILON;
      const uint32_t collidesWithA = hotA.collidesWith;

      // Check subsequent bodies until their minX > our maxX (early exit!)
      for (size_t j = i + 1; j < sorted.size(); ++j) {
        size_t idxB = sorted[j];
        const auto& hotB = m_storage.hotData[idxB];

        // SAP early termination: if B's minX > A's maxX, no more overlaps possible
        if (hotB.aabbMinX > maxXA) break;

        if (!hotB.active) continue;

        // X already overlaps (from SAP), check Y overlap
        if (hotA.aabbMaxY + SPATIAL_QUERY_EPSILON < hotB.aabbMinY ||
            hotA.aabbMinY - SPATIAL_QUERY_EPSILON > hotB.aabbMaxY) continue;

        // Layer mask check
        if ((collidesWithA & hotB.layers) == 0) continue;

        // Safety: skip self-pairs (shouldn't happen with unique indices)
        if (idxA != idxB) {
          indexPairs.emplace_back(idxA, idxB);
        }
      }
    }
  }

  // ========================================================================
  // 2. MOVABLE-VS-STATIC: Iterate each movable against statics
  //    Uses pre-computed staticIndices from culling area
  // ========================================================================
  static constexpr size_t STATIC_DIRECT_THRESHOLD = 500;

  for (size_t mi = 0; mi < movableIndices.size(); ++mi) {
    size_t dynamicIdx = movableIndices[mi];
    const auto& dynamicHot = m_storage.hotData[dynamicIdx];
    if (!dynamicHot.active) continue;

    const uint32_t dynamicCollidesWith = dynamicHot.collidesWith;

    if (staticIndices.size() <= STATIC_DIRECT_THRESHOLD) {
      // DIRECT PATH: Check all statics in culling area
      for (size_t si = 0; si < staticIndices.size(); ++si) {
        size_t staticIdx = staticIndices[si];
        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active) continue;

        // AABB overlap test
        if (dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON < staticHot.aabbMinX ||
            dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxX ||
            dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON < staticHot.aabbMinY ||
            dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxY) continue;

        // Layer mask check
        if ((dynamicCollidesWith & staticHot.layers) == 0) continue;

        indexPairs.emplace_back(dynamicIdx, staticIdx);
      }
    } else {
      // SPATIAL HASH PATH: Query for nearby statics only
      float queryMinX = dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON;
      float queryMinY = dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON;
      float queryMaxX = dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON;
      float queryMaxY = dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON;

      auto& staticCandidates = getPooledVector();
      m_staticSpatialHash.queryRegionBounds(queryMinX, queryMinY, queryMaxX, queryMaxY, staticCandidates);

      for (size_t si = 0; si < staticCandidates.size(); ++si) {
        size_t staticIdx = staticCandidates[si];
        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active) continue;
        if ((dynamicCollidesWith & staticHot.layers) == 0) continue;
        indexPairs.emplace_back(dynamicIdx, staticIdx);
      }
      returnPooledVector(staticCandidates);
    }
  }
}

void CollisionManager::broadphaseMultiThreaded(
    std::vector<std::pair<size_t, size_t>>& indexPairs,
    size_t batchCount,
    size_t batchSize) {
  auto& threadSystem = HammerEngine::ThreadSystem::Instance();

  const auto& movableIndices = m_collisionPool.movableIndices;

  // NOTE: AABB caches already refreshed at start of updateSOA via refreshCachedAABBs()
  // No per-body update needed here (prevents data races by design)

  // Phase 3: Cache removed - m_staticSpatialHash queried directly (thread-safe for reads)

  // Reuse per-batch buffers (avoid allocations)
  if (!m_broadphasePairBuffers) {
    m_broadphasePairBuffers =
        std::make_shared<std::vector<std::vector<std::pair<size_t, size_t>>>>();
  }
  m_broadphasePairBuffers->resize(batchCount);

  // Reserve capacity for each batch
  size_t estimatedPerBatch = (movableIndices.size() / batchCount) * 4;
  for (size_t i = 0; i < batchCount; ++i) {
    (*m_broadphasePairBuffers)[i].clear();
    (*m_broadphasePairBuffers)[i].reserve(estimatedPerBatch);
  }

  // Submit batches (threads will only do READ-ONLY queries, no state modifications)
  {
    std::lock_guard<std::mutex> lock(m_broadphaseFuturesMutex);
    m_broadphaseFutures.clear();
    m_broadphaseFutures.reserve(batchCount);

    auto pairBuffers = m_broadphasePairBuffers;  // Capture shared_ptr by value

    for (size_t i = 0; i < batchCount; ++i) {
      size_t startIdx = i * batchSize;
      size_t endIdx = std::min(startIdx + batchSize, movableIndices.size());

      m_broadphaseFutures.push_back(
          threadSystem.enqueueTaskWithResult(
              [this, startIdx, endIdx, pairBuffers, i]() -> void {
                try {
                  broadphaseBatch((*pairBuffers)[i], startIdx, endIdx);
                } catch (const std::exception& e) {
                  COLLISION_ERROR(std::format(
                      "Exception in broadphase batch {}: {}", i, e.what()));
                }
              },
              HammerEngine::TaskPriority::High,
              std::format("Collision_Broadphase_Batch_{}", i)
          )
      );
    }
  }

  // Wait for completion
  {
    std::lock_guard<std::mutex> lock(m_broadphaseFuturesMutex);
    for (auto& future : m_broadphaseFutures) {
      future.wait();
    }
  }

  // Merge results (single allocation, sequential merge)
  indexPairs.clear();
  size_t totalPairs = std::accumulate(
      m_broadphasePairBuffers->begin(), m_broadphasePairBuffers->end(), size_t{0},
      [](size_t sum, const auto& batchPairs) { return sum + batchPairs.size(); });
  indexPairs.reserve(totalPairs);

  for (const auto& batchPairs : *m_broadphasePairBuffers) {
    indexPairs.insert(indexPairs.end(),
                     batchPairs.begin(),
                     batchPairs.end());
  }

#ifndef NDEBUG
  // Interval stats logging - zero overhead in release (entire block compiles out)
  static thread_local uint64_t logFrameCounter = 0;
  if (++logFrameCounter % 300 == 0 && movableIndices.size() > 0) {
    COLLISION_DEBUG(std::format("Broadphase: multi-threaded [{} batches, {} movable bodies, {} pairs]",
                               batchCount, movableIndices.size(), indexPairs.size()));
  }
#endif
}

void CollisionManager::broadphaseBatch(
    std::vector<std::pair<size_t, size_t>>& outIndexPairs,
    size_t startIdx,
    size_t endIdx) {
  const auto& pools = m_collisionPool;
  const auto& movableIndices = pools.movableIndices;
  const auto& staticIndices = pools.staticIndices;

  // Process each movable in this batch's range [startIdx, endIdx)
  // All data is READ-ONLY - safe for parallel access
  for (size_t mi = startIdx; mi < endIdx && mi < movableIndices.size(); ++mi) {
    size_t dynamicIdx = movableIndices[mi];

    if (dynamicIdx >= m_storage.hotData.size()) continue;
    if (!m_storage.hotData[dynamicIdx].active) continue;

    const auto& dynamicHot = m_storage.hotData[dynamicIdx];
    const uint32_t dynamicCollidesWith = dynamicHot.collidesWith;

    // Broadcast dynamic AABB for SIMD comparison (with epsilon)
    Float4 dynMinX = broadcast(dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON);
    Float4 dynMinY = broadcast(dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON);
    Float4 dynMaxX = broadcast(dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON);
    Float4 dynMaxY = broadcast(dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON);
    const Int4 dynamicMaskVec = broadcast_int(dynamicCollidesWith);

    // ========================================================================
    // 1. MOVABLE-VS-MOVABLE: Direct iteration with SIMD AABB checks
    //    Only check movables with index > mi to avoid duplicates across batches
    // ========================================================================
    size_t startJ = mi + 1;
    const size_t movableSimdEnd = startJ + ((movableIndices.size() - startJ) / 4) * 4;

    for (size_t mj = startJ; mj < movableSimdEnd; mj += 4) {
      size_t c0 = movableIndices[mj], c1 = movableIndices[mj+1];
      size_t c2 = movableIndices[mj+2], c3 = movableIndices[mj+3];

      // Load 4 candidate AABBs
      Float4 candMinX = set(m_storage.hotData[c0].aabbMinX, m_storage.hotData[c1].aabbMinX,
                            m_storage.hotData[c2].aabbMinX, m_storage.hotData[c3].aabbMinX);
      Float4 candMaxX = set(m_storage.hotData[c0].aabbMaxX, m_storage.hotData[c1].aabbMaxX,
                            m_storage.hotData[c2].aabbMaxX, m_storage.hotData[c3].aabbMaxX);
      Float4 candMinY = set(m_storage.hotData[c0].aabbMinY, m_storage.hotData[c1].aabbMinY,
                            m_storage.hotData[c2].aabbMinY, m_storage.hotData[c3].aabbMinY);
      Float4 candMaxY = set(m_storage.hotData[c0].aabbMaxY, m_storage.hotData[c1].aabbMaxY,
                            m_storage.hotData[c2].aabbMaxY, m_storage.hotData[c3].aabbMaxY);

      // SIMD AABB overlap test
      Float4 noOverlapX1 = cmplt(dynMaxX, candMinX);
      Float4 noOverlapX2 = cmplt(candMaxX, dynMinX);
      Float4 noOverlapY1 = cmplt(dynMaxY, candMinY);
      Float4 noOverlapY2 = cmplt(candMaxY, dynMinY);
      Float4 noOverlap = bitwise_or(bitwise_or(noOverlapX1, noOverlapX2), bitwise_or(noOverlapY1, noOverlapY2));
      int noOverlapMask = movemask(noOverlap);

      if (noOverlapMask == 0xF) continue;

      // Layer mask check
      Int4 candLayers = set_int4(m_storage.hotData[c0].layers, m_storage.hotData[c1].layers,
                                 m_storage.hotData[c2].layers, m_storage.hotData[c3].layers);
      Int4 layerResult = bitwise_and(dynamicMaskVec, candLayers);
      Int4 layerZero = cmpeq_int(layerResult, setzero_int());
      int layerFailMask = movemask_int(layerZero);

      for (size_t k = 0; k < 4; ++k) {
        if (((noOverlapMask >> k) & 1) == 0 && ((layerFailMask >> (k * 4)) & 0xF) == 0) {
          size_t candIdx = movableIndices[mj + k];
          if (candIdx != dynamicIdx && m_storage.hotData[candIdx].active) {
            outIndexPairs.emplace_back(dynamicIdx, candIdx);
          }
        }
      }
    }

    // Scalar tail for remaining movables
    for (size_t mj = movableSimdEnd; mj < movableIndices.size(); ++mj) {
      size_t candIdx = movableIndices[mj];
      if (candIdx == dynamicIdx) continue;
      const auto& candHot = m_storage.hotData[candIdx];
      if (!candHot.active) continue;
      if (dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON < candHot.aabbMinX ||
          dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON > candHot.aabbMaxX ||
          dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON < candHot.aabbMinY ||
          dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON > candHot.aabbMaxY) continue;
      if ((dynamicCollidesWith & candHot.layers) == 0) continue;
      outIndexPairs.emplace_back(dynamicIdx, candIdx);
    }

    // ========================================================================
    // 2. MOVABLE-VS-STATIC: Choose strategy based on static count
    //    staticIndices is READ-ONLY - safe for parallel access
    // ========================================================================
    static constexpr size_t STATIC_DIRECT_THRESHOLD = 500;
    const Int4 staticMaskVec = broadcast_int(dynamicCollidesWith);

    if (staticIndices.size() <= STATIC_DIRECT_THRESHOLD) {
      // DIRECT PATH: Iterate all culled statics with SIMD AABB checks
      size_t si = 0;
      const size_t staticSimdEnd = (staticIndices.size() / 4) * 4;

      for (; si < staticSimdEnd; si += 4) {
        size_t s0 = staticIndices[si], s1 = staticIndices[si+1];
        size_t s2 = staticIndices[si+2], s3 = staticIndices[si+3];

        Float4 statMinX = set(m_storage.hotData[s0].aabbMinX, m_storage.hotData[s1].aabbMinX,
                              m_storage.hotData[s2].aabbMinX, m_storage.hotData[s3].aabbMinX);
        Float4 statMaxX = set(m_storage.hotData[s0].aabbMaxX, m_storage.hotData[s1].aabbMaxX,
                              m_storage.hotData[s2].aabbMaxX, m_storage.hotData[s3].aabbMaxX);
        Float4 statMinY = set(m_storage.hotData[s0].aabbMinY, m_storage.hotData[s1].aabbMinY,
                              m_storage.hotData[s2].aabbMinY, m_storage.hotData[s3].aabbMinY);
        Float4 statMaxY = set(m_storage.hotData[s0].aabbMaxY, m_storage.hotData[s1].aabbMaxY,
                              m_storage.hotData[s2].aabbMaxY, m_storage.hotData[s3].aabbMaxY);

        Float4 noOverlapX1 = cmplt(dynMaxX, statMinX);
        Float4 noOverlapX2 = cmplt(statMaxX, dynMinX);
        Float4 noOverlapY1 = cmplt(dynMaxY, statMinY);
        Float4 noOverlapY2 = cmplt(statMaxY, dynMinY);
        Float4 noOverlap = bitwise_or(bitwise_or(noOverlapX1, noOverlapX2), bitwise_or(noOverlapY1, noOverlapY2));
        int noOverlapMask = movemask(noOverlap);

        if (noOverlapMask == 0xF) continue;

        Int4 staticLayers = set_int4(m_storage.hotData[s0].layers, m_storage.hotData[s1].layers,
                                     m_storage.hotData[s2].layers, m_storage.hotData[s3].layers);
        Int4 staticResult = bitwise_and(staticLayers, staticMaskVec);
        Int4 staticCmp = cmpeq_int(staticResult, setzero_int());
        int layerFailMask = movemask_int(staticCmp);

        for (size_t j = 0; j < 4; ++j) {
          if (((noOverlapMask >> j) & 1) == 0 && ((layerFailMask >> (j * 4)) & 0xF) == 0) {
            if (m_storage.hotData[staticIndices[si + j]].active) {
              outIndexPairs.emplace_back(dynamicIdx, staticIndices[si + j]);
            }
          }
        }
      }

      for (; si < staticIndices.size(); ++si) {
        size_t staticIdx = staticIndices[si];
        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active) continue;
        if (dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON < staticHot.aabbMinX ||
            dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxX ||
            dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON < staticHot.aabbMinY ||
            dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxY) continue;
        if ((dynamicCollidesWith & staticHot.layers) == 0) continue;
        outIndexPairs.emplace_back(dynamicIdx, staticIdx);
      }
    } else {
      // SPATIAL HASH PATH: Query for nearby statics (thread-safe)
      float queryMinX = dynamicHot.aabbMinX - SPATIAL_QUERY_EPSILON;
      float queryMinY = dynamicHot.aabbMinY - SPATIAL_QUERY_EPSILON;
      float queryMaxX = dynamicHot.aabbMaxX + SPATIAL_QUERY_EPSILON;
      float queryMaxY = dynamicHot.aabbMaxY + SPATIAL_QUERY_EPSILON;

      thread_local std::vector<size_t> staticCandidates;
      thread_local HammerEngine::HierarchicalSpatialHash::QueryBuffers queryBuffers;
      staticCandidates.clear();
      m_staticSpatialHash.queryRegionBoundsThreadSafe(
          queryMinX, queryMinY, queryMaxX, queryMaxY, staticCandidates, queryBuffers);

      size_t si = 0;
      const size_t staticSimdEnd = (staticCandidates.size() / 4) * 4;

      for (; si < staticSimdEnd; si += 4) {
        Int4 staticLayers = set_int4(
          m_storage.hotData[staticCandidates[si]].layers,
          m_storage.hotData[staticCandidates[si+1]].layers,
          m_storage.hotData[staticCandidates[si+2]].layers,
          m_storage.hotData[staticCandidates[si+3]].layers
        );
        Int4 staticResult = bitwise_and(staticLayers, staticMaskVec);
        Int4 staticCmp = cmpeq_int(staticResult, setzero_int());
        int staticFailMask = movemask_int(staticCmp);

        if (staticFailMask == 0xFFFF) continue;

        for (size_t j = 0; j < 4; ++j) {
          if (((staticFailMask >> (j * 4)) & 0xF) == 0) {
            size_t staticIdx = staticCandidates[si + j];
            if (m_storage.hotData[staticIdx].active) {
              outIndexPairs.emplace_back(dynamicIdx, staticIdx);
            }
          }
        }
      }

      for (; si < staticCandidates.size(); ++si) {
        size_t staticIdx = staticCandidates[si];
        const auto& staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active || (dynamicCollidesWith & staticHot.layers) == 0) continue;
        outIndexPairs.emplace_back(dynamicIdx, staticIdx);
      }
    }
  }
}

void CollisionManager::narrowphaseSOA(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                                      std::vector<CollisionInfo>& collisions) const {
  // Dispatcher: Choose single-threaded or multi-threaded path based on WorkerBudget

  if (indexPairs.empty()) {
    collisions.clear();
    m_lastNarrowphaseWasThreaded = false;
    return;
  }

  // Query WorkerBudget for optimal configuration
  auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
  size_t optimalWorkers = budgetMgr.getOptimalWorkers(
      HammerEngine::SystemType::Collision, indexPairs.size());

  auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
      HammerEngine::SystemType::Collision,
      indexPairs.size(),
      optimalWorkers);

  // Threshold: multi-thread only if WorkerBudget recommends it
  if (batchCount <= 1 || indexPairs.size() < MIN_PAIRS_FOR_THREADING) {
    m_lastNarrowphaseWasThreaded = false;
    m_lastNarrowphaseBatchCount = 1;
    narrowphaseSingleThreaded(indexPairs, collisions);
  } else {
    m_lastNarrowphaseWasThreaded = true;
    m_lastNarrowphaseBatchCount = batchCount;
    narrowphaseMultiThreaded(indexPairs, collisions, batchCount, batchSize);
  }
}

void CollisionManager::narrowphaseSingleThreaded(const std::vector<std::pair<size_t, size_t>>& indexPairs,
                                                 std::vector<CollisionInfo>& collisions) const {
  collisions.clear();
  collisions.reserve(indexPairs.size() / 4); // Conservative estimate

  // PERFORMANCE: Fast velocity threshold for deep penetration correction
  // Only trigger special handling for genuinely fast-moving bodies (250px/s @ 60fps = 4.17px/frame)
  constexpr float FAST_VELOCITY_THRESHOLD_SQ = FAST_VELOCITY_THRESHOLD * FAST_VELOCITY_THRESHOLD; // 62500.0f

  // SIMDMath abstraction: Batch process AABB intersection tests (4 pairs at a time)
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
          float const centerXA = (minXA + maxXA) * 0.5f;
          float const centerXB = (minXB + maxXB) * 0.5f;
          normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
        } else {
          minPen = overlapY;
          float const centerYA = (minYA + maxYA) * 0.5f;
          float const centerYB = (minYB + maxYB) * 0.5f;
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
    alignas(16) float minXA[4], minYA[4], maxXA[4], maxYA[4];
    alignas(16) float minXB[4], minYB[4], maxXB[4], maxYB[4];

    m_storage.getCachedAABBBounds(aIdx0, minXA[0], minYA[0], maxXA[0], maxYA[0]);
    m_storage.getCachedAABBBounds(bIdx0, minXB[0], minYB[0], maxXB[0], maxYB[0]);
    m_storage.getCachedAABBBounds(aIdx1, minXA[1], minYA[1], maxXA[1], maxYA[1]);
    m_storage.getCachedAABBBounds(bIdx1, minXB[1], minYB[1], maxXB[1], maxYB[1]);
    m_storage.getCachedAABBBounds(aIdx2, minXA[2], minYA[2], maxXA[2], maxYA[2]);
    m_storage.getCachedAABBBounds(bIdx2, minXB[2], minYB[2], maxXB[2], maxYB[2]);
    m_storage.getCachedAABBBounds(aIdx3, minXA[3], minYA[3], maxXA[3], maxYA[3]);
    m_storage.getCachedAABBBounds(bIdx3, minXB[3], minYB[3], maxXB[3], maxYB[3]);

    // SIMDMath intersection test for 4 pairs
    Float4 const maxXA_v = load4(maxXA);
    Float4 const minXB_v = load4(minXB);
    Float4 const maxXB_v = load4(maxXB);
    Float4 const minXA_v = load4(minXA);
    Float4 const maxYA_v = load4(maxYA);
    Float4 const minYB_v = load4(minYB);
    Float4 const maxYB_v = load4(maxYB);
    Float4 const minYA_v = load4(minYA);

    // Test: maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA
    Float4 const xFail1 = cmplt(maxXA_v, minXB_v);
    Float4 const xFail2 = cmplt(maxXB_v, minXA_v);
    Float4 const yFail1 = cmplt(maxYA_v, minYB_v);
    Float4 const yFail2 = cmplt(maxYB_v, minYA_v);

    Float4 const fail = bitwise_or(bitwise_or(xFail1, xFail2), bitwise_or(yFail1, yFail2));
    int failMask = movemask(fail);

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
      // Get velocity from EDM for deep penetration check
      Vector2D velA = (hotA.edmIndex != SIZE_MAX)
          ? EntityDataManager::Instance().getTransformByIndex(hotA.edmIndex).velocity
          : Vector2D(0, 0);
      if (minPen > DEEP_PENETRATION_THRESHOLD && velA.lengthSquared() > FAST_VELOCITY_THRESHOLD_SQ) {
        // Push opposite to velocity direction (player was moving INTO the collision)
        normal = (velA.getX() > 0) ? Vector2D(-1, 0) : Vector2D(1, 0);
      } else {
        // Standard center comparison for shallow penetrations
        float const centerXA = (minXA + maxXA) * 0.5f;
        float const centerXB = (minXB + maxXB) * 0.5f;
        normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
      }
    } else {
      // Separate on Y-axis
      minPen = overlapY;

      // DEEP PENETRATION FIX: Use velocity direction instead of center comparison
      // Only needed for rare edge cases (reduced from 16px with cache fixes)
      constexpr float DEEP_PENETRATION_THRESHOLD = 10.0f;
      // Get velocity from EDM for deep penetration check
      Vector2D velA = (hotA.edmIndex != SIZE_MAX)
          ? EntityDataManager::Instance().getTransformByIndex(hotA.edmIndex).velocity
          : Vector2D(0, 0);
      if (minPen > DEEP_PENETRATION_THRESHOLD && velA.lengthSquared() > FAST_VELOCITY_THRESHOLD_SQ) {
        // Push opposite to velocity direction (player was moving INTO the collision)
        normal = (velA.getY() > 0) ? Vector2D(0, -1) : Vector2D(0, 1);
      } else {
        // Standard center comparison for shallow penetrations
        float const centerYA = (minYA + maxYA) * 0.5f;
        float const centerYB = (minYB + maxYB) * 0.5f;
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

void CollisionManager::narrowphaseMultiThreaded(
    const std::vector<std::pair<size_t, size_t>>& indexPairs,
    std::vector<CollisionInfo>& collisions,
    size_t batchCount,
    size_t batchSize) const {

  auto& threadSystem = HammerEngine::ThreadSystem::Instance();

  // Reuse per-batch buffers (avoid allocations)
  if (!m_batchCollisionBuffers) {
    m_batchCollisionBuffers =
        std::make_shared<std::vector<std::vector<CollisionInfo>>>();
  }
  m_batchCollisionBuffers->resize(batchCount);

  // Reserve capacity (estimate 50% of pairs become collisions)
  size_t estimatedPerBatch = (indexPairs.size() / batchCount) / 2;
  for (size_t i = 0; i < batchCount; ++i) {
    (*m_batchCollisionBuffers)[i].clear();
    (*m_batchCollisionBuffers)[i].reserve(estimatedPerBatch);
  }

  // Submit batches to ThreadSystem
  {
    std::lock_guard<std::mutex> lock(m_narrowphaseFuturesMutex);
    m_narrowphaseFutures.clear();
    m_narrowphaseFutures.reserve(batchCount);

    // Capture shared_ptr by value to keep buffers alive during async execution
    auto batchBuffers = m_batchCollisionBuffers;

    for (size_t i = 0; i < batchCount; ++i) {
      size_t startIdx = i * batchSize;
      size_t endIdx = std::min(startIdx + batchSize, indexPairs.size());

      m_narrowphaseFutures.push_back(
          threadSystem.enqueueTaskWithResult(
              [this, &indexPairs, startIdx, endIdx, batchBuffers, i]() -> void {
                try {
                  narrowphaseBatch(indexPairs, startIdx, endIdx,
                                 (*batchBuffers)[i]);
                } catch (const std::exception& e) {
                  COLLISION_ERROR(std::format(
                      "Exception in narrowphase batch {}: {}", i, e.what()));
                } catch (...) {
                  COLLISION_ERROR(std::format(
                      "Unknown exception in narrowphase batch {}", i));
                }
              },
              HammerEngine::TaskPriority::High,
              std::format("Collision_Narrowphase_Batch_{}", i)
          )
      );
    }
  }

  // Wait for all batches to complete
  {
    std::lock_guard<std::mutex> lock(m_narrowphaseFuturesMutex);
    for (auto& future : m_narrowphaseFutures) {
      if (future.valid()) {
        future.wait();
      }
    }
  }

  // Merge per-batch results into output buffer
  collisions.clear();
  size_t totalCollisions = std::accumulate(
      m_batchCollisionBuffers->begin(), m_batchCollisionBuffers->end(), size_t{0},
      [](size_t sum, const auto& batchCollisions) { return sum + batchCollisions.size(); });
  collisions.reserve(totalCollisions);

  for (const auto& batchCollisions : *m_batchCollisionBuffers) {
    collisions.insert(collisions.end(),
                     batchCollisions.begin(),
                     batchCollisions.end());
  }
}

void CollisionManager::narrowphaseBatch(
    const std::vector<std::pair<size_t, size_t>>& indexPairs,
    size_t startIdx,
    size_t endIdx,
    std::vector<CollisionInfo>& outCollisions) const {

  // Calculate SIMD boundary for THIS batch
  const size_t batchSize = endIdx - startIdx;
  const size_t simdEnd = startIdx + (batchSize / 4) * 4;

  // SIMD loop: Process 4 pairs at a time (from narrowphaseSingleThreaded lines 1865-1989)
  for (size_t i = startIdx; i < simdEnd; i += 4) {
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
      // Fall back to scalar for this batch of 4
      for (size_t j = i; j < i + 4 && j < endIdx; ++j) {
        processNarrowphasePairScalar(indexPairs[j], outCollisions);
      }
      continue;
    }

    // Get AABB bounds for all 4 pairs
    alignas(16) float minXA[4], minYA[4], maxXA[4], maxYA[4];
    alignas(16) float minXB[4], minYB[4], maxXB[4], maxYB[4];

    m_storage.getCachedAABBBounds(aIdx0, minXA[0], minYA[0], maxXA[0], maxYA[0]);
    m_storage.getCachedAABBBounds(bIdx0, minXB[0], minYB[0], maxXB[0], maxYB[0]);
    m_storage.getCachedAABBBounds(aIdx1, minXA[1], minYA[1], maxXA[1], maxYA[1]);
    m_storage.getCachedAABBBounds(bIdx1, minXB[1], minYB[1], maxXB[1], maxYB[1]);
    m_storage.getCachedAABBBounds(aIdx2, minXA[2], minYA[2], maxXA[2], maxYA[2]);
    m_storage.getCachedAABBBounds(bIdx2, minXB[2], minYB[2], maxXB[2], maxYB[2]);
    m_storage.getCachedAABBBounds(aIdx3, minXA[3], minYA[3], maxXA[3], maxYA[3]);
    m_storage.getCachedAABBBounds(bIdx3, minXB[3], minYB[3], maxXB[3], maxYB[3]);

    // SIMDMath intersection test for 4 pairs
    Float4 const maxXA_v = load4(maxXA);
    Float4 const minXB_v = load4(minXB);
    Float4 const maxXB_v = load4(maxXB);
    Float4 const minXA_v = load4(minXA);
    Float4 const maxYA_v = load4(maxYA);
    Float4 const minYB_v = load4(minYB);
    Float4 const maxYB_v = load4(maxYB);
    Float4 const minYA_v = load4(minYA);

    // Test: maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA
    Float4 const xFail1 = cmplt(maxXA_v, minXB_v);
    Float4 const xFail2 = cmplt(maxXB_v, minXA_v);
    Float4 const yFail1 = cmplt(maxYA_v, minYB_v);
    Float4 const yFail2 = cmplt(maxYB_v, minYA_v);

    Float4 const fail = bitwise_or(bitwise_or(xFail1, xFail2), bitwise_or(yFail1, yFail2));
    int failMask = movemask(fail);

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

      outCollisions.push_back(CollisionInfo{entityA, entityB, normal, minPen, isEitherTrigger, aIdx, bIdx});
    }
  }

  // Scalar tail for remainder (if batchSize not divisible by 4)
  for (size_t i = simdEnd; i < endIdx; ++i) {
    processNarrowphasePairScalar(indexPairs[i], outCollisions);
  }
}

void CollisionManager::processNarrowphasePairScalar(
    const std::pair<size_t, size_t>& pair,
    std::vector<CollisionInfo>& outCollisions) const {

  const auto& [aIdx, bIdx] = pair;

  // Bounds check
  if (aIdx >= m_storage.hotData.size() || bIdx >= m_storage.hotData.size()) {
    return;
  }

  const auto& hotA = m_storage.hotData[aIdx];
  const auto& hotB = m_storage.hotData[bIdx];

  if (!hotA.active || !hotB.active) {
    return;
  }

  // Get cached AABB bounds
  float minXA, minYA, maxXA, maxYA;
  float minXB, minYB, maxXB, maxYB;
  m_storage.getCachedAABBBounds(aIdx, minXA, minYA, maxXA, maxYA);
  m_storage.getCachedAABBBounds(bIdx, minXB, minYB, maxXB, maxYB);

  // AABB intersection test using cached bounds
  if (maxXA < minXB || maxXB < minXA || maxYA < minYB || maxYB < minYA) {
    return;
  }

  // Compute collision details using cached bounds
  float overlapX = std::min(maxXA, maxXB) - std::max(minXA, minXB);
  float overlapY = std::min(maxYA, maxYB) - std::max(minYA, minYB);

  // Standard AABB resolution: separate on axis of minimum penetration
  constexpr float AXIS_PREFERENCE_EPSILON = 0.01f;
  float minPen;
  Vector2D normal;

  constexpr float FAST_VELOCITY_THRESHOLD_SQ = FAST_VELOCITY_THRESHOLD * FAST_VELOCITY_THRESHOLD;
  constexpr float DEEP_PENETRATION_THRESHOLD = 10.0f;

  // Get velocity from EDM for deep penetration check
  Vector2D velA = (hotA.edmIndex != SIZE_MAX)
      ? EntityDataManager::Instance().getTransformByIndex(hotA.edmIndex).velocity
      : Vector2D(0, 0);

  if (overlapX < overlapY - AXIS_PREFERENCE_EPSILON) {
    // Separate on X-axis
    minPen = overlapX;

    // DEEP PENETRATION FIX: Use velocity direction instead of center comparison
    if (minPen > DEEP_PENETRATION_THRESHOLD && velA.lengthSquared() > FAST_VELOCITY_THRESHOLD_SQ) {
      normal = (velA.getX() > 0) ? Vector2D(-1, 0) : Vector2D(1, 0);
    } else {
      float const centerXA = (minXA + maxXA) * 0.5f;
      float const centerXB = (minXB + maxXB) * 0.5f;
      normal = (centerXA < centerXB) ? Vector2D(1, 0) : Vector2D(-1, 0);
    }
  } else {
    // Separate on Y-axis
    minPen = overlapY;

    if (minPen > DEEP_PENETRATION_THRESHOLD && velA.lengthSquared() > FAST_VELOCITY_THRESHOLD_SQ) {
      normal = (velA.getY() > 0) ? Vector2D(0, -1) : Vector2D(0, 1);
    } else {
      float const centerYA = (minYA + maxYA) * 0.5f;
      float const centerYB = (minYB + maxYB) * 0.5f;
      normal = (centerYA < centerYB) ? Vector2D(0, 1) : Vector2D(0, -1);
    }
  }

  // Create collision info
  EntityID entityA = (aIdx < m_storage.entityIds.size()) ? m_storage.entityIds[aIdx] : 0;
  EntityID entityB = (bIdx < m_storage.entityIds.size()) ? m_storage.entityIds[bIdx] : 0;
  bool isEitherTrigger = hotA.isTrigger || hotB.isTrigger;

  outCollisions.push_back(CollisionInfo{
      entityA, entityB, normal, minPen, isEitherTrigger, aIdx, bIdx
  });
}

// ========== NEW SOA UPDATE METHOD ==========

void CollisionManager::updateSOA(float dt) {
  (void)dt;

  using clock = std::chrono::steady_clock; // Needed for WorkerBudget timing
#ifdef DEBUG
  auto t0 = clock::now();
#endif

  // Refresh cached AABB bounds from EntityDataManager (single source of truth)
  // AIManager integrates movement and writes to EDM - we read from EDM here
  refreshCachedAABBs();

  // Check storage state at start of update
  size_t bodyCount = m_storage.size();

  // Early exit if no bodies to process (optimization for idle states)
  if (bodyCount == 0) {
    return;
  }

  // Prepare collision processing for this frame
  prepareCollisionBuffers(bodyCount); // Prepare collision buffers

  // Count active dynamic bodies (with configurable culling)
  CullingArea const cullingArea = createDefaultCullingArea();

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

  // NOTE: Coarse region cache removed in Phase 3 - static bodies queried directly via m_staticSpatialHash

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

  // Use pooled vector for index pairs (avoids per-frame allocation)
  auto& indexPairs = m_collisionPool.broadphaseIndexPairs;

  // BROADPHASE: Generate collision pairs using spatial hash
#ifdef DEBUG
  auto t1 = clock::now();
#endif
  broadphaseSOA(indexPairs);
  auto t2 = clock::now(); // Needed for WorkerBudget timing

  // NARROWPHASE: Detailed collision detection and response calculation
  const size_t pairCount = indexPairs.size();
  narrowphaseSOA(indexPairs, m_collisionPool.collisionBuffer);
  auto t3 = clock::now();

  // Report batch completion for adaptive tuning (WorkerBudget integration)
  if (m_lastNarrowphaseWasThreaded && pairCount > 0) {
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    double narrowphaseMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
    budgetMgr.reportBatchCompletion(
        HammerEngine::SystemType::Collision,
        pairCount,
        m_lastNarrowphaseBatchCount,
        narrowphaseMs
    );
  }

  // RESOLUTION: Apply collision responses and update positions
  // Batch resolution: Process 4 collisions at a time for cache efficiency
  size_t collIdx = 0;
  const size_t collSimdEnd = (m_collisionPool.collisionBuffer.size() / 4) * 4;

  for (; collIdx < collSimdEnd; collIdx += 4) {
    // Process 4 collisions in a batch
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
#ifdef DEBUG
  auto t4 = clock::now();
#endif

  // NOTE: syncEntitiesToSOA() removed - resolveSOA() writes directly to EDM
  // Entity accessors (getPosition/setPosition) read from EDM as source of truth
#ifdef DEBUG
  auto t5 = clock::now();
#endif

  // TRIGGER PROCESSING: Handle trigger enter/exit events
  processTriggerEventsSOA();

#ifdef DEBUG
  auto t6 = clock::now();

  // Track detailed performance metrics (debug only - zero overhead in release)
  updatePerformanceMetricsSOA(t0, t1, t2, t3, t4, t5, t6,
                               bodyCount, activeMovableBodies, pairCount, m_collisionPool.collisionBuffer.size(),
                               activeBodies, dynamicBodiesCulled, staticBodiesCulled, cullingMs,
                               totalStaticBodies, totalMovableBodies);
#endif
}


// ========== SOA UPDATE HELPER METHODS ==========

// Sync spatial hash for active bodies after culling
void CollisionManager::syncSpatialHashesWithActiveIndices() {
  // OPTIMIZATION: Dynamic spatial hash no longer needed!
  // The optimized broadphase uses:
  // - pools.movableIndices for movable-vs-movable (direct SIMD iteration)
  // - pools.staticIndices or m_staticSpatialHash for movable-vs-static
  // This eliminates ~160 hash insert calls per frame (each with allocation + mutex overhead)

  // Static hash is managed separately via rebuildStaticSpatialHash()
}


void CollisionManager::rebuildStaticSpatialHash() {
  // Only called when static objects are added/removed
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

// NOTE: updateStaticCollisionCacheForMovableBodies(), evictStaleCacheEntries(),
// and populateCacheForRegion() removed in Phase 3 - static bodies queried directly via m_staticSpatialHash

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

  // Apply position corrections using SIMDMath abstraction
  // Access positions from EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();

  // Skip bodies without valid EDM index (test bodies, not yet attached)
  bool hasValidA = (hotA.edmIndex != SIZE_MAX);
  bool hasValidB = (hotB.edmIndex != SIZE_MAX);
  if (!hasValidA && !hasValidB) {
    return;  // Neither body has EDM entry, skip resolution
  }

  if (typeA != BodyType::STATIC && typeB != BodyType::STATIC && hasValidA && hasValidB) {
    // Both dynamic/kinematic with valid EDM - split the correction
    Float4 const normal = set(collision.normal.getX(), collision.normal.getY(), 0, 0);
    Float4 const pushVec = broadcast(push);
    Float4 const correction = mul(normal, pushVec);

    // Read positions from EDM
    auto& transformA = edm.getTransformByIndex(hotA.edmIndex);
    auto& transformB = edm.getTransformByIndex(hotB.edmIndex);
    Float4 posA = set(transformA.position.getX(), transformA.position.getY(), 0, 0);
    Float4 posB = set(transformB.position.getX(), transformB.position.getY(), 0, 0);

    posA = sub(posA, correction);
    posB = add(posB, correction);

    alignas(16) float resultA[4], resultB[4];
    store4(resultA, posA);
    store4(resultB, posB);

    // Write positions back to EDM
    transformA.position.setX(resultA[0]);
    transformA.position.setY(resultA[1]);
    transformB.position.setX(resultB[0]);
    transformB.position.setY(resultB[1]);

    // Update cached AABBs
    const auto& edmHotA = edm.getHotDataByIndex(hotA.edmIndex);
    const auto& edmHotB = edm.getHotDataByIndex(hotB.edmIndex);
    hotA.aabbMinX = resultA[0] - edmHotA.halfWidth;
    hotA.aabbMinY = resultA[1] - edmHotA.halfHeight;
    hotA.aabbMaxX = resultA[0] + edmHotA.halfWidth;
    hotA.aabbMaxY = resultA[1] + edmHotA.halfHeight;
    hotB.aabbMinX = resultB[0] - edmHotB.halfWidth;
    hotB.aabbMinY = resultB[1] - edmHotB.halfHeight;
    hotB.aabbMaxX = resultB[0] + edmHotB.halfWidth;
    hotB.aabbMaxY = resultB[1] + edmHotB.halfHeight;
  } else if (typeA != BodyType::STATIC && hasValidA) {
    // Only A moves (has valid EDM)
    Float4 const normal = set(collision.normal.getX(), collision.normal.getY(), 0, 0);
    Float4 const penVec = broadcast(collision.penetration);
    Float4 const correction = mul(normal, penVec);

    auto& transformA = edm.getTransformByIndex(hotA.edmIndex);
    Float4 posA = set(transformA.position.getX(), transformA.position.getY(), 0, 0);
    posA = sub(posA, correction);

    alignas(16) float resultA[4];
    store4(resultA, posA);

    transformA.position.setX(resultA[0]);
    transformA.position.setY(resultA[1]);

    // Update cached AABB
    const auto& edmHotA = edm.getHotDataByIndex(hotA.edmIndex);
    hotA.aabbMinX = resultA[0] - edmHotA.halfWidth;
    hotA.aabbMinY = resultA[1] - edmHotA.halfHeight;
    hotA.aabbMaxX = resultA[0] + edmHotA.halfWidth;
    hotA.aabbMaxY = resultA[1] + edmHotA.halfHeight;
  } else if (typeB != BodyType::STATIC && hasValidB) {
    // Only B moves (has valid EDM)
    Float4 const normal = set(collision.normal.getX(), collision.normal.getY(), 0, 0);
    Float4 const penVec = broadcast(collision.penetration);
    Float4 const correction = mul(normal, penVec);

    auto& transformB = edm.getTransformByIndex(hotB.edmIndex);
    Float4 posB = set(transformB.position.getX(), transformB.position.getY(), 0, 0);
    posB = add(posB, correction);

    alignas(16) float resultB[4];
    store4(resultB, posB);

    transformB.position.setX(resultB[0]);
    transformB.position.setY(resultB[1]);

    // Update cached AABB
    const auto& edmHotB = edm.getHotDataByIndex(hotB.edmIndex);
    hotB.aabbMinX = resultB[0] - edmHotB.halfWidth;
    hotB.aabbMinY = resultB[1] - edmHotB.halfHeight;
    hotB.aabbMaxX = resultB[0] + edmHotB.halfWidth;
    hotB.aabbMaxY = resultB[1] + edmHotB.halfHeight;
  }

  // Apply velocity damping for dynamic bodies using SIMDMath abstraction
  auto dampenVelocitySIMD = [](Vector2D& velocity, const Vector2D& collisionNormal, float restitution, bool isStatic) {
    // SIMD dot product
    Float4 vel = set(velocity.getX(), velocity.getY(), 0, 0);
    Float4 const norm = set(collisionNormal.getX(), collisionNormal.getY(), 0, 0);
    Float4 const dot_vec = mul(vel, norm);
    float const vdotn = horizontal_add(dot_vec);

    // Normal points from A to B, so vdotn > 0 means moving TOWARD collision
    // vdotn < 0 means moving AWAY from collision
    if (vdotn < 0) {
      return; // Moving away - no damping needed
    }

    // SIMD velocity damping
    Float4 const vdotnVec = broadcast(vdotn);
    Float4 const normalVel = mul(norm, vdotnVec);

    // For static collisions, zero velocity; for dynamic, use restitution
    Float4 const dampFactor = broadcast(isStatic ? 1.0f : (1.0f + restitution));
    Float4 const dampedVel = mul(normalVel, dampFactor);
    vel = sub(vel, dampedVel);

    alignas(16) float result[4];
    store4(result, vel);

    velocity.setX(result[0]);
    velocity.setY(result[1]);
  };

  if (typeA == BodyType::DYNAMIC && hotA.edmIndex != SIZE_MAX) {
    bool const staticCollision = (typeB == BodyType::STATIC);
    auto& velA = edm.getTransformByIndex(hotA.edmIndex).velocity;
    dampenVelocitySIMD(velA, collision.normal, m_storage.coldData[indexA].restitution, staticCollision);
  }
  if (typeB == BodyType::DYNAMIC && hotB.edmIndex != SIZE_MAX) {
    bool const staticCollision = (typeA == BodyType::STATIC);
    // For entity B, flip the normal (normal points A->B, but we need B->A)
    Vector2D const flippedNormal = collision.normal * -1.0f;
    auto& velB = edm.getTransformByIndex(hotB.edmIndex).velocity;
    dampenVelocitySIMD(velB, flippedNormal, m_storage.coldData[indexB].restitution, staticCollision);
  }

  // Add tangential slide for NPC-vs-NPC collisions (but not player)
  if (typeA == BodyType::DYNAMIC && typeB == BodyType::DYNAMIC &&
      hotA.edmIndex != SIZE_MAX && hotB.edmIndex != SIZE_MAX) {
    bool isPlayerA = (hotA.layers & CollisionLayer::Layer_Player) != 0;
    bool isPlayerB = (hotB.layers & CollisionLayer::Layer_Player) != 0;

    if (!isPlayerA && !isPlayerB) {
      Vector2D const tangent(-collision.normal.getY(), collision.normal.getX());
      float slideBoost = std::min(5.0f, std::max(0.5f, collision.penetration * 5.0f));

      EntityID entityA = collision.a;  // Use already-known entity IDs
      EntityID entityB = collision.b;

      auto& velA = edm.getTransformByIndex(hotA.edmIndex).velocity;
      auto& velB = edm.getTransformByIndex(hotB.edmIndex).velocity;

      if (entityA < entityB) {
        velA += tangent * slideBoost;
        velB -= tangent * slideBoost;
      } else {
        velA -= tangent * slideBoost;
        velB += tangent * slideBoost;
      }
    }
  }

  // Clamp velocities to reasonable limits using SIMDMath abstraction
  auto clampVelocitySIMD = [](Vector2D& velocity) {
    const float maxSpeed = 300.0f;

    // SIMD length calculation
    Float4 vel = set(velocity.getX(), velocity.getY(), 0, 0);
    Float4 const sq = mul(vel, vel);
    float const lenSq = horizontal_add(sq);
    float speed = std::sqrt(lenSq);

    if (speed > maxSpeed && speed > 0.0f) {
      // SIMD velocity scaling
      Float4 const scale = broadcast(maxSpeed / speed);
      vel = mul(vel, scale);

      alignas(16) float result[4];
      store4(result, vel);
      velocity.setX(result[0]);
      velocity.setY(result[1]);
    }
  };

  // Clamp velocities in EDM (already modified above)
  if (typeA == BodyType::DYNAMIC && hotA.edmIndex != SIZE_MAX) {
    clampVelocitySIMD(edm.getTransformByIndex(hotA.edmIndex).velocity);
  }
  if (typeB == BodyType::DYNAMIC && hotB.edmIndex != SIZE_MAX) {
    clampVelocitySIMD(edm.getTransformByIndex(hotB.edmIndex).velocity);
  }

  // NOTE: Position corrections already written to EDM above
  // No need for redundant write-back - EDM is now the source of truth
}

// NOTE: syncEntitiesToSOA() removed in Phase 4 - Entity::setPosition/getPosition
// now read/write directly through EDM, so syncing EDM → Entity is redundant

void CollisionManager::refreshCachedAABB(size_t index) {
  // Update single body's cached AABB from EntityDataManager
  // Following AIManager pattern: direct EDM access via edmIndex
  auto& hot = m_storage.hotData[index];

  // Skip bodies without valid EDM index (test bodies, not yet attached)
  if (hot.edmIndex == SIZE_MAX) {
    hot.aabbDirty = 0;  // Mark as clean even if not updated
    return;
  }

  // Get position and half-extents from EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  const auto& transform = edm.getTransformByIndex(hot.edmIndex);
  const auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);

  // Update cached AABB bounds
  float posX = transform.position.getX();
  float posY = transform.position.getY();
  float halfW = edmHot.halfWidth;
  float halfH = edmHot.halfHeight;

  hot.aabbMinX = posX - halfW;
  hot.aabbMinY = posY - halfH;
  hot.aabbMaxX = posX + halfW;
  hot.aabbMaxY = posY + halfH;
  hot.aabbDirty = 0;
}

void CollisionManager::refreshCachedAABBs() {
  // OPTIMIZATION: Only refresh AABBs for MOVABLE bodies (dynamic + kinematic)
  // Static bodies don't move - their AABBs are set once at creation and never change
  // This reduces iterations from 15K (all bodies) to ~170 (movable only)

  auto& edm = EntityDataManager::Instance();
  const auto& movableIndices = m_movableBodyIndices;
  const size_t count = movableIndices.size();

  if (count == 0) return;

  // SIMD batch processing (4 bodies at a time)
  size_t mi = 0;
  const size_t simdEnd = (count / 4) * 4;

  for (; mi < simdEnd; mi += 4) {
    size_t i0 = movableIndices[mi], i1 = movableIndices[mi + 1];
    size_t i2 = movableIndices[mi + 2], i3 = movableIndices[mi + 3];

    // Load EDM indices
    size_t idx0 = m_storage.hotData[i0].edmIndex;
    size_t idx1 = m_storage.hotData[i1].edmIndex;
    size_t idx2 = m_storage.hotData[i2].edmIndex;
    size_t idx3 = m_storage.hotData[i3].edmIndex;

    // If any index is invalid, fall back to scalar for this batch
    if (idx0 == SIZE_MAX || idx1 == SIZE_MAX || idx2 == SIZE_MAX || idx3 == SIZE_MAX) {
      for (size_t j = 0; j < 4; ++j) {
        refreshCachedAABB(movableIndices[mi + j]);
      }
      continue;
    }

    // Fetch transforms from EDM
    const auto& t0 = edm.getTransformByIndex(idx0);
    const auto& t1 = edm.getTransformByIndex(idx1);
    const auto& t2 = edm.getTransformByIndex(idx2);
    const auto& t3 = edm.getTransformByIndex(idx3);

    // Fetch half-extents from EDM
    const auto& h0 = edm.getHotDataByIndex(idx0);
    const auto& h1 = edm.getHotDataByIndex(idx1);
    const auto& h2 = edm.getHotDataByIndex(idx2);
    const auto& h3 = edm.getHotDataByIndex(idx3);

    // SIMD positions
    Float4 posX = set(t0.position.getX(), t1.position.getX(),
                      t2.position.getX(), t3.position.getX());
    Float4 posY = set(t0.position.getY(), t1.position.getY(),
                      t2.position.getY(), t3.position.getY());

    // SIMD half-extents
    Float4 halfW = set(h0.halfWidth, h1.halfWidth, h2.halfWidth, h3.halfWidth);
    Float4 halfH = set(h0.halfHeight, h1.halfHeight, h2.halfHeight, h3.halfHeight);

    // Compute AABB bounds
    Float4 minX = sub(posX, halfW);
    Float4 minY = sub(posY, halfH);
    Float4 maxX = add(posX, halfW);
    Float4 maxY = add(posY, halfH);

    // Extract and store results
    alignas(16) float minXArr[4], minYArr[4], maxXArr[4], maxYArr[4];
    store4(minXArr, minX);
    store4(minYArr, minY);
    store4(maxXArr, maxX);
    store4(maxYArr, maxY);

    // Store back to storage indices
    auto& hot0 = m_storage.hotData[i0];
    auto& hot1 = m_storage.hotData[i1];
    auto& hot2 = m_storage.hotData[i2];
    auto& hot3 = m_storage.hotData[i3];

    hot0.aabbMinX = minXArr[0]; hot0.aabbMinY = minYArr[0];
    hot0.aabbMaxX = maxXArr[0]; hot0.aabbMaxY = maxYArr[0]; hot0.aabbDirty = 0;
    hot1.aabbMinX = minXArr[1]; hot1.aabbMinY = minYArr[1];
    hot1.aabbMaxX = maxXArr[1]; hot1.aabbMaxY = maxYArr[1]; hot1.aabbDirty = 0;
    hot2.aabbMinX = minXArr[2]; hot2.aabbMinY = minYArr[2];
    hot2.aabbMaxX = maxXArr[2]; hot2.aabbMaxY = maxYArr[2]; hot2.aabbDirty = 0;
    hot3.aabbMinX = minXArr[3]; hot3.aabbMinY = minYArr[3];
    hot3.aabbMaxX = maxXArr[3]; hot3.aabbMaxY = maxYArr[3]; hot3.aabbDirty = 0;
  }

  // Scalar tail
  for (; mi < count; ++mi) {
    refreshCachedAABB(movableIndices[mi]);
  }
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
  // Reuse member buffer to avoid per-frame hash table allocation
  m_currentTriggerPairsBuffer.clear();

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
    m_currentTriggerPairsBuffer.insert(key);

    if (!m_activeTriggerPairs.count(key)) {
      // Check cooldown
      auto cdIt = m_triggerCooldownUntil.find(triggerId);
      bool cooled = (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);

      if (cooled) {
        HammerEngine::TriggerTag triggerTag = static_cast<HammerEngine::TriggerTag>(triggerHot->triggerTag);
        // Get player position from EDM (single source of truth)
        auto& edm = EntityDataManager::Instance();
        const auto& playerTransform = edm.getTransformByIndex(playerHot->edmIndex);
        Vector2D playerPos = playerTransform.position;

        WorldTriggerEvent evt(playerId, triggerId, triggerTag, playerPos, TriggerPhase::Enter);
        EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);

        COLLISION_DEBUG(std::format("Player {} ENTERED trigger {} (tag: {}) at position ({}, {})",
                                    playerId, triggerId, static_cast<int>(triggerTag),
                                    playerPos.getX(), playerPos.getY()));

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
    if (!m_currentTriggerPairsBuffer.count(it->first)) {
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
          // Get position from EDM (single source of truth)
          auto& edm = EntityDataManager::Instance();
          const auto& transform = edm.getTransformByIndex(hot.edmIndex);
          triggerPos = transform.position;
          triggerTag = static_cast<HammerEngine::TriggerTag>(hot.triggerTag);
        }
      }

      WorldTriggerEvent evt(playerId, triggerId, triggerTag, triggerPos, TriggerPhase::Exit);
      EventManager::Instance().triggerWorldTrigger(evt, EventManager::DispatchMode::Deferred);

      COLLISION_DEBUG(std::format("Player {} EXITED trigger {} (tag: {}) at position ({}, {})",
                                  playerId, triggerId, static_cast<int>(triggerTag),
                                  triggerPos.getX(), triggerPos.getY()));

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

  // Basic counters - always tracked (minimal overhead)
  m_perf.lastPairs = pairCount;
  m_perf.lastCollisions = collisionCount;
  m_perf.bodyCount = bodyCount;
  m_perf.frames += 1;

#ifdef DEBUG
  // Detailed timing metrics and logging - zero overhead in release builds
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

  // PERFORMANCE OPTIMIZATION METRICS: Track optimization effectiveness
  m_perf.lastActiveBodies = activeBodies > 0 ? activeBodies : bodyCount;
  m_perf.lastDynamicBodiesCulled = dynamicBodiesCulled;
  m_perf.lastStaticBodiesCulled = staticBodiesCulled;
  m_perf.totalStaticBodies = totalStaticBodies;
  m_perf.totalMovableBodies = totalMovableBodies;
  m_perf.lastCullingMs = cullingMs;

  m_perf.updateAverage(m_perf.lastTotalMs);
  m_perf.updateBroadphaseAverage(d12);

  // Interval stats logging
  static thread_local uint64_t logFrameCounter = 0;
  ++logFrameCounter;

  // Performance warnings (throttled to reduce spam during benchmarks)
  COLLISION_WARN_IF(m_perf.lastTotalMs > 5.0 && logFrameCounter % 60 == 0,
      std::format("Slow frame: {:.2f}ms (broad:{:.2f}, narrow:{:.2f}, pairs:{})",
                  m_perf.lastTotalMs, d12, d23, pairCount));

  // Periodic statistics (every 300 frames) - concise format matching AIManager style
  if (logFrameCounter % 300 == 0 && bodyCount > 0) {
    // Threading status
    std::string threadingStatus;
    if (m_lastBroadphaseWasThreaded || m_lastNarrowphaseWasThreaded) {
      threadingStatus = std::format(" [Threaded: Broad:{} Narrow:{}]",
          m_lastBroadphaseWasThreaded ? std::format("{} batches", m_lastBroadphaseBatchCount) : "no",
          m_lastNarrowphaseWasThreaded ? std::format("{} batches", m_lastNarrowphaseBatchCount) : "no");
    } else {
      threadingStatus = " [Single-threaded]";
    }

    // Calculate effective world size from AABB (center + halfSize format)
    float worldW = m_worldBounds.halfSize.getX() * 2.0f;
    float worldH = m_worldBounds.halfSize.getY() * 2.0f;

    size_t culledStatics = m_collisionPool.staticIndices.size();
    auto d01 = std::chrono::duration<double, std::milli>(t1 - t0).count(); // Pre-broadphase
    COLLISION_DEBUG(std::format("Collision Summary - Bodies: {} ({} movable, {} statics in cull), "
                                "Total: {:.2f}ms, Pre: {:.2f}ms, Broad: {:.2f}ms, Narrow: {:.2f}ms, "
                                "Pairs: {}, Collisions: {}{}, Bounds: {}x{}",
                                bodyCount, activeMovableBodies, culledStatics,
                                m_perf.lastTotalMs, d01, d12, d23,
                                pairCount, collisionCount, threadingStatus,
                                static_cast<int>(worldW), static_cast<int>(worldH)));
  }
#endif // DEBUG
}

void CollisionManager::updateKinematicBatchSOA(const std::vector<KinematicUpdate>& updates) {
  if (updates.empty()) return;

  // Batch update all kinematic bodies - update EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  for (const auto& bodyUpdate : updates) {
    auto it = m_storage.entityToIndex.find(bodyUpdate.id);
    if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
      size_t index = it->second;
      auto& hot = m_storage.hotData[index];
      if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC && hot.edmIndex != SIZE_MAX) {
        // Update EDM - it owns position/velocity
        auto& transform = edm.getTransformByIndex(hot.edmIndex);
        transform.position = bodyUpdate.position;
        transform.velocity = bodyUpdate.velocity;

        // Update cached AABB immediately for spatial queries
        const auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);
        float px = bodyUpdate.position.getX();
        float py = bodyUpdate.position.getY();
        float hw = edmHot.halfWidth;
        float hh = edmHot.halfHeight;

        hot.aabbMinX = px - hw;
        hot.aabbMinY = py - hh;
        hot.aabbMaxX = px + hw;
        hot.aabbMaxY = py + hh;
        hot.aabbDirty = 0;
        hot.active = true;
        // NOTE: Dynamic spatial hash update removed - broadphase uses pools.movableIndices directly
      }
    }
  }
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

  // Merge all batch updates into EDM (single source of truth)
  auto& edm = EntityDataManager::Instance();
  for (const auto& batchBuffer : batchUpdates) {
    for (const auto& bodyUpdate : batchBuffer) {
      auto it = m_storage.entityToIndex.find(bodyUpdate.id);
      if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
        size_t index = it->second;
        auto& hot = m_storage.hotData[index];
        if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC && hot.edmIndex != SIZE_MAX) {
          // Update EDM - it owns position/velocity
          auto& transform = edm.getTransformByIndex(hot.edmIndex);
          transform.position = bodyUpdate.position;
          transform.velocity = bodyUpdate.velocity;

          // Update cached AABB immediately for spatial queries
          const auto& edmHot = edm.getHotDataByIndex(hot.edmIndex);
          float px = bodyUpdate.position.getX();
          float py = bodyUpdate.position.getY();
          float hw = edmHot.halfWidth;
          float hh = edmHot.halfHeight;

          // Update cached AABB
          hot.aabbMinX = px - hw;
          hot.aabbMinY = py - hh;
          hot.aabbMaxX = px + hw;
          hot.aabbMaxY = py + hh;
          hot.aabbDirty = 0;
          hot.active = true;
          // NOTE: Dynamic spatial hash update removed - broadphase uses pools.movableIndices directly
        }
      }
    }
  }
}

void CollisionManager::applyKinematicUpdates(std::vector<KinematicUpdate>& updates) {
  // Convenience wrapper for single-vector updates (avoids allocation in caller)
  // Just wrap in a single-element batch and call the batched version
  if (updates.empty()) return;

  std::vector<std::vector<KinematicUpdate>> singleBatch(1);
  singleBatch[0] = std::move(updates);  // Move to avoid copy
  applyBatchedKinematicUpdates(singleBatch);
  updates = std::move(singleBatch[0]);  // Move back to preserve buffer for reuse
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
    // Update EDM - it owns velocity
    auto& edm = EntityDataManager::Instance();
    auto& transform = edm.getTransformByIndex(m_storage.hotData[index].edmIndex);
    transform.velocity = velocity;
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

  // OPTIMIZATION: O(1) lookup using cached player index (regression fix for commit 768ad87)
  // Was: O(18K) iteration to find player entity
  if (m_playerBodyIndex.has_value()) {
    size_t playerIndex = *m_playerBodyIndex;
    if (playerIndex < m_storage.hotData.size()) {
      const auto& hot = m_storage.hotData[playerIndex];
      if (hot.active && (hot.layers & CollisionLayer::Layer_Player)) {
        // Get position from EDM (single source of truth)
        if (hot.edmIndex != SIZE_MAX) {
          auto& edm = EntityDataManager::Instance();
          const auto& transform = edm.getTransformByIndex(hot.edmIndex);
          playerPos = transform.position;
          playerFound = true;
        }
      }
    }
  }

  if (playerFound) {
    // Create player-centered culling area (use configurable buffer)
    area.minX = playerPos.getX() - m_cullingBuffer;
    area.minY = playerPos.getY() - m_cullingBuffer;
    area.maxX = playerPos.getX() + m_cullingBuffer;
    area.maxY = playerPos.getY() + m_cullingBuffer;
  } else {
    // Player not found - use world center with configurable culling area
    area.minX = 0.0f - m_cullingBuffer;
    area.minY = 0.0f - m_cullingBuffer;
    area.maxX = 0.0f + m_cullingBuffer;
    area.maxY = 0.0f + m_cullingBuffer;

#ifdef DEBUG
    // Log warning only once every 300 frames to avoid spam
    static thread_local uint64_t logFrameCounter = 0;
    if (++logFrameCounter % 300 == 1) {
      COLLISION_WARN("Player entity not found for culling area - using world center (logged every 300 frames)");
    }
#endif
  }

  return area;
}

// ========== PERFORMANCE: VECTOR POOLING METHODS ==========

std::vector<size_t>& CollisionManager::getPooledVector() {
  /* THREAD SAFETY: Lock-free vector pool with atomic index
   *
   * THREAD SAFE because:
   * - m_nextPoolIndex is std::atomic<size_t> with relaxed memory order
   * - Pool initialized once in init(), never reallocated
   * - fetch_add() provides lock-free thread-safe index allocation
   * - Each thread gets a unique vector from the pool (no contention)
   *
   * PERFORMANCE:
   * - No mutex overhead (lock-free atomic operations)
   * - Round-robin allocation distributes load across pool
   * - Vectors retain capacity across frames (no allocations)
   */

  // Pool is initialized in init(), not here (removed lazy initialization)
  // Use atomic round-robin allocation for thread-safe access
  size_t idx = m_nextPoolIndex.fetch_add(1, std::memory_order_relaxed) % m_vectorPool.size();

  auto& vec = m_vectorPool[idx];
  vec.clear(); // Clear but retain capacity
  return vec;
}

void CollisionManager::returnPooledVector(std::vector<size_t>& vec) {
  // Vector is automatically returned to pool via reference
  // Just clear it to avoid holding onto data
  vec.clear();
}

