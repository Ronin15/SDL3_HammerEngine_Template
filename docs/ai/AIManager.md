# AI Manager System Documentation

## ⚠️ IMPORTANT: Architecture Updates (v4.0+)

**Major Changes - Unified System Architecture:**
- **UNIFIED**: Single high-performance spatial system using `AIEntityData` with all optimizations
- **ENHANCED**: Distance-based optimization integrated into main `update()` method
- **SIMPLIFIED**: One registration flow: `registerEntityForUpdates()` + `assignBehaviorToEntity()`
- **PERFORMANCE**: Batch processing with threading, cache-friendly data structures, type-indexed behaviors
- **MAINTAINED**: All existing features: global pause, messaging, player reference, priority support
- **REMOVED**: Dual entity management system (managed entities vs main entities)
- **CHANGED**: All processing now through single `AIManager::update()` called from game states

## Overview

The AI Manager is a high-performance, unified system for managing autonomous behaviors for game entities. It provides a single, optimized framework for implementing and controlling various AI behaviors with advanced performance features:

1. **Unified Spatial System** - Single `AIEntityData` structure with cache-friendly batch processing
2. **Distance-based optimization** - Frame skipping for distant entities based on player distance
3. **Priority-based management** - Higher priority entities get larger distance thresholds
4. **Individual behavior instances** - Each entity gets its own behavior state via clone()
5. **Threading & Batching** - Automatic batch processing with ThreadSystem integration
6. **Type-indexed behaviors** - Fast behavior dispatch using enumerated types
7. **Message queue system** - Asynchronous communication with behaviors
8. **Global AI pause/resume** - Complete halt of all AI processing with thread-safe controls
9. **Performance monitoring** - Built-in statistics and performance tracking

## ⚠️ CRITICAL: Individual Behavior Instances Architecture

### Major Architecture Change (v2.1+)

**Previous Architecture (DEPRECATED - DO NOT USE)**: 
- All NPCs shared single behavior instances per type
- Caused race conditions, state interference, and system crashes

**Current Architecture (REQUIRED)**: 
- Each NPC receives its own cloned behavior instance
- Complete state isolation and thread safety
- Stable performance up to 10,000+ NPCs on screen at once.

### Implementation

All behaviors now implement the `clone()` method:

```cpp
class PatrolBehavior : public AIBehavior {
public:
    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed, m_includeOffscreenPoints);
        cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
        cloned->setActive(m_active);
        cloned->setPriority(m_priority);
        cloned->setUpdateFrequency(m_updateFrequency);
        cloned->setUpdateDistances(m_maxUpdateDistance, m_mediumUpdateDistance, m_minUpdateDistance);
        return cloned;
    }
};
```

### Benefits of Individual Instances

- ✅ **No State Interference**: Each NPC has independent waypoints, targets, timers
- ✅ **Thread Safety**: No race conditions between NPCs
- ✅ **Performance**: Linear scaling instead of exponential degradation
- ✅ **Stability**: Eliminates cache invalidation thrashing
- ✅ **Memory Cost**: ~5.5MB for 10,000 NPCs (negligible vs. system crashes)

### Memory Impact Analysis

| NPCs | Shared Model | Individual Model | Increase |
|------|-------------|------------------|----------|
| 100  | 1.5KB       | 150KB           | 0.15MB   |
| 1000 | 1.5KB       | 1.5MB           | 1.5MB    |
| 5000 | 1.5KB       | 2.5MB           | 2.5MB    |

**Trade-off**: 2.5MB memory cost eliminates system crashes and provides stable performance.
## Core Components

### AIManager

The central management class that handles:
- Registration of behaviors
- Assignment of behaviors to entities
- Updating all behaviors during the game loop using ThreadSystem
- Communication with behaviors via messages
- Priority-based scheduling of AI tasks

**Performance Optimizations:**
- Entity-behavior caching for faster lookups
- Batch processing of entities with the same behavior
- Early exit conditions to avoid unnecessary updates
- Message queue system for deferred communication
- Priority-based task scheduling for optimal CPU utilization

