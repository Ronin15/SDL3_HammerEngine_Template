/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/* ARCHITECTURAL NOTE: CollisionManager Threading Strategy
 *
 * CollisionManager uses HYBRID threading:
 * - Broadphase: Multi-threaded via WorkerBudget (when >150 movables)
 * - Narrowphase: Single-threaded scalar processing
 *
 * Broadphase parallelization:
 * - Sweep-and-prune with early termination
 * - Per-batch output buffers eliminate lock contention
 * - SIMD used for movable-vs-static checks (4-wide)
 * - Threshold (MIN_MOVABLE_FOR_BROADPHASE_THREADING) prevents overhead
 *
 * Narrowphase is single-threaded because:
 * - Observed workloads don't benefit from threading overhead
 * - Scalar pair processing is sufficient for current entity counts
 * - SIMD is used in resolve() for position correction, not narrowphase
 *
 * Performance: Handles 27K+ bodies @ 60 FPS on Apple Silicon.
 */

#include "managers/CollisionManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "events/WorldEvent.hpp"
#include "events/WorldTriggerEvent.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldManager.hpp"
#include "utils/SIMDMath.hpp"
#include "world/WorldData.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <map>
#include <numeric>
#include <queue>
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
  size_t operator()(const std::pair<int, int> &p) const noexcept {
    constexpr uint64_t kFibMult =
        11400714819323198485ULL; // 2^64 / φ (golden ratio)
    uint64_t h1 =
        static_cast<uint64_t>(static_cast<uint32_t>(p.first)) * kFibMult;
    uint64_t h2 =
        static_cast<uint64_t>(static_cast<uint32_t>(p.second)) * kFibMult;
    return h1 ^ (h2 >> 1); // Shift h2 to break symmetry (a,b) != (b,a)
  }
};

bool CollisionManager::init() {
  if (m_initialized)
    return true;
  m_storage.clear();
  subscribeWorldEvents();
  COLLISION_INFO(
      "STORAGE LIFECYCLE: init() cleared SOA storage and spatial hash");

  // PERFORMANCE: Pre-allocate vector pool to prevent FPS dips from
  // reallocations Initialize vector pool (moved from lazy initialization in
  // getPooledVector)
  m_vectorPool.clear();
  m_vectorPool.reserve(32);
  for (size_t i = 0; i < 16; ++i) {
    m_vectorPool.emplace_back();
    m_vectorPool.back().reserve(64); // Pre-allocate reasonable capacity
  }
  m_nextPoolIndex.store(0, std::memory_order_relaxed);

  // Pre-reserve reusable containers to avoid per-frame allocations
  m_currentTriggerPairsBuffer.reserve(1000); // Typical trigger count
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

  COLLISION_INFO(std::format(
      "STORAGE LIFECYCLE: clean() clearing {} SOA bodies", m_storage.size()));

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
  COLLISION_INFO(std::format("STORAGE LIFECYCLE: prepareForStateTransition() "
                             "clearing {} SOA bodies (dynamic + static)",
                             soaBodyCount));

  // Clear all collision bodies and spatial hashes
  m_storage.clear();
  m_staticSpatialHash.clear();
  m_dynamicSpatialHash.clear();
  m_eventOnlySpatialHash.clear();

  // Clear collision buffers to prevent dangling references to deleted bodies
  m_collisionPool.resetFrame();

  // Re-initialize vector pool (must not leave it empty to prevent
  // divide-by-zero in getPooledVector)
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
  auto &em = EventManager::Instance();
  for (const auto &token : m_handlerTokens) {
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

  // Reset world bounds to minimal (will be set by
  // WorldLoadedEvent/WorldGeneratedEvent)
  m_worldBounds = AABB(0.0f, 0.0f, 0.0f, 0.0f);

  // Reset verbose logging to default
  m_verboseLogs = false;

  size_t finalBodyCount = m_storage.size();
  COLLISION_INFO(std::format(
      "CollisionManager state transition complete - {} bodies remaining",
      finalBodyCount));
}

void CollisionManager::setWorldBounds(float minX, float minY, float maxX,
                                      float maxY) {
  const float cx = (minX + maxX) * 0.5f;
  const float cy = (minY + maxY) * 0.5f;
  const float hw = (maxX - minX) * 0.5f;
  const float hh = (maxY - minY) * 0.5f;
  m_worldBounds = AABB(cx, cy, hw, hh);

  COLLISION_DEBUG(std::format("World bounds set: [{},{}] - [{},{}]", minX, minY,
                              maxX, maxY));
}

EntityID CollisionManager::createTriggerArea(const AABB &aabb,
                                             HammerEngine::TriggerTag tag,
                                             HammerEngine::TriggerType type,
                                             uint32_t layerMask,
                                             uint32_t collideMask) {
  const Vector2D center(aabb.center.getX(), aabb.center.getY());
  const float halfW = aabb.halfSize.getX();
  const float halfH = aabb.halfSize.getY();

  // Register trigger with EDM first (single source of truth)
  auto &edm = EntityDataManager::Instance();
  EntityHandle handle = edm.createTrigger(center, halfW, halfH, tag, type);
  EntityID id = handle.getId();
  size_t edmIndex = edm.getStaticIndex(handle);

  // Create collision body in m_storage with EDM reference
  const Vector2D halfSize(halfW, halfH);
  addStaticBody(id, center, halfSize, layerMask, collideMask, true,
                static_cast<uint8_t>(tag), static_cast<uint8_t>(type),
                edmIndex);
  return id;
}

EntityID CollisionManager::createTriggerAreaAt(
    float cx, float cy, float halfW, float halfH, HammerEngine::TriggerTag tag,
    HammerEngine::TriggerType type, uint32_t layerMask, uint32_t collideMask) {
  return createTriggerArea(AABB(cx, cy, halfW, halfH), tag, type, layerMask,
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
    if (ty < 0 || ty >= h)
      return false;
    const int rowWidth = static_cast<int>(world->grid[ty].size());
    if (tx < 0 || tx >= rowWidth)
      return false;
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
      bool const isEdge = !isWaterTile(x - 1, y) || // Left neighbor
                          !isWaterTile(x + 1, y) || // Right neighbor
                          !isWaterTile(x, y - 1) || // Top neighbor
                          !isWaterTile(x, y + 1);   // Bottom neighbor

      if (!isEdge) {
        ++skippedInterior;
        continue; // Skip interior water tiles - player can't enter from here
      }

      const float cx = x * tileSize + tileSize * 0.5f;
      const float cy = y * tileSize + tileSize * 0.5f;
      // Water triggers are EventOnly - skip broadphase, detect player overlap
      // only
      createTriggerAreaAt(cx, cy, tileSize * 0.5f, tileSize * 0.5f, tag,
                          HammerEngine::TriggerType::EventOnly,
                          CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
      ++created;
    }
  }

  COLLISION_DEBUG_IF(
      skippedInterior > 0,
      std::format(
          "Water triggers: {} edge triggers created, {} interior tiles skipped",
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

          const auto &neighbor = world->grid[ny][nx];
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
      const auto &firstTile = buildingTiles[0];
      int minX = firstTile.first;
      int maxX = firstTile.first;
      int minY = firstTile.second;
      int maxY = firstTile.second;

      for (const auto &[tx, ty] : buildingTiles) {
        minX = std::min(minX, tx);
        maxX = std::max(maxX, tx);
        minY = std::min(minY, ty);
        maxY = std::max(maxY, ty);
      }

      // Check if building is a solid rectangle (all tiles present)
      const int expectedTiles = (maxX - minX + 1) * (maxY - minY + 1);
      const bool isRectangle =
          (static_cast<int>(buildingTiles.size()) == expectedTiles);

      COLLISION_DEBUG(std::format("Building {}: bounds ({},{}) to ({},{}), "
                                  "tiles={}, expected={}, isRectangle={}",
                                  tile.buildingId, minX, minY, maxX, maxY,
                                  buildingTiles.size(), expectedTiles,
                                  isRectangle ? "YES" : "NO"));

      auto &edm = EntityDataManager::Instance();

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

        // Register with EDM first (single source of truth)
        EntityHandle handle =
            edm.createStaticBody(center, halfWidth, halfHeight);
        EntityID id = handle.getId();
        size_t edmIndex = edm.getStaticIndex(handle);

        // Create collision body with EDM reference
        addStaticBody(id, center, halfSize, CollisionLayer::Layer_Environment,
                      0xFFFFFFFFu, false, 0,
                      static_cast<uint8_t>(HammerEngine::TriggerType::Physical),
                      edmIndex);
        ++created;

        COLLISION_INFO(std::format(
            "Building {}: created 1 collision body (rectangle {}x{} tiles) "
            "at center({},{}) halfSize({},{}) AABB[{},{} to {},{}]",
            tile.buildingId, maxX - minX + 1, maxY - minY + 1, cx, cy,
            halfWidth, halfHeight, worldMinX, worldMinY, worldMaxX, worldMaxY));
      } else {
        // COMPLEX CASE: Non-rectangular building - use row decomposition
        std::map<int, std::vector<int>> rowToColumns;
        for (const auto &[tx, ty] : buildingTiles) {
          rowToColumns[ty].push_back(tx);
        }

        for (auto &[row, columns] : rowToColumns) {
          std::sort(columns.begin(), columns.end());
        }

        uint16_t subBodyIndex = 0;
        for (const auto &[row, columns] : rowToColumns) {
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

            // Register with EDM first
            EntityHandle handle =
                edm.createStaticBody(center, halfWidth, halfHeight);
            EntityID id = handle.getId();
            size_t edmIndex = edm.getStaticIndex(handle);

            // Create collision body with EDM reference
            addStaticBody(
                id, center, halfSize, CollisionLayer::Layer_Environment,
                0xFFFFFFFFu, false, 0,
                static_cast<uint8_t>(HammerEngine::TriggerType::Physical),
                edmIndex);
            ++created;
            ++subBodyIndex;

            ++i;
          }
        }

        COLLISION_INFO(std::format(
            "Building {}: created {} collision bodies (non-rectangular)",
            tile.buildingId, subBodyIndex));
      }
    }
  }

  return created;
}

bool CollisionManager::overlaps(EntityID a, EntityID b) const {
  auto itA = m_storage.entityToIndex.find(a);
  auto itB = m_storage.entityToIndex.find(b);
  if (itA == m_storage.entityToIndex.end() ||
      itB == m_storage.entityToIndex.end())
    return false;

  size_t indexA = itA->second;
  size_t indexB = itB->second;

  AABB aabbA = m_storage.computeAABB(indexA);
  AABB aabbB = m_storage.computeAABB(indexB);
  return aabbA.intersects(aabbB);
}

void CollisionManager::queryArea(const AABB &area,
                                 std::vector<EntityID> &out) const {
  out.clear();

  // If static hash is dirty (not yet rebuilt after add/remove), fall back to
  // linear scan This ensures correctness in tests and edge cases while
  // providing O(log n) in production (production always runs update() before AI
  // queries, which rebuilds the hash)
  if (m_staticHashDirty) {
    // Linear scan fallback - always correct
    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto &hot = m_storage.hotData[i];
      if (!hot.active)
        continue;

      AABB bodyAABB = m_storage.computeAABB(i);
      if (bodyAABB.intersects(area)) {
        out.push_back(m_storage.entityIds[i]);
      }
    }
    return;
  }

  // PERFORMANCE: Use spatial hash for O(log n) query (production path)
  // Thread-safe: uses thread-local buffers to avoid contention
  thread_local std::vector<size_t> staticIndices;
  thread_local HammerEngine::HierarchicalSpatialHash::QueryBuffers queryBuffers;

  staticIndices.clear();

  float minX = area.left();
  float minY = area.top();
  float maxX = area.right();
  float maxY = area.bottom();

  m_staticSpatialHash.queryRegionBoundsThreadSafe(minX, minY, maxX, maxY,
                                                  staticIndices, queryBuffers);

  out.reserve(staticIndices.size());

  for (size_t idx : staticIndices) {
    if (idx < m_storage.hotData.size() && m_storage.hotData[idx].active) {
      out.push_back(m_storage.entityIds[idx]);
    }
  }
}

bool CollisionManager::queryAreaHasStaticOverlap(const AABB &area) const {
  // If static hash is dirty (not yet rebuilt after add/remove), fall back to
  // linear scan
  if (m_staticHashDirty) {
    for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
      const auto &hot = m_storage.hotData[i];
      if (!hot.active)
        continue;
      if (static_cast<BodyType>(hot.bodyType) != BodyType::STATIC)
        continue;

      AABB bodyAABB = m_storage.computeAABB(i);
      if (bodyAABB.intersects(area)) {
        return true;
      }
    }
    return false;
  }

  // PERFORMANCE: Use spatial hash for O(log n) query (production path)
  thread_local std::vector<size_t> staticIndices;
  thread_local HammerEngine::HierarchicalSpatialHash::QueryBuffers queryBuffers;

  staticIndices.clear();

  float minX = area.left();
  float minY = area.top();
  float maxX = area.right();
  float maxY = area.bottom();

  m_staticSpatialHash.queryRegionBoundsThreadSafe(minX, minY, maxX, maxY,
                                                  staticIndices, queryBuffers);

  return std::any_of(staticIndices.begin(), staticIndices.end(),
                     [this, &area](size_t idx) {
                       if (idx >= m_storage.hotData.size() ||
                           !m_storage.hotData[idx].active) {
                         return false;
                       }
                       const auto &hot = m_storage.hotData[idx];
                       if (static_cast<BodyType>(hot.bodyType) != BodyType::STATIC) {
                         return false;
                       }
                       AABB bodyAABB = m_storage.computeAABB(idx);
                       return bodyAABB.intersects(area);
                     });
}

