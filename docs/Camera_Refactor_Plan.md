# Camera-Driven Rendering Refactor Plan (SDL3_HammerEngine_Template)

## Purpose

Transition the engine's rendering system to a robust, camera-centric architecture supporting multiple cameras (split-screen, minimap, cinematic), event-driven updates, and high-performance culling. This plan is designed for agent implementation, with explicit references, edge case notes, and a stepwise checklist.

---

## Architectural Principles

- **Camera(s) as the Source of Truth:**  
  All rendering (entities, particles, UI) is driven by camera data—viewport, transforms, effects, and world-to-screen logic.
- **Separation of Concerns:**  
  - Managers (EntityManager, ParticleManager, AIManager) handle update culling and provide camera-aware queries for visible objects.
  - The engine/game state orchestrates rendering, using camera(s) for render culling and calling object render methods.
- **Event-Driven Updates:**  
  Camera movement, zoom, viewport changes, and minimap toggling are dispatched as events via EventManager/EventFactory. Managers subscribe to these events to update culling and simulation logic.

---

## Key Files & Subsystems

- `include/utils/Camera.hpp` / `src/utils/Camera.cpp`
- `include/core/GameEngine.hpp` / `src/core/GameEngine.cpp`
- `include/managers/GameStateManager.hpp` / `src/managers/GameStateManager.cpp`
- Game states: `src/gameStates/*`, `include/gameStates/*`
- `include/managers/UIManager.hpp` / `src/managers/UIManager.cpp`
- `include/managers/ParticleManager.hpp` / `src/managers/ParticleManager.cpp`
- Entity/particle rendering code
- Event system: `include/events/EventManager.hpp`, `src/events/EventManager.cpp`, `include/events/EventFactory.hpp`, `src/events/EventFactory.cpp`
- Documentation: `docs/GameEngine.md`, `docs/ui/UIManager_Guide.md`, `docs/managers/ParticleManager.md`, `docs/events/EventManager.md`, `docs/events/EventFactory.md`
- Tests: rendering, camera, UI, events

---

## Implementation Checklist

- [ ] **Architectural Refactor**
  - [ ] Update all rendering code to use Camera transforms (world-to-screen, culling).
  - [ ] Remove global offset-based rendering logic.
  - [ ] Refactor entity, particle, and UI rendering to accept Camera(s) as input.
  - [ ] Ensure all managers (UIManager, ParticleManager, etc.) use Camera for rendering queries.

- [ ] **Multi-Camera Support**
  - [ ] Update Camera API to support multiple active cameras.
  - [ ] Refactor GameEngine/GameStateManager to manage and dispatch multiple cameras.
  - [ ] Implement split-screen and minimap rendering using multiple viewports.
  - [ ] Integrate camera events for viewport/effect updates.

- [ ] **SDL3 Renderer Integration**
  - [ ] Research and implement SDL3 multi-viewport/logical presentation support.
  - [ ] Ensure compatibility with SDL3 render passes and presentation modes.

- [ ] **Culling & Effects**
  - [ ] Integrate camera culling for entities/particles—only render objects within camera viewports.
  - [ ] Managers provide camera-aware queries (e.g., `getVisibleEntities(camera)`).
  - [ ] Implement camera effects (shake, zoom, etc.) in rendering pipeline.

- [ ] **Event System Integration**
  - [ ] Implement camera and minimap events using EventManager and EventFactory.
  - [ ] Update managers to subscribe and respond to camera/minimap events.
  - [ ] Document new event types and update event system docs.

- [ ] **Minimap Support**
  - [ ] Implement dedicated minimap camera and viewport.
  - [ ] Add minimap event types and handlers (toggle, resize, camera move).
  - [ ] Update UIManager, ParticleManager, and entity rendering for minimap.
  - [ ] Add/modify tests for minimap features and event-driven updates.

- [ ] **Documentation & Tests**
  - [ ] Update all relevant documentation for camera-driven rendering and event integration.
  - [ ] Add/modify tests for multi-camera, culling, world-to-screen, effects, and minimap.

- [ ] **Edge Cases & Validation**
  - [ ] Validate rendering for all camera configurations (single, split-screen, minimap).
  - [ ] Test event-driven camera updates and effect propagation.
  - [ ] Ensure UI and overlays are correctly positioned/scaled for each camera.
  - [ ] Minimap overlays/icons must be correctly transformed and culled.
  - [ ] Minimap events should not interfere with main camera events.
  - [ ] Support for toggling, resizing, and repositioning minimap via events.

---

## Performance Principle

- **Critical:**  
  Managers handle update culling for simulation (e.g., AIManager culls distant entities for logic).  
  The engine/game state handles render culling—querying managers for visible objects using camera data, then rendering only those on screen.  
  This separation is essential for performance, scalability, and maintainability.

---

## Edge Case Notes

- **Split-screen:** Each camera must have its own viewport and culling logic.
- **Minimap:** Requires a separate camera with different zoom/position; overlays/icons must be correctly transformed and culled.
- **Camera shake/zoom:** Effects must be isolated per camera.
- **UI overlays:** Must be rendered in correct screen space for each camera.
- **Event integration:** Camera and minimap events must trigger viewport/effect updates in all relevant managers.

---

## References

- [docs/core/GameEngine.md](docs/core/GameEngine.md)
- [docs/ui/UIManager_Guide.md](docs/ui/UIManager_Guide.md)
- [docs/managers/ParticleManager.md](docs/managers/ParticleManager.md)
- [docs/events/EventManager.md](docs/events/EventManager.md)
- [docs/events/EventFactory.md](docs/events/EventFactory.md)
- SDL3 documentation: [SDL3 Wiki](https://wiki.libsdl.org/SDL3/CategoryRender)

---

## Next Steps

- When ready, copy this plan to the appropriate documentation file (e.g., `docs/utils/Camera_Refactor_Plan.md`).
- Proceed with implementation according to the checklist above.
