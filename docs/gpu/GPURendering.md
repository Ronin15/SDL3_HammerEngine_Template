# GPU Rendering System Documentation

**Where to find the code:**
- Headers: `include/gpu/`
- Implementation: `src/gpu/`
- Scene Renderer: `include/utils/GPUSceneRenderer.hpp`
- Shaders: `res/shaders/`
- Tests: `tests/gpu/`

## Overview

The SDL3 GPU rendering system provides a modern graphics pipeline built on SDL3's GPU API. It runs alongside (not replacing) the SDL_Renderer path, enabled via `-DUSE_SDL3_GPU=ON`.

### Key Features

- **Two-Pass Rendering**: Scene texture for world content, then composite to swapchain with effects
- **Triple-Buffered Vertex Pools**: Zero CPU stall during GPU rendering
- **Sprite Batching**: Up to 25,000 sprites per batch
- **Multi-Backend Shaders**: Automatic SPIR-V (Vulkan) or MSL (Metal) selection
- **Day/Night Lighting**: Ambient tinting via composite shader

## Architecture

### Component Hierarchy

```
GPUDevice (singleton)          - SDL_GPUDevice wrapper
    ↓
GPURenderer (singleton)        - Frame orchestration
    ├── Pipelines             - Graphics pipeline states
    ├── Samplers              - Texture sampling config
    ├── VertexPools           - Triple-buffered vertex storage
    ├── SpriteBatches         - Batched sprite recording
    └── SceneTexture          - Intermediate render target
    ↓
GPUSceneRenderer (per-state)   - Scene rendering facade
    └── GPUSceneContext       - Per-frame render parameters
```

### Core Components

| Component | Location | Purpose |
|-----------|----------|---------|
| **GPUDevice** | `gpu/GPUDevice.hpp` | Singleton device wrapper, shader format queries |
| **GPURenderer** | `gpu/GPURenderer.hpp` | Frame lifecycle, pipeline state, uniforms |
| **GPUShaderManager** | `gpu/GPUShaderManager.hpp` | Shader loading and caching |
| **GPUBuffer** | `gpu/GPUBuffer.hpp` | RAII GPU buffer wrapper |
| **GPUTexture** | `gpu/GPUTexture.hpp` | RAII texture wrapper |
| **GPUPipeline** | `gpu/GPUPipeline.hpp` | RAII pipeline wrapper |
| **GPUVertexPool** | `gpu/GPUVertexPool.hpp` | Triple-buffered vertex storage |
| **SpriteBatch** | `gpu/SpriteBatch.hpp` | Batched sprite recording |
| **GPUSceneRenderer** | `utils/GPUSceneRenderer.hpp` | Scene rendering facade |

## Frame Lifecycle

### Recording Phase (in `recordGPUVertices`)

```
GameEngine::recordGPUVertices()
├── gpuRenderer.beginFrame()           // Map vertex buffers, start copy pass
├── gpuSceneRenderer.beginScene()      // Setup sprite batch, get context
│   ├── worldMgr.recordGPUTiles(ctx)   // Draw world tiles to sprite batch
│   ├── npcCtrl.recordGPU(ctx)         // Draw NPCs to sprite batch
│   └── resourceCtrl.recordGPU(ctx)    // Draw resources to sprite batch
├── gpuSceneRenderer.endSpriteBatch()  // Finalize atlas-based sprites
├── player.recordGPUVertices()         // Record to entity batch (separate texture)
└── particleMgr.recordGPUVertices()    // Record to particle pool
```

### Render Phase (in `renderGPUScene`)

```
GameEngine::renderGPUScene()
├── gpuRenderer.beginScenePass()       // End copy, begin scene render pass
├── gpuSceneRenderer.renderScene()     // Issue draw calls for world sprites
├── renderEntities()                   // Issue draw calls for entities
└── renderParticles()                  // Issue draw calls for particles
```

### Composite Phase (in `renderGPUUI`)

```
GameEngine::renderGPUUI()
├── gpuRenderer.beginSwapchainPass()   // End scene, begin swapchain pass
├── gpuRenderer.renderComposite()      // Composite scene with day/night + zoom
└── uiMgr.renderGPU()                  // Render UI directly to swapchain
```

