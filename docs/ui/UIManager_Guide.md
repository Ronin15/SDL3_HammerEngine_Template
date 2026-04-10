# UIManager Guide

**Code:** `include/managers/UIManager.hpp`, `src/managers/UIManager.cpp`

## Overview

`UIManager` is the engine's main-thread UI system. It owns component creation, layout, theming, animation, input routing, and both SDL_Renderer and SDL3_GPU rendering paths.

The branch-specific changes worth knowing:

- GPU text is atlas-backed through SDL3_ttf draw sequences
- raster text placement is snapped to whole pixels before vertex emission
- combat HUD helpers are part of the public API
- UI update belongs in `GameState::update()`, not in `render()`

## Basic Pattern

```cpp
void SomeState::update(float dt) {
    auto& ui = UIManager::Instance();
    ui.update(dt);
}

void SomeState::render(SDL_Renderer* renderer, float alpha) {
    UIManager::Instance().render(renderer);
}

#ifdef USE_SDL3_GPU
void SomeState::renderGPUUI(VoidLight-Framework::GPURenderer& gpu,
                            SDL_GPURenderPass* swapchainPass) {
    UIManager::Instance().renderGPU(gpu, swapchainPass);
}
#endif
```

## GPU Text Path

On the GPU renderer path:

- text draw data comes from `TTF_GetGPUTextDrawData()`
- `UIManager` records atlas-backed text quads into the UI vertex pool
- the renderer selects alpha or SDF pipelines based on the draw sequence type

Guidelines:

- do not generate one GPU texture per UI label
- do not apply UV flips or half-texel offsets
- snap integer-layout text positions before emitting vertices

## Combat HUD Helpers

This branch adds built-in combat HUD helpers used by gameplay/demo states:

```cpp
createCombatHUD();
updateCombatHUD(playerHealth, playerStamina, hasTarget, targetHealth);
destroyCombatHUD();
```

Use them from states that own combat flow instead of rebuilding the same health/stamina/target frame wiring by hand.

## Positioning

After creating components, call `setComponentPositioning()` when the element must survive resize/fullscreen changes. Prefer the existing helpers when they fit:

- `createTitleAtTop()`
- `createButtonAtBottom()`
- `createCenteredButton()`
- `createCenteredDialog()`

## Rendering Notes

- `render()` is for the SDL_Renderer path
- `renderGPU()` is for swapchain-pass UI rendering
- the engine, not the state, owns clear/present

## Related Docs

- [GPU Rendering](../gpu/GPURendering.md)
- [SDL3 Logical Presentation Modes](SDL3_Logical_Presentation_Modes.md)
