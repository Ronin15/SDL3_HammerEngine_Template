/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/WorldManager.hpp"
#include "core/Logger.hpp"
#include "managers/EventManager.hpp"
#include "managers/WorldResourceManager.hpp"
#include "events/ResourceChangeEvent.hpp"
#include <algorithm>

namespace HammerEngine {

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
        m_tileRenderer = std::make_unique<TileRenderer>();
        registerEventHandlers();
        
        m_isShutdown = false;
        m_initialized.store(true, std::memory_order_release);
        WORLD_MANAGER_INFO("WorldManager initialized successfully");
        return true;
    } catch (const std::exception& ex) {
        WORLD_MANAGER_ERROR("WorldManager::init - Exception: " + std::string(ex.what()));
        return false;
    }
}

void WorldManager::clean() {
    if (m_isShutdown) {
        return;
    }
    
    std::lock_guard<std::shared_mutex> lock(m_worldMutex);
    
    unregisterEventHandlers();
    unloadWorld();
    m_tileRenderer.reset();
    
    m_initialized.store(false, std::memory_order_release);
    m_isShutdown = true;
    
    WORLD_MANAGER_INFO("WorldManager cleaned up");
}

bool WorldManager::loadNewWorld(const WorldGenerationConfig& config) {
    if (!m_initialized.load(std::memory_order_acquire)) {
        WORLD_MANAGER_ERROR("WorldManager not initialized");
        return false;
    }
    
    std::lock_guard<std::shared_mutex> lock(m_worldMutex);
    
    try {
        auto newWorld = WorldGenerator::generateWorld(config);
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
        
        // Create world in WorldResourceManager
        if (!WorldResourceManager::Instance().createWorld(m_currentWorld->worldId)) {
            WORLD_MANAGER_WARN("Failed to create world in WorldResourceManager: " + m_currentWorld->worldId);
        }
        
        // Fire world loaded event
        fireWorldLoadedEvent(m_currentWorld->worldId);
        
        WORLD_MANAGER_INFO("Successfully loaded new world: " + m_currentWorld->worldId);
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
    if (m_currentWorld) {
        WORLD_MANAGER_INFO("Unloading world: " + m_currentWorld->worldId);
        m_currentWorld.reset();
    }
}

const Tile* WorldManager::getTileAt(int x, int y) const {
    std::shared_lock<std::shared_mutex> lock(m_worldMutex);
    
    if (!m_currentWorld || !isValidPosition(x, y)) {
        return nullptr;
    }
    
    return &m_currentWorld->grid[y][x];
}

Tile* WorldManager::getTileAt(int x, int y) {
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

void WorldManager::render(int cameraX, int cameraY, int viewportWidth, int viewportHeight) {
    if (!m_initialized.load(std::memory_order_acquire) || !m_currentWorld || !m_renderingEnabled) {
        return;
    }
    
    std::shared_lock<std::shared_mutex> lock(m_worldMutex);
    
    if (m_tileRenderer) {
        m_tileRenderer->renderVisibleTiles(*m_currentWorld, cameraX, cameraY, viewportWidth, viewportHeight);
    }
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
    
    Tile& tile = m_currentWorld->grid[targetY][targetX];
    
    if (tile.obstacleType == ObstacleType::NONE) {
        WORLD_MANAGER_WARN("No harvestable resource at position: (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ")");
        return false;
    }
    
    // Store the original obstacle type for resource tracking
    ObstacleType harvestedType = tile.obstacleType;
    (void)harvestedType; // Suppress unused warning
    
    // Remove the obstacle
    tile.obstacleType = ObstacleType::NONE;
    tile.resourceHandle = HammerEngine::ResourceHandle{};
    
    // Fire tile changed event
    fireTileChangedEvent(targetX, targetY, tile);
    
    // Notify WorldResourceManager about resource depletion
    // This is a placeholder - actual resource tracking would need proper resource handles
    
    WORLD_MANAGER_INFO("Resource harvested at (" + std::to_string(targetX) + ", " + std::to_string(targetY) + ") by entity " + std::to_string(entityId));
    return true;
}

bool WorldManager::updateTile(int x, int y, const Tile& newTile) {
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

void WorldManager::fireTileChangedEvent(int x, int y, const Tile& tile) {
    // This would create and fire a TileChangedEvent
    // For now, just log the change
    WORLD_MANAGER_DEBUG("Tile changed at (" + std::to_string(x) + ", " + std::to_string(y) + "): biome=" + std::to_string(static_cast<int>(tile.biome)) + ", obstacle=" + std::to_string(static_cast<int>(tile.obstacleType)));
}

void WorldManager::fireWorldLoadedEvent(const std::string& worldId) {
    // This would create and fire a WorldLoadedEvent
    // For now, just log the event
    WORLD_MANAGER_INFO("World loaded event fired for world: " + worldId);
}

void WorldManager::registerEventHandlers() {
    // Register handlers for HarvestResourceEvent and other world-related events
    // This would use the EventManager to register handlers
    WORLD_MANAGER_DEBUG("WorldManager event handlers registered");
}

void WorldManager::unregisterEventHandlers() {
    // Unregister event handlers
    WORLD_MANAGER_DEBUG("WorldManager event handlers unregistered");
}

// TileRenderer Implementation

void TileRenderer::renderVisibleTiles(const WorldData& world, int cameraX, int cameraY, 
                                     int viewportWidth, int viewportHeight) {
    if (world.grid.empty()) {
        return;
    }
    
    int worldHeight = static_cast<int>(world.grid.size());
    int worldWidth = static_cast<int>(world.grid[0].size());
    
    // Calculate visible tile range with padding
    int startX = std::max(0, cameraX - VIEWPORT_PADDING);
    int endX = std::min(worldWidth, cameraX + viewportWidth + VIEWPORT_PADDING);
    int startY = std::max(0, cameraY - VIEWPORT_PADDING);
    int endY = std::min(worldHeight, cameraY + viewportHeight + VIEWPORT_PADDING);
    
    // Render visible tiles
    for (int y = startY; y < endY; ++y) {
        for (int x = startX; x < endX; ++x) {
            int screenX = x - cameraX;
            int screenY = y - cameraY;
            
            // Only render if within actual viewport
            if (screenX >= 0 && screenX < viewportWidth && screenY >= 0 && screenY < viewportHeight) {
                renderTile(world.grid[y][x], screenX, screenY);
            }
        }
    }
}

void TileRenderer::renderTile(const Tile& tile, int screenX, int screenY) {
    char character;
    std::pair<uint8_t, uint8_t> colors;
    
    // Determine character and color based on tile content
    if (tile.obstacleType != ObstacleType::NONE) {
        character = getObstacleCharacter(tile.obstacleType);
        colors = getObstacleColor(tile.obstacleType);
    } else if (tile.isWater) {
        character = '~';
        colors = {4, 0}; // Blue on black
    } else {
        character = getBiomeCharacter(tile.biome);
        colors = getBiomeColor(tile.biome);
    }
    
    // Suppress unused warnings for now - actual FontManager integration would use these
    (void)character;
    (void)colors;
    (void)screenX;
    (void)screenY;
    
    // Use FontManager to render the character
    // This is a simplified version - actual implementation would use FontManager
    // FontManager::Instance().renderCharacter(character, screenX, screenY, colors.first, colors.second);
}

char TileRenderer::getBiomeCharacter(Biome biome) const {
    switch (biome) {
        case Biome::DESERT:     return '.';
        case Biome::FOREST:     return '"';
        case Biome::MOUNTAIN:   return '^';
        case Biome::SWAMP:      return '%';
        case Biome::HAUNTED:    return 'H';
        case Biome::CELESTIAL:  return '*';
        case Biome::OCEAN:      return '~';
        default:                return ' ';
    }
}

char TileRenderer::getObstacleCharacter(ObstacleType obstacle) const {
    switch (obstacle) {
        case ObstacleType::TREE:    return 'T';
        case ObstacleType::WATER:   return '~';
        case ObstacleType::ROCK:    return '#';
        case ObstacleType::BUILDING: return 'B';
        default:                    return ' ';
    }
}

std::pair<uint8_t, uint8_t> TileRenderer::getBiomeColor(Biome biome) const {
    // Returns foreground, background color pair
    switch (biome) {
        case Biome::DESERT:     return {14, 0}; // Yellow on black
        case Biome::FOREST:     return {2, 0};  // Green on black
        case Biome::MOUNTAIN:   return {8, 0};  // Gray on black
        case Biome::SWAMP:      return {6, 0};  // Brown on black
        case Biome::HAUNTED:    return {5, 0};  // Purple on black
        case Biome::CELESTIAL:  return {11, 0}; // Cyan on black
        case Biome::OCEAN:      return {1, 0};  // Blue on black
        default:                return {7, 0};  // White on black
    }
}

std::pair<uint8_t, uint8_t> TileRenderer::getObstacleColor(ObstacleType obstacle) const {
    // Returns foreground, background color pair
    switch (obstacle) {
        case ObstacleType::TREE:    return {2, 0};  // Green on black
        case ObstacleType::ROCK:    return {8, 0};  // Gray on black
        case ObstacleType::WATER:   return {1, 0};  // Blue on black
        case ObstacleType::BUILDING: return {11, 0}; // Cyan on black
        default:                    return {7, 0};  // White on black
    }
}

}