# CombatController

**Code:** `include/controllers/combat/CombatController.hpp`, `src/controllers/combat/CombatController.cpp`

## Overview

`CombatController` is the player-facing melee combat helper used by gameplay and demo states. It is frame-updatable because it owns:

- attack cooldown timing
- stamina regeneration
- short-lived target HUD state
- reusable query buffers for nearby AI targets

The controller performs hit detection by querying nearby AI/EDM entities directly, applies damage through the `DamageEvent` gameplay path, and updates UI-facing state plus the gameplay event log.

## Core API

```cpp
bool tryAttack();
void update(float deltaTime);

EntityHandle getTargetedHandle() const;
float getTargetDisplayTimer() const;
bool hasActiveTarget() const;
float getTargetHealth() const;
```

Constants:

```cpp
ATTACK_STAMINA_COST = 10.0f
STAMINA_REGEN_RATE = 15.0f
TARGET_DISPLAY_DURATION = 3.0f
ATTACK_COOLDOWN = 0.5f
```

## Runtime Flow

1. `tryAttack()` checks cooldown and player stamina.
2. `performAttack()` queries nearby handles using a reused buffer.
3. valid targets are damaged through the current player/EDM combat path.
4. the hit target is cached in `m_targetedHandle` for HUD display.
5. `update()` regenerates stamina and expires the target frame timer.

## HUD Integration

`CombatController` supplies the data used by combat HUD UI:

- current target handle
- remaining target frame lifetime
- current target health via EDM

This branch also adds dedicated combat HUD helpers in `UIManager`; states should prefer those over ad-hoc target widgets.

## Notes

- The controller owns a reusable `m_nearbyHandlesBuffer` to avoid per-frame allocations.
- `getTargetHealth()` reads the current target health from EDM, so the HUD reflects live data instead of stale cached values.
- The controller updates the gameplay event log as part of player combat feedback; do not expect the old event-only flow from previous docs.
