# Entity System Documentation

## Overview

The Entity System provides a robust and flexible framework for managing all game entities, including players, NPCs, and dynamic objects. It combines a data-oriented approach for core entity properties with a state machine pattern for managing entity behaviors and actions.

## Table of Contents

- [Core Entity Structure](#core-entity-structure)
- [Entity States](#entity-states)
- [Entity State Machines](#entity-state-machines)
- [Player Entities](#player-entities)
- [NPC Entities](#npc-entities)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Related Documentation](#related-documentation)

## Core Entity Structure

Entities in the Hammer Engine are built upon a base `Entity` class, which provides fundamental properties and functionalities common to all game objects.

**Key features:**
- Position, velocity, acceleration
- Dimensions (width, height)
- Unique ID (`EntityId`)
- Basic rendering capabilities
- State machine integration

**Where to find the code:**
- Header: `include/entities/Entity.hpp`
- Implementation: `src/entities/Entity.cpp`

## Entity States

Entities utilize a state machine pattern to manage their current actions and behaviors. This provides a clear, modular way to define how an entity behaves in different contexts (e.g., idle, walking, attacking, dying).

See also: **[Entity States](EntityStates.md)** for detailed documentation on available NPC and Player states.

## Entity State Machines

Each entity (Player, NPC) maintains its own state machine, which manages transitions between different states. The state machine ensures that entities only move between valid states and handles the entry and exit logic for each state.

**Key features:**
- Current state management
- State transition logic
- Entry and exit actions for states
- Event-driven state changes

**Where to find the code:**
- Base State: `include/entities/EntityState.hpp`
- Base State Machine: `include/entities/EntityStateMachine.hpp`

## Player Entities

The `Player` class extends the base `Entity` to provide player-specific functionalities, including input handling, player-specific states, and interaction with game systems.

**Key features:**
- Input-driven movement and actions
- Player-specific state machine
- Inventory management
- Interaction with UI

**Where to find the code:**
- Header: `include/entities/Player.hpp`
- Implementation: `src/entities/Player.cpp`

## NPC Entities

The `NPC` (Non-Player Character) class extends the base `Entity` to provide functionalities for AI-controlled characters. NPCs integrate with the `AIManager` for behavior management and have their own set of states.

**Key features:**
- AI-driven behaviors (Wander, Patrol, Attack, etc.)
- NPC-specific state machine
- Health and combat attributes
- Interaction with environment

**Where to find the code:**
- Header: `include/entities/NPC.hpp`
- Implementation: `src/entities/NPC.cpp`

## API Reference

### Entity Class

```cpp
class Entity {
public:
    Entity(const std::string& textureId, const Vector2D& position, int width, int height);
    virtual ~Entity() = default;

    virtual void update(float deltaTime);
    virtual void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha);

    EntityId getId() const;
    const Vector2D& getPosition() const;
    void setPosition(const Vector2D& position);
    // ... other common getters/setters
};
```

### Player Class

```cpp
class Player : public Entity {
public:
    Player(const std::string& textureId, const Vector2D& position, int width, int height);

    void handleInput(const SDL_Event& event);
    void update(float deltaTime) override;
    // ... player-specific methods
};
```

### NPC Class

```cpp
class NPC : public Entity {
public:
    NPC(const std::string& textureId, const Vector2D& position, int width, int height);

    void update(float deltaTime) override;
    // ... NPC-specific methods
};
```

## Best Practices

- **State-Driven Logic**: Implement entity-specific logic within their states to maintain modularity and prevent tangled code.
- **Event-Driven Interactions**: Use the `EventManager` for communication between entities and other game systems (e.g., `CombatEvent` for combat).
- **Data-Oriented Design**: Keep core entity data compact and cache-friendly, especially for collections of many entities.

## Related Documentation

- **Entity States:** `EntityStates.md` - Detailed documentation for all entity states.
- **AIManager:** `../ai/AIManager.md` - AI behavior management for NPCs.
- **CombatController:** `../controllers/CombatController.md` - Combat logic and event handling.
- **EventManager:** `../events/EventManager.md` - Global event system.
- **GameStates:** `../gameStates/README.md` - Managing different game modes.
