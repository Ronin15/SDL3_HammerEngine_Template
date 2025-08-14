# Minimap Implementation Plan for HammerEngine UI Manager

## Overview

This document outlines the design and implementation steps for integrating a basic minimap into the HammerEngine UI system. The minimap will display the current area of the world and indicate which regions have been discovered by the player. The design follows the engine's architecture, rendering flow, and code style guidelines.

---

## 1. Architectural Context

- **Rendering:** All rendering is performed via the active `GameState`'s `render()` function, which is called by the `GameEngine`. UI elements, including the minimap, must use the shared renderer/context.
- **UI Manager:** The UI Manager orchestrates all UI widgets and overlays, including the minimap.
- **World Discovery:** Discovery status is tracked per area/tile and updated as the player explores.

---

## 2. Data Structures

### 2.1 Discovery Grid

- Add a discovery grid to the world data structure:
  - Example: `std::vector<std::vector<bool>> discoveredTiles;`
  - Alternatively, use a bitmask or a custom `DiscoveryMap` class for efficiency.
- The grid tracks which tiles/areas have been discovered by the player.

### 2.2 Player Position

- Expose the player's current world coordinates for minimap rendering.

---

## 3. Minimap Widget Design

### 3.1 Class: `MinimapWidget`

- **Responsibilities:**
  - Accept discovery grid and player position.
  - Render discovered/undiscovered areas (e.g., two colors).
  - Draw player marker.
  - Support basic layout (corner overlay, scaling).
- **API Example:**
  - `void setDiscoveryData(const DiscoveryGrid&)`
  - `void setPlayerPosition(const Vec2&)`
  - `void render(SDL_Renderer*)`
  - `void update(float dt)`

### 3.2 Rendering

- The widget does not own or create its own renderer.
- All drawing is performed using the renderer/context provided by the main game loop.

---

## 4. UI Manager Integration

- **UIManager** owns and manages the minimap widget.
- **UIManager::renderUI(SDL_Renderer*)** is called from within `GameState::render()`.
- **MinimapWidget::render(SDL_Renderer*)** is called as part of the UIManager's render pass.
- UIManager handles layout, positioning, and event routing for the minimap.

---

## 5. Data Flow

- **Discovery data and player position** are updated by the game logic.
- **UIManager** receives updated data and passes it to the minimap widget before rendering.
- All rendering and UI updates occur on the main render thread.

---

## 6. Serialization (Optional)

- If persistent discovery status is desired:
  - Serialize the discovery grid as part of the save game system.
  - On load, restore the grid so the minimap reflects previously explored areas.
- If not required, skip this step.

---

## 7. Implementation Steps

1. **Design and add discovery grid to world data.**
2. **Create `MinimapWidget` class with rendering and update logic.**
3. **Integrate minimap widget into UI Manager.**
4. **Connect world/player data to minimap.**
5. **Implement rendering pipeline for minimap overlay.**
6. **(Optional) Add serialization for discovery status.**
7. **Write unit and integration tests for minimap logic and rendering.**
8. **Update documentation and usage guides.**

---

## 8. Example Render Flow

```cpp
// Pseudocode for render flow
void GameState::render(SDL_Renderer* renderer) {
    // Draw world, entities, etc.
    // ...
    // Draw UI overlays
    uiManager.renderUI(renderer);
}

void UIManager::renderUI(SDL_Renderer* renderer) {
    // Draw other UI elements
    // ...
    // Draw minimap overlay
    minimapWidget.render(renderer);
}
```

---

## 9. Extensibility

- The framework allows for future expansion:
  - Multiple minimap styles (fog of war, icons, zoom).
  - Customization of colors, markers, overlays.
  - Event handling (mouse hover, click, tooltips).

---

## 10. Testing & Documentation

- Add unit tests for discovery logic and minimap rendering.
- Add integration tests for minimap updates in gameplay.
- Document minimap architecture, API, and usage in `docs/ui/` and `docs/world/`.

---

## 11. References

- See `docs/Camera_Refactor_Plan.md` for related UI overlay and rendering architecture.
- See `include/core/GameEngine.hpp`, `include/ui/UIManager.hpp`, and `include/world/WorldData.hpp` for integration points.

---

**End of Implementation Plan**