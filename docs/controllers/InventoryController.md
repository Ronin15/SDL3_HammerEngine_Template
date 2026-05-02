# InventoryController

## Role

`InventoryController` is a state-scoped gameplay controller for player inventory interaction and inventory UI. It owns:

- pickup interaction within `PICKUP_RADIUS`
- inventory panel creation and visibility
- inventory grid refresh after `ResourceChangeEvent`
- gear slot display and equip/unequip clicks
- hotbar assignment handoff to `HudController`
- event-log entries for resource changes

It does not own inventory storage. Player inventory and equipment state remain in the player/EDM resource path; the controller reflects and mutates that state through existing player/resource APIs.

## Runtime Flow

`GamePlayState` adds the controller with `m_controllers.add<InventoryController>(mp_Player)`, calls `initializeInventoryUI()` after UI setup, and then lets `m_controllers.updateAll(dt)` plus state input routing drive it.

Typical input order in gameplay:

1. `OpenInventory` toggles the inventory panel.
2. `Interact` prioritizes merchant trade, then pickup, then harvest interaction.
3. When the inventory is open, slot clicks may equip gear, consume usable items, or start a hotbar assignment.
4. During hotbar assignment, the controller tracks the dragged resource handle. Inventory-origin drops assign through `HudController::assignHotbarItem(...)`; hotbar-origin drags reorder through `HudController::moveHotbarItem(...)`.

## UI Contract

- `INVENTORY_PANEL_ID`, `INVENTORY_TITLE_ID`, `INVENTORY_STATUS_ID`, and `EVENT_LOG_ID` are stable component IDs used by gameplay state and tests.
- `refreshInventoryUI()` rebuilds the grid entries, so click handlers must copy any `ResourceHandle` they need before triggering refresh.
- Gear slots are displayed inside the existing inventory overlay; inventory items remain visible while gear is assigned.
- Pause/resume hides or restores inventory and hotbar UI through controller APIs instead of direct component ownership in the state.

## Rules

- Keep this controller state-scoped; it should not become a resource manager.
- Keep gameplay stat effects separate from first-pass UI/state equipment handling unless a feature explicitly wires those effects.
- Unknown equipment slot metadata should fail equip instead of silently defaulting to weapon.

## Related Docs

- [HudController](HudController.md)
- [ControllerRegistry](ControllerRegistry.md)
- [EntityDataManager](../managers/EntityDataManager.md)
- [ResourceHandle System](../utils/ResourceHandle_System.md)
