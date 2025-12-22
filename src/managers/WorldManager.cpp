/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "managers/EventManager.hpp"
#include "managers/TextureManager.hpp"
#include "managers/ResourceTemplateManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "events/ResourceChangeEvent.hpp"
#include "events/TimeEvent.hpp"

#include "events/HarvestResourceEvent.hpp"
#include "SDL3/SDL_render.h"
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
        // Note: Event handlers will be registered later to avoid race conditions with EventManager

        m_isShutdown = false;
        m_initialized.store(true, std::memory_order_release);
        WORLD_MANAGER_INFO("WorldManager initialized successfully (event handlers will be registered later)");
        return true;
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("WorldManager::init - Exception: {}", ex.what()));
        return false;
    }
}

void WorldManager::setupEventHandlers() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        WORLD_MANAGER_ERROR("WorldManager not initialized - cannot setup event handlers");
        return;
    }

    try {
        registerEventHandlers();

        // Subscribe TileRenderer to season events for seasonal texture switching
        if (m_tileRenderer) {
            m_tileRenderer->subscribeToSeasonEvents();
        }

        WORLD_MANAGER_DEBUG("WorldManager event handlers setup complete");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("WorldManager::setupEventHandlers - Exception: {}", ex.what()));
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

    unloadWorldUnsafe();  // Use unsafe version to avoid double-locking
    m_tileRenderer.reset();

    m_initialized.store(false, std::memory_order_release);
    m_isShutdown = true;

    WORLD_MANAGER_INFO("WorldManager cleaned up");
}

bool WorldManager::loadNewWorld(const HammerEngine::WorldGenerationConfig& config,
                               const HammerEngine::WorldGenerationProgressCallback& progressCallback) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        WORLD_MANAGER_ERROR("WorldManager not initialized");
        return false;
    }

    std::lock_guard<std::shared_mutex> lock(m_worldMutex);

    try {
        auto newWorld = HammerEngine::WorldGenerator::generateWorld(config, progressCallback);
        if (!newWorld) {
            WORLD_MANAGER_ERROR("Failed to generate new world");
            return false;
        }

        // Unload current world if it exists
        if (m_currentWorld) {
            WORLD_MANAGER_INFO(std::format("Unloading current world: {}", m_currentWorld->worldId));
        }

        // Set new world
        m_currentWorld = std::move(newWorld);

        // Register world with WorldResourceManager
        WorldResourceManager::Instance().createWorld(m_currentWorld->worldId);

        // Initialize world resources based on world data
        initializeWorldResources();

        WORLD_MANAGER_INFO(std::format("Successfully loaded new world: {}", m_currentWorld->worldId));

        // Schedule world loaded event for next frame using ThreadSystem
        // Don't fire event while holding world mutex - use low priority to avoid blocking critical tasks
        std::string worldIdCopy = m_currentWorld->worldId;
        // Schedule world loaded event for next frame using ThreadSystem to avoid deadlocks
        // Use high priority to ensure it executes quickly for tests
        WORLD_MANAGER_INFO(std::format("Enqueuing WorldLoadedEvent task for world: {}", worldIdCopy));
        HammerEngine::ThreadSystem::Instance().enqueueTask([worldIdCopy, this]() {
            WORLD_MANAGER_INFO(std::format("Executing WorldLoadedEvent task for world: {}", worldIdCopy));
            fireWorldLoadedEvent(worldIdCopy);
        }, HammerEngine::TaskPriority::High, std::format("WorldLoadedEvent_{}", worldIdCopy));

        return true;
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("WorldManager::loadNewWorld - Exception: {}", ex.what()));
        return false;
    }
}

bool WorldManager::loadWorld(const std::string& worldId) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        WORLD_MANAGER_ERROR("WorldManager not initialized");
        return false;
    }

    // Suppress unused parameter warning since this is not implemented yet
    (void)worldId;

    WORLD_MANAGER_WARN("WorldManager::loadWorld - Loading saved worlds not yet implemented");
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
        // Uses deferred clearing (thread-safe) - actual clear happens on render thread
        if (m_tileRenderer) {
            m_tileRenderer->clearChunkCache();
        }

        m_currentWorld.reset();
    }
}

const HammerEngine::Tile* WorldManager::getTileAt(int x, int y) const {
    std::shared_lock<std::shared_mutex> lock(m_worldMutex);

    if (!m_currentWorld || !isValidPosition(x, y)) {
        return nullptr;
    }

    return &m_currentWorld->grid[y][x];
}

HammerEngine::Tile* WorldManager::getTileAt(int x, int y) {
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
    const int width = height > 0 ? static_cast<int>(m_currentWorld->grid[0].size()) : 0;

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
                         int viewportWidth, int viewportHeight) {
    if (!m_initialized.load(std::memory_order_acquire) || !m_renderingEnabled || !renderer) {
        return;
    }

    std::shared_lock<std::shared_mutex> lock(m_worldMutex);

    // Check m_currentWorld AFTER acquiring lock to prevent TOCTOU race condition
    if (!m_currentWorld || !m_tileRenderer) {
        return;
    }

    m_tileRenderer->renderVisibleTiles(*m_currentWorld, renderer, cameraX, cameraY, viewportWidth, viewportHeight);
}

bool WorldManager::handleHarvestResource(int entityId, int targetX, int targetY) {
    if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld) {
        WORLD_MANAGER_ERROR("WorldManager not initialized or no active world");
        return false;
    }

    // Suppress unused parameter warning since this is for future resource tracking
    (void)entityId;

    std::lock_guard<std::shared_mutex> lock(m_worldMutex);

    if (!isValidPosition(targetX, targetY)) {
        WORLD_MANAGER_ERROR(std::format("Invalid harvest position: ({}, {})", targetX, targetY));
        return false;
    }

    HammerEngine::Tile& tile = m_currentWorld->grid[targetY][targetX];

    if (tile.obstacleType == HammerEngine::ObstacleType::NONE) {
        WORLD_MANAGER_WARN(std::format("No harvestable resource at position: ({}, {})", targetX, targetY));
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
    // This is a placeholder - actual resource tracking would need proper resource handles

    WORLD_MANAGER_INFO(std::format("Resource harvested at ({}, {}) by entity {}", targetX, targetY, entityId));
    return true;
}

