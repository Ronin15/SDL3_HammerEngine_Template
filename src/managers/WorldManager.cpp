/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "utils/Camera.hpp"
#include "utils/FrameProfiler.hpp"
#include "utils/SIMDMath.hpp"
#include "core/ThreadSystem.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/TimeEvent.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "utils/JsonReader.hpp"

#include "events/HarvestResourceEvent.hpp"
#include <algorithm>
#include <cmath>
#include <format>

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#include "gpu/SpriteBatch.hpp"
#endif

using namespace HammerEngine::SIMD;

bool WorldManager::init() {
  if (m_initialized.load(std::memory_order_acquire)) {
    WORLD_MANAGER_WARN("WorldManager already initialized");
    return true;
  }

  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  if (m_initialized.load(std::memory_order_acquire)) {
    return true; // Double-check after acquiring lock
  }

  try {
    m_tileRenderer = std::make_unique<HammerEngine::TileRenderer>();
    // Note: Event handlers will be registered later to avoid race conditions
    // with EventManager

    m_isShutdown = false;
    m_initialized.store(true, std::memory_order_release);
    WORLD_MANAGER_INFO("WorldManager initialized successfully (event handlers "
                       "will be registered later)");
    return true;
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("WorldManager::init - Exception: {}", ex.what()));
    return false;
  }
}

void WorldManager::setupEventHandlers() {
  if (!m_initialized.load(std::memory_order_acquire)) {
    WORLD_MANAGER_ERROR(
        "WorldManager not initialized - cannot setup event handlers");
    return;
  }

  try {
    registerEventHandlers();

    // Subscribe TileRenderer to season events for seasonal texture switching
    if (m_tileRenderer) {
      m_tileRenderer->subscribeToSeasonEvents();
    }

    WORLD_MANAGER_DEBUG("WorldManager event handlers setup complete");
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(std::format(
        "WorldManager::setupEventHandlers - Exception: {}", ex.what()));
  }
}

void WorldManager::clean() {
  if (!m_initialized.load(std::memory_order_acquire) || m_isShutdown) {
    return;
  }

  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  unregisterEventHandlers();

  // Unsubscribe TileRenderer from season events before cleanup
  if (m_tileRenderer) {
    m_tileRenderer->unsubscribeFromSeasonEvents();
  }

  unloadWorldUnsafe(); // Use unsafe version to avoid double-locking
  m_tileRenderer.reset();

  m_initialized.store(false, std::memory_order_release);
  m_isShutdown = true;

  WORLD_MANAGER_INFO("WorldManager cleaned up");
}

bool WorldManager::loadNewWorld(
    const HammerEngine::WorldGenerationConfig &config,
    const HammerEngine::WorldGenerationProgressCallback &progressCallback) {
  if (!m_initialized.load(std::memory_order_acquire)) {
    WORLD_MANAGER_ERROR("WorldManager not initialized");
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  try {
    auto newWorld =
        HammerEngine::WorldGenerator::generateWorld(config, progressCallback);
    if (!newWorld) {
      WORLD_MANAGER_ERROR("Failed to generate new world");
      return false;
    }

    // Unload current world if it exists
    WORLD_MANAGER_INFO_IF(
        m_currentWorld,
        std::format("Unloading current world: {}", m_currentWorld->worldId));

    // Set new world
    m_currentWorld = std::move(newWorld);

    // Register world with WorldResourceManager
    WorldResourceManager::Instance().createWorld(m_currentWorld->worldId);

    // Initialize world resources based on world data
    initializeWorldResources();

    WORLD_MANAGER_INFO(std::format("Successfully loaded new world: {}",
                                   m_currentWorld->worldId));

    // Schedule world loaded event for next frame using ThreadSystem
    // Don't fire event while holding world mutex - use low priority to avoid
    // blocking critical tasks
    std::string worldIdCopy = m_currentWorld->worldId;
    // Schedule world loaded event for next frame using ThreadSystem to avoid
    // deadlocks Use high priority to ensure it executes quickly for tests
    WORLD_MANAGER_INFO(std::format(
        "Enqueuing WorldLoadedEvent task for world: {}", worldIdCopy));
    HammerEngine::ThreadSystem::Instance().enqueueTask(
        [worldIdCopy, this]() {
          WORLD_MANAGER_INFO(std::format(
              "Executing WorldLoadedEvent task for world: {}", worldIdCopy));
          fireWorldLoadedEvent(worldIdCopy);
        },
        HammerEngine::TaskPriority::High,
        std::format("WorldLoadedEvent_{}", worldIdCopy));

    return true;
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("WorldManager::loadNewWorld - Exception: {}", ex.what()));
    return false;
  }
}

bool WorldManager::loadWorld(const std::string &worldId) {
  if (!m_initialized.load(std::memory_order_acquire)) {
    WORLD_MANAGER_ERROR("WorldManager not initialized");
    return false;
  }

  // Suppress unused parameter warning since this is not implemented yet
  (void)worldId;

  WORLD_MANAGER_WARN(
      "WorldManager::loadWorld - Loading saved worlds not yet implemented");
  return false;
}

void WorldManager::unloadWorld() {
  // Take exclusive lock to ensure atomic unload operation
  // This prevents render thread from accessing world data during deallocation
  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  unloadWorldUnsafe();
}

void WorldManager::unloadWorldUnsafe() {
  // Internal method - assumes caller already holds the lock
  if (m_currentWorld) {
    std::string worldId = m_currentWorld->worldId;
    WORLD_MANAGER_INFO(std::format("Unloading world: {}", worldId));

    // Fire world unloaded event before clearing the world
    fireWorldUnloadedEvent(worldId);

    // Clear chunk cache to prevent stale textures when new world loads
    // Uses deferred clearing (thread-safe) - actual clear happens on render
    // thread
    if (m_tileRenderer) {
      m_tileRenderer->clearChunkCache();
    }

    m_currentWorld.reset();
  }
}

const HammerEngine::Tile *WorldManager::getTileAt(int x, int y) const {
  std::shared_lock<std::shared_mutex> lock(m_worldMutex);

  if (!m_currentWorld || !isValidPosition(x, y)) {
    return nullptr;
  }

  return &m_currentWorld->grid[y][x];
}

HammerEngine::Tile *WorldManager::getTileAt(int x, int y) {
  std::shared_lock<std::shared_mutex> lock(m_worldMutex);

  if (!m_currentWorld || !isValidPosition(x, y)) {
    return nullptr;
  }

  return &m_currentWorld->grid[y][x];
}

bool WorldManager::isValidPosition(int x, int y) const {
  if (!m_currentWorld) {
    return false;
  }

  const int height = static_cast<int>(m_currentWorld->grid.size());
  const int width =
      height > 0 ? static_cast<int>(m_currentWorld->grid[0].size()) : 0;

  return x >= 0 && x < width && y >= 0 && y < height;
}

std::string WorldManager::getCurrentWorldId() const {
  std::shared_lock<std::shared_mutex> lock(m_worldMutex);
  return m_currentWorld ? m_currentWorld->worldId : "";
}

bool WorldManager::hasActiveWorld() const {
  std::shared_lock<std::shared_mutex> lock(m_worldMutex);
  return m_currentWorld != nullptr;
}

void WorldManager::update() {
  if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld) {
    return;
  }

  // Currently no per-frame world updates needed
  // This could be extended for dynamic world changes, weather effects, etc.
}

void WorldManager::render(SDL_Renderer* renderer, float cameraX, float cameraY,
                          float viewportWidth, float viewportHeight) {
  if (!m_initialized.load(std::memory_order_acquire) || !m_renderingEnabled ||
      !renderer) {
    return;
  }

  // No lock needed - render always runs on main thread after update completes.
  // World modifications (load/unload/harvest) also run on main thread sequentially.
  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  m_tileRenderer->render(*m_currentWorld, renderer, cameraX, cameraY,
                         viewportWidth, viewportHeight);
}

void WorldManager::prefetchChunks(SDL_Renderer* renderer, HammerEngine::Camera& camera) {
  if (!m_initialized.load(std::memory_order_acquire) || !m_renderingEnabled ||
      !renderer) {
    return;
  }

  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  // Get camera parameters
  float zoom = camera.getZoom();
  const auto& viewport = camera.getViewport();
  float viewWidth = viewport.width / zoom;
  float viewHeight = viewport.height / zoom;

  // Get floored camera position
  float rawX = 0.0f;
  float rawY = 0.0f;
  camera.getRenderOffset(rawX, rawY, 0.0f);
  float cameraX = std::floor(rawX);
  float cameraY = std::floor(rawY);

  m_tileRenderer->prefetchChunks(*m_currentWorld, renderer, cameraX, cameraY,
                                  viewWidth, viewHeight);
}

void WorldManager::prewarmChunks(SDL_Renderer* renderer, float cameraX, float cameraY,
                                  float viewportWidth, float viewportHeight) {
  if (!m_initialized.load(std::memory_order_acquire) || !renderer) {
    return;
  }

  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  m_tileRenderer->prewarmChunks(*m_currentWorld, renderer, cameraX, cameraY,
                                 viewportWidth, viewportHeight);
}

void WorldManager::prefetchChunksInternal() {
  if (!m_initialized.load(std::memory_order_acquire) || !m_renderingEnabled ||
      !mp_renderer || !mp_activeCamera) {
    return;
  }

  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  // Get camera parameters
  float zoom = mp_activeCamera->getZoom();
  const auto& viewport = mp_activeCamera->getViewport();
  float viewWidth = viewport.width / zoom;
  float viewHeight = viewport.height / zoom;

  // Get floored camera position
  float rawX = 0.0f;
  float rawY = 0.0f;
  mp_activeCamera->getRenderOffset(rawX, rawY, 0.0f);
  float cameraX = std::floor(rawX);
  float cameraY = std::floor(rawY);

  m_tileRenderer->prefetchChunks(*m_currentWorld, mp_renderer, cameraX, cameraY,
                                  viewWidth, viewHeight);
}

bool WorldManager::handleHarvestResource(int entityId, int targetX,
                                         int targetY) {
  if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld) {
    WORLD_MANAGER_ERROR("WorldManager not initialized or no active world");
    return false;
  }

  // Suppress unused parameter warning since this is for future resource
  // tracking
  (void)entityId;

  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  if (!isValidPosition(targetX, targetY)) {
    WORLD_MANAGER_ERROR(
        std::format("Invalid harvest position: ({}, {})", targetX, targetY));
    return false;
  }

  HammerEngine::Tile &tile = m_currentWorld->grid[targetY][targetX];

  if (tile.obstacleType == HammerEngine::ObstacleType::NONE) {
    WORLD_MANAGER_WARN(std::format(
        "No harvestable resource at position: ({}, {})", targetX, targetY));
    return false;
  }

  // Store the original obstacle type for resource tracking
  const HammerEngine::ObstacleType harvestedType = tile.obstacleType;
  (void)harvestedType; // Suppress unused warning

  // Remove the obstacle
  tile.obstacleType = HammerEngine::ObstacleType::NONE;
  tile.resourceHandle = HammerEngine::ResourceHandle{};

  // Fire tile changed event
  fireTileChangedEvent(targetX, targetY, tile);

  // Notify WorldResourceManager about resource depletion
  // This is a placeholder - actual resource tracking would need proper resource
  // handles

  WORLD_MANAGER_INFO(std::format("Resource harvested at ({}, {}) by entity {}",
                                 targetX, targetY, entityId));
  return true;
}

bool WorldManager::updateTile(int x, int y, const HammerEngine::Tile &newTile) {
  if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld) {
    WORLD_MANAGER_ERROR("WorldManager not initialized or no active world");
    return false;
  }

  std::lock_guard<std::shared_mutex> lock(m_worldMutex);

  if (!isValidPosition(x, y)) {
    WORLD_MANAGER_ERROR(std::format("Invalid tile position: ({}, {})", x, y));
    return false;
  }

  const HammerEngine::Tile &oldTile = m_currentWorld->grid[y][x];

  // Skip invalidation entirely if tile data hasn't changed
  if (oldTile.biome == newTile.biome &&
      oldTile.obstacleType == newTile.obstacleType &&
      oldTile.decorationType == newTile.decorationType &&
      oldTile.buildingId == newTile.buildingId &&
      oldTile.buildingSize == newTile.buildingSize &&
      oldTile.isTopLeftOfBuilding == newTile.isTopLeftOfBuilding &&
      oldTile.isWater == newTile.isWater) {
    return true; // No visual change, skip chunk invalidation
  }

  // Check if change affects sprite overhang (obstacles/buildings have tall sprites)
  const bool hasOverhangChange =
      (oldTile.obstacleType != newTile.obstacleType ||
       oldTile.buildingId != newTile.buildingId ||
       oldTile.buildingSize != newTile.buildingSize ||
       oldTile.isTopLeftOfBuilding != newTile.isTopLeftOfBuilding);

  m_currentWorld->grid[y][x] = newTile;

  // Invalidate chunk containing this tile AND adjacent chunks only if sprite
  // overhang is affected. Sprites can extend up to 2 tiles into neighbors.
  if (m_tileRenderer) {
    constexpr int chunkSize = 16;    // TileRenderer::CHUNK_SIZE
    constexpr int overhangTiles = 2; // SPRITE_OVERHANG (64) / TILE_SIZE (32)

    const int chunkX = x / chunkSize;
    const int chunkY = y / chunkSize;

    // Primary chunk always invalidated when tile changes
    m_tileRenderer->invalidateChunk(chunkX, chunkY);

    // Neighbors only invalidated for sprite overhang changes (obstacles/buildings)
    if (hasOverhangChange) {
      const int localX = x % chunkSize;
      const int localY = y % chunkSize;

      const bool nearLeft = localX < overhangTiles;
      const bool nearRight = localX >= (chunkSize - overhangTiles);
      const bool nearTop = localY < overhangTiles;
      const bool nearBottom = localY >= (chunkSize - overhangTiles);

      if (nearLeft) {
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY);
      }
      if (nearRight) {
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY);
      }
      if (nearTop) {
        m_tileRenderer->invalidateChunk(chunkX, chunkY - 1);
      }
      if (nearBottom) {
        m_tileRenderer->invalidateChunk(chunkX, chunkY + 1);
      }

      // Diagonal neighbors only if near both edges
      if (nearLeft && nearTop) {
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY - 1);
      }
      if (nearRight && nearTop) {
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY - 1);
      }
      if (nearLeft && nearBottom) {
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY + 1);
      }
      if (nearRight && nearBottom) {
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY + 1);
      }
    }
  }

  fireTileChangedEvent(x, y, newTile);

  return true;
}

