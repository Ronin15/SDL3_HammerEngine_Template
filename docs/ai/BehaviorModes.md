# AI Behavior Modes Documentation

## Overview

The Forge Game Engine AI system includes mode-based behavior configuration for PatrolBehavior and WanderBehavior. These modes provide automatic setup for common patterns, eliminating manual configuration while ensuring consistent behavior across different NPC types.

## PatrolBehavior Modes

PatrolBehavior supports multiple patrol modes that automatically configure different types of patrol patterns.

### Available Modes

#### `PatrolMode::FIXED_WAYPOINTS`
**Use Case**: Traditional patrol routes with predefined waypoints
- **Configuration**: Uses manually defined waypoint sequences
- **Best For**: Guards with specific routes, scripted patrol paths

#### `PatrolMode::RANDOM_AREA`
**Use Case**: Dynamic patrol within a rectangular area
- **Configuration**: 
  - Area: Left 40% of screen (50px margin)
  - Waypoints: 6 random points
  - Min Distance: 80px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Area guards, flexible coverage

#### `PatrolMode::CIRCULAR_AREA`
**Use Case**: Patrol around a central location
- **Configuration**:
  - Area: Right 75% of screen, 120px radius
  - Waypoints: 5 random points in circle
  - Min Distance: 60px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Perimeter guards, defensive positions

#### `PatrolMode::EVENT_TARGET`
**Use Case**: Patrol around specific objectives or targets
- **Configuration**:
  - Target: Screen center (configurable)
  - Radius: 150px around target
  - Waypoints: 8 points with slight randomization
  - Pattern: Circular with varied distances
- **Best For**: Objective guards, VIP protection

### Registration Example

```cpp
void setupPatrolBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;
    
    // Fixed waypoint patrol
    auto fixedPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f
    );
    fixedPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Patrol", std::move(fixedPatrol));
    
    // Random area patrol
    auto randomPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f
    );
    randomPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));
    
    // Circular area patrol
    auto circlePatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f
    );
    circlePatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("CirclePatrol", std::move(circlePatrol));
    
    // Event target patrol
    auto eventPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f
    );
    eventPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("EventTarget", std::move(eventPatrol));
}
```

## WanderBehavior Modes

WanderBehavior supports multiple wander modes that automatically configure different movement ranges and behaviors.

### Available Modes

#### `WanderMode::SMALL_AREA`
**Use Case**: NPCs that should stay in personal/local space
- **Configuration**:
  - Radius: 75px
  - Direction Changes: Every 1.5 seconds
  - Offscreen Probability: 5%
- **Best For**: Guards at posts, shopkeepers, stationary NPCs

#### `WanderMode::MEDIUM_AREA`
**Use Case**: Standard wandering for most NPCs
- **Configuration**:
  - Radius: 200px
  - Direction Changes: Every 2.5 seconds
  - Offscreen Probability: 10%
- **Best For**: Villagers, general NPCs, default behavior

#### `WanderMode::LARGE_AREA`
**Use Case**: NPCs that need to cover large areas
- **Configuration**:
  - Radius: 450px
  - Direction Changes: Every 3.5 seconds
  - Offscreen Probability: 20%
- **Best For**: Merchants, scouts, explorers, roaming NPCs

#### `WanderMode::EVENT_TARGET`
**Use Case**: Wander around specific objectives or targets
- **Configuration**:
  - Radius: 150px around target
  - Direction Changes: Every 2.0 seconds
  - Offscreen Probability: 5%
- **Best For**: Objective guards, followers

### Registration Example

```cpp
void setupWanderBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;
    
    // Small area wander
    auto smallWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 1.5f
    );
    smallWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));
    
    // Medium area wander (standard)
    auto mediumWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f
    );
    mediumWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Wander", std::move(mediumWander));
    
    // Large area wander
    auto largeWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 2.5f
    );
    largeWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("LargeWander", std::move(largeWander));
    
    // Event target wander
    auto eventWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 2.0f
    );
    eventWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("EventWander", std::move(eventWander));
}
```

## NPC Type Assignment

### Example Assignment Logic

```cpp
std::string getBehaviorForNPCType(const std::string& npcType, size_t npcIndex) {
    static std::unordered_map<std::string, size_t> counters;
    size_t index = counters[npcType]++;
    
    if (npcType == "Guard") {
        std::vector<std::string> behaviors = {
            "Patrol", "RandomPatrol", "CirclePatrol", "SmallWander", "EventTarget"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Villager") {
        std::vector<std::string> behaviors = {
            "SmallWander", "Wander", "RandomPatrol"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Merchant") {
        std::vector<std::string> behaviors = {
            "Wander", "LargeWander", "RandomPatrol", "CirclePatrol"
        };
        return behaviors[index % behaviors.size()];
    }
    
    return "Wander"; // Default
}
```

