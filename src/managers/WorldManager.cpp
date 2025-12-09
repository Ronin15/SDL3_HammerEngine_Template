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
        WORLD_MANAGER_ERROR("WorldManager::init - Exception: " + std::string(ex.what()));
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
        WORLD_MANAGER_ERROR("WorldManager::setupEventHandlers - Exception: " + std::string(ex.what()));
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
            WORLD_MANAGER_INFO("Unloading current world: " + m_currentWorld->worldId);
        }

        // Set new world
        m_currentWorld = std::move(newWorld);

        // Register world with WorldResourceManager
        WorldResourceManager::Instance().createWorld(m_currentWorld->worldId);

        // Initialize world resources based on world data
        initializeWorldResources();

        WORLD_MANAGER_INFO("Successfully loaded new world: " + m_currentWorld->worldId);

        // Schedule world loaded event for next frame using ThreadSystem
        // Don't fire event while holding world mutex - use low priority to avoid blocking critical tasks
        std::string worldIdCopy = m_currentWorld->worldId;
        // Schedule world loaded event for next frame using ThreadSystem to avoid deadlocks
        // Use high priority to ensure it executes quickly for tests
        WORLD_MANAGER_INFO("Enqueuing WorldLoadedEvent task for world: " + worldIdCopy);
        HammerEngine::ThreadSystem::Instance().enqueueTask([worldIdCopy, this]() {
            WORLD_MANAGER_INFO("Executing WorldLoadedEvent task for world: " + worldIdCopy);
            fireWorldLoadedEvent(worldIdCopy);
        }, HammerEngine::TaskPriority::High, "WorldLoadedEvent_" + worldIdCopy);

        return true;
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("WorldManager::loadNewWorld - Exception: " + std::string(ex.what()));
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
        WORLD_MANAGER_INFO("Unloading world: " + worldId);

        // Fire world unloaded event before clearing the world
        fireWorldUnloadedEvent(worldId);

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

    int height = static_cast<int>(m_currentWorld->grid.size());
    int width = height > 0 ? static_cast<int>(m_currentWorld->grid[0].size()) : 0;

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
        WORLD_MANAGER_ERROR("Invalid harvest position: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ")");
        return false;
    }

    HammerEngine::Tile& tile = m_currentWorld->grid[targetY][targetX];

    if (tile.obstacleType == HammerEngine::ObstacleType::NONE) {
        WORLD_MANAGER_WARN("No harvestable resource at position: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ")");
        return false;
    }

    // Store the original obstacle type for resource tracking
    HammerEngine::ObstacleType harvestedType = tile.obstacleType;
    (void)harvestedType; // Suppress unused warning

    // Remove the obstacle
    tile.obstacleType = HammerEngine::ObstacleType::NONE;
    tile.resourceHandle = HammerEngine::ResourceHandle{};

    // Fire tile changed event
    fireTileChangedEvent(targetX, targetY, tile);

    // Notify WorldResourceManager about resource depletion
    // This is a placeholder - actual resource tracking would need proper resource handles

    WORLD_MANAGER_INFO("Resource harvested at (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ") by entity " + std::to_string(entityId));
    return true;
}

bool WorldManager::updateTile(int x, int y, const HammerEngine::Tile& newTile) {
    if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld) {
        WORLD_MANAGER_ERROR("WorldManager not initialized or no active world");
        return false;
    }

    std::lock_guard<std::shared_mutex> lock(m_worldMutex);

    if (!isValidPosition(x, y)) {
        WORLD_MANAGER_ERROR("Invalid tile position: (" + std::to_string(x) + ", " + std::to_string(y) + ")");
        return false;
    }

    m_currentWorld->grid[y][x] = newTile;
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

        WORLD_MANAGER_DEBUG("TileChangedEvent fired for tile at (" + std::to_string(x) + ", " + std::to_string(y) + ")");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire TileChangedEvent: " + std::string(ex.what()));
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

        WORLD_MANAGER_INFO("WorldLoadedEvent registered and executed for world: " + worldId + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire WorldLoadedEvent: " + std::string(ex.what()));
    }
}

void WorldManager::fireWorldUnloadedEvent(const std::string& worldId) {
    try {
        // Trigger world unloaded via EventManager (no registration)
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerWorldUnloaded(worldId, EventManager::DispatchMode::Deferred);

        WORLD_MANAGER_INFO("WorldUnloadedEvent fired for world: " + worldId);
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire WorldUnloadedEvent: " + std::string(ex.what()));
    }
}

