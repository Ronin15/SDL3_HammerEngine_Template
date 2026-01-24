/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_MANAGER_HPP
#define WORLD_MANAGER_HPP

#include "world/WorldData.hpp"
#include "world/WorldGenerator.hpp"
#include "managers/GameTimeManager.hpp"
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

namespace HammerEngine {
class Camera;  // Forward declaration for camera pointer storage

// World object definition loaded from JSON
struct WorldObjectDef {
    std::string id;
    std::string name;
    std::string textureId;
    bool seasonal{false};
    bool blocking{false};
    bool harvestable{false};
    int buildingSize{0};  // For buildings: 1=hut, 2=house, 3=large, 4=cityhall
};

// World objects data loaded from world_objects.json
struct WorldObjectsData {
    std::string version;
    std::unordered_map<std::string, WorldObjectDef> biomes;
    std::unordered_map<std::string, WorldObjectDef> obstacles;
    std::unordered_map<std::string, WorldObjectDef> decorations;
    std::unordered_map<std::string, WorldObjectDef> buildings;
    bool loaded{false};
};

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

    /**
     * @brief Update dirty chunk textures (call BEFORE SceneRenderer::beginScene)
     *
     * This method handles all render target switching for chunk texture updates.
     * Call this before beginScene() to avoid render target conflicts.
     *
     * @param world World data
     * @param renderer SDL renderer
     * @param cameraX Camera X offset
     * @param cameraY Camera Y offset
     * @param viewportWidth Viewport width at 1x scale
     * @param viewportHeight Viewport height at 1x scale
     */
    void updateDirtyChunks(const WorldData& world, SDL_Renderer* renderer,
                           float cameraX, float cameraY,
                           float viewportWidth, float viewportHeight);

    /**
     * @brief Render cached chunk textures to the current render target
     *
     * Only composites pre-rendered chunk textures - no render target changes.
     * Safe to call within SceneRenderer's begin/end block.
     *
     * @param world World data to render
     * @param renderer SDL renderer
     * @param cameraX Camera X offset (floored for pixel-perfect tile alignment)
     * @param cameraY Camera Y offset (floored for pixel-perfect tile alignment)
     * @param viewportWidth Viewport width at 1x scale
     * @param viewportHeight Viewport height at 1x scale
     */
    void render(const WorldData& world, SDL_Renderer* renderer,
                float cameraX, float cameraY,
                float viewportWidth, float viewportHeight);

    void renderTile(const Tile& tile, SDL_Renderer* renderer, float screenX, float screenY) const;

    // Chunk texture management
    void invalidateChunk(int chunkX, int chunkY);  // Mark chunk for re-rendering
    void clearChunkCache();  // Clean up all chunk textures

    // Season management
    void subscribeToSeasonEvents();
    void unsubscribeFromSeasonEvents();
    Season getCurrentSeason() const { return m_currentSeason; }
    void setCurrentSeason(Season season);

    // World objects data access
    const WorldObjectsData& getWorldObjectsData() const { return m_worldObjects; }

private:
    // Load world object definitions from JSON
    void loadWorldObjects();
    // Update cached texture IDs when season changes (eliminates per-frame string allocations)
    void updateCachedTextureIDs();

    // Cached seasonal texture IDs - pre-computed to avoid heap allocations in render loop
    struct SeasonalTextureIDs {
        std::string biome_default;
        std::string biome_desert;
        std::string biome_forest;
        std::string biome_plains;
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
        // Ore deposit textures (non-seasonal)
        std::string obstacle_iron_deposit;
        std::string obstacle_gold_deposit;
        std::string obstacle_copper_deposit;
        std::string obstacle_mithril_deposit;
        std::string obstacle_limestone_deposit;
        std::string obstacle_coal_deposit;
        // Gem deposit textures (non-seasonal)
        std::string obstacle_emerald_deposit;
        std::string obstacle_ruby_deposit;
        std::string obstacle_sapphire_deposit;
        std::string obstacle_diamond_deposit;
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
        std::string decoration_dead_log_hz;
        std::string decoration_dead_log_vertical;
        std::string decoration_lily_pad;
        std::string decoration_water_flower;
    } m_cachedTextureIDs;

    // Cached texture with dimensions and atlas source coords
    // When using atlas: ptr points to shared atlas, atlasX/Y are source rect origin
    struct CachedTexture {
        SDL_Texture* ptr{nullptr};
        float w{0}, h{0};
        float atlasX{0}, atlasY{0};  // Source rect origin in atlas (0,0 = full texture)
    };

    struct YSortedSprite {
        float y;
        float renderX;
        float renderY;
        const CachedTexture* tex;
        bool isBuilding;
        int buildingWidth;
        int buildingHeight;
    };

    struct ChunkCache {
        std::shared_ptr<SDL_Texture> texture;
        bool dirty{true};
        uint64_t lastUsedFrame{0};
    };

    static uint64_t makeChunkKey(int chunkX, int chunkY) {
        return (static_cast<uint64_t>(chunkY) << 32) | static_cast<uint32_t>(chunkX);
    }

    // Cached texture pointers - eliminates hash map lookups in hot render loop
    struct CachedTileTextures {
        CachedTexture biome_default;
        CachedTexture biome_desert;
        CachedTexture biome_forest;
        CachedTexture biome_plains;
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
        // Ore deposit textures
        CachedTexture obstacle_iron_deposit;
        CachedTexture obstacle_gold_deposit;
        CachedTexture obstacle_copper_deposit;
        CachedTexture obstacle_mithril_deposit;
        CachedTexture obstacle_limestone_deposit;
        CachedTexture obstacle_coal_deposit;
        // Gem deposit textures
        CachedTexture obstacle_emerald_deposit;
        CachedTexture obstacle_ruby_deposit;
        CachedTexture obstacle_sapphire_deposit;
        CachedTexture obstacle_diamond_deposit;
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
        CachedTexture decoration_dead_log_hz;
        CachedTexture decoration_dead_log_vertical;
        CachedTexture decoration_lily_pad;
        CachedTexture decoration_water_flower;
    } m_cachedTextures;