void WorldManager::fireTileChangedEvent(int x, int y,
                                        const HammerEngine::Tile &tile) {
  // Increment world version for change tracking by other systems
  // (PathfinderManager, etc.)
  m_worldVersion.fetch_add(1, std::memory_order_release);

  try {
    // Use tile information to determine change type based on tile properties
    std::string changeType = "tile_modified";
    if (tile.isWater) {
      changeType = "water_tile_changed";
    } else if (tile.biome == HammerEngine::Biome::FOREST) {
      changeType = "forest_tile_changed";
    } else if (tile.biome == HammerEngine::Biome::MOUNTAIN) {
      changeType = "mountain_tile_changed";
    }

    // Trigger world tile changed through EventManager (no registration)
    const EventManager &eventMgr = EventManager::Instance();
    (void)eventMgr.triggerTileChanged(x, y, changeType,
                                      EventManager::DispatchMode::Deferred);

    WORLD_MANAGER_DEBUG(
        std::format("TileChangedEvent fired for tile at ({}, {})", x, y));
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("Failed to fire TileChangedEvent: {}", ex.what()));
  }
}

void WorldManager::fireWorldLoadedEvent(const std::string &worldId) {
  // Increment world version when world is loaded (major change for other
  // systems)
  m_worldVersion.fetch_add(1, std::memory_order_release);

  try {
    // Get world dimensions
    int width, height;
    if (!getWorldDimensions(width, height)) {
      width = 0;
      height = 0;
    }

    // Trigger a world loaded event via EventManager (no registration)
    const EventManager &eventMgr = EventManager::Instance();
    (void)eventMgr.triggerWorldLoaded(worldId, width, height,
                                      EventManager::DispatchMode::Deferred);

    WORLD_MANAGER_INFO(std::format(
        "WorldLoadedEvent registered and executed for world: {} ({}x{})",
        worldId, width, height));
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("Failed to fire WorldLoadedEvent: {}", ex.what()));
  }
}

void WorldManager::fireWorldUnloadedEvent(const std::string &worldId) {
  try {
    // Trigger world unloaded via EventManager (no registration)
    const EventManager &eventMgr = EventManager::Instance();
    (void)eventMgr.triggerWorldUnloaded(worldId,
                                        EventManager::DispatchMode::Deferred);

    WORLD_MANAGER_INFO(
        std::format("WorldUnloadedEvent fired for world: {}", worldId));
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("Failed to fire WorldUnloadedEvent: {}", ex.what()));
  }
}

void WorldManager::registerEventHandlers() {
  try {
    EventManager &eventMgr = EventManager::Instance();
    m_handlerTokens.clear();

    // Register handler for world events (to respond to events from other
    // systems)
    m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
        EventTypeId::World, [](const EventData &data) {
          if (data.isActive() && data.event) {
            // Handle world-related events from other systems
            WORLD_MANAGER_DEBUG(
                std::format("WorldManager received world event: {}",
                            data.event->getName()));
          }
        }));

    // Register handler for camera events (world bounds may affect camera)
    m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
        EventTypeId::Camera, [](const EventData &data) {
          if (data.isActive() && data.event) {
            // Handle camera events that may require world data updates
            WORLD_MANAGER_DEBUG(
                std::format("WorldManager received camera event: {}",
                            data.event->getName()));
          }
        }));

    // Register handler for resource change events (resource changes may affect
    // world state)
    m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
        EventTypeId::ResourceChange, [](const EventData &data) {
          if (data.isActive() && data.event) {
            // Handle resource changes that may affect world generation or state
            auto resourceEvent =
                std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
            WORLD_MANAGER_DEBUG_IF(
                resourceEvent,
                std::format(
                    "WorldManager received resource change: {} changed by {}",
                    resourceEvent
                        ? resourceEvent->getResourceHandle().toString()
                        : "",
                    resourceEvent ? resourceEvent->getQuantityChange() : 0));
          }
        }));

    // Register handler for harvest resource events
    m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(
        EventTypeId::Harvest, [this](const EventData &data) {
          if (data.isActive() && data.event) {
            auto harvestEvent =
                std::dynamic_pointer_cast<HarvestResourceEvent>(data.event);
            if (harvestEvent) {
              WORLD_MANAGER_DEBUG(std::format(
                  "WorldManager received harvest request from entity {} at "
                  "({}, {})",
                  harvestEvent->getEntityId(), harvestEvent->getTargetX(),
                  harvestEvent->getTargetY()));

              // Handle the harvest request
              handleHarvestResource(harvestEvent->getEntityId(),
                                    harvestEvent->getTargetX(),
                                    harvestEvent->getTargetY());
            }
          }
        }));

    WORLD_MANAGER_DEBUG("WorldManager event handlers registered");
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("Failed to register event handlers: {}", ex.what()));
  }
}

void WorldManager::unregisterEventHandlers() {
  try {
    auto &eventMgr = EventManager::Instance();
    for (const auto &tok : m_handlerTokens) {
      (void)eventMgr.removeHandler(tok);
    }
    m_handlerTokens.clear();
    WORLD_MANAGER_DEBUG(
        "WorldManager event handlers unregistered (tokens cleared)");
  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(
        std::format("Failed to unregister event handlers: {}", ex.what()));
  }
}

bool WorldManager::getWorldDimensions(int &width, int &height) const {
  std::shared_lock<std::shared_mutex> lock(m_worldMutex);

  if (!m_currentWorld || m_currentWorld->grid.empty()) {
    width = 0;
    height = 0;
    return false;
  }

  height = static_cast<int>(m_currentWorld->grid.size());
  width = height > 0 ? static_cast<int>(m_currentWorld->grid[0].size()) : 0;

  return width > 0 && height > 0;
}

bool WorldManager::getWorldBounds(float &minX, float &minY, float &maxX,
                                  float &maxY) const {
  int width, height;
  if (!getWorldDimensions(width, height)) {
    // Set default bounds if no world is loaded
    minX = 0.0f;
    minY = 0.0f;
    maxX = 1000.0f;
    maxY = 1000.0f;
    return false;
  }

  // World bounds in pixel coordinates - convert from tile count to pixels
  minX = 0.0f;
  minY = 0.0f;
  maxX = static_cast<float>(width) *
         HammerEngine::TILE_SIZE; // Convert tiles to pixels
  maxY = static_cast<float>(height) *
         HammerEngine::TILE_SIZE; // Convert tiles to pixels

  return true;
}

void WorldManager::initializeWorldResources() {
  if (!m_currentWorld || m_currentWorld->grid.empty()) {
    WORLD_MANAGER_WARN("Cannot initialize resources - no world loaded");
    return;
  }

  WORLD_MANAGER_INFO(std::format("Initializing world resources for world: {}",
                                 m_currentWorld->worldId));

  // Get ResourceTemplateManager to access available resources
  const auto &resourceMgr = ResourceTemplateManager::Instance();

  // Count resources to distribute based on biomes and elevation
  int totalTiles = 0;
  int forestTiles = 0;
  int mountainTiles = 0;
  int swampTiles = 0;
  int celestialTiles = 0;
  int highElevationTiles = 0;

  // First pass: count tile types
  for (const auto &row : m_currentWorld->grid) {
    for (const auto &tile : row) {
      if (!tile.isWater) {
        totalTiles++;

        switch (tile.biome) {
        case HammerEngine::Biome::FOREST:
          forestTiles++;
          break;
        case HammerEngine::Biome::MOUNTAIN:
          mountainTiles++;
          break;
        case HammerEngine::Biome::SWAMP:
          swampTiles++;
          break;
        case HammerEngine::Biome::CELESTIAL:
          celestialTiles++;
          break;
        default:
          // Desert, Ocean, and Haunted biomes don't affect base resource
          // calculations
          break;
        }

        if (tile.elevation > 0.7f) {
          highElevationTiles++;
        }
      }
    }
  }

  if (totalTiles == 0) {
    WORLD_MANAGER_WARN("No land tiles found for resource initialization");
    return;
  }

  try {
    auto& edm = EntityDataManager::Instance();
    const std::string& worldId = m_currentWorld->worldId;

    // Helper lambda to spawn harvestables at appropriate tile positions
    auto spawnHarvestablesInBiome = [&](HammerEngine::ResourceHandle handle,
                                        HammerEngine::Biome targetBiome,
                                        int count, int yieldMin, int yieldMax,
                                        float respawnTime) {
      if (!handle.isValid() || count <= 0) return;

      int spawned = 0;
      const size_t gridHeight = m_currentWorld->grid.size();
      if (gridHeight == 0) return;
      const size_t gridWidth = m_currentWorld->grid[0].size();

      // Distribute harvestables across the world
      for (size_t y = 0; y < gridHeight && spawned < count; ++y) {
        for (size_t x = 0; x < gridWidth && spawned < count; ++x) {
          const auto& tile = m_currentWorld->grid[y][x];
          if (tile.isWater) continue;
          if (tile.biome != targetBiome) continue;

          // Skip some tiles for natural distribution (every ~10 tiles)
          if ((x + y * 7) % 10 != 0) continue;

          Vector2D pos(static_cast<float>(x) * HammerEngine::TILE_SIZE + HammerEngine::TILE_SIZE * 0.5f,
                       static_cast<float>(y) * HammerEngine::TILE_SIZE + HammerEngine::TILE_SIZE * 0.5f);

          // createHarvestable auto-registers with WRM using worldId
          EntityHandle h = edm.createHarvestable(pos, handle, yieldMin, yieldMax, respawnTime, worldId);
          if (h.isValid()) {
            ++spawned;
          }
        }
      }
      WORLD_MANAGER_DEBUG(std::format("Spawned {} harvestables of type {} in {}",
                                      spawned, handle.toString(), worldId));
    };

    // Helper for high-elevation resources
    auto spawnHarvestablesAtElevation = [&](HammerEngine::ResourceHandle handle,
                                            float minElevation, int count,
                                            int yieldMin, int yieldMax,
                                            float respawnTime) {
      if (!handle.isValid() || count <= 0) return;

      int spawned = 0;
      const size_t gridHeight = m_currentWorld->grid.size();
      if (gridHeight == 0) return;
      const size_t gridWidth = m_currentWorld->grid[0].size();

      for (size_t y = 0; y < gridHeight && spawned < count; ++y) {
        for (size_t x = 0; x < gridWidth && spawned < count; ++x) {
          const auto& tile = m_currentWorld->grid[y][x];
          if (tile.isWater || tile.elevation < minElevation) continue;

          // Skip some tiles for natural distribution
          if ((x + y * 11) % 12 != 0) continue;

          Vector2D pos(static_cast<float>(x) * HammerEngine::TILE_SIZE + HammerEngine::TILE_SIZE * 0.5f,
                       static_cast<float>(y) * HammerEngine::TILE_SIZE + HammerEngine::TILE_SIZE * 0.5f);

          // createHarvestable auto-registers with WRM using worldId
          EntityHandle h = edm.createHarvestable(pos, handle, yieldMin, yieldMax, respawnTime, worldId);
          if (h.isValid()) {
            ++spawned;
          }
        }
      }
      WORLD_MANAGER_DEBUG(std::format("Spawned {} high-elevation harvestables of type {}",
                                      spawned, handle.toString()));
    };

    // Calculate target harvestable counts based on tile counts
    const int baseCount = std::max(5, totalTiles / 100);

    // Basic resources - spawn as harvestables in appropriate biomes
    auto woodHandle = resourceMgr.getHandleById("wood");
    spawnHarvestablesInBiome(woodHandle, HammerEngine::Biome::FOREST,
                             baseCount + forestTiles / 20, 1, 3, 60.0f);

    auto ironHandle = resourceMgr.getHandleById("iron_ore");
    spawnHarvestablesInBiome(ironHandle, HammerEngine::Biome::MOUNTAIN,
                             baseCount + mountainTiles / 25, 1, 2, 90.0f);

    // Gold ore - rarer than iron, found in mountains
    auto goldHandle = resourceMgr.getHandleById("gold_ore");
    spawnHarvestablesInBiome(goldHandle, HammerEngine::Biome::MOUNTAIN,
                             std::max(1, mountainTiles / 40), 1, 2, 120.0f);

    // Rare resources
    if (mountainTiles > 0) {
      auto mithrilHandle = resourceMgr.getHandleById("mithril_ore");
      spawnHarvestablesInBiome(mithrilHandle, HammerEngine::Biome::MOUNTAIN,
                               std::max(1, mountainTiles / 50), 1, 1, 180.0f);
    }

    if (forestTiles > 0) {
      auto enchantedWoodHandle = resourceMgr.getHandleById("enchanted_wood");
      spawnHarvestablesInBiome(enchantedWoodHandle, HammerEngine::Biome::FOREST,
                               std::max(1, forestTiles / 40), 1, 2, 120.0f);
    }

    if (celestialTiles > 0) {
      auto crystalHandle = resourceMgr.getHandleById("crystal_essence");
      spawnHarvestablesInBiome(crystalHandle, HammerEngine::Biome::CELESTIAL,
                               std::max(1, celestialTiles / 30), 1, 2, 150.0f);
    }

    if (swampTiles > 0) {
      auto voidSilkHandle = resourceMgr.getHandleById("void_silk");
      spawnHarvestablesInBiome(voidSilkHandle, HammerEngine::Biome::SWAMP,
                               std::max(1, swampTiles / 60), 1, 1, 200.0f);
    }

    // High elevation resources
    if (highElevationTiles > 0) {
      auto stoneHandle = resourceMgr.getHandleById("enchanted_stone");
      spawnHarvestablesAtElevation(stoneHandle, 0.7f,
                                   std::max(1, highElevationTiles / 30),
                                   1, 3, 90.0f);
    }

    WORLD_MANAGER_INFO(std::format(
        "World harvestable initialization completed for {} ({} tiles processed)",
        m_currentWorld->worldId, totalTiles));

  } catch (const std::exception &ex) {
    WORLD_MANAGER_ERROR(std::format(
        "Error during world resource initialization: {}", ex.what()));
  }
}

