# WorldManager Implementation Plan

## 1. Overview

This document outlines the implementation plan for the `WorldManager`, a new engine component responsible for procedurally generating, managing, and rendering a grid-based game world. The design emphasizes modularity, data-driven configuration, and deep integration with existing engine systems like the `EventManager` and `WorldResourceManager`.

## 2. Core Architecture & Data Structures

### 2.1. File Structure

The following new files will be created:

-   `include/managers/WorldManager.hpp`
-   `src/managers/WorldManager.cpp`
-   `include/world/WorldData.hpp`
-   `include/world/WorldGenerator.hpp`
-   `src/world/WorldGenerator.cpp`
-   `docs/managers/WorldManager_Implementation_Plan.md` (this file)

### 2.2. Core Data Structures (`WorldData.hpp`)

To promote separation of concerns, world-related data structures will be defined in `include/world/WorldData.hpp`.

#### `WorldGenerationConfig`

A data-driven struct to control the world generation process. This allows for easy tuning and presets.

```cpp
struct WorldGenerationConfig {
    int width;
    int height;
    int seed;
    // Noise parameters for elevation, humidity, etc.
    float elevationFrequency;
    float humidityFrequency;
    // Thresholds for biome determination
    float waterLevel;
    float mountainLevel;
};
```

#### `Biome` Enum

Defines the different types of biomes available in the world.

```cpp
enum class Biome {
    DESERT,
    FOREST,
    MOUNTAIN,
    SWAMP,
    HAUNTED,
    CELESTIAL,
    OCEAN // For deep water areas
};
```

#### `ObstacleType` Enum

Defines the types of obstacles that can appear on a tile.

```cpp
enum class ObstacleType {
    NONE,
    TREE,
    ROCK,
    LOG
};
```

#### `Tile` Struct

Represents a single cell in the world grid. It is a plain data struct.

```cpp
#include "utils/ResourceHandle.hpp"

struct Tile {
    Biome biome;
    ObstacleType obstacleType = ObstacleType::NONE;
    float elevation = 0.0f;
    bool isWater = false;
    HammerEngine::ResourceHandle resourceHandle; // Link to a resource (e.g., wood for a tree)
};
```

#### `WorldData` Struct

A container for the entire state of a generated world.

```cpp
#include <vector>
#include <string>

struct WorldData {
    std::string worldId;
    std::vector<std::vector<Tile>> grid;
    // Other world-level metadata can be added here
};
```

## 3. Class Design

### 3.1. `WorldManager`

-   **Location:** `include/managers/`, `src/managers/`
-   **Pattern:** Singleton, consistent with other managers.
-   **Responsibilities:**
    -   Manages the lifecycle of the active `WorldData` object.
    -   Acts as the primary API for other systems to interact with the world (e.g., `getTileAt(x, y)`).
    -   Orchestrates world loading and unloading.
    -   Integrates with other managers (`EventManager`, `WorldResourceManager`).
    -   Manages the `TileRenderer` and the camera/viewport for optimized rendering.

### 3.2. `WorldGenerator`

-   **Location:** `include/world/`, `src/world/`
-   **Pattern:** Standalone utility class.
-   **Responsibilities:**
    -   Contains all procedural generation logic.
    -   Implements a modular pipeline to generate the world.
    -   Takes a `WorldGenerationConfig` and produces a `std::unique_ptr<WorldData>`.

#### Generation Pipeline:

1.  `generateNoiseMaps()`: Creates elevation and humidity maps using a noise library (e.g., FastNoiseLite).
2.  `assignBiomes()`: Assigns a `Biome` to each tile based on noise values.
3.  `createWaterBodies()`: Forms lakes based on an elevation threshold and carves rivers using a hydraulic erosion simulation.
4.  `distributeObstacles()`: Places obstacles based on biome-specific rules.
5.  `calculateInitialResources()`: Creates a summary of harvestable resources in the newly generated world.

## 4. Integration with Engine Systems

### 4.1. `WorldResourceManager`

-   When a new world is loaded, `WorldManager` will:
    1.  Call `WorldResourceManager::Instance().createWorld(worldId)` to register the new world space.
    2.  Use the initial resource summary from the `WorldGenerator` to populate the `WorldResourceManager` with the world's starting resource totals via `addResource()`.

### 4.2. `EventManager`

The `WorldManager` will be an event listener and dispatcher.

-   **Events Emitted by `WorldManager`:**
    -   `WorldLoadedEvent(worldId)`: Fired after a new world is loaded and initialized.
    -   `TileChangedEvent(x, y, newTileState)`: Fired when a tile's state is modified.

-   **Events Handled by `WorldManager`:**
    -   `HarvestResourceEvent(entityId, targetX, targetY)`: On this event, the `WorldManager` will:
        1.  Update the tile at `(targetX, targetY)` (e.g., remove a tree).
        2.  Notify the `WorldResourceManager` to decrement the corresponding resource count for the world.
        3.  Fire a `TileChangedEvent`.

### 4.3. `GameStateManager`

-   The `WorldManager`'s lifecycle will be tied to game states.
-   `GamePlayState::onEnter()` will trigger `WorldManager::Instance().loadNewWorld(...)`.
-   `GamePlayState::onExit()` will trigger `WorldManager::Instance().unloadWorld()`.

## 5. Rendering

### 5.1. `TileRenderer`

-   A dedicated rendering class will be created to handle drawing the world.
-   It will be responsible for translating `Tile` data into visual output.
-   **Initial Implementation:** Use `FontManager` to draw codepage 437 characters. The renderer will map each `Tile` state (biome, obstacle, water) to a specific character and color.
-   **Future Upgrade:** The `TileRenderer` can be modified to use `TextureManager` to draw sprites/textures instead, without requiring any changes to the `WorldManager`.

### 5.2. Optimized Rendering

-   The `WorldManager::render()` method will not draw the entire world every frame.
-   It will determine the visible tile range based on a camera's position and zoom level and instruct the `TileRenderer` to only draw the tiles within that viewport.

## 6. Unit Testing

-   **`WorldGeneratorTests`:**
    -   Given a constant seed, verify that the generated world is identical on every run.
    -   Test biome distribution rules (e.g., swamps are in low-lying areas).
    -   Test that obstacle placement follows biome rules.
-   **`WorldManagerTests`:**
    -   Test the public API: `loadNewWorld`, `getTileAt`, `unloadWorld`.
    -   Verify correct handling of out-of-bounds requests.
-   **Integration Tests:**
    -   Simulate a `HarvestResourceEvent` and verify that the `WorldManager`, `WorldResourceManager`, and a mock entity's inventory are all updated correctly.
    -   Verify that a `WorldLoadedEvent` is fired with the correct data.