bool WorldManager::updateTile(int x, int y, const HammerEngine::Tile& newTile) {
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

    // Invalidate chunk containing this tile AND adjacent chunks (for sprite overhang)
    // Sprites can extend from adjacent tiles into neighboring chunks
    if (m_tileRenderer) {
        constexpr int chunkSize = 32;  // TileRenderer::CHUNK_SIZE
        const int chunkX = x / chunkSize;
        const int chunkY = y / chunkSize;

        // Invalidate primary chunk
        m_tileRenderer->invalidateChunk(chunkX, chunkY);

        // Invalidate adjacent chunks that might render this tile's sprites
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY);      // Left
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY);      // Right
        m_tileRenderer->invalidateChunk(chunkX, chunkY - 1);      // Top
        m_tileRenderer->invalidateChunk(chunkX, chunkY + 1);      // Bottom
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY - 1);  // Top-left
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY - 1);  // Top-right
        m_tileRenderer->invalidateChunk(chunkX - 1, chunkY + 1);  // Bottom-left
        m_tileRenderer->invalidateChunk(chunkX + 1, chunkY + 1);  // Bottom-right
    }

    fireTileChangedEvent(x, y, newTile);

    return true;
}

void WorldManager::fireTileChangedEvent(int x, int y, const HammerEngine::Tile& tile) {
    // Increment world version for change tracking by other systems (PathfinderManager, etc.)
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
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerTileChanged(x, y, changeType, EventManager::DispatchMode::Deferred);

        WORLD_MANAGER_DEBUG(std::format("TileChangedEvent fired for tile at ({}, {})", x, y));
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Failed to fire TileChangedEvent: {}", ex.what()));
    }
}

void WorldManager::fireWorldLoadedEvent(const std::string& worldId) {
    // Increment world version when world is loaded (major change for other systems)
    m_worldVersion.fetch_add(1, std::memory_order_release);
    
    try {
        // Get world dimensions
        int width, height;
        if (!getWorldDimensions(width, height)) {
            width = 0;
            height = 0;
        }

        // Trigger a world loaded event via EventManager (no registration)
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerWorldLoaded(worldId, width, height, EventManager::DispatchMode::Deferred);

        WORLD_MANAGER_INFO(std::format("WorldLoadedEvent registered and executed for world: {} ({}x{})", worldId, width, height));
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Failed to fire WorldLoadedEvent: {}", ex.what()));
    }
}

void WorldManager::fireWorldUnloadedEvent(const std::string& worldId) {
    try {
        // Trigger world unloaded via EventManager (no registration)
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerWorldUnloaded(worldId, EventManager::DispatchMode::Deferred);

        WORLD_MANAGER_INFO(std::format("WorldUnloadedEvent fired for world: {}", worldId));
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Failed to fire WorldUnloadedEvent: {}", ex.what()));
    }
}

void WorldManager::registerEventHandlers() {
    try {
        EventManager & eventMgr = EventManager::Instance();
        m_handlerTokens.clear();

        // Register handler for world events (to respond to events from other systems)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::World, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle world-related events from other systems
                WORLD_MANAGER_DEBUG(std::format("WorldManager received world event: {}", data.event->getName()));
            }
        }));

        // Register handler for camera events (world bounds may affect camera)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::Camera, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle camera events that may require world data updates
                WORLD_MANAGER_DEBUG(std::format("WorldManager received camera event: {}", data.event->getName()));
            }
        }));

        // Register handler for resource change events (resource changes may affect world state)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::ResourceChange, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle resource changes that may affect world generation or state
                auto resourceEvent = std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
                if (resourceEvent) {
                    WORLD_MANAGER_DEBUG(std::format("WorldManager received resource change: {} changed by {}",
                                       resourceEvent->getResourceHandle().toString(),
                                       resourceEvent->getQuantityChange()));
                }
            }
        }));

        // Register handler for harvest resource events
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::Harvest, [this](const EventData& data) {
            if (data.isActive() && data.event) {
                auto harvestEvent = std::dynamic_pointer_cast<HarvestResourceEvent>(data.event);
                if (harvestEvent) {
                    WORLD_MANAGER_DEBUG(std::format("WorldManager received harvest request from entity {} at ({}, {})",
                                       harvestEvent->getEntityId(),
                                       harvestEvent->getTargetX(),
                                       harvestEvent->getTargetY()));

                    // Handle the harvest request
                    handleHarvestResource(harvestEvent->getEntityId(),
                                        harvestEvent->getTargetX(),
                                        harvestEvent->getTargetY());
                }
            }
        }));

        WORLD_MANAGER_DEBUG("WorldManager event handlers registered");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Failed to register event handlers: {}", ex.what()));
    }
}

void WorldManager::unregisterEventHandlers() {
    try {
        auto &eventMgr = EventManager::Instance();
        for (const auto &tok : m_handlerTokens) {
            (void)eventMgr.removeHandler(tok);
        }
        m_handlerTokens.clear();
        WORLD_MANAGER_DEBUG("WorldManager event handlers unregistered (tokens cleared)");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Failed to unregister event handlers: {}", ex.what()));
    }
}

