/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WORLD_MANAGER_HPP
#define WORLD_MANAGER_HPP

#include "world/WorldData.hpp"
#include "world/WorldGenerator.hpp"
#include "managers/GameTimeManager.hpp"
#include "utils/Vector2D.hpp"
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

#ifdef USE_SDL3_GPU
struct SDL_GPURenderPass;
#endif

namespace HammerEngine {
class Camera;  // Forward declaration for camera pointer storage

#ifdef USE_SDL3_GPU
class GPURenderer;  // Forward declaration for GPU rendering
class GPUTexture;   // Forward declaration for GPU texture
class SpriteBatch;  // Forward declaration for sprite batch
#endif

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
    static constexpr int SPRITE_OVERHANG = 64;  // Padding for sprites extending beyond tile bounds (2 tiles)

    // Chunk-based rendering - smaller chunks = faster per-chunk render, more chunks total
    static constexpr int CHUNK_SIZE = 16;  // 16x16 tiles per chunk (256 tiles vs 1024)

public:
    TileRenderer();
    ~TileRenderer();

    // Disable copy/move for event handler safety
    TileRenderer(const TileRenderer&) = delete;
    TileRenderer& operator=(const TileRenderer&) = delete;

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

#ifdef USE_SDL3_GPU
    /**
     * @brief Record visible tile vertices for GPU rendering
     *
     * Records all visible tile sprites to the sprite batch using atlas coordinates.
     * Assumes batch is already begin()-ed by GPUSceneRenderer.
     * No chunk textures needed - renders directly from tile data each frame.
     *
     * @param spriteBatch Sprite batch to draw to (already begin()-ed)
     * @param cameraX Camera X offset (floored for pixel-perfect alignment)
     * @param cameraY Camera Y offset (floored for pixel-perfect alignment)
     * @param viewportWidth Viewport width at 1x scale
     * @param viewportHeight Viewport height at 1x scale
     * @param zoom Current zoom level
     * @param season Current season for seasonal textures
     */
    void recordGPUTiles(SpriteBatch& spriteBatch, float cameraX, float cameraY,
                        float viewportWidth, float viewportHeight, float zoom,
                        Season season);

    /**
     * @brief Get the atlas GPU texture
     * @return Pointer to GPU texture, or nullptr if not using atlas
     */
    GPUTexture* getAtlasGPUTexture() const;
#endif

    /**
     * @brief Handle dirty chunk re-rendering with proper render target management
     *
     * Processes dirty chunks (from season changes, etc.) with a per-frame budget
     * to avoid stuttering. Handles deferred cache clears and ensures proper
     * render target restoration after chunk operations.
     *
     * Called during update phase via WorldRenderPipeline::prepareChunks().
     *
     * @param world World data
     * @param renderer SDL renderer
     * @param cameraX Camera X offset
     * @param cameraY Camera Y offset
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     */
    void prefetchChunks(const WorldData& world, SDL_Renderer* renderer,
                        float cameraX, float cameraY,
                        float viewportWidth, float viewportHeight);

    /**
     * @brief Pre-warm all visible chunks without budget limits
     *
     * Renders all chunks in the visible area. Called during loading to
     * eliminate hitches on initial camera movement.
     *
     * @param world World data
     * @param renderer SDL renderer
     * @param cameraX Camera X offset
     * @param cameraY Camera Y offset
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     */
    void prewarmChunks(const WorldData& world, SDL_Renderer* renderer,
                       float cameraX, float cameraY,
                       float viewportWidth, float viewportHeight);

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

    // New: Direct 2D grid for O(1) chunk access (replaces hash map)
    struct ChunkData {
        std::shared_ptr<SDL_Texture> texture;
        bool dirty{true};
    };

    // SoA layout for SIMD screen position calculation
    struct VisibleChunks {
        std::vector<SDL_Texture*> textures;
        std::vector<float> worldX, worldY;        // World positions
        std::vector<float> srcX, srcY, srcW, srcH; // Source rects (edge clipping)
        std::vector<float> screenX, screenY;       // Computed each frame via SIMD
        size_t count{0};

        void clear() {
            textures.clear();
            worldX.clear(); worldY.clear();
            srcX.clear(); srcY.clear(); srcW.clear(); srcH.clear();
            screenX.clear(); screenY.clear();
            count = 0;
        }

        void reserve(size_t n) {
            textures.reserve(n);
            worldX.reserve(n); worldY.reserve(n);
            srcX.reserve(n); srcY.reserve(n); srcW.reserve(n); srcH.reserve(n);
            screenX.reserve(n); screenY.reserve(n);
        }

        void push_back(SDL_Texture* tex, float wx, float wy,
                       float sx, float sy, float sw, float sh) {
            textures.push_back(tex);
            worldX.push_back(wx); worldY.push_back(wy);
            srcX.push_back(sx); srcY.push_back(sy);
            srcW.push_back(sw); srcH.push_back(sh);
            screenX.push_back(0.0f); screenY.push_back(0.0f);
            ++count;
        }
    };



    // Removed: makeChunkKey() - no longer needed with 2D grid

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

    // Lookup tables for O(1) texture access (indexed by enum value)
    static constexpr size_t BIOME_COUNT = 8;
    static constexpr size_t DECORATION_COUNT = 17;
    static constexpr size_t OBSTACLE_COUNT = 15;
    const CachedTexture* m_biomeLUT[BIOME_COUNT]{};
    const CachedTexture* m_decorationLUT[DECORATION_COUNT]{};
    const CachedTexture* m_obstacleLUT[OBSTACLE_COUNT]{};
    void buildLookupTables();