// ============================================================================
// Chunk Cache Management (WorldManager delegates to TileRenderer)
// ============================================================================

void WorldManager::invalidateChunk(int chunkX, int chunkY) {
  if (m_tileRenderer) {
    m_tileRenderer->invalidateChunk(chunkX, chunkY);
  }
}

void WorldManager::clearChunkCache() {
  if (m_tileRenderer) {
    m_tileRenderer->clearChunkCache();
  }
}

// ============================================================================
// Season Management (WorldManager delegates to TileRenderer)
// ============================================================================

void WorldManager::subscribeToSeasonEvents() {
  if (m_tileRenderer) {
    m_tileRenderer->subscribeToSeasonEvents();
  }
}

void WorldManager::unsubscribeFromSeasonEvents() {
  if (m_tileRenderer) {
    m_tileRenderer->unsubscribeFromSeasonEvents();
  }
}

Season WorldManager::getCurrentSeason() const {
  if (m_tileRenderer) {
    return m_tileRenderer->getCurrentSeason();
  }
  return Season::Spring; // Default if no renderer
}

void WorldManager::setCurrentSeason(Season season) {
  if (m_tileRenderer) {
    m_tileRenderer->setCurrentSeason(season);
  }
}

// TileRenderer Implementation

HammerEngine::TileRenderer::TileRenderer()
    : m_currentSeason(Season::Spring), m_subscribedToSeasons(false) {
  // Pre-allocate buffers to avoid per-frame reallocations
  m_ySortBuffer.reserve(512);
  m_visibleChunks.reserve(64);   // Typical viewport ~24 chunks

  // Load world object definitions from JSON
  loadWorldObjects();

  // Get atlas pointer and pre-load source rect coords from JSON
  initAtlasCoords();

  // Initialize cached texture pointers/coords for current season
  updateCachedTextureIDs();
}

void HammerEngine::TileRenderer::loadWorldObjects() {
  JsonReader reader;
  if (!reader.loadFromFile("res/data/world_objects.json")) {
    WORLD_MANAGER_WARN(std::format(
        "Could not load world_objects.json: {} - using hardcoded defaults",
        reader.getLastError()));
    return;
  }

  const auto& root = reader.getRoot();
  if (!root.isObject()) {
    WORLD_MANAGER_WARN("world_objects.json root is not an object");
    return;
  }

  // Parse version
  if (root.hasKey("version") && root["version"].isString()) {
    m_worldObjects.version = root["version"].asString();
  }

  // Helper to parse a single object definition from a key-value pair
  auto parseObjectDef = [](const std::string& id, const JsonValue& obj) -> WorldObjectDef {
    WorldObjectDef def;
    def.id = id;
    if (obj.hasKey("name") && obj["name"].isString()) {
      def.name = obj["name"].asString();
    }
    if (obj.hasKey("textureId") && obj["textureId"].isString()) {
      def.textureId = obj["textureId"].asString();
    }
    if (obj.hasKey("seasonal") && obj["seasonal"].isBool()) {
      def.seasonal = obj["seasonal"].asBool();
    }
    if (obj.hasKey("blocking") && obj["blocking"].isBool()) {
      def.blocking = obj["blocking"].asBool();
    }
    if (obj.hasKey("harvestable") && obj["harvestable"].isBool()) {
      def.harvestable = obj["harvestable"].asBool();
    }
    if (obj.hasKey("buildingSize") && obj["buildingSize"].isNumber()) {
      def.buildingSize = obj["buildingSize"].asInt();
    }
    return def;
  };

  // Helper to parse a category (object format: { "key": { ... }, ... })
  auto parseCategory = [&parseObjectDef](const JsonValue& root, const std::string& category,
                                          std::unordered_map<std::string, WorldObjectDef>& target) {
    if (!root.hasKey(category) || !root[category].isObject()) {
      return;
    }
    const auto& catObj = root[category].asObject();
    for (const auto& [key, value] : catObj) {
      if (value.isObject()) {
        target[key] = parseObjectDef(key, value);
      }
    }
  };

  // Parse all categories (object format for tool compatibility)
  parseCategory(root, "biomes", m_worldObjects.biomes);
  parseCategory(root, "obstacles", m_worldObjects.obstacles);
  parseCategory(root, "decorations", m_worldObjects.decorations);
  parseCategory(root, "buildings", m_worldObjects.buildings);

  m_worldObjects.loaded = true;
  WORLD_MANAGER_INFO(std::format(
      "Loaded world_objects.json v{}: {} biomes, {} obstacles, {} decorations, {} buildings",
      m_worldObjects.version,
      m_worldObjects.biomes.size(),
      m_worldObjects.obstacles.size(),
      m_worldObjects.decorations.size(),
      m_worldObjects.buildings.size()));
}