### Finalize Phase

```
GameEngine::present()
└── gpuRenderer.endFrame()             // Submit command buffer
```

## Two-Pass Rendering Architecture

### Why Two Passes?

1. **Scene Texture** (intermediate): All world content renders here at native resolution
2. **Swapchain** (final): Composite shader applies zoom, sub-pixel offset, and day/night tinting

This separation enables:
- Pixel-perfect tile rendering (floored camera coordinates)
- Smooth sub-pixel scrolling via composite offset
- Global lighting effects without per-sprite shader changes
- Clean separation between world and UI rendering

### Pass Details

| Pass | Target | Content | Pipelines |
|------|--------|---------|-----------|
| Copy | N/A | Vertex uploads | N/A |
| Scene | Scene Texture | Tiles, NPCs, particles | SpriteOpaque, SpriteAlpha, Particle |
| Swapchain | Screen | Composite + UI | Composite, UISprite, UIPrimitive |

## Vertex Pool System

### Triple-Buffering

```
Frame N: GPU reads buffer 0
         CPU writes buffer 2
Frame N+1: GPU reads buffer 1
           CPU writes buffer 0
```

Benefits:
- Zero GPU stalls waiting for CPU
- No per-frame memory allocation
- Efficient batch submission

### Pool Types

| Pool | Vertex Type | Capacity | Usage |
|------|-------------|----------|-------|
| Sprite | SpriteVertex (20B) | 150K vertices | World tiles |
| Entity | SpriteVertex (20B) | 50K vertices | Player, NPCs |
| Particle | ColorVertex (12B) | 100K vertices | Particles |
| Primitive | ColorVertex (12B) | 10K vertices | Debug lines |
| UI | SpriteVertex (20B) | 20K vertices | UI elements |

## GameState Integration

### Required Methods

```cpp
class MyGameState : public GameState {
public:
    // Record GPU vertices (called before render pass begins)
    void recordGPUVertices(float interpolation) override;

    // Render scene to scene texture (called during scene pass)
    void renderGPUScene(GPURenderer& gpuRenderer,
                        SDL_GPURenderPass* scenePass,
                        float interpolation) override;

    // Render UI to swapchain (called during swapchain pass)
    void renderGPUUI(GPURenderer& gpuRenderer,
                     SDL_GPURenderPass* uiPass) override;
};
```

### Implementation Example

```cpp
void GamePlayState::recordGPUVertices(float interpolation) {
    auto& gpuRenderer = GPURenderer::Instance();

    // Begin scene and get context
    auto ctx = m_gpuSceneRenderer.beginScene(gpuRenderer, m_camera, interpolation);
    if (!ctx) return;

    // Record world tiles (uses ctx.spriteBatch->draw())
    WorldManager::Instance().recordGPUTiles(ctx);

    // Record NPCs
    m_controllers.get<NPCRenderController>()->recordGPU(ctx);

    // Finalize atlas-based sprites
    m_gpuSceneRenderer.endSpriteBatch();

    // Record player (separate entity batch with different texture)
    if (mp_Player) {
        mp_Player->recordGPUVertices(gpuRenderer, ctx.cameraX, ctx.cameraY,
                                     ctx.viewWidth, ctx.viewHeight, interpolation);
    }

    // Record particles
    ParticleManager::Instance().recordGPUVertices(gpuRenderer,
                                                   ctx.cameraX, ctx.cameraY,
                                                   ctx.viewWidth, ctx.viewHeight);

    m_gpuSceneRenderer.endScene();
}

void GamePlayState::renderGPUScene(GPURenderer& gpuRenderer,
                                   SDL_GPURenderPass* scenePass,
                                   float interpolation) {
    // Render recorded sprites to scene texture
    m_gpuSceneRenderer.renderScene(gpuRenderer, scenePass);

    // Render entities
    renderEntitiesGPU(gpuRenderer, scenePass, interpolation);
}

void GamePlayState::renderGPUUI(GPURenderer& gpuRenderer,
                                SDL_GPURenderPass* uiPass) {
    UIManager::Instance().renderGPU(gpuRenderer, uiPass);
}
```

## Shader System

### File Locations

