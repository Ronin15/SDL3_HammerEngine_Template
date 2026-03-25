# Combat Events

**Code:** `include/events/CombatEvent.hpp`, `include/events/EntityEvents.hpp`, `include/managers/EventManager.hpp`, `src/managers/EventManager.cpp`, `src/controllers/combat/CombatController.cpp`, `src/ai/behaviors/AttackBehavior.cpp`

## Overview

`CombatEvent` is the notification side of combat and now uses `EventTypeId::CombatNotification`.

The current hot combat mutation path is `DamageEvent` under `EventTypeId::Combat`.

Combat producers build a `DamageEvent`, then `EventManager` applies the core damage result to EDM before combat subscribers run. That means combat handlers now observe post-commit state such as:

- updated target health
- `DamageEvent::getRemainingHealth()`
- `DamageEvent::wasLethal()`
- any EDM-backed knockback/alive-state updates already applied

`include/events/CombatEvent.hpp` still exists, but it is not the main runtime damage path described here.

## Active Runtime Flow

```text
CombatController / AI behavior
    -> acquire or create DamageEvent
    -> EventManager dispatch under EventTypeId::Combat
    -> EventManager resolves EDM target
    -> EventManager applies health + knockback + lethal handling
    -> EventManager updates DamageEvent result fields
    -> EventManager dispatches subscribed combat handlers
```

For player attacks, `CombatController` currently uses immediate combat dispatch so the hit result is available in the same call path. AI attack batches typically enqueue deferred combat events and flush them at the end of the worker batch.

## Active Damage Event

```cpp
class DamageEvent : public Event {
public:
    void configure(EntityHandle source, EntityHandle target,
                   float damage, const Vector2D& knockback);

    EntityHandle getSource() const;
    EntityHandle getTarget() const;
    float getDamage() const;
    const Vector2D& getKnockback() const;

    float getRemainingHealth() const;
    bool wasLethal() const;
};
```

Important routing detail:

- `DamageEvent::getTypeId()` returns `EventTypeId::Combat`
- combat handlers that mutate or observe damage results should subscribe to `EventTypeId::Combat`
- the payload they usually receive on the hot path is `DamageEvent`

## Dispatch Modes

Combat traffic can use either dispatch mode:

- `Immediate`: apply combat result immediately, then run handlers
- `Deferred`: queue combat work for the main-thread drain pass, then run handlers after commit

Deferred combat events still preserve global enqueue order relative to non-combat deferred events.

## Producing Combat Events

### Player combat

`CombatController` performs hit detection, acquires a pooled `DamageEvent`, configures it, and dispatches it immediately:

```cpp
auto& eventMgr = EventManager::Instance();
auto damageEvent = eventMgr.acquireDamageEvent();
damageEvent->configure(playerHandle, targetHandle, attackDamage, knockback);
eventMgr.dispatchEvent(damageEvent, EventManager::DispatchMode::Immediate);
```

After dispatch returns, callers can read the applied result from EDM or from the event object itself.

### AI combat

AI attack behavior creates deferred `DamageEvent` payloads inside worker batches and later flushes them via `EventManager::enqueueBatch()`. `EventManager` may pre-prepare those combat events in WorkerBudget-guided batches before main-thread commit.

## Subscribing to Combat Events

```cpp
#include "events/EntityEvents.hpp"
#include "managers/EventManager.hpp"

m_combatToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Combat,
    [this](const EventData& data) {
        auto damageEvent = std::static_pointer_cast<DamageEvent>(data.event);
        if (!damageEvent) {
            return;
        }

        onDamageApplied(damageEvent->getTarget(),
                        damageEvent->getRemainingHealth(),
                        damageEvent->wasLethal());
    });

EventManager::Instance().removeHandler(EventTypeId::Combat, m_combatToken);
```

Handlers should treat the payload as an already-processed combat result, not as a request they need to apply themselves.

## Combat Notification Event

`include/events/CombatEvent.hpp` defines a separate `CombatEvent` base and `CombatEventType` enum for notification-style combat signaling. It reports `EventTypeId::CombatNotification`.

Use this event when you want to broadcast combat state changes without entering the damage mutation pipeline. For gameplay damage, prefer `DamageEvent` documentation and the `EventManager` docs.

## Related Documentation

- **CombatController:** `docs/controllers/CombatController.md`
- **EventManager:** `docs/events/EventManager.md`
- **EventManager Advanced:** `docs/events/EventManager_Advanced.md`