### AIBehavior Base Class

The abstract interface that all behaviors must implement:
- `update(Entity*)`: Called each frame to update entity movement/actions
- `init(Entity*)`: Called when a behavior is first assigned to an entity
- `clean(Entity*)`: Called when a behavior is removed from an entity
- `onMessage(Entity*, const std::string&)`: Handles messages sent to the behavior

**Optimization Methods:**
- `shouldUpdate(Entity*)`: Early exit condition checking if entity should be updated
- `isEntityInRange(Entity*)`: Early exit condition checking if entity is in range
- `isWithinUpdateFrequency()`: Early exit condition for update frequency control
- `setUpdateFrequency(int)`: Sets how often the behavior should update (1=every frame)

## Available Behaviors

### WanderBehavior

Entities move randomly within a defined area, changing direction periodically.

**Configuration options:**
- Movement speed
- Direction change interval
- Wander area radius

### PatrolBehavior

Entities follow a predefined path of waypoints, moving from point to point in sequence.

**Configuration options:**
- Array of waypoint positions
- Movement speed
- Waypoint radius (how close to get before moving to next point)

### ChaseBehavior

Entities pursue a target (typically the player) when within detection range.

**Configuration options:**
- Target entity reference
- Chase speed
- Maximum detection/pursuit range
- Minimum distance to maintain from target

## Example Usage

### Basic Setup

```cpp
// AI Manager is initialization happens at the beginning of the starup sequence.
AIManager::Instance().init();

// Create and register behaviors
auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 3000.0f, 200.0f);
AIManager::Instance().registerBehavior("Wander", wanderBehavior);

// Create patrol points
std::vector<Vector2D> patrolPoints;
patrolPoints.push_back(Vector2D(100, 100));
patrolPoints.push_back(Vector2D(500, 100));
patrolPoints.push_back(Vector2D(500, 400));
patrolPoints.push_back(Vector2D(100, 400));
auto patrolBehavior = std::make_shared<PatrolBehavior>(patrolPoints, 1.5f);
AIManager::Instance().registerBehavior("Patrol", patrolBehavior);

// Create chase behavior (targeting the player)
auto chaseBehavior = std::make_shared<ChaseBehavior>(player, 2.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("Chase", chaseBehavior);
```

### Assigning Behaviors to Entities

```cpp
// Create an NPC (using shared_ptr for EntityPtr)
auto npc = std::make_shared<NPC>("npc_sprite", Vector2D(250, 250), 64, 64);

// Register entity with priority and behavior in one call (preferred)
AIManager::Instance().registerEntityForUpdates(npc, 5, "Wander");

// Alternative: Register with priority only
AIManager::Instance().registerEntityForUpdates(npc, 5);

// Process queued assignments (typically called once per frame)
AIManager::Instance().processPendingBehaviorAssignments();

// Change behavior during gameplay
if (isPlayerDetected) {
    AIManager::Instance().queueBehaviorAssignment(npc, "Chase");
}
```

### Controlling Behaviors with Messages

```cpp
// Pause a specific entity's behavior (immediate delivery)
AIManager::Instance().sendMessageToEntity(npc, "pause", true);

// Resume a specific entity's behavior (queued for next update)
AIManager::Instance().sendMessageToEntity(npc, "resume");

// Pause all AI entities (immediate delivery)
AIManager::Instance().broadcastMessage("pause", true);

// Reverse the patrol route for a specific entity (queued for next update)
AIManager::Instance().sendMessageToEntity(npc, "reverse");

// Manually process queued messages (normally happens automatically during update())
AIManager::Instance().processMessageQueue();
```

**Note**: For game-wide pause functionality (like pause menus or debugging), use the Global AI Pause System instead of broadcasting pause messages. Message-based pausing is better for gameplay mechanics affecting specific entities or groups.

### Global AI Pause/Resume System

The AIManager provides a global pause system that completely halts all AI processing, including entity updates and behavior execution.

#### Usage

