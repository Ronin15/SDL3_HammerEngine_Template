# GameplayHUDController

## Role

`GameplayHUDController` is a **state-scoped** controller that bridges committed gameplay events into **read-only HUD state**.

- Subscribes to `EventTypeId::Combat`
- Tracks the current target (NPC) when the player deals damage
- Exposes `hasActiveTarget()`, `getTargetLabel()`, and `getTargetHealth()` for HUD rendering
- Does not mutate UI components directly; the active `GameState` queries it when updating the HUD

The target display is transient and auto-expires after `TARGET_DISPLAY_DURATION`.

## Typical Usage (GamePlayState)

- Add it in `enter()` via `ControllerRegistry`
- Call `m_controllers.updateAll(dt)` each frame
- Query it when rendering/updating combat HUD UI

## Rules

- Controllers remain **state-scoped**; they must not become shadow managers.
- Prefer event-driven updates; only poll EDM for small, transient UI state (like current target health).

## Related Docs

- [Controllers Overview](README.md)
- [ControllerRegistry](ControllerRegistry.md)
- [CombatController](CombatController.md)
- [EventManager](../events/EventManager.md)

