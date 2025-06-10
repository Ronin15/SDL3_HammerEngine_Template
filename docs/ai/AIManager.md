# AI Manager System Documentation

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

## Individual Behavior Instances Architecture

### Core Architecture Principle

**Each NPC receives its own cloned behavior instance** to ensure complete state isolation and thread safety.

All behaviors implement the `clone()` method:

```cpp
class PatrolBehavior : public AIBehavior {
public:
    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed, m_includeOffscreenPoints);
        cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
        cloned->setActive(m_active);
        cloned->setPriority(m_priority);
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
- `executeLogic(Entity*)`: Called each frame to update entity movement/actions
- `init(Entity*)`: Called when a behavior is first assigned to an entity
- `clean(Entity*)`: Called when a behavior is removed from an entity
- `onMessage(Entity*, const std::string&)`: Handles messages sent to the behavior
- `clone()`: Creates individual behavior instances for each entity

## Available Behaviors

### WanderBehavior

Entities move randomly within a defined area, changing direction periodically.

**Mode-Based Configuration:**
- `SMALL_AREA`: 75px radius, local movement
- `MEDIUM_AREA`: 200px radius, standard NPCs
- `LARGE_AREA`: 450px radius, roaming NPCs
- `EVENT_TARGET`: 150px radius around specific targets

### PatrolBehavior

Entities follow predefined paths or patrol areas with various patterns.

**Mode-Based Configuration:**
- `FIXED_WAYPOINTS`: Traditional patrol routes with predefined waypoints
- `RANDOM_AREA`: Dynamic patrol within rectangular areas
- `CIRCULAR_AREA`: Patrol around central locations
- `EVENT_TARGET`: Patrol around specific objectives

### ChaseBehavior

Entities pursue a target (typically the player) when within detection range.

**Configuration options:**
- Target entity reference
- Chase speed
- Maximum detection/pursuit range
- Minimum distance to maintain from target

## Quick Start

### Basic Setup

```cpp
// Initialize AI Manager
AIManager::Instance().init();

// Register mode-based behaviors
auto wanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));

auto patrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 1.5f);
AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));

// Create chase behavior (targeting the player)
auto chaseBehavior = std::make_shared<ChaseBehavior>(player, 2.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("Chase", chaseBehavior);
```

### Entity Registration and Behavior Assignment

```cpp
// Create an NPC
auto npc = std::make_shared<NPC>("npc_sprite", Vector2D(250, 250), 64, 64);

// Register entity with priority and assign behavior
AIManager::Instance().registerEntityForUpdates(npc, 5);
AIManager::Instance().assignBehaviorToEntity(npc, "Wander");

// Alternative: Combined registration and assignment
AIManager::Instance().registerEntityForUpdates(npc, 5, "Wander");

// Set player reference for distance optimization
AIManager::Instance().setPlayerForDistanceOptimization(player);
```

### Game Loop Integration

```cpp
void GameState::update() {
    // Update player first
    player->update();
    
    // AI Manager handles all entity updates automatically via GameEngine
    // No manual AIManager::update() call needed in game states
}
```

## Advanced Features

### Global AI Pause/Resume System

```cpp
// Pause all AI processing globally
AIManager::Instance().setGlobalPause(true);

// Resume all AI processing
AIManager::Instance().setGlobalPause(false);

// Check current pause state
bool isPaused = AIManager::Instance().isGloballyPaused();
```

### Message System

```cpp
// Send message to specific entity
AIManager::Instance().sendMessageToEntity(npc, "pause", true);

// Broadcast message to all entities
AIManager::Instance().broadcastMessage("resume");

// Messages are processed automatically during update cycle
```

### Batch Behavior Assignment

```cpp
// Queue multiple assignments for efficient processing
for (auto& npc : npcs) {
    AIManager::Instance().queueBehaviorAssignment(npc, "Wander");
}

// Assignments processed automatically by GameEngine
size_t processed = AIManager::Instance().processPendingBehaviorAssignments();
```

## Performance Optimization

### Distance-Based Entity Optimization

The AIManager uses sophisticated distance-based optimization:

| Distance Range | Update Frequency | Priority Multiplier |
|---------------|------------------|-------------------|
| **Close** (≤4000 units) | Every frame | 1.0 + (priority × 0.1) |
| **Medium** (≤6000 units) | Every 15 frames | Applied to thresholds |
| **Far** (≤10000 units) | Every 30 frames | Higher priority = larger range |

### Priority System

**Priority Levels (0-9):**
- **0-2**: Background entities (ambient creatures, decorative NPCs)
- **3-5**: Standard entities (villagers, merchants, guards)
- **6-8**: Important entities (quest NPCs, mini-bosses)
- **9**: Critical entities (main bosses, story characters)

Higher priority entities get larger effective update distances and more frequent processing.

### Threading & Batch Processing

- Automatic threading for large entity counts (>200 entities)
- Optimal batch sizes (64-1000 entities per batch)
- ThreadSystem integration for parallel processing
- Worker budget allocation (60% of available workers)

## Performance Monitoring

```cpp
// Get detailed performance statistics
AIPerformanceStats stats = AIManager::Instance().getPerformanceStats();
std::cout << "Entities per second: " << stats.entitiesPerSecond << std::endl;

// Monitor entity and behavior counts
size_t entityCount = AIManager::Instance().getManagedEntityCount();
size_t behaviorCount = AIManager::Instance().getBehaviorCount();
size_t totalAssignments = AIManager::Instance().getTotalAssignmentCount();
```

## Creating Custom Behaviors

```cpp
class FlankingBehavior : public AIBehavior {
public:
    FlankingBehavior(EntityPtr target, float speed = 2.0f);

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
        return std::make_shared<FlankingBehavior>(m_target, m_speed);
    }

private:
    EntityPtr m_target;
    float m_speed;
};
```

## API Reference

### Core AIManager Methods

```cpp
// Initialization
bool init();
void clean();