```
res/shaders/
├── sprite.vert.glsl     # Sprite vertex shader (source)
├── sprite.frag.glsl     # Sprite fragment shader (source)
├── color.vert.glsl      # Primitive vertex shader (source)
├── color.frag.glsl      # Primitive fragment shader (source)
├── composite.vert.glsl  # Fullscreen quad vertex shader (source)
├── composite.frag.glsl  # Composite fragment shader (source)
├── sprite.vert.spv      # SPIR-V compiled (Vulkan)
├── sprite.frag.spv      # SPIR-V compiled (Vulkan)
├── sprite.vert.metal    # MSL compiled (Metal)
└── sprite.frag.metal    # MSL compiled (Metal)
```

### Format Selection

GPUShaderManager automatically selects the correct format:
- **Vulkan** (Linux, Windows): SPIR-V (`.spv`)
- **Metal** (macOS, iOS): MSL (`.metal`)

### Shader Compilation

Shaders are pre-compiled during CMake build (when `-DUSE_SDL3_GPU=ON`):
- GLSL → SPIR-V via `glslangValidator`
- GLSL → MSL via `spirv-cross`

## Day/Night Lighting

### Controller Integration

DayNightController calls `GPURenderer::setDayNightParams()` each frame after interpolating lighting values:

```cpp
void DayNightController::updateGPULighting() {
    GPURenderer::Instance().setDayNightParams(
        m_currentR, m_currentG, m_currentB, m_currentA
    );
}
```

### Composite Shader Application

The composite fragment shader applies ambient tinting:

```glsl
vec4 scene = texture(sceneTexture, texCoord);
vec3 tinted = mix(scene.rgb, scene.rgb * ambientColor.rgb, ambientColor.a);
fragColor = vec4(tinted, scene.a);
```

## API Reference

### GPURenderer

```cpp
// Frame lifecycle
void beginFrame();                           // Start frame, map buffers
SDL_GPURenderPass* beginScenePass();         // End copy, begin scene pass
SDL_GPURenderPass* beginSwapchainPass();     // End scene, begin swapchain pass
void endFrame();                             // Submit command buffer

// Composite control
void setCompositeParams(float zoom, float subPixelX, float subPixelY);
void setDayNightParams(float r, float g, float b, float alpha);
void renderComposite(SDL_GPURenderPass* pass);

// Uniforms
void pushViewProjection(SDL_GPURenderPass* pass, const float* matrix);
static void createOrthoMatrix(float left, float right, float bottom, float top, float* out);

// Accessors
SDL_GPUGraphicsPipeline* getSpriteAlphaPipeline() const;
GPUVertexPool& getSpriteVertexPool();
SpriteBatch& getSpriteBatch();
```

### GPUSceneRenderer

```cpp
// Scene lifecycle
GPUSceneContext beginScene(GPURenderer& gpuRenderer, Camera& camera, float alpha);
void endSpriteBatch();                       // Finalize atlas sprites
void endScene();                             // Cleanup
void renderScene(GPURenderer& gpuRenderer, SDL_GPURenderPass* scenePass);
```

### GPUSceneContext

```cpp
struct GPUSceneContext {
    float cameraX, cameraY;          // Floored camera position
    float viewWidth, viewHeight;     // View dimensions at 1x scale
    float zoom;                      // Current zoom level
    float interpolationAlpha;        // For smooth rendering
    Vector2D cameraCenter;           // Camera world position
    SpriteBatch* spriteBatch;        // For atlas-based drawing
    bool valid;                      // Context validity
};
```

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Scene Texture | ~8MB @ 1080p | R8G8B8A8_UNORM |
| Sprite Batch | 25K sprites | 6 vertices each |
| Vertex Pool | 150K vertices | Triple-buffered |
| Draw Calls | ~10-20/frame | Batched by texture |

## Related Documentation

- **[SceneRenderer](../utils/SceneRenderer.md)** - SDL_Renderer path equivalent
- **[WorldRenderPipeline](../utils/WorldRenderPipeline.md)** - SDL_Renderer rendering facade
- **[DayNightController](../controllers/DayNightController.md)** - Lighting control
- **[FrameProfiler](../utils/FrameProfiler.md)** - GPU timing phases