bool CollisionManager::getBodyCenter(EntityID id, Vector2D &outCenter) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  const auto &hot = m_storage.hotData[it->second];

  // Static bodies (including triggers): use cached AABB - they don't move
  if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC ||
      hot.edmIndex == SIZE_MAX) {
    outCenter = Vector2D((hot.aabbMinX + hot.aabbMaxX) * 0.5f,
                         (hot.aabbMinY + hot.aabbMaxY) * 0.5f);
  } else {
    // Dynamic/Kinematic: get from EDM (single source of truth)
    const auto &transform =
        EntityDataManager::Instance().getTransformByIndex(hot.edmIndex);
    outCenter = transform.position;
  }
  return true;
}

bool CollisionManager::isDynamic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) ==
         BodyType::DYNAMIC;
}

bool CollisionManager::isKinematic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) ==
         BodyType::KINEMATIC;
}

bool CollisionManager::isStatic(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return static_cast<BodyType>(m_storage.hotData[it->second].bodyType) ==
         BodyType::STATIC;
}

bool CollisionManager::isTrigger(EntityID id) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end())
    return false;

  return m_storage.hotData[it->second].isTrigger;
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

  // Count trigger types for Phase 3 optimization visibility
  size_t eventOnlyTriggers = 0;
  size_t physicalTriggers = 0;
  size_t solidObstacles = 0;
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto &hot = m_storage.hotData[i];
    if (hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      if (hot.isTrigger != 0) {
        if (hot.triggerType ==
            static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly)) {
          ++eventOnlyTriggers;
        } else {
          ++physicalTriggers;
        }
      } else {
        ++solidObstacles;
      }
    }
  }

  COLLISION_INFO("Collision Statistics:");
  COLLISION_INFO(std::format("  Total Bodies: {}", getBodyCount()));
  COLLISION_INFO(
      std::format("  Static Bodies: {} (obstacles + triggers)", staticBodies));
  COLLISION_INFO(
      std::format("    - Solid obstacles: {} (broadphase)", solidObstacles));
  COLLISION_INFO(std::format(
      "    - Physical triggers: {} (broadphase + events)", physicalTriggers));
  COLLISION_INFO(
      std::format("    - EventOnly triggers: {} (events only, skip broadphase)",
                  eventOnlyTriggers));
  COLLISION_INFO(std::format("  Kinematic Bodies: {} (NPCs)", kinematicBodies));
  COLLISION_INFO(
      std::format("  Dynamic Bodies: {} (player, projectiles)", dynamicBodies));

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
  return std::count_if(
      m_storage.hotData.begin(), m_storage.hotData.end(), [](const auto &hot) {
        return hot.active &&
               static_cast<BodyType>(hot.bodyType) == BodyType::STATIC;
      });
}

size_t CollisionManager::getKinematicBodyCount() const {
  return std::count_if(
      m_storage.hotData.begin(), m_storage.hotData.end(), [](const auto &hot) {
        return hot.active &&
               static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC;
      });
}

size_t CollisionManager::getDynamicBodyCount() const {
  return std::count_if(
      m_storage.hotData.begin(), m_storage.hotData.end(), [](const auto &hot) {
        return hot.active &&
               static_cast<BodyType>(hot.bodyType) == BodyType::DYNAMIC;
      });
}

void CollisionManager::rebuildStaticFromWorld() {
  std::lock_guard<std::mutex> lock(m_staticRebuildMutex);
  const WorldManager &wm = WorldManager::Instance();
  const auto *world = wm.getWorldData();
  if (!world)
    return;

  // PRE-RESERVE CAPACITY: Prevent vector reallocations during rebuild
  // This eliminates race condition with background queryArea() calls from
  // PathfindingGrid Conservative estimate: 25% of tiles could become water edge
  // triggers + building bodies
  const int gridH = static_cast<int>(world->grid.size());
  const int gridW = gridH > 0 ? static_cast<int>(world->grid[0].size()) : 0;
  size_t estimatedBodies = static_cast<size_t>(gridH * gridW) / 4;
  m_storage.ensureCapacity(m_storage.size() + estimatedBodies);

  // Remove any existing STATIC world bodies from SOA storage
  std::vector<EntityID> toRemove;
  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto &hot = m_storage.hotData[i];
    if (hot.active && static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      toRemove.push_back(m_storage.entityIds[i]);
    }
  }
  for (auto id : toRemove) {
    removeCollisionBody(id);
  }

  // Create solid collision bodies for obstacles and triggers for movement
  // penalties
  size_t solidBodies = createStaticObstacleBodies();
  size_t waterTriggers =
      createTriggersForWaterTiles(HammerEngine::TriggerTag::Water);

  if (solidBodies > 0 || waterTriggers > 0) {
    COLLISION_INFO(
        std::format("World colliders built: solid={}, water triggers={}",
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
        COLLISION_DEBUG(std::format(
            "Building collision body found: buildingId={}, subBodyIndex={}",
            buildingId, subBodyIndex));
      }
    }
    COLLISION_INFO(std::format("Total building collision bodies in storage: {}",
                               buildingBodyCount));
    logCollisionStatistics();
#endif

    // Force immediate static spatial hash rebuild for world changes
    rebuildStaticSpatialHashUnlocked();
  }

  // Signal that static collision bodies are ready for dependent systems
  // PathfinderManager waits for this before building its navigation grid
  EventManager::Instance().triggerStaticCollidersReady(
      solidBodies, waterTriggers, EventManager::DispatchMode::Immediate);
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

    // Update water trigger for this tile
    // For dynamic tile changes, we still track by tile coordinates
    // TODO: Consider storing tile->entityID mapping for better cleanup
    if (tile.isWater) {
      float const cx = x * tileSize + tileSize * 0.5f;
      float const cy = y * tileSize + tileSize * 0.5f;
      // Water triggers are EventOnly - skip broadphase, detect player overlap
      // only
      createTriggerAreaAt(cx, cy, tileSize * 0.5f, tileSize * 0.5f,
                          HammerEngine::TriggerTag::Water,
                          HammerEngine::TriggerType::EventOnly,
                          CollisionLayer::Layer_Environment, 0xFFFFFFFFu);
    }

    // Update solid obstacle collision body for this tile (BUILDING only)
    // Remove old per-tile collision body (legacy)
    EntityID oldObstacleId =
        (static_cast<EntityID>(2ull) << 61) |
        (static_cast<EntityID>(static_cast<uint32_t>(y)) << 31) |
        static_cast<EntityID>(static_cast<uint32_t>(x));
    removeCollisionBody(oldObstacleId);

    if (tile.obstacleType == ObstacleType::BUILDING && tile.buildingId > 0) {
      // Find all connected building tiles to create unified collision body
      // This prevents collision seams when buildings are adjacent
      std::unordered_set<std::pair<int, int>, PairHash> visited;
      std::queue<std::pair<int, int>> toVisit;
      std::vector<std::pair<int, int>> buildingTiles;

      toVisit.push({x, y});
      visited.insert({x, y});

      // Flood fill to find all connected building tiles (same buildingId or
      // adjacent buildings)
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

          const auto &neighbor = world->grid[ny][nx];

          // Only connect tiles with the SAME buildingId (forms one building
          // unit)
          if (neighbor.obstacleType == ObstacleType::BUILDING &&
              neighbor.buildingId == tile.buildingId) {
            visited.insert({nx, ny});
            toVisit.push({nx, ny});
          }
        }
      }

      // Only create collision body if this is the top-left tile of the cluster
      bool isTopLeft = true;
      for (const auto &[tx, ty] : buildingTiles) {
        if (ty < y || (ty == y && tx < x)) {
          isTopLeft = false;
          break;
        }
      }

      if (isTopLeft) {
        // REMOVE OLD COLLISION BODIES: Handle BOTH old and new EntityID formats
        // OLD format (single body): (3ull << 61) | buildingId
        // NEW format (multi-body): (3ull << 61) | (buildingId << 16) |
        // subBodyIndex

        // First, remove OLD format single bounding box (if it exists)
        EntityID oldSingleBodyId =
            (static_cast<EntityID>(3ull) << 61) |
            static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
        removeCollisionBody(oldSingleBodyId);

        // Then, remove all NEW format multi-body collision bodies
        uint16_t subBodyIndex = 0;
        while (
            subBodyIndex <
            MAX_BUILDING_SUB_BODIES) { // Safety limit to prevent infinite loop
          EntityID newMultiBodyId =
              (static_cast<EntityID>(3ull) << 61) |
              (static_cast<EntityID>(tile.buildingId) << 16) |
              static_cast<EntityID>(subBodyIndex);

          auto it = m_storage.entityToIndex.find(newMultiBodyId);
          if (it == m_storage.entityToIndex.end()) {
            break; // No more sub-bodies for this building
          }

          removeCollisionBody(newMultiBodyId);
          ++subBodyIndex;
        }

        // ROW-BASED RECTANGULAR DECOMPOSITION for accurate collision on
        // non-rectangular buildings Group tiles by row and create one collision
        // body per contiguous horizontal span
        std::map<int, std::vector<int>> rowToColumns;
        for (const auto &[tx, ty] : buildingTiles) {
          rowToColumns[ty].push_back(tx);
        }

        // Sort columns in each row for contiguous span detection
        for (auto &[row, columns] : rowToColumns) {
          std::sort(columns.begin(), columns.end());
        }

        // Create collision bodies for each row's contiguous spans
        subBodyIndex = 0;
        for (const auto &[row, columns] : rowToColumns) {
          size_t i = 0;
          while (i < columns.size()) {
            int spanStart = columns[i];
            int spanEnd = spanStart;

            // Extend span while tiles are contiguous
            while (i + 1 < columns.size() && columns[i + 1] == columns[i] + 1) {
              ++i;
              spanEnd = columns[i];
            }

            // Create collision body for this span with vertical overlap to
            // eliminate seams Small overlap prevents gaps between row bodies
            // due to floating point precision
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

            // Register with EDM first (single source of truth)
            auto &edm = EntityDataManager::Instance();
            EntityHandle handle =
                edm.createStaticBody(center, halfWidth, halfHeight);
            EntityID id = handle.getId();
            size_t edmIndex = edm.getStaticIndex(handle);

            addStaticBody(
                id, center, halfSize, CollisionLayer::Layer_Environment,
                0xFFFFFFFFu, false, 0,
                static_cast<uint8_t>(HammerEngine::TriggerType::Physical),
                edmIndex);
            ++subBodyIndex;

            ++i; // Move to next potential span
          }
        }
      }
    } else if (tile.obstacleType != ObstacleType::BUILDING &&
               tile.buildingId > 0) {
      // Tile was a building but no longer is - remove ALL building collision
      // bodies Handle BOTH old and new EntityID formats

      // Remove OLD format single bounding box (if it exists)
      EntityID oldSingleBodyId =
          (static_cast<EntityID>(3ull) << 61) |
          static_cast<EntityID>(static_cast<uint32_t>(tile.buildingId));
      removeCollisionBody(oldSingleBodyId);

      // Remove all NEW format multi-body collision bodies
      uint16_t subBodyIndex = 0;
      while (subBodyIndex < 1000) { // Safety limit to prevent infinite loop
        EntityID newMultiBodyId =
            (static_cast<EntityID>(3ull) << 61) |
            (static_cast<EntityID>(tile.buildingId) << 16) |
            static_cast<EntityID>(subBodyIndex);

        auto it = m_storage.entityToIndex.find(newMultiBodyId);
        if (it == m_storage.entityToIndex.end()) {
          break; // No more sub-bodies for this building
        }

        removeCollisionBody(newMultiBodyId);
        ++subBodyIndex;
      }
    }

    // ROCK and TREE movement penalties are handled by pathfinding system
    // No collision triggers needed for these obstacle types

    // Mark static hash as needing rebuild since tile changed
    m_staticHashDirty = true;
    m_staticQueryCacheDirty = true;
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
          // This event handler serves as confirmation that world cleanup
          // completed
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

// ========== STATIC BODY STORAGE MANAGEMENT ==========
// EDM-CENTRIC: Static bodies (buildings, triggers, obstacles) are stored in
// m_storage Movables (players, NPCs) are managed entirely by EDM - no m_storage
// entry

