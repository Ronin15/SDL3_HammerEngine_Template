# WorldManager Implementation Plan

## 1. Overview

This document outlines the implementation plan for the `WorldManager`, a new engine component responsible for procedurally generating, managing, and rendering a grid-based game world. The design emphasizes modularity, data-driven configuration, and deep integration with existing engine systems like the `EventManager` and `WorldResourceManager`.

## 🚀 Implementation Status

### ✅ Completed Components
- **Core Architecture**: All primary files created and implemented
- **Data Structures**: `WorldData.hpp` with all planned structs and enums
- **World Generator**: Full procedural generation pipeline implemented
- **WorldManager Class**: Complete singleton implementation with thread safety
- **TileRenderer**: ASCII-based rendering system implemented
- **Unit Tests**: Comprehensive test suites for both WorldGenerator and WorldManager
- **Error Handling**: Robust bounds checking and validation
- **Thread Safety**: Full thread-safe implementation with shared_mutex

### 🔧 Partially Completed
- **GameState Integration**: WorldManager is referenced in GamePlayState and EventDemoState but not fully integrated into lifecycle management
- **Event System Integration**: Event handling structure implemented but specific event types may need definition
- **Resource Manager Integration**: Structure exists but full integration pending

### 📋 Remaining Tasks
- **Complete GameState Integration**: Implement `loadNewWorld()` calls in `GamePlayState::onEnter()` and `unloadWorld()` in `GamePlayState::onExit()`
- **Event Types Definition**: Define and implement `WorldLoadedEvent`, `TileChangedEvent`, and `HarvestResourceEvent`
- **Resource Manager Integration**: Complete integration with `WorldResourceManager` for resource tracking
- **Performance Optimization**: Implement and test viewport-based rendering optimizations
- **Advanced Features**: Consider implementing save/load functionality for generated worlds

## 2. Core Architecture & Data Structures

### 2.1. File Structure ✅ COMPLETED

All planned files have been successfully created and implemented:

-   ✅ `include/managers/WorldManager.hpp` - Complete singleton implementation
-   ✅ `src/managers/WorldManager.cpp` - Full functionality implemented
-   ✅ `include/world/WorldData.hpp` - All data structures defined
-   ✅ `include/world/WorldGenerator.hpp` - Complete generator interface
-   ✅ `src/world/WorldGenerator.cpp` - Full procedural generation pipeline
-   ✅ `tests/world/WorldManagerTests.cpp` - Comprehensive test suite
-   ✅ `tests/world/WorldGeneratorTests.cpp` - Complete generator tests
-   ✅ `docs/managers/WorldManager_Implementation_Plan.md` (this file)

### 2.2. Core Data Structures (`WorldData.hpp`) ✅ COMPLETED

All planned data structures have been implemented with minor refinements:

#### `WorldGenerationConfig` ✅
```cpp
struct WorldGenerationConfig {
    int width;
    int height;
    int seed;
    float elevationFrequency;
    float humidityFrequency;
    float waterLevel;
    float mountainLevel;
};
```

#### `Biome` Enum ✅
```cpp
enum class Biome {
    DESERT,
    FOREST,
    MOUNTAIN,
    SWAMP,
    HAUNTED,
    CELESTIAL,
    OCEAN
};
```

#### `ObstacleType` Enum ✅ (Modified)
**Note**: Implementation differs slightly from plan - includes WATER and BUILDING types:
```cpp
enum class ObstacleType {
    NONE,
    ROCK,
    TREE,
    WATER,    // Added
    BUILDING  // Added (instead of LOG)
};
```

#### `Tile` Struct ✅
```cpp
struct Tile {
    Biome biome;
    ObstacleType obstacleType = ObstacleType::NONE;
    float elevation = 0.0f;
    bool isWater = false;
    HammerEngine::ResourceHandle resourceHandle;
};
```

#### `WorldData` Struct ✅
```cpp
struct WorldData {
    std::string worldId;
    std::vector<std::vector<Tile>> grid;
};
```

## 3. Class Design

### 3.1. `WorldManager` ✅ COMPLETED