bool WorldManager::getWorldDimensions(int& width, int& height) const {
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

bool WorldManager::getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const {
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
    maxX = static_cast<float>(width) * HammerEngine::TILE_SIZE;   // Convert tiles to pixels
    maxY = static_cast<float>(height) * HammerEngine::TILE_SIZE;  // Convert tiles to pixels

    return true;
}

void WorldManager::initializeWorldResources() {
    if (!m_currentWorld || m_currentWorld->grid.empty()) {
        WORLD_MANAGER_WARN("Cannot initialize resources - no world loaded");
        return;
    }

    WORLD_MANAGER_INFO(std::format("Initializing world resources for world: {}", m_currentWorld->worldId));

    // Get ResourceTemplateManager to access available resources
    const auto& resourceMgr = ResourceTemplateManager::Instance();

    // Count resources to distribute based on biomes and elevation
    int totalTiles = 0;
    int forestTiles = 0;
    int mountainTiles = 0;
    int swampTiles = 0;
    int celestialTiles = 0;
    int highElevationTiles = 0;

    // First pass: count tile types
    for (const auto& row : m_currentWorld->grid) {
        for (const auto& tile : row) {
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
                        // Desert, Ocean, and Haunted biomes don't affect base resource calculations
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
        // Calculate base resource quantities based on world size
        const int baseAmount = std::max(10, totalTiles / 20); // Scale with world size
        // Basic resources available everywhere
        auto woodHandle = resourceMgr.getHandleById("wood");
        auto ironHandle = resourceMgr.getHandleById("iron_ore");
        auto goldHandle = resourceMgr.getHandleById("gold");

        if (woodHandle.isValid()) {
            const int woodAmount = baseAmount + forestTiles * 2; // More wood in forests
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, woodHandle, woodAmount);
            WORLD_MANAGER_DEBUG(std::format("Added {} wood to world", woodAmount));
        }

        if (ironHandle.isValid()) {
            const int ironAmount = baseAmount + mountainTiles; // More iron in mountains
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, ironHandle, ironAmount);
            WORLD_MANAGER_DEBUG(std::format("Added {} iron ore to world", ironAmount));
        }

        if (goldHandle.isValid()) {
            const int goldAmount = baseAmount * 3; // Basic starting gold
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, goldHandle, goldAmount);
            WORLD_MANAGER_DEBUG(std::format("Added {} gold to world", goldAmount));
        }

        // Rare resources based on specific biomes and elevation
        if (mountainTiles > 0) {
            auto mithrilHandle = resourceMgr.getHandleById("mithril_ore");
            if (mithrilHandle.isValid()) {
                const int mithrilAmount = std::max(1, mountainTiles / 10); // Rare resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, mithrilHandle, mithrilAmount);
                WORLD_MANAGER_DEBUG(std::format("Added {} mithril ore to world", mithrilAmount));
            }
        }

        if (forestTiles > 0) {
            auto enchantedWoodHandle = resourceMgr.getHandleById("enchanted_wood");
            if (enchantedWoodHandle.isValid()) {
                const int enchantedAmount = std::max(1, forestTiles / 15); // Rare forest resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, enchantedWoodHandle, enchantedAmount);
                WORLD_MANAGER_DEBUG(std::format("Added {} enchanted wood to world", enchantedAmount));
            }
        }

        if (celestialTiles > 0) {
            auto crystalHandle = resourceMgr.getHandleById("crystal_essence");
            if (crystalHandle.isValid()) {
                const int crystalAmount = std::max(1, celestialTiles / 8); // Celestial biome exclusive
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, crystalHandle, crystalAmount);
                WORLD_MANAGER_DEBUG(std::format("Added {} crystal essence to world", crystalAmount));
            }
        }

        if (swampTiles > 0) {
            auto voidSilkHandle = resourceMgr.getHandleById("void_silk");
            if (voidSilkHandle.isValid()) {
                const int voidSilkAmount = std::max(1, swampTiles / 20); // Very rare swamp resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, voidSilkHandle, voidSilkAmount);
                WORLD_MANAGER_DEBUG(std::format("Added {} void silk to world", voidSilkAmount));
            }
        }

        // High elevation bonuses
        if (highElevationTiles > 0) {
            auto stoneHandle = resourceMgr.getHandleById("enchanted_stone");
            if (stoneHandle.isValid()) {
                const int stoneAmount = highElevationTiles / 5; // Building materials from high areas
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, stoneHandle, stoneAmount);
                WORLD_MANAGER_DEBUG(std::format("Added {} enchanted stone to world", stoneAmount));
            }
        }

        // Energy resources based on world size
        auto arcaneEnergyHandle = resourceMgr.getHandleById("arcane_energy");
        if (arcaneEnergyHandle.isValid()) {
            const int energyAmount = totalTiles * 2; // Abundant energy resource
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, arcaneEnergyHandle, energyAmount);
            WORLD_MANAGER_DEBUG(std::format("Added {} arcane energy to world", energyAmount));
        }

        WORLD_MANAGER_INFO(std::format("World resource initialization completed for {} ({} tiles processed)",
                          m_currentWorld->worldId, totalTiles));

    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR(std::format("Error during world resource initialization: {}", ex.what()));
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
    return Season::Spring;  // Default if no renderer
}

void WorldManager::setCurrentSeason(Season season) {
    if (m_tileRenderer) {
        m_tileRenderer->setCurrentSeason(season);
    }
}

// TileRenderer Implementation

HammerEngine::TileRenderer::TileRenderer()
    : m_currentSeason(Season::Spring), m_subscribedToSeasons(false)
{
    // Initialize cached texture IDs for current season
    updateCachedTextureIDs();
}

