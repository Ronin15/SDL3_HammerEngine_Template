# SDL3 HammerEngine Architecture

## Overview

The engine is still layered as:

- Core
- Managers
- GameStates
- Entities / Controllers

The architecture includes:

- `EventManager` remains part of the event-driven architecture and serves as the central event processing hub with main-thread deferred draining, built-in combat processing, and sequence-preserved ordering across combat and non-combat queues
- AI is EDM-backed and executed through `BehaviorExecutors` + `AICommandBus`
- `WorldResourceManager` is a registry/spatial index over EDM, not a quantity store
- GPU rendering uses explicit swapchain acquisition
- `TimestepManager` accepts real display refresh data for cadence snapping
- `GameOverState` is part of the state graph

## Update Flow

Typical frame shape:

1. `GameEngine` starts the frame
2. input and deferred events are processed
3. active GameState updates UI/controllers/state logic
4. managers such as AI, collision, particles, and background simulation run
5. render path draws scene/UI
6. present completes the frame

## Rendering Paths

### SDL_Renderer

- world scene is rendered and composited once
- UI renders afterward
- `GameEngine::present()` performs the one present call for the frame

### SDL3_GPU

```text
beginFrame
acquireSwapchainTexture
beginScenePass
beginSwapchainPass
endFrame
```

States provide scene/UI hooks, but frame lifetime and presentation remain engine-owned.

## GameState Flow

Key states include:

- `LogoState`
- `MainMenuState`
- `SettingsMenuState`
- `LoadingState`
- `GamePlayState`
- demo states
- `GameOverState`

Important transitions:

- `MainMenuState -> LoadingState -> GamePlayState`
- `GamePlayState -> GameOverState`
- AI-heavy demos may also route to `GameOverState`

## State Teardown

AI/world-heavy states follow this pattern:

1. unregister state-owned handlers
2. call `prepareForStateTransition()` on active managers in dependency order
3. clear controllers and UI
4. tear down world/entity state

This matters because deferred events, AI command commits, and WRM spatial indices all participate in runtime state now.