```cpp
// Pause all AI processing globally
AIManager::Instance().setGlobalPause(true);

// Resume all AI processing
AIManager::Instance().setGlobalPause(false);

// Check current pause state
bool isPaused = AIManager::Instance().isGloballyPaused();
```

#### Example Implementation in Game State

```cpp
void AIDemoState::update() {
    // Handle spacebar for AI pause/resume
    static bool wasSpacePressed = false;
    bool isSpacePressed = InputManager::Instance().isKeyDown(SDL_SCANCODE_SPACE);
    
    if (isSpacePressed && !wasSpacePressed) {
        // Toggle global pause state
        bool currentlyPaused = AIManager::Instance().isGloballyPaused();
        AIManager::Instance().setGlobalPause(!currentlyPaused);
        
        // Optional: Also send messages for behaviors that need them
        std::string message = !currentlyPaused ? "pause" : "resume";
        AIManager::Instance().broadcastMessage(message, true);
        
        std::cout << "AI " << (!currentlyPaused ? "PAUSED" : "RESUMED") << std::endl;
    }
    wasSpacePressed = isSpacePressed;
    
    // Your other update logic...
}
```

#### Benefits

- **Complete AI Halt**: When paused, no entity updates or AI behavior processing occurs
- **Thread-Safe**: Uses atomic operations for safe concurrent access
- **Performance**: Minimal overhead when checking pause state
- **Visual Consistency**: NPCs immediately stop moving and animating when paused
- **Debugging**: Perfect for debugging AI systems or examining specific game states

#### When to Use Global Pause vs Message-Based Pause

**Use Global Pause (`setGlobalPause()`) for:**
- Game pause menus
- Debug/development modes
- Cutscenes or dialogue sequences
- Performance profiling
- Any scenario where ALL AI should stop completely

**Use Message-Based Pause (`broadcastMessage("pause")`) for:**
- Specific gameplay mechanics (e.g., time-stop spells)
- Individual entity control
- Behavior-specific pause logic
- When you need entities to continue physics updates but stop AI decisions

#### Technical Details

- Uses `std::atomic<bool>` for thread-safe access across multiple threads
- Early return in `update()` prevents all AI processing when paused
- Combines with message system for behaviors that need specific pause/resume handling
- Memory order semantics ensure proper synchronization across CPU cores

### Integration with Game Loop

The AIManager uses a unified spatial system. Entities are registered once and automatically updated by GameEngine:

```cpp
void GamePlayState::enter() {
    // Register entities with priority and behavior in one call (recommended)
    AIManager::Instance().registerEntityForUpdates(npc, 7, "Chase");
    AIManager::Instance().setPlayerForDistanceOptimization(player);
}

void GamePlayState::update() {
    // Update player first
    player->update();
    
    // Update AI Manager - processes queued assignments and updates all entities
    AIManager::Instance().update();
    
    // Your other game-specific update code...
    checkCollisions();
    updateUI();
}
```

### Cleanup

When switching game states or shutting down, clean up entities and AI system:

```cpp
void GamePlayState::exit() {
    // Unregister entities from AI updates (prevents accessing destroyed entities)
    for (auto& npc : m_npcs) {
        AIManager::Instance().unregisterEntityFromUpdates(npc);
        AIManager::Instance().unassignBehaviorFromEntity(npc);
    }
    
    // Clean up AI Manager (only if shutting down completely)
    AIManager::Instance().clean();

    // Your other cleanup code...
}
```

## Creating Custom Behaviors

To create a custom behavior, inherit from the AIBehavior base class and implement the required methods:

```cpp
class FlankingBehavior : public AIBehavior {
public:
    FlankingBehavior(EntityPtr target, float speed = 2.0f, float flankDistance = 100.0f);

    void init(EntityPtr entity) override {
        // Initialize flanking behavior
    }

    void executeLogic(EntityPtr entity) override {
        // Implement flanking logic
    }

    void clean(EntityPtr entity) override {
        // Clean up flanking behavior
    }

    std::string getName() const override {
        return "Flanking";
    }

    // REQUIRED: Clone method for individual instances
    std::shared_ptr<AIBehavior> clone() const override {
        return std::make_shared<FlankingBehavior>(m_target, m_speed, m_flankDistance);
    }

private:
    EntityPtr m_target;
    float m_speed;
    float m_flankDistance;
};
```