void WorldManager::registerEventHandlers() {
    try {
        EventManager& eventMgr = EventManager::Instance();
        m_handlerTokens.clear();

        // Register handler for world events (to respond to events from other systems)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::World, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle world-related events from other systems
                WORLD_MANAGER_DEBUG("WorldManager received world event: " + data.event->getName());
            }
        }));

        // Register handler for camera events (world bounds may affect camera)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::Camera, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle camera events that may require world data updates
                WORLD_MANAGER_DEBUG("WorldManager received camera event: " + data.event->getName());
            }
        }));

        // Register handler for resource change events (resource changes may affect world state)
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::ResourceChange, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle resource changes that may affect world generation or state
                auto resourceEvent = std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
                if (resourceEvent) {
                    WORLD_MANAGER_DEBUG("WorldManager received resource change: " +
                                       resourceEvent->getResourceHandle().toString() +
                                       " changed by " + std::to_string(resourceEvent->getQuantityChange()));
                }
            }
        }));

        // Register handler for harvest resource events
        m_handlerTokens.push_back(eventMgr.registerHandlerWithToken(EventTypeId::Harvest, [this](const EventData& data) {
            if (data.isActive() && data.event) {
                auto harvestEvent = std::dynamic_pointer_cast<HarvestResourceEvent>(data.event);
                if (harvestEvent) {
                    WORLD_MANAGER_DEBUG("WorldManager received harvest request from entity " +
                                       std::to_string(harvestEvent->getEntityId()) +
                                       " at (" + std::to_string(harvestEvent->getTargetX()) +
                                       ", " + std::to_string(harvestEvent->getTargetY()) + ")");

                    // Handle the harvest request
                    handleHarvestResource(harvestEvent->getEntityId(),
                                        harvestEvent->getTargetX(),
                                        harvestEvent->getTargetY());
                }
            }
        }));

        WORLD_MANAGER_DEBUG("WorldManager event handlers registered");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to register event handlers: " + std::string(ex.what()));
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
        WORLD_MANAGER_ERROR("Failed to unregister event handlers: " + std::string(ex.what()));
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

    WORLD_MANAGER_INFO("Initializing world resources for world: " + m_currentWorld->worldId);

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
        int baseAmount = std::max(10, totalTiles / 20); // Scale with world size
        // Basic resources available everywhere
        auto woodHandle = resourceMgr.getHandleById("wood");
        auto ironHandle = resourceMgr.getHandleById("iron_ore");
        auto goldHandle = resourceMgr.getHandleById("gold");

        if (woodHandle.isValid()) {
            int woodAmount = baseAmount + forestTiles * 2; // More wood in forests
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, woodHandle, woodAmount);
            WORLD_MANAGER_DEBUG("Added " + std::to_string(woodAmount) + " wood to world");
        }

        if (ironHandle.isValid()) {
            int ironAmount = baseAmount + mountainTiles; // More iron in mountains
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, ironHandle, ironAmount);
            WORLD_MANAGER_DEBUG("Added " + std::to_string(ironAmount) + " iron ore to world");
        }

        if (goldHandle.isValid()) {
            int goldAmount = baseAmount * 3; // Basic starting gold
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, goldHandle, goldAmount);
            WORLD_MANAGER_DEBUG("Added " + std::to_string(goldAmount) + " gold to world");
        }

        // Rare resources based on specific biomes and elevation
        if (mountainTiles > 0) {
            auto mithrilHandle = resourceMgr.getHandleById("mithril_ore");
            if (mithrilHandle.isValid()) {
                int mithrilAmount = std::max(1, mountainTiles / 10); // Rare resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, mithrilHandle, mithrilAmount);
                WORLD_MANAGER_DEBUG("Added " + std::to_string(mithrilAmount) + " mithril ore to world");
            }
        }

        if (forestTiles > 0) {
            auto enchantedWoodHandle = resourceMgr.getHandleById("enchanted_wood");
            if (enchantedWoodHandle.isValid()) {
                int enchantedAmount = std::max(1, forestTiles / 15); // Rare forest resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, enchantedWoodHandle, enchantedAmount);
                WORLD_MANAGER_DEBUG("Added " + std::to_string(enchantedAmount) + " enchanted wood to world");
            }
        }

        if (celestialTiles > 0) {
            auto crystalHandle = resourceMgr.getHandleById("crystal_essence");
            if (crystalHandle.isValid()) {
                int crystalAmount = std::max(1, celestialTiles / 8); // Celestial biome exclusive
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, crystalHandle, crystalAmount);
                WORLD_MANAGER_DEBUG("Added " + std::to_string(crystalAmount) + " crystal essence to world");
            }
        }

        if (swampTiles > 0) {
            auto voidSilkHandle = resourceMgr.getHandleById("void_silk");
            if (voidSilkHandle.isValid()) {
                int voidSilkAmount = std::max(1, swampTiles / 20); // Very rare swamp resource
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, voidSilkHandle, voidSilkAmount);
                WORLD_MANAGER_DEBUG("Added " + std::to_string(voidSilkAmount) + " void silk to world");
            }
        }

        // High elevation bonuses
        if (highElevationTiles > 0) {
            auto stoneHandle = resourceMgr.getHandleById("enchanted_stone");
            if (stoneHandle.isValid()) {
                int stoneAmount = highElevationTiles / 5; // Building materials from high areas
                WorldResourceManager::Instance().addResource(m_currentWorld->worldId, stoneHandle, stoneAmount);
                WORLD_MANAGER_DEBUG("Added " + std::to_string(stoneAmount) + " enchanted stone to world");
            }
        }

        // Energy resources based on world size
        auto arcaneEnergyHandle = resourceMgr.getHandleById("arcane_energy");
        if (arcaneEnergyHandle.isValid()) {
            int energyAmount = totalTiles * 2; // Abundant energy resource
            WorldResourceManager::Instance().addResource(m_currentWorld->worldId, arcaneEnergyHandle, energyAmount);
            WORLD_MANAGER_DEBUG("Added " + std::to_string(energyAmount) + " arcane energy to world");
        }

        WORLD_MANAGER_INFO("World resource initialization completed for " + m_currentWorld->worldId +
                          " (" + std::to_string(totalTiles) + " tiles processed)");

    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Error during world resource initialization: " + std::string(ex.what()));
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
    static const char* seasonPrefixes[] = {"spring_", "summer_", "fall_", "winter_"};
    const char* prefix = seasonPrefixes[static_cast<int>(m_currentSeason)];

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

    // Validate critical textures to catch missing seasonal assets early
    if (!m_cachedTextures.biome_default.ptr) {
        WORLD_MANAGER_ERROR("TileRenderer: Missing biome_default texture for season " +
                           std::to_string(static_cast<int>(m_currentSeason)));
    }
    if (!m_cachedTextures.obstacle_water.ptr) {
        WORLD_MANAGER_ERROR("TileRenderer: Missing obstacle_water texture for season " +
                           std::to_string(static_cast<int>(m_currentSeason)));
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
    uint64_t key = makeChunkKey(chunkX, chunkY);
    auto it = m_chunkCache.find(key);
    if (it != m_chunkCache.end()) {
        it->second.dirty = true;
    }
}

