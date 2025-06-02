# AI Behavior Modes Documentation

## Overview

The Forge Game Engine AI system includes advanced mode-based behavior systems for PatrolBehavior and WanderBehavior. These modes allow you to easily create different variants of behaviors without manual configuration, providing clean separation of concerns and reusable behavior patterns.

## PatrolBehavior Modes

PatrolBehavior supports multiple patrol modes that automatically configure different types of patrol patterns. Each mode is designed for specific use cases and automatically sets up appropriate parameters.

### Available Patrol Modes

#### `PatrolMode::FIXED_WAYPOINTS`
**Use Case**: Traditional patrol routes with predefined waypoints
- **Configuration**: Uses manually defined waypoint sequences
- **Best For**: Guards with specific routes, scripted patrol paths
- **Default Setup**: Creates a rectangular patrol pattern if no waypoints provided

#### `PatrolMode::RANDOM_AREA`
**Use Case**: Dynamic patrol within a rectangular area
- **Configuration**: 
  - Area: Left 40% of screen (50px margin)
  - Waypoints: 6 random points
  - Min Distance: 80px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Area guards, market patrol, flexible coverage

#### `PatrolMode::CIRCULAR_AREA`
**Use Case**: Patrol around a central location
- **Configuration**:
  - Area: Right 75% of screen, 120px radius
  - Waypoints: 5 random points in circle
  - Min Distance: 60px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Perimeter guards, defensive positions, area denial

#### `PatrolMode::EVENT_TARGET`
**Use Case**: Patrol around specific objectives or targets
- **Configuration**:
  - Target: Screen center (configurable)
  - Radius: 150px around target
  - Waypoints: 8 points with slight randomization
  - Pattern: Circular with varied distances
- **Best For**: Objective guards, VIP protection, dynamic targeting

### Registration Examples

```cpp
// Register different patrol behavior variants
void setupPatrolBehaviors() {
    // Fixed waypoint patrol (traditional)
    auto fixedPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f, true
    );
    AIManager::Instance().registerBehavior("Patrol", std::move(fixedPatrol));
    
    // Random area patrol
    auto randomPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f, false
    );
    AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));
    
    // Circular area patrol
    auto circlePatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f, false
    );
    AIManager::Instance().registerBehavior("CirclePatrol", std::move(circlePatrol));
    
    // Event target patrol
    auto eventPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f, false
    );
    AIManager::Instance().registerBehavior("EventTarget", std::move(eventPatrol));
}
```

### NPC Type Assignment Examples

```cpp
std::string getPatrolBehaviorForNPC(const std::string& npcType, size_t npcIndex) {
    if (npcType == "Guard") {
        std::vector<std::string> guardBehaviors = {
            "Patrol", "RandomPatrol", "CirclePatrol", "EventTarget"
        };
        return guardBehaviors[npcIndex % guardBehaviors.size()];
    }
    else if (npcType == "Merchant") {
        std::vector<std::string> merchantBehaviors = {
            "RandomPatrol", "CirclePatrol", "Patrol"
        };
        return merchantBehaviors[npcIndex % merchantBehaviors.size()];
    }
    return "Patrol"; // Default
}
```

### Advanced Configuration

```cpp
// Create patrol behavior with custom screen dimensions
auto patrol = std::make_unique<PatrolBehavior>(
    PatrolBehavior::PatrolMode::RANDOM_AREA, 2.5f
);
patrol->setScreenDimensions(1920.0f, 1080.0f);
patrol->setAutoRegenerate(true);
patrol->setMinWaypointDistance(100.0f);

// Update event target position dynamically
if (patrol->getPatrolMode() == PatrolBehavior::PatrolMode::EVENT_TARGET) {
    patrol->updateEventTarget(newTargetPosition);
}
```

## WanderBehavior Modes

WanderBehavior supports multiple wander modes that automatically configure different movement ranges and behaviors. Each mode is optimized for specific NPC roles and movement patterns.

### Available Wander Modes

#### `WanderMode::SMALL_AREA`
**Use Case**: NPCs that should stay in personal/local space
- **Configuration**:
  - Radius: 75px
  - Direction Changes: Every 1.5 seconds
  - Offscreen Probability: 5%
  - Speed: Configurable (default 1.5f)
- **Best For**: Guards at posts, shopkeepers, stationary NPCs

#### `WanderMode::MEDIUM_AREA`
**Use Case**: Standard wandering for most NPCs
- **Configuration**:
  - Radius: 200px
  - Direction Changes: Every 2.5 seconds
  - Offscreen Probability: 10%
  - Speed: Configurable (default 2.0f)
- **Best For**: Villagers, general NPCs, default behavior

#### `WanderMode::LARGE_AREA`
**Use Case**: NPCs that need to cover large areas
- **Configuration**:
  - Radius: 450px
  - Direction Changes: Every 3.5 seconds
  - Offscreen Probability: 20%
  - Speed: Configurable (default 2.5f)
- **Best For**: Merchants, scouts, explorers, roaming NPCs

#### `WanderMode::EVENT_TARGET`
**Use Case**: Wander around specific objectives or targets
- **Configuration**:
  - Radius: 150px around target
  - Direction Changes: Every 2.0 seconds
  - Offscreen Probability: 5%
  - Speed: Configurable (default 2.0f)
- **Best For**: Objective guards, followers, context-sensitive movement

### Registration Examples

