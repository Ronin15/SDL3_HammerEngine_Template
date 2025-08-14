# World System Integration Plan

This document outlines the step-by-step integration plan for combining the WorldManager, WorldResourceManager, EventManager, and Camera systems into the HammerEngine. This plan follows industry best practices from engines like Godot, Unreal, and other professional C++ game engines.

## Overview

The goal is to integrate:
- **World management** (WorldManager, World/Tile data)
- **Resource management** (WorldResourceManager)
- **Event-driven systems** (EventManager)
- **Camera utility** (player-following, world navigation)
- **Game state management** (GamePlayState, EventDemoState)

## Industry Best Practices Referenced

- [Game Programming Patterns: Game Loop](https://gameprogrammingpatterns.com/game-loop.html)
- [Gaffer on Games: Fix Your Timestep](https://gafferongames.com/post/fix_your_timestep/)
- Godot Engine subsystem architecture
- Unreal Engine manager patterns
- Professional C++ game engine design patterns

## Step-by-Step Integration Plan

### 1. Subsystem Ownership & Access

- [ ] **1.1. Singleton Pattern for Core Managers**
  - Ensure all core managers (WorldManager, WorldResourceManager, EventManager) use singleton or service locator pattern for global access
  - Avoid static initialization order issues
  - Maintain thread-safe access patterns

- [ ] **1.2. Explicit Initialization Order**
  - Define explicit initialization order for all managers at engine startup
  - Example: ResourceManager before WorldManager (if world generation needs resources)
  - Document initialization dependencies

- [ ] **1.3. Interface Boundaries**
  - Expose only necessary interfaces for each manager to minimize coupling
  - Use abstract interfaces where possible (e.g., `IResourceProvider` for world gen)
  - Implement loose coupling via observer/event patterns

### 2. Event-Driven Architecture

- [ ] **2.1. Centralized Event Bus**
  - Use centralized EventManager/event bus for all cross-system communication
  - Implement fire-and-forget pattern for state changes
  - Use direct API calls for data queries

- [ ] **2.2. Event Type Definitions**
  - Define and document event types for world/resource/camera/gameplay changes
  - Examples: `TileChangedEvent`, `ResourceHarvestedEvent`, `PlayerMovedEvent`
  - Ensure type safety and clear event contracts

- [ ] **2.3. Event Subscription**
  - Ensure all systems fire and subscribe to relevant events
  - Avoid direct calls between systems where events are more appropriate
  - Maintain clear separation of concerns

- [ ] **2.4. Thread Safety**
  - Implement thread-safe event queues if events are fired from multiple threads
  - Use lock-free queues or double-buffered event lists to avoid contention
  - Follow engine's existing threading patterns

### 3. World & Resource System Integration

- [ ] **3.1. Ownership Boundaries**
  - WorldManager owns spatial structure (tiles, entities)
  - WorldResourceManager owns resource data and logic
  - Clear separation of responsibilities

- [ ] **3.2. Communication Patterns**
  - World queries WorldResourceManager for resource state
  - WorldResourceManager updates world via events
  - Maintain unidirectional data flow where possible

- [ ] **3.3. Data-Driven Design**
  - Use data-driven resource definitions (JSON/tables) loaded at startup
  - WorldGenerator uses resource definitions to populate world
  - Maintain configuration flexibility

- [ ] **3.4. Testability**
  - Allow injection of mock resource managers for world generation tests
  - Ensure integration points are easily mockable
  - Maintain unit test coverage

### 4. Camera System

- [ ] **4.1. Camera as Utility Class**
  - Implement Camera as utility class/component, not singleton
  - Avoid tight coupling to rendering or player systems
  - Maintain modular design

- [ ] **4.2. Camera Properties**
  - Camera holds: position, viewport size, target entity (optional), world bounds
  - Implement clean API for camera manipulation
  - Support different camera modes (follow, free, fixed)

- [ ] **4.3. Player-Following Logic**
  - On each update, camera follows the player with smoothing/interpolation
  - Implement lerp/smooth damp for natural movement
  - Allow configuration of follow parameters

- [ ] **4.4. World Bounds Clamping**
  - Clamp camera position to world bounds
  - Prevent showing out-of-bounds areas
  - Handle edge cases gracefully

- [ ] **4.5. Integration with Game State**
  - Game state owns the camera instance
  - Pass camera's view rectangle to renderer and UI systems
  - Maintain clear ownership hierarchy

### 5. Game State & World Integration

- [ ] **5.1. Game State as Orchestrator**
  - GamePlayState and EventDemoState own references to world, camera, and managers
  - Maintain clear orchestration responsibilities
  - Follow existing game state patterns

- [ ] **5.2. Update Loop Integration**
  - On each update: process input → update world (fire events) → update camera → render visible world
  - Follow engine's existing update/render synchronization
  - Maintain consistent frame timing

- [ ] **5.3. Dependency Injection for Testing**
  - Allow game states to accept custom/mocked managers for tests
  - Maintain testability without breaking production code
  - Follow engine's testing patterns

### 6. Documentation & Testing

- [ ] **6.1. Integration Documentation**
  - Document all integration points clearly
  - Update diagrams and API documentation
  - Maintain up-to-date architectural overviews

- [ ] **6.2. Automated Testing**
  - Write/extend automated tests for event propagation
  - Test resource queries and camera logic
  - Ensure integration test coverage

- [ ] **6.3. Test Documentation Updates**
  - Update test documentation and coverage reports
  - Document testing approaches for new integration
  - Maintain testing best practices

### 7. Review & Refactor

- [ ] **7.1. Integration Review**
  - Review integration for modularity, testability, and maintainability
  - Ensure compliance with engine architecture guidelines
  - Validate performance characteristics

- [ ] **7.2. Refactoring**
  - Refactor as needed to reduce coupling and improve clarity
  - Apply SOLID principles and clean code practices
  - Maintain backward compatibility where possible

- [ ] **7.3. Documentation Updates**
  - Update documentation to reflect any changes
  - Ensure architectural decisions are well-documented
  - Maintain consistency with existing documentation

## Expected Outcomes

After completing this integration plan:

1. **Modular Design**: Clear separation of concerns between world, resource, event, and camera systems
2. **Event-Driven Communication**: Loose coupling via centralized event system
3. **Testable Architecture**: Easy to mock and test individual components
4. **Camera System**: Smooth player-following camera with world bounds support
5. **Game State Integration**: Clean orchestration in GamePlayState and EventDemoState
6. **Documentation**: Comprehensive documentation of integration points
7. **Maintainability**: Code that follows engine conventions and is easy to extend

## Implementation Notes

- Follow existing engine code style and conventions
- Maintain thread safety according to engine's threading model
- Use engine's existing logging and error handling patterns
- Ensure compatibility with existing serialization system
- Test thoroughly on all target platforms

## References

- `docs/managers/WorldManager.md`
- `docs/managers/WorldResourceManager.md`
- `docs/managers/EventManager.md`
- `docs/GameEngine.md`
- `docs/GameLoop.md`
- `AGENTS.md` - Build and test commands