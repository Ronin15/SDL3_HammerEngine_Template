# UIManager Guide

**Code:** `include/managers/UIManager.hpp`, `src/managers/UIManager.cpp`

## Overview

`UIManager` is the engine's main-thread UI system. It owns component creation, layout, theming, animation, input routing, and SDL3_GPU UI rendering.

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

void SomeState::recordGPUVertices(VoidLight::GPURenderer& gpu, float) {
    UIManager::Instance().recordGPUVertices(gpu);
}

void SomeState::renderGPUUI(VoidLight::GPURenderer& gpu,
                            SDL_GPURenderPass* swapchainPass) {
    UIManager::Instance().renderGPU(gpu, swapchainPass);
}
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
updateCombatHUD(playerHealth, playerStamina, hasTarget, targetName, targetHealth);
destroyCombatHUD();
```

Use them from states that own combat flow instead of rebuilding the same health/stamina/target frame wiring by hand.

## Input Focus and Simulated Clicks

`UIManager` tracks keyboard/controller selection separately from mouse hover. Shared menu states use `MenuNavigation::applySelection()` to set the current keyboard selection only after controller navigation is active, keeping mouse-only hover behavior clean.

`simulateClick()` queues the component callback. Tests and state code that depend on the callback having run should call `UIManager::update(...)` after simulating the click.

## Images and Atlas Icons

Use the atlas-backed image APIs for inventory, hotbar, and other resource icons instead of creating ad-hoc texture ownership in controllers. Controllers provide the component ID and texture/resource identity; `UIManager` owns component rendering.

For transparent or steady-looking interactive components, set `hoverColor` and `pressedColor` to match the base background color. Otherwise the UI will visibly flash on hover/press even if the component appears transparent at rest.

## Positioning

After creating components, call `setComponentPositioning()` when the element must survive resize/fullscreen changes. Prefer the existing helpers when they fit:

- `createTitleAtTop()`
- `createButtonAtBottom()`
- `createCenteredButton()`
- `createCenteredDialog()`

## Rendering Notes

- `recordGPUVertices()` records UI primitives, text, and images into GPU vertex pools
- `renderGPU()` submits the recorded UI draw commands during the swapchain UI pass
- the engine, not the state, owns clear/present

## Related Docs

- [GPU Rendering](../gpu/GPURendering.md)
- [SDL3 Logical Presentation Modes](SDL3_Logical_Presentation_Modes.md)
