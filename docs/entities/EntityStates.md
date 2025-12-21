# Entity States Documentation

## Overview

This document details the various states that Player and NPC entities can transition through, managed by their respective state machines. The state machine pattern centralizes behavior logic, ensuring modularity, clear transitions, and robust handling of entity actions.

Each state defines `enter()`, `execute()`, and `exit()` methods, which are called when the state is entered, updated each frame, and exited, respectively.

## Table of Contents

- [Core State Machine Concepts](#core-state-machine-concepts)
- [NPC States](#npc-states)
  - [NPCIdleState](#npcidlestate)
  - [NPCWalkingState](#npcwalkingstate)
  - [NPCAttackingState](#npcattackingstate)
  - [NPCHurtState](#npchurtstate)
  - [NPCDyingState](#npcdyingstate)
  - [NPCRecoveringState](#npcrecoveringstate)
- [Player States](#player-states)
  - [PlayerIdleState](#playeridlestate)
  - [PlayerAttackingState](#playerattackingstate)
  - [PlayerRunningState](#playerrunningstate)
- [Creating Custom States](#creating-custom-states)
- [Best Practices](#best-practices)
- [Related Documentation](#related-documentation)

## Core State Machine Concepts

Entities (Player and NPC) utilize an `EntityStateMachine` to manage their current behavior.

**Key Components:**
- **`EntityState`**: Base abstract class for all states, defining `enter()`, `execute()`, `exit()`, and `onMessage()` interfaces.
- **`EntityStateMachine`**: Manages the current state, handles state transitions, and delegates `update()` calls to the active state.

**State Transition Example:**
```cpp
// In a state's execute() method or a controller
if (entity->getHealth() <= 0) {
    entity->getStateMachine().changeState(std::make_unique<NPCDyingState>());
} else if (targetInRange) {
    entity->getStateMachine().changeState(std::make_unique<NPCAttackingState>(target));
}
```

## NPC States

NPCs can transition through a variety of states that define their behavior in the game world. These states often integrate with the `AIManager` for movement and action logic.

### NPCIdleState

**Purpose**: Represents an NPC that is not actively performing any major action. It might be standing still, performing minor fidget animations, or simply waiting for a trigger.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCIdleState.hpp`
- Implementation: `src/entities/npcStates/NPCIdleState.cpp`

**Transitions from:** Any state where the NPC's current task is complete or interrupted.
**Transitions to:** `NPCWalkingState` (if a new path is set), `NPCAttackingState` (if a target is acquired), `NPCHurtState` (if damaged).

**Key Features**:
- Minimal CPU usage.
- Can be configured to trigger minor ambient animations.

### NPCWalkingState

**Purpose**: Represents an NPC that is actively moving towards a destination, often following a path provided by the `PathfinderManager` or a simple direction.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCWalkingState.hpp`
- Implementation: `src/entities/npcStates/NPCWalkingState.cpp`

**Transitions from:** `NPCIdleState`, `NPCRecoveringState`.
**Transitions to:** `NPCIdleState` (if destination reached), `NPCAttackingState` (if a target is acquired and in range), `NPCHurtState` (if damaged).

**Key Features**:
- Integrates with `PathfinderManager` for navigation.
- Updates entity's position and animation based on movement.

### NPCAttackingState

**Purpose**: Represents an NPC that is actively engaging in combat with a target. This state handles attack animations, damage dealing, and cooldowns.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCAttackingState.hpp`
- Implementation: `src/entities/npcStates/NPCAttackingState.cpp`

**Transitions from:** `NPCIdleState`, `NPCWalkingState`.
**Transitions to:** `NPCIdleState` (if target defeated or lost), `NPCHurtState` (if damaged), `NPCDyingState` (if health drops to zero).

**Key Features**:
- Coordinated with `CombatController` for damage application.
- Manages attack animations and cooldown timers.

### NPCHurtState

**Purpose**: Represents an NPC that has just taken damage and is reacting to it. This state typically involves a brief stun, recoil animation, or invulnerability period.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCHurtState.hpp`
- Implementation: `src/entities/npcStates/NPCHurtState.cpp`

**Transitions from:** `NPCIdleState`, `NPCWalkingState`, `NPCAttackingState`.
**Transitions to:** `NPCIdleState` (after recovery), `NPCDyingState` (if health drops to zero).

**Key Features**:
- Short duration.
- Triggers damaged visual effects or sound cues.

### NPCDyingState

**Purpose**: Represents an NPC whose health has dropped to zero. This state handles death animations, sound effects, and eventual removal from the game world.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCDyingState.hpp`
- Implementation: `src/entities/npcStates/NPCDyingState.cpp`

**Transitions from:** `NPCIdleState`, `NPCWalkingState`, `NPCAttackingState`, `NPCHurtState`, `NPCRecoveringState`.
**Transitions to:** None (final state before removal).

**Key Features**:
- Triggers death animation and effects.
- Can trigger loot drops or quest updates via `EventManager`.

### NPCRecoveringState

**Purpose**: Represents an NPC that is in a recovery period, perhaps after a major attack, a stun, or a specific interaction. It's a temporary state to prevent immediate action.

**Where to find the code:**
- Header: `include/entities/npcStates/NPCRecoveringState.hpp`
- Implementation: `src/entities/npcStates/NPCRecoveringState.cpp`

**Transitions from:** `NPCHurtState`, `NPCAttackingState` (after a powerful attack).
**Transitions to:** `NPCIdleState`, `NPCWalkingState`, `NPCAttackingState`.

**Key Features**:
- Implements a timer for the recovery duration.
- Prevents the NPC from acting during recovery.

## Player States

Player entities have their own set of states, often driven by player input and interactions.

### PlayerIdleState

**Purpose**: The default state when the player is not moving or performing any active actions.

**Where to find the code:**
- Header: `include/entities/playerStates/PlayerIdleState.hpp`
- Implementation: `src/entities/playerStates/PlayerIdleState.cpp`

**Transitions from:** `PlayerRunningState`, `PlayerAttackingState`.
**Transitions to:** `PlayerRunningState` (on movement input), `PlayerAttackingState` (on attack input).

**Key Features**:
- Reacts to input to transition to other states.
- Minimal animation (e.g., breathing, weapon at rest).

### PlayerAttackingState

**Purpose**: Represents the player performing an attack. This state manages attack animations, hit detection, and cooldowns.

**Where to find the code:**
- Header: `include/entities/playerStates/PlayerAttackingState.hpp`
- Implementation: `src/entities/playerStates/PlayerAttackingState.cpp`

**Transitions from:** `PlayerIdleState`, `PlayerRunningState`.
**Transitions to:** `PlayerIdleState` (after attack animation and cooldown), `PlayerRunningState` (if movement input resumes).

**Key Features**:
- Coordinated with `CombatController` for damage application.
- Manages attack animations and cooldowns.
- Typically locks player movement during the attack animation.

### PlayerRunningState

**Purpose**: Represents the player moving based on input.

**Where to find the code:**
- Header: `include/entities/playerStates/PlayerRunningState.hpp`
- Implementation: `src/entities/playerStates/PlayerRunningState.cpp`

**Transitions from:** `PlayerIdleState`, `PlayerAttackingState`.
**Transitions to:** `PlayerIdleState` (when movement input ceases), `PlayerAttackingState` (on attack input).

**Key Features**:
- Updates player position based on input.
- Controls running animations.

## Creating Custom States

To create a new custom state for an `Entity`, follow these steps:

1.  **Inherit from `EntityState`**: Create a new class that publicly inherits from `EntityState`.
2.  **Implement `enter()`, `execute()`, `exit()`**: Define the behavior for when the state is entered, active, and exited.
3.  **Implement `onMessage()` (Optional)**: Handle any specific messages relevant to this state.
4.  **Implement `clone()` (if creating instances for multiple entities)**: If the state needs unique data per entity, provide a `clone()` method.
5.  **Integrate with `EntityStateMachine`**: Update the `EntityStateMachine` logic to allow transitions to and from your new state.

**Example Structure for a New State:**
```cpp
// include/entities/playerStates/PlayerDodgingState.hpp
class PlayerDodgingState : public EntityState {
public:
    PlayerDodgingState(float dodgeDuration);
    void enter(Entity& entity) override;
    void execute(Entity& entity, float deltaTime) override;
    void exit(Entity& entity) override;
    void onMessage(Entity& entity, const std::string& message) override;

private:
    float m_dodgeTimer;
    float m_dodgeDuration;
    Vector2D m_dodgeDirection;
};
```

## Best Practices

-   **Single Responsibility**: Each state should have a clear, single purpose.
-   **Minimize Coupling**: States should ideally only interact with the `Entity` they belong to and dispatch events for broader system interactions.
-   **Clear Transitions**: Define explicit conditions for state changes to avoid ambiguity.
-   **Use `EventManager`**: For communication outside the entity's direct hierarchy (e.g., `CombatEvent` when attacking).

## Related Documentation

-   **Entity System Overview:** `README.md` - High-level overview of the entity system.
-   **AIManager:** `../../ai/AIManager.md` - How AI behaviors are managed for NPCs.
-   **CombatController:** `../../controllers/CombatController.md` - Handles combat logic that may trigger state changes.
-   **EventManager:** `../../events/EventManager.md` - Event-driven communication between systems.