void HammerEngine::TileRenderer::updateCachedTextureIDs() {
  // If using atlas, just apply pre-loaded coords (no lookups)
  if (m_useAtlas) {
    applyCoordsToTextures(m_currentSeason);
    buildLookupTables();  // Must build LUTs for render loop
    return;
  }

  // Fallback: individual textures via TextureManager
  static const char *const seasonPrefixes[] = {"spring_", "summer_", "fall_",
                                               "winter_"};
  const char *const prefix = seasonPrefixes[static_cast<int>(m_currentSeason)];

  // Helper to get texture ID from JSON or use default, with optional seasonal prefix
  auto getTextureId = [prefix](
      const std::unordered_map<std::string, WorldObjectDef>& map,
      const std::string& id,
      const std::string& defaultTexture) -> std::string {
    auto it = map.find(id);
    if (it != map.end() && !it->second.textureId.empty()) {
      if (it->second.seasonal) {
        return std::string(prefix) + it->second.textureId;
      }
      return it->second.textureId;
    }
    // Default: assume seasonal
    return std::string(prefix) + defaultTexture;
  };

  // Pre-compute all seasonal texture IDs once (eliminates ~24,000 heap
  // allocations/frame at 4K)
  // Use JSON data when loaded, otherwise fall back to hardcoded defaults
  if (m_worldObjects.loaded) {
    m_cachedTextureIDs.biome_default = getTextureId(m_worldObjects.biomes, "default", "biome_default");
    m_cachedTextureIDs.biome_desert = getTextureId(m_worldObjects.biomes, "desert", "biome_desert");
    m_cachedTextureIDs.biome_forest = getTextureId(m_worldObjects.biomes, "forest", "biome_forest");
    m_cachedTextureIDs.biome_plains = getTextureId(m_worldObjects.biomes, "plains", "biome_plains");
    m_cachedTextureIDs.biome_mountain = getTextureId(m_worldObjects.biomes, "mountain", "biome_mountain");
    m_cachedTextureIDs.biome_swamp = getTextureId(m_worldObjects.biomes, "swamp", "biome_swamp");
    m_cachedTextureIDs.biome_haunted = getTextureId(m_worldObjects.biomes, "haunted", "biome_haunted");
    m_cachedTextureIDs.biome_celestial = getTextureId(m_worldObjects.biomes, "celestial", "biome_celestial");
    m_cachedTextureIDs.biome_ocean = getTextureId(m_worldObjects.biomes, "ocean", "biome_ocean");
    m_cachedTextureIDs.obstacle_water = getTextureId(m_worldObjects.obstacles, "water", "obstacle_water");
    m_cachedTextureIDs.obstacle_tree = getTextureId(m_worldObjects.obstacles, "tree", "obstacle_tree");
    m_cachedTextureIDs.obstacle_rock = getTextureId(m_worldObjects.obstacles, "rock", "obstacle_rock");
    m_cachedTextureIDs.building_hut = getTextureId(m_worldObjects.buildings, "hut", "building_hut");
    m_cachedTextureIDs.building_house = getTextureId(m_worldObjects.buildings, "house", "building_house");
    m_cachedTextureIDs.building_large = getTextureId(m_worldObjects.buildings, "large", "building_large");
    m_cachedTextureIDs.building_cityhall = getTextureId(m_worldObjects.buildings, "cityhall", "building_cityhall");
    // Ore deposits (non-seasonal)
    m_cachedTextureIDs.obstacle_iron_deposit = getTextureId(m_worldObjects.obstacles, "iron_deposit", "ore_iron_deposit");
    m_cachedTextureIDs.obstacle_gold_deposit = getTextureId(m_worldObjects.obstacles, "gold_deposit", "ore_gold_deposit");
    m_cachedTextureIDs.obstacle_copper_deposit = getTextureId(m_worldObjects.obstacles, "copper_deposit", "ore_copper_deposit");
    m_cachedTextureIDs.obstacle_mithril_deposit = getTextureId(m_worldObjects.obstacles, "mithril_deposit", "ore_mithril_deposit");
    m_cachedTextureIDs.obstacle_limestone_deposit = getTextureId(m_worldObjects.obstacles, "limestone_deposit", "ore_limestone_deposit");
    m_cachedTextureIDs.obstacle_coal_deposit = getTextureId(m_worldObjects.obstacles, "coal_deposit", "ore_coal_deposit");
    // Gem deposits (non-seasonal)
    m_cachedTextureIDs.obstacle_emerald_deposit = getTextureId(m_worldObjects.obstacles, "emerald_deposit", "gem_emerald_deposit");
    m_cachedTextureIDs.obstacle_ruby_deposit = getTextureId(m_worldObjects.obstacles, "ruby_deposit", "gem_ruby_deposit");
    m_cachedTextureIDs.obstacle_sapphire_deposit = getTextureId(m_worldObjects.obstacles, "sapphire_deposit", "gem_sapphire_deposit");
    m_cachedTextureIDs.obstacle_diamond_deposit = getTextureId(m_worldObjects.obstacles, "diamond_deposit", "gem_diamond_deposit");
  } else {
    // Hardcoded defaults when JSON not loaded
    m_cachedTextureIDs.biome_default = std::string(prefix) + "biome_default";
    m_cachedTextureIDs.biome_desert = std::string(prefix) + "biome_desert";
    m_cachedTextureIDs.biome_forest = std::string(prefix) + "biome_forest";
    m_cachedTextureIDs.biome_plains = std::string(prefix) + "biome_plains";
    m_cachedTextureIDs.biome_mountain = std::string(prefix) + "biome_mountain";
    m_cachedTextureIDs.biome_swamp = std::string(prefix) + "biome_swamp";
    m_cachedTextureIDs.biome_haunted = std::string(prefix) + "biome_haunted";
    m_cachedTextureIDs.biome_celestial = std::string(prefix) + "biome_celestial";
    m_cachedTextureIDs.biome_ocean = std::string(prefix) + "biome_ocean";
    m_cachedTextureIDs.obstacle_water = std::string(prefix) + "obstacle_water";
    m_cachedTextureIDs.obstacle_tree = std::string(prefix) + "obstacle_tree";
    m_cachedTextureIDs.obstacle_rock = std::string(prefix) + "obstacle_rock";
    m_cachedTextureIDs.building_hut = std::string(prefix) + "building_hut";
    m_cachedTextureIDs.building_house = std::string(prefix) + "building_house";
    m_cachedTextureIDs.building_large = std::string(prefix) + "building_large";
    m_cachedTextureIDs.building_cityhall = std::string(prefix) + "building_cityhall";
    // Ore deposits (non-seasonal, no prefix)
    m_cachedTextureIDs.obstacle_iron_deposit = "ore_iron_deposit";
    m_cachedTextureIDs.obstacle_gold_deposit = "ore_gold_deposit";
    m_cachedTextureIDs.obstacle_copper_deposit = "ore_copper_deposit";
    m_cachedTextureIDs.obstacle_mithril_deposit = "ore_mithril_deposit";
    m_cachedTextureIDs.obstacle_limestone_deposit = "ore_limestone_deposit";
    m_cachedTextureIDs.obstacle_coal_deposit = "ore_coal_deposit";
    // Gem deposits (non-seasonal, no prefix)
    m_cachedTextureIDs.obstacle_emerald_deposit = "gem_emerald_deposit";
    m_cachedTextureIDs.obstacle_ruby_deposit = "gem_ruby_deposit";
    m_cachedTextureIDs.obstacle_sapphire_deposit = "gem_sapphire_deposit";
    m_cachedTextureIDs.obstacle_diamond_deposit = "gem_diamond_deposit";
  }

  // Cache raw texture pointers for direct rendering (eliminates ~8,000 hash
  // lookups/frame at 4K)
  auto &texMgr = TextureManager::Instance();
  auto getPtr = [&texMgr](const std::string &id) -> SDL_Texture * {
    auto tex = texMgr.getTexture(id);
    return tex ? tex.get() : nullptr;
  };

  // Cache texture pointers and dimensions (queried once here, not per-draw)
  auto cacheTexture = [&](CachedTexture &cached, const std::string &id) {
    cached.ptr = getPtr(id);
    if (cached.ptr)
      SDL_GetTextureSize(cached.ptr, &cached.w, &cached.h);
  };

  cacheTexture(m_cachedTextures.biome_default,
               m_cachedTextureIDs.biome_default);
  cacheTexture(m_cachedTextures.biome_desert, m_cachedTextureIDs.biome_desert);
  cacheTexture(m_cachedTextures.biome_forest, m_cachedTextureIDs.biome_forest);
  cacheTexture(m_cachedTextures.biome_plains, m_cachedTextureIDs.biome_plains);
  cacheTexture(m_cachedTextures.biome_mountain,
               m_cachedTextureIDs.biome_mountain);
  cacheTexture(m_cachedTextures.biome_swamp, m_cachedTextureIDs.biome_swamp);
  cacheTexture(m_cachedTextures.biome_haunted,
               m_cachedTextureIDs.biome_haunted);
  cacheTexture(m_cachedTextures.biome_celestial,
               m_cachedTextureIDs.biome_celestial);
  cacheTexture(m_cachedTextures.biome_ocean, m_cachedTextureIDs.biome_ocean);
  cacheTexture(m_cachedTextures.obstacle_water,
               m_cachedTextureIDs.obstacle_water);
  cacheTexture(m_cachedTextures.obstacle_tree,
               m_cachedTextureIDs.obstacle_tree);
  cacheTexture(m_cachedTextures.obstacle_rock,
               m_cachedTextureIDs.obstacle_rock);
  cacheTexture(m_cachedTextures.building_hut, m_cachedTextureIDs.building_hut);
  cacheTexture(m_cachedTextures.building_house,
               m_cachedTextureIDs.building_house);
  cacheTexture(m_cachedTextures.building_large,
               m_cachedTextureIDs.building_large);
  cacheTexture(m_cachedTextures.building_cityhall,
               m_cachedTextureIDs.building_cityhall);

  // Ore deposit textures
  cacheTexture(m_cachedTextures.obstacle_iron_deposit,
               m_cachedTextureIDs.obstacle_iron_deposit);
  cacheTexture(m_cachedTextures.obstacle_gold_deposit,
               m_cachedTextureIDs.obstacle_gold_deposit);
  cacheTexture(m_cachedTextures.obstacle_copper_deposit,
               m_cachedTextureIDs.obstacle_copper_deposit);
  cacheTexture(m_cachedTextures.obstacle_mithril_deposit,
               m_cachedTextureIDs.obstacle_mithril_deposit);
  cacheTexture(m_cachedTextures.obstacle_limestone_deposit,
               m_cachedTextureIDs.obstacle_limestone_deposit);
  cacheTexture(m_cachedTextures.obstacle_coal_deposit,
               m_cachedTextureIDs.obstacle_coal_deposit);
  // Gem deposit textures
  cacheTexture(m_cachedTextures.obstacle_emerald_deposit,
               m_cachedTextureIDs.obstacle_emerald_deposit);
  cacheTexture(m_cachedTextures.obstacle_ruby_deposit,
               m_cachedTextureIDs.obstacle_ruby_deposit);
  cacheTexture(m_cachedTextures.obstacle_sapphire_deposit,
               m_cachedTextureIDs.obstacle_sapphire_deposit);
  cacheTexture(m_cachedTextures.obstacle_diamond_deposit,
               m_cachedTextureIDs.obstacle_diamond_deposit);

  // Decoration textures - handle seasonal variants
  // Flowers only appear in Spring/Summer (empty string = won't render)
  const bool hasFlowers =
      (m_currentSeason == Season::Spring || m_currentSeason == Season::Summer);
  m_cachedTextureIDs.decoration_flower_blue = hasFlowers ? "flower_blue" : "";
  m_cachedTextureIDs.decoration_flower_pink = hasFlowers ? "flower_pink" : "";
  m_cachedTextureIDs.decoration_flower_white = hasFlowers ? "flower_white" : "";
  m_cachedTextureIDs.decoration_flower_yellow =
      hasFlowers ? "flower_yellow" : "";

  // Mushrooms - no seasonal variants
  m_cachedTextureIDs.decoration_mushroom_purple = "mushroom_purple";
  m_cachedTextureIDs.decoration_mushroom_tan = "mushroom_tan";

  // Grass - seasonal variants (use dead grass in fall/winter)
  if (m_currentSeason == Season::Spring) {
    m_cachedTextureIDs.decoration_grass_small = "spring_obstacle_grass";
    m_cachedTextureIDs.decoration_grass_large = "spring_obstacle_grass";
  } else if (m_currentSeason == Season::Summer) {
    m_cachedTextureIDs.decoration_grass_small = "summer_obstacle_grass";
    m_cachedTextureIDs.decoration_grass_large = "summer_obstacle_grass";
  } else {
    m_cachedTextureIDs.decoration_grass_small = "dead_grass_obstacle_small";
    m_cachedTextureIDs.decoration_grass_large = "dead_grass_obstacle_large";
  }

  // Bushes - seasonal variants (prefix pattern: season_bush)
  if (m_currentSeason == Season::Fall) {
    m_cachedTextureIDs.decoration_bush = "fall_bush";
  } else if (m_currentSeason == Season::Winter) {
    m_cachedTextureIDs.decoration_bush = "winter_bush";
  } else if (m_currentSeason == Season::Spring) {
    m_cachedTextureIDs.decoration_bush = "spring_bush";
  } else {
    m_cachedTextureIDs.decoration_bush = "summer_bush";
  }

  // Stumps and rocks - no seasonal variants
  m_cachedTextureIDs.decoration_stump_small = "stump_obstacle_small";
  m_cachedTextureIDs.decoration_stump_medium = "stump_obstacle_medium";
  m_cachedTextureIDs.decoration_rock_small = "obstacle_rock";

  // Logs - no seasonal variants
  m_cachedTextureIDs.decoration_dead_log_hz = "dead_log_hz";
  m_cachedTextureIDs.decoration_dead_log_vertical = "dead_log_vertical";

  // Water decorations - no seasonal variants
  m_cachedTextureIDs.decoration_lily_pad = "small_lily_pad";
  m_cachedTextureIDs.decoration_water_flower = "blue_water_flower";

  // Cache decoration texture pointers
  cacheTexture(m_cachedTextures.decoration_flower_blue,
               m_cachedTextureIDs.decoration_flower_blue);
  cacheTexture(m_cachedTextures.decoration_flower_pink,
               m_cachedTextureIDs.decoration_flower_pink);
  cacheTexture(m_cachedTextures.decoration_flower_white,
               m_cachedTextureIDs.decoration_flower_white);
  cacheTexture(m_cachedTextures.decoration_flower_yellow,
               m_cachedTextureIDs.decoration_flower_yellow);
  cacheTexture(m_cachedTextures.decoration_mushroom_purple,
               m_cachedTextureIDs.decoration_mushroom_purple);
  cacheTexture(m_cachedTextures.decoration_mushroom_tan,
               m_cachedTextureIDs.decoration_mushroom_tan);
  cacheTexture(m_cachedTextures.decoration_grass_small,
               m_cachedTextureIDs.decoration_grass_small);
  cacheTexture(m_cachedTextures.decoration_grass_large,
               m_cachedTextureIDs.decoration_grass_large);
  cacheTexture(m_cachedTextures.decoration_bush,
               m_cachedTextureIDs.decoration_bush);
  cacheTexture(m_cachedTextures.decoration_stump_small,
               m_cachedTextureIDs.decoration_stump_small);
  cacheTexture(m_cachedTextures.decoration_stump_medium,
               m_cachedTextureIDs.decoration_stump_medium);
  cacheTexture(m_cachedTextures.decoration_rock_small,
               m_cachedTextureIDs.decoration_rock_small);
  cacheTexture(m_cachedTextures.decoration_dead_log_hz,
               m_cachedTextureIDs.decoration_dead_log_hz);
  cacheTexture(m_cachedTextures.decoration_dead_log_vertical,
               m_cachedTextureIDs.decoration_dead_log_vertical);
  cacheTexture(m_cachedTextures.decoration_lily_pad,
               m_cachedTextureIDs.decoration_lily_pad);
  cacheTexture(m_cachedTextures.decoration_water_flower,
               m_cachedTextureIDs.decoration_water_flower);

  // Validate critical textures to catch missing seasonal assets early
  if (!m_cachedTextures.biome_default.ptr) {
    WORLD_MANAGER_ERROR(
        std::format("TileRenderer: Missing biome_default texture for season {}",
                    static_cast<int>(m_currentSeason)));
  }
  if (!m_cachedTextures.obstacle_water.ptr) {
    WORLD_MANAGER_ERROR(std::format(
        "TileRenderer: Missing obstacle_water texture for season {}",
        static_cast<int>(m_currentSeason)));
  }

  // Build O(1) lookup tables for render loop
  buildLookupTables();
}

void HammerEngine::TileRenderer::buildLookupTables() {
  // Biome lookup table (indexed by Biome enum: DESERT=0...OCEAN=7)
  m_biomeLUT[0] = &m_cachedTextures.biome_desert;    // DESERT
  m_biomeLUT[1] = &m_cachedTextures.biome_forest;    // FOREST
  m_biomeLUT[2] = &m_cachedTextures.biome_plains;    // PLAINS
  m_biomeLUT[3] = &m_cachedTextures.biome_mountain;  // MOUNTAIN
  m_biomeLUT[4] = &m_cachedTextures.biome_swamp;     // SWAMP
  m_biomeLUT[5] = &m_cachedTextures.biome_haunted;   // HAUNTED
  m_biomeLUT[6] = &m_cachedTextures.biome_celestial; // CELESTIAL
  m_biomeLUT[7] = &m_cachedTextures.biome_ocean;     // OCEAN

  // Decoration lookup table (indexed by DecorationType enum: NONE=0...WATER_FLOWER=16)
  m_decorationLUT[0] = nullptr;  // NONE
  m_decorationLUT[1] = &m_cachedTextures.decoration_flower_blue;
  m_decorationLUT[2] = &m_cachedTextures.decoration_flower_pink;
  m_decorationLUT[3] = &m_cachedTextures.decoration_flower_white;
  m_decorationLUT[4] = &m_cachedTextures.decoration_flower_yellow;
  m_decorationLUT[5] = &m_cachedTextures.decoration_mushroom_purple;
  m_decorationLUT[6] = &m_cachedTextures.decoration_mushroom_tan;
  m_decorationLUT[7] = &m_cachedTextures.decoration_grass_small;
  m_decorationLUT[8] = &m_cachedTextures.decoration_grass_large;
  m_decorationLUT[9] = &m_cachedTextures.decoration_bush;
  m_decorationLUT[10] = &m_cachedTextures.decoration_stump_small;
  m_decorationLUT[11] = &m_cachedTextures.decoration_stump_medium;
  m_decorationLUT[12] = &m_cachedTextures.decoration_rock_small;
  m_decorationLUT[13] = &m_cachedTextures.decoration_dead_log_hz;
  m_decorationLUT[14] = &m_cachedTextures.decoration_dead_log_vertical;
  m_decorationLUT[15] = &m_cachedTextures.decoration_lily_pad;
  m_decorationLUT[16] = &m_cachedTextures.decoration_water_flower;

  // Obstacle lookup table (indexed by ObstacleType enum: NONE=0...DIAMOND_DEPOSIT=14)
  m_obstacleLUT[0] = nullptr;  // NONE
  m_obstacleLUT[1] = &m_cachedTextures.obstacle_rock;           // ROCK
  m_obstacleLUT[2] = &m_cachedTextures.obstacle_tree;           // TREE
  m_obstacleLUT[3] = &m_cachedTextures.obstacle_water;          // WATER
  m_obstacleLUT[4] = nullptr;  // BUILDING (handled separately)
  m_obstacleLUT[5] = &m_cachedTextures.obstacle_iron_deposit;
  m_obstacleLUT[6] = &m_cachedTextures.obstacle_gold_deposit;
  m_obstacleLUT[7] = &m_cachedTextures.obstacle_copper_deposit;
  m_obstacleLUT[8] = &m_cachedTextures.obstacle_mithril_deposit;
  m_obstacleLUT[9] = &m_cachedTextures.obstacle_limestone_deposit;
  m_obstacleLUT[10] = &m_cachedTextures.obstacle_coal_deposit;
  m_obstacleLUT[11] = &m_cachedTextures.obstacle_emerald_deposit;
  m_obstacleLUT[12] = &m_cachedTextures.obstacle_ruby_deposit;
  m_obstacleLUT[13] = &m_cachedTextures.obstacle_sapphire_deposit;
  m_obstacleLUT[14] = &m_cachedTextures.obstacle_diamond_deposit;
}