    mutable std::vector<YSortedSprite> m_ySortBuffer;

    std::unordered_map<uint64_t, ChunkCache> m_chunkCache;
    uint64_t m_frameCounter{0};
    static constexpr size_t MAX_CACHED_CHUNKS = 64;
    mutable std::vector<uint64_t> m_visibleKeysBuffer;
    mutable std::vector<std::pair<uint64_t, uint64_t>> m_evictionBuffer;
    std::atomic<bool> m_cachePendingClear{false};

    void renderChunkToTexture(const WorldData& world, SDL_Renderer* renderer,
                              int chunkX, int chunkY, SDL_Texture* target);

    void onSeasonChange(const EventData& data);

    // Seasonal texture ID helper
    std::string getSeasonalTextureID(const std::string& baseID) const;

    std::string getBiomeTexture(HammerEngine::Biome biome) const;
    std::string getObstacleTexture(HammerEngine::ObstacleType obstacle) const;

    // Season tracking
    Season m_currentSeason{Season::Spring};
    EventManager::HandlerToken m_seasonToken{};
    bool m_subscribedToSeasons{false};

    // World object definitions loaded from JSON
    WorldObjectsData m_worldObjects;

    // Atlas-based rendering (single texture, source rects from JSON)
    // Pre-loaded seasonal coords - eliminates runtime lookups on season change
    struct AtlasCoords {
        float x{0}, y{0}, w{0}, h{0};
    };

    // All tile type coords per season (indexed by Season enum)
    struct SeasonalTileCoords {
        AtlasCoords biome_default;
        AtlasCoords biome_desert;
        AtlasCoords biome_forest;
        AtlasCoords biome_plains;
        AtlasCoords biome_mountain;
        AtlasCoords biome_swamp;
        AtlasCoords biome_haunted;
        AtlasCoords biome_celestial;
        AtlasCoords biome_ocean;
        AtlasCoords obstacle_water;
        AtlasCoords obstacle_tree;
        AtlasCoords obstacle_rock;
        AtlasCoords building_hut;
        AtlasCoords building_house;
        AtlasCoords building_large;
        AtlasCoords building_cityhall;
        // Ore deposit coords
        AtlasCoords obstacle_iron_deposit;
        AtlasCoords obstacle_gold_deposit;
        AtlasCoords obstacle_copper_deposit;
        AtlasCoords obstacle_mithril_deposit;
        AtlasCoords obstacle_limestone_deposit;
        AtlasCoords obstacle_coal_deposit;
        // Gem deposit coords
        AtlasCoords obstacle_emerald_deposit;
        AtlasCoords obstacle_ruby_deposit;
        AtlasCoords obstacle_sapphire_deposit;
        AtlasCoords obstacle_diamond_deposit;
        AtlasCoords decoration_flower_blue;
        AtlasCoords decoration_flower_pink;
        AtlasCoords decoration_flower_white;
        AtlasCoords decoration_flower_yellow;
        AtlasCoords decoration_mushroom_purple;
        AtlasCoords decoration_mushroom_tan;
        AtlasCoords decoration_grass_small;
        AtlasCoords decoration_grass_large;
        AtlasCoords decoration_bush;
        AtlasCoords decoration_stump_small;
        AtlasCoords decoration_stump_medium;
        AtlasCoords decoration_rock_small;
        AtlasCoords decoration_dead_log_hz;
        AtlasCoords decoration_dead_log_vertical;
        AtlasCoords decoration_lily_pad;
        AtlasCoords decoration_water_flower;
    };

    SeasonalTileCoords m_seasonalCoords[4];  // Indexed by Season enum (Spring=0, Summer=1, Fall=2, Winter=3)
    SDL_Texture* m_atlasPtr{nullptr};        // Single shared atlas texture
    bool m_useAtlas{false};                  // True if atlas loaded successfully

    // Get atlas pointer from TextureManager and pre-load source rect coords from JSON
    void initAtlasCoords();
    void applyCoordsToTextures(Season season);
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

    /**
     * @brief Set renderer for chunk texture updates (called by GameEngine at init)
     */
    void setRenderer(SDL_Renderer* renderer) { mp_renderer = renderer; }

    /**
     * @brief Set active camera for chunk visibility (called by states with world rendering)
     */
    void setActiveCamera(HammerEngine::Camera* camera) { mp_activeCamera = camera; }

    /**
     * @brief Update dirty chunk textures (call in GameState::update before render)
     *
     * Uses stored renderer and active camera. Handles all render target switching
     * for chunk texture updates before SceneRenderer pipeline begins.
     */
    void updateDirtyChunks();

    /**
     * @brief Render tiles to the current render target
     *
     * Renders visible tile chunks directly to the current render target.
     * Call this within SceneRenderer's begin/end block.
     *
     * @param renderer SDL renderer
     * @param cameraX Camera X offset (floored for pixel-perfect tile alignment)
     * @param cameraY Camera Y offset (floored for pixel-perfect tile alignment)
     * @param viewportWidth Viewport width at 1x scale
     * @param viewportHeight Viewport height at 1x scale
     */
    void render(SDL_Renderer* renderer, float cameraX, float cameraY,
                float viewportWidth, float viewportHeight);

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

    // Renderer and camera for chunk texture updates
    SDL_Renderer* mp_renderer{nullptr};
    HammerEngine::Camera* mp_activeCamera{nullptr};
};

#endif // WORLD_MANAGER_HPP
