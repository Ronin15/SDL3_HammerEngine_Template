# GameOverState

**Code:** `include/gameStates/GameOverState.hpp`, `src/gameStates/GameOverState.cpp`

## Overview

`GameOverState` is the dedicated player-death screen. It pauses gameplay globally and builds a simple overlay UI.

## Behavior

- enables global pause on entry
- creates overlay, title, message, and two centered buttons
- supports `Retry` and `Main Menu`
- supports keyboard shortcuts:
  - `R` -> retry gameplay
  - `M` -> return to main menu

## Rendering

Renderer path:

- `update()` calls `UIManager::update(0.0f)`
- `render()` calls `UIManager::render(...)`

GPU path:

- `recordGPUVertices(...)` records UI vertices
- `renderGPUUI(...)` renders UI during the swapchain pass

## Exit

`exit()` uses `UIManager::prepareForStateTransition()` so the overlay is removed before the next state enters.
