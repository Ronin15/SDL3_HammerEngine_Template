/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
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
#include <format>

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

  std::shared_lock<std::shared_mutex> lock(m_worldMutex);

  // Check m_currentWorld AFTER acquiring lock to prevent TOCTOU race condition
  if (!m_currentWorld || !m_tileRenderer) {
    return;
  }

  m_tileRenderer->render(*m_currentWorld, renderer, cameraX, cameraY,
                         viewportWidth, viewportHeight);
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

  m_currentWorld->grid[y][x] = newTile;

  // Invalidate chunk containing this tile AND adjacent chunks only if tile is
  // near chunk edge (within overhang distance). Sprites can extend up to 2 tiles
  // (SPRITE_OVERHANG / TILE_SIZE) into neighboring chunks.
  if (m_tileRenderer) {
    constexpr int chunkSize = 32;    // TileRenderer::CHUNK_SIZE
    constexpr int overhangTiles = 2; // SPRITE_OVERHANG (64) / TILE_SIZE (32)

    const int chunkX = x / chunkSize;
    const int chunkY = y / chunkSize;
    const int localX = x % chunkSize;
    const int localY = y % chunkSize;

    // Primary chunk always invalidated
    m_tileRenderer->invalidateChunk(chunkX, chunkY);

    // Neighbors only if tile is near edge (within overhang distance)
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
}

void HammerEngine::TileRenderer::setCurrentSeason(Season season) {
  if (m_currentSeason != season) {
    m_currentSeason = season;
    updateCachedTextureIDs();
    clearChunkCache(); // Season change requires re-rendering all chunks with
                       // new textures
  }
}

void HammerEngine::TileRenderer::invalidateChunk([[maybe_unused]] int chunkX,
                                                 [[maybe_unused]] int chunkY) {
  // No-op: Direct rendering mode doesn't cache chunks
  // Kept for API compatibility with WorldManager::updateTile()
}

void HammerEngine::TileRenderer::clearChunkCache() {
  // No-op: Direct rendering mode doesn't cache chunks
  // Kept for API compatibility with season changes and world unload
}

HammerEngine::TileRenderer::~TileRenderer() { unsubscribeFromSeasonEvents(); }