size_t CollisionManager::addStaticBody(EntityID id, const Vector2D &position,
                                       const Vector2D &halfSize, uint32_t layer,
                                       uint32_t collidesWith, bool isTrigger,
                                       uint8_t triggerTag, uint8_t triggerType,
                                       size_t edmIndex) {
  // Check if entity already exists
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end()) {
    // Update existing static body
    size_t index = it->second;
    if (index < m_storage.hotData.size()) {
      auto &hot = m_storage.hotData[index];

      float px = position.getX();
      float py = position.getY();
      float hw = halfSize.getX();
      float hh = halfSize.getY();
      hot.aabbMinX = px - hw;
      hot.aabbMinY = py - hh;
      hot.aabbMaxX = px + hw;
      hot.aabbMaxY = py + hh;
      hot.layers = layer;
      hot.collidesWith = collidesWith;
      hot.active = true;
      hot.triggerType = triggerType;
      hot.edmIndex = edmIndex;
    }
    return it->second;
  }

  // Add new static body
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
  hotData.layers = layer;
  hotData.collidesWith = collidesWith;
  hotData.bodyType = static_cast<uint8_t>(BodyType::STATIC);
  hotData.triggerTag = triggerTag;
  hotData.triggerType = triggerType;
  hotData.active = true;
  hotData.isTrigger = isTrigger;
  hotData.edmIndex = edmIndex;

  // Initialize coarse cell coords for cache optimization
  AABB initialAABB(px, py, hw, hh);
  auto initialCoarseCell = m_staticSpatialHash.getCoarseCoord(initialAABB);
  hotData.coarseCellX = static_cast<int16_t>(initialCoarseCell.x);
  hotData.coarseCellY = static_cast<int16_t>(initialCoarseCell.y);

  CollisionStorage::ColdData coldData{};

  m_storage.hotData.push_back(hotData);
  m_storage.coldData.push_back(coldData);
  m_storage.entityIds.push_back(id);
  m_storage.entityToIndex[id] = newIndex;

  // Fire event and mark hash dirty for static bodies
  float radius = std::max(hw, hh) + 16.0f;
  std::string description =
      std::format("Static obstacle added at ({}, {})", px, py);
  EventManager::Instance().triggerCollisionObstacleChanged(
      position, radius, description, EventManager::DispatchMode::Deferred);
  m_staticHashDirty = true;
  m_staticQueryCacheDirty = true;

  return newIndex;
}

void CollisionManager::removeCollisionBody(EntityID id) {
  auto it = m_storage.entityToIndex.find(id);
  if (it == m_storage.entityToIndex.end()) {
    return; // Entity not found
  }

  size_t indexToRemove = it->second;
  size_t lastIndex = m_storage.size() - 1;

  // Fire collision obstacle changed event for static bodies before removal
  if (indexToRemove < m_storage.size()) {
    const auto &hot = m_storage.hotData[indexToRemove];
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      Vector2D position;
      float halfW, halfH;
      if (hot.edmIndex != SIZE_MAX) {
        const auto &edm = EntityDataManager::Instance();
        const auto &edmHot = edm.getStaticHotDataByIndex(hot.edmIndex);
        position = edmHot.transform.position;
        halfW = edmHot.halfWidth;
        halfH = edmHot.halfHeight;
      } else {
        position = Vector2D((hot.aabbMinX + hot.aabbMaxX) * 0.5f,
                            (hot.aabbMinY + hot.aabbMaxY) * 0.5f);
        halfW = (hot.aabbMaxX - hot.aabbMinX) * 0.5f;
        halfH = (hot.aabbMaxY - hot.aabbMinY) * 0.5f;
      }
      float radius = std::max(halfW, halfH) + 16.0f;
      std::string description =
          std::format("Static obstacle removed from ({}, {})", position.getX(),
                      position.getY());
      EventManager::Instance().triggerCollisionObstacleChanged(
          position, radius, description, EventManager::DispatchMode::Deferred);
      m_staticHashDirty = true;
      m_staticQueryCacheDirty = true;
    }
  }

  if (indexToRemove != lastIndex) {
    // Swap with last element
    m_storage.hotData[indexToRemove] = m_storage.hotData[lastIndex];
    m_storage.coldData[indexToRemove] = m_storage.coldData[lastIndex];
    m_storage.entityIds[indexToRemove] = m_storage.entityIds[lastIndex];

    EntityID movedEntity = m_storage.entityIds[indexToRemove];
    m_storage.entityToIndex[movedEntity] = indexToRemove;
  }

  m_storage.hotData.pop_back();
  m_storage.coldData.pop_back();
  m_storage.entityIds.pop_back();
  m_storage.entityToIndex.erase(id);

  // Clean up trigger-related state
  for (auto triggerIt = m_activeTriggerPairs.begin();
       triggerIt != m_activeTriggerPairs.end();) {
    if (triggerIt->second.first == id || triggerIt->second.second == id) {
      triggerIt = m_activeTriggerPairs.erase(triggerIt);
    } else {
      ++triggerIt;
    }
  }
  m_triggerCooldownUntil.erase(id);
}

bool CollisionManager::getCollisionBody(EntityID id, size_t &outIndex) const {
  auto it = m_storage.entityToIndex.find(id);
  if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
    outIndex = it->second;
    return true;
  }
  return false;
}

void CollisionManager::updateCollisionBodyPosition(
    EntityID id, const Vector2D &newPosition) {
  size_t index;
  if (getCollisionBody(id, index)) {
    auto &hot = m_storage.hotData[index];

    // Static bodies (including moving platforms) update AABB directly in
    // m_storage
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      // Calculate half-size from current AABB
      float halfW = (hot.aabbMaxX - hot.aabbMinX) * 0.5f;
      float halfH = (hot.aabbMaxY - hot.aabbMinY) * 0.5f;

      // Update AABB position
      hot.aabbMinX = newPosition.getX() - halfW;
      hot.aabbMinY = newPosition.getY() - halfH;
      hot.aabbMaxX = newPosition.getX() + halfW;
      hot.aabbMaxY = newPosition.getY() + halfH;

      // Mark static hash dirty for rebuild
      m_staticHashDirty = true;
      m_staticQueryCacheDirty = true;
      return;
    }

    // Write position to EDM (single source of truth) for dynamic/kinematic
    // bodies
    if (hot.edmIndex != SIZE_MAX) {
      auto &edm = EntityDataManager::Instance();
      edm.getTransformByIndex(hot.edmIndex).position = newPosition;
      const auto &edmHot = edm.getHotDataByIndex(hot.edmIndex);

      // Update cached AABB
      float halfW = edmHot.halfWidth;
      float halfH = edmHot.halfHeight;
      hot.aabbMinX = newPosition.getX() - halfW;
      hot.aabbMinY = newPosition.getY() - halfH;
      hot.aabbMaxX = newPosition.getX() + halfW;
      hot.aabbMaxY = newPosition.getY() + halfH;
    }
  }
}

void CollisionManager::updateCollisionBodyVelocity(
    EntityID id, const Vector2D &newVelocity) {
  size_t index;
  if (getCollisionBody(id, index)) {
    const auto &hot = m_storage.hotData[index];
    // Static bodies have no velocity
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      return;
    }
    // Write velocity to EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      EntityDataManager::Instance().getTransformByIndex(hot.edmIndex).velocity =
          newVelocity;
    }
  }
}

Vector2D CollisionManager::getCollisionBodyVelocity(EntityID id) const {
  size_t index;
  if (getCollisionBody(id, index)) {
    const auto &hot = m_storage.hotData[index];
    // Static bodies have no velocity
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      return Vector2D(0, 0);
    }
    // Read velocity from EDM (single source of truth)
    if (hot.edmIndex != SIZE_MAX) {
      return EntityDataManager::Instance()
          .getTransformByIndex(hot.edmIndex)
          .velocity;
    }
  }
  return Vector2D(0, 0);
}