-   **Location:** `include/managers/`, `src/managers/` ✅
-   **Pattern:** Singleton, consistent with other managers ✅
-   **Responsibilities:** ✅ All implemented
    -   ✅ Manages the lifecycle of the active `WorldData` object
    -   ✅ Acts as the primary API for other systems to interact with the world (e.g., `getTileAt(x, y)`)
    -   ✅ Orchestrates world loading and unloading
    -   🔧 Integrates with other managers (`EventManager`, `WorldResourceManager`) - Structure implemented, full integration pending
    -   ✅ Manages the `TileRenderer` and the camera/viewport for optimized rendering

**Additional Features Implemented:**
- ✅ Thread-safe operations with `std::shared_mutex`
- ✅ Camera management with viewport control
- ✅ Rendering enable/disable functionality
- ✅ World bounds and dimensions API
- ✅ Comprehensive error handling and validation

### 3.2. `WorldGenerator` ✅ COMPLETED

-   **Location:** `include/world/`, `src/world/` ✅
-   **Pattern:** Standalone utility class ✅
-   **Responsibilities:** ✅ All implemented
    -   ✅ Contains all procedural generation logic
    -   ✅ Implements a modular pipeline to generate the world
    -   ✅ Takes a `WorldGenerationConfig` and produces a `std::unique_ptr<WorldData>`

#### Generation Pipeline: ✅ COMPLETED

All pipeline stages have been fully implemented:

1.  ✅ `generateNoiseMaps()`: Creates elevation and humidity maps using custom Perlin noise implementation
2.  ✅ `assignBiomes()`: Assigns a `Biome` to each tile based on noise values
3.  ✅ `createWaterBodies()`: Forms water bodies based on elevation threshold
4.  ✅ `distributeObstacles()`: Places obstacles based on biome-specific rules
5.  ✅ `calculateInitialResources()`: Creates a summary of harvestable resources in the newly generated world

### 3.3. `TileRenderer` ✅ COMPLETED

**Additional Implementation:**
- ✅ Embedded within `WorldManager.hpp` as a supporting class
- ✅ ASCII-based rendering using codepage 437 characters
- ✅ Viewport-based rendering for performance optimization
- ✅ Color mapping for different biomes and obstacles
- ✅ Optimized rendering with viewport padding for smooth scrolling

## 4. Integration with Engine Systems

### 4.1. `WorldResourceManager` 🔧 PARTIALLY COMPLETED

Structure implemented but full integration pending:

-   ✅ API structure exists in `WorldManager` for resource integration
-   📋 **TODO**: Complete implementation of `initializeWorldResources()` method
-   📋 **TODO**: When a new world is loaded, `WorldManager` should:
    1.  Call `WorldResourceManager::Instance().createWorld(worldId)` to register the new world space
    2.  Use the initial resource summary from the `WorldGenerator` to populate the `WorldResourceManager` with the world's starting resource totals via `addResource()`

### 4.2. `EventManager` 🔧 PARTIALLY COMPLETED

Event handling structure implemented but specific event types need definition:

-   ✅ Event registration/unregistration methods implemented in `WorldManager`
-   ✅ Event firing methods implemented (`fireTileChangedEvent`, `fireWorldLoadedEvent`, etc.)
-   📋 **TODO**: Define specific event types:

    **Events to be Emitted by `WorldManager`:**
    -   📋 `WorldLoadedEvent(worldId)`: Fired after a new world is loaded and initialized
    -   📋 `TileChangedEvent(x, y, newTileState)`: Fired when a tile's state is modified

    **Events to be Handled by `WorldManager`:**
    -   📋 `HarvestResourceEvent(entityId, targetX, targetY)`: On this event, the `WorldManager` should:
        1.  Update the tile at `(targetX, targetY)` (e.g., remove a tree)
        2.  Notify the `WorldResourceManager` to decrement the corresponding resource count for the world
        3.  Fire a `TileChangedEvent`

### 4.3. `GameStateManager` 🔧 PARTIALLY COMPLETED

Basic integration exists but lifecycle management needs completion:

