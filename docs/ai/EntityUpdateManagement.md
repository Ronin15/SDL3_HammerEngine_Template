# AIManager Entity Update Management System

## Overview

The AIManager provides centralized entity update management with sophisticated distance-based optimization and priority-based tiered frame limiting. This system consolidates both entity updates (movement/animation) and AI behavior logic into a single, optimized controller.

## Architecture Benefits

### **Centralized Control**
- All NPC updates (AI behaviors + movement/animation) managed in one place
- Single distance calculation per entity for both movement and AI logic
- Unified priority system for tiered performance optimization
- Game states simply call one method for all NPC management

### **Tiered Performance Optimization**
The system uses a sophisticated priority-based approach that adjusts effective update ranges:

**Priority 0 (Background NPCs):**
- **Close entities** (< 800 units): Update every frame (100% performance)
- **Medium distance** (800-1000 units): Update every 15 frames (93% CPU reduction)
- **Far entities** (1000-2500 units): Update every 30 frames (97% CPU reduction)
- **Very distant** (> 2500 units): Update every 60 frames (98% CPU reduction)

**Priority 5 (Important NPCs):**
- **Close entities** (< 4800 units): Update every frame
- **Medium distance** (4800-6000 units): Update every 15 frames
- **Far entities** (6000-15000 units): Update every 30 frames
- **Very distant** (> 15000 units): Update every 60 frames

**Priority 9 (Boss/Critical NPCs):**
- **Close entities** (< 8000 units): Update every frame
- **Medium distance** (8000-10000 units): Update every 15 frames
- **Far entities** (10000-25000 units): Update every 30 frames
- **Very distant** (> 25000 units): Update every 60 frames

### **Simplified Game State Management**
Game states no longer need complex update loops:
```cpp
// OLD: Manual update management with multiple systems
for (auto& npc : npcs) {
    if (npc->shouldUpdateBasedOnDistance(player)) {
        npc->update();
    }
}
// Separate AI behavior updates with different timing...

// NEW: Single centralized call
AIManager::Instance().updateManagedEntities();
```

## API Reference

### Entity Registration

```cpp
// Register an entity for centralized updates with priority
void registerEntityForUpdates(EntityPtr entity, int priority = 5);

// Unregister an entity from updates
void unregisterEntityFromUpdates(EntityPtr entity);

// Set the player reference for distance calculations
void setPlayerForDistanceOptimization(EntityPtr player);
```

### Configuration

```cpp
// Configure base distance thresholds (before priority multiplier)
void configureDistanceThresholds(float maxUpdateDist = 8000.0f, 
                               float mediumUpdateDist = 10000.0f, 
                               float minUpdateDist = 25000.0f);

// Get entity count
size_t getRegisteredEntityCount() const;
```

### Automatic Processing

```cpp
// Called from game state update methods
void updateManagedEntities();
```

## Usage Examples

### Game State Setup

```cpp
bool MyGameState::enter() {
    // Create player
    m_player = std::make_shared<Player>();
    
    // Set player reference for distance optimization (once per state)
    AIManager::Instance().setPlayerForDistanceOptimization(m_player);
    
    // Create and register NPCs with different priorities
    for (int i = 0; i < npcCount; ++i) {
        auto npc = std::make_shared<NPC>();
        
        // Determine priority based on NPC importance
        int priority = 5; // Default priority
        if (isImportantNPC(npc)) {
            priority = 9;  // High priority - large update ranges
        } else if (isGuardNPC(npc)) {
            priority = 5;  // Medium priority
        } else {
            priority = 2;  // Low priority - small update ranges
        }
        
        // Register with AIManager for centralized updates with priority
        AIManager::Instance().registerEntityForUpdates(npc, priority);
        
        // Assign AI behavior (behaviors have no priority - only define logic)
        std::string behaviorName = determineBehaviorType(npc);
        AIManager::Instance().assignBehaviorToEntity(npc, behaviorName);
        
        m_npcs.push_back(npc);
    }
    
    return true;
}
```

### Game State Update

```cpp
void MyGameState::update() {
    // Update player (always full rate)
    if (m_player) {
        m_player->update();
    }
    
    // Let AIManager handle ALL NPC updates (movement + AI logic with optimization)
    AIManager::Instance().updateManagedEntities();
    
    // Handle game-specific logic
    handleInput();
    updateGameLogic();
    
    // Clean up invalid entities if needed
    cleanupDeadNPCs();
}
```

### Setting Entity Priorities

```cpp
void createNPCs() {
    // Create different types of NPCs with appropriate priorities
    
    // Background villagers (low priority)
    for (int i = 0; i < villagerCount; ++i) {
        auto villager = std::make_shared<NPC>();
        AIManager::Instance().registerEntityForUpdates(villager, 2);  // Low priority
        AIManager::Instance().assignBehaviorToEntity(villager, "Wander");
    }
    
    // Guard patrols (medium priority)  
    for (int i = 0; i < guardCount; ++i) {
        auto guard = std::make_shared<NPC>();
        AIManager::Instance().registerEntityForUpdates(guard, 5);  // Medium priority
        AIManager::Instance().assignBehaviorToEntity(guard, "Patrol");
    }
    
    // Boss enemies (high priority)
    for (int i = 0; i < bossCount; ++i) {
        auto boss = std::make_shared<NPC>();
        AIManager::Instance().registerEntityForUpdates(boss, 9);  // High priority
        AIManager::Instance().assignBehaviorToEntity(boss, "Chase");
    }
}

void setupBehaviorTemplates() {
    // Behaviors only define logic - no priorities
    auto wanderBehavior = std::make_shared<WanderBehavior>();
    AIManager::Instance().registerBehavior("Wander", wanderBehavior);
    
    auto patrolBehavior = std::make_shared<PatrolBehavior>(waypoints);
    AIManager::Instance().registerBehavior("Patrol", patrolBehavior);
    
    auto chaseBehavior = std::make_shared<ChaseBehavior>();
    AIManager::Instance().registerBehavior("Chase", chaseBehavior);
}
```