void CollisionManager::updateCollisionBodySize(EntityID id,
                                               const Vector2D &newHalfSize) {
  size_t index;
  if (getCollisionBody(id, index)) {
    auto &hot = m_storage.hotData[index];
    // Static bodies don't change size at runtime
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      return;
    }
    // Write halfSize to EDM (single source of truth) for dynamic/kinematic
    // bodies
    if (hot.edmIndex != SIZE_MAX) {
      auto &edm = EntityDataManager::Instance();
      auto &edmHot = edm.getHotDataByIndex(hot.edmIndex);
      edmHot.halfWidth = newHalfSize.getX();
      edmHot.halfHeight = newHalfSize.getY();

      // Update cached AABB
      const auto &transform = edm.getTransformByIndex(hot.edmIndex);
      float px = transform.position.getX();
      float py = transform.position.getY();
      hot.aabbMinX = px - newHalfSize.getX();
      hot.aabbMinY = py - newHalfSize.getY();
      hot.aabbMaxX = px + newHalfSize.getX();
      hot.aabbMaxY = py + newHalfSize.getY();
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
        m_storage.hotData[index].edmIndex =
            EntityDataManager::Instance().getIndex(entity->getHandle());
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

// EDM-CENTRIC: Build active indices from EntityDataManager Active tier
// Movables come from EDM's Active tier entities with collision enabled
// Statics stay in CollisionManager's m_staticSpatialHash
std::tuple<size_t, size_t, size_t>
CollisionManager::buildActiveIndices(const CullingArea &cullingArea) const {
  auto &edm = EntityDataManager::Instance();
  auto &pools = m_collisionPool;

  // Store current culling area for use in broadphase queries
  m_currentCullingArea = cullingArea;
  pools.movableIndices.clear();
  pools.movableAABBs.clear();
  // NOTE: staticIndices NOT cleared here - cached and only cleared inside
  // cullingChanged block

  // Track total body counts
  size_t totalStatic = 0;
  size_t totalDynamic = 0;
  size_t totalKinematic = 0;

  // Query m_staticSpatialHash for statics in culling area
  // OPTIMIZATION: Cache result when culling area unchanged
  bool cullingChanged = m_staticQueryCacheDirty ||
                        cullingArea.minX != m_lastStaticQueryCullingArea.minX ||
                        cullingArea.maxX != m_lastStaticQueryCullingArea.maxX ||
                        cullingArea.minY != m_lastStaticQueryCullingArea.minY ||
                        cullingArea.maxY != m_lastStaticQueryCullingArea.maxY;

  if (cullingChanged) {
    pools.staticIndices.clear();
    pools.staticAABBs.clear();
    const float cullCenterX = (cullingArea.minX + cullingArea.maxX) * 0.5f;
    const float cullCenterY = (cullingArea.minY + cullingArea.maxY) * 0.5f;
    const float cullHalfW = (cullingArea.maxX - cullingArea.minX) * 0.5f;
    const float cullHalfH = (cullingArea.maxY - cullingArea.minY) * 0.5f;

    if (cullHalfW > 0.0f && cullHalfH > 0.0f) {
      AABB cullAABB(cullCenterX, cullCenterY, cullHalfW, cullHalfH);
      m_staticSpatialHash.queryRegion(cullAABB, pools.staticIndices);

      // Cache static AABBs for contiguous memory access in broadphase
      // This avoids scattered m_storage.hotData[idx] access in SIMD loops
      // Filter EventOnly triggers from broadphase - they get separate detection
      // via spatial query
      pools.staticAABBs.reserve(pools.staticIndices.size());

      // Filter indices and build AABBs for physics bodies only
      std::vector<size_t> physicsIndices;
      physicsIndices.reserve(pools.staticIndices.size());

      for (size_t storageIdx : pools.staticIndices) {
        const auto &staticHot = m_storage.hotData[storageIdx];

        // Skip inactive statics at cache time - avoids per-frame active checks
        if (!staticHot.active)
          continue;

        // EventOnly triggers skip broadphase entirely - detected via per-entity
        // spatial query
        if (staticHot.isTrigger != 0 &&
            staticHot.triggerType ==
                static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly)) {
          continue;
        }

        // Physical bodies (obstacles, physical triggers) go to broadphase
        physicsIndices.push_back(storageIdx);
        CollisionPool::StaticAABB aabb;
        aabb.minX = staticHot.aabbMinX;
        aabb.minY = staticHot.aabbMinY;
        aabb.maxX = staticHot.aabbMaxX;
        aabb.maxY = staticHot.aabbMaxY;
        aabb.layers = staticHot.layers;
        aabb.active = staticHot.active;
        pools.staticAABBs.push_back(aabb);
      }

      // Replace staticIndices with filtered physics-only indices
      pools.staticIndices = std::move(physicsIndices);
    }
    m_lastStaticQueryCullingArea = cullingArea;
    m_staticQueryCacheDirty = false;
  }
  // else: reuse pools.staticIndices and pools.staticAABBs from previous frame
  totalStatic = pools.staticIndices.size();

  // EDM-CENTRIC: Get Active tier entities with collision enabled
  // Uses cached filtered indices - avoids O(18K) iteration and in-loop
  // filtering
  for (size_t edmIdx : edm.getActiveIndicesWithCollision()) {
    const auto &hot = edm.getHotDataByIndex(edmIdx);
    // No hasCollision() check needed - already filtered by EDM

    // Get position from EDM transform
    const auto &transform = edm.getTransformByIndex(edmIdx);
    float posX = transform.position.getX();
    float posY = transform.position.getY();

    // Apply culling based on position
    if (cullingArea.minX != cullingArea.maxX ||
        cullingArea.minY != cullingArea.maxY) {
      if (!cullingArea.contains(posX, posY)) {
        continue;
      }
    }

    // Count by EntityKind (Player/Projectile are dynamic, others kinematic)
    if (hot.kind == EntityKind::Player || hot.kind == EntityKind::Projectile) {
      totalDynamic++;
    } else {
      totalKinematic++;
    }

    // Store EDM index and compute AABB from EDM data
    pools.movableIndices.push_back(edmIdx);

    // Compute and cache AABB + entity data for fast broadphase/narrowphase
    // access Caching entityId and isTrigger here avoids EDM calls in
    // narrowphase
    CollisionPool::MovableAABB aabb;
    aabb.minX = posX - hot.halfWidth;
    aabb.minY = posY - hot.halfHeight;
    aabb.maxX = posX + hot.halfWidth;
    aabb.maxY = posY + hot.halfHeight;
    aabb.layers = hot.collisionLayers;
    aabb.collidesWith = hot.collisionMask;
    aabb.entityId =
        edm.getEntityId(edmIdx);      // Cache to avoid EDM call in narrowphase
    aabb.isTrigger = hot.isTrigger(); // Cache to avoid EDM call in narrowphase
    pools.movableAABBs.push_back(aabb);
  }

  return {totalStatic, totalDynamic, totalKinematic};
}

// Internal helper methods moved to private section

// ========== EDM-CENTRIC BROADPHASE IMPLEMENTATION ==========

void CollisionManager::broadphase() {
  // Dispatcher: Choose single-threaded or multi-threaded path based on
  // WorkerBudget Output: pools.movableMovablePairs and pools.movableStaticPairs

  const auto &pools = m_collisionPool;
  const auto &movableIndices = pools.movableIndices;

  if (movableIndices.empty()) {
    m_lastBroadphaseWasThreaded = false;
    return;
  }

  // Query WorkerBudget for optimal configuration
  auto &budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
  size_t optimalWorkers = budgetMgr.getOptimalWorkers(
      HammerEngine::SystemType::Collision, movableIndices.size());

  auto [batchCount, batchSize] =
      budgetMgr.getBatchStrategy(HammerEngine::SystemType::Collision,
                                 movableIndices.size(), optimalWorkers);

  // Threshold: multi-thread only if WorkerBudget recommends it
  if (batchCount <= 1 ||
      movableIndices.size() < MIN_MOVABLE_FOR_BROADPHASE_THREADING) {
    m_lastBroadphaseWasThreaded = false;
    m_lastBroadphaseBatchCount = 1;
    broadphaseSingleThreaded();
  } else {
    m_lastBroadphaseWasThreaded = true;
    m_lastBroadphaseBatchCount = batchCount;
    broadphaseMultiThreaded(batchCount, batchSize);
  }
}

void CollisionManager::broadphaseSingleThreaded() {
  // EDM-CENTRIC: Uses pools.movableAABBs for movables, m_storage for statics
  // Output goes to pools.movableMovablePairs and pools.movableStaticPairs

  auto &pools = m_collisionPool;
  const auto &movableIndices = pools.movableIndices;
  const auto &movableAABBs = pools.movableAABBs;
  const auto &staticIndices = pools.staticIndices;

  pools.movableMovablePairs.clear();
  pools.movableStaticPairs.clear();

  // ========================================================================
  // 1. MOVABLE-VS-MOVABLE: Sweep-and-Prune using EDM-computed AABBs
  //    Sort pool indices by minX, only check pairs with overlapping X ranges
  // ========================================================================
  if (movableIndices.size() > 1) {
    // Sort pool indices by minX for sweep-and-prune
    // sortedMovableIndices now contains pool indices (0..N-1), not EDM/storage
    // indices
    auto &sorted = pools.sortedMovableIndices;
    sorted.resize(movableIndices.size());
    for (size_t i = 0; i < movableIndices.size(); ++i) {
      sorted[i] = i; // Pool index
    }
    std::sort(sorted.begin(), sorted.end(),
              [&movableAABBs](size_t a, size_t b) {
                return movableAABBs[a].minX < movableAABBs[b].minX;
              });

    // Sweep: for each body, only check bodies that start before this one ends
    // (X overlap) - SIMD 4-wide Y-overlap + layer checks
    for (size_t i = 0; i < sorted.size(); ++i) {
      size_t poolIdxA = sorted[i];
      const auto &aabbA = movableAABBs[poolIdxA];

      const float maxXA = aabbA.maxX + SPATIAL_QUERY_EPSILON;
      const uint32_t collidesWithA = aabbA.collidesWith;

      // SIMD broadcasts for A's Y bounds and layer mask
      const Float4 minYA = broadcast(aabbA.minY - SPATIAL_QUERY_EPSILON);
      const Float4 maxYA = broadcast(aabbA.maxY + SPATIAL_QUERY_EPSILON);
      const Int4 layerMaskA = broadcast_int(static_cast<int32_t>(collidesWithA));

      size_t j = i + 1;
      while (j < sorted.size()) {
        // Check if we can batch 4 bodies with X overlap
        if (j + 4 <= sorted.size() && movableAABBs[sorted[j + 3]].minX <= maxXA) {
          // SIMD PATH: All 4 have X overlap, batch Y + layer check
          const auto &b0 = movableAABBs[sorted[j]];
          const auto &b1 = movableAABBs[sorted[j + 1]];
          const auto &b2 = movableAABBs[sorted[j + 2]];
          const auto &b3 = movableAABBs[sorted[j + 3]];

          Float4 minYB = set(b0.minY, b1.minY, b2.minY, b3.minY);
          Float4 maxYB = set(b0.maxY, b1.maxY, b2.maxY, b3.maxY);

          // Y overlap test
          Float4 noOverlapY1 = cmplt(maxYA, minYB);
          Float4 noOverlapY2 = cmplt(maxYB, minYA);
          Float4 noOverlapY = bitwise_or(noOverlapY1, noOverlapY2);
          int noOverlapMask = movemask(noOverlapY);

          // Early-out if all 4 fail Y overlap (common in X-clustered but Y-spread)
          if (noOverlapMask == 0xF) {
            j += 4;
            continue;
          }

          // Layer mask test
          Int4 layersB = set_int4(b0.layers, b1.layers, b2.layers, b3.layers);
          Int4 layerResult = bitwise_and(layerMaskA, layersB);
          Int4 layerFail = cmpeq_int(layerResult, setzero_int());
          int layerFailMask = movemask_int(layerFail);

          // Process lanes that passed both tests
          for (size_t k = 0; k < 4; ++k) {
            if (((noOverlapMask >> k) & 1) == 0 &&
                ((layerFailMask >> (k * 4)) & 0xF) == 0) {
              pools.movableMovablePairs.emplace_back(poolIdxA, sorted[j + k]);
            }
          }
          j += 4;
        } else {
          // SCALAR PATH: Check one at a time with SAP early exit
          size_t poolIdxB = sorted[j];
          const auto &aabbB = movableAABBs[poolIdxB];

          if (aabbB.minX > maxXA)
            break;  // SAP early termination

          if (!(aabbA.maxY + SPATIAL_QUERY_EPSILON < aabbB.minY ||
                aabbA.minY - SPATIAL_QUERY_EPSILON > aabbB.maxY)) {
            if ((collidesWithA & aabbB.layers) != 0) {
              pools.movableMovablePairs.emplace_back(poolIdxA, poolIdxB);
            }
          }
          ++j;
        }
      }
    }
  }

  // ========================================================================
  // 2. MOVABLE-VS-STATIC: Iterate each movable against statics
  //    Movables use pools.movableAABBs, statics use cached pools.staticAABBs
  // ========================================================================
  // DYNAMIC THRESHOLD: Use spatial hash when it filters significantly.
  // Direct path: O(movables × statics) comparisons but no query overhead.
  // Spatial hash: O(movables × nearby_statics) with query + overlap test.
  const bool useDirectPath = (staticIndices.size() < 100);
  const auto &staticAABBs = pools.staticAABBs;

  for (size_t poolIdx = 0; poolIdx < movableIndices.size(); ++poolIdx) {
    const auto &movableAABB = movableAABBs[poolIdx];
    const uint32_t movableCollidesWith = movableAABB.collidesWith;

    if (useDirectPath) {
      // DIRECT PATH: SIMD 4-wide check of cached staticAABBs
      Float4 dynMinX = broadcast(movableAABB.minX - SPATIAL_QUERY_EPSILON);
      Float4 dynMinY = broadcast(movableAABB.minY - SPATIAL_QUERY_EPSILON);
      Float4 dynMaxX = broadcast(movableAABB.maxX + SPATIAL_QUERY_EPSILON);
      Float4 dynMaxY = broadcast(movableAABB.maxY + SPATIAL_QUERY_EPSILON);
      const Int4 layerMaskVec = broadcast_int(static_cast<int32_t>(movableCollidesWith));

      size_t si = 0;
      const size_t simdEnd = (staticAABBs.size() / 4) * 4;

      for (; si < simdEnd; si += 4) {
        const auto &a0 = staticAABBs[si];
        const auto &a1 = staticAABBs[si + 1];
        const auto &a2 = staticAABBs[si + 2];
        const auto &a3 = staticAABBs[si + 3];

        Float4 statMinX = set(a0.minX, a1.minX, a2.minX, a3.minX);
        Float4 statMaxX = set(a0.maxX, a1.maxX, a2.maxX, a3.maxX);
        Float4 statMinY = set(a0.minY, a1.minY, a2.minY, a3.minY);
        Float4 statMaxY = set(a0.maxY, a1.maxY, a2.maxY, a3.maxY);

        Float4 noOverlapX1 = cmplt(dynMaxX, statMinX);
        Float4 noOverlapX2 = cmplt(statMaxX, dynMinX);
        Float4 noOverlapY1 = cmplt(dynMaxY, statMinY);
        Float4 noOverlapY2 = cmplt(statMaxY, dynMinY);
        Float4 noOverlap = bitwise_or(bitwise_or(noOverlapX1, noOverlapX2),
                                      bitwise_or(noOverlapY1, noOverlapY2));
        int noOverlapMask = movemask(noOverlap);

        if (noOverlapMask == 0xF)
          continue;

        Int4 staticLayers = set_int4(a0.layers, a1.layers, a2.layers, a3.layers);
        Int4 layerResult = bitwise_and(staticLayers, layerMaskVec);
        Int4 layerFail = cmpeq_int(layerResult, setzero_int());
        int layerFailMask = movemask_int(layerFail);

        for (size_t j = 0; j < 4; ++j) {
          if (((noOverlapMask >> j) & 1) == 0 &&
              ((layerFailMask >> (j * 4)) & 0xF) == 0) {
            if (staticAABBs[si + j].active) {
              pools.movableStaticPairs.emplace_back(poolIdx, staticIndices[si + j]);
            }
          }
        }
      }

      // Scalar tail
      for (; si < staticAABBs.size(); ++si) {
        const auto &staticAABB = staticAABBs[si];
        if (!staticAABB.active)
          continue;
        if (movableAABB.maxX + SPATIAL_QUERY_EPSILON < staticAABB.minX ||
            movableAABB.minX - SPATIAL_QUERY_EPSILON > staticAABB.maxX ||
            movableAABB.maxY + SPATIAL_QUERY_EPSILON < staticAABB.minY ||
            movableAABB.minY - SPATIAL_QUERY_EPSILON > staticAABB.maxY)
          continue;
        if ((movableCollidesWith & staticAABB.layers) == 0)
          continue;
        pools.movableStaticPairs.emplace_back(poolIdx, staticIndices[si]);
      }
    } else {
      // SPATIAL HASH PATH: Query for nearby statics only
      float queryMinX = movableAABB.minX - SPATIAL_QUERY_EPSILON;
      float queryMinY = movableAABB.minY - SPATIAL_QUERY_EPSILON;
      float queryMaxX = movableAABB.maxX + SPATIAL_QUERY_EPSILON;
      float queryMaxY = movableAABB.maxY + SPATIAL_QUERY_EPSILON;

      auto &staticCandidates = getPooledVector();
      m_staticSpatialHash.queryRegionBounds(queryMinX, queryMinY, queryMaxX,
                                            queryMaxY, staticCandidates);

      for (size_t si = 0; si < staticCandidates.size(); ++si) {
        size_t staticIdx = staticCandidates[si];
        const auto &staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active)
          continue;
        if ((movableCollidesWith & staticHot.layers) == 0)
          continue;
        // AABB overlap test - spatial hash returns region candidates, not
        // guaranteed overlaps
        if (movableAABB.maxX + SPATIAL_QUERY_EPSILON < staticHot.aabbMinX ||
            movableAABB.minX - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxX ||
            movableAABB.maxY + SPATIAL_QUERY_EPSILON < staticHot.aabbMinY ||
            movableAABB.minY - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxY)
          continue;
        pools.movableStaticPairs.emplace_back(poolIdx, staticIdx);
      }
      returnPooledVector(staticCandidates);
    }
  }
}