    mutable std::vector<YSortedSprite> m_ySortBuffer;

    // 2D chunk grid - O(1) access, replaces hash map
    std::vector<std::vector<ChunkData>> m_chunkGrid;
    int m_gridWidth{0};   // Number of chunks in X
    int m_gridHeight{0};  // Number of chunks in Y
    bool m_gridInitialized{false};

    // Visible chunk cache with SoA layout for SIMD
    VisibleChunks m_visibleChunks;
    int m_lastCamChunkX{-1};  // Last camera chunk for change detection
    int m_lastCamChunkY{-1};

    // Constants
    static constexpr size_t TEXTURE_POOL_SIZE = 150;  // Pool for chunk textures
    static constexpr int MAX_DIRTY_PER_FRAME = 2;     // Max chunks to re-render per frame

    std::atomic<bool> m_cachePendingClear{false};
    bool m_hasDirtyChunks{false};  // Early-out flag for prefetchChunks

    // Texture pool - reuse textures instead of create/destroy during gameplay
    std::vector<std::shared_ptr<SDL_Texture>> m_texturePool;
    bool m_poolInitialized{false};
    void initTexturePool(SDL_Renderer* renderer);
    std::shared_ptr<SDL_Texture> acquireTexture(SDL_Renderer* renderer);
    void releaseTexture(std::shared_ptr<SDL_Texture> tex);

    // Pre-computed constants for chunk calculations
    static constexpr int CHUNK_PIXELS = CHUNK_SIZE * static_cast<int>(TILE_SIZE);
    static constexpr float INV_CHUNK_PIXELS = 1.0f / static_cast<float>(CHUNK_PIXELS);
    static constexpr int CHUNK_TEXTURE_SIZE = CHUNK_PIXELS + SPRITE_OVERHANG * 2;


    void renderChunkToTexture(const WorldData& world, SDL_Renderer* renderer,
                              int chunkX, int chunkY, SDL_Texture* target);

    /**
     * @brief Initialize entire chunk grid at load time
     *
     * Pre-renders all chunks during loading phase. Called once when world loads.
     *
     * @param world World data
     * @param renderer SDL renderer
     */
    void initChunkGrid(const WorldData& world, SDL_Renderer* renderer);

    /**
     * @brief Rebuild visible chunk list when camera crosses chunk boundary
     *
     * Populates m_visibleChunks with chunks that are currently visible,
     * including edge clipping information for proper rendering.
     *
     * @param camChunkX Camera chunk X coordinate
     * @param camChunkY Camera chunk Y coordinate
     * @param viewW Viewport width in pixels
     * @param viewH Viewport height in pixels
     */
    void rebuildVisibleList(int camChunkX, int camChunkY, float viewW, float viewH);

    /**
     * @brief Calculate screen positions using SIMD (4 chunks at a time)
     *
     * @param cameraX Camera X offset
     * @param cameraY Camera Y offset
     */
    void calculateScreenPositionsSIMD(float cameraX, float cameraY);

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

#ifdef USE_SDL3_GPU
    GPUTexture* m_atlasGPUPtr{nullptr};  // GPU atlas texture pointer

    // Helper to get atlas coords for a tile
    const AtlasCoords& getBiomeAtlasCoords(Biome biome, Season season) const;
    const AtlasCoords& getObstacleAtlasCoords(ObstacleType obstacle, Season season) const;
#endif

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

    /**
     * @brief Handle dirty chunk re-rendering
     *
     * Called from WorldRenderPipeline::prepareChunks() to process dirty chunks
     * (from season changes, etc.) with proper render target management.
     *
     * @param renderer SDL renderer
     * @param camera Camera for position and viewport
     */
    void prefetchChunks(SDL_Renderer* renderer, HammerEngine::Camera& camera);

    /**
     * @brief Internal prefetch using stored renderer and camera
     *
     * Called from WorldRenderPipeline when renderer is not directly available.
     * Uses mp_renderer and mp_activeCamera set via setRenderer() and setActiveCamera().
     */
    void prefetchChunksInternal();

    /**
     * @brief Pre-warm all visible chunks during loading
     *
     * Renders all chunks that would be visible at the given position without
     * budget limits. Call during loading screen to eliminate hitches on initial
     * camera movement.
     *
     * @param renderer SDL renderer
     * @param cameraX Camera X offset
     * @param cameraY Camera Y offset
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     */
    void prewarmChunks(SDL_Renderer* renderer, float cameraX, float cameraY,
                       float viewportWidth, float viewportHeight);

#ifdef USE_SDL3_GPU
    /**
     * @brief Record world tile vertices for GPU rendering
     *
     * Records all visible tile sprites to the sprite batch.
     * Uses the existing atlas texture coordinates for each tile type.
     * Batch lifecycle is managed by caller (GPUSceneRenderer) - this just draws.
     *
     * @param spriteBatch Sprite batch to draw to (already begin()-ed)
     * @param cameraX Camera X offset (floored for pixel-perfect alignment)
     * @param cameraY Camera Y offset (floored for pixel-perfect alignment)
     * @param viewWidth Viewport width at 1x scale
     * @param viewHeight Viewport height at 1x scale
     * @param zoom Current zoom level
     */
    void recordGPU(HammerEngine::SpriteBatch& spriteBatch, float cameraX, float cameraY,
                   float viewWidth, float viewHeight, float zoom);
#endif

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
