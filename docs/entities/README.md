# Entity System Documentation

## Overview

The Entity System provides a robust and flexible framework for managing all game entities, including players, NPCs, and dynamic objects. It uses a **Data-Oriented Design (DoD)** approach with the `EntityDataManager` as the single source of truth for all entity data, combined with lightweight `EntityHandle` references and a state machine pattern for managing behaviors.

## Table of Contents

- [EntityDataManager Integration](#entitydatamanager-integration)
- [EntityHandle System](#entityhandle-system)
- [Simulation Tiers](#simulation-tiers)
- [Core Entity Structure](#core-entity-structure)
- [Entity States](#entity-states)
- [Entity State Machines](#entity-state-machines)
- [Player Entities](#player-entities)
- [NPC Entities](#npc-entities)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Related Documentation](#related-documentation)

## EntityDataManager Integration

All entity data is stored centrally in `EntityDataManager` using Structure-of-Arrays (SoA) layout for cache-optimal access:

**Key features:**
- Single source of truth (eliminates 4x position duplication)
- Cache-optimal 64-byte `EntityHotData` structs (one cache line)
- Thread-safe index-based accessors for parallel batch processing
- Type-specific data blocks (CharacterData, ItemData, ProjectileData)
- Supports 100K+ entities with tiered simulation

**Where to find the code:**
- Header: `include/managers/EntityDataManager.hpp`
- Implementation: `src/managers/EntityDataManager.cpp`

```cpp
// Entity creation via EDM
auto& edm = EntityDataManager::Instance();
EntityHandle npc = edm.createNPC(position, EntityKind::NPC);

// Data access via handle
auto& hotData = edm.getHotData(npc);
auto& transform = edm.getTransform(npc);
```

## EntityHandle System

`EntityHandle` provides lightweight, type-safe entity references without RTTI.

**See: [EntityHandle Documentation](EntityHandle.md)** for complete API reference.

**Key features:**
- 16-byte struct suitable for passing by value
- `EntityKind` enum for fast type checking
- Generation counter for stale reference detection
- Replaces raw pointers and EntityID lookups

**Where to find the code:**
- Header: `include/entities/EntityHandle.hpp`

```cpp
struct EntityHandle {
    EntityID id;           // Unique identifier
    EntityKind kind;       // Type without RTTI
    uint8_t generation;    // Stale reference detection

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool isNPC() const noexcept;
    [[nodiscard]] bool isPlayer() const noexcept;
};
```

### EntityKind Enumeration

```cpp
enum class EntityKind : uint8_t {
    Player, NPC,           // Characters (have health, AI)
    DroppedItem, Container, Harvestable,  // Interactables
    Projectile, AreaEffect,               // Combat (short-lived)
    Prop, Trigger,                        // Environment
    StaticObstacle                        // World geometry
};
```

## Simulation Tiers

Entities are assigned to simulation tiers based on distance from camera/player:

| Tier | Processing | Use Case |
|------|------------|----------|
| **Active** | Full AI, collision, render | Near camera (visible) |
| **Background** | Position-only @ 10Hz | Off-screen but nearby |
| **Hibernated** | No updates, data stored | Far away |

```cpp
enum class SimulationTier : uint8_t {
    Active = 0,      // Full update every frame
    Background = 1,  // Simplified updates at 10Hz
    Hibernated = 2   // No updates, data only
};
```

**Tier management:**
- `BackgroundSimulationManager` handles tier reassignment every 60 frames
- Active tier entities processed by AIManager, CollisionManager
- Background tier entities processed by BackgroundSimulationManager

## Core Entity Structure

Entities in the Hammer Engine are built upon a base `Entity` class, which provides fundamental properties and functionalities common to all game objects.

**Key features:**
- EntityHandle for EDM integration
- Position, velocity, acceleration (stored in EDM)
- Dimensions (width, height)
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
- State Manager: `include/managers/EntityStateManager.hpp`

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

### EntityHandle

```cpp
struct EntityHandle {
    EntityID id;
    EntityKind kind;
    uint8_t generation;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool isNPC() const noexcept;
    [[nodiscard]] bool isPlayer() const noexcept;
    [[nodiscard]] EntityKind getKind() const noexcept;
};
```

### EntityDataManager

```cpp
class EntityDataManager {
public:
    static EntityDataManager& Instance();

    // Entity creation
    EntityHandle createNPC(const Vector2D& position, EntityKind kind);
    EntityHandle createPlayer(const Vector2D& position);

    // Data access (by handle)
    EntityHotData& getHotData(EntityHandle handle);
    TransformData& getTransform(EntityHandle handle);

    // Index-based access (for batch processing)
    EntityHotData& getHotDataByIndex(uint32_t edmIndex);
    TransformData& getTransformByIndex(uint32_t edmIndex);

    // Handle validation
    [[nodiscard]] bool isValidHandle(EntityHandle handle) const;
    [[nodiscard]] uint32_t getEdmIndex(EntityHandle handle) const;
};
```

### Entity Class

```cpp
class Entity {
public:
    Entity(const std::string& textureId, const Vector2D& position, int width, int height);
    virtual ~Entity() = default;

    virtual void update(float deltaTime);
    virtual void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha);

    [[nodiscard]] EntityHandle getHandle() const;
    [[nodiscard]] const Vector2D& getPosition() const;
    void setPosition(const Vector2D& position);
};
```

### Player Class

```cpp
class Player : public Entity {
public:
    Player(const std::string& textureId, const Vector2D& position, int width, int height);

    void handleInput(const SDL_Event& event);
    void update(float deltaTime) override;
};
```

### NPC Class

```cpp
class NPC : public Entity {
public:
    NPC(const std::string& textureId, const Vector2D& position, int width, int height);

    void update(float deltaTime) override;
    [[nodiscard]] EntityHandle getHandle() const;
};
```

## Best Practices

- **Use EntityDataManager**: All entity data should flow through EDM. Never cache position data locally when EDM is the source of truth.
- **Use EntityHandle**: Pass handles by value instead of raw pointers. Validate with `isValid()` before accessing data.
- **Tier-Aware Processing**: Check entity tier before expensive operations. Background/Hibernated entities should skip full AI/collision.
- **Index-Based Batch Processing**: For parallel processing, use `getHotDataByIndex()` with pre-cached indices to avoid map lookups.
- **State-Driven Logic**: Implement entity-specific logic within their states to maintain modularity and prevent tangled code.
- **Event-Driven Interactions**: Use the `EventManager` for communication between entities and other game systems (e.g., `CombatEvent` for combat).

```cpp
// GOOD - Use EDM for position data
auto& transform = EntityDataManager::Instance().getTransform(handle);
transform.position = newPosition;

// BAD - Local position copy becomes stale
Vector2D localPos = entity->getPosition();  // May diverge from EDM
```

## Related Documentation

- **Entity States:** `EntityStates.md` - Detailed documentation for all entity states.
- **EntityDataManager:** `../managers/EntityDataManager.md` - Central data authority for all entities.
- **BackgroundSimulationManager:** `../managers/BackgroundSimulationManager.md` - Off-screen entity simulation.
- **AIManager:** `../ai/AIManager.md` - AI behavior management for NPCs.
- **CombatController:** `../controllers/CombatController.md` - Combat logic and event handling.
- **EventManager:** `../events/EventManager.md` - Global event system.
- **GameStates:** `../gameStates/README.md` - Managing different game modes.