void HammerEngine::TileRenderer::initAtlasCoords() {
  // Get atlas texture pointer from TextureManager (already loaded)
  auto atlasTex = TextureManager::Instance().getTexture("atlas");
  if (!atlasTex) {
    WORLD_MANAGER_INFO("Atlas texture not found - using individual textures");
    m_useAtlas = false;
    return;
  }
  m_atlasPtr = atlasTex.get();

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

void HammerEngine::TileRenderer::render(
    const HammerEngine::WorldData& world, SDL_Renderer* renderer,
    float cameraX, float cameraY, float viewportWidth, float viewportHeight) {
  if (world.grid.empty()) {
    WORLD_MANAGER_WARN("TileRenderer: Cannot render - world grid is empty");
    return;
  }

  if (!renderer) {
    WORLD_MANAGER_ERROR("TileRenderer: Cannot render - renderer is null");
    return;
  }

  // Camera position is already floored by SceneRenderer for pixel-perfect tiles
  const float flooredCamX = cameraX;
  const float flooredCamY = cameraY;

  // Calculate visible tile range with padding for sprites extending beyond bounds
  const int worldWidth = static_cast<int>(world.grid[0].size());
  const int worldHeight = static_cast<int>(world.grid.size());
  constexpr int tileSize = static_cast<int>(TILE_SIZE);

  // Visible tile range (with 1 tile padding for partial tiles at edges)
  const int startTileX = std::max(0, static_cast<int>(flooredCamX / TILE_SIZE) - VIEWPORT_PADDING);
  const int startTileY = std::max(0, static_cast<int>(flooredCamY / TILE_SIZE) - VIEWPORT_PADDING);
  const int endTileX = std::min(worldWidth, static_cast<int>((flooredCamX + viewportWidth) / TILE_SIZE) + VIEWPORT_PADDING + 1);
  const int endTileY = std::min(worldHeight, static_cast<int>((flooredCamY + viewportHeight) / TILE_SIZE) + VIEWPORT_PADDING + 1);

  // Extended range for sprites that overhang into view (trees, buildings extend upward)
  const int spriteStartX = std::max(0, startTileX - 2);
  const int spriteStartY = std::max(0, startTileY - 4);  // 4 tiles for tall sprites
  const int spriteEndX = std::min(worldWidth, endTileX + 2);
  const int spriteEndY = std::min(worldHeight, endTileY + 1);

  // LAYER 1: Render biome tiles directly from atlas
  // SDL3 automatically batches these draws since they all use the same atlas texture
  for (int y = startTileY; y < endTileY; ++y) {
    for (int x = startTileX; x < endTileX; ++x) {
      const HammerEngine::Tile& tile = world.grid[y][x];

      // Calculate screen position
      const float screenX = static_cast<float>(x * tileSize) - flooredCamX;
      const float screenY = static_cast<float>(y * tileSize) - flooredCamY;

      const CachedTexture* tex = &m_cachedTextures.biome_default;
      if (tile.isWater || tile.obstacleType == HammerEngine::ObstacleType::WATER) {
        tex = &m_cachedTextures.obstacle_water;
      } else {
        switch (tile.biome) {
        case HammerEngine::Biome::DESERT:
          tex = &m_cachedTextures.biome_desert;
          break;
        case HammerEngine::Biome::FOREST:
          tex = &m_cachedTextures.biome_forest;
          break;
        case HammerEngine::Biome::PLAINS:
          tex = &m_cachedTextures.biome_plains;
          break;
        case HammerEngine::Biome::MOUNTAIN:
          tex = &m_cachedTextures.biome_mountain;
          break;
        case HammerEngine::Biome::SWAMP:
          tex = &m_cachedTextures.biome_swamp;
          break;
        case HammerEngine::Biome::HAUNTED:
          tex = &m_cachedTextures.biome_haunted;
          break;
        case HammerEngine::Biome::CELESTIAL:
          tex = &m_cachedTextures.biome_celestial;
          break;
        case HammerEngine::Biome::OCEAN:
          tex = &m_cachedTextures.biome_ocean;
          break;
        default:
          break;
        }
      }

      if (tex->ptr) {
        SDL_FRect srcRect = {tex->atlasX, tex->atlasY, tex->w, tex->h};
        SDL_FRect destRect = {screenX, screenY, static_cast<float>(tileSize), static_cast<float>(tileSize)};
        SDL_RenderTexture(renderer, tex->ptr, &srcRect, &destRect);
      }
    }
  }

  // LAYER 2: Render decorations (ground-level, before Y-sorted obstacles)
  for (int y = spriteStartY; y < spriteEndY; ++y) {
    for (int x = spriteStartX; x < spriteEndX; ++x) {
      const HammerEngine::Tile& tile = world.grid[y][x];

      if (tile.decorationType == HammerEngine::DecorationType::NONE) {
        continue;
      }

      // Skip decorations on land obstacles (trees, rocks have visual priority)
      if (tile.obstacleType != HammerEngine::ObstacleType::NONE &&
          tile.obstacleType != HammerEngine::ObstacleType::WATER) {
        continue;
      }

      const float screenX = static_cast<float>(x * tileSize) - flooredCamX;
      const float screenY = static_cast<float>(y * tileSize) - flooredCamY;

      const CachedTexture* tex = nullptr;
      switch (tile.decorationType) {
      case HammerEngine::DecorationType::FLOWER_BLUE:
        tex = &m_cachedTextures.decoration_flower_blue;
        break;
      case HammerEngine::DecorationType::FLOWER_PINK:
        tex = &m_cachedTextures.decoration_flower_pink;
        break;
      case HammerEngine::DecorationType::FLOWER_WHITE:
        tex = &m_cachedTextures.decoration_flower_white;
        break;
      case HammerEngine::DecorationType::FLOWER_YELLOW:
        tex = &m_cachedTextures.decoration_flower_yellow;
        break;
      case HammerEngine::DecorationType::MUSHROOM_PURPLE:
        tex = &m_cachedTextures.decoration_mushroom_purple;
        break;
      case HammerEngine::DecorationType::MUSHROOM_TAN:
        tex = &m_cachedTextures.decoration_mushroom_tan;
        break;
      case HammerEngine::DecorationType::GRASS_SMALL:
        tex = &m_cachedTextures.decoration_grass_small;
        break;
      case HammerEngine::DecorationType::GRASS_LARGE:
        tex = &m_cachedTextures.decoration_grass_large;
        break;
      case HammerEngine::DecorationType::BUSH:
        tex = &m_cachedTextures.decoration_bush;
        break;
      case HammerEngine::DecorationType::STUMP_SMALL:
        tex = &m_cachedTextures.decoration_stump_small;
        break;
      case HammerEngine::DecorationType::STUMP_MEDIUM:
        tex = &m_cachedTextures.decoration_stump_medium;
        break;
      case HammerEngine::DecorationType::ROCK_SMALL:
        tex = &m_cachedTextures.decoration_rock_small;
        break;
      case HammerEngine::DecorationType::DEAD_LOG_HZ:
        tex = &m_cachedTextures.decoration_dead_log_hz;
        break;
      case HammerEngine::DecorationType::DEAD_LOG_VERTICAL:
        tex = &m_cachedTextures.decoration_dead_log_vertical;
        break;
      case HammerEngine::DecorationType::LILY_PAD:
        tex = &m_cachedTextures.decoration_lily_pad;
        break;
      case HammerEngine::DecorationType::WATER_FLOWER:
        tex = &m_cachedTextures.decoration_water_flower;
        break;
      default:
        continue;
      }

      if (!tex || !tex->ptr) {
        continue;
      }

      // Center decoration horizontally, align bottom to tile bottom
      const float offsetX = (TILE_SIZE - tex->w) / 2.0f;
      const float offsetY = TILE_SIZE - tex->h;

      SDL_FRect srcRect = {tex->atlasX, tex->atlasY, tex->w, tex->h};
      SDL_FRect destRect = {screenX + offsetX, screenY + offsetY, tex->w, tex->h};
      SDL_RenderTexture(renderer, tex->ptr, &srcRect, &destRect);
    }
  }

  // LAYER 3: Collect obstacles and buildings for Y-sorted rendering
  m_ySortBuffer.clear();
  // Reserve capacity to prevent per-frame reallocations (estimated max visible sprites)
  if (m_ySortBuffer.capacity() < 512) {
    m_ySortBuffer.reserve(512);
  }

  for (int y = spriteStartY; y < spriteEndY; ++y) {
    for (int x = spriteStartX; x < spriteEndX; ++x) {
      const HammerEngine::Tile& tile = world.grid[y][x];

      const float screenX = static_cast<float>(x * tileSize) - flooredCamX;
      const float screenY = static_cast<float>(y * tileSize) - flooredCamY;

      const bool isPartOfBuilding =
          (tile.obstacleType == HammerEngine::ObstacleType::BUILDING &&
           !tile.isTopLeftOfBuilding);

      // Obstacles (trees, rocks) - bottom-center positioned
      if (!isPartOfBuilding &&
          tile.obstacleType != HammerEngine::ObstacleType::NONE &&
          tile.obstacleType != HammerEngine::ObstacleType::BUILDING) {

        const CachedTexture* tex = &m_cachedTextures.biome_default;
        switch (tile.obstacleType) {
        case HammerEngine::ObstacleType::TREE:
          tex = &m_cachedTextures.obstacle_tree;
          break;
        case HammerEngine::ObstacleType::ROCK:
          tex = &m_cachedTextures.obstacle_rock;
          break;
        case HammerEngine::ObstacleType::WATER:
          continue;
        case HammerEngine::ObstacleType::IRON_DEPOSIT:
          tex = &m_cachedTextures.obstacle_iron_deposit;
          break;
        case HammerEngine::ObstacleType::GOLD_DEPOSIT:
          tex = &m_cachedTextures.obstacle_gold_deposit;
          break;
        case HammerEngine::ObstacleType::COPPER_DEPOSIT:
          tex = &m_cachedTextures.obstacle_copper_deposit;
          break;
        case HammerEngine::ObstacleType::MITHRIL_DEPOSIT:
          tex = &m_cachedTextures.obstacle_mithril_deposit;
          break;
        case HammerEngine::ObstacleType::LIMESTONE_DEPOSIT:
          tex = &m_cachedTextures.obstacle_limestone_deposit;
          break;
        case HammerEngine::ObstacleType::COAL_DEPOSIT:
          tex = &m_cachedTextures.obstacle_coal_deposit;
          break;
        case HammerEngine::ObstacleType::EMERALD_DEPOSIT:
          tex = &m_cachedTextures.obstacle_emerald_deposit;
          break;
        case HammerEngine::ObstacleType::RUBY_DEPOSIT:
          tex = &m_cachedTextures.obstacle_ruby_deposit;
          break;
        case HammerEngine::ObstacleType::SAPPHIRE_DEPOSIT:
          tex = &m_cachedTextures.obstacle_sapphire_deposit;
          break;
        case HammerEngine::ObstacleType::DIAMOND_DEPOSIT:
          tex = &m_cachedTextures.obstacle_diamond_deposit;
          break;
        default:
          break;
        }

        const float offsetX = (TILE_SIZE - tex->w) / 2.0f;
        const float offsetY = TILE_SIZE - tex->h;
        const float sortY = screenY + TILE_SIZE;

        m_ySortBuffer.push_back(
            {sortY, screenX + offsetX, screenY + offsetY, tex, false, 0, 0});
      }

      // Buildings - from top-left tile only
      if (tile.obstacleType == HammerEngine::ObstacleType::BUILDING &&
          tile.isTopLeftOfBuilding) {
        const CachedTexture* tex = &m_cachedTextures.building_hut;
        switch (tile.buildingSize) {
        case 1:
          tex = &m_cachedTextures.building_hut;
          break;
        case 2:
          tex = &m_cachedTextures.building_house;
          break;
        case 3:
          tex = &m_cachedTextures.building_large;
          break;
        case 4:
          tex = &m_cachedTextures.building_cityhall;
          break;
        default:
          break;
        }

        float sortY = screenY + (tile.buildingSize * TILE_SIZE);

        m_ySortBuffer.push_back({sortY, screenX, screenY, tex, true,
                                 static_cast<int>(tex->w),
                                 static_cast<int>(tex->h)});
      }
    }
  }

  // Sort by Y (bottom of sprite) for proper depth layering
  // Secondary sort by X, then renderY for fully deterministic ordering
  std::sort(
      m_ySortBuffer.begin(), m_ySortBuffer.end(),
      [](const YSortedSprite& a, const YSortedSprite& b) {
        if (a.y != b.y) return a.y < b.y;
        if (a.renderX != b.renderX) return a.renderX < b.renderX;
        return a.renderY < b.renderY;  // Final tie-breaker
      });

  // Render sorted sprites directly from atlas
  for (const auto& sprite : m_ySortBuffer) {
    const float spriteW = sprite.isBuilding ? static_cast<float>(sprite.buildingWidth)
                                            : sprite.tex->w;
    const float spriteH = sprite.isBuilding ? static_cast<float>(sprite.buildingHeight)
                                            : sprite.tex->h;
    SDL_FRect srcRect = {sprite.tex->atlasX, sprite.tex->atlasY, sprite.tex->w, sprite.tex->h};
    SDL_FRect destRect = {sprite.renderX, sprite.renderY, spriteW, spriteH};
    SDL_RenderTexture(renderer, sprite.tex->ptr, &srcRect, &destRect);
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