void HammerEngine::TileRenderer::setCurrentSeason(Season season) {
  if (m_currentSeason != season) {
    m_currentSeason = season;
    updateCachedTextureIDs();
    clearChunkCache(); // Season change requires re-rendering all chunks with
                       // new textures
  }
}

void HammerEngine::TileRenderer::invalidateChunk(int chunkX, int chunkY) {
  if (!m_gridInitialized) return;
  if (chunkY >= 0 && chunkY < m_gridHeight &&
      chunkX >= 0 && chunkX < m_gridWidth) {
    m_chunkGrid[chunkY][chunkX].dirty = true;
    m_hasDirtyChunks = true;
  }
}

void HammerEngine::TileRenderer::clearChunkCache() {
  // Mark all chunks as dirty for re-rendering (e.g., on season change)
  m_cachePendingClear.store(true, std::memory_order_release);
  m_hasDirtyChunks = true;
}

void HammerEngine::TileRenderer::initTexturePool(SDL_Renderer *renderer) {
  if (m_poolInitialized || !renderer) {
    return;
  }

  constexpr int chunkPixelSize =
      CHUNK_SIZE * static_cast<int>(TILE_SIZE) + SPRITE_OVERHANG * 2;

  m_texturePool.reserve(TEXTURE_POOL_SIZE);
  for (size_t i = 0; i < TEXTURE_POOL_SIZE; ++i) {
    SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                         SDL_TEXTUREACCESS_TARGET,
                                         chunkPixelSize, chunkPixelSize);
    if (tex) {
      SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
      m_texturePool.push_back(
          std::shared_ptr<SDL_Texture>(tex, SDL_DestroyTexture));
    }
  }
  m_poolInitialized = true;
  WORLD_MANAGER_INFO(
      std::format("Texture pool initialized with {} textures", m_texturePool.size()));
}

std::shared_ptr<SDL_Texture>
HammerEngine::TileRenderer::acquireTexture(SDL_Renderer *renderer) {
  // Try to get from pool first
  if (!m_texturePool.empty()) {
    auto tex = std::move(m_texturePool.back());
    m_texturePool.pop_back();
    return tex;
  }

  // Pool empty - create new texture (should be rare after warmup)
  constexpr int chunkPixelSize =
      CHUNK_SIZE * static_cast<int>(TILE_SIZE) + SPRITE_OVERHANG * 2;
  SDL_Texture *tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET,
                                       chunkPixelSize, chunkPixelSize);
  if (!tex) {
    return nullptr;
  }
  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  return std::shared_ptr<SDL_Texture>(tex, SDL_DestroyTexture);
}

void HammerEngine::TileRenderer::releaseTexture(std::shared_ptr<SDL_Texture> tex) {
  if (tex && m_texturePool.size() < TEXTURE_POOL_SIZE) {
    m_texturePool.push_back(std::move(tex));
  }
  // If pool is full, texture is destroyed via shared_ptr destructor
}

HammerEngine::TileRenderer::~TileRenderer() { unsubscribeFromSeasonEvents(); }

void HammerEngine::TileRenderer::initAtlasCoords() {
  // Get atlas texture pointer from TextureManager (already loaded)
  // For GPU rendering, SDL_Texture may not exist - check GPU texture instead
  auto atlasTex = TextureManager::Instance().getTexture("atlas");
  if (atlasTex) {
    m_atlasPtr = atlasTex.get();
  }
#ifdef USE_SDL3_GPU
  else {
    // Check if GPU texture is available (GPU rendering path)
    auto* gpuTex = TextureManager::Instance().getGPUTexture("atlas");
    if (gpuTex) {
      m_atlasGPUPtr = gpuTex;
      WORLD_MANAGER_INFO("Using GPU atlas texture for rendering");
    } else {
      WORLD_MANAGER_INFO("Atlas texture not found - using individual textures");
      m_useAtlas = false;
      return;
    }
  }
#else
  if (!atlasTex) {
    WORLD_MANAGER_INFO("Atlas texture not found - using individual textures");
    m_useAtlas = false;
    return;
  }
#endif

  // Load atlas.json for source rect coordinates
  JsonReader atlasReader;
  if (!atlasReader.loadFromFile("res/data/atlas.json")) {
    WORLD_MANAGER_WARN("Could not load atlas.json - using individual textures");
    m_useAtlas = false;
    return;
  }

  const auto& atlasRoot = atlasReader.getRoot();
  if (!atlasRoot.isObject() || !atlasRoot.hasKey("regions")) {
    WORLD_MANAGER_WARN("Invalid atlas.json format - using individual textures");
    m_useAtlas = false;
    return;
  }

  const auto& regions = atlasRoot["regions"].asObject();

  // Load world_objects.json to get texture IDs for each object type
  JsonReader worldReader;
  if (!worldReader.loadFromFile("res/data/world_objects.json")) {
    WORLD_MANAGER_WARN("Could not load world_objects.json - using individual textures");
    m_useAtlas = false;
    return;
  }

  const auto& worldRoot = worldReader.getRoot();

  // Helper to get coords from atlas regions
  auto getCoords = [&regions](const std::string& id) -> AtlasCoords {
    auto it = regions.find(id);
    if (it == regions.end()) {
      return {0, 0, 0, 0};
    }
    const auto& r = it->second;
    return {
      static_cast<float>(r["x"].asNumber()),
      static_cast<float>(r["y"].asNumber()),
      static_cast<float>(r["w"].asNumber()),
      static_cast<float>(r["h"].asNumber())
    };
  };

  // Helper to get textureId from world_objects.json
  auto getTextureId = [&worldRoot](const std::string& category, const std::string& key) -> std::string {
    if (!worldRoot.hasKey(category)) return "";
    const auto& cat = worldRoot[category];
    if (!cat.isObject() || !cat.hasKey(key)) return "";
    const auto& obj = cat[key];
    if (!obj.hasKey("textureId")) return "";
    return obj["textureId"].asString();
  };

  // Helper to check if object is seasonal
  auto isSeasonal = [&worldRoot](const std::string& category, const std::string& key) -> bool {
    if (!worldRoot.hasKey(category)) return false;
    const auto& cat = worldRoot[category];
    if (!cat.isObject() || !cat.hasKey(key)) return false;
    const auto& obj = cat[key];
    if (!obj.hasKey("seasonal")) return false;
    return obj["seasonal"].asBool();
  };

  // Season prefixes
  static const char* seasonPrefixes[] = {"spring_", "summer_", "fall_", "winter_"};

  // Pre-load coords for all seasons
  for (int s = 0; s < 4; ++s) {
    const std::string prefix = seasonPrefixes[s];
    auto& coords = m_seasonalCoords[s];

    // Biomes - get textureId from JSON, apply seasonal prefix
    auto loadBiome = [&](AtlasCoords& target, const std::string& key) {
      std::string texId = getTextureId("biomes", key);
      if (texId.empty()) texId = "biome_" + key;  // Fallback
      target = isSeasonal("biomes", key) ? getCoords(prefix + texId) : getCoords(texId);
    };

    loadBiome(coords.biome_default, "default");
    loadBiome(coords.biome_desert, "desert");
    loadBiome(coords.biome_forest, "forest");
    loadBiome(coords.biome_plains, "plains");
    loadBiome(coords.biome_mountain, "mountain");
    loadBiome(coords.biome_swamp, "swamp");
    loadBiome(coords.biome_haunted, "haunted");
    loadBiome(coords.biome_celestial, "celestial");
    loadBiome(coords.biome_ocean, "ocean");

    // Obstacles - get textureId from JSON
    auto loadObstacle = [&](AtlasCoords& target, const std::string& key) {
      std::string texId = getTextureId("obstacles", key);
      if (texId.empty()) texId = "obstacle_" + key;
      target = isSeasonal("obstacles", key) ? getCoords(prefix + texId) : getCoords(texId);
    };

    loadObstacle(coords.obstacle_water, "water");
    loadObstacle(coords.obstacle_tree, "tree");
    loadObstacle(coords.obstacle_rock, "rock");

    // Buildings - get textureId from JSON
    auto loadBuilding = [&](AtlasCoords& target, const std::string& key) {
      std::string texId = getTextureId("buildings", key);
      if (texId.empty()) texId = "building_" + key;
      target = isSeasonal("buildings", key) ? getCoords(prefix + texId) : getCoords(texId);
    };

    loadBuilding(coords.building_hut, "hut");
    loadBuilding(coords.building_house, "house");
    loadBuilding(coords.building_large, "large");
    loadBuilding(coords.building_cityhall, "cityhall");

    // Ore deposits (non-seasonal)
    loadObstacle(coords.obstacle_iron_deposit, "iron_deposit");
    loadObstacle(coords.obstacle_gold_deposit, "gold_deposit");
    loadObstacle(coords.obstacle_copper_deposit, "copper_deposit");
    loadObstacle(coords.obstacle_mithril_deposit, "mithril_deposit");
    loadObstacle(coords.obstacle_limestone_deposit, "limestone_deposit");
    loadObstacle(coords.obstacle_coal_deposit, "coal_deposit");
    // Gem deposits (non-seasonal)
    loadObstacle(coords.obstacle_emerald_deposit, "emerald_deposit");
    loadObstacle(coords.obstacle_ruby_deposit, "ruby_deposit");
    loadObstacle(coords.obstacle_sapphire_deposit, "sapphire_deposit");
    loadObstacle(coords.obstacle_diamond_deposit, "diamond_deposit");

    // Decorations - special handling for seasonal availability
    auto loadDecoration = [&](AtlasCoords& target, const std::string& key) {
      if (!worldRoot.hasKey("decorations")) {
        target = {0, 0, 0, 0};
        return;
      }
      const auto& decorations = worldRoot["decorations"];
      if (!decorations.hasKey(key)) {
        target = {0, 0, 0, 0};
        return;
      }
      const auto& obj = decorations[key];

      // Check if this decoration has season restrictions
      if (obj.hasKey("seasons")) {
        const auto& seasons = obj["seasons"].asArray();
        bool availableThisSeason = false;
        for (const auto& seasonVal : seasons) {
          const std::string& seasonName = seasonVal.asString();
          if ((s == 0 && seasonName == "spring") ||
              (s == 1 && seasonName == "summer") ||
              (s == 2 && seasonName == "fall") ||
              (s == 3 && seasonName == "winter")) {
            availableThisSeason = true;
            break;
          }
        }
        if (!availableThisSeason) {
          target = {0, 0, 0, 0};
          return;
        }
      }

      std::string texId = obj.hasKey("textureId") ? obj["textureId"].asString() : key;

      // Check for fallback seasons
      if (obj.hasKey("fallbackSeasons")) {
        const auto& fallbacks = obj["fallbackSeasons"];
        const char* seasonNames[] = {"spring", "summer", "fall", "winter"};
        if (fallbacks.hasKey(seasonNames[s])) {
          texId = fallbacks[seasonNames[s]].asString();
          target = getCoords(texId);
          return;
        }
      }

      // Check for seasonal textures override
      if (obj.hasKey("seasonTextures")) {
        const auto& seasonTex = obj["seasonTextures"];
        const char* seasonNames[] = {"spring", "summer", "fall", "winter"};
        if (seasonTex.hasKey(seasonNames[s])) {
          texId = seasonTex[seasonNames[s]].asString();
          target = getCoords(texId);
          return;
        }
      }

      // Apply seasonal prefix if marked seasonal
      bool seasonal = obj.hasKey("seasonal") && obj["seasonal"].asBool();
      target = seasonal ? getCoords(prefix + texId) : getCoords(texId);
    };

    loadDecoration(coords.decoration_flower_blue, "flower_blue");
    loadDecoration(coords.decoration_flower_pink, "flower_pink");
    loadDecoration(coords.decoration_flower_white, "flower_white");
    loadDecoration(coords.decoration_flower_yellow, "flower_yellow");
    loadDecoration(coords.decoration_mushroom_purple, "mushroom_purple");
    loadDecoration(coords.decoration_mushroom_tan, "mushroom_tan");
    loadDecoration(coords.decoration_grass_small, "grass_small");
    loadDecoration(coords.decoration_grass_large, "grass_large");
    loadDecoration(coords.decoration_bush, "bush");
    loadDecoration(coords.decoration_stump_small, "stump_small");
    loadDecoration(coords.decoration_stump_medium, "stump_medium");
    loadDecoration(coords.decoration_rock_small, "rock_small");
    loadDecoration(coords.decoration_dead_log_hz, "dead_log_hz");
    loadDecoration(coords.decoration_dead_log_vertical, "dead_log_vertical");
    loadDecoration(coords.decoration_lily_pad, "lily_pad");
    loadDecoration(coords.decoration_water_flower, "water_flower");
  }

  m_useAtlas = true;
  WORLD_MANAGER_INFO("TileRenderer: Atlas coords loaded from world_objects.json");
}