void CollisionManager::broadphaseMultiThreaded(size_t batchCount,
                                               size_t batchSize) {
  // EDM-CENTRIC: Multi-threaded broadphase using pools.movableAABBs
  // Output: Per-batch pair buffers merged into
  // pools.movableMovablePairs/movableStaticPairs Matches AIManager pattern:
  // reusable member buffers, no mutex overhead

  auto &threadSystem = HammerEngine::ThreadSystem::Instance();
  auto &pools = m_collisionPool;
  const auto &movableIndices = pools.movableIndices;
  const auto &movableAABBs = pools.movableAABBs;

  // ========================================================================
  // SWEEP-AND-PRUNE SETUP: Sort movables by minX for early termination in MM
  // This reduces MM from O(n²) to O(n log n + n×k) where k << n
  // ========================================================================
  auto &sorted = pools.sortedMovableIndices;
  sorted.resize(movableIndices.size());
  for (size_t i = 0; i < movableIndices.size(); ++i) {
    sorted[i] = i; // Pool index
  }
  std::sort(sorted.begin(), sorted.end(), [&movableAABBs](size_t a, size_t b) {
    return movableAABBs[a].minX < movableAABBs[b].minX;
  });

  // Resize batch buffers if needed (keeps capacity, avoids allocations)
  if (m_broadphaseBatchBuffers.size() < batchCount) {
    m_broadphaseBatchBuffers.resize(batchCount);
  }

  // Clear batch buffers (keeps capacity from previous frames)
  for (size_t i = 0; i < batchCount; ++i) {
    m_broadphaseBatchBuffers[i].clear();
  }

  // Submit batches (no mutex - futures are thread-safe)
  m_broadphaseFutures.clear();
  m_broadphaseFutures.reserve(batchCount);

  for (size_t i = 0; i < batchCount; ++i) {
    size_t startIdx = i * batchSize;
    size_t endIdx = std::min(startIdx + batchSize, movableIndices.size());

    m_broadphaseFutures.push_back(threadSystem.enqueueTaskWithResult(
        [this, startIdx, endIdx, i]() -> void {
          try {
            broadphaseBatch(startIdx, endIdx,
                            m_broadphaseBatchBuffers[i].movableMovable,
                            m_broadphaseBatchBuffers[i].movableStatic);
          } catch (const std::exception &e) {
            COLLISION_ERROR(std::format("Exception in broadphase batch {}: {}",
                                        i, e.what()));
          }
        },
        HammerEngine::TaskPriority::High, "Collision_Broadphase"));
  }

  // Wait for completion (no mutex - matches AIManager pattern)
  for (auto &future : m_broadphaseFutures) {
    if (future.valid()) {
      future.get();
    }
  }

  // Merge results into pools
  pools.movableMovablePairs.clear();
  pools.movableStaticPairs.clear();

  size_t totalMM = 0, totalMS = 0;
  for (size_t i = 0; i < batchCount; ++i) {
    totalMM += m_broadphaseBatchBuffers[i].movableMovable.size();
    totalMS += m_broadphaseBatchBuffers[i].movableStatic.size();
  }
  pools.movableMovablePairs.reserve(totalMM);
  pools.movableStaticPairs.reserve(totalMS);

  for (size_t i = 0; i < batchCount; ++i) {
    pools.movableMovablePairs.insert(
        pools.movableMovablePairs.end(),
        m_broadphaseBatchBuffers[i].movableMovable.begin(),
        m_broadphaseBatchBuffers[i].movableMovable.end());
    pools.movableStaticPairs.insert(
        pools.movableStaticPairs.end(),
        m_broadphaseBatchBuffers[i].movableStatic.begin(),
        m_broadphaseBatchBuffers[i].movableStatic.end());
  }

#ifndef NDEBUG
  static thread_local uint64_t logFrameCounter = 0;
  if (++logFrameCounter % 300 == 0 && movableIndices.size() > 0) {
    COLLISION_DEBUG(std::format("Broadphase: multi-threaded [{} batches, {} "
                                "movables, {} MM pairs, {} MS pairs]",
                                batchCount, movableIndices.size(),
                                pools.movableMovablePairs.size(),
                                pools.movableStaticPairs.size()));
  }
#endif
}

void CollisionManager::broadphaseBatch(
    size_t startIdx, size_t endIdx,
    std::vector<std::pair<size_t, size_t>> &outMovableMovable,
    std::vector<std::pair<size_t, size_t>> &outMovableStatic) {
  // EDM-CENTRIC: Uses pools.movableAABBs for movables, m_storage for statics
  // Thread-safe: Each batch writes to its own output vectors
  // Uses Sweep-and-Prune (SAP) for MM: sorted indices with early termination

  const auto &pools = m_collisionPool;
  const auto &movableAABBs = pools.movableAABBs;
  const auto &staticIndices = pools.staticIndices;
  const auto &sorted = pools.sortedMovableIndices; // Pre-sorted by minX

  // ========================================================================
  // Process each SORTED position in this batch's range [startIdx, endIdx)
  // ========================================================================
  for (size_t sortedI = startIdx; sortedI < endIdx && sortedI < sorted.size();
       ++sortedI) {
    size_t poolIdxA = sorted[sortedI]; // Actual pool index from sorted order
    const auto &aabbA = movableAABBs[poolIdxA];
    const uint32_t collidesWithA = aabbA.collidesWith;
    const float maxXA = aabbA.maxX + SPATIAL_QUERY_EPSILON;

    // ========================================================================
    // 1. MOVABLE-VS-MOVABLE: Sweep-and-Prune with early termination
    //    Check subsequent sorted movables until minX > maxX (SAP exit)
    // ========================================================================
    for (size_t sortedJ = sortedI + 1; sortedJ < sorted.size(); ++sortedJ) {
      size_t poolIdxB = sorted[sortedJ];
      const auto &aabbB = movableAABBs[poolIdxB];

      // SAP early termination: if B's minX > A's maxX, no more overlaps
      // possible
      if (aabbB.minX > maxXA)
        break;

      // X already overlaps (from SAP), check Y overlap
      if (aabbA.maxY + SPATIAL_QUERY_EPSILON < aabbB.minY ||
          aabbA.minY - SPATIAL_QUERY_EPSILON > aabbB.maxY)
        continue;

      // Layer mask check
      if ((collidesWithA & aabbB.layers) == 0)
        continue;

      // Store pool indices for movable-movable pair
      outMovableMovable.emplace_back(poolIdxA, poolIdxB);
    }

    // ========================================================================
    // 2. MOVABLE-VS-STATIC: Check against statics in m_storage
    // ========================================================================
    // DYNAMIC THRESHOLD: Use spatial hash when it filters significantly.
    // Direct path: O(batch × statics) SIMD comparisons but no query overhead.
    // Spatial hash: O(batch × nearby_statics) with query + overlap test.
    const bool useDirectPath = (staticIndices.size() < 100);
    const auto &staticAABBs = pools.staticAABBs;

    if (useDirectPath) {
      // DIRECT PATH: Iterate cached staticAABBs with SIMD (contiguous memory
      // access) Setup SIMD broadcasts for this movable
      Float4 dynMinX = broadcast(aabbA.minX - SPATIAL_QUERY_EPSILON);
      Float4 dynMinY = broadcast(aabbA.minY - SPATIAL_QUERY_EPSILON);
      Float4 dynMaxX = broadcast(aabbA.maxX + SPATIAL_QUERY_EPSILON);
      Float4 dynMaxY = broadcast(aabbA.maxY + SPATIAL_QUERY_EPSILON);
      const Int4 staticMaskVec = broadcast_int(collidesWithA);

      size_t si = 0;
      const size_t staticSimdEnd = (staticAABBs.size() / 4) * 4;

      for (; si < staticSimdEnd; si += 4) {
        // Contiguous access to cached staticAABBs - no scattered memory reads
        const auto &a0 = staticAABBs[si];
        const auto &a1 = staticAABBs[si + 1];
        const auto &a2 = staticAABBs[si + 2];
        const auto &a3 = staticAABBs[si + 3];

        Float4 statMinX = set(a0.minX, a1.minX, a2.minX, a3.minX);
        Float4 statMaxX = set(a0.maxX, a1.maxX, a2.maxX, a3.maxX);
        Float4 statMinY = set(a0.minY, a1.minY, a2.minY, a3.minY);
        Float4 statMaxY = set(a0.maxY, a1.maxY, a2.maxY, a3.maxY);

        Float4 noOverlapX1 = cmplt(dynMaxX, statMinX);
        Float4 noOverlapX2 = cmplt(statMaxX, dynMinX);
        Float4 noOverlapY1 = cmplt(dynMaxY, statMinY);
        Float4 noOverlapY2 = cmplt(statMaxY, dynMinY);
        Float4 noOverlap = bitwise_or(bitwise_or(noOverlapX1, noOverlapX2),
                                      bitwise_or(noOverlapY1, noOverlapY2));
        int noOverlapMask = movemask(noOverlap);

        if (noOverlapMask == 0xF)
          continue;

        Int4 staticLayers =
            set_int4(a0.layers, a1.layers, a2.layers, a3.layers);
        Int4 staticResult = bitwise_and(staticLayers, staticMaskVec);
        Int4 staticCmp = cmpeq_int(staticResult, setzero_int());
        int layerFailMask = movemask_int(staticCmp);

        for (size_t j = 0; j < 4; ++j) {
          if (((noOverlapMask >> j) & 1) == 0 &&
              ((layerFailMask >> (j * 4)) & 0xF) == 0) {
            if (staticAABBs[si + j].active) {
              outMovableStatic.emplace_back(poolIdxA, staticIndices[si + j]);
            }
          }
        }
      }

      // Scalar tail using cached AABBs
      for (; si < staticAABBs.size(); ++si) {
        const auto &staticAABB = staticAABBs[si];
        if (!staticAABB.active)
          continue;
        if (aabbA.maxX + SPATIAL_QUERY_EPSILON < staticAABB.minX ||
            aabbA.minX - SPATIAL_QUERY_EPSILON > staticAABB.maxX ||
            aabbA.maxY + SPATIAL_QUERY_EPSILON < staticAABB.minY ||
            aabbA.minY - SPATIAL_QUERY_EPSILON > staticAABB.maxY)
          continue;
        if ((collidesWithA & staticAABB.layers) == 0)
          continue;
        outMovableStatic.emplace_back(poolIdxA, staticIndices[si]);
      }
    } else {
      // SPATIAL HASH PATH: Query for nearby statics (thread-safe)
      float queryMinX = aabbA.minX - SPATIAL_QUERY_EPSILON;
      float queryMinY = aabbA.minY - SPATIAL_QUERY_EPSILON;
      float queryMaxX = aabbA.maxX + SPATIAL_QUERY_EPSILON;
      float queryMaxY = aabbA.maxY + SPATIAL_QUERY_EPSILON;

      thread_local std::vector<size_t> staticCandidates;
      thread_local HammerEngine::HierarchicalSpatialHash::QueryBuffers
          queryBuffers;
      staticCandidates.clear();
      m_staticSpatialHash.queryRegionBoundsThreadSafe(
          queryMinX, queryMinY, queryMaxX, queryMaxY, staticCandidates,
          queryBuffers);

      for (size_t staticIdx : staticCandidates) {
        const auto &staticHot = m_storage.hotData[staticIdx];
        if (!staticHot.active || (collidesWithA & staticHot.layers) == 0)
          continue;
        // AABB overlap test - spatial hash returns region candidates, not
        // guaranteed overlaps
        if (aabbA.maxX + SPATIAL_QUERY_EPSILON < staticHot.aabbMinX ||
            aabbA.minX - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxX ||
            aabbA.maxY + SPATIAL_QUERY_EPSILON < staticHot.aabbMinY ||
            aabbA.minY - SPATIAL_QUERY_EPSILON > staticHot.aabbMaxY)
          continue;
        outMovableStatic.emplace_back(poolIdxA, staticIdx);
      }
    }
  }
}

void CollisionManager::narrowphase(
    std::vector<CollisionInfo> &collisions) const {
  // EDM-CENTRIC: Process movableMovablePairs and movableStaticPairs
  const auto &pools = m_collisionPool;
  size_t totalPairs =
      pools.movableMovablePairs.size() + pools.movableStaticPairs.size();

  if (totalPairs == 0) {
    collisions.clear();
    return;
  }

  // Single-threaded narrowphase (sufficient for current workloads)
  narrowphaseSingleThreaded(collisions);
}

