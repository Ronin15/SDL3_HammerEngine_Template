/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_MANAGER_HPP
#define WORLD_MANAGER_HPP

#include "world/WorldData.hpp"
#include "world/WorldGenerator.hpp"
#include "utils/ResourceHandle.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <shared_mutex>

namespace HammerEngine {

class TileRenderer {
private:
    static constexpr int VIEWPORT_PADDING = 2;
    
public:
    void renderVisibleTiles(const WorldData& world, int cameraX, int cameraY, 
                           int viewportWidth, int viewportHeight);
    void renderTile(const Tile& tile, int screenX, int screenY);
    
private:
    std::string getBiomeTexture(HammerEngine::Biome biome) const;
    std::string getObstacleTexture(HammerEngine::ObstacleType obstacle) const;
};

}

class WorldManager {
public:
    static WorldManager& Instance() {
        static WorldManager instance;
        return instance;
    }
    
    bool init();
    void clean();
    bool isInitialized() const { return m_initialized.load(std::memory_order_acquire); }
    bool isShutdown() const { return m_isShutdown; }
    
    // Post-initialization setup that requires other managers to be ready
    void setupEventHandlers();
    
    bool loadNewWorld(const HammerEngine::WorldGenerationConfig& config);
    bool loadWorld(const std::string& worldId);
    void unloadWorld();
    
    const HammerEngine::Tile* getTileAt(int x, int y) const;
    HammerEngine::Tile* getTileAt(int x, int y);
    
    bool isValidPosition(int x, int y) const;
    std::string getCurrentWorldId() const;
    bool hasActiveWorld() const;
    
    void update();
    void render(int cameraX = 0, int cameraY = 0, 
               int viewportWidth = 80, int viewportHeight = 25);
    
    bool handleHarvestResource(int entityId, int targetX, int targetY);
    bool updateTile(int x, int y, const HammerEngine::Tile& newTile);
    
    void enableRendering(bool enable) { m_renderingEnabled = enable; }
    bool isRenderingEnabled() const { return m_renderingEnabled; }
    
    void setCamera(int x, int y) { m_cameraX = x; m_cameraY = y; }
    void setCameraViewport(int width, int height) { 
        m_viewportWidth = width; 
        m_viewportHeight = height; 
    }
    
    const HammerEngine::WorldData* getWorldData() const { return m_currentWorld.get(); }
    
    /**
     * @brief Gets the world dimensions in tiles
     * @param width Output world width
     * @param height Output world height  
     * @return True if world is loaded and dimensions are valid
     */
    bool getWorldDimensions(int& width, int& height) const;
    
    /**
     * @brief Gets world bounds in world coordinates
     * @param minX Output minimum X coordinate
     * @param minY Output minimum Y coordinate
     * @param maxX Output maximum X coordinate  
     * @param maxY Output maximum Y coordinate
     * @return True if world is loaded and bounds are valid
     */
    bool getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const;
    
private:
    WorldManager() = default;
    ~WorldManager() {
        if (!m_isShutdown) {
            clean();
        }
    }
    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
    
    void fireTileChangedEvent(int x, int y, const HammerEngine::Tile& tile);
    void fireWorldLoadedEvent(const std::string& worldId);
    void fireWorldUnloadedEvent(const std::string& worldId);
    void registerEventHandlers();
    void unregisterEventHandlers();
    void initializeWorldResources();
    
    std::unique_ptr<HammerEngine::WorldData> m_currentWorld;
    std::unique_ptr<HammerEngine::TileRenderer> m_tileRenderer;
    
    mutable std::shared_mutex m_worldMutex;
    std::atomic<bool> m_initialized{false};
    bool m_isShutdown{false};
    
    bool m_renderingEnabled{true};
    int m_cameraX{0};
    int m_cameraY{0};
    int m_viewportWidth{80};
    int m_viewportHeight{25};
};

#endif // WORLD_MANAGER_HPP