void HammerEngine::TileRenderer::updateCachedTextureIDs()
{
    static const char* const seasonPrefixes[] = {"spring_", "summer_", "fall_", "winter_"};
    const char* const prefix = seasonPrefixes[static_cast<int>(m_currentSeason)];

    // Pre-compute all seasonal texture IDs once (eliminates ~24,000 heap allocations/frame at 4K)
    m_cachedTextureIDs.biome_default = std::string(prefix) + "biome_default";
    m_cachedTextureIDs.biome_desert = std::string(prefix) + "biome_desert";
    m_cachedTextureIDs.biome_forest = std::string(prefix) + "biome_forest";
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

    // Cache raw texture pointers for direct rendering (eliminates ~8,000 hash lookups/frame at 4K)
    auto& texMgr = TextureManager::Instance();
    auto getPtr = [&texMgr](const std::string& id) -> SDL_Texture* {
        auto tex = texMgr.getTexture(id);
        return tex ? tex.get() : nullptr;
    };

    // Cache texture pointers and dimensions (queried once here, not per-draw)
    auto cacheTexture = [&](CachedTexture& cached, const std::string& id) {
        cached.ptr = getPtr(id);
        if (cached.ptr) SDL_GetTextureSize(cached.ptr, &cached.w, &cached.h);
    };

    cacheTexture(m_cachedTextures.biome_default, m_cachedTextureIDs.biome_default);
    cacheTexture(m_cachedTextures.biome_desert, m_cachedTextureIDs.biome_desert);
    cacheTexture(m_cachedTextures.biome_forest, m_cachedTextureIDs.biome_forest);
    cacheTexture(m_cachedTextures.biome_mountain, m_cachedTextureIDs.biome_mountain);
    cacheTexture(m_cachedTextures.biome_swamp, m_cachedTextureIDs.biome_swamp);
    cacheTexture(m_cachedTextures.biome_haunted, m_cachedTextureIDs.biome_haunted);
    cacheTexture(m_cachedTextures.biome_celestial, m_cachedTextureIDs.biome_celestial);
    cacheTexture(m_cachedTextures.biome_ocean, m_cachedTextureIDs.biome_ocean);
    cacheTexture(m_cachedTextures.obstacle_water, m_cachedTextureIDs.obstacle_water);
    cacheTexture(m_cachedTextures.obstacle_tree, m_cachedTextureIDs.obstacle_tree);
    cacheTexture(m_cachedTextures.obstacle_rock, m_cachedTextureIDs.obstacle_rock);
    cacheTexture(m_cachedTextures.building_hut, m_cachedTextureIDs.building_hut);
    cacheTexture(m_cachedTextures.building_house, m_cachedTextureIDs.building_house);
    cacheTexture(m_cachedTextures.building_large, m_cachedTextureIDs.building_large);
    cacheTexture(m_cachedTextures.building_cityhall, m_cachedTextureIDs.building_cityhall);

    // Decoration textures - handle seasonal variants
    // Flowers only appear in Spring/Summer (empty string = won't render)
    const bool hasFlowers = (m_currentSeason == Season::Spring || m_currentSeason == Season::Summer);
    m_cachedTextureIDs.decoration_flower_blue = hasFlowers ? "flower_blue" : "";
    m_cachedTextureIDs.decoration_flower_pink = hasFlowers ? "flower_pink" : "";
    m_cachedTextureIDs.decoration_flower_white = hasFlowers ? "flower_white" : "";
    m_cachedTextureIDs.decoration_flower_yellow = hasFlowers ? "flower_yellow" : "";

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

    // Bushes - seasonal variants
    if (m_currentSeason == Season::Fall) {
        m_cachedTextureIDs.decoration_bush = "bush_fall";
    } else if (m_currentSeason == Season::Winter) {
        m_cachedTextureIDs.decoration_bush = "bush_winter";
    } else {
        m_cachedTextureIDs.decoration_bush = "bush_spring";
    }

    // Stumps and rocks - no seasonal variants
    m_cachedTextureIDs.decoration_stump_small = "stump_obstacle_small";
    m_cachedTextureIDs.decoration_stump_medium = "stump_obstacle_medium";
    m_cachedTextureIDs.decoration_rock_small = "obstacle_rock";

    // Cache decoration texture pointers
    cacheTexture(m_cachedTextures.decoration_flower_blue, m_cachedTextureIDs.decoration_flower_blue);
    cacheTexture(m_cachedTextures.decoration_flower_pink, m_cachedTextureIDs.decoration_flower_pink);
    cacheTexture(m_cachedTextures.decoration_flower_white, m_cachedTextureIDs.decoration_flower_white);
    cacheTexture(m_cachedTextures.decoration_flower_yellow, m_cachedTextureIDs.decoration_flower_yellow);
    cacheTexture(m_cachedTextures.decoration_mushroom_purple, m_cachedTextureIDs.decoration_mushroom_purple);
    cacheTexture(m_cachedTextures.decoration_mushroom_tan, m_cachedTextureIDs.decoration_mushroom_tan);
    cacheTexture(m_cachedTextures.decoration_grass_small, m_cachedTextureIDs.decoration_grass_small);
    cacheTexture(m_cachedTextures.decoration_grass_large, m_cachedTextureIDs.decoration_grass_large);
    cacheTexture(m_cachedTextures.decoration_bush, m_cachedTextureIDs.decoration_bush);
    cacheTexture(m_cachedTextures.decoration_stump_small, m_cachedTextureIDs.decoration_stump_small);
    cacheTexture(m_cachedTextures.decoration_stump_medium, m_cachedTextureIDs.decoration_stump_medium);
    cacheTexture(m_cachedTextures.decoration_rock_small, m_cachedTextureIDs.decoration_rock_small);

    // Validate critical textures to catch missing seasonal assets early
    if (!m_cachedTextures.biome_default.ptr) {
        WORLD_MANAGER_ERROR(std::format("TileRenderer: Missing biome_default texture for season {}",
                           static_cast<int>(m_currentSeason)));
    }
    if (!m_cachedTextures.obstacle_water.ptr) {
        WORLD_MANAGER_ERROR(std::format("TileRenderer: Missing obstacle_water texture for season {}",
                           static_cast<int>(m_currentSeason)));
    }
}

