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
#include "events/WorldEvent.hpp"
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
    unloadWorldUnsafe();  // Use unsafe version to avoid double-locking
    m_tileRenderer.reset();
    
    m_initialized.store(false, std::memory_order_release);
    m_isShutdown = true;
    
    WORLD_MANAGER_INFO("WorldManager cleaned up");
}

bool WorldManager::loadNewWorld(const HammerEngine::WorldGenerationConfig& config) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        WORLD_MANAGER_ERROR("WorldManager not initialized");
        return false;
    }
    
    std::lock_guard<std::shared_mutex> lock(m_worldMutex);
    
    try {
        auto newWorld = HammerEngine::WorldGenerator::generateWorld(config);
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
        
        // Create TileChangedEvent and fire it through EventManager
        auto tileEvent = std::make_shared<TileChangedEvent>(x, y, changeType);
        
        EventManager& eventMgr = EventManager::Instance();
        std::string eventName = "tile_changed_" + std::to_string(x) + "_" + std::to_string(y);
        
        // Register and execute the event
        eventMgr.registerEvent(eventName, tileEvent);
        eventMgr.executeEvent(eventName);
        
        WORLD_MANAGER_DEBUG("TileChangedEvent fired for tile at (" + std::to_string(x) + ", " + std::to_string(y) + ")");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire TileChangedEvent: " + std::string(ex.what()));
    }
}

void WorldManager::fireWorldLoadedEvent(const std::string& worldId) {
    try {
        // Get world dimensions
        int width, height;
        if (!getWorldDimensions(width, height)) {
            width = 0;
            height = 0;
        }
        
        // Create WorldLoadedEvent and register it for processing
        auto worldEvent = std::make_shared<WorldLoadedEvent>(worldId, width, height);
        
        EventManager& eventMgr = EventManager::Instance();
        std::string eventName = "world_loaded_" + worldId;
        
        // Register and execute the event for asynchronous processing
        // Don't execute immediately to avoid deadlocks
        eventMgr.registerEvent(eventName, worldEvent);
        eventMgr.executeEvent(eventName);
        
        WORLD_MANAGER_INFO("WorldLoadedEvent registered and executed for world: " + worldId + " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire WorldLoadedEvent: " + std::string(ex.what()));
    }
}

void WorldManager::fireWorldUnloadedEvent(const std::string& worldId) {
    try {
        // Create WorldUnloadedEvent and fire it through EventManager
        auto worldEvent = std::make_shared<WorldUnloadedEvent>(worldId);
        
        EventManager& eventMgr = EventManager::Instance();
        std::string eventName = "world_unloaded_" + worldId;
        
        // Register and execute the event
        eventMgr.registerEvent(eventName, worldEvent);
        eventMgr.executeEvent(eventName);
        
        WORLD_MANAGER_INFO("WorldUnloadedEvent fired for world: " + worldId);
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to fire WorldUnloadedEvent: " + std::string(ex.what()));
    }
}

void WorldManager::registerEventHandlers() {
    try {
        EventManager& eventMgr = EventManager::Instance();
        
        // Register handler for world events (to respond to events from other systems)
        eventMgr.registerHandler(EventTypeId::World, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle world-related events from other systems
                WORLD_MANAGER_DEBUG("WorldManager received world event: " + data.event->getName());
            }
        });
        
        // Register handler for camera events (world bounds may affect camera)
        eventMgr.registerHandler(EventTypeId::Camera, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle camera events that may require world data updates
                WORLD_MANAGER_DEBUG("WorldManager received camera event: " + data.event->getName());
            }
        });
        
        // Register handler for resource change events (resource changes may affect world state)
        eventMgr.registerHandler(EventTypeId::ResourceChange, [](const EventData& data) {
            if (data.isActive() && data.event) {
                // Handle resource changes that may affect world generation or state
                auto resourceEvent = std::dynamic_pointer_cast<ResourceChangeEvent>(data.event);
                if (resourceEvent) {
                    WORLD_MANAGER_DEBUG("WorldManager received resource change: " + 
                                       resourceEvent->getResourceHandle().toString() + 
                                       " changed by " + std::to_string(resourceEvent->getQuantityChange()));
                }
            }
        });
        
        // Register handler for harvest resource events
        eventMgr.registerHandler(EventTypeId::Harvest, [this](const EventData& data) {
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
        });
        
        WORLD_MANAGER_DEBUG("WorldManager event handlers registered");
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("Failed to register event handlers: " + std::string(ex.what()));
    }
}

void WorldManager::unregisterEventHandlers() {
    try {        
        // Unregister world event handlers
        // Note: EventManager should handle cleanup automatically during shutdown
        // but we can be explicit about it for clarity
        
        WORLD_MANAGER_DEBUG("WorldManager event handlers unregistered");
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
    
    // World bounds in tile coordinates (assuming each tile is 1x1 unit)
    minX = 0.0f;
    minY = 0.0f;
    maxX = static_cast<float>(width);
    maxY = static_cast<float>(height);
    
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
    
    // Calculate base resource quantities based on world size
    int baseAmount = std::max(10, totalTiles / 20); // Scale with world size
    
    try {
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
    
    // Calculate visible tile range directly (no chunking during render)
    int startTileX = std::max(0, static_cast<int>(cameraX / TILE_SIZE) - 1);
    int endTileX = std::min(static_cast<int>(world.grid[0].size()), 
                           static_cast<int>((cameraX + viewportWidth) / TILE_SIZE) + 2);
    int startTileY = std::max(0, static_cast<int>(cameraY / TILE_SIZE) - 1);
    int endTileY = std::min(static_cast<int>(world.grid.size()), 
                           static_cast<int>((cameraY + viewportHeight) / TILE_SIZE) + 2);
    
    // Render visible tiles directly to the main renderer (no render target switching)
    for (int y = startTileY; y < endTileY; ++y) {
        for (int x = startTileX; x < endTileX; ++x) {
            // Calculate screen position
            float screenX = (x * TILE_SIZE) - cameraX;
            float screenY = (y * TILE_SIZE) - cameraY;
            
            // Render tile directly using the GameEngine renderer
            renderTile(world.grid[y][x], renderer, screenX, screenY);
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
    
    std::string textureID;
    
    // Determine texture based on tile content (prioritize obstacles over biome)
    if (tile.obstacleType != HammerEngine::ObstacleType::NONE) {
        textureID = getObstacleTexture(tile.obstacleType);
    } else if (tile.isWater) {
        textureID = "obstacle_water";
    } else {
        textureID = getBiomeTexture(tile.biome);
    }
    
    // Use TextureManager's tile-optimized float precision rendering
    TextureManager::Instance().drawTileF(textureID, screenX, screenY, TILE_SIZE, TILE_SIZE, renderer);
    
    // Debug logging for texture issues (only in debug builds)
    #ifdef DEBUG
    if (textureID.empty()) {
        WORLD_MANAGER_WARN("TileRenderer: Empty texture ID for tile at screen position (" + std::to_string(screenX) + ", " + std::to_string(screenY) + ")");
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
        case HammerEngine::ObstacleType::BUILDING: return "obstacle_building";
        default:                    return "biome_default";
    }
}


        


        