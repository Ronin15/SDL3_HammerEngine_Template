# FrameProfiler Documentation

**Where to find the code:**
- Header: `include/utils/FrameProfiler.hpp`
- Implementation: `src/utils/FrameProfiler.cpp`

## Overview

FrameProfiler is a debug-only frame timing system that detects and reports performance hitches. It provides three-tier profiling (Frame → Manager → Render phases), automatic hitch logging, and a live F3 debug overlay. In Release builds, all profiler code compiles to nothing.

### Key Features

- **Three-Tier Timing**: Frame phases, manager phases, and render sub-phases
- **Hitch Detection**: Automatic logging when frame time exceeds threshold
- **Live Overlay**: F3 toggles real-time timing display
- **Zero Release Cost**: Completely empty in Release builds (no overhead)
- **RAII Timers**: Scoped timers for automatic begin/end

## Quick Start

```cpp
#include "utils/FrameProfiler.hpp"

// In GameEngine::update()
void GameEngine::update(float dt) {
    PROFILE_PHASE(FramePhase::Update);

    {
        PROFILE_MANAGER(ManagerPhase::AI);
        AIManager::Instance().update(dt);
    }

    {
        PROFILE_MANAGER(ManagerPhase::Collision);
        CollisionManager::Instance().update(dt);
    }
}

// In GameEngine::render()
void GameEngine::render(float alpha) {
    PROFILE_PHASE(FramePhase::Render);

    {
        PROFILE_RENDER(RenderPhase::WorldTiles);
        WorldManager::Instance().render(renderer, cameraX, cameraY, w, h);
    }

    {
        PROFILE_RENDER(RenderPhase::UI);
        UIManager::Instance().render();
    }
}
```

## Profiling Hierarchy

### FramePhase (Top Level)

| Phase | Description |
|-------|-------------|
| `Events` | SDL event polling and input handling |
| `Update` | Game logic, physics, AI |
| `Render` | Drawing commands |
| `Present` | SDL_RenderPresent / VSync wait |

### ManagerPhase (Update Breakdown)

| Phase | Description |
|-------|-------------|
| `Event` | EventManager processing |
| `GameState` | GameStateManager update |
| `AI` | AIManager update |
| `Particle` | ParticleManager update |
| `Pathfinder` | PathfinderManager update |
| `Collision` | CollisionManager update |
| `BackgroundSim` | BackgroundSimulationManager update |

### RenderPhase (Render Breakdown)

| Phase | Description |
|-------|-------------|
| `BeginScene` | SceneRenderer setup |
| `WorldTiles` | TileRenderer chunk drawing |
| `Entities` | NPCs, player rendering |
| `EndScene` | Composite to screen |
| `UI` | UIManager render |

### GPU-Specific Render Phases

| Phase | Description |
|-------|-------------|
| `GPUCmdBuffer` | Command buffer acquisition |
| `GPUSwapchain` | Swapchain texture acquisition (VSync wait) |
| `GPUVertexMap` | Vertex pool mapping |
| `GPUCopyPass` | Begin copy pass |
| `GPUUpload` | Vertex/texture uploads |
| `GPUScenePass` | Scene render pass |
| `GPUSwapPass` | Swapchain render pass |
| `GPUSubmit` | Command buffer submission |

## RAII Timers

### ScopedPhaseTimer

Times a frame phase (Events, Update, Render, Present):

```cpp
void GameEngine::processEvents() {
    ScopedPhaseTimer timer(FramePhase::Events);
    // Event handling code...
} // Timer ends automatically
```

### ScopedManagerTimer

Times a manager's update:

```cpp
void GameEngine::updateManagers(float dt) {
    {
        ScopedManagerTimer timer(ManagerPhase::AI);
        AIManager::Instance().update(dt);
    }
    {
        ScopedManagerTimer timer(ManagerPhase::Collision);
        CollisionManager::Instance().update(dt);
    }
}
```

### ScopedRenderTimer

Times a render sub-phase (CPU command queue time):

```cpp
void GamePlayState::render(float alpha) {
    {
        ScopedRenderTimer timer(RenderPhase::WorldTiles);
        WorldManager::Instance().render(renderer, cameraX, cameraY, w, h);
    }
}
```

### ScopedRenderTimerGPU

Times a render sub-phase with GPU flush (actual GPU time):

```cpp
void GamePlayState::render(float alpha) {
    {
        ScopedRenderTimerGPU timer(RenderPhase::EndScene, renderer);
        m_sceneRenderer.endScene(renderer);
    } // Flushes GPU before ending timer
}
```

