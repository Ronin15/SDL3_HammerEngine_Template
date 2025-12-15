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
    static constexpr int SPRITE_OVERHANG = 64;  // Padding for sprites extending beyond tile bounds (increased for tall trees/buildings)

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
        // Decoration textures (seasonal where applicable)
        std::string decoration_flower_blue;
        std::string decoration_flower_pink;
        std::string decoration_flower_white;
        std::string decoration_flower_yellow;
        std::string decoration_mushroom_purple;
        std::string decoration_mushroom_tan;
        std::string decoration_grass_small;
        std::string decoration_grass_large;
        std::string decoration_bush;
        std::string decoration_stump_small;
        std::string decoration_stump_medium;
        std::string decoration_rock_small;
    } m_cachedTextureIDs;

    // Cached texture with dimensions - keeps pointer and size together
    struct CachedTexture {
        SDL_Texture* ptr{nullptr};
        float w{0}, h{0};
    };

    // Y-sorted sprite data for unified chunk rendering (obstacles rendered into chunks)
    struct YSortedSprite {
        float y;           // Y position for sorting (bottom of sprite)
        float renderX;     // Local X in chunk texture
        float renderY;     // Local Y in chunk texture
        const CachedTexture* tex;
        bool isBuilding;
        int buildingWidth;
        int buildingHeight;
    };

    // Cached texture pointers and dimensions - eliminates hash map lookups in hot render loop
    struct CachedTileTextures {
        CachedTexture biome_default;
        CachedTexture biome_desert;
        CachedTexture biome_forest;
        CachedTexture biome_mountain;
        CachedTexture biome_swamp;
        CachedTexture biome_haunted;
        CachedTexture biome_celestial;
        CachedTexture biome_ocean;
        CachedTexture obstacle_water;
        CachedTexture obstacle_tree;
        CachedTexture obstacle_rock;
        CachedTexture building_hut;
        CachedTexture building_house;
        CachedTexture building_large;
        CachedTexture building_cityhall;
        // Decoration textures
        CachedTexture decoration_flower_blue;
        CachedTexture decoration_flower_pink;
        CachedTexture decoration_flower_white;
        CachedTexture decoration_flower_yellow;
        CachedTexture decoration_mushroom_purple;
        CachedTexture decoration_mushroom_tan;
        CachedTexture decoration_grass_small;
        CachedTexture decoration_grass_large;
        CachedTexture decoration_bush;
        CachedTexture decoration_stump_small;
        CachedTexture decoration_stump_medium;
        CachedTexture decoration_rock_small;
    } m_cachedTextures;

    // Chunk texture cache - pre-rendered tile chunks to reduce draw calls
    // Thread safety: Season events (onSeasonChange) dispatch from update thread while
    // render runs on main thread. Deferred cache clear pattern ensures safe texture destruction.
    struct ChunkCache {
        std::shared_ptr<SDL_Texture> texture;
        bool dirty{true};  // Needs re-render
        uint64_t lastUsedFrame{0};  // For LRU eviction
    };
    std::unordered_map<uint64_t, ChunkCache> m_chunkCache;  // Key: (chunkY << 32) | chunkX
    uint64_t m_frameCounter{0};  // For LRU tracking
    static constexpr size_t MAX_CACHED_CHUNKS = 64;  // ~256MB max VRAM (4MB per 1024x1024 chunk)

    // Reusable buffers for render loop (avoids per-frame allocations per CLAUDE.md)
    mutable std::vector<uint64_t> m_visibleKeysBuffer;
    mutable std::vector<std::pair<uint64_t, uint64_t>> m_evictionBuffer;
    mutable std::vector<YSortedSprite> m_ySortBuffer;  // For Y-sorted obstacle rendering in chunks

    // Chunk cache helpers
    static uint64_t makeChunkKey(int chunkX, int chunkY) {
        return (static_cast<uint64_t>(chunkY) << 32) | static_cast<uint32_t>(chunkX);
    }
    void renderChunkToTexture(const WorldData& world, SDL_Renderer* renderer,
                              int chunkX, int chunkY, SDL_Texture* target);

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

    // Deferred cache invalidation - set by update thread, cleared by render thread
    // Ensures textures are only destroyed when not in use by Metal command encoder
    std::atomic<bool> m_cachePendingClear{false};
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

    // Chunk cache management (delegates to TileRenderer)
    void invalidateChunk(int chunkX, int chunkY);
    void clearChunkCache();

    // Season management (delegates to TileRenderer)
    void subscribeToSeasonEvents();
    void unsubscribeFromSeasonEvents();
    Season getCurrentSeason() const;
    void setCurrentSeason(Season season);
    
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