void HammerEngine::TileRenderer::setCurrentSeason(Season season)
{
    if (m_currentSeason != season) {
        m_currentSeason = season;
        updateCachedTextureIDs();
        clearChunkCache();  // Season change requires re-rendering all chunks with new textures
    }
}

void HammerEngine::TileRenderer::invalidateChunk(int chunkX, int chunkY)
{
    const uint64_t key = makeChunkKey(chunkX, chunkY);
    auto it = m_chunkCache.find(key);
    if (it != m_chunkCache.end()) {
        it->second.dirty = true;
    }
}

void HammerEngine::TileRenderer::clearChunkCache()
{
    // Defer cache clear to render thread to prevent Metal command encoder crash
    // Update thread calls this during season change events while render may be using textures
    m_cachePendingClear.store(true, std::memory_order_release);
    WORLD_MANAGER_DEBUG("TileRenderer: Chunk cache invalidated (deferred clear pending)");
}

HammerEngine::TileRenderer::~TileRenderer()
{
    unsubscribeFromSeasonEvents();
}

void HammerEngine::TileRenderer::subscribeToSeasonEvents()
{
    if (m_subscribedToSeasons) {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    m_seasonToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Time,
        [this](const EventData& data) { onSeasonChange(data); }
    );
    m_subscribedToSeasons = true;

    // Initialize with current season from GameTime
    m_currentSeason = GameTimeManager::Instance().getSeason();
    WORLD_MANAGER_INFO(std::format("TileRenderer subscribed to season events, current season: {}",
                       GameTimeManager::Instance().getSeasonName()));
}

void HammerEngine::TileRenderer::unsubscribeFromSeasonEvents()
{
    if (!m_subscribedToSeasons) {
        return;
    }

    auto& eventMgr = EventManager::Instance();
    eventMgr.removeHandler(m_seasonToken);
    m_subscribedToSeasons = false;
    WORLD_MANAGER_DEBUG("TileRenderer unsubscribed from season events");
}

void HammerEngine::TileRenderer::onSeasonChange(const EventData& data)
{
    if (!data.event) {
        return;
    }

    // Match TimeController pattern - use static_pointer_cast + getTimeEventType()
    const auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
    if (timeEvent->getTimeEventType() != TimeEventType::SeasonChanged) {
        return;
    }

    const auto seasonEvent = std::static_pointer_cast<SeasonChangedEvent>(data.event);
    const Season newSeason = seasonEvent->getSeason();
    if (newSeason != m_currentSeason) {
        WORLD_MANAGER_INFO(std::format("TileRenderer: Season changed to {}", seasonEvent->getSeasonName()));
        setCurrentSeason(newSeason);  // This updates m_currentSeason AND refreshes cached texture IDs
    }
}

std::string HammerEngine::TileRenderer::getSeasonalTextureID(const std::string& baseID) const
{
    static const char* const seasonPrefixes[] = {"spring_", "summer_", "fall_", "winter_"};
    return seasonPrefixes[static_cast<int>(m_currentSeason)] + baseID;
}