void HammerEngine::TileRenderer::applyCoordsToTextures(Season season) {
  if (!m_useAtlas || !m_atlasPtr) {
    return;
  }

  const int s = static_cast<int>(season);
  const auto& coords = m_seasonalCoords[s];

  // Helper to apply coords to cached texture
  auto apply = [this](CachedTexture& tex, const AtlasCoords& c) {
    tex.ptr = m_atlasPtr;
    tex.atlasX = c.x;
    tex.atlasY = c.y;
    tex.w = c.w;
    tex.h = c.h;
  };

  // Apply all coords
  apply(m_cachedTextures.biome_default, coords.biome_default);
  apply(m_cachedTextures.biome_desert, coords.biome_desert);
  apply(m_cachedTextures.biome_forest, coords.biome_forest);
  apply(m_cachedTextures.biome_plains, coords.biome_plains);
  apply(m_cachedTextures.biome_mountain, coords.biome_mountain);
  apply(m_cachedTextures.biome_swamp, coords.biome_swamp);
  apply(m_cachedTextures.biome_haunted, coords.biome_haunted);
  apply(m_cachedTextures.biome_celestial, coords.biome_celestial);
  apply(m_cachedTextures.biome_ocean, coords.biome_ocean);
  apply(m_cachedTextures.obstacle_water, coords.obstacle_water);
  apply(m_cachedTextures.obstacle_tree, coords.obstacle_tree);
  apply(m_cachedTextures.obstacle_rock, coords.obstacle_rock);
  apply(m_cachedTextures.building_hut, coords.building_hut);
  apply(m_cachedTextures.building_house, coords.building_house);
  apply(m_cachedTextures.building_large, coords.building_large);
  apply(m_cachedTextures.building_cityhall, coords.building_cityhall);
  // Ore deposits
  apply(m_cachedTextures.obstacle_iron_deposit, coords.obstacle_iron_deposit);
  apply(m_cachedTextures.obstacle_gold_deposit, coords.obstacle_gold_deposit);
  apply(m_cachedTextures.obstacle_copper_deposit, coords.obstacle_copper_deposit);
  apply(m_cachedTextures.obstacle_mithril_deposit, coords.obstacle_mithril_deposit);
  apply(m_cachedTextures.obstacle_limestone_deposit, coords.obstacle_limestone_deposit);
  apply(m_cachedTextures.obstacle_coal_deposit, coords.obstacle_coal_deposit);
  // Gem deposits
  apply(m_cachedTextures.obstacle_emerald_deposit, coords.obstacle_emerald_deposit);
  apply(m_cachedTextures.obstacle_ruby_deposit, coords.obstacle_ruby_deposit);
  apply(m_cachedTextures.obstacle_sapphire_deposit, coords.obstacle_sapphire_deposit);
  apply(m_cachedTextures.obstacle_diamond_deposit, coords.obstacle_diamond_deposit);
  apply(m_cachedTextures.decoration_flower_blue, coords.decoration_flower_blue);
  apply(m_cachedTextures.decoration_flower_pink, coords.decoration_flower_pink);
  apply(m_cachedTextures.decoration_flower_white, coords.decoration_flower_white);
  apply(m_cachedTextures.decoration_flower_yellow, coords.decoration_flower_yellow);
  apply(m_cachedTextures.decoration_mushroom_purple, coords.decoration_mushroom_purple);
  apply(m_cachedTextures.decoration_mushroom_tan, coords.decoration_mushroom_tan);
  apply(m_cachedTextures.decoration_grass_small, coords.decoration_grass_small);
  apply(m_cachedTextures.decoration_grass_large, coords.decoration_grass_large);
  apply(m_cachedTextures.decoration_bush, coords.decoration_bush);
  apply(m_cachedTextures.decoration_stump_small, coords.decoration_stump_small);
  apply(m_cachedTextures.decoration_stump_medium, coords.decoration_stump_medium);
  apply(m_cachedTextures.decoration_rock_small, coords.decoration_rock_small);
  apply(m_cachedTextures.decoration_dead_log_hz, coords.decoration_dead_log_hz);
  apply(m_cachedTextures.decoration_dead_log_vertical, coords.decoration_dead_log_vertical);
  apply(m_cachedTextures.decoration_lily_pad, coords.decoration_lily_pad);
  apply(m_cachedTextures.decoration_water_flower, coords.decoration_water_flower);
}

void HammerEngine::TileRenderer::subscribeToSeasonEvents() {
  if (m_subscribedToSeasons) {
    return;
  }

  auto &eventMgr = EventManager::Instance();
  m_seasonToken = eventMgr.registerHandlerWithToken(
      EventTypeId::Time,
      [this](const EventData &data) { onSeasonChange(data); });
  m_subscribedToSeasons = true;

  // Initialize with current season from GameTime
  m_currentSeason = GameTimeManager::Instance().getSeason();
  WORLD_MANAGER_INFO(std::format(
      "TileRenderer subscribed to season events, current season: {}",
      GameTimeManager::Instance().getSeasonName()));
}

void HammerEngine::TileRenderer::unsubscribeFromSeasonEvents() {
  if (!m_subscribedToSeasons) {
    return;
  }

  auto &eventMgr = EventManager::Instance();
  eventMgr.removeHandler(m_seasonToken);
  m_subscribedToSeasons = false;
  WORLD_MANAGER_DEBUG("TileRenderer unsubscribed from season events");
}

void HammerEngine::TileRenderer::onSeasonChange(const EventData &data) {
  if (!data.event) {
    return;
  }

  // Match TimeController pattern - use static_pointer_cast + getTimeEventType()
  const auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
  if (timeEvent->getTimeEventType() != TimeEventType::SeasonChanged) {
    return;
  }

  const auto seasonEvent =
      std::static_pointer_cast<SeasonChangedEvent>(data.event);
  const Season newSeason = seasonEvent->getSeason();
  if (newSeason != m_currentSeason) {
    WORLD_MANAGER_INFO(std::format("TileRenderer: Season changed to {}",
                                   seasonEvent->getSeasonName()));
    setCurrentSeason(newSeason); // This updates m_currentSeason AND refreshes
                                 // cached texture IDs
  }
}

std::string HammerEngine::TileRenderer::getSeasonalTextureID(
    const std::string &baseID) const {
  static const char *const seasonPrefixes[] = {"spring_", "summer_", "fall_",
                                               "winter_"};
  return seasonPrefixes[static_cast<int>(m_currentSeason)] + baseID;
}

void HammerEngine::TileRenderer::renderChunkToTexture(
    const HammerEngine::WorldData &world, SDL_Renderer *renderer, int chunkX,
    int chunkY, SDL_Texture *target) {
  // Called from updateDirtyChunks() which runs before SceneRenderer pipeline,
  // so no need to save/restore render target
  SDL_SetRenderTarget(renderer, target);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
  SDL_RenderClear(renderer);

  const int worldWidth = static_cast<int>(world.grid[0].size());
  const int worldHeight = static_cast<int>(world.grid.size());
  const int startTileX = chunkX * CHUNK_SIZE;
  const int startTileY = chunkY * CHUNK_SIZE;
  const int endTileX = std::min(startTileX + CHUNK_SIZE, worldWidth);
  const int endTileY = std::min(startTileY + CHUNK_SIZE, worldHeight);

  constexpr int tileSize = static_cast<int>(TILE_SIZE);
  constexpr float tileSizeF = TILE_SIZE;

  // LAYER 1: Biomes - O(1) lookup table, no switch statements
  // NOTE: Biomes don't overhang, so we render ONLY the chunk area (not extended)
  // This reduces draw calls from 400 to 256 per chunk
  const CachedTexture *waterTex = &m_cachedTextures.obstacle_water;
  const CachedTexture *defaultTex = &m_cachedTextures.biome_default;

  float baseLocalY = static_cast<float>(SPRITE_OVERHANG);
  for (int y = startTileY; y < endTileY; ++y) {
    const auto &row = world.grid[y];
    float localX = static_cast<float>(SPRITE_OVERHANG);

    for (int x = startTileX; x < endTileX; ++x) {
      const HammerEngine::Tile &tile = row[x];

      // O(1) texture lookup - no branching for biome type
      const CachedTexture *tex;
      if (tile.isWater || tile.obstacleType == HammerEngine::ObstacleType::WATER) {
        tex = waterTex;
      } else {
        const size_t biomeIdx = static_cast<size_t>(tile.biome);
        tex = (biomeIdx < BIOME_COUNT) ? m_biomeLUT[biomeIdx] : defaultTex;
      }

      if (tex && tex->ptr) {
        SDL_FRect srcRect = {tex->atlasX, tex->atlasY, tex->w, tex->h};
        SDL_FRect destRect = {localX, baseLocalY, tileSizeF, tileSizeF};
        SDL_RenderTexture(renderer, tex->ptr, &srcRect, &destRect);
      }
      localX += tileSizeF;
    }
    baseLocalY += tileSizeF;
  }

  // LAYER 2: Decorations - O(1) lookup, early continue for common case
  baseLocalY = static_cast<float>(SPRITE_OVERHANG);
  for (int y = startTileY; y < endTileY; ++y) {
    const auto &row = world.grid[y];
    float localX = static_cast<float>(SPRITE_OVERHANG);

    for (int x = startTileX; x < endTileX; ++x) {
      const HammerEngine::Tile &tile = row[x];
      const auto decoType = static_cast<size_t>(tile.decorationType);

      // Fast path: skip if no decoration or blocked by obstacle
      if (decoType == 0 ||
          (tile.obstacleType != HammerEngine::ObstacleType::NONE &&
           tile.obstacleType != HammerEngine::ObstacleType::WATER)) {
        localX += tileSizeF;
        continue;
      }

      // O(1) lookup
      const CachedTexture *tex = (decoType < DECORATION_COUNT) ? m_decorationLUT[decoType] : nullptr;

      if (tex && tex->ptr) {
        const float offsetX = (tileSizeF - tex->w) * 0.5f;
        const float offsetY = tileSizeF - tex->h;
        SDL_FRect srcRect = {tex->atlasX, tex->atlasY, tex->w, tex->h};
        SDL_FRect destRect = {localX + offsetX, baseLocalY + offsetY, tex->w, tex->h};
        SDL_RenderTexture(renderer, tex->ptr, &srcRect, &destRect);
      }
      localX += tileSizeF;
    }
    baseLocalY += tileSizeF;
  }

  // LAYER 3: Y-sorted obstacles and buildings - optimized with lookup tables
  // Sprites extend UPWARD from their base, so scan BELOW chunk for tall sprite bases
  // X: ±1 tile for sprite width overhang
  // Y: 0 above (sprites don't extend down), +4 below (tallest buildings are 4 tiles)
  const int spriteStartX = std::max(0, startTileX - 1);
  const int spriteStartY = startTileY;  // No extension above - sprites extend upward, not down
  const int spriteEndX = std::min(worldWidth, endTileX + 1);
  const int spriteEndY = std::min(worldHeight, endTileY + 4);  // Scan below for tall sprites

  m_ySortBuffer.clear();

  // Building textures array for O(1) lookup by size
  const CachedTexture* buildingLUT[5] = {
    &m_cachedTextures.building_hut,    // size 0 (fallback)
    &m_cachedTextures.building_hut,    // size 1
    &m_cachedTextures.building_house,  // size 2
    &m_cachedTextures.building_large,  // size 3
    &m_cachedTextures.building_cityhall // size 4
  };

  baseLocalY = static_cast<float>((spriteStartY - startTileY) * tileSize + SPRITE_OVERHANG);
  for (int y = spriteStartY; y < spriteEndY; ++y) {
    const auto &row = world.grid[y];
    float localX = static_cast<float>((spriteStartX - startTileX) * tileSize + SPRITE_OVERHANG);

    for (int x = spriteStartX; x < spriteEndX; ++x) {
      const HammerEngine::Tile &tile = row[x];
      const auto obstacleIdx = static_cast<size_t>(tile.obstacleType);

      // Skip NONE, WATER (rendered in layer 1), and building parts (not top-left)
      if (obstacleIdx == 0 || obstacleIdx == 3) {  // NONE=0, WATER=3
        localX += tileSizeF;
        continue;
      }

      // Handle buildings
      if (obstacleIdx == 4) {  // BUILDING
        if (tile.isTopLeftOfBuilding) {
          const size_t sizeIdx = std::min(static_cast<size_t>(tile.buildingSize), size_t{4});
          const CachedTexture *tex = buildingLUT[sizeIdx];
          if (tex && tex->ptr) {
            const float sortY = baseLocalY + (tile.buildingSize * tileSizeF);
            m_ySortBuffer.push_back({sortY, localX, baseLocalY, tex, true,
                                     static_cast<int>(tex->w),
                                     static_cast<int>(tex->h)});
          }
        }
        localX += tileSizeF;
        continue;
      }

      // Regular obstacles - O(1) lookup
      const CachedTexture *tex = (obstacleIdx < OBSTACLE_COUNT) ? m_obstacleLUT[obstacleIdx] : nullptr;
      if (tex && tex->ptr) {
        const float offsetX = (tileSizeF - tex->w) * 0.5f;
        const float offsetY = tileSizeF - tex->h;
        const float sortY = baseLocalY + tileSizeF;
        m_ySortBuffer.push_back({sortY, localX + offsetX, baseLocalY + offsetY, tex, false, 0, 0});
      }
      localX += tileSizeF;
    }
    baseLocalY += tileSizeF;
  }

  // Sort is unavoidable for correct Y-ordering, but buffer is pre-allocated
  if (!m_ySortBuffer.empty()) {
    std::sort(m_ySortBuffer.begin(), m_ySortBuffer.end(),
              [](const YSortedSprite &a, const YSortedSprite &b) {
                if (a.y != b.y) return a.y < b.y;
                if (a.renderX != b.renderX) return a.renderX < b.renderX;
                return a.renderY < b.renderY;
              });

    // Render all sorted sprites
    for (const auto &sprite : m_ySortBuffer) {
      const float spriteW = sprite.isBuilding ? static_cast<float>(sprite.buildingWidth) : sprite.tex->w;
      const float spriteH = sprite.isBuilding ? static_cast<float>(sprite.buildingHeight) : sprite.tex->h;
      SDL_FRect srcRect = {sprite.tex->atlasX, sprite.tex->atlasY, sprite.tex->w, sprite.tex->h};
      SDL_FRect destRect = {sprite.renderX, sprite.renderY, spriteW, spriteH};
      SDL_RenderTexture(renderer, sprite.tex->ptr, &srcRect, &destRect);
    }
  }

  SDL_SetRenderTarget(renderer, nullptr);
}

