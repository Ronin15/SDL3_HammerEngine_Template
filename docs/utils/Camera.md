# Camera

**Code:** `include/utils/Camera.hpp`, `src/utils/Camera.cpp`

## Overview

`Camera` is a non-singleton 2D camera with free, follow, and fixed modes, zoom levels, world clamping, and coordinate conversion helpers.

The important branch-local behavior change is in follow mode: coordinate conversions now use the last rendered center rather than the raw live position.

## Follow Mode

Follow mode now combines:

- subtle damping / smoothing toward the target
- `m_lastRenderedCenter` caching
- conversion helpers tied to what was actually rendered last frame

Why this matters:

- screen-to-world and world-to-screen conversions stay aligned with the viewport the player saw
- click/interaction coordinates no longer drift when the camera is mid-smoothing

## Coordinate Conversion

In follow mode:

- `worldToScreen(...)` uses `m_lastRenderedCenter`
- `screenToWorld(...)` uses `m_lastRenderedCenter`

In free/fixed mode:

- conversions use the current camera position directly

This branch makes the rendered camera center the authoritative conversion source for smoothed follow cameras.

## Practical Guidance

- call `update(deltaTime)` every frame before rendering
- keep viewport dimensions synchronized with the current logical render size
- if you snap or directly reposition the camera, the rendered-center cache is synchronized accordingly

## Related Docs

- [GameEngine](../core/GameEngine.md)
- [GPU Rendering](../gpu/GPURendering.md)