void HammerEngine::TileRenderer::renderChunkToTexture(const HammerEngine::WorldData& world, SDL_Renderer* renderer,
                                                       int chunkX, int chunkY, SDL_Texture* target) {
    // Set render target to chunk texture
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    // Calculate tile range for this chunk (extended by 1 tile for padding fill)
    const int worldWidth = static_cast<int>(world.grid[0].size());
    const int worldHeight = static_cast<int>(world.grid.size());
    const int startTileX = chunkX * CHUNK_SIZE;
    const int startTileY = chunkY * CHUNK_SIZE;
    const int endTileX = std::min(startTileX + CHUNK_SIZE, worldWidth);
    const int endTileY = std::min(startTileY + CHUNK_SIZE, worldHeight);

    // Extended range for biome rendering (fills padding area)
    const int extStartX = std::max(0, startTileX - 1);
    const int extStartY = std::max(0, startTileY - 1);
    const int extEndX = std::min(worldWidth, endTileX + 1);
    const int extEndY = std::min(worldHeight, endTileY + 1);

    constexpr int tileSize = static_cast<int>(TILE_SIZE);

    // LAYER 1: Biomes - fill entire texture including padding
    for (int y = extStartY; y < extEndY; ++y) {
        for (int x = extStartX; x < extEndX; ++x) {
            const HammerEngine::Tile& tile = world.grid[y][x];
            // Biomes render without SPRITE_OVERHANG offset for padding fill
            const float localX = static_cast<float>((x - startTileX) * tileSize + SPRITE_OVERHANG);
            const float localY = static_cast<float>((y - startTileY) * tileSize + SPRITE_OVERHANG);

            const CachedTexture* tex = &m_cachedTextures.biome_default;
            if (tile.isWater) {
                tex = &m_cachedTextures.obstacle_water;
            } else {
                switch (tile.biome) {
                    case HammerEngine::Biome::DESERT:    tex = &m_cachedTextures.biome_desert; break;
                    case HammerEngine::Biome::FOREST:   tex = &m_cachedTextures.biome_forest; break;
                    case HammerEngine::Biome::MOUNTAIN: tex = &m_cachedTextures.biome_mountain; break;
                    case HammerEngine::Biome::SWAMP:    tex = &m_cachedTextures.biome_swamp; break;
                    case HammerEngine::Biome::HAUNTED:  tex = &m_cachedTextures.biome_haunted; break;
                    case HammerEngine::Biome::CELESTIAL: tex = &m_cachedTextures.biome_celestial; break;
                    case HammerEngine::Biome::OCEAN:    tex = &m_cachedTextures.biome_ocean; break;
                    default: break;
                }
            }
            // CRITICAL: Use tileSize,tileSize for biomes - NOT tex->w,tex->h
            // This ensures exact grid alignment regardless of source texture size
            TextureManager::drawTileDirect(tex->ptr, localX, localY, tileSize, tileSize, renderer);
        }
    }

    // LAYER 2: Decorations (ground-level, rendered before Y-sorted obstacles)
    for (int y = startTileY; y < endTileY; ++y) {
        for (int x = startTileX; x < endTileX; ++x) {
            const HammerEngine::Tile& tile = world.grid[y][x];

            if (tile.decorationType == HammerEngine::DecorationType::NONE) {
                continue;
            }

            const float localX = static_cast<float>((x - startTileX) * tileSize + SPRITE_OVERHANG);
            const float localY = static_cast<float>((y - startTileY) * tileSize + SPRITE_OVERHANG);

            const CachedTexture* tex = nullptr;
            switch (tile.decorationType) {
                case HammerEngine::DecorationType::FLOWER_BLUE:
                    tex = &m_cachedTextures.decoration_flower_blue; break;
                case HammerEngine::DecorationType::FLOWER_PINK:
                    tex = &m_cachedTextures.decoration_flower_pink; break;
                case HammerEngine::DecorationType::FLOWER_WHITE:
                    tex = &m_cachedTextures.decoration_flower_white; break;
                case HammerEngine::DecorationType::FLOWER_YELLOW:
                    tex = &m_cachedTextures.decoration_flower_yellow; break;
                case HammerEngine::DecorationType::MUSHROOM_PURPLE:
                    tex = &m_cachedTextures.decoration_mushroom_purple; break;
                case HammerEngine::DecorationType::MUSHROOM_TAN:
                    tex = &m_cachedTextures.decoration_mushroom_tan; break;
                case HammerEngine::DecorationType::GRASS_SMALL:
                    tex = &m_cachedTextures.decoration_grass_small; break;
                case HammerEngine::DecorationType::GRASS_LARGE:
                    tex = &m_cachedTextures.decoration_grass_large; break;
                case HammerEngine::DecorationType::BUSH:
                    tex = &m_cachedTextures.decoration_bush; break;
                case HammerEngine::DecorationType::STUMP_SMALL:
                    tex = &m_cachedTextures.decoration_stump_small; break;
                case HammerEngine::DecorationType::STUMP_MEDIUM:
                    tex = &m_cachedTextures.decoration_stump_medium; break;
                case HammerEngine::DecorationType::ROCK_SMALL:
                    tex = &m_cachedTextures.decoration_rock_small; break;
                default:
                    continue;
            }

            // Skip if texture not loaded (e.g., flowers in winter have empty texture ID)
            if (!tex || !tex->ptr) {
                continue;
            }

            // Center decoration horizontally, align bottom to tile bottom
            const float offsetX = (TILE_SIZE - tex->w) / 2.0f;
            const float offsetY = TILE_SIZE - tex->h;

            TextureManager::drawTileDirect(tex->ptr, localX + offsetX, localY + offsetY,
                                           static_cast<int>(tex->w), static_cast<int>(tex->h), renderer);
        }
    }

    // LAYER 3: Collect obstacles and buildings for Y-sorted rendering
    // Extended range to capture sprites that overhang into this chunk from adjacent tiles
    const int spriteStartX = std::max(0, startTileX - 2);  // 2 tiles for building overhang
    const int spriteStartY = std::max(0, startTileY - 4);  // 4 tiles for tall sprites above
    const int spriteEndX = std::min(worldWidth, endTileX + 2);
    const int spriteEndY = std::min(worldHeight, endTileY + 1);

    m_ySortBuffer.clear();

    for (int y = spriteStartY; y < spriteEndY; ++y) {
        for (int x = spriteStartX; x < spriteEndX; ++x) {
            const HammerEngine::Tile& tile = world.grid[y][x];

            // Calculate local position in chunk texture
            const float localX = static_cast<float>((x - startTileX) * tileSize + SPRITE_OVERHANG);
            const float localY = static_cast<float>((y - startTileY) * tileSize + SPRITE_OVERHANG);

            const bool isPartOfBuilding = (tile.obstacleType == HammerEngine::ObstacleType::BUILDING &&
                                     !tile.isTopLeftOfBuilding);

            // Obstacles (trees, rocks) - bottom-center positioned
            if (!isPartOfBuilding &&
                tile.obstacleType != HammerEngine::ObstacleType::NONE &&
                tile.obstacleType != HammerEngine::ObstacleType::BUILDING) {

                const CachedTexture* tex = &m_cachedTextures.biome_default;
                switch (tile.obstacleType) {
                    case HammerEngine::ObstacleType::TREE:  tex = &m_cachedTextures.obstacle_tree; break;
                    case HammerEngine::ObstacleType::ROCK:  tex = &m_cachedTextures.obstacle_rock; break;
                    case HammerEngine::ObstacleType::WATER: continue;  // Water is biome layer
                    default: break;
                }

                const float offsetX = (TILE_SIZE - tex->w) / 2.0f;
                const float offsetY = TILE_SIZE - tex->h;

                // Y-sort key is bottom of sprite (tile Y + 1 tile = bottom)
                const float sortY = localY + TILE_SIZE;

                m_ySortBuffer.push_back({sortY, localX + offsetX, localY + offsetY, tex, false, 0, 0});
            }

            // Buildings - from top-left tile only
            if (tile.obstacleType == HammerEngine::ObstacleType::BUILDING && tile.isTopLeftOfBuilding) {
                const CachedTexture* tex = &m_cachedTextures.building_hut;
                switch (tile.buildingSize) {
                    case 1:  tex = &m_cachedTextures.building_hut; break;
                    case 2:  tex = &m_cachedTextures.building_house; break;
                    case 3:  tex = &m_cachedTextures.building_large; break;
                    case 4:  tex = &m_cachedTextures.building_cityhall; break;
                    default: break;
                }

                // Y-sort key is bottom of building footprint
                float sortY = localY + (tile.buildingSize * TILE_SIZE);

                m_ySortBuffer.push_back({sortY, localX, localY, tex, true,
                                         static_cast<int>(tex->w), static_cast<int>(tex->h)});
            }
        }
    }

    // Sort by Y (bottom of sprite) for proper layering
    std::sort(m_ySortBuffer.begin(), m_ySortBuffer.end(),
              [](const YSortedSprite& a, const YSortedSprite& b) { return a.y < b.y; });

    // Render sorted sprites into chunk texture
    for (const auto& sprite : m_ySortBuffer) {
        const int spriteW = sprite.isBuilding ? sprite.buildingWidth : static_cast<int>(sprite.tex->w);
        const int spriteH = sprite.isBuilding ? sprite.buildingHeight : static_cast<int>(sprite.tex->h);
        TextureManager::drawTileDirect(sprite.tex->ptr, sprite.renderX, sprite.renderY,
                                       spriteW, spriteH, renderer);
    }

    // Reset render target to default (screen)
    SDL_SetRenderTarget(renderer, nullptr);
}

