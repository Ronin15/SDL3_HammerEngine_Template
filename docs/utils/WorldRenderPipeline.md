# WorldRenderPipeline Documentation

**Where to find the code:**
- Header: `include/utils/WorldRenderPipeline.hpp`
- Implementation: `src/utils/WorldRenderPipeline.cpp`

**Note:** This documentation covers the **SDL_Renderer path only**. For GPU rendering, see [GPURendering.md](../gpu/GPURendering.md) which uses `GPUSceneRenderer` as its parallel implementation.

## Overview

WorldRenderPipeline is a unified facade that coordinates chunk management and scene composition for the SDL_Renderer rendering path. It wraps SceneRenderer and provides a four-phase architecture for clean, predictable world rendering.

### Key Features

- **Four-Phase Architecture**: prepareChunks → beginScene → renderWorld → endScene
- **Unified Coordination**: Single point of control for TileRenderer and SceneRenderer
- **RenderContext**: All render parameters computed once and shared
- **Loading-Time Pre-warm**: Renders visible chunks during loading to prevent hitches

## Architecture

### The Problem

Without WorldRenderPipeline, game states must:
1. Manually coordinate dirty chunk processing
2. Call SceneRenderer begin/end with correct parameters
3. Track render state across multiple systems
4. Duplicate camera calculations

### The Solution

WorldRenderPipeline provides a simple four-phase API that handles all coordination internally.

## Four-Phase Architecture

### Phase 1: prepareChunks (in update)

```cpp
void update(float dt) {
    updateCamera(dt);
    m_renderPipeline->prepareChunks(*m_camera, dt);
}
```

Processes dirty chunks (from season changes, tile modifications) with proper render target management. Call this in `update()`, not `render()`.

### Phase 2: beginScene (in render)

```cpp
void render(float interpolation) {
    auto ctx = m_renderPipeline->beginScene(renderer, *m_camera, interpolation);
    if (!ctx) return;  // Handle error
    // ...
}
```

Sets up SceneRenderer intermediate texture and calculates all render parameters once. Returns a RenderContext containing floored camera positions, view dimensions, and zoom.

### Phase 3: renderWorld (in render)

```cpp
void render(float interpolation) {
    auto ctx = m_renderPipeline->beginScene(renderer, *m_camera, interpolation);
    if (!ctx) return;

    m_renderPipeline->renderWorld(renderer, ctx);

    // Render entities using ctx.cameraX, ctx.cameraY
    for (auto& entity : m_entities) {
        entity->render(renderer, ctx.cameraX, ctx.cameraY, interpolation);
    }
    // ...
}
```

Renders visible tile chunks to the current render target using pre-computed context.

### Phase 4: endScene (in render)

```cpp
void render(float interpolation) {
    // ... beginScene, renderWorld, entities ...

    m_renderPipeline->endScene(renderer);

    // UI renders after endScene (at 1.0 scale)
    UIManager::Instance().render();
}
```

Composites the intermediate texture to screen with zoom and sub-pixel offset. Resets render scale to 1.0 for UI rendering.

## RenderContext Structure

```cpp
struct RenderContext {
    // Camera position for entities (floored - sub-pixel via composite offset)
    float cameraX{0.0f};
    float cameraY{0.0f};

    // Camera position for tiles (floored - pixel-aligned, same as cameraX/Y)
    float flooredCameraX{0.0f};
    float flooredCameraY{0.0f};

    // Sub-pixel offset for smooth scrolling (applied in endScene)
    float subPixelOffsetX{0.0f};
    float subPixelOffsetY{0.0f};

    // View dimensions at 1x scale (divide by zoom for effective view)
    float viewWidth{0.0f};
    float viewHeight{0.0f};

    // Current zoom level
    float zoom{1.0f};

    // Camera world position (for followed entity - avoids double-interpolation jitter)
    Vector2D cameraCenter{0.0f, 0.0f};

    // Whether the context is valid (beginScene succeeded)
    bool valid{false};

    explicit operator bool() const { return valid; }
};
```

### Field Usage

| Field | Use For | Notes |
|-------|---------|-------|
| `cameraX/Y` | Entity rendering | Floored for pixel alignment |
| `flooredCameraX/Y` | Tile rendering | Same as cameraX/Y, explicit name |
| `subPixelOffsetX/Y` | Internal | Applied in endScene composite |
| `viewWidth/Height` | Visibility culling | At 1x scale |
| `zoom` | Scale queries | Applied automatically |
| `cameraCenter` | Followed entity | Use instead of entity.getPosition() |
| `valid` | Error checking | False if beginScene failed |

## Usage Examples

### Complete GameState Integration

