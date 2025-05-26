# AI System Documentation

## Overview

The AI system in the Forge Game Engine provides a flexible, high-performance framework for creating and managing AI behaviors for game entities. The system is designed to be modular, extensible, and efficient, with optimizations for handling large numbers of AI-controlled entities. It integrates with the ThreadSystem component for efficient parallel processing with priority-based task scheduling.

## Key Components

### AIManager

The `AIManager` is the central controller for all AI behaviors in the game. It handles:

- Registration of reusable AI behaviors
- Assignment of behaviors to entities
- Efficient updating of all AI-controlled entities
- Message passing between components and behaviors
- Priority-based multithreaded AI processing

#### Usage Example

```cpp
// Initialize ThreadSystem first
Forge::ThreadSystem::Instance().init();

// Initialize the AI system
AIManager::Instance().init();

// Register a behavior
auto chaseBehavior = std::make_shared<ChaseBehavior>();
AIManager::Instance().registerBehavior("Chase", chaseBehavior);

// Assign behavior to an entity
AIManager::Instance().assignBehaviorToEntity(enemy, "Chase");

// Send a message to the entity's behavior
AIManager::Instance().sendMessageToEntity(enemy, "pause");

// Configure threading with priorities
AIManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);
```

### AIBehavior

`AIBehavior` is the base class for all AI behaviors. It defines the interface that all behaviors must implement:

- `update(Entity*)`: Main method called each frame for behavior execution
- `init(Entity*)`: Called when the behavior is first assigned to an entity
- `clean(Entity*)`: Called when the behavior is removed from an entity
- `getName()`: Returns the behavior's name for identification
- `onMessage(Entity*, const std::string&)`: Handles messages sent to the behavior

#### Creating Custom Behaviors

To create a custom behavior, inherit from `AIBehavior` and implement the required methods:

```cpp
class MyCustomBehavior : public AIBehavior {
public:
    void update(Entity* entity) override {
        // Behavior logic here
    }
    
    void init(Entity* entity) override {
        // Initialization logic
    }
    
    void clean(Entity* entity) override {
        // Cleanup logic
    }
    
    std::string getName() const override {
        return "MyCustom";
    }
};
```

## Performance Optimizations

The AI system includes several performance optimizations to ensure efficient operation with large numbers of entities:

### 1. Distance-Based Update Frequency

Entities further from the player are updated less frequently, saving CPU cycles for more important nearby entities:

- **Close Range**: Updated every frame
- **Medium Range**: Updated every 3 frames
- **Far Range**: Updated every 5 frames
- **Very Far**: Updated every 10 frames

High-priority behaviors (priority > 8) are always updated regardless of distance. For other behaviors, the update frequency is determined by the entity's distance from the player and the behavior's priority level. If the player entity cannot be found (which may happen in certain game states), the system automatically falls back to using distance from the origin.

#### Customizing Update Distances

```cpp
// Set custom update distances for a behavior
myBehavior->setMaxUpdateDistance(800.0f);      // Close range - update every frame
myBehavior->setMediumUpdateDistance(1600.0f);  // Medium range - update every 3 frames
myBehavior->setMinUpdateDistance(2400.0f);     // Far range - update every 5 frames

// Or set all at once
myBehavior->setUpdateDistances(800.0f, 1600.0f, 2400.0f);

// Set priority (0-9, higher = more important)
// Priority > 8 will cause the behavior to update every frame regardless of distance
// Lower priorities will update based on distance from player
myBehavior->setPriority(8);
```

### 2. Memory Preallocation

The AI system preallocates memory for its internal containers to reduce runtime allocations and fragmentation:

- Behavior registry (up to 20 behavior types)
- Entity-behavior assignments (up to 100 entities)
- Message queues (up to 128 messages)

### 3. Batch Processing

Entities with the same behavior type are processed in batches, improving cache locality and enabling parallel processing:

```cpp
// Process multiple entities with the same behavior at once
AIManager::Instance().batchProcessEntities("Patrol", patrollingEntities);
```

### 4. Multi-threading Support with Task Priorities

When multiple CPU cores are available, the AI system can update behaviors in parallel with configurable priorities:

- The system automatically detects available cores
- Tasks are scheduled based on priority levels (Critical, High, Normal, Low, Idle)
- Batch updates are distributed across worker threads with appropriate priorities
- Critical AI behaviors are processed before lower-priority ones
- Threading can be configured or disabled as needed

```cpp
// Configure AI threading with custom settings
AIManager::Instance().configureThreading(true, 4, Forge::TaskPriority::High);
```

## Behavior Messaging System

The AI system includes a messaging system that allows communication with behaviors:

```cpp
// Send message to a specific entity
AIManager::Instance().sendMessageToEntity(entity, "attack", true);  // immediate=true

// Broadcast to all entities
AIManager::Instance().broadcastMessage("player_spotted", false);  // immediate=false
```

Common messages include:
- `"pause"`: Temporarily pause the behavior
- `"resume"`: Resume a paused behavior
- `"target_lost"`: Inform behavior that target is no longer valid

## Pre-built Behaviors

The engine includes several pre-built behaviors:

### ChaseBehavior