### Complete Integration Example

```cpp
void GameState::createNPC(const std::string& npcType, const Vector2D& position) {
    auto npc = std::make_shared<NPC>(npcType, position, 64, 64);
    
    // Get behavior and priority for this NPC type
    static std::unordered_map<std::string, size_t> npcCounters;
    size_t npcIndex = npcCounters[npcType]++;
    
    std::string behaviorName = getBehaviorForNPCType(npcType, npcIndex);
    int priority = getPriorityForNPCType(npcType);
    
    // Register with AI system
    AIManager::Instance().registerEntityForUpdates(npc, priority, behaviorName);
    
    // Track the NPC
    m_npcs.push_back(npc);
}

int getPriorityForNPCType(const std::string& npcType) {
    if (npcType == "Guard") return 7;
    else if (npcType == "Merchant") return 5;
    else if (npcType == "Villager") return 2;
    return 3; // Default
}
```

## Mode Comparison Tables

### PatrolBehavior Mode Comparison

| Mode | Area Type | Waypoints | Auto-Regen | Use Case |
|------|-----------|-----------|------------|----------|
| FIXED_WAYPOINTS | Manual | User-defined | No | Scripted routes |
| RANDOM_AREA | Rectangle | 6 random | Yes | Flexible coverage |
| CIRCULAR_AREA | Circle | 5 random | Yes | Perimeter defense |
| EVENT_TARGET | Circle | 8 around target | No | Objective protection |

### WanderBehavior Mode Comparison

| Mode | Radius | Change Freq | Offscreen % | Use Case |
|------|--------|-------------|-------------|----------|
| SMALL_AREA | 75px | 1.5s | 5% | Local/stationary |
| MEDIUM_AREA | 200px | 2.5s | 10% | Standard NPCs |
| LARGE_AREA | 450px | 3.5s | 20% | Roaming NPCs |
| EVENT_TARGET | 150px | 2.0s | 5% | Objective-based |

## Best Practices

### 1. Mode Selection Guidelines
- **Guards**: Use SMALL_AREA wander for posts, RANDOM_AREA patrol for coverage
- **Villagers**: Prefer SMALL_AREA and MEDIUM_AREA wander to stay local
- **Merchants**: Use MEDIUM_AREA and LARGE_AREA for market movement
- **Scouts/Explorers**: Use LARGE_AREA modes for wide coverage

### 2. Configuration Tips
```cpp
// Always set screen dimensions for proper area calculation
behavior->setScreenDimensions(worldWidth, worldHeight);

// Adjust auto-regeneration based on gameplay needs
patrol->setAutoRegenerate(true);  // Dynamic patrol routes
patrol->setAutoRegenerate(false); // Static patrol routes

// Use appropriate speeds for each mode
// Small areas: 1.0-1.5f (slower, more precise)
// Large areas: 2.0-3.0f (faster, more ground coverage)
```

### 3. Performance Considerations
- Mode-based behaviors have minimal overhead compared to manual configuration
- Auto-regeneration adds slight CPU cost but provides dynamic behavior
- Large area modes may require more frequent updates for smooth movement

## Migration from Manual Configuration

### Before (Manual Setup)
```cpp
// Old way - manual configuration
auto patrol = std::make_unique<PatrolBehavior>(waypoints, 2.0f);
patrol->setRandomPatrolArea(topLeft, bottomRight, 6);
patrol->setAutoRegenerate(true);
patrol->setMinWaypointDistance(80.0f);
```

### After (Mode-Based)
```cpp
// New way - mode-based automatic configuration
auto patrol = std::make_unique<PatrolBehavior>(
    PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f
);
// All configuration handled automatically
```

## Advanced Configuration

### Dynamic Event Target Updates
```cpp
// Update event target position during gameplay
if (patrol->getPatrolMode() == PatrolBehavior::PatrolMode::EVENT_TARGET) {
    patrol->updateEventTarget(newTargetPosition);
}
```

### Custom Settings Override
```cpp
// Create mode-based behavior then customize
auto patrol = std::make_unique<PatrolBehavior>(
    PatrolBehavior::PatrolMode::RANDOM_AREA, 2.5f
);
patrol->setScreenDimensions(2560.0f, 1440.0f); // 4K screen
patrol->setAutoRegenerate(false); // Override auto-regeneration
patrol->setMinWaypointDistance(120.0f); // Larger spacing
```

This mode-based system provides cleaner code, better separation of concerns, and easier maintenance while preserving all the functionality of manual configuration.