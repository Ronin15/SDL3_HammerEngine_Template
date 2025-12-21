# CombatController Documentation

**Where to find the code:**
- Header: `include/controllers/combat/CombatController.hpp`
- Implementation: `src/controllers/combat/CombatController.cpp`
- Tests: `tests/controllers/CombatControllerTests.cpp` (assuming this will be created)

## Overview

The CombatController is responsible for managing all in-game combat logic and interactions. It handles hit detection, damage calculation, applying status effects, and integrating with entity state machines and the event system to orchestrate dynamic combat scenarios.

## Event Flow

```
InputManager (Player Attack) / AIManager (NPC Attack)
  → Dispatches CombatEvent (AttackInitiated)
    → CombatController processes event, calculates hit/damage
      → Dispatches CombatEvent (DamageDealt, EntityDied, StatusEffectApplied)
        → GamePlayState (or other subscribers) react to combat outcomes
```

## Quick Start

```cpp
#include "controllers/combat/CombatController.hpp"
#include "events/CombatEvent.hpp"

// In GameState::enter()
CombatController::Instance().subscribe();

// Subscribe to combat-related events for UI or game logic updates
m_damageToken = EventManager::Instance().registerHandlerWithToken(
    EventTypeId::Combat,
    [this](const EventData& data) { onCombatEvent(data); }
);

// In GameState::exit()
EventManager::Instance().removeHandler(m_damageToken);
CombatController::Instance().unsubscribe();
```

## Combat Mechanics

### Hit Detection
The CombatController uses collision information from the CollisionManager to determine if an attack hits its target. This can include:
- Bounding box checks
- Pixel-perfect collision (for specific attacks)
- Line-of-sight checks

### Damage Calculation
Damage is calculated based on:
- Attacker's stats (e.g., strength, weapon damage)
- Defender's stats (e.g., armor, defense)
- Critical hit chance
- Random damage variation

### Status Effects
The controller can apply various status effects (e.g., poison, bleed, stun) to entities during combat. These effects are managed through the entity's state machine or a dedicated status effect system.

## API Reference

### subscribe()

```cpp
void subscribe();
```

Subscribes to necessary combat-related events (e.g., `CombatEvent::AttackInitiated`).

### unsubscribe()

```cpp
void unsubscribe();
```

Unsubscribes from all combat-related events.

### initiateAttack(EntityPtr attacker, EntityPtr target, AttackType type)

```cpp
void initiateAttack(EntityPtr attacker, EntityPtr target, AttackType type);
```

Initiates an attack from `attacker` to `target` of a specific `type`. This method triggers the hit detection and damage calculation process.

### applyDamage(EntityPtr target, float damage, DamageType type)

```cpp
void applyDamage(EntityPtr target, float damage, DamageType type);
```

Applies a specified amount of `damage` of a certain `type` to the `target` entity.

### applyStatusEffect(EntityPtr target, StatusEffect effect, float duration)

```cpp
void applyStatusEffect(EntityPtr target, StatusEffect effect, float duration);
```

Applies a `status effect` to the `target` entity for a given `duration`.

### getCombatantState(EntityPtr entity)

```cpp
CombatState getCombatantState(EntityPtr entity) const;
```

Retrieves the current combat state of a specific entity (e.g., attacking, damaged, dodging).

### getCombatLog()

```cpp
const std::vector<CombatLogEntry>& getCombatLog() const;
```

Returns a log of recent combat events for display or analysis.

## Usage Example

```cpp
// GamePlayState.cpp
#include "controllers/combat/CombatController.hpp"
#include "events/CombatEvent.hpp" // Assuming CombatEvent exists

class GamePlayState : public GameState {
private:
    EventManager::HandlerToken m_combatEventToken;

public:
    bool enter() override {
        // Subscribe CombatController
        CombatController::Instance().subscribe();

        // Subscribe to combat events for UI/game logic
        m_combatEventToken = EventManager::Instance().registerHandlerWithToken(
            EventTypeId::Combat, // Assuming a Combat EventType
            [this](const EventData& data) { onCombatEvent(data); }
        );

        return true;
    }

    void exit() override {
        EventManager::Instance().removeHandler(m_combatEventToken);
        CombatController::Instance().unsubscribe();
    }

private:
    void onCombatEvent(const EventData& data) {
        auto combatEvent = std::static_pointer_cast<CombatEvent>(data.event);

        if (combatEvent->getCombatEventType() == CombatEventType::DamageDealt) {
            // Update UI to show damage numbers
            displayDamageNumber(combatEvent->getTargetId(), combatEvent->getDamageAmount());
        } else if (combatEvent->getCombatEventType() == CombatEventType::EntityDied) {
            // Handle entity death (e.g., remove from game, play animation)
            handleEntityDeath(combatEvent->getTargetId());
        }
    }
};

// Example usage in an entity's attack logic
void Player::performAttack(EntityPtr target) {
    // Other attack animations/SFX
    CombatController::Instance().initiateAttack(shared_from_this(), target, AttackType::Melee);
}
```

## Performance Characteristics

- **Per-frame cost:** Minimal (event-driven, processes events only when they occur)
- **Memory:** Moderate (stores combat states, log entries, etc.)
- **Allocations:** Event-driven, minimized per-frame allocations through object pooling or similar patterns for log entries.

## Best Practices

- **Integrate with Entity State Machines**: Use combat events to trigger state transitions (e.g., Idle -> Attacking -> Damaged -> Dying).
- **Clear Event Handling**: Ensure GameStates or other systems subscribe to relevant `CombatEvent` types to react appropriately (e.g., UI for damage, game logic for death).
- **Decouple Visuals**: CombatController should focus on logic; visual feedback (damage numbers, blood splatters) should be handled by subscribers to `CombatEvent`.

## Related Documentation

- **Controller Pattern:** `docs/controllers/README.md`
- **EventManager:** `docs/events/EventManager.md`
- **Entity State Machines:** (Link to relevant entity documentation if available)
- **CollisionManager:** `docs/managers/CollisionManager.md`
- **CombatEvent:** `docs/events/CombatEvent.md` (once created)
