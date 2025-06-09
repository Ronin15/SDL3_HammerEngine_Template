# AIManager Unified Spatial System

## Overview

The AIManager provides a high-performance, unified spatial system for managing AI entities. It combines entity registration, behavior assignment, and optimized updates into a single, cache-friendly system that can efficiently handle thousands of entities.

## Architecture

### **Unified Spatial System**
- Single `AIEntityData` structure stores all entity information
- Cache-friendly batch processing with optimal memory layout
- Type-indexed behavior dispatch for fast execution
- Individual behavior instances via clone() pattern
- Distance-based frame skipping for performance optimization

### **Core Components**
- **AIEntityData**: Spatial data structure with behavior, position, timing, priority
- **Batch Processing**: ThreadSystem integration for parallel processing
- **Distance Optimization**: Frame skipping based on player distance and priority
- **Performance Monitoring**: Built-in statistics and profiling

## Usage Pattern

### Registration & Assignment
```cpp
// 1. Register entity with priority
AIManager::Instance().registerEntityForUpdates(npc, 5);

// 2. Assign behavior (creates individual behavior instance)
AIManager::Instance().assignBehaviorToEntity(npc, "Wander");

// Entity is now fully managed by unified system
```

### Game State Integration
```cpp
void GameState::enter() {
    // Set player reference for distance optimization
    AIManager::Instance().setPlayerForDistanceOptimization(m_player);
    
    // Create and register entities
    for (int i = 0; i < npcCount; ++i) {
        auto npc = std::make_shared<NPC>();
        AIManager::Instance().registerEntityForUpdates(npc, 5);
        AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
    }
}

void GameState::update() {
    // Update player manually
    m_player->update();
    
    // All AI entities updated automatically by AIManager::Instance().update() in main GameLoop
}
```

## Performance Characteristics

### **Distance Optimization**
Entities are updated at different frequencies based on distance from player:

| Distance Range | Update Frequency | CPU Usage |
|---------------|------------------|-----------|
| **Close** (≤ maxDist) | Every frame | 100% |
| **Medium** (≤ mediumDist) | Every 15 frames | ~7% |
| **Far** (≤ minDist) | Every 30 frames | ~3% |
| **Very Far** (> minDist) | Every 60 frames | ~2% |

### **Priority System**
Higher priority entities get larger effective distance thresholds:

```cpp
// Priority affects effective distance calculation
float priorityFactor = 1.0f + (entityPriority * 0.1f);
float adjustedMaxDist = maxUpdateDistance * priorityMultiplier * priorityFactor;
```

**Example with default distances (1000, 500, 200):**
- **Priority 0**: Effective distances (1000, 500, 200)
- **Priority 5**: Effective distances (1500, 750, 300) 
- **Priority 9**: Effective distances (1900, 950, 380)

### **Batch Processing**
- Automatic threading for large entity counts (>100 entities)
- Optimal batch sizes (1000-10000 entities per batch)
- ThreadSystem integration for parallel processing
- Timeout protection against hung tasks

## API Reference

### **Core Methods**
```cpp
// Entity lifecycle
void registerEntityForUpdates(EntityPtr entity, int priority = 0);
void unregisterEntityFromUpdates(EntityPtr entity);

// Behavior management  
void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);
void unassignBehaviorFromEntity(EntityPtr entity);
bool entityHasBehavior(EntityPtr entity) const;

// Player reference for distance optimization
void setPlayerForDistanceOptimization(EntityPtr player);
EntityPtr getPlayerReference() const;

// System control
void update(); // Called by GameEngine - processes all entities
void setGlobalPause(bool paused);
bool isGloballyPaused() const;

// Monitoring
size_t getManagedEntityCount() const;
AIPerformanceStats getPerformanceStats() const;
```

### **Configuration**
```cpp
// Threading configuration
void configureThreading(bool useThreading, unsigned int maxThreads = 0);

// Priority multiplier (affects all distance calculations)
void configurePriorityMultiplier(float multiplier = 1.0f);
```

## Implementation Details

