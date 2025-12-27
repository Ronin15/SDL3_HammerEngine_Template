# Combat Events Documentation

**Where to find the code:**
- Header: `include/events/CombatEvent.hpp`
- Dispatched by: `src/controllers/combat/CombatController.cpp`
- Handled by: GameStates, UI systems, or other game logic components

## Overview

Combat events are dispatched by the `CombatController` to notify subscribers of significant combat-related actions and outcomes. These events are crucial for updating UI, triggering animations, managing entity states, and coordinating other game systems during combat.

All combat events inherit from `CombatEvent` and are dispatched with `Deferred` mode to ensure consistent processing.

## Event Type Hierarchy

```
Event (base)
  └── CombatEvent (base for all combat events)
        ├── AttackInitiatedEvent
        ├── DamageDealtEvent
        ├── EntityDiedEvent
        ├── StatusEffectAppliedEvent
        └── HealEvent
```

## CombatEventType Enum

```cpp
enum class CombatEventType {
    AttackInitiated,       // An attack has begun
    DamageDealt,           // Damage has been applied to an entity
    EntityDied,            // An entity's health reached zero
    StatusEffectApplied,   // A status effect (e.g., poison, stun) was applied
    Heal                   // An entity has regained health
};
```

## Event Details

### AttackInitiatedEvent

Fired when an attack is initiated by an entity.

```cpp
class AttackInitiatedEvent : public CombatEvent {
    EntityId getAttackerId() const;
    EntityId getTargetId() const;
    AttackType getAttackType() const; // e.g., Melee, Ranged, Spell
};
```

**Use Cases:**
- Trigger attack animations for the attacker
- Display pre-attack visual effects
- Play attack sound effects

### DamageDealtEvent

Fired when damage has been successfully dealt to a target entity.

```cpp
class DamageDealtEvent : public CombatEvent {
    EntityId getAttackerId() const;
    EntityId getTargetId() const;
    float getDamageAmount() const;
    DamageType getDamageType() const; // e.g., Physical, Fire, Magic
    bool isCriticalHit() const;
    float getNewHealth() const;
};
```

**Use Cases:**
- Display damage numbers in UI
- Update health bars
- Play hit sound effects
- Trigger damaged animations for the target

### EntityDiedEvent

Fired when an entity's health drops to zero or below.

```cpp
class EntityDiedEvent : public CombatEvent {
    EntityId getVictimId() const;
    EntityId getKillerId() const; // May be invalid if no specific killer
};
```

**Use Cases:**
- Trigger death animations
- Remove entity from active game world
- Update score/experience for the killer
- Spawn loot

### StatusEffectAppliedEvent

Fired when a status effect is applied to an entity.

```cpp
class StatusEffectAppliedEvent : public CombatEvent {
    EntityId getTargetId() const;
    StatusEffectType getEffectType() const; // e.g., Poison, Stun, Bleed
    float getDuration() const;
    float getPotency() const;
};
```

**Use Cases:**
- Display status effect icons on UI
- Apply visual effects to the affected entity
- Modify entity's stats or behavior

### HealEvent

Fired when an entity regains health.

```cpp
class HealEvent : public CombatEvent {
    EntityId getTargetId() const;
    float getHealAmount() const;
    float getNewHealth() const;
};
```

**Use Cases:**
- Display healing numbers in UI
- Update health bars
- Play healing sound effects

## Subscribing to Combat Events

### Using EventManager Directly

```cpp
#include "events/CombatEvent.hpp"
#include "managers/EventManager.hpp"

// Subscribe
m_combatToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Combat,
    [this](const EventData& data) {
        auto combatEvent = std::static_pointer_cast<CombatEvent>(data.event);

        switch (combatEvent->getCombatEventType()) {
            case CombatEventType::DamageDealt:
                handleDamageDealt(std::static_pointer_cast<DamageDealtEvent>(data.event));
                break;
            case CombatEventType::EntityDied:
                handleEntityDied(std::static_pointer_cast<EntityDiedEvent>(data.event));
                break;
            // ... other cases
        }
    }
);

// Unsubscribe
EventManager::Instance().removeHandler(m_combatToken);
```

### Using CombatController (Recommended for managing combat logic)

The CombatController itself subscribes to initial attack events and dispatches further events based on combat outcomes. GameStates or other systems would typically subscribe to the events *dispatched by* the `CombatController` to react to combat results.

```cpp
// In GameState::enter()
CombatController::Instance().subscribe(); // CombatController registers its own handlers

// GameState subscribes to outcomes for UI/logic
m_damageToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Combat,
    [this](const EventData& data) { onCombatOutcome(data); }
);
```

## Dispatch Mode

All combat events are dispatched with `EventManager::DispatchMode::Deferred`:

- Events are queued by `CombatController` (or other sources)
- Processed after game logic update completes
- Ensures consistent game state during handling
- Prevents immediate side effects that could disrupt current frame's logic

## Related Documentation

- **CombatController:** `docs/controllers/CombatController.md` - Controller that dispatches these events
- **EventManager:** `docs/events/EventManager.md` - Event subscription system
- **Entity State Machines:** (Link to relevant entity documentation if available)
- **GameStates:** (Link to relevant GameState documentation if available)