void CollisionManager::narrowphaseSingleThreaded(
    std::vector<CollisionInfo> &collisions) const {
  // EDM-CENTRIC: Process movableMovablePairs and movableStaticPairs
  // movableMovablePairs: (poolIdxA, poolIdxB) - both into
  // movableIndices/movableAABBs movableStaticPairs: (poolIdx, storageIdx) -
  // poolIdx into movableIndices, storageIdx into m_storage NOTE: EntityId and
  // isTrigger are cached in MovableAABB - no EDM calls needed here

  const auto &pools = m_collisionPool;
  const auto &movableIndices = pools.movableIndices;
  const auto &movableAABBs = pools.movableAABBs;

  collisions.clear();
  collisions.reserve(
      (pools.movableMovablePairs.size() + pools.movableStaticPairs.size()) / 4);

  constexpr float AXIS_PREFERENCE_EPSILON = 0.01f;

  // Helper lambda to create collision info from AABB overlap
  // NOTE: Broadphase already confirmed AABB overlap - no need to re-test here
  auto processOverlap =
      [&collisions](float minXA, float minYA, float maxXA, float maxYA,
                    float minXB, float minYB, float maxXB, float maxYB,
                    EntityID entityA, EntityID entityB, bool isTriggerA,
                    bool isTriggerB, size_t idxA, size_t idxB,
                    bool isMovableMovable) {
        // Compute overlap (broadphase guarantees intersection)
        float overlapX = std::min(maxXA, maxXB) - std::max(minXA, minXB);
        float overlapY = std::min(maxYA, maxYB) - std::max(minYA, minYB);

        float minPen;
        Vector2D normal;

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

        bool isEitherTrigger = isTriggerA || isTriggerB;
        collisions.push_back(CollisionInfo{entityA, entityB, normal, minPen,
                                           isEitherTrigger, idxA, idxB,
                                           isMovableMovable});
      };

  // ========================================================================
  // 1. MOVABLE-VS-MOVABLE: Both indices are pool indices into movableAABBs
  // Uses cached entityId and isTrigger - no EDM calls needed
  // ========================================================================
  for (const auto &[poolIdxA, poolIdxB] : pools.movableMovablePairs) {
    if (poolIdxA >= movableAABBs.size() || poolIdxB >= movableAABBs.size())
      continue;

    const auto &aabbA = movableAABBs[poolIdxA];
    const auto &aabbB = movableAABBs[poolIdxB];

    // Use cached values from MovableAABB (populated in buildActiveIndices)
    size_t edmIdxA = movableIndices[poolIdxA];
    size_t edmIdxB = movableIndices[poolIdxB];

    processOverlap(aabbA.minX, aabbA.minY, aabbA.maxX, aabbA.maxY, aabbB.minX,
                   aabbB.minY, aabbB.maxX, aabbB.maxY, aabbA.entityId,
                   aabbB.entityId,                   // Cached in MovableAABB
                   aabbA.isTrigger, aabbB.isTrigger, // Cached in MovableAABB
                   edmIdxA, edmIdxB,
                   true); // isMovableMovable = true
  }

  // ========================================================================
  // 2. MOVABLE-VS-STATIC: poolIdx into movableAABBs, storageIdx into m_storage
  // Uses cached entityId and isTrigger for movable - no EDM calls needed
  // ========================================================================
  for (const auto &[poolIdx, storageIdx] : pools.movableStaticPairs) {
    if (poolIdx >= movableAABBs.size() ||
        storageIdx >= m_storage.hotData.size())
      continue;

    const auto &movableAABB = movableAABBs[poolIdx];
    const auto &staticHot = m_storage.hotData[storageIdx];
    // NOTE: No active check needed - broadphase already filtered inactive
    // statics

    // Use cached values from MovableAABB (populated in buildActiveIndices)
    size_t edmIdx = movableIndices[poolIdx];

    // Get EntityID from m_storage for static (already accessed for AABB)
    EntityID entityB = (storageIdx < m_storage.entityIds.size())
                           ? m_storage.entityIds[storageIdx]
                           : 0;

    processOverlap(
        movableAABB.minX, movableAABB.minY, movableAABB.maxX, movableAABB.maxY,
        staticHot.aabbMinX, staticHot.aabbMinY, staticHot.aabbMaxX,
        staticHot.aabbMaxY, movableAABB.entityId,
        entityB, // Movable uses cached entityId
        movableAABB.isTrigger,
        staticHot.isTrigger != 0, // Movable uses cached isTrigger
        edmIdx, storageIdx,
        false); // isMovableMovable = false (movable-static collision)
  }
}

// ========== MAIN UPDATE METHOD ==========

void CollisionManager::update(float dt) {
  (void)dt;

  // Early exit checks
  if (!m_initialized || m_isShutdown ||
      m_globallyPaused.load(std::memory_order_acquire))
    return;

  using clock = std::chrono::steady_clock; // Needed for WorkerBudget timing
#ifdef DEBUG
  auto t0 = clock::now();
#endif

  // Check storage state at start of update (statics only now)
  size_t staticBodyCount = m_storage.size();

  // EDM-CENTRIC: Also check for active movables in EDM
  auto &edm = EntityDataManager::Instance();
  size_t activeMovableCount = edm.getActiveIndices().size();

  // Early exit if no active movables - collision detection only matters for moving entities
  // Static-vs-static collision is meaningless since statics never move
  if (activeMovableCount == 0) {
    return;
  }

  // Prepare collision processing for this frame
  prepareCollisionBuffers(staticBodyCount +
                          activeMovableCount); // Prepare collision buffers

  // Count active dynamic bodies (with configurable culling)
  CullingArea const cullingArea = createDefaultCullingArea();

  // Rebuild static spatial hash if needed (batched from add/remove operations)
  if (m_staticHashDirty) {
    rebuildStaticSpatialHash();
    m_staticHashDirty = false;
  }

  // MOVEMENT INTEGRATION: Removed redundant loop (was lines 1846-1871)
  // AIManager now handles all position updates via updateKinematicBatch() and
  // applyBatchedKinematicUpdates() which directly set hot.position to the final
  // integrated position. CollisionManager's job is ONLY to:
  //   1. Detect collisions using positions updated by AIManager
  //   2. Resolve collisions (push bodies apart via resolve())
  // This eliminates 28k+ unnecessary iterations per frame and fixes
  // double-integration bug.

  // Track culling metrics
  auto cullingStart = clock::now();

  // OPTIMIZATION: buildActiveIndices now returns body type counts during
  // iteration This avoids 3 expensive std::count_if calls (83,901 iterations
  // for 27k bodies!)
  auto [totalStaticBodies, totalDynamicBodies, totalKinematicBodies] =
      buildActiveIndices(cullingArea);
  size_t totalMovableBodies = totalDynamicBodies + totalKinematicBodies;
  size_t bodyCount = staticBodyCount + activeMovableCount; // For metrics

  auto cullingEnd = clock::now();

  // Sync spatial hashes after culling, only for active bodies
  syncSpatialHashesWithActiveIndices();

  // Reset static culling counter for this frame
  m_perf.lastStaticBodiesCulled = 0;

  double cullingMs =
      std::chrono::duration<double, std::milli>(cullingEnd - cullingStart)
          .count();

  size_t activeMovableBodies = m_collisionPool.movableIndices.size();
  size_t activeStaticBodies = m_collisionPool.staticIndices.size();
  size_t activeBodies = activeMovableBodies + activeStaticBodies;

  // CULLING METRICS: Calculate accurate counts of culled bodies

  // Calculate culled counts
  size_t staticBodiesCulled = (totalStaticBodies > activeStaticBodies)
                                  ? (totalStaticBodies - activeStaticBodies)
                                  : 0;
  size_t dynamicBodiesCulled = (totalMovableBodies > activeMovableBodies)
                                   ? (totalMovableBodies - activeMovableBodies)
                                   : 0;

  // BROADPHASE: Generate collision pairs using spatial hash
  // Pairs stored in pools.movableMovablePairs and pools.movableStaticPairs
  auto t1 = clock::now();
  broadphase();
  auto t2 = clock::now();

  // Report broadphase batch completion for adaptive tuning (WorkerBudget hill
  // climb)
  if (m_lastBroadphaseWasThreaded && activeMovableBodies > 0) {
    auto &budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    double broadphaseMs =
        std::chrono::duration<double, std::milli>(t2 - t1).count();
    budgetMgr.reportBatchCompletion(HammerEngine::SystemType::Collision,
                                    activeMovableBodies,
                                    m_lastBroadphaseBatchCount, broadphaseMs);
  }

  // NARROWPHASE: Detailed collision detection and response calculation
  const size_t pairCount = m_collisionPool.movableMovablePairs.size() +
                           m_collisionPool.movableStaticPairs.size();
  narrowphase(m_collisionPool.collisionBuffer);
  auto t3 = clock::now();

  // RESOLUTION: Apply collision responses and update positions
  // Batch resolution: Process 4 collisions at a time for cache efficiency
  size_t collIdx = 0;
  const size_t collSimdEnd = (m_collisionPool.collisionBuffer.size() / 4) * 4;

  for (; collIdx < collSimdEnd; collIdx += 4) {
    // Process 4 collisions in a batch
    for (size_t j = 0; j < 4; ++j) {
      const auto &collision = m_collisionPool.collisionBuffer[collIdx + j];
      resolve(collision);
      for (const auto &cb : m_callbacks) {
        cb(collision);
      }
    }
  }

  // Scalar tail
  for (; collIdx < m_collisionPool.collisionBuffer.size(); ++collIdx) {
    const auto &collision = m_collisionPool.collisionBuffer[collIdx];
    resolve(collision);
    for (const auto &cb : m_callbacks) {
      cb(collision);
    }
  }
#ifdef DEBUG
  auto t4 = clock::now();
#endif

#ifdef DEBUG
  auto t5 = clock::now();
#endif

  // TRIGGER PROCESSING: Handle trigger enter/exit events
  // PHASE 3.2: Detect EventOnly triggers (bypassed broadphase)
  detectEventOnlyTriggers();
  processTriggerEvents();

#ifdef DEBUG
  auto t6 = clock::now();

  // Track detailed performance metrics (debug only - zero overhead in release)
  updatePerformanceMetrics(
      t0, t1, t2, t3, t4, t5, t6, bodyCount, activeMovableBodies, pairCount,
      m_collisionPool.collisionBuffer.size(), activeBodies, dynamicBodiesCulled,
      staticBodiesCulled, cullingMs, totalStaticBodies, totalMovableBodies);
#endif
}

// ========== SOA UPDATE HELPER METHODS ==========

// Sync spatial hash for active bodies after culling
void CollisionManager::syncSpatialHashesWithActiveIndices() {
  // OPTIMIZATION: Dynamic spatial hash no longer needed!
  // The optimized broadphase uses:
  // - pools.movableIndices for movable-vs-movable (direct SIMD iteration)
  // - pools.staticIndices or m_staticSpatialHash for movable-vs-static
  // This eliminates ~160 hash insert calls per frame (each with allocation +
  // mutex overhead)

  // Static hash is managed separately via rebuildStaticSpatialHash()
}

void CollisionManager::rebuildStaticSpatialHash() {
  std::lock_guard<std::mutex> lock(m_staticRebuildMutex);
  rebuildStaticSpatialHashUnlocked();
}

void CollisionManager::rebuildStaticSpatialHashUnlocked() {
  // Only called when static objects are added/removed
  m_staticSpatialHash.clear();
  m_eventOnlySpatialHash.clear();

  for (size_t i = 0; i < m_storage.hotData.size(); ++i) {
    const auto &hot = m_storage.hotData[i];
    if (!hot.active)
      continue;

    BodyType bodyType = static_cast<BodyType>(hot.bodyType);
    if (bodyType == BodyType::STATIC) {
      AABB aabb = m_storage.computeAABB(i);

      // EventOnly triggers go to separate hash (keeps broadphase fast)
      if (hot.isTrigger != 0 &&
          hot.triggerType ==
              static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly)) {
        m_eventOnlySpatialHash.insert(i, aabb);
      } else {
        m_staticSpatialHash.insert(i, aabb);
      }
    }
  }
}

// NOTE: updateStaticCollisionCacheForMovableBodies(), evictStaleCacheEntries(),
// and populateCacheForRegion() removed in Phase 3 - static bodies queried
// directly via m_staticSpatialHash