void HammerEngine::TileRenderer::render(
    const HammerEngine::WorldData &world, SDL_Renderer *renderer, float cameraX,
    float cameraY, float viewportWidth, float viewportHeight) {
  PROFILE_RENDER_GPU(HammerEngine::RenderPhase::WorldTiles, renderer);

  if (world.grid.empty() || !renderer || !m_gridInitialized) {
    return;
  }

  // Check if camera crossed chunk boundary - rebuild visible list
  const int camChunkX = static_cast<int>(cameraX * INV_CHUNK_PIXELS);
  const int camChunkY = static_cast<int>(cameraY * INV_CHUNK_PIXELS);

  if (camChunkX != m_lastCamChunkX || camChunkY != m_lastCamChunkY) {
    rebuildVisibleList(camChunkX, camChunkY, viewportWidth, viewportHeight);
    m_lastCamChunkX = camChunkX;
    m_lastCamChunkY = camChunkY;
  }

  // SIMD: Calculate screen positions (4 chunks at a time)
  calculateScreenPositionsSIMD(cameraX, cameraY);

  // Draw all visible chunks
  for (size_t i = 0; i < m_visibleChunks.count; ++i) {
    SDL_FRect srcRect = {
        m_visibleChunks.srcX[i], m_visibleChunks.srcY[i],
        m_visibleChunks.srcW[i], m_visibleChunks.srcH[i]
    };
    SDL_FRect destRect = {
        m_visibleChunks.screenX[i], m_visibleChunks.screenY[i],
        m_visibleChunks.srcW[i], m_visibleChunks.srcH[i]
    };
    SDL_RenderTexture(renderer, m_visibleChunks.textures[i], &srcRect, &destRect);
  }
}

void HammerEngine::TileRenderer::prefetchChunks(
    const HammerEngine::WorldData &world, SDL_Renderer *renderer,
    float cameraX, float cameraY, float viewportWidth, float viewportHeight) {
  // Handles dirty chunk re-rendering, deferred cache clears (season changes),
  // and ensures proper render target restoration after chunk operations.
  if (world.grid.empty() || !renderer || !m_gridInitialized) {
    return;
  }

  // Handle deferred cache clear (e.g., season change)
  if (m_cachePendingClear.exchange(false, std::memory_order_acq_rel)) {
    for (auto& row : m_chunkGrid) {
      for (auto& chunk : row) {
        chunk.dirty = true;
      }
    }
    m_hasDirtyChunks = true;
    m_lastCamChunkX = -1;  // Force visible list rebuild
  }

  // Early-out: no dirty chunks
  if (!m_hasDirtyChunks) {
    return;
  }

  // Calculate extended chunk range (visible + 2 chunk margin in all directions)
  constexpr int margin = 2;
  const int startX = std::max(0, static_cast<int>(cameraX * INV_CHUNK_PIXELS) - margin);
  const int startY = std::max(0, static_cast<int>(cameraY * INV_CHUNK_PIXELS) - margin);
  const int endX = std::min(m_gridWidth, static_cast<int>((cameraX + viewportWidth) * INV_CHUNK_PIXELS) + 1 + margin);
  const int endY = std::min(m_gridHeight, static_cast<int>((cameraY + viewportHeight) * INV_CHUNK_PIXELS) + 1 + margin);

  // Re-render dirty chunks with budget to avoid stuttering
  constexpr int renderBudget = 4;
  int rendered = 0;
  bool anyDirty = false;

  for (int cy = startY; cy < endY && rendered < renderBudget; ++cy) {
    for (int cx = startX; cx < endX && rendered < renderBudget; ++cx) {
      auto& chunk = m_chunkGrid[cy][cx];
      if (chunk.dirty && chunk.texture) {
        renderChunkToTexture(world, renderer, cx, cy, chunk.texture.get());
        chunk.dirty = false;
        ++rendered;
      }
    }
  }

  // Check if more dirty chunks remain
  if (rendered == renderBudget) {
    for (int cy = startY; cy < endY; ++cy) {
      for (int cx = startX; cx < endX; ++cx) {
        if (m_chunkGrid[cy][cx].dirty) {
          anyDirty = true;
          break;
        }
      }
      if (anyDirty) break;
    }
  }

  m_hasDirtyChunks = anyDirty;

  // Force visible list rebuild if chunks were re-rendered
  if (rendered > 0) {
    m_lastCamChunkX = -1;
    // Restore render target after chunk operations
    SDL_SetRenderTarget(renderer, nullptr);
  }
}

void HammerEngine::TileRenderer::prewarmChunks(
    const HammerEngine::WorldData &world, SDL_Renderer *renderer,
    float cameraX, float cameraY, float viewportWidth, float viewportHeight) {
  if (world.grid.empty() || !renderer) {
    return;
  }

  // Initialize texture pool
  if (!m_poolInitialized) {
    initTexturePool(renderer);
  }

  // Initialize chunk grid if not done yet
  if (!m_gridInitialized) {
    initChunkGrid(world, renderer);
    return;  // Grid init renders all chunks
  }

  // Calculate visible chunk range with margin for smooth initial scrolling
  constexpr int margin = 3;
  const int startX = std::max(0, static_cast<int>(cameraX * INV_CHUNK_PIXELS) - margin);
  const int startY = std::max(0, static_cast<int>(cameraY * INV_CHUNK_PIXELS) - margin);
  const int endX = std::min(m_gridWidth, static_cast<int>((cameraX + viewportWidth) * INV_CHUNK_PIXELS) + 1 + margin);
  const int endY = std::min(m_gridHeight, static_cast<int>((cameraY + viewportHeight) * INV_CHUNK_PIXELS) + 1 + margin);

  // Re-render any dirty chunks in visible area - no budget limit for prewarm
  bool anyRendered = false;
  for (int cy = startY; cy < endY; ++cy) {
    for (int cx = startX; cx < endX; ++cx) {
      auto& chunk = m_chunkGrid[cy][cx];
      if (chunk.dirty && chunk.texture) {
        renderChunkToTexture(world, renderer, cx, cy, chunk.texture.get());
        chunk.dirty = false;
        anyRendered = true;
      }
    }
  }

  // Restore render target after prewarming chunks
  if (anyRendered) {
    SDL_SetRenderTarget(renderer, nullptr);
  }

  m_hasDirtyChunks = false;  // All visible chunks are now rendered
  m_lastCamChunkX = -1;      // Force visible list rebuild
}