```cpp
class GamePlayState : public GameState {
private:
    Camera m_camera;
    std::unique_ptr<WorldRenderPipeline> m_renderPipeline;

public:
    bool enter() override {
        m_renderPipeline = std::make_unique<WorldRenderPipeline>();
        return true;
    }

    void update(float dt) override {
        // Update camera first
        m_camera.update(dt);

        // Phase 1: Prepare chunks (process dirty chunks)
        m_renderPipeline->prepareChunks(m_camera, dt);

        // Other update logic...
    }

    void render(float interpolation) override {
        auto& renderer = GameEngine::Instance().getRenderer();

        // Phase 2: Begin scene - get render context
        auto ctx = m_renderPipeline->beginScene(renderer, m_camera, interpolation);
        if (!ctx) {
            GAMESTATE_WARN("RenderPipeline unavailable");
            return;
        }

        // Phase 3: Render world tiles
        m_renderPipeline->renderWorld(renderer, ctx);

        // Render entities using context coordinates
        for (auto& npc : m_npcs) {
            npc->render(renderer, ctx.cameraX, ctx.cameraY, interpolation);
        }

        // Render followed entity using cameraCenter (avoids jitter)
        if (mp_Player) {
            mp_Player->renderAtCameraCenter(renderer, ctx.cameraCenter, interpolation);
        }

        // Render particles
        ParticleManager::Instance().render(renderer, ctx.cameraX, ctx.cameraY);

        // Phase 4: End scene - composite with zoom and sub-pixel offset
        m_renderPipeline->endScene(renderer);

        // UI renders at native resolution (after endScene)
        UIManager::Instance().render();
    }
};
```

### Loading-Time Pre-warm

Prevent hitches when gameplay starts by pre-warming visible chunks during loading:

```cpp
void LoadingState::onWorldGenerationComplete() {
    // Pre-warm chunks that will be visible at spawn point
    m_renderPipeline->prewarmVisibleChunks(
        renderer,
        spawnPoint.x, spawnPoint.y,
        static_cast<float>(m_viewportWidth),
        static_cast<float>(m_viewportHeight)
    );
}
```

## API Reference

### Constructor/Destructor

```cpp
WorldRenderPipeline();
~WorldRenderPipeline();

// Non-copyable (owns resources)
WorldRenderPipeline(const WorldRenderPipeline&) = delete;
WorldRenderPipeline& operator=(const WorldRenderPipeline&) = delete;

// Movable
WorldRenderPipeline(WorldRenderPipeline&&) noexcept;
WorldRenderPipeline& operator=(WorldRenderPipeline&&) noexcept;
```

### Phase Methods

```cpp
// Phase 1: Call in update()
void prepareChunks(Camera& camera, float deltaTime);

// Phase 2: Call in render(), returns context
RenderContext beginScene(SDL_Renderer* renderer, Camera& camera, float interpolationAlpha);

// Phase 3: Call after beginScene
void renderWorld(SDL_Renderer* renderer, const RenderContext& ctx);

// Phase 4: Call after all scene rendering
void endScene(SDL_Renderer* renderer);
```

### Utility Methods

```cpp
// Pre-warm chunks during loading
void prewarmVisibleChunks(SDL_Renderer* renderer, float centerX, float centerY,
                          float viewWidth, float viewHeight);

// Access underlying SceneRenderer (advanced use)
SceneRenderer* getSceneRenderer();

// Check if between beginScene/endScene
bool isSceneActive() const;
```

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| prepareChunks | ~0.5ms (dirty) | 0ms if no dirty chunks |
| beginScene | ~0.1ms | Render target switch |
| renderWorld | ~1-3ms | Depends on visible chunks |
| endScene | ~0.1ms | Composite to screen |

## Relationship to Other Systems

### SceneRenderer

WorldRenderPipeline owns a SceneRenderer internally. The pipeline manages the SceneRenderer lifecycle and provides a simpler API.

### Camera

The pipeline requires a Camera reference for:
- Position (via `getRenderOffset()`)
- Zoom level (via `getZoom()`)
- Viewport dimensions (via `getViewport()`)

### WorldManager

The pipeline coordinates with WorldManager for:
- Dirty chunk processing (season changes)
- Tile rendering (via TileRenderer)
- Chunk visibility calculations

### GPUSceneRenderer (GPU Path)

For GPU rendering, use GPUSceneRenderer instead. The two systems are parallel implementations:

| SDL_Renderer Path | GPU Path |
|-------------------|----------|
| WorldRenderPipeline | GPUSceneRenderer |
| SceneRenderer | GPURenderer (scene texture) |
| RenderContext | GPUSceneContext |

## Related Documentation

- **[SceneRenderer](SceneRenderer.md)** - Underlying scene rendering
- **[Camera](Camera.md)** - Camera position and zoom
- **[WorldManager](../managers/WorldManager.md)** - Chunk and tile management
- **[GPURendering](../gpu/GPURendering.md)** - GPU path equivalent