void HammerEngine::TileRenderer::renderVisibleTiles(const HammerEngine::WorldData& world, SDL_Renderer* renderer,
                                     float cameraX, float cameraY, int viewportWidth, int viewportHeight) {
    if (world.grid.empty()) {
        WORLD_MANAGER_WARN("TileRenderer: Cannot render tiles - world grid is empty");
        return;
    }

    if (!renderer) {
        WORLD_MANAGER_ERROR("TileRenderer: Cannot render tiles - renderer is null");
        return;
    }

    // Process deferred cache clear safely on render thread (before any texture access)
    // This prevents Metal command encoder crash when season change clears cache during render
    if (m_cachePendingClear.exchange(false, std::memory_order_acq_rel)) {
        m_chunkCache.clear();
        WORLD_MANAGER_DEBUG("TileRenderer: Chunk cache cleared (deferred)");
    }

    // Increment frame counter for LRU tracking
    ++m_frameCounter;

    // Calculate visible chunk range
    const int worldWidth = static_cast<int>(world.grid[0].size());
    const int worldHeight = static_cast<int>(world.grid.size());
    const int maxChunkX = (worldWidth + CHUNK_SIZE - 1) / CHUNK_SIZE;
    const int maxChunkY = (worldHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

    const int startChunkX = std::max(0, static_cast<int>(cameraX / (CHUNK_SIZE * TILE_SIZE)));
    const int startChunkY = std::max(0, static_cast<int>(cameraY / (CHUNK_SIZE * TILE_SIZE)));
    const int endChunkX = std::min(maxChunkX, static_cast<int>((cameraX + viewportWidth) / (CHUNK_SIZE * TILE_SIZE)) + 1);
    const int endChunkY = std::min(maxChunkY, static_cast<int>((cameraY + viewportHeight) / (CHUNK_SIZE * TILE_SIZE)) + 1);

    // Chunk texture size includes padding for sprites extending beyond tile bounds
    constexpr int chunkPixelSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE) + SPRITE_OVERHANG * 2;

    // Track currently visible chunk keys for LRU eviction (uses member buffer to avoid per-frame allocs)
    m_visibleKeysBuffer.clear();

    // Render visible chunks (typically 4-16 chunks vs 8000 individual tiles)
    for (int chunkY = startChunkY; chunkY <= endChunkY; ++chunkY) {
        for (int chunkX = startChunkX; chunkX <= endChunkX; ++chunkX) {
            const uint64_t key = makeChunkKey(chunkX, chunkY);
            m_visibleKeysBuffer.push_back(key);

            // Get or create chunk cache entry
            auto it = m_chunkCache.find(key);
            if (it == m_chunkCache.end()) {
                // Create new chunk texture
                SDL_Texture* tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                     SDL_TEXTUREACCESS_TARGET, chunkPixelSize, chunkPixelSize);
                if (!tex) {
                    WORLD_MANAGER_ERROR("Failed to create chunk texture");
                    continue;
                }
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

                ChunkCache cache;
                cache.texture = std::shared_ptr<SDL_Texture>(tex, SDL_DestroyTexture);
                cache.dirty = true;
                cache.lastUsedFrame = m_frameCounter;
                it = m_chunkCache.emplace(key, std::move(cache)).first;
            }

            ChunkCache& chunk = it->second;
            chunk.lastUsedFrame = m_frameCounter;  // Update LRU timestamp

            // Re-render chunk if dirty
            if (chunk.dirty && chunk.texture) {
                renderChunkToTexture(world, renderer, chunkX, chunkY, chunk.texture.get());
                chunk.dirty = false;
            }

            // Render chunk texture to screen with source rect to prevent overlap/double-blend
            if (chunk.texture) {
                constexpr int chunkWorldSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE);

                // Default: full chunk texture
                float srcX = 0;
                float srcY = 0;
                float srcW = static_cast<float>(chunkPixelSize);
                float srcH = static_cast<float>(chunkPixelSize);

                float screenX = static_cast<float>(chunkX * chunkWorldSize) - cameraX - SPRITE_OVERHANG;
                float screenY = static_cast<float>(chunkY * chunkWorldSize) - cameraY - SPRITE_OVERHANG;
                float destW = srcW;
                float destH = srcH;

                // If not leftmost visible chunk, exclude left padding (already drawn by chunk to left)
                if (chunkX > startChunkX) {
                    srcX = SPRITE_OVERHANG;
                    srcW -= SPRITE_OVERHANG;
                    screenX += SPRITE_OVERHANG;
                    destW = srcW;
                }

                // If not topmost visible chunk, exclude top padding (already drawn by chunk above)
                if (chunkY > startChunkY) {
                    srcY = SPRITE_OVERHANG;
                    srcH -= SPRITE_OVERHANG;
                    screenY += SPRITE_OVERHANG;
                    destH = srcH;
                }

                SDL_FRect srcRect = {srcX, srcY, srcW, srcH};
                SDL_FRect destRect = {screenX, screenY, destW, destH};
                SDL_RenderTexture(renderer, chunk.texture.get(), &srcRect, &destRect);
            }
        }
    }

    // LRU eviction: Remove oldest chunks when cache exceeds limit
    if (m_chunkCache.size() > MAX_CACHED_CHUNKS) {
        // Find chunks not currently visible and sort by last used frame (uses member buffer)
        m_evictionBuffer.clear();
        for (const auto& [key, cache] : m_chunkCache) {
            // Don't evict currently visible chunks
            if (std::find(m_visibleKeysBuffer.begin(), m_visibleKeysBuffer.end(), key) == m_visibleKeysBuffer.end()) {
                m_evictionBuffer.emplace_back(key, cache.lastUsedFrame);
            }
        }

        // Sort by oldest first (lowest frame number)
        std::sort(m_evictionBuffer.begin(), m_evictionBuffer.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        // Evict oldest chunks until we're under the limit
        const size_t toEvict = m_chunkCache.size() - MAX_CACHED_CHUNKS;
        for (size_t i = 0; i < std::min(toEvict, m_evictionBuffer.size()); ++i) {
            m_chunkCache.erase(m_evictionBuffer[i].first);
        }

        // Note: toEvict is always > 0 here since we're inside the size() > MAX block
        WORLD_MANAGER_DEBUG(std::format("TileRenderer: Evicted {} chunks from cache (cache size: {})",
                           std::min(toEvict, m_evictionBuffer.size()), m_chunkCache.size()));
    }
}