## Threading Considerations

The AIManager optionally utilizes the ThreadSystem to distribute AI updates across multiple CPU cores. This is enabled by default but can be controlled through the AIManager's initialization:

```cpp
// First ensure ThreadSystem is initialized
Forge::ThreadSystem::Instance().init();

// Initialize AIManager
AIManager::Instance().init();

// Disable threading or customize for AI updates
AIManager::Instance().configureThreading(false); // Disable threading
AIManager::Instance().configureThreading(true, 4); // Enable with 4 threads
AIManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High); // Enable with default threads and high priority
```

When threading is enabled, be careful about accessing shared resources from behavior update methods. Consider using locks or designing behaviors to be thread-safe. The ThreadSystem supports task priorities, allowing you to control which AI tasks get processed first:

- `Forge::TaskPriority::Critical` (0) - For mission-critical AI (e.g., boss behaviors, player-interacting NPCs)
- `Forge::TaskPriority::High` (1) - For important AI that needs quick responses (e.g., combat enemies)
- `Forge::TaskPriority::Normal` (2) - Default for most AI behaviors
- `Forge::TaskPriority::Low` (3) - For background AI that isn't time-sensitive
- `Forge::TaskPriority::Idle` (4) - For very low-priority AI tasks

The ThreadSystem automatically manages task capacity and scheduling based on priorities, ensuring critical AI behaviors receive CPU time before less important ones.

## Entity Registration System

### Usage Pattern

```cpp
// RECOMMENDED: Register with priority and behavior in one call
for (int i = 0; i < numNPCs; ++i) {
    auto npc = createNPC();
    
    // Single call handles registration + behavior assignment
    AIManager::Instance().registerEntityForUpdates(npc, 5, "Wander");
}

// Alternative: Traditional separate calls (still supported)
AIManager::Instance().registerEntityForUpdates(npc, 5);
AIManager::Instance().queueBehaviorAssignment(npc, "Wander");
```

### Implementation Details

The consolidated system:

1. **Single API call**: `registerEntityForUpdates(entity, priority, behaviorName)`
2. **Internal delegation**: Calls existing registration and queuing methods
3. **Automatic processing**: Behavior assignments processed during AIManager::update()
4. **Performance optimized**: Reduced overhead for mass entity creation

### Performance Benefits

- **Reduced function call overhead**: 50% fewer external API calls
- **Better cache locality**: Related operations grouped together
- **Lower singleton access cost**: Single `AIManager::Instance()` call per entity
- **Improved batch creation**: Especially beneficial for large entity counts (10k+ NPCs)
```cpp
// OLD: Local batching (deprecated)
std::vector<std::pair<EntityPtr, std::string>> localQueue;
localQueue.push_back({entity, behaviorName});
// ... process locally

// NEW: Global batching (recommended)
AIManager::Instance().queueBehaviorAssignment(entity, behaviorName);
// Automatically processed when AIManager::update() is called
```

## Current Processing Flow (v4.0+)

The AIManager processing flow has been optimized for performance and thread safety:

### Game State Integration

```cpp
void YourGameState::update() {
    // 1. Update player first
    m_player->update();
    
    // 2. Process AI system - handles queued assignments and entity updates
    AIManager::Instance().update();
    
    // 3. Your other game logic
    checkCollisions();
    updateUI();
}
```

### Internal Processing Order

When `AIManager::update()` is called, it processes in this order:

1. **Process Queued Assignments**: `processPendingBehaviorAssignments()` - handles all queued behavior changes
2. **Distance Calculations**: Get player reference and calculate entity distances  
3. **Frame Limiting**: Apply priority-based frame limiting based on distance thresholds
4. **Batch Processing**: Execute behavior logic for active entities using threading
5. **Cleanup**: Remove inactive entities and update performance statistics

### Key Benefits