### **AIEntityData Structure**
```cpp
struct AIEntityData {
    EntityPtr entity;                    // Strong reference to entity
    std::shared_ptr<AIBehavior> behavior; // Individual behavior instance
    BehaviorType behaviorType;           // Type-indexed for fast dispatch
    Vector2D lastPosition;               // Spatial tracking
    float lastUpdateTime;                // Timing optimization
    int frameCounter;                    // Per-entity frame counting
    int priority;                        // Priority level (0-9)
    bool active;                         // Active state flag
};
```

### **Distance Thresholds**
```cpp
// Default distance settings (can be modified)
std::atomic<float> m_maxUpdateDistance{1000.0f};     // Close range
std::atomic<float> m_mediumUpdateDistance{500.0f};   // Medium range  
std::atomic<float> m_minUpdateDistance{200.0f};      // Far range
std::atomic<float> m_priorityMultiplier{1.0f};       // Global multiplier
```

### **Update Algorithm**
```cpp
bool shouldUpdateEntity(EntityPtr entity, EntityPtr player, int& frameCounter, int priority) {
    frameCounter++;
    
    if (!player) {
        return frameCounter % 10 == 0; // Update every 10 frames if no player
    }
    
    float distance = (entity->getPosition() - player->getPosition()).length();
    float priorityFactor = 1.0f + (priority * 0.1f);
    
    float adjustedMaxDist = m_maxUpdateDistance * m_priorityMultiplier * priorityFactor;
    float adjustedMediumDist = m_mediumUpdateDistance * m_priorityMultiplier * priorityFactor;
    float adjustedMinDist = m_minUpdateDistance * m_priorityMultiplier * priorityFactor;
    
    if (distance <= adjustedMaxDist) {
        return true; // Every frame
    } else if (distance <= adjustedMediumDist) {
        return frameCounter % 15 == 0; // Every 15 frames
    } else if (distance <= adjustedMinDist) {
        return frameCounter % 30 == 0; // Every 30 frames
    }
    
    return frameCounter % 60 == 0; // Every 60 frames
}
```

## Thread Safety

- **Registration/Unregistration**: Thread-safe using shared_mutex
- **Behavior Assignment**: Thread-safe with proper locking
- **Update Processing**: Read-only access during batch processing
- **Global Pause**: Atomic boolean for thread-safe access
- **Performance Stats**: Mutex-protected statistics collection

## Memory Management

- **Strong References**: EntityPtr (shared_ptr) prevents premature deletion
- **Individual Behaviors**: Each entity gets own behavior instance via clone()
- **Automatic Cleanup**: Inactive entities removed during cleanup cycles
- **Index Rebuilding**: Efficient index map reconstruction after cleanup

## Performance Tuning

### **Entity Count Guidelines**
| Entity Count | Performance | Recommended Settings |
|--------------|-------------|---------------------|
| **< 1,000** | Excellent | Default settings |
| **1,000-5,000** | Very Good | Consider threading |
| **5,000-10,000** | Good | Enable threading, tune priorities |
| **10,000+** | Acceptable | Aggressive distance optimization |

### **Priority Assignment Guidelines**
- **Priority 0-2**: Background NPCs, ambient creatures
- **Priority 3-5**: Interactive NPCs, guards, merchants  
- **Priority 6-8**: Important characters, mini-bosses
- **Priority 9**: Main bosses, critical gameplay elements

### **Optimization Tips**
1. Use appropriate priorities for different NPC types
2. Set reasonable world bounds for distance calculations
3. Enable threading for large entity counts
4. Monitor performance stats to identify bottlenecks
5. Consider reducing update frequencies for distant entities

## Migration from Old Systems

### **From Dual Entity Management**
1. Remove `updateManagedEntities()` calls from game states
2. Keep `registerEntityForUpdates()` and `assignBehaviorToEntity()` calls
3. Remove manual entity update loops
4. Let GameEngine handle all updates via `AIManager::update()`

### **From Manual Update Loops**
1. Replace manual NPC update loops with registration calls
2. Set player reference with `setPlayerForDistanceOptimization()`
3. Remove distance calculations from game code
4. Trust the unified system to handle optimization

## Future Enhancements

- **Spatial Partitioning**: Only check entities in nearby spatial regions
- **Adaptive Thresholds**: Dynamic distance adjustment based on performance
- **LOD Integration**: Different behavior complexity based on distance
- **Predictive Updates**: Skip updates for entities with predictable movement
- **Memory Pool**: Pre-allocated entity data for better cache performance