## Macros

For convenience, use macros instead of explicit timer construction:

| Macro | Usage |
|-------|-------|
| `PROFILE_FRAME_BEGIN()` | Call at frame start |
| `PROFILE_FRAME_END()` | Call at frame end |
| `PROFILE_PHASE(p)` | Scoped phase timer |
| `PROFILE_MANAGER(m)` | Scoped manager timer |
| `PROFILE_RENDER(r)` | Scoped render timer |
| `PROFILE_RENDER_GPU(r, renderer)` | Scoped render timer with GPU flush |

In Release builds, all macros compile to `((void)0)`.

## API Reference

### Configuration

```cpp
void setThresholdMs(double ms);      // Set hitch threshold (default: 20ms)
double getThresholdMs() const;       // Get current threshold
```

### Overlay Control

```cpp
void toggleOverlay();                // Toggle F3 debug overlay
bool isOverlayVisible() const;       // Check overlay visibility
```

### Suppression

```cpp
void suppressFrames(uint32_t count = 5);  // Suppress hitch detection
bool isSuppressed() const;                // Check if suppressed
```

Use suppression during:
- State transitions
- Resource loading
- Engine initialization

### Statistics

```cpp
uint32_t getHitchCount() const;              // Total hitches since startup
uint64_t getFrameCount() const;              // Current frame number
double getLastFrameTimeMs() const;           // Last frame's total time
double getPhaseTimeMs(FramePhase p) const;   // Time for specific phase
double getManagerTimeMs(ManagerPhase m) const; // Time for specific manager
```

## Hitch Detection

When a frame exceeds the threshold (default 20ms), the profiler logs:

```
[PROFILER] Hitch detected: 25.3ms (threshold: 20.0ms)
  Cause: Update phase (12.5ms)
  Worst manager: AI (8.2ms)
```

### Threshold Configuration

```cpp
// Set threshold to 25ms for 40fps target
FrameProfiler::Instance().setThresholdMs(25.0);

// Set threshold to 16.67ms for strict 60fps
FrameProfiler::Instance().setThresholdMs(16.67);
```

### VSync Exclusion

The profiler excludes VSync wait time from hitch calculations. For GPU rendering, `GPUSwapchain` time (where VSync wait occurs) is automatically subtracted from the frame total.

## F3 Debug Overlay

Press F3 to toggle the live timing overlay, which shows:

- Total frame time
- Phase breakdown (Events, Update, Render, Present)
- Hitch indicators (arrows pointing to slow phases)
- Threshold indicator

The overlay uses UIManager for rendering and avoids per-frame allocations by caching text strings.

## Usage in GameEngine

```cpp
void GameEngine::runLoop() {
    while (m_running) {
        PROFILE_FRAME_BEGIN();

        {
            PROFILE_PHASE(FramePhase::Events);
            processEvents();
        }

        {
            PROFILE_PHASE(FramePhase::Update);
            update(m_dt);
        }

        {
            PROFILE_PHASE(FramePhase::Render);
            render(m_interpolation);
        }

        {
            PROFILE_PHASE(FramePhase::Present);
            present();
        }

        PROFILE_FRAME_END();
    }
}
```

## State Transition Suppression

Suppress hitches during expected delays:

```cpp
void GameStateManager::changeState(StateID newState) {
    // Suppress hitches during state transition
    FrameProfiler::Instance().suppressFrames(10);

    mp_currentState->exit();
    mp_currentState = createState(newState);
    mp_currentState->enter();
}
```

## Release Build Behavior

In Release builds (`NDEBUG` defined):
- `FrameProfiler` class is a stub with empty methods
- All macros expand to `((void)0)`
- Zero runtime overhead
- Zero code bloat

```cpp
// Release build - this compiles to nothing
PROFILE_PHASE(FramePhase::Update);  // ((void)0)
```

## Performance Characteristics

| Metric | Debug | Release |
|--------|-------|---------|
| Overhead per timer | ~100ns | 0ns |
| Memory | ~2KB | 0 bytes |
| Text rendering | ~0.5ms (overlay on) | N/A |

## Related Documentation

- **[GPURendering](../gpu/GPURendering.md)** - GPU-specific profiler phases
- **[GameEngine](../core/GameEngine.md)** - Frame loop integration
- **[TimestepManager](../managers/TimestepManager.md)** - Frame timing