-   ✅ `WorldManager` is included and used in `GamePlayState` and `EventDemoState`
-   ✅ World bounds checking is implemented in game states
-   📋 **TODO**: Complete lifecycle integration:
    -   📋 `GamePlayState::onEnter()` should trigger `WorldManager::Instance().loadNewWorld(...)`
    -   📋 `GamePlayState::onExit()` should trigger `WorldManager::Instance().unloadWorld()`

## 5. Rendering ✅ COMPLETED

### 5.1. `TileRenderer` ✅ COMPLETED

All planned rendering functionality has been implemented:

-   ✅ A dedicated rendering class has been created to handle drawing the world
-   ✅ Responsible for translating `Tile` data into visual output
-   ✅ **Initial Implementation**: Uses `FontManager` to draw codepage 437 characters with full character and color mapping for each `Tile` state (biome, obstacle, water)
-   ✅ **Upgrade Path**: The `TileRenderer` can be modified to use `TextureManager` to draw sprites/textures instead, without requiring any changes to the `WorldManager`

### 5.2. Optimized Rendering ✅ COMPLETED

All performance optimizations have been implemented:

-   ✅ The `WorldManager::render()` method does not draw the entire world every frame
-   ✅ Determines the visible tile range based on camera position and zoom level
-   ✅ Instructs the `TileRenderer` to only draw tiles within the viewport
-   ✅ Includes viewport padding (`VIEWPORT_PADDING = 2`) for smooth scrolling
-   ✅ Full camera management with configurable viewport dimensions

## 6. Unit Testing ✅ COMPLETED

All planned testing has been implemented with comprehensive coverage:

-   ✅ **`WorldGeneratorTests`** (`tests/world/WorldGeneratorTests.cpp`):
    -   ✅ Deterministic generation testing: Given a constant seed, verify that the generated world is identical on every run
    -   ✅ Biome distribution validation: Test biome distribution rules (e.g., swamps are in low-lying areas)
    -   ✅ Obstacle placement testing: Test that obstacle placement follows biome rules
    -   ✅ World configuration validation: Test various generation parameters
    -   ✅ Edge case handling: Test invalid configurations and boundary conditions

-   ✅ **`WorldManagerTests`** (`tests/world/WorldManagerTests.cpp`):
    -   ✅ Public API testing: `loadNewWorld`, `getTileAt`, `unloadWorld`
    -   ✅ Bounds checking: Verify correct handling of out-of-bounds requests
    -   ✅ Thread safety testing: Concurrent access validation
    -   ✅ Camera and viewport functionality testing
    -   ✅ Resource handle integration testing
    -   ✅ Initialization and cleanup testing

-   📋 **Integration Tests** (TODO):
    -   📋 Simulate a `HarvestResourceEvent` and verify that the `WorldManager`, `WorldResourceManager`, and a mock entity's inventory are all updated correctly
    -   📋 Verify that a `WorldLoadedEvent` is fired with the correct data

## 7. Next Steps & Priorities

### High Priority
1. **Complete GameState Integration**: Implement proper world loading/unloading in `GamePlayState` lifecycle
2. **Define Event Types**: Create specific event classes for world events
3. **Complete Resource Manager Integration**: Finish `initializeWorldResources()` implementation

### Medium Priority
4. **Integration Testing**: Implement cross-system integration tests
5. **Performance Optimization**: Profile and optimize rendering performance
6. **Documentation**: Update manager documentation with usage examples

### Low Priority
7. **Advanced Features**: Consider save/load functionality for generated worlds
8. **Enhanced Rendering**: Investigate texture-based rendering upgrade
9. **World Persistence**: Add world save/load capabilities

## 8. Architecture Notes

The implemented `WorldManager` follows all planned architectural principles:
- ✅ **Modularity**: Clean separation between generation, management, and rendering
- ✅ **Data-Driven**: Configuration-based world generation
- ✅ **Thread Safety**: Full thread-safe implementation
- ✅ **Performance**: Optimized viewport-based rendering
- ✅ **Integration Ready**: Prepared for full engine system integration

The core functionality is complete and ready for use, with remaining work focused on deeper engine integration and advanced features.
