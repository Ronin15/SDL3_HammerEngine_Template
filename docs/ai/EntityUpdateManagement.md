# AIManager Entity Update Management System

## Overview

The AIManager has been extended to provide centralized entity update management with distance-based optimization. This system consolidates entity updates across all game states, providing consistent performance optimization and simplifying game state management.

## Architecture Benefits

### **Centralized Update Management**
- All entity updates (AI behaviors + movement/animation) managed in one place
- Consistent distance-based optimization across all game states
- Single point of control for update frequency and performance tuning

### **Performance Optimization**
Simple distance-based update frequency optimization:

- **Close entities** (< 8000 units): Update every frame (100% performance)
- **Medium distance** (8000-10000 units): Update every 15 frames (93% CPU reduction)
- **Far entities** (10000-25000 units): Update every 30 frames (97% CPU reduction)
- **Very distant** (> 25000 units): Update every 60 frames (98% CPU reduction)

### **Simplified Game States**
Game states no longer need to manage complex update loops:
```cpp
// OLD: Manual update management
for (auto& npc : npcs) {
    if (npc->shouldUpdateBasedOnDistance(player)) {
        npc->update();
    }
}

// NEW: Simple registration
AIManager::Instance().registerEntityForUpdates(npc, player);
```

## API Reference

### Entity Registration

```cpp
// Register an entity for centralized updates
void registerEntityForUpdates(EntityPtr entity, EntityPtr player = nullptr);

// Unregister an entity from updates
void unregisterEntityFromUpdates(EntityPtr entity);

// Set the player reference for distance calculations
void setPlayerForDistanceOptimization(EntityPtr player);
```

### Configuration

```cpp
// Configure distance thresholds
void configureDistanceThresholds(float maxUpdateDist = 8000.0f, 
                               float mediumUpdateDist = 10000.0f, 
                               float minUpdateDist = 25000.0f);

// Get entity count
size_t getRegisteredEntityCount() const;
```

### Automatic Processing

```cpp
// Called automatically by AIManager::update()
void updateManagedEntities();
```

## Usage Examples

### Game State Setup

```cpp
bool MyGameState::enter() {
    // Create player
    m_player = std::make_shared<Player>();
    
    // Set player reference for distance optimization
    AIManager::Instance().setPlayerForDistanceOptimization(m_player);
    
    // Create and register NPCs
    for (int i = 0; i < npcCount; ++i) {
        auto npc = std::make_shared<NPC>();
        
        // Register with AIManager for centralized updates
        AIManager::Instance().registerEntityForUpdates(npc, m_player);
        
        // Assign AI behavior (optional)
        AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
        
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
    
    // NPC updates now handled automatically by AIManager
    // Just clean up invalid entities
    auto it = m_npcs.begin();
    while (it != m_npcs.end()) {
        if (*it) {
            ++it;
        } else {
            // Clean up dead entities
            AIManager::Instance().unregisterEntityFromUpdates(*it);
            it = m_npcs.erase(it);
        }
    }
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
- Uses shared_mutex for optimal read/write performance
- Automatic cleanup of expired entity weak_ptr references

### Memory Management
- Uses weak_ptr to avoid circular references
- Automatic cleanup of expired entities during updates
- No memory leaks across game state transitions

### Performance Monitoring
- Integration with AIManager's existing performance tracking
- Exception handling for robust entity updates
- Graceful degradation when entities are destroyed

## **Distance Optimization Algorithm**

```cpp
bool shouldUpdateEntity(EntityPtr entity, EntityPtr player, int& frameCounter) {
    frameCounter++;
    
    if (!player) return true; // Always update if no player reference
    
    Vector2D toPlayer = player->getPosition() - entity->getPosition();
    float distSq = toPlayer.lengthSquared();
    
    int requiredFrames;
    if (distSq < maxDist * maxDist) {
        requiredFrames = 1;  // Every frame for close entities
    } else if (distSq < mediumDist * mediumDist) {
        requiredFrames = 15; // Every 15 frames for medium distance
    } else if (distSq < minDist * minDist) {
        requiredFrames = 30; // Every 30 frames for far distance
    } else {
        requiredFrames = 60; // Every 60 frames for very distant entities
    }
    
    if (frameCounter >= requiredFrames) {
        frameCounter = 0;
        return true;
    }
    
    return false;
}
```

## Integration with Existing Systems

### AI Behavior System
- **Independent Optimization**: Entity updates use simple distance-based optimization
- **AI Behavior Optimization**: AI behaviors have their own priority-based distance optimization
- **Simple Integration**: Works alongside existing AI system without complex interactions

### Game Engine Integration
- Called automatically from `GameEngine::processBackgroundTasks()`
- No changes required to main game loop
- Leverages existing ThreadSystem for parallel processing

### State Management
- Survives game state transitions naturally
- Clean separation between AI logic and entity updates
- Consistent behavior across all game states

## Migration Guide

### From Manual Entity Updates

1. **Remove manual update loops** from game states
2. **Register entities** with `registerEntityForUpdates()`
3. **Set player reference** with `setPlayerForDistanceOptimization()`
4. **Unregister entities** in cleanup/exit methods

### **Configuration Options**

Distance threshold configurations:
- **Conservative**: Use default thresholds (8000/10000/25000)
- **Aggressive**: Reduce thresholds for better performance (5000/7500/15000)
- **Quality**: Increase thresholds for smoother animation (12000/18000/35000)

## Future Enhancements

### Planned Features
- **Priority-based updates**: High-priority entities update more frequently
- **Level-of-detail (LOD)**: Different animation quality based on distance
- **Adaptive thresholds**: Dynamic adjustment based on frame rate
- **Entity pooling**: Efficient reuse of entity objects
- **Update scheduling**: Spread updates across multiple frames for consistent performance

### Performance Metrics
- Track update frequency per distance tier
- Monitor frame time impact
- Measure memory usage optimization
- Profile thread utilization