- **Thread Safety**: All processing on main thread eliminates race conditions
- **Performance**: Batch processing with distance-based frame limiting
- **Flexibility**: Queued assignments allow behavior changes from any context
- **Reliability**: Centralized processing ensures consistent behavior

## Performance Optimizations

### 1. Entity Component Caching

Entity-behavior relationships are cached for faster lookups during updates:

```cpp
// The cache is automatically maintained
// Force a cache rebuild if needed
AIManager::Instance().ensureOptimizationCachesValid();
```

### 2. Batch Processing

Entities with the same behavior are processed in batches for better cache coherency:

```cpp
// Create a vector of entities
std::vector<Entity*> enemyGroup = getEnemiesInSector();

// Process all entities with the same behavior in one batch
AIManager::Instance().batchProcessEntities("ChaseBehavior", enemyGroup);
```

### 3. Early Exit Conditions

Set early exit conditions to skip unnecessary updates:

```cpp
// Create a behavior that only updates every 3 frames
auto patrolBehavior = std::make_shared<PatrolBehavior>(patrolPoints, 1.5f);
patrolBehavior->setUpdateFrequency(3);
AIManager::Instance().registerBehavior("Patrol", patrolBehavior);

// In your custom behavior class:
bool YourBehavior::shouldUpdate([[maybe_unused]] Entity* entity) const override {
    float distanceToPlayer = entity->getPosition().distance(player->getPosition());
    return distanceToPlayer < 1000.0f; // Skip updates for distant entities
}
```

### 4. Message Queue System

Messages can be queued for batch processing instead of immediate delivery. The system uses an optimized double-buffered queue for better performance:

```cpp
// Queue a message (default) - processed during next update
try {
    AIManager::Instance().sendMessageToEntity(npc.get(), "patrol");
} catch (const std::exception& e) {
    std::cerr << "Failed to queue message: " << e.what() << std::endl;
}

// Send message immediately when needed
try {
    AIManager::Instance().sendMessageToEntity(npc.get(), "evade", true);
} catch (const std::exception& e) {
    std::cerr << "Failed to deliver message: " << e.what() << std::endl;
}

// Manually process all queued messages (normally done during update)
AIManager::Instance().processMessageQueue();
```

### Performance Tips

1. **Limit active behaviors**: Only register and assign behaviors you're actively using.
2. **Optimize waypoints**: Use fewer waypoints for simple patrol routes.
3. **Adjust update frequency**: Use the built-in update frequency control for less important entities.
4. **Cull inactive entities**: Unassign behaviors from entities that are far from the player or inactive.
5. **Use batch processing**: Leverage the built-in batch processing for entities with the same behavior type.
6. **Use early exit conditions**: Configure behaviors to skip updates when not necessary.
7. **Queue non-urgent messages**: Use the message queue system for non-urgent communication.
8. **Add proper error handling**: Always wrap behavior code in try-catch blocks to prevent crashes.
9. **Use string_view parameters**: When possible, use std::string_view for string parameters to reduce copying.
10. **Examine performance statistics**: Use the built-in performance tracking to identify bottlenecks.

## Implementation Details

### Distance-Based Entity Optimization System

The AIManager uses a sophisticated distance-based optimization system for managing entities:

```cpp
// Entity update info structure
struct EntityUpdateInfo {
    EntityWeakPtr entityWeak;      // Weak reference to prevent circular dependencies
    int frameCounter{0};           // Frame counting for update frequency
    uint64_t lastUpdateTime{0};    // High-precision timing
    int priority{5};               // Entity priority (0-9) for distance-based updates
};
```

#### Distance Thresholds:
- **Close Distance** (≤6000 units): Update every frame
- **Medium Distance** (≤8000 units): Update every 15 frames  
- **Far Distance** (≤20000 units): Update every 30 frames
- **Out of Range** (>20000 units): Minimal updates

#### Priority System:
**Priority Multiplier**: `1.0 + (priority × 0.1)`