Causes an entity to chase a target entity within a specified range.

```cpp
auto chase = std::make_shared<ChaseBehavior>(playerEntity, 5.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("ChasePlayer", chase);
```

### PatrolBehavior

Makes an entity patrol between a series of waypoints.

```cpp
auto patrol = std::make_shared<PatrolBehavior>();
patrol->addWaypoint(Vector2D(100, 100));
patrol->addWaypoint(Vector2D(400, 100));
patrol->addWaypoint(Vector2D(400, 400));
patrol->addWaypoint(Vector2D(100, 400));
AIManager::Instance().registerBehavior("GuardPatrol", patrol);
```

### WanderBehavior

Creates random movement within a specified area.

```cpp
auto wander = std::make_shared<WanderBehavior>(300.0f, 2.0f);  // radius, speed
AIManager::Instance().registerBehavior("RandomWander", wander);
```

## Best Practices

1. **Register behaviors once, reuse many times**:
   Create behavior instances once and register them with unique names, then assign to multiple entities.

2. **Use appropriate update frequencies and priorities**:
   - Set priority > 8 for critical behaviors that must update every frame
   - Use lower priorities for behaviors that can update less frequently based on distance to player
   - Adjust update distances based on your game's scale and expected player movement speed
   - Consider the fallback mechanism (distance from origin) for game states without a player

3. **Leverage batch processing**:
   Group similar entities and process them together for better performance.

4. **Clean up properly**:
   Call `AIManager::Instance().unassignBehaviorFromEntity()` when entities are destroyed.

5. **Use messages for coordination**:
   The messaging system allows behaviors to communicate without tight coupling.

6. **Configure appropriate thread priorities**:
   - Use `Forge::TaskPriority::Critical` for mission-critical AI (boss behaviors, player-interacting NPCs)
   - Use `Forge::TaskPriority::High` for important AI that needs quick responses (combat enemies)
   - Use `Forge::TaskPriority::Normal` for standard NPCs and ambient creatures (default)
   - Use `Forge::TaskPriority::Low` for background or distant NPCs
   - Use `Forge::TaskPriority::Idle` for cosmetic entities and very low-priority behaviors

## Debugging

The AI system includes built-in performance monitoring:

```cpp
// Check how many entities are being managed
size_t entityCount = AIManager::Instance().getManagedEntityCount();

// Check how many behaviors are registered
size_t behaviorCount = AIManager::Instance().getBehaviorCount();

// Configure threading for debugging
AIManager::Instance().configureThreading(false); // Disable threading
AIManager::Instance().configureThreading(true, 1); // Single worker thread
AIManager::Instance().configureThreading(true, 0, Forge::TaskPriority::Normal); // Default threads with normal priority
```

Enable AI debug logging by defining `AI_DEBUG_LOGGING` in your build configuration to see detailed logs of AI system operation. The ThreadSystem component provides additional diagnostics for troubleshooting task scheduling and thread usage patterns.

## ThreadSystem Integration

The AI system has been fully integrated with the updated ThreadSystem, providing several key improvements:

### Priority-Based Task Scheduling

AI tasks are scheduled based on priority levels:

```cpp
// Configure priority for AI tasks
AIManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);

// Set priority for specific behavior
AIManager::Instance().setPriorityForBehavior("Chase", Forge::TaskPriority::Critical);
```

Available priority levels (from highest to lowest):
- `Forge::TaskPriority::Critical` (0): For mission-critical AI (bosses, key NPCs)
- `Forge::TaskPriority::High` (1): For important AI needing quick responses
- `Forge::TaskPriority::Normal` (2): Default for standard AI behaviors
- `Forge::TaskPriority::Low` (3): For background/distant AI entities
- `Forge::TaskPriority::Idle` (4): For cosmetic/non-essential AI

### Improved Thread Management

The updated ThreadSystem provides:

- **Better Thread Shutdown**: Clean shutdown with proper task completion
- **Task Descriptions**: Descriptive task names for easier debugging
- **Performance Monitoring**: Built-in tracking of task execution times
- **Auto-Capacity Management**: Automatic queue capacity adjustment
- **Consistent Timing**: Uses std::chrono for precise timing control

### Implementation Example

```cpp
// Initialize AI with specific threading configuration
bool MyGameState::init() {
    // Initialize ThreadSystem first
    Forge::ThreadSystem::Instance().init();
    
    // Initialize AIManager
    AIManager::Instance().init();
    
    // Configure AIManager with high priority and 4 worker threads
    AIManager::Instance().configureThreading(true, 4, Forge::TaskPriority::High);
    
    // Register behaviors
    auto chaseBehavior = std::make_shared<ChaseBehavior>(player, 3.0f);
    AIManager::Instance().registerBehavior("Chase", chaseBehavior);
    
    // Create NPCs with AI
    for (int i = 0; i < 100; i++) {
        auto npc = createNPC();
        AIManager::Instance().assignBehaviorToEntity(npc, "Chase");
    }
    
    return true;
}
```

For more details on the ThreadSystem, see the [ThreadSystem documentation](../ThreadSystem.md) and [ThreadSystem API Reference](../ThreadSystem_API.md).