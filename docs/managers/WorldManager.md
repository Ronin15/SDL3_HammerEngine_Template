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

The `WorldData` struct holds the world's configuration, tile data, and other metadata.

### TileRenderer

The `TileRenderer` is responsible for rendering the world's tiles. It uses a chunk-based approach to optimize rendering performance. Chunks are pre-rendered to textures, and only the visible chunks are drawn to the screen.

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

## Best Practices

- **Initialize Early**: Initialize the `WorldManager` during your game's startup sequence.
- **Unload Worlds**: Always unload the current world before loading a new one or changing game states to prevent memory leaks.
- **Use `isValidPosition`**: Before accessing tiles, check if the position is valid to avoid errors.
- **Cache World Data**: For performance-critical sections, get a pointer to the `WorldData` and access it directly, but be mindful of thread safety.