void HammerEngine::TileRenderer::clearChunkCache()
{
    m_chunkCache.clear();
    WORLD_MANAGER_DEBUG("TileRenderer: Chunk cache cleared");
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
    m_currentSeason = GameTime::Instance().getSeason();
    WORLD_MANAGER_INFO("TileRenderer subscribed to season events, current season: " +
                       std::string(GameTime::Instance().getSeasonName()));
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
    auto timeEvent = std::static_pointer_cast<TimeEvent>(data.event);
    if (timeEvent->getTimeEventType() != TimeEventType::SeasonChanged) {
        return;
    }

    auto seasonEvent = std::static_pointer_cast<SeasonChangedEvent>(data.event);
    Season newSeason = seasonEvent->getSeason();
    if (newSeason != m_currentSeason) {
        WORLD_MANAGER_INFO("TileRenderer: Season changed to " + seasonEvent->getSeasonName());
        setCurrentSeason(newSeason);  // This updates m_currentSeason AND refreshes cached texture IDs
    }
}

std::string HammerEngine::TileRenderer::getSeasonalTextureID(const std::string& baseID) const
{
    static const char* seasonPrefixes[] = {"spring_", "summer_", "fall_", "winter_"};
    return seasonPrefixes[static_cast<int>(m_currentSeason)] + baseID;
}