void HammerEngine::TileRenderer::initChunkGrid(
    const HammerEngine::WorldData &world, SDL_Renderer *renderer) {
  if (world.grid.empty() || !renderer) {
    return;
  }

  const int worldWidth = static_cast<int>(world.grid[0].size());
  const int worldHeight = static_cast<int>(world.grid.size());
  m_gridWidth = (worldWidth + CHUNK_SIZE - 1) / CHUNK_SIZE;
  m_gridHeight = (worldHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

  WORLD_MANAGER_INFO(std::format("Initializing chunk grid: {}x{} chunks ({} total)",
                                  m_gridWidth, m_gridHeight, m_gridWidth * m_gridHeight));

  // Allocate 2D grid
  m_chunkGrid.resize(m_gridHeight);
  for (auto& row : m_chunkGrid) {
    row.resize(m_gridWidth);
  }

  // Create and render all chunk textures
  for (int cy = 0; cy < m_gridHeight; ++cy) {
    for (int cx = 0; cx < m_gridWidth; ++cx) {
      auto tex = acquireTexture(renderer);
      if (tex) {
        renderChunkToTexture(world, renderer, cx, cy, tex.get());
        m_chunkGrid[cy][cx].texture = std::move(tex);
        m_chunkGrid[cy][cx].dirty = false;
      }
    }
  }

  // Restore render target after initializing all chunks
  SDL_SetRenderTarget(renderer, nullptr);

  m_gridInitialized = true;
  m_hasDirtyChunks = false;
  m_lastCamChunkX = -1;  // Force visible list rebuild on first render

  WORLD_MANAGER_INFO("Chunk grid initialization complete");
}

void HammerEngine::TileRenderer::rebuildVisibleList(
    int camChunkX, int camChunkY, float viewW, float viewH) {
  m_visibleChunks.clear();

  // Calculate visible range with 1 chunk margin for edge sprites
  const int chunksX = static_cast<int>(viewW * INV_CHUNK_PIXELS) + 3;
  const int chunksY = static_cast<int>(viewH * INV_CHUNK_PIXELS) + 3;

  const int startX = std::max(0, camChunkX - 1);
  const int startY = std::max(0, camChunkY - 1);
  const int endX = std::min(m_gridWidth, camChunkX + chunksX);
  const int endY = std::min(m_gridHeight, camChunkY + chunksY);

  // Reserve space for expected visible chunks
  m_visibleChunks.reserve((endX - startX) * (endY - startY));

  for (int cy = startY; cy < endY; ++cy) {
    for (int cx = startX; cx < endX; ++cx) {
      SDL_Texture* tex = m_chunkGrid[cy][cx].texture.get();
      if (!tex) continue;

      // Calculate source rect with edge clipping for sprite overhang
      const float srcX = (cx > startX) ? static_cast<float>(SPRITE_OVERHANG) : 0.0f;
      const float srcY = (cy > startY) ? static_cast<float>(SPRITE_OVERHANG) : 0.0f;
      const float srcW = static_cast<float>(CHUNK_TEXTURE_SIZE) - srcX;
      const float srcH = static_cast<float>(CHUNK_TEXTURE_SIZE) - srcY;

      // Calculate world position (accounting for overhang offset)
      const float worldX = static_cast<float>(cx * CHUNK_PIXELS) -
                           static_cast<float>(SPRITE_OVERHANG) + srcX;
      const float worldY = static_cast<float>(cy * CHUNK_PIXELS) -
                           static_cast<float>(SPRITE_OVERHANG) + srcY;

      m_visibleChunks.push_back(tex, worldX, worldY, srcX, srcY, srcW, srcH);
    }
  }
}

void HammerEngine::TileRenderer::calculateScreenPositionsSIMD(
    float cameraX, float cameraY) {
  const size_t count = m_visibleChunks.count;
  if (count == 0) return;

  // Ensure output vectors have proper size
  m_visibleChunks.screenX.resize(count);
  m_visibleChunks.screenY.resize(count);

  size_t i = 0;

  // SIMD: Process 4 chunks at a time
  const Float4 camXVec = broadcast(cameraX);
  const Float4 camYVec = broadcast(cameraY);

  for (; i + 4 <= count; i += 4) {
    Float4 wx = load4(&m_visibleChunks.worldX[i]);
    Float4 wy = load4(&m_visibleChunks.worldY[i]);
    Float4 sx = sub(wx, camXVec);
    Float4 sy = sub(wy, camYVec);
    store4(&m_visibleChunks.screenX[i], sx);
    store4(&m_visibleChunks.screenY[i], sy);
  }

  // Scalar tail for remaining chunks
  for (; i < count; ++i) {
    m_visibleChunks.screenX[i] = m_visibleChunks.worldX[i] - cameraX;
    m_visibleChunks.screenY[i] = m_visibleChunks.worldY[i] - cameraY;
  }
}

void HammerEngine::TileRenderer::renderTile(const HammerEngine::Tile &tile,
                                            SDL_Renderer *renderer,
                                            float screenX,
                                            float screenY) const {
  if (!renderer) {
    WORLD_MANAGER_ERROR("TileRenderer: Cannot render tile - renderer is null");
    return;
  }

  // LAYER 1: Always render biome texture as the base layer (with seasonal
  // variant)
  std::string biomeTextureID;
  if (tile.isWater) {
    biomeTextureID = getSeasonalTextureID("obstacle_water");
  } else {
    biomeTextureID = getSeasonalTextureID(getBiomeTexture(tile.biome));
  }

  // Render base biome layer
  TextureManager::Instance().drawTileF(biomeTextureID, screenX, screenY,
                                       TILE_SIZE, TILE_SIZE, renderer);

  // LAYER 2: Render obstacles on top of biome (if present) with seasonal
  // variant
  if (tile.obstacleType != HammerEngine::ObstacleType::NONE) {
    const std::string obstacleTextureID =
        getSeasonalTextureID(getObstacleTexture(tile.obstacleType));

    // Render obstacle layer on top of biome
    TextureManager::Instance().drawTileF(obstacleTextureID, screenX, screenY,
                                         TILE_SIZE, TILE_SIZE, renderer);
  }

  // Debug logging for texture issues (only in debug builds)
  WORLD_MANAGER_WARN_IF(biomeTextureID.empty(),
                        std::format("TileRenderer: Empty biome texture ID for "
                                    "tile at screen position ({}, {})",
                                    screenX, screenY));
}

std::string
HammerEngine::TileRenderer::getBiomeTexture(HammerEngine::Biome biome) const {
  switch (biome) {
  case HammerEngine::Biome::DESERT:
    return "biome_desert";
  case HammerEngine::Biome::FOREST:
    return "biome_forest";
  case HammerEngine::Biome::PLAINS:
    return "biome_plains";
  case HammerEngine::Biome::MOUNTAIN:
    return "biome_mountain";
  case HammerEngine::Biome::SWAMP:
    return "biome_swamp";
  case HammerEngine::Biome::HAUNTED:
    return "biome_haunted";
  case HammerEngine::Biome::CELESTIAL:
    return "biome_celestial";
  case HammerEngine::Biome::OCEAN:
    return "biome_ocean";
  default:
    return "biome_default";
  }
}

std::string HammerEngine::TileRenderer::getObstacleTexture(
    HammerEngine::ObstacleType obstacle) const {
  switch (obstacle) {
  case HammerEngine::ObstacleType::TREE:
    return "obstacle_tree";
  case HammerEngine::ObstacleType::ROCK:
    return "obstacle_rock";
  case HammerEngine::ObstacleType::WATER:
    return "obstacle_water";
  case HammerEngine::ObstacleType::BUILDING:
    return "building_hut"; // Default to hut texture
  // Ore deposits
  case HammerEngine::ObstacleType::IRON_DEPOSIT:
    return "ore_iron_deposit";
  case HammerEngine::ObstacleType::GOLD_DEPOSIT:
    return "ore_gold_deposit";
  case HammerEngine::ObstacleType::COPPER_DEPOSIT:
    return "ore_copper_deposit";
  case HammerEngine::ObstacleType::MITHRIL_DEPOSIT:
    return "ore_mithril_deposit";
  case HammerEngine::ObstacleType::LIMESTONE_DEPOSIT:
    return "ore_limestone_deposit";
  case HammerEngine::ObstacleType::COAL_DEPOSIT:
    return "ore_coal_deposit";
  // Gem deposits
  case HammerEngine::ObstacleType::EMERALD_DEPOSIT:
    return "gem_emerald_deposit";
  case HammerEngine::ObstacleType::RUBY_DEPOSIT:
    return "gem_ruby_deposit";
  case HammerEngine::ObstacleType::SAPPHIRE_DEPOSIT:
    return "gem_sapphire_deposit";
  case HammerEngine::ObstacleType::DIAMOND_DEPOSIT:
    return "gem_diamond_deposit";
  default:
    return "biome_default";
  }
}

#ifdef USE_SDL3_GPU
// ============================================================================
// GPU Rendering Implementation
// ============================================================================

auto HammerEngine::TileRenderer::getBiomeAtlasCoords(Biome biome, Season season) const
    -> const AtlasCoords& {
  const auto& coords = m_seasonalCoords[static_cast<int>(season)];
  switch (biome) {
  case Biome::DESERT:
    return coords.biome_desert;
  case Biome::FOREST:
    return coords.biome_forest;
  case Biome::PLAINS:
    return coords.biome_plains;
  case Biome::MOUNTAIN:
    return coords.biome_mountain;
  case Biome::SWAMP:
    return coords.biome_swamp;
  case Biome::HAUNTED:
    return coords.biome_haunted;
  case Biome::CELESTIAL:
    return coords.biome_celestial;
  case Biome::OCEAN:
    return coords.biome_ocean;
  default:
    return coords.biome_default;
  }
}

auto HammerEngine::TileRenderer::getObstacleAtlasCoords(ObstacleType obstacle, Season season) const
    -> const AtlasCoords& {
  const auto& coords = m_seasonalCoords[static_cast<int>(season)];
  switch (obstacle) {
  case ObstacleType::TREE:
    return coords.obstacle_tree;
  case ObstacleType::ROCK:
    return coords.obstacle_rock;
  case ObstacleType::WATER:
    return coords.obstacle_water;
  case ObstacleType::BUILDING:
    return coords.building_hut;
  case ObstacleType::IRON_DEPOSIT:
    return coords.obstacle_iron_deposit;
  case ObstacleType::GOLD_DEPOSIT:
    return coords.obstacle_gold_deposit;
  case ObstacleType::COPPER_DEPOSIT:
    return coords.obstacle_copper_deposit;
  case ObstacleType::MITHRIL_DEPOSIT:
    return coords.obstacle_mithril_deposit;
  case ObstacleType::LIMESTONE_DEPOSIT:
    return coords.obstacle_limestone_deposit;
  case ObstacleType::COAL_DEPOSIT:
    return coords.obstacle_coal_deposit;
  case ObstacleType::EMERALD_DEPOSIT:
    return coords.obstacle_emerald_deposit;
  case ObstacleType::RUBY_DEPOSIT:
    return coords.obstacle_ruby_deposit;
  case ObstacleType::SAPPHIRE_DEPOSIT:
    return coords.obstacle_sapphire_deposit;
  case ObstacleType::DIAMOND_DEPOSIT:
    return coords.obstacle_diamond_deposit;
  default:
    return coords.biome_default;
  }
}

HammerEngine::GPUTexture*
HammerEngine::TileRenderer::getAtlasGPUTexture() const {
  if (!m_useAtlas) {
    return nullptr;
  }
  // Get GPU texture from TextureManager
  if (!m_atlasGPUPtr) {
    const_cast<TileRenderer*>(this)->m_atlasGPUPtr =
        TextureManager::Instance().getGPUTexture("atlas");
  }
  return m_atlasGPUPtr;
}

void HammerEngine::TileRenderer::recordGPUTiles(
    SpriteBatch& spriteBatch, float cameraX, float cameraY,
    float viewportWidth, float viewportHeight, float zoom,
    Season season) {

  // GPU rendering doesn't require chunk grid (m_gridInitialized)
  // It only requires atlas coords to be loaded (m_useAtlas)
  if (!m_useAtlas) {
    return;
  }

  // Get world data from WorldManager
  const auto* worldData = WorldManager::Instance().getWorldData();
  if (!worldData || worldData->grid.empty()) {
    return;
  }

  // 2D grid dimensions
  const int worldHeight = static_cast<int>(worldData->grid.size());
  const int worldWidth = static_cast<int>(worldData->grid[0].size());

  // Calculate visible tile range (with padding for partially visible tiles)
  const float effectiveViewWidth = viewportWidth / zoom;
  const float effectiveViewHeight = viewportHeight / zoom;

  const int startTileX = std::max(0, static_cast<int>(cameraX / TILE_SIZE) - VIEWPORT_PADDING);
  const int startTileY = std::max(0, static_cast<int>(cameraY / TILE_SIZE) - VIEWPORT_PADDING);
  const int endTileX = std::min(worldWidth,
                                static_cast<int>((cameraX + effectiveViewWidth) / TILE_SIZE) + VIEWPORT_PADDING + 1);
  const int endTileY = std::min(worldHeight,
                                static_cast<int>((cameraY + effectiveViewHeight) / TILE_SIZE) + VIEWPORT_PADDING + 1);

  // Early exit if no visible tiles
  if (startTileX >= endTileX || startTileY >= endTileY) {
    return;
  }

  // Pre-fetch seasonal coords (avoids per-tile lookups)
  const auto& sc = m_seasonalCoords[static_cast<int>(season)];

  // Render at 1x scale (matching SDL_Renderer approach)
  // Zoom is handled in the composite shader, not by scaling tile positions
  const float scaledTileSize = TILE_SIZE;  // 1x scale, no zoom multiplier
  const float baseCameraX = cameraX;       // No zoom multiplier
  const float baseCameraY = cameraY;
  const float startScreenX = startTileX * scaledTileSize - baseCameraX;

  // Debug: Log tile rendering params on viewport change
  static float lastViewportW = 0, lastViewportH = 0;
  if (viewportWidth != lastViewportW || viewportHeight != lastViewportH) {
    WORLD_MANAGER_INFO(std::format(
        "GPU tile params: viewport={}x{}, zoom={}, tileSize={}, "
        "scaledTileSize={}, visibleTiles={}x{}, cameraY={}, effectiveH={}, endTileY={}",
        viewportWidth, viewportHeight, zoom, TILE_SIZE,
        scaledTileSize, endTileX - startTileX, endTileY - startTileY,
        cameraY, effectiveViewHeight, endTileY));
    lastViewportW = viewportWidth;
    lastViewportH = viewportHeight;
  }

  // Build biome lookup table for O(1) access (enum value -> coords pointer)
  // Avoids switch overhead in inner loop
  const AtlasCoords* biomeLUT[8] = {
      &sc.biome_desert, &sc.biome_forest, &sc.biome_plains, &sc.biome_mountain,
      &sc.biome_swamp, &sc.biome_haunted, &sc.biome_celestial, &sc.biome_ocean
  };

  // Build obstacle lookup table (enum value -> coords pointer)
  // Index 0 = NONE (unused), indices 1-14 match ObstacleType enum values
  const AtlasCoords* obstacleLUT[15] = {
      &sc.biome_default,          // NONE (never used)
      &sc.obstacle_rock,          // ROCK
      &sc.obstacle_tree,          // TREE
      &sc.obstacle_water,         // WATER
      &sc.building_hut,           // BUILDING
      &sc.obstacle_iron_deposit,  // IRON_DEPOSIT
      &sc.obstacle_gold_deposit,  // GOLD_DEPOSIT
      &sc.obstacle_copper_deposit, // COPPER_DEPOSIT
      &sc.obstacle_mithril_deposit, // MITHRIL_DEPOSIT
      &sc.obstacle_limestone_deposit, // LIMESTONE_DEPOSIT
      &sc.obstacle_coal_deposit,  // COAL_DEPOSIT
      &sc.obstacle_emerald_deposit, // EMERALD_DEPOSIT
      &sc.obstacle_ruby_deposit,  // RUBY_DEPOSIT
      &sc.obstacle_sapphire_deposit, // SAPPHIRE_DEPOSIT
      &sc.obstacle_diamond_deposit // DIAMOND_DEPOSIT
  };

  // Iterate visible tiles with cache-friendly row-major order
  for (int tileY = startTileY; tileY < endTileY; ++tileY) {
    // Cache row reference for contiguous memory access
    const auto& row = worldData->grid[tileY];
    const float screenY = tileY * scaledTileSize - baseCameraY;
    float screenX = startScreenX;

    for (int tileX = startTileX; tileX < endTileX; ++tileX) {
      const auto& tile = row[tileX];

      // Layer 1: Biome base tile - LUT for water or biome enum
      const AtlasCoords* biomeCoords = tile.isWater
          ? &sc.obstacle_water
          : biomeLUT[static_cast<int>(tile.biome)];

      spriteBatch.draw(
          biomeCoords->x, biomeCoords->y, biomeCoords->w, biomeCoords->h,
          screenX, screenY, scaledTileSize, scaledTileSize);

      // Layer 2: Obstacle (if any) - direct LUT access
      if (tile.obstacleType != ObstacleType::NONE) {
        const auto obstacleIdx = static_cast<int>(tile.obstacleType);
        const AtlasCoords* obstacleCoords = obstacleLUT[obstacleIdx];

        spriteBatch.draw(
            obstacleCoords->x, obstacleCoords->y, obstacleCoords->w, obstacleCoords->h,
            screenX, screenY, scaledTileSize, scaledTileSize);
      }

      screenX += scaledTileSize;
    }
  }
  // Batch end() is called by GPUSceneRenderer, not here
}

// WorldManager GPU method - delegates to TileRenderer
void WorldManager::recordGPU(HammerEngine::SpriteBatch& spriteBatch,
                             float cameraX, float cameraY,
                             float viewWidth, float viewHeight, float zoom) {
  if (!m_initialized.load(std::memory_order_acquire) || !m_renderingEnabled) {
    return;
  }

  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  // Get current season
  auto season = getCurrentSeason();

  // Delegate to TileRenderer - batch is already begin()-ed by GPUSceneRenderer
  m_tileRenderer->recordGPUTiles(spriteBatch, cameraX, cameraY,
                                 viewWidth, viewHeight, zoom, season);
}
#endif
