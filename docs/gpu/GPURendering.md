# GPU Rendering

**Code:** `include/gpu/`, `src/gpu/`

## Overview

The SDL3 GPU path is built around `GPURenderer` and a two-pass scene-plus-swapchain flow. The current branch makes swapchain acquisition explicit instead of hiding it inside a monolithic frame begin.

## Frame Contract

```cpp
bool beginFrame();
bool acquireSwapchainTexture();
SDL_GPURenderPass* beginScenePass();
SDL_GPURenderPass* beginSwapchainPass();
void endFrame();
```

If `acquireSwapchainTexture()` fails, the engine can skip the presentable frame cleanly.

## Pass Layout

1. `beginFrame()`
   - acquire command buffer
   - map upload buffers
   - begin copy/upload work
2. `acquireSwapchainTexture()`
   - acquire the current swapchain texture
3. `beginScenePass()`
   - render world content to the intermediate scene texture
4. `beginSwapchainPass()`
   - composite the scene texture
   - render UI directly to the swapchain
5. `endFrame()`
   - close the active pass
   - submit the command buffer

## Important Branch Details

- UI text is atlas-backed through SDL3_ttf GPU draw sequences
- `GPURenderer` exposes dedicated UI text pipelines:
  - `getUITextAlphaPipeline()`
  - `getUITextSDFPipeline()`
- vertex pools are triple-buffered
- UI/menu text should be snapped to whole pixels before vertex emission

## GameState Integration

GPU-capable states usually provide:

```cpp
recordGPUVertices(...)
renderGPUScene(...)
renderGPUUI(...)
```

GameStates record and issue scene/UI work, but `GameEngine` and `GPURenderer` still own frame lifetime, swapchain lifetime, and presentation.
