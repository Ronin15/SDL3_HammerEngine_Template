/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_MANAGER_HPP
#define WORLD_MANAGER_HPP

#include "world/WorldData.hpp"
#include "world/WorldGenerator.hpp"
#include "utils/ResourceHandle.hpp"
#include "core/GameTime.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include "managers/EventManager.hpp"

// Forward declarations
struct SDL_Renderer;
struct SDL_Texture;

struct SDL_Renderer;

namespace HammerEngine {

class TileRenderer {
private:
    static constexpr float TILE_SIZE = 32.0f;  // Use float for smooth movement
    static constexpr int VIEWPORT_PADDING = 2;

    // Chunk-based rendering for scalability to massive maps
    static constexpr int CHUNK_SIZE = 32;  // 32x32 tiles per chunk

public:
    TileRenderer();
    ~TileRenderer();

    // Disable copy/move for event handler safety
    TileRenderer(const TileRenderer&) = delete;
    TileRenderer& operator=(const TileRenderer&) = delete;

    void renderVisibleTiles(const WorldData& world, SDL_Renderer* renderer,
                           float cameraX, float cameraY, int viewportWidth, int viewportHeight);
    void renderTile(const Tile& tile, SDL_Renderer* renderer, float screenX, float screenY) const;

    // Chunk texture management
    void invalidateChunk(int chunkX, int chunkY);  // Mark chunk for re-rendering
    void clearChunkCache();  // Clean up all chunk textures

    // Season management
    void subscribeToSeasonEvents();
    void unsubscribeFromSeasonEvents();
    Season getCurrentSeason() const { return m_currentSeason; }
    void setCurrentSeason(Season season);

private:
    // Update cached texture IDs when season changes (eliminates per-frame string allocations)
    void updateCachedTextureIDs();

    // Cached seasonal texture IDs - pre-computed to avoid heap allocations in render loop
    struct SeasonalTextureIDs {
        std::string biome_default;
        std::string biome_desert;
        std::string biome_forest;
        std::string biome_mountain;
        std::string biome_swamp;
        std::string biome_haunted;
        std::string biome_celestial;
        std::string biome_ocean;
        std::string obstacle_water;
        std::string obstacle_tree;
        std::string obstacle_rock;
        std::string building_hut;
        std::string building_house;
        std::string building_large;
        std::string building_cityhall;
    } m_cachedTextureIDs;


    // Season change handler
    void onSeasonChange(const EventData& data);

    // Seasonal texture ID helper
    std::string getSeasonalTextureID(const std::string& baseID) const;

    // Chunk-based rendering helpers
    struct ChunkBounds {
        int startChunkX, endChunkX;
        int startChunkY, endChunkY;
    };

    ChunkBounds calculateVisibleChunks(float cameraX, float cameraY, int viewportWidth, int viewportHeight) const;

    std::string getBiomeTexture(HammerEngine::Biome biome) const;
    std::string getObstacleTexture(HammerEngine::ObstacleType obstacle) const;

    // Season tracking
    Season m_currentSeason{Season::Spring};
    EventManager::HandlerToken m_seasonToken{};
    bool m_subscribedToSeasons{false};
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

    bool loadNewWorld(const HammerEngine::WorldGenerationConfig& config,
                     const HammerEngine::WorldGenerationProgressCallback& progressCallback = nullptr);
    bool loadWorld(const std::string& worldId);
    void unloadWorld();
    
    const HammerEngine::Tile* getTileAt(int x, int y) const;
    HammerEngine::Tile* getTileAt(int x, int y);
    
    bool isValidPosition(int x, int y) const;
    std::string getCurrentWorldId() const;
    bool hasActiveWorld() const;
    
    void update();
    void render(SDL_Renderer* renderer, float cameraX, float cameraY, 
               int viewportWidth, int viewportHeight);
    
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
     * @brief Gets the current world version for change detection
     * @return Current world version (increments on tile changes)
     */
    uint64_t getWorldVersion() const { return m_worldVersion.load(std::memory_order_acquire); }
    
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
    void unloadWorldUnsafe();  // Internal method - assumes caller already holds lock
    
    std::unique_ptr<HammerEngine::WorldData> m_currentWorld;
    std::unique_ptr<HammerEngine::TileRenderer> m_tileRenderer;
    
    mutable std::shared_mutex m_worldMutex;
    std::atomic<bool> m_initialized{false};
    bool m_isShutdown{false};
    
    // World version tracking for change detection by other systems
    std::atomic<uint64_t> m_worldVersion{0};
    
    bool m_renderingEnabled{true};
    int m_cameraX{0};
    int m_cameraY{0};
    int m_viewportWidth{80};
    int m_viewportHeight{25};

    // Handler tokens for clean unregister
    std::vector<EventManager::HandlerToken> m_handlerTokens;
};

#endif // WORLD_MANAGER_HPP
