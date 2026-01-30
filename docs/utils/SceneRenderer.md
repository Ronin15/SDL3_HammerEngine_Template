# SceneRenderer Documentation

**Where to find the code:**
- Header: `include/utils/SceneRenderer.hpp`
- Implementation: `src/utils/SceneRenderer.cpp`

## Overview

SceneRenderer is a utility class that provides pixel-perfect zoomed scene rendering with smooth sub-pixel scrolling. It manages an intermediate render texture that enables smooth camera movement at any zoom level while maintaining pixel-perfect tile rendering.

### Key Features

- **Pixel-Perfect Rendering**: All content renders to intermediate texture using floored (integer) camera coordinates
- **Smooth Sub-Pixel Scrolling**: Sub-pixel camera smoothness via composite offset, not per-entity positioning
- **Zoom Support**: Works with Camera's discrete zoom levels
- **No Jitter**: Tiles and entities move together without relative jitter
- **Non-Singleton**: GameStates own instances (following Camera pattern)

## Architecture

### The Sub-Pixel Scrolling Problem

Without SceneRenderer, smooth camera scrolling at zoom levels other than 1.0x causes visual artifacts:

1. **Direct per-pixel rendering**: Camera at position (100.5, 200.7) means:
   - Tile at (0,0) renders at screen position (-100.5, -200.7)
   - At 2x zoom, this fractional pixel causes bilinear filtering artifacts
   - Tiles appear to "shimmer" as camera moves between pixel boundaries

2. **SceneRenderer solution**:
   - All content renders with **floored** camera coordinates (100, 200)
   - Sub-pixel offset (0.5, 0.7) applied during final composite
   - Tiles remain pixel-aligned in intermediate texture
   - Only the final composite uses linear filtering for smooth scrolling

### Intermediate Texture Pattern

```
┌─────────────────────────────────────────────────────────────┐
│                     beginScene()                             │
│  - Set intermediate texture as render target                │
│  - Clear with transparent black                             │
│  - Return SceneContext with floored camera coordinates      │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   World Rendering                            │
│  - WorldManager::render() uses ctx.flooredCameraX/Y         │
│  - Tiles render at pixel-aligned positions                  │
│  - No fractional pixel math = no shimmer                    │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   Entity Rendering                           │
│  - Entities use ctx.cameraX/Y (also floored)                │
│  - All entities pixel-aligned in intermediate texture       │
│  - Followed entity uses ctx.cameraCenter to avoid jitter    │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      endScene()                              │
│  - Restore screen as render target                          │
│  - Apply zoom scale (SDL_SetRenderScale)                    │
│  - Composite with sub-pixel offset for smooth scrolling     │
│  - Reset render scale to 1.0 for UI                         │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                     UI Rendering                             │
│  - Renders at native resolution (scale = 1.0)               │
│  - Not affected by camera zoom or position                  │
└─────────────────────────────────────────────────────────────┘
```

## SceneContext Struct

The `beginScene()` method returns a `SceneContext` containing all render parameters:

```cpp
struct SceneContext {
    // Camera position for entities (floored - sub-pixel via composite offset)
    float cameraX{0.0f};
    float cameraY{0.0f};

    // Camera position for tiles (floored - pixel-aligned, same as cameraX/Y)
    float flooredCameraX{0.0f};
    float flooredCameraY{0.0f};

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
| `cameraX/Y` | Entity rendering | Floored, same as flooredCameraX/Y |
| `flooredCameraX/Y` | Tile rendering | Explicitly named for clarity |
| `viewWidth/Height` | Visibility culling | At 1x scale, divide by zoom for world units |
| `zoom` | Render scaling | Applied automatically in endScene() |
| `cameraCenter` | Followed entity position | Use instead of entity.getPosition() |
| `valid` | Error checking | Returns false if beginScene() failed |

## API Reference

### Construction

```cpp
SceneRenderer();                        // Default constructor
~SceneRenderer();                       // Destructor
SceneRenderer(SceneRenderer&&);         // Move constructor
SceneRenderer& operator=(SceneRenderer&&); // Move assignment
// Non-copyable (owns texture resources)
```

### Scene Lifecycle

#### `SceneContext beginScene(SDL_Renderer* renderer, Camera& camera, float interpolationAlpha)`

Begins scene rendering by setting up the intermediate texture.

**Parameters:**
- `renderer`: SDL renderer
- `camera`: Camera for position and zoom
- `interpolationAlpha`: Interpolation alpha for smooth rendering (0.0 to 1.0)

**Returns:** SceneContext with render parameters, or invalid context on failure

**Usage:**
```cpp
SceneContext ctx = sceneRenderer.beginScene(renderer, camera, alpha);
if (!ctx) {
    // Handle error - fallback to direct rendering
    return;
}
```

#### `void endScene(SDL_Renderer* renderer)`

Ends scene rendering and composites to screen.

**Actions:**
1. Restores screen as render target
2. Applies zoom via `SDL_SetRenderScale()`
3. Sets LINEAR filtering for smooth sub-pixel composite
4. Draws intermediate texture with sub-pixel offset
5. Restores NEAREST filtering for next frame
6. Resets render scale to 1.0 for UI rendering

#### `bool isSceneActive() const`

Returns true if `beginScene()` was called without matching `endScene()`.

## Usage Examples

### Basic Integration (GamePlayState)

```cpp
class GamePlayState : public GameState {
private:
    Camera m_camera;
    SceneRenderer m_sceneRenderer;  // Owned by GameState

public:
    void render(float interpolationAlpha) override {
        auto& renderer = GameEngine::Instance().getRenderer();

        // Begin scene - get render context
        auto ctx = m_sceneRenderer.beginScene(renderer, m_camera, interpolationAlpha);
        if (!ctx) {
            // Fallback or error handling
            return;
        }

        // Render world tiles (use floored camera)
        WorldManager::Instance().render(
            renderer,
            ctx.flooredCameraX, ctx.flooredCameraY,
            static_cast<int>(ctx.viewWidth),
            static_cast<int>(ctx.viewHeight)
        );

        // Render entities (use floored camera - same values)
        for (auto& entity : m_entities) {
            entity->render(renderer, ctx.cameraX, ctx.cameraY, interpolationAlpha);
        }

        // Render followed entity using cameraCenter (avoids jitter)
        if (mp_Player) {
            // Player renders at camera center to avoid double-interpolation
            mp_Player->renderAtCameraCenter(renderer, ctx.cameraCenter, interpolationAlpha);
        }

        // End scene - composite with zoom and sub-pixel offset
        m_sceneRenderer.endScene(renderer);

        // UI renders after endScene (at 1.0 scale)
        UIManager::Instance().render();
    }
};
```

### Followed Entity Rendering

The `cameraCenter` field solves the "double interpolation jitter" problem:

```cpp
// WRONG: Double interpolation causes jitter
// Entity interpolates its position
// Camera interpolates to follow entity
// Net effect: visual jitter as both interpolate independently
void Player::render(float cameraX, float cameraY, float alpha) {
    Vector2D pos = interpolate(m_previousPos, m_currentPos, alpha);
    Vector2D screenPos = pos - Vector2D(cameraX, cameraY);
    // Entity and camera interpolate differently = jitter
}