// Behavior management
void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior);
bool hasBehavior(const std::string& behaviorName) const;
std::shared_ptr<AIBehavior> getBehavior(const std::string& behaviorName) const;

// Entity registration
void registerEntityForUpdates(EntityPtr entity, int priority = 5);
void registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName);
void unregisterEntityFromUpdates(EntityPtr entity);

// Behavior assignment
void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);
void queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName);
size_t processPendingBehaviorAssignments();
void unassignBehaviorFromEntity(EntityPtr entity);
bool entityHasBehavior(EntityPtr entity) const;

// Player reference for distance optimization
void setPlayerForDistanceOptimization(EntityPtr player);
EntityPtr getPlayerReference() const;
bool isPlayerValid() const;

// Global controls
void setGlobalPause(bool paused);
bool isGloballyPaused() const;

// Messaging system
void sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate = false);
void broadcastMessage(const std::string& message, bool immediate = false);

// Performance monitoring
AIPerformanceStats getPerformanceStats() const;
size_t getManagedEntityCount() const;
size_t getBehaviorCount() const;
size_t getTotalAssignmentCount() const;
```

### AIBehavior Interface

```cpp
// Core behavior methods (pure virtual)
virtual void executeLogic(EntityPtr entity) = 0;
virtual void init(EntityPtr entity) = 0;
virtual void clean(EntityPtr entity) = 0;
virtual std::string getName() const = 0;
virtual std::shared_ptr<AIBehavior> clone() const = 0;

// Optional methods
virtual void onMessage(EntityPtr entity, const std::string& message);
virtual bool isActive() const;
virtual void setActive(bool active);
virtual bool isEntityInRange(EntityPtr entity) const;
```

## Best Practices

### Entity Creation Pattern

```cpp
void createNPCGroup(const std::string& npcType, int count) {
    for (int i = 0; i < count; ++i) {
        auto npc = createNPC(npcType);
        
        // Determine behavior based on NPC type
        std::string behavior = getBehaviorForNPCType(npcType, i);
        int priority = getPriorityForNPCType(npcType);
        
        // Single call for registration and assignment
        AIManager::Instance().registerEntityForUpdates(npc, priority, behavior);
    }
}
```

### Performance Tips

1. **Use appropriate priorities** for different NPC types
2. **Set player reference** for distance optimization
3. **Queue assignments** for batch processing
4. **Monitor performance stats** to identify bottlenecks
5. **Use mode-based behaviors** for consistent configuration
6. **Clean up properly** when changing game states

### Error Handling

```cpp
// Always wrap AI operations in try-catch blocks
try {
    AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
} catch (const std::exception& e) {
    std::cerr << "Failed to assign behavior: " << e.what() << std::endl;
}
```

### Cleanup on State Transitions

```cpp
void GameState::exit() {
    // Unregister entities before state change
    for (auto& npc : m_npcs) {
        AIManager::Instance().unregisterEntityFromUpdates(npc);
        AIManager::Instance().unassignBehaviorFromEntity(npc);
    }
    
    // Note: Don't clean AIManager - it's used across game states
}
```

## Thread Safety

The AIManager is designed to be thread-safe:
- **Registration/Assignment**: Protected by shared_mutex
- **Message Queue**: Thread-safe double-buffered system
- **Global Pause**: Atomic boolean for lock-free access
- **Performance Stats**: Mutex-protected collection
- **Behavior Processing**: Read-only access during updates

## Memory Management

- **Strong References**: EntityPtr (shared_ptr) prevents premature deletion
- **Individual Behaviors**: Each entity gets own behavior instance via clone()
- **Automatic Cleanup**: Inactive entities removed during cleanup cycles
- **Efficient Containers**: Pre-allocated vectors and optimized data structures

## Integration with Game Engine

The AIManager integrates seamlessly with the GameEngine:

1. **GameEngine calls** `AIManager::update()` automatically each frame
2. **Batch assignments** are processed before entity updates
3. **ThreadSystem integration** provides parallel processing
4. **Worker budget system** ensures fair resource allocation

This eliminates the need for manual AI update calls in game states while providing optimal performance and resource management.

For specific behavior configuration details, see the behavior mode documentation. For integration patterns and advanced usage, refer to the developer guides.