void HammerEngine::TileRenderer::renderChunkToTexture(const HammerEngine::WorldData& world, SDL_Renderer* renderer,
                                                       int chunkX, int chunkY, SDL_Texture* target) {
    // Set render target to chunk texture
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    // Calculate tile range for this chunk (extended by 1 tile for padding fill)
    int worldWidth = static_cast<int>(world.grid[0].size());
    int worldHeight = static_cast<int>(world.grid.size());
    int startTileX = chunkX * CHUNK_SIZE;
    int startTileY = chunkY * CHUNK_SIZE;
    int endTileX = std::min(startTileX + CHUNK_SIZE, worldWidth);
    int endTileY = std::min(startTileY + CHUNK_SIZE, worldHeight);

    constexpr int tileSize = static_cast<int>(TILE_SIZE);

    // LAYER 1: Biomes - render at (0,0) to fill texture edge-to-edge
    // No padding offset needed since sprites are rendered separately (Pass 2)
    // This prevents transparent pixel bleed at chunk boundaries
    for (int y = startTileY; y < endTileY; ++y) {
        for (int x = startTileX; x < endTileX; ++x) {
            const HammerEngine::Tile& tile = world.grid[y][x];
            // Render biomes from (0,0) - no padding offset
            float localX = static_cast<float>((x - startTileX) * tileSize);
            float localY = static_cast<float>((y - startTileY) * tileSize);

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
            TextureManager::drawTileDirect(tex->ptr, localX, localY, static_cast<int>(tex->w), static_cast<int>(tex->h), renderer);
        }
    }

    // NOTE: Obstacles and buildings are rendered in a separate pass directly to screen
    // (after all chunk biomes are composited) to avoid chunk boundary clipping issues.
    // See renderVisibleTiles() PASS 2 for sprite rendering.

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

    // Increment frame counter for LRU tracking
    ++m_frameCounter;

    // Calculate visible chunk range
    int worldWidth = static_cast<int>(world.grid[0].size());
    int worldHeight = static_cast<int>(world.grid.size());
    int maxChunkX = (worldWidth + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int maxChunkY = (worldHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

    int startChunkX = std::max(0, static_cast<int>(cameraX / (CHUNK_SIZE * TILE_SIZE)));
    int startChunkY = std::max(0, static_cast<int>(cameraY / (CHUNK_SIZE * TILE_SIZE)));
    int endChunkX = std::min(maxChunkX, static_cast<int>((cameraX + viewportWidth) / (CHUNK_SIZE * TILE_SIZE)) + 1);
    int endChunkY = std::min(maxChunkY, static_cast<int>((cameraY + viewportHeight) / (CHUNK_SIZE * TILE_SIZE)) + 1);

    // Chunk texture size includes padding for sprites extending beyond tile bounds
    constexpr int chunkPixelSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE) + SPRITE_OVERHANG * 2;

    // Track currently visible chunk keys for LRU eviction
    std::vector<uint64_t> visibleKeys;

    // Render visible chunks (typically 4-16 chunks vs 8000 individual tiles)
    for (int chunkY = startChunkY; chunkY <= endChunkY; ++chunkY) {
        for (int chunkX = startChunkX; chunkX <= endChunkX; ++chunkX) {
            uint64_t key = makeChunkKey(chunkX, chunkY);
            visibleKeys.push_back(key);

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

            // Render chunk texture to screen - biomes fill from (0,0)
            if (chunk.texture) {
                constexpr int chunkWorldSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE);

                // Source rect: biomes are rendered from (0,0) in the texture
                SDL_FRect srcRect = {
                    0.0f,
                    0.0f,
                    static_cast<float>(chunkWorldSize),
                    static_cast<float>(chunkWorldSize)
                };

                // Dest position: chunks tile seamlessly
                float screenX = static_cast<float>(chunkX * chunkWorldSize) - cameraX;
                float screenY = static_cast<float>(chunkY * chunkWorldSize) - cameraY;

                SDL_FRect destRect = {screenX, screenY,
                                      static_cast<float>(chunkWorldSize),
                                      static_cast<float>(chunkWorldSize)};
                SDL_RenderTexture(renderer, chunk.texture.get(), &srcRect, &destRect);
            }
        }
    }

    // PASS 2: Render all visible sprites directly to screen
    // This ensures sprites are never covered by adjacent chunk biomes
    constexpr int tileSize = static_cast<int>(TILE_SIZE);
    int startTileX = std::max(0, static_cast<int>(cameraX / TILE_SIZE) - 1);
    int startTileY = std::max(0, static_cast<int>(cameraY / TILE_SIZE) - 1);
    int endTileX = std::min(worldWidth, static_cast<int>((cameraX + viewportWidth) / TILE_SIZE) + 2);
    int endTileY = std::min(worldHeight, static_cast<int>((cameraY + viewportHeight) / TILE_SIZE) + 2);

    for (int y = startTileY; y < endTileY; ++y) {
        for (int x = startTileX; x < endTileX; ++x) {
            const HammerEngine::Tile& tile = world.grid[y][x];

            // Skip if no obstacle
            if (tile.obstacleType == HammerEngine::ObstacleType::NONE) {
                continue;
            }

            // Calculate screen position
            float screenX = static_cast<float>(x * tileSize) - cameraX;
            float screenY = static_cast<float>(y * tileSize) - cameraY;

            // Render obstacles (trees, rocks) - bottom-center positioned
            if (tile.obstacleType != HammerEngine::ObstacleType::BUILDING) {
                const CachedTexture* tex = &m_cachedTextures.obstacle_tree;
                switch (tile.obstacleType) {
                    case HammerEngine::ObstacleType::TREE:  tex = &m_cachedTextures.obstacle_tree; break;
                    case HammerEngine::ObstacleType::ROCK:  tex = &m_cachedTextures.obstacle_rock; break;
                    case HammerEngine::ObstacleType::WATER: tex = &m_cachedTextures.obstacle_water; break;
                    default: break;
                }
                float offsetX = (tileSize - tex->w) / 2.0f;
                float offsetY = tileSize - tex->h;
                TextureManager::drawTileDirect(tex->ptr, screenX + offsetX, screenY + offsetY,
                                               static_cast<int>(tex->w), static_cast<int>(tex->h), renderer);
            }

            // Render buildings (from top-left tile only to avoid duplicates)
            if (tile.obstacleType == HammerEngine::ObstacleType::BUILDING && tile.isTopLeftOfBuilding) {
                const CachedTexture* tex = &m_cachedTextures.building_hut;
                switch (tile.buildingSize) {
                    case 1:  tex = &m_cachedTextures.building_hut; break;
                    case 2:  tex = &m_cachedTextures.building_house; break;
                    case 3:  tex = &m_cachedTextures.building_large; break;
                    case 4:  tex = &m_cachedTextures.building_cityhall; break;
                    default: break;
                }
                TextureManager::drawTileDirect(tex->ptr, screenX, screenY,
                                               static_cast<int>(tex->w), static_cast<int>(tex->h), renderer);
            }
        }
    }

    // LRU eviction: Remove oldest chunks when cache exceeds limit
    if (m_chunkCache.size() > MAX_CACHED_CHUNKS) {
        // Find chunks not currently visible and sort by last used frame
        std::vector<std::pair<uint64_t, uint64_t>> evictionCandidates;  // key, lastUsedFrame
        for (const auto& [key, cache] : m_chunkCache) {
            // Don't evict currently visible chunks
            if (std::find(visibleKeys.begin(), visibleKeys.end(), key) == visibleKeys.end()) {
                evictionCandidates.emplace_back(key, cache.lastUsedFrame);
            }
        }

        // Sort by oldest first (lowest frame number)
        std::sort(evictionCandidates.begin(), evictionCandidates.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        // Evict oldest chunks until we're under the limit
        size_t toEvict = m_chunkCache.size() - MAX_CACHED_CHUNKS;
        for (size_t i = 0; i < std::min(toEvict, evictionCandidates.size()); ++i) {
            m_chunkCache.erase(evictionCandidates[i].first);
        }

        if (toEvict > 0) {
            WORLD_MANAGER_DEBUG("TileRenderer: Evicted " + std::to_string(std::min(toEvict, evictionCandidates.size())) +
                               " chunks from cache (cache size: " + std::to_string(m_chunkCache.size()) + ")");
        }
    }
}

HammerEngine::TileRenderer::ChunkBounds HammerEngine::TileRenderer::calculateVisibleChunks(
    float cameraX, float cameraY, int viewportWidth, int viewportHeight) const {

    // Add generous padding for chunk pre-loading (load chunks well in advance)
    float chunkPadding = (CHUNK_SIZE * TILE_SIZE) * 2.0f;  // Load 2 full chunks ahead in each direction

    // Calculate camera bounds with expanded padding for chunk pre-loading
    float leftBound = cameraX - (viewportWidth * 0.5f) - chunkPadding;
    float rightBound = cameraX + (viewportWidth * 0.5f) + chunkPadding;
    float topBound = cameraY - (viewportHeight * 0.5f) - chunkPadding;
    float bottomBound = cameraY + (viewportHeight * 0.5f) + chunkPadding;

    // Convert to chunk coordinates
    ChunkBounds bounds;
    int chunkPixelSize = CHUNK_SIZE * static_cast<int>(TILE_SIZE);
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
        std::string obstacleTextureID = getSeasonalTextureID(getObstacleTexture(tile.obstacleType));

        // Render obstacle layer on top of biome
        TextureManager::Instance().drawTileF(obstacleTextureID, screenX, screenY, TILE_SIZE, TILE_SIZE, renderer);
    }

    // Debug logging for texture issues (only in debug builds)
    #ifdef DEBUG
    if (biomeTextureID.empty()) {
        WORLD_MANAGER_WARN("TileRenderer: Empty biome texture ID for tile at screen position (" + std::to_string(screenX) + ", " + std::to_string(screenY) + ")");
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
