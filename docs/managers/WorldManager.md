# WorldManager Documentation

**Where to find the code:**
- Implementation: `src/managers/WorldManager.cpp`
- Header: `include/managers/WorldManager.hpp`

**Singleton Access:** Use `WorldManager::Instance()` to access the manager.

## Overview

The `WorldManager` is a singleton responsible for managing the game world, including loading, unloading, and rendering the world, as well as handling interactions with world objects. It uses a chunk-based rendering system for efficient rendering of large worlds.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [World Management](#world-management)
- [Rendering](#rendering)
- [Event Handling](#event-handling)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)

## Quick Start

### Basic Setup

```cpp
// In your GameState's enter() method
auto& worldManager = WorldManager::Instance();

// Load a new world
HammerEngine::WorldGenerationConfig config;
config.width = 100;
config.height = 100;
config.seed = 12345;
worldManager.loadNewWorld(config);

// Set the camera position and viewport
worldManager.setCamera(0, 0);
worldManager.setCameraViewport(1280, 720);
```

### Game Loop Integration

```cpp
void MyGameState::update(float dt) {
    auto& worldManager = WorldManager::Instance();
    worldManager.update();
}

void MyGameState::render() {
    auto& worldManager = WorldManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    worldManager.render(gameEngine.getRenderer(), m_camera.getX(), m_camera.getY(), gameEngine.getLogicalWidth(), gameEngine.getLogicalHeight());
}
```

## Architecture

The `WorldManager` uses a `WorldData` object to store the current world's tile data and a `TileRenderer` to handle the rendering of the world. The `TileRenderer` uses a chunk-based system to efficiently render only the visible portions of the world.

### WorldData

The `WorldData` struct holds the world's configuration, tile data, and other metadata:

```cpp
struct WorldData {
    std::string worldId;
    std::vector<std::vector<Tile>> grid;  // 2D tile grid
};
```

### TileRenderer

The `TileRenderer` is responsible for rendering the world's tiles. It uses a chunk-based approach to optimize rendering performance. Chunks are pre-rendered to textures, and only the visible chunks are drawn to the screen.

## Chunk-Based Rendering System

WorldManager uses chunked rendering for efficient handling of large worlds.

### How Chunks Work

1. **Chunk Creation**: World is divided into fixed-size chunks (e.g., 16x16 tiles)
2. **Pre-rendering**: Each chunk renders its tiles to a texture once
3. **Viewport Culling**: Only chunks overlapping the viewport are drawn
4. **Cache Invalidation**: Chunks re-render when tiles change or seasons change

### Viewport Padding

The renderer includes padding beyond the visible viewport to ensure smooth scrolling:
- Prevents pop-in at screen edges
- Chunks slightly outside viewport are still rendered
- Enables seamless camera movement

### Tile Culling Optimization

```cpp
// Internal culling logic (simplified)
void TileRenderer::render(float cameraX, float cameraY, int viewW, int viewH) {
    // Calculate visible tile range with padding
    int startTileX = static_cast<int>(cameraX / TILE_SIZE) - 1;
    int endTileX = static_cast<int>((cameraX + viewW) / TILE_SIZE) + 2;

    // Only render chunks that overlap this range
    for (auto& chunk : m_chunks) {
        if (chunk.overlaps(startTileX, endTileX, startTileY, endTileY)) {
            chunk.render(renderer);
        }
    }
}
```

## Biome System

The world uses procedural biome generation with 8 distinct biome types.

### Available Biomes

| Biome | Description | Characteristics |
|-------|-------------|-----------------|
| **DESERT** | Arid sandy regions | Sparse vegetation, ore deposits |
| **FOREST** | Dense woodland | Many trees, wildlife, mushrooms |
| **PLAINS** | Open grassland | Sparse vegetation, flowers |
| **MOUNTAIN** | Rocky highlands | Ore deposits, difficult terrain |
| **SWAMP** | Wetland areas | Water tiles, lily pads, frogs |
| **HAUNTED** | Dark corrupted land | Special visual effects |
| **CELESTIAL** | Magical regions | Unique resources |
| **OCEAN** | Deep water | Water tiles, no land traversal |

### Biome Assignment

Biomes are assigned during world generation based on noise functions:

```cpp
// Simplified biome selection logic
Biome selectBiome(float elevation, float humidity) {
    if (elevation < waterLevel) return Biome::OCEAN;
    if (elevation > mountainLevel) return Biome::MOUNTAIN;
    if (humidity > swampThreshold) return Biome::SWAMP;
    if (humidity < desertThreshold) return Biome::DESERT;
    if (elevation > hillThreshold) return Biome::FOREST;
    return Biome::PLAINS;
}
```

### Tile Structure

Each tile stores its biome and additional properties:

```cpp
struct Tile {
    Biome biome;                              // Biome type
    ObstacleType obstacleType = NONE;         // Rock, tree, water, building, ore deposits
    DecorationType decorationType = NONE;     // Flowers, mushrooms, grass, stumps
    float elevation = 0.0f;                   // Height value (0.0-1.0)
    bool isWater = false;                     // Water tile flag
    HammerEngine::ResourceHandle resourceHandle;  // Associated resource

    // Building support for multi-tile structures
    uint32_t buildingId = 0;                  // 0 = no building, >0 = unique ID
    uint8_t buildingSize = 0;                 // Connected building tile count
    bool isTopLeftOfBuilding = false;         // Render optimization flag
};
```

## Village and Building Generation

The world generator creates villages with multi-tile buildings.

### Building Placement

Buildings are placed during world generation with these rules:
1. Must be on valid terrain (not water, mountains)
2. Maintain minimum spacing from other buildings
3. Connected tiles share the same `buildingId`
4. Only top-left tile has `isTopLeftOfBuilding = true` (for single-draw rendering)

### Building Sizes

| Size | Dimensions | Typical Use |
|------|------------|-------------|
| 1 | 1x1 tile | Huts, small structures |
| 2 | 2x2 tiles | Houses |
| 3 | 3x3 tiles | Large buildings |
| 4 | 4x4 tiles | City halls, temples |

### Building Rendering

Buildings render as single sprites from their top-left tile:

```cpp
// In TileRenderer (simplified)
if (tile.obstacleType == ObstacleType::BUILDING && tile.isTopLeftOfBuilding) {
    // Draw building sprite at this position
    std::string textureKey = getBuildingTexture(tile.buildingSize);
    renderBuildingSprite(textureKey, tileX, tileY, tile.buildingSize);
}
// Skip non-top-left building tiles (covered by main sprite)
```

## Obstacle Types

Tiles can contain various obstacles that affect gameplay:

```cpp
enum class ObstacleType {
    NONE,
    ROCK,               // Impassable terrain
    TREE,               // Impassable, harvestable
    WATER,              // Impassable (swimming not implemented)
    BUILDING,           // Multi-tile structures
    // Ore deposits (harvestable)
    IRON_DEPOSIT, GOLD_DEPOSIT, COPPER_DEPOSIT,
    MITHRIL_DEPOSIT, LIMESTONE_DEPOSIT, COAL_DEPOSIT,
    // Gem deposits (harvestable)
    EMERALD_DEPOSIT, RUBY_DEPOSIT, SAPPHIRE_DEPOSIT, DIAMOND_DEPOSIT
};
```

## Decoration Types

Non-blocking visual elements that add variety:

```cpp
enum class DecorationType : uint8_t {
    NONE,
    FLOWER_BLUE, FLOWER_PINK, FLOWER_WHITE, FLOWER_YELLOW,
    MUSHROOM_PURPLE, MUSHROOM_TAN,
    GRASS_SMALL, GRASS_LARGE,
    BUSH,
    STUMP_SMALL, STUMP_MEDIUM,
    ROCK_SMALL,
    DEAD_LOG_HZ, DEAD_LOG_VERTICAL,
    LILY_PAD, WATER_FLOWER
};
```

## World Management

### Loading and Unloading Worlds

- `loadNewWorld(const HammerEngine::WorldGenerationConfig& config)`: Generates and loads a new world based on the provided configuration.
- `loadWorld(const std::string& worldId)`: Loads a world from a file (not yet implemented).
- `unloadWorld()`: Unloads the current world.

### Tile and Position Management

- `getTileAt(int x, int y)`: Returns the tile at the specified world coordinates.
- `isValidPosition(int x, int y)`: Checks if the given coordinates are within the world boundaries.
- `updateTile(int x, int y, const HammerEngine::Tile& newTile)`: Updates the tile at the specified coordinates.

## Rendering

The `render` method takes the renderer, camera position, and viewport dimensions to render the visible portion of the world.

```cpp
void render(SDL_Renderer* renderer, float cameraX, float cameraY, int viewportWidth, int viewportHeight);
```

## Event Handling

The `WorldManager` fires events when the world state changes:

- `TileChangedEvent`: Fired when a tile is updated.
- `WorldLoadedEvent`: Fired when a new world is loaded.
- `WorldUnloadedEvent`: Fired when the current world is unloaded.

It also listens for `HarvestResourceEvent` to handle resource harvesting by entities.

## API Reference

### Core Methods

- `bool init()`: Initializes the manager.
- `void clean()`: Cleans up resources.
- `bool isInitialized() const`: Checks if the manager is initialized.
- `bool isShutdown() const`: Checks if the manager has been shut down.
- `void setupEventHandlers()`: Sets up event handlers.

### World Operations

- `bool loadNewWorld(const HammerEngine::WorldGenerationConfig& config)`
- `bool loadWorld(const std::string& worldId)`
- `void unloadWorld()`
- `const HammerEngine::Tile* getTileAt(int x, int y) const`
- `bool isValidPosition(int x, int y) const`
- `std::string getCurrentWorldId() const`
- `bool hasActiveWorld() const`
- `bool updateTile(int x, int y, const HammerEngine::Tile& newTile)`
- `bool getWorldDimensions(int& width, int& height) const`
- `bool getWorldBounds(float& minX, float& minY, float& maxX, float& maxY) const`

### Rendering and Camera

- `void update()`: Updates the world state.
- `void render(SDL_Renderer* renderer, float cameraX, float cameraY, int viewportWidth, int viewportHeight)`
- `void enableRendering(bool enable)`
- `bool isRenderingEnabled() const`
- `void setCamera(int x, int y)`
- `void setCameraViewport(int width, int height)`

## Seasonal Texture System

The WorldManager supports seasonal texture variations that automatically swap based on the current game season. This system is tightly integrated with the GameTime system.

### How It Works

1. **Event Subscription**: WorldManager subscribes to `SeasonChangedEvent` from EventManager
2. **Texture ID Caching**: Pre-computed seasonal texture IDs eliminate per-frame string allocations
3. **Chunk Invalidation**: When season changes, chunk cache is invalidated (deferred for thread safety)
4. **Automatic Rendering**: Next render pass uses new seasonal textures

### Seasonal Texture Naming Convention

Textures follow this naming pattern:
- **Base textures**: `biome_forest`, `obstacle_tree`, `building_house`
- **Seasonal variants**: `spring_biome_forest`, `summer_obstacle_tree`, `winter_building_house`

If a seasonal variant doesn't exist, the base texture is used as fallback.

### Texture Categories with Season Support

| Category | Examples |
|----------|----------|
| Biomes | `biome_default`, `biome_forest`, `biome_desert`, `biome_mountain`, etc. |
| Obstacles | `obstacle_tree`, `obstacle_rock`, `obstacle_water` |
| Buildings | `building_hut`, `building_house`, `building_large`, `building_cityhall` |
| Decorations | `decoration_flower_*`, `decoration_bush`, `decoration_grass_*` |

### Usage

```cpp
// In GameState::enter() - subscribe to season events
WorldManager::Instance().subscribeToSeasonEvents();

// In GameState::exit() - unsubscribe
WorldManager::Instance().unsubscribeFromSeasonEvents();

// Query current season
Season season = WorldManager::Instance().getCurrentSeason();

// Manual season change (usually handled automatically by GameTime)
WorldManager::Instance().setCurrentSeason(Season::Winter);
```

### API Reference

```cpp
void subscribeToSeasonEvents();     // Subscribe to SeasonChangedEvent
void unsubscribeFromSeasonEvents(); // Unsubscribe from season events
Season getCurrentSeason() const;    // Get current season
void setCurrentSeason(Season s);    // Manually set season (invalidates cache)
```

### Thread Safety

The seasonal texture system uses a deferred invalidation pattern for thread safety:

1. **Update Thread**: Season event sets atomic flag `m_seasonTexturesDirty`
2. **Render Thread**: Checks flag before rendering, clears cache if dirty
3. **No Race Conditions**: Texture destruction happens on render thread only

```cpp
// Internal flow (handled automatically)
// Update thread:
m_seasonTexturesDirty.store(true, std::memory_order_release);

// Render thread (before rendering):
if (m_seasonTexturesDirty.exchange(false, std::memory_order_acq_rel)) {
    clearChunkCache();  // Safe - on render thread
    updateCachedTextureIDs();
    refreshCachedTextures();
}
```

### Performance Characteristics

- **Per-frame cost**: Zero (texture IDs pre-cached)
- **Season change cost**: O(chunks) - all chunks invalidated
- **Memory**: Cached texture pointers + dimensions per texture type
- **Hash lookups**: Eliminated via CachedTexture pointers

### CachedTexture Pattern

The `CachedTexture` struct holds texture pointer with dimensions to avoid repeated lookups:

```cpp
struct CachedTexture {
    SDL_Texture* ptr{nullptr};
    float w{0}, h{0};  // Cached dimensions
};
```

This eliminates:
- Hash map lookups in render loop
- `SDL_QueryTexture()` calls per tile
- String allocations for texture ID lookups

## Best Practices

- **Initialize Early**: Initialize the `WorldManager` during your game's startup sequence.
- **Unload Worlds**: Always unload the current world before loading a new one or changing game states to prevent memory leaks.
- **Use `isValidPosition`**: Before accessing tiles, check if the position is valid to avoid errors.
- **Cache World Data**: For performance-critical sections, get a pointer to the `WorldData` and access it directly, but be mindful of thread safety.
- **Subscribe to Seasons**: Call `subscribeToSeasonEvents()` in states that render the world to get automatic seasonal texture updates.

## Related Documentation

- **GameTime**: `docs/core/GameTime.md` - Time system that triggers season changes
- **TextureManager**: `docs/managers/TextureManager.md` - Texture loading and caching
- **SeasonChangedEvent**: `docs/events/TimeEvents.md` - Season change event details