void CollisionManager::resolve(const CollisionInfo &collision) {
  if (collision.trigger)
    return; // Triggers don't need position resolution

  auto &edm = EntityDataManager::Instance();

  // EDM-CENTRIC resolution:
  // - isMovableMovable=true: both indexA and indexB are EDM indices
  // - isMovableMovable=false: indexA is EDM index (movable), indexB is storage
  // index (static - doesn't move)

  if (collision.isMovableMovable) {
    // MOVABLE-MOVABLE: Both indices are EDM indices
    size_t edmIdxA = collision.indexA;
    size_t edmIdxB = collision.indexB;

    // Indices already validated in narrowphase
    if (edmIdxA == SIZE_MAX || edmIdxB == SIZE_MAX)
      return;

    const float push = collision.penetration * 0.5f;

    // Both movables - split the correction
    Float4 const normal =
        set(collision.normal.getX(), collision.normal.getY(), 0, 0);
    Float4 const pushVec = broadcast(push);
    Float4 const correction = mul(normal, pushVec);

    // Read positions from EDM
    auto &transformA = edm.getTransformByIndex(edmIdxA);
    auto &transformB = edm.getTransformByIndex(edmIdxB);
    Float4 posA =
        set(transformA.position.getX(), transformA.position.getY(), 0, 0);
    Float4 posB =
        set(transformB.position.getX(), transformB.position.getY(), 0, 0);

    posA = sub(posA, correction);
    posB = add(posB, correction);

    alignas(16) float resultA[4], resultB[4];
    store4_aligned(resultA, posA);
    store4_aligned(resultB, posB);

    // Write positions back to EDM
    transformA.position.setX(resultA[0]);
    transformA.position.setY(resultA[1]);
    transformB.position.setX(resultB[0]);
    transformB.position.setY(resultB[1]);
  } else {
    // MOVABLE-STATIC: indexA is EDM index (movable), indexB is storage index
    // (static)
    size_t edmIdx = collision.indexA;

    // Index already validated in narrowphase
    if (edmIdx == SIZE_MAX)
      return;

    // Only movable body moves - push fully away from static
    Float4 const normal =
        set(collision.normal.getX(), collision.normal.getY(), 0, 0);
    Float4 const penVec = broadcast(collision.penetration);
    Float4 const correction = mul(normal, penVec);

    auto &transform = edm.getTransformByIndex(edmIdx);
    Float4 pos =
        set(transform.position.getX(), transform.position.getY(), 0, 0);
    pos = sub(pos, correction);

    alignas(16) float result[4];
    store4_aligned(result, pos);

    // Write position back to EDM
    transform.position.setX(result[0]);
    transform.position.setY(result[1]);
  }
}

void CollisionManager::detectEventOnlyTriggers() {
  // Detect EventOnly trigger overlaps via per-entity spatial query
  // Adaptive strategy based on entity count:
  // - <50 entities: Spatial queries O(N × ~k nearby triggers)
  // - >=50 entities: Sweep-and-prune O((N+T) log (N+T))

  auto &pools = m_collisionPool;
  pools.eventOnlyOverlaps.clear();

  const auto &edm = EntityDataManager::Instance();
  auto triggerIndices = edm.getTriggerDetectionIndices();

  if (triggerIndices.empty() || pools.movableAABBs.empty()) {
    return;
  }

  // ADAPTIVE STRATEGY: Switch at 50 entities (tunable threshold)
  constexpr size_t SWEEP_THRESHOLD = 50;

  if (triggerIndices.size() < SWEEP_THRESHOLD) {
    // SPATIAL QUERY PATH: O(N × ~k nearby triggers)
    detectEventOnlyTriggersSpatial(triggerIndices);
  } else {
    // SWEEP-AND-PRUNE PATH: O((N+T) log (N+T))
    detectEventOnlyTriggersSweep(triggerIndices);
  }
}

// Path A: Spatial queries for small entity counts
void CollisionManager::detectEventOnlyTriggersSpatial(
    std::span<const size_t> triggerIndices) {
  auto &edm = EntityDataManager::Instance();
  auto &pools = m_collisionPool;

  for (size_t edmIdx : triggerIndices) {
    const auto &hot = edm.getHotDataByIndex(edmIdx);
    const auto &transform = edm.getTransformByIndex(edmIdx);

    float px = transform.position.getX();
    float py = transform.position.getY();
    float hw = hot.halfWidth;
    float hh = hot.halfHeight;
    AABB entityAABB(px, py, hw, hh);

    m_triggerCandidates.clear();
    m_eventOnlySpatialHash.queryRegion(entityAABB, m_triggerCandidates);

    // Find pool index
    size_t poolIdx = findPoolIndex(edmIdx);
    if (poolIdx == SIZE_MAX)
      continue;

    // Filter for EventOnly triggers
    for (size_t storageIdx : m_triggerCandidates) {
      if (isEventOnlyTriggerOverlap(storageIdx, px, py, hw, hh,
                                    hot.collisionMask)) {
        pools.eventOnlyOverlaps.push_back({poolIdx, storageIdx});
      }
    }
  }
}

// Path B: Sweep-and-prune for large entity counts
void CollisionManager::detectEventOnlyTriggersSweep(
    std::span<const size_t> triggerIndices) {
  auto &edm = EntityDataManager::Instance();

  // Build sorted edge list (entities + eventOnly triggers)
  struct Edge {
    float x;
    size_t idx; // edmIdx for entities, storageIdx for triggers
    bool isStart;
    bool isTrigger;
  };
  std::vector<Edge> edges;
  edges.reserve((triggerIndices.size() + m_storage.size()) * 2);

  // Add entity edges
  for (size_t edmIdx : triggerIndices) {
    const auto &hot = edm.getHotDataByIndex(edmIdx);
    const auto &transform = edm.getTransformByIndex(edmIdx);
    float px = transform.position.getX();
    float hw = hot.halfWidth;
    edges.push_back({px - hw, edmIdx, true, false});
    edges.push_back({px + hw, edmIdx, false, false});
  }

  // Add EventOnly trigger edges (scan storage once)
  for (size_t i = 0; i < m_storage.size(); ++i) {
    const auto &hot = m_storage.hotData[i];
    if (!hot.active || hot.isTrigger == 0 ||
        hot.triggerType !=
            static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly)) {
      continue;
    }
    edges.push_back({hot.aabbMinX, i, true, true});
    edges.push_back({hot.aabbMaxX, i, false, true});
  }

  // Sort by X coordinate
  std::sort(edges.begin(), edges.end(), [](const Edge &a, const Edge &b) {
    return a.x < b.x || (a.x == b.x && a.isStart > b.isStart);
  });

  // Sweep: track active entities and triggers
  std::unordered_set<size_t> activeEntities;
  std::unordered_set<size_t> activeTriggers;

  for (const auto &edge : edges) {
    if (edge.isStart) {
      if (edge.isTrigger) {
        // Trigger starting - test against all active entities
        for (size_t edmIdx : activeEntities) {
          testTriggerOverlapAndRecord(edmIdx, edge.idx);
        }
        activeTriggers.insert(edge.idx);
      } else {
        // Entity starting - test against all active triggers
        for (size_t storageIdx : activeTriggers) {
          testTriggerOverlapAndRecord(edge.idx, storageIdx);
        }
        activeEntities.insert(edge.idx);
      }
    } else {
      // Edge ending - remove from active set
      if (edge.isTrigger)
        activeTriggers.erase(edge.idx);
      else
        activeEntities.erase(edge.idx);
    }
  }
}

// Helper: Test Y overlap and layer mask, record if valid
void CollisionManager::testTriggerOverlapAndRecord(size_t edmIdx,
                                                   size_t storageIdx) {
  auto &edm = EntityDataManager::Instance();
  const auto &entityHot = edm.getHotDataByIndex(edmIdx);
  const auto &entityTransform = edm.getTransformByIndex(edmIdx);
  const auto &triggerHot = m_storage.hotData[storageIdx];

  float py = entityTransform.position.getY();
  float hh = entityHot.halfHeight;

  // Y overlap check
  if (py + hh < triggerHot.aabbMinY || py - hh > triggerHot.aabbMaxY)
    return;

  // Layer check
  if ((entityHot.collisionMask & triggerHot.layers) == 0)
    return;

  // Find pool index and record
  size_t poolIdx = findPoolIndex(edmIdx);
  if (poolIdx != SIZE_MAX) {
    m_collisionPool.eventOnlyOverlaps.push_back({poolIdx, storageIdx});
  }
}

// Helper: Find the pool index for a given EDM index
size_t CollisionManager::findPoolIndex(size_t edmIdx) const {
  const auto &pools = m_collisionPool;
  for (size_t i = 0; i < pools.movableIndices.size(); ++i) {
    if (pools.movableIndices[i] == edmIdx) {
      return i;
    }
  }
  return SIZE_MAX;
}

// Helper: Check if storage index is an EventOnly trigger overlapping the given
// AABB
bool CollisionManager::isEventOnlyTriggerOverlap(size_t storageIdx, float px,
                                                 float py, float hw, float hh,
                                                 uint16_t mask) const {
  const auto &hot = m_storage.hotData[storageIdx];

  // Must be an active EventOnly trigger
  if (!hot.active || hot.isTrigger == 0 ||
      hot.triggerType !=
          static_cast<uint8_t>(HammerEngine::TriggerType::EventOnly)) {
    return false;
  }

  // AABB overlap test
  if (px + hw < hot.aabbMinX || px - hw > hot.aabbMaxX ||
      py + hh < hot.aabbMinY || py - hh > hot.aabbMaxY) {
    return false;
  }

  // Layer mask check
  return (mask & hot.layers) != 0;
}

void CollisionManager::processTriggerEvents() {
  // EDM-CENTRIC: Process trigger events with correct index semantics
  // - Movable-movable (isMovableMovable=true): both indices are EDM indices
  // (skip - movables aren't triggers)
  // - Movable-static (isMovableMovable=false): indexA is EDM index, indexB is
  // m_storage index

  auto makeKey = [](EntityID a, EntityID b) -> uint64_t {
    uint64_t x = static_cast<uint64_t>(a);
    uint64_t y = static_cast<uint64_t>(b);
    if (x > y)
      std::swap(x, y);
    return (x * 1469598103934665603ull) ^ (y + 1099511628211ull);
  };

  auto now = std::chrono::steady_clock::now();
  auto &edm = EntityDataManager::Instance();

  // Reuse member buffer to avoid per-frame hash table allocation
  m_currentTriggerPairsBuffer.clear();

  for (const auto &collision : m_collisionPool.collisionBuffer) {
    // EDM-CENTRIC: Skip movable-movable collisions (neither can be a trigger)
    if (collision.isMovableMovable) {
      continue;
    }

    // Movable-static collision: indexA = EDM index (movable), indexB =
    // m_storage index (static)
    size_t edmIdx = collision.indexA;
    size_t storageIdx = collision.indexB;

    // Validate indices
    if (storageIdx >= m_storage.hotData.size())
      continue;

    // Get movable data from EDM, static data from m_storage
    const auto &movableHot = edm.getHotDataByIndex(edmIdx);
    const auto &staticHot = m_storage.hotData[storageIdx];

    // Check for player-trigger interaction
    // Player is always the movable (indexA/EDM), trigger is always static
    // (indexB/m_storage)
    bool isPlayer =
        (movableHot.collisionLayers & CollisionLayer::Layer_Player) != 0;
    bool staticIsTrigger = staticHot.isTrigger;

    if (!isPlayer || !staticIsTrigger) {
      continue; // Not a player-trigger interaction
    }

    EntityID playerId = collision.a;
    EntityID triggerId = collision.b;

    uint64_t key = makeKey(playerId, triggerId);
    m_currentTriggerPairsBuffer.insert(key);

    if (!m_activeTriggerPairs.count(key)) {
      // Check cooldown
      auto cdIt = m_triggerCooldownUntil.find(triggerId);
      bool cooled =
          (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);

      if (cooled) {
        HammerEngine::TriggerTag triggerTag =
            static_cast<HammerEngine::TriggerTag>(staticHot.triggerTag);
        // Get player position from EDM (single source of truth) using EDM index
        // directly
        const auto &playerTransform = edm.getTransformByIndex(edmIdx);
        Vector2D playerPos = playerTransform.position;

        WorldTriggerEvent evt(playerId, triggerId, triggerTag, playerPos,
                              TriggerPhase::Enter);
        EventManager::Instance().triggerWorldTrigger(
            evt, EventManager::DispatchMode::Deferred);

        COLLISION_DEBUG(std::format(
            "Player {} ENTERED trigger {} (tag: {}) at position ({}, {})",
            playerId, triggerId, static_cast<int>(triggerTag), playerPos.getX(),
            playerPos.getY()));

        if (m_defaultTriggerCooldownSec > 0.0f) {
          m_triggerCooldownUntil[triggerId] =
              now +
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double>(m_defaultTriggerCooldownSec));
        }
      }

      m_activeTriggerPairs.emplace(key, std::make_pair(playerId, triggerId));
    }
  }

  // Process EventOnly trigger overlaps (detected via per-entity spatial query)
  // Only entities with NEEDS_TRIGGER_DETECTION flag are in eventOnlyOverlaps
  for (const auto &overlap : m_collisionPool.eventOnlyOverlaps) {
    size_t movablePoolIdx = overlap.movablePoolIdx;
    size_t storageIdx = overlap.triggerStorageIdx;

    // Validate indices
    if (movablePoolIdx >= m_collisionPool.movableAABBs.size())
      continue;
    if (storageIdx >= m_storage.hotData.size())
      continue;

    // Get EDM index from pool index
    size_t edmIdx = m_collisionPool.movableIndices[movablePoolIdx];
    const auto &movableAABB = m_collisionPool.movableAABBs[movablePoolIdx];
    const auto &staticHot = m_storage.hotData[storageIdx];

    // Entity already has NEEDS_TRIGGER_DETECTION flag (filtered in
    // detectEventOnlyTriggers)

    EntityID entityId = movableAABB.entityId;
    EntityID triggerId = m_storage.entityIds[storageIdx];

    uint64_t key = makeKey(entityId, triggerId);
    m_currentTriggerPairsBuffer.insert(key);

    if (!m_activeTriggerPairs.count(key)) {
      // Check cooldown
      auto cdIt = m_triggerCooldownUntil.find(triggerId);
      bool cooled =
          (cdIt == m_triggerCooldownUntil.end()) || (now >= cdIt->second);

      if (cooled) {
        HammerEngine::TriggerTag triggerTag =
            static_cast<HammerEngine::TriggerTag>(staticHot.triggerTag);
        // Get entity position from EDM (single source of truth)
        const auto &entityTransform = edm.getTransformByIndex(edmIdx);
        Vector2D entityPos = entityTransform.position;

        WorldTriggerEvent evt(entityId, triggerId, triggerTag, entityPos,
                              TriggerPhase::Enter);
        EventManager::Instance().triggerWorldTrigger(
            evt, EventManager::DispatchMode::Deferred);

        COLLISION_DEBUG(std::format("Entity {} ENTERED EventOnly trigger {} "
                                    "(tag: {}) at position ({}, {})",
                                    entityId, triggerId,
                                    static_cast<int>(triggerTag),
                                    entityPos.getX(), entityPos.getY()));

        if (m_defaultTriggerCooldownSec > 0.0f) {
          m_triggerCooldownUntil[triggerId] =
              now +
              std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                  std::chrono::duration<double>(m_defaultTriggerCooldownSec));
        }
      }

      m_activeTriggerPairs.emplace(key, std::make_pair(entityId, triggerId));
    }
  }

  // Remove stale pairs (trigger exits)
  for (auto it = m_activeTriggerPairs.begin();
       it != m_activeTriggerPairs.end();) {
    if (!m_currentTriggerPairsBuffer.count(it->first)) {
      EntityID entityId = it->second.first;
      EntityID triggerId = it->second.second;

      // Find trigger hot data for position - use hash lookup instead of linear
      // search
      Vector2D triggerPos(0, 0);
      HammerEngine::TriggerTag triggerTag = HammerEngine::TriggerTag::None;
      auto triggerIt = m_storage.entityToIndex.find(triggerId);
      if (triggerIt != m_storage.entityToIndex.end()) {
        size_t triggerIndex = triggerIt->second;
        if (triggerIndex < m_storage.hotData.size()) {
          const auto &hot = m_storage.hotData[triggerIndex];
          // Triggers don't move - use cached AABB center (no EDM lookup needed)
          triggerPos = Vector2D((hot.aabbMinX + hot.aabbMaxX) * 0.5f,
                                (hot.aabbMinY + hot.aabbMaxY) * 0.5f);
          triggerTag = static_cast<HammerEngine::TriggerTag>(hot.triggerTag);
        }
      }

      WorldTriggerEvent evt(entityId, triggerId, triggerTag, triggerPos,
                            TriggerPhase::Exit);
      EventManager::Instance().triggerWorldTrigger(
          evt, EventManager::DispatchMode::Deferred);

      COLLISION_DEBUG(std::format(
          "Entity {} EXITED trigger {} (tag: {}) at position ({}, {})",
          entityId, triggerId, static_cast<int>(triggerTag), triggerPos.getX(),
          triggerPos.getY()));

      it = m_activeTriggerPairs.erase(it);
    } else {
      ++it;
    }
  }
}