// CORRECT: Use cameraCenter for followed entity
void Player::renderAtCameraCenter(SDL_Renderer* r, const Vector2D& center, float alpha) {
    // Followed entity is always at camera center
    // No separate interpolation needed
    float screenX = (m_viewWidth / 2.0f);
    float screenY = (m_viewHeight / 2.0f);
    // Smooth and stable
}
```

### Error Handling

```cpp
void GameState::render(float alpha) {
    auto ctx = m_sceneRenderer.beginScene(renderer, m_camera, alpha);

    if (!ctx) {
        // SceneRenderer failed - render directly without zoom features
        GAMESTATE_WARN("SceneRenderer unavailable, using direct rendering");
        renderDirect(renderer, alpha);
        return;
    }

    // Normal rendering with SceneRenderer
    // ...

    m_sceneRenderer.endScene(renderer);
}
```

## Technical Details

### Texture Management

- **Format**: RGBA8888 with target access
- **Blend Mode**: SDL_BLENDMODE_BLEND (alpha compositing)
- **Scale Mode**: NEAREST (pixel-perfect) for tile rendering, LINEAR for final composite
- **Padding**: 128 pixels extra for sprite overhang at edges

### Sub-Pixel Offset Calculation

```cpp
// In beginScene():
float flooredCameraX = std::floor(rawCameraX);
float flooredCameraY = std::floor(rawCameraY);
m_subPixelOffsetX = rawCameraX - flooredCameraX;  // 0.0 to ~1.0
m_subPixelOffsetY = rawCameraY - flooredCameraY;  // 0.0 to ~1.0

// In endScene():
// Negative offset shifts scene to compensate for floored positions
SDL_FRect destRect = {-m_subPixelOffsetX, -m_subPixelOffsetY, viewW, viewH};
SDL_RenderTexture(renderer, m_intermediateTexture.get(), &srcRect, &destRect);
```

### Zoom Application

```cpp
// In endScene():
SDL_SetRenderScale(renderer, m_currentZoom, m_currentZoom);  // Apply zoom
// ... composite ...
SDL_SetRenderScale(renderer, 1.0f, 1.0f);  // Reset for UI
```

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Texture Memory | ~8MB @ 1920x1080 | RGBA8888 + padding |
| Per-Frame Cost | ~0.1ms | One texture copy |
| Texture Resize | Rare | Only when viewport grows |

### Best Practices

1. **Create once, reuse**: SceneRenderer owns its texture across frames
2. **Match Camera lifetime**: Create SceneRenderer alongside Camera in GameState
3. **Check context validity**: Always verify `ctx.valid` or use `if (!ctx)`
4. **UI after endScene**: Render all UI elements after `endScene()` returns

## Integration with Other Systems

### Camera

SceneRenderer requires a Camera reference for:
- Position (via `getRenderOffset()` for interpolation)
- Zoom level (via `getZoom()`)
- Viewport dimensions (via `getViewport()`)

### WorldManager

WorldManager's `render()` method accepts the floored camera coordinates:
```cpp
worldManager.render(renderer, ctx.flooredCameraX, ctx.flooredCameraY, viewW, viewH);
```

### Entity Rendering

Entities use the same floored camera coordinates. The followed entity should use `cameraCenter` to avoid jitter.

## Related Documentation

- **[Camera](Camera.md)** - Camera position, zoom, and interpolation
- **[WorldManager](../managers/WorldManager.md)** - Tile rendering with chunks
- **[GameEngine](../core/GameEngine.md)** - Render loop and interpolation alpha