```cpp
// Register different wander behavior variants
void setupWanderBehaviors() {
    // Small area wander (local movement)
    auto smallWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 1.5f
    );
    AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));
    
    // Medium area wander (standard)
    auto mediumWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f
    );
    AIManager::Instance().registerBehavior("Wander", std::move(mediumWander));
    
    // Large area wander (roaming)
    auto largeWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 2.5f
    );
    AIManager::Instance().registerBehavior("LargeWander", std::move(largeWander));
    
    // Event target wander
    auto eventWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 2.0f
    );
    AIManager::Instance().registerBehavior("EventWander", std::move(eventWander));
}
```

### NPC Type Assignment Examples

```cpp
std::string getWanderBehaviorForNPC(const std::string& npcType, size_t npcIndex) {
    if (npcType == "Villager") {
        std::vector<std::string> villagerBehaviors = {
            "SmallWander", "Wander"  // Stay local
        };
        return villagerBehaviors[npcIndex % villagerBehaviors.size()];
    }
    else if (npcType == "Merchant") {
        std::vector<std::string> merchantBehaviors = {
            "Wander", "LargeWander"  // Market movement
        };
        return merchantBehaviors[npcIndex % merchantBehaviors.size()];
    }
    else if (npcType == "Guard") {
        std::vector<std::string> guardBehaviors = {
            "SmallWander", "EventWander"  // Post or objective-based
        };
        return guardBehaviors[npcIndex % guardBehaviors.size()];
    }
    return "Wander"; // Default medium area
}
```

## Complete Integration Example

```cpp
class GameState {
public:
    void setupAIBehaviors() {
        // Set up screen dimensions
        float worldWidth = 1280.0f;
        float worldHeight = 720.0f;
        
        // Register patrol behaviors
        auto fixedPatrol = std::make_unique<PatrolBehavior>(
            PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f, true
        );
        fixedPatrol->setScreenDimensions(worldWidth, worldHeight);
        AIManager::Instance().registerBehavior("Patrol", std::move(fixedPatrol));
        
        auto randomPatrol = std::make_unique<PatrolBehavior>(
            PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f, false
        );
        randomPatrol->setScreenDimensions(worldWidth, worldHeight);
        AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));
        
        // Register wander behaviors
        auto smallWander = std::make_unique<WanderBehavior>(
            WanderBehavior::WanderMode::SMALL_AREA, 1.5f
        );
        smallWander->setScreenDimensions(worldWidth, worldHeight);
        AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));
        
        auto largeWander = std::make_unique<WanderBehavior>(
            WanderBehavior::WanderMode::LARGE_AREA, 2.5f
        );
        largeWander->setScreenDimensions(worldWidth, worldHeight);
        AIManager::Instance().registerBehavior("LargeWander", std::move(largeWander));
    }
    
    void createNPC(const std::string& npcType, const Vector2D& position) {
        auto npc = NPC::create(npcType, position);
        
        // Assign behavior based on type and count
        static std::unordered_map<std::string, size_t> npcCounters;
        size_t npcIndex = npcCounters[npcType]++;
        
        std::string behaviorName;
        if (npcType == "Guard") {
            std::vector<std::string> behaviors = {
                "Patrol", "RandomPatrol", "SmallWander", "EventWander"
            };
            behaviorName = behaviors[npcIndex % behaviors.size()];
        }
        else if (npcType == "Villager") {
            std::vector<std::string> behaviors = {
                "SmallWander", "Wander", "RandomPatrol"
            };
            behaviorName = behaviors[npcIndex % behaviors.size()];
        }
        else if (npcType == "Merchant") {
            std::vector<std::string> behaviors = {
                "Wander", "LargeWander", "RandomPatrol"
            };
            behaviorName = behaviors[npcIndex % behaviors.size()];
        }
        
        // Register and assign behavior
        AIManager::Instance().registerEntityForUpdates(npc);
        AIManager::Instance().queueBehaviorAssignment(npc, behaviorName);
    }
};
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
- **Warriors**: Use EVENT_TARGET modes for objective-based movement

### 2. Performance Considerations
- **Mode-based behaviors** have minimal overhead compared to manual configuration
- **Auto-regeneration** adds slight CPU cost but provides dynamic behavior
- **Large area modes** may require more frequent updates for smooth movement

### 3. Configuration Tips
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

### 4. Integration with Game States
- Register behaviors once in state setup
- Use static counters for consistent behavior cycling
- Keep behavior logic in the behavior classes, not in game states
- Use batched assignment for multiple NPCs

## Troubleshooting

### Common Issues

**NPCs not moving in expected areas:**
- Check `setScreenDimensions()` is called with correct values
- Verify mode-specific area calculations are appropriate for your world size

**All NPCs using same behavior:**
- Use static counters per NPC type for proper cycling
- Ensure behavior assignment happens after NPC creation

**Performance issues with large numbers:**
- Consider disabling auto-regeneration for static scenarios
- Use appropriate update frequencies and priorities
- Monitor behavior instance memory usage

### Debug Information
```cpp
// Check current patrol mode
if (auto patrol = std::dynamic_pointer_cast<PatrolBehavior>(behavior)) {
    auto mode = patrol->getPatrolMode();
    // Log or debug mode information
}

// Monitor behavior performance
size_t behaviorCount = AIManager::Instance().getBehaviorCount();
size_t entityCount = AIManager::Instance().getManagedEntityCount();
```

## Migration from Manual Configuration

### Before (Manual Setup)
```cpp
// Old way - manual configuration in game state
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

This mode-based system provides cleaner code, better separation of concerns, and easier maintenance while preserving all the functionality of manual configuration.