void CollisionManager::updatePerformanceMetrics(
    std::chrono::steady_clock::time_point t0,
    std::chrono::steady_clock::time_point t1,
    std::chrono::steady_clock::time_point t2,
    std::chrono::steady_clock::time_point t3,
    std::chrono::steady_clock::time_point t4,
    std::chrono::steady_clock::time_point t5,
    std::chrono::steady_clock::time_point t6, size_t bodyCount,
    size_t activeMovableBodies, size_t pairCount, size_t collisionCount,
    size_t activeBodies, size_t dynamicBodiesCulled, size_t staticBodiesCulled,
    double cullingMs, size_t totalStaticBodies, size_t totalMovableBodies) {

  // Basic counters - always tracked (minimal overhead)
  m_perf.lastPairs = pairCount;
  m_perf.lastCollisions = collisionCount;
  m_perf.bodyCount = bodyCount;
  m_perf.frames += 1;

#ifdef DEBUG
  // TRIGGER DETECTION METRICS: Debug/benchmark only
  m_perf.lastTriggerDetectors =
      EntityDataManager::Instance().getTriggerDetectionIndices().size();
  m_perf.lastTriggerOverlaps = m_collisionPool.eventOnlyOverlaps.size();

  // Detailed timing metrics and logging - zero overhead in release builds
  auto d12 =
      std::chrono::duration<double, std::milli>(t2 - t1).count(); // Broadphase
  auto d23 =
      std::chrono::duration<double, std::milli>(t3 - t2).count(); // Narrowphase
  auto d34 =
      std::chrono::duration<double, std::milli>(t4 - t3).count(); // Resolution
  auto d45 = std::chrono::duration<double, std::milli>(t5 - t4)
                 .count(); // Sync entities
  auto d56 = std::chrono::duration<double, std::milli>(t6 - t5)
                 .count(); // Trigger processing
  auto d06 =
      std::chrono::duration<double, std::milli>(t6 - t0).count(); // Total

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
  COLLISION_WARN_IF(
      m_perf.lastTotalMs > 5.0 && logFrameCounter % 60 == 0,
      std::format(
          "Slow frame: {:.2f}ms (broad:{:.2f}, narrow:{:.2f}, pairs:{})",
          m_perf.lastTotalMs, d12, d23, pairCount));

  // Periodic statistics (every 300 frames) - concise format with phase
  // breakdown
  if (logFrameCounter % 300 == 0 && bodyCount > 0) {
    size_t staticsInBroadphase = m_collisionPool.staticIndices.size();
    size_t triggerDetectionEntities =
        EntityDataManager::Instance().getTriggerDetectionIndices().size();
    size_t eventOnlyOverlaps = m_collisionPool.eventOnlyOverlaps.size();
    auto d01 = std::chrono::duration<double, std::milli>(t1 - t0)
                   .count(); // Pre-broadphase setup
    COLLISION_DEBUG(std::format("Collision: {} movable, {} static "
                                "(broadphase), {:.2f}ms (pairs:{}, hits:{})",
                                activeMovableBodies, staticsInBroadphase,
                                m_perf.lastTotalMs, pairCount, collisionCount));
    COLLISION_DEBUG(std::format(
        "  Phases: setup:{:.2f}ms, broad:{:.2f}ms, narrow:{:.2f}ms, "
        "resolve:{:.2f}ms | triggerDetect:{} overlaps:{}",
        d01, d12, d23, d34, triggerDetectionEntities, eventOnlyOverlaps));
  }
#endif // DEBUG
}

void CollisionManager::updateKinematicBatch(
    const std::vector<KinematicUpdate> &updates) {
  if (updates.empty())
    return;

  // Batch update all kinematic bodies - update EDM (single source of truth)
  auto &edm = EntityDataManager::Instance();
  for (const auto &bodyUpdate : updates) {
    auto it = m_storage.entityToIndex.find(bodyUpdate.id);
    if (it != m_storage.entityToIndex.end() && it->second < m_storage.size()) {
      size_t index = it->second;
      auto &hot = m_storage.hotData[index];
      if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC &&
          hot.edmIndex != SIZE_MAX) {
        // Update EDM - it owns position/velocity
        auto &transform = edm.getTransformByIndex(hot.edmIndex);
        transform.position = bodyUpdate.position;
        transform.velocity = bodyUpdate.velocity;

        // Update cached AABB immediately for spatial queries
        const auto &edmHot = edm.getHotDataByIndex(hot.edmIndex);
        float px = bodyUpdate.position.getX();
        float py = bodyUpdate.position.getY();
        float hw = edmHot.halfWidth;
        float hh = edmHot.halfHeight;

        hot.aabbMinX = px - hw;
        hot.aabbMinY = py - hh;
        hot.aabbMaxX = px + hw;
        hot.aabbMaxY = py + hh;
        hot.active = true;
      }
    }
  }
}

void CollisionManager::applyBatchedKinematicUpdates(
    const std::vector<std::vector<KinematicUpdate>> &batchUpdates) {
  // PER-BATCH COLLISION UPDATES: Zero contention approach
  // Each AI batch has its own buffer, we merge them here with no mutex needed.
  // This eliminates the serialization bottleneck that caused frame jitter.

  if (batchUpdates.empty())
    return;

  // Count total updates for efficiency
  size_t totalUpdates = std::accumulate(
      batchUpdates.begin(), batchUpdates.end(), size_t{0},
      [](size_t sum, const auto &batch) { return sum + batch.size(); });

  if (totalUpdates == 0)
    return;

  // Merge all batch updates into EDM (single source of truth)
  auto &edm = EntityDataManager::Instance();
  for (const auto &batchBuffer : batchUpdates) {
    for (const auto &bodyUpdate : batchBuffer) {
      auto it = m_storage.entityToIndex.find(bodyUpdate.id);
      if (it != m_storage.entityToIndex.end() &&
          it->second < m_storage.size()) {
        size_t index = it->second;
        auto &hot = m_storage.hotData[index];
        if (static_cast<BodyType>(hot.bodyType) == BodyType::KINEMATIC &&
            hot.edmIndex != SIZE_MAX) {
          // Update EDM - it owns position/velocity
          auto &transform = edm.getTransformByIndex(hot.edmIndex);
          transform.position = bodyUpdate.position;
          transform.velocity = bodyUpdate.velocity;

          // Update cached AABB immediately for spatial queries
          const auto &edmHot = edm.getHotDataByIndex(hot.edmIndex);
          float px = bodyUpdate.position.getX();
          float py = bodyUpdate.position.getY();
          float hw = edmHot.halfWidth;
          float hh = edmHot.halfHeight;

          hot.aabbMinX = px - hw;
          hot.aabbMinY = py - hh;
          hot.aabbMaxX = px + hw;
          hot.aabbMaxY = py + hh;
          hot.active = true;
        }
      }
    }
  }
}

void CollisionManager::applyKinematicUpdates(
    std::vector<KinematicUpdate> &updates) {
  // Convenience wrapper for single-vector updates (avoids allocation in caller)
  // Just wrap in a single-element batch and call the batched version
  if (updates.empty())
    return;

  std::vector<std::vector<KinematicUpdate>> singleBatch(1);
  singleBatch[0] = std::move(updates); // Move to avoid copy
  applyBatchedKinematicUpdates(singleBatch);
  updates = std::move(singleBatch[0]); // Move back to preserve buffer for reuse
}

// ========== SOA BODY MANAGEMENT METHODS ==========

void CollisionManager::setBodyEnabled(EntityID id, bool enabled) {
  size_t index;
  if (getCollisionBody(id, index)) {
    m_storage.hotData[index].active = enabled ? 1 : 0;
  }
}

void CollisionManager::setBodyLayer(EntityID id, uint32_t layerMask,
                                    uint32_t collideMask) {
  size_t index;
  if (getCollisionBody(id, index)) {
    auto &hot = m_storage.hotData[index];
    hot.layers = layerMask;
    hot.collidesWith = collideMask;
  }
}

void CollisionManager::setVelocity(EntityID id, const Vector2D &velocity) {
  size_t index;
  if (getCollisionBody(id, index)) {
    auto &hot = m_storage.hotData[index];
    // Static bodies have no velocity
    if (static_cast<BodyType>(hot.bodyType) == BodyType::STATIC) {
      return;
    }
    // Update EDM - it owns velocity (if registered)
    if (hot.edmIndex != SIZE_MAX) {
      auto &transform =
          EntityDataManager::Instance().getTransformByIndex(hot.edmIndex);
      transform.velocity = velocity;
    }
  }
}

void CollisionManager::setBodyTrigger(EntityID id, bool isTrigger) {
  size_t index;
  if (getCollisionBody(id, index)) {
    m_storage.hotData[index].isTrigger = isTrigger ? 1 : 0;
  }
}

CollisionManager::CullingArea
CollisionManager::createDefaultCullingArea() const {
  // EDM-CENTRIC: Center culling on player position (reference point from BGM)
  // This matches the tier system's proximity filtering used by AIManager
  const auto &bgm = BackgroundSimulationManager::Instance();
  Vector2D refPoint = bgm.getReferencePoint();
  float radius = bgm.getActiveRadius();

  CullingArea area;
  area.minX = refPoint.getX() - radius;
  area.minY = refPoint.getY() - radius;
  area.maxX = refPoint.getX() + radius;
  area.maxY = refPoint.getY() + radius;
  return area;
}

// ========== PERFORMANCE: VECTOR POOLING METHODS ==========

std::vector<size_t> &CollisionManager::getPooledVector() {
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
  size_t idx = m_nextPoolIndex.fetch_add(1, std::memory_order_relaxed) %
               m_vectorPool.size();

  auto &vec = m_vectorPool[idx];
  vec.clear(); // Clear but retain capacity
  return vec;
}

void CollisionManager::returnPooledVector(std::vector<size_t> &vec) {
  // Vector is automatically returned to pool via reference
  // Just clear it to avoid holding onto data
  vec.clear();
}
