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
    char getBiomeCharacter(Biome biome) const;
    char getObstacleCharacter(ObstacleType obstacle) const;
    std::pair<uint8_t, uint8_t> getBiomeColor(Biome biome) const;
    std::pair<uint8_t, uint8_t> getObstacleColor(ObstacleType obstacle) const;
};

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
    
    bool loadNewWorld(const WorldGenerationConfig& config);
    bool loadWorld(const std::string& worldId);
    void unloadWorld();
    
    const Tile* getTileAt(int x, int y) const;
    Tile* getTileAt(int x, int y);
    
    bool isValidPosition(int x, int y) const;
    std::string getCurrentWorldId() const;
    bool hasActiveWorld() const;
    
    void update();
    void render(int cameraX = 0, int cameraY = 0, 
               int viewportWidth = 80, int viewportHeight = 25);
    
    bool handleHarvestResource(int entityId, int targetX, int targetY);
    bool updateTile(int x, int y, const Tile& newTile);
    
    void enableRendering(bool enable) { m_renderingEnabled = enable; }
    bool isRenderingEnabled() const { return m_renderingEnabled; }
    
    void setCamera(int x, int y) { m_cameraX = x; m_cameraY = y; }
    void setCameraViewport(int width, int height) { 
        m_viewportWidth = width; 
        m_viewportHeight = height; 
    }
    
    const WorldData* getWorldData() const { return m_currentWorld.get(); }
    
private:
    WorldManager() = default;
    ~WorldManager() {
        if (!m_isShutdown) {
            clean();
        }
    }
    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
    
    void fireTileChangedEvent(int x, int y, const Tile& tile);
    void fireWorldLoadedEvent(const std::string& worldId);
    void registerEventHandlers();
    void unregisterEventHandlers();
    
    std::unique_ptr<WorldData> m_currentWorld;
    std::unique_ptr<TileRenderer> m_tileRenderer;
    
    mutable std::shared_mutex m_worldMutex;
    std::atomic<bool> m_initialized{false};
    bool m_isShutdown{false};
    
    bool m_renderingEnabled{true};
    int m_cameraX{0};
    int m_cameraY{0};
    int m_viewportWidth{80};
    int m_viewportHeight{25};
};

}

#endif // WORLD_MANAGER_HPP