HammerEngine::TileRenderer::ChunkBounds HammerEngine::TileRenderer::calculateVisibleChunks(
    float cameraX, float cameraY, int viewportWidth, int viewportHeight) const {

    // Add generous padding for chunk pre-loading (load chunks well in advance)
    const float chunkPadding = (CHUNK_SIZE * TILE_SIZE) * 2.0f;  // Load 2 full chunks ahead in each direction

    // Calculate camera bounds with expanded padding for chunk pre-loading
    const float leftBound = cameraX - (viewportWidth * 0.5f) - chunkPadding;
    const float rightBound = cameraX + (viewportWidth * 0.5f) + chunkPadding;
    const float topBound = cameraY - (viewportHeight * 0.5f) - chunkPadding;
    const float bottomBound = cameraY + (viewportHeight * 0.5f) + chunkPadding;

    // Convert to chunk coordinates
    ChunkBounds bounds;
    const int chunkPixelSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE);
    bounds.startChunkX = std::max(0, static_cast<int>(leftBound) / chunkPixelSize);
    bounds.endChunkX = static_cast<int>(rightBound) / chunkPixelSize;
    bounds.startChunkY = std::max(0, static_cast<int>(topBound) / chunkPixelSize);
    bounds.endChunkY = static_cast<int>(bottomBound) / chunkPixelSize;

    return bounds;
}

void HammerEngine::TileRenderer::renderTile(const HammerEngine::Tile& tile, SDL_Renderer* renderer, float screenX, float screenY) const {
    if (!renderer) {
        WORLD_MANAGER_ERROR("TileRenderer: Cannot render tile - renderer is null");
        return;
    }

    // LAYER 1: Always render biome texture as the base layer (with seasonal variant)
    std::string biomeTextureID;
    if (tile.isWater) {
        biomeTextureID = getSeasonalTextureID("obstacle_water");
    } else {
        biomeTextureID = getSeasonalTextureID(getBiomeTexture(tile.biome));
    }

    // Render base biome layer
    TextureManager::Instance().drawTileF(biomeTextureID, screenX, screenY, TILE_SIZE, TILE_SIZE, renderer);

    // LAYER 2: Render obstacles on top of biome (if present) with seasonal variant
    if (tile.obstacleType != HammerEngine::ObstacleType::NONE) {
        const std::string obstacleTextureID = getSeasonalTextureID(getObstacleTexture(tile.obstacleType));

        // Render obstacle layer on top of biome
        TextureManager::Instance().drawTileF(obstacleTextureID, screenX, screenY, TILE_SIZE, TILE_SIZE, renderer);
    }

    // Debug logging for texture issues (only in debug builds)
    #ifdef DEBUG
    if (biomeTextureID.empty()) {
        WORLD_MANAGER_WARN(std::format("TileRenderer: Empty biome texture ID for tile at screen position ({}, {})",
                           screenX, screenY));
    }
    #endif
}

std::string HammerEngine::TileRenderer::getBiomeTexture(HammerEngine::Biome biome) const {
    switch (biome) {
        case HammerEngine::Biome::DESERT:     return "biome_desert";
        case HammerEngine::Biome::FOREST:     return "biome_forest";
        case HammerEngine::Biome::MOUNTAIN:   return "biome_mountain";
        case HammerEngine::Biome::SWAMP:      return "biome_swamp";
        case HammerEngine::Biome::HAUNTED:    return "biome_haunted";
        case HammerEngine::Biome::CELESTIAL:  return "biome_celestial";
        case HammerEngine::Biome::OCEAN:      return "biome_ocean";
        default:                return "biome_default";
    }
}

std::string HammerEngine::TileRenderer::getObstacleTexture(HammerEngine::ObstacleType obstacle) const {
    switch (obstacle) {
        case HammerEngine::ObstacleType::TREE:    return "obstacle_tree";
        case HammerEngine::ObstacleType::ROCK:    return "obstacle_rock";
        case HammerEngine::ObstacleType::WATER:   return "obstacle_water";
        case HammerEngine::ObstacleType::BUILDING: return "building_hut"; // Default to hut texture
        default:                    return "biome_default";
    }
}