### Game State Cleanup

```cpp
bool MyGameState::exit() {
    // Unregister all entities
    for (auto& npc : m_npcs) {
        if (npc) {
            AIManager::Instance().unregisterEntityFromUpdates(npc);
            AIManager::Instance().unassignBehaviorFromEntity(npc);
        }
    }
    
    m_npcs.clear();
    return true;
}
```

## Implementation Details

### Thread Safety
- All entity registration/unregistration operations are thread-safe
- Uses shared_mutex for optimal read/write performance during updates
- Automatic cleanup of expired entity weak_ptr references
- No memory leaks across game state transitions

### Priority System Algorithm
```cpp
// Entity priority multiplier calculation (priority stored per entity in AIManager)
float priorityMultiplier = globalMultiplier * ((entityPriority + 1) / 10.0f); // 0.1 to 1.0 range

// Effective distance calculation using entity's priority
if (distSq < maxDist * maxDist * priorityMultiplier) {
    requiredFrames = 1;  // Every frame for close entities
} else if (distSq < mediumDist * mediumDist * priorityMultiplier) {
    requiredFrames = 15; // Every 15 frames for medium distance
} else if (distSq < minDist * minDist * priorityMultiplier) {
    requiredFrames = 30; // Every 30 frames for far distance
} else {
    requiredFrames = 60; // Every 60 frames for very distant entities
}
```

### Memory Management
- Uses weak_ptr to avoid circular references
- Automatic cleanup of expired entities during updates
- Per-entity frame counters managed internally
- Graceful handling of entity destruction

## Integration with Existing Systems

### AI Behavior System
- **Unified Priority System**: Entity updates automatically use the same priority as their assigned AI behavior
- **Synchronized Updates**: Both AI decision-making and entity movement use identical timing
- **Pure Logic Behaviors**: Behaviors now focus only on logic, AIManager handles all timing

### Game Engine Integration
- Called from game state update methods (main thread)
- No background thread entity updates (proper game loop integration)
- Leverages existing ThreadSystem for AI behavior processing
- Clean separation between game logic and background tasks

### State Management
- Survives game state transitions naturally
- Clean separation between AI logic and entity updates
- Consistent behavior across all game states
- Simple migration from manual update loops

## Migration Guide

### From Manual Entity Updates

1. **Remove manual update loops** from game states
2. **Set player reference** with `setPlayerForDistanceOptimization()` (once per state)
3. **Register entities** with `registerEntityForUpdates(entity, priority)`
4. **Call updateManagedEntities()** in game state update
5. **Set entity priorities** during registration based on NPC importance
6. **Unregister entities** in cleanup/exit methods

### Entity Priority Configuration Guidelines

- **Priority 0-2**: Background NPCs, ambient creatures, distant objects (small update ranges)
- **Priority 3-6**: Guards, merchants, interactive NPCs (medium update ranges)
- **Priority 7-9**: Bosses, main characters, critical gameplay elements (large update ranges)

Note: Priorities are set per entity during registration, not on behavior templates.

### Performance Tuning

**Conservative Settings** (better performance):
```cpp
AIManager::Instance().configureDistanceThresholds(5000, 7500, 15000);
```

**Quality Settings** (smoother animation):
```cpp
AIManager::Instance().configureDistanceThresholds(12000, 18000, 35000);
```

**Default Settings** (balanced):
```cpp
AIManager::Instance().configureDistanceThresholds(8000, 10000, 25000);
```

## Performance Characteristics

### Scalability Targets

| Entity Count | Performance | Priority Mix | Use Case |
|--------------|-------------|--------------|----------|
| **< 100** | Excellent | Any | Small scenes |
| **100-500** | Very Good | Mostly low priority | Medium scenes |
| **500-1000** | Good | 80% low, 20% high priority | Large battles |
| **1000+** | Acceptable | 90% low, 10% high priority | Massive worlds |

### CPU Impact by Priority

| Priority Level | Effective Range | CPU Impact | Typical NPCs |
|---------------|----------------|------------|--------------|
| **0-2** | 800-2400 units | Low | Villagers, animals |
| **3-6** | 3200-6400 units | Medium | Guards, merchants |
| **7-9** | 6400-8000 units | High | Bosses, main characters |

## Future Enhancements

### Planned Features
- **Dynamic priority adjustment**: Runtime priority changes based on gameplay events
- **Spatial partitioning**: Only check entities in nearby regions
- **Adaptive thresholds**: Dynamic adjustment based on frame rate
- **Entity pooling**: Efficient reuse of entity objects
- **LOD integration**: Different animation quality based on distance
- **Update scheduling**: Spread updates across multiple frames for consistent performance

### Performance Metrics
- Track update frequency per priority tier
- Monitor frame time impact per entity count
- Measure memory usage optimization
- Profile thread utilization during batch processing