**Example**: Priority 1 NPC gets 1.1x range multiplier:
- Close range: 6000 × 1.1 = 6,600 units (every frame)
- Medium range: 8000 × 1.1 = 8,800 units (every 15 frames)
- Far range: 20000 × 1.1 = 22,000 units (every 30 frames)

**Priority 0-2**: Background entities | **Priority 3-5**: Standard entities | **Priority 6-9**: Important/Critical entities

### Individual Behavior Instances

Each entity gets its own behavior instance via the `clone()` method:

```cpp
// When assigning behavior to entity
std::shared_ptr<AIBehavior> behaviorTemplate = getBehavior(behaviorName);
std::shared_ptr<AIBehavior> behaviorInstance = behaviorTemplate->clone();
// Each entity has unique state
```

This prevents state interference between entities using the same behavior type.

### Message Queue System

The AIManager uses an asynchronous message queue for entity communication:
- Messages are queued and processed during the next update cycle
- Prevents immediate state changes that could cause race conditions
- Allows for batched message processing for better performance
The message queue system provides:
- Deferred message delivery for non-critical communications
- Batched processing of messages during update cycles
- Thread-safe message queue implementation with double-buffering
- Optimized memory handling with move semantics
- Performance statistics tracking for message processing
- Optional immediate delivery for time-critical messages
- Improved thread safety with enhanced synchronization

Messages are now prioritized alongside other tasks when using the updated ThreadSystem, ensuring critical messages are processed before lower-priority ones.

## API Reference

### Core AIManager Methods

```cpp
// Basic AIManager methods
bool init();
void update();
void clean();
void resetBehaviors();

// Behavior management
void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior);
bool hasBehavior(const std::string& behaviorName) const;
std::shared_ptr<AIBehavior> getBehavior(const std::string& behaviorName) const;
size_t getBehaviorCount() const;

// Entity-behavior assignment
void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);
void queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName);
size_t processPendingBehaviorAssignments();
size_t getPendingBehaviorAssignmentCount() const;

void unassignBehaviorFromEntity(EntityPtr entity);
bool entityHasBehavior(EntityPtr entity) const;

// 🔥 Consolidated Entity Registration System (v4.1+)
void registerEntityForUpdates(EntityPtr entity, int priority = 5);
void registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName);
void unregisterEntityFromUpdates(EntityPtr entity);
size_t getManagedEntityCount() const;

// Player reference for distance optimization
void setPlayerForDistanceOptimization(EntityPtr player);
EntityPtr getPlayerReference() const;
bool isPlayerValid() const;

// Performance monitoring
AIPerformanceStats getPerformanceStats() const;
size_t getBehaviorUpdateCount() const;

// Messaging system
void sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate = false);
void broadcastMessage(const std::string& message, bool immediate = false);
size_t processMessageQueue();

// 🆕 Global AI Pause/Resume System (v3.1+)
void setGlobalPause(bool paused);
bool isGloballyPaused() const;
```

### AIBehavior Methods

```cpp
// Core behavior methods
virtual void executeLogic(EntityPtr entity) = 0;
virtual void init(EntityPtr entity) = 0;
virtual void clean(EntityPtr entity) = 0;
virtual std::string getName() const = 0;

// Individual instance creation (REQUIRED for v2.1+ architecture)
virtual std::shared_ptr<AIBehavior> clone() const = 0;

// Optional message handling (with unused parameter attribute)
virtual void onMessage([[maybe_unused]] EntityPtr entity, 
                       [[maybe_unused]] const std::string& message);

// State management
virtual bool isActive() const;
virtual void setActive(bool active);

// Entity range checks (behavior-specific logic)
virtual bool isEntityInRange([[maybe_unused]] EntityPtr entity) const;

// Entity cleanup
virtual void cleanupEntity(EntityPtr entity);
```

For the complete API details, see:
- `include/managers/AIManager.hpp`
- `include/ai/AIBehavior.hpp`
- `include/ai/behaviors/WanderBehavior.hpp`
- `include/ai/behaviors/PatrolBehavior.hpp`
- `include/ai/behaviors/ChaseBehavior.hpp`
