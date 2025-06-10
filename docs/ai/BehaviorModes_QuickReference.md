# AI Behavior Modes - Quick Reference

## PatrolBehavior Modes

### Registration
```cpp
// Fixed waypoints (traditional)
auto patrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f);
patrol->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("Patrol", std::move(patrol));

// Random rectangular area
auto randomPatrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
randomPatrol->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));

// Circular area
auto circlePatrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f);
circlePatrol->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("CirclePatrol", std::move(circlePatrol));

// Event target
auto eventPatrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f);
eventPatrol->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("EventTarget", std::move(eventPatrol));
```

### Mode Specifications
| Mode | Area | Waypoints | Auto-Regen | Location |
|------|------|-----------|------------|----------|
| FIXED_WAYPOINTS | User-defined | Manual | No | Anywhere |
| RANDOM_AREA | 40% left screen | 6 random | Yes | Left side |
| CIRCULAR_AREA | 120px radius | 5 random | Yes | Right 75% |
| EVENT_TARGET | 150px radius | 8 around target | No | Screen center |

## WanderBehavior Modes

### Registration
```cpp
// Small area (75px radius)
auto smallWander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 1.5f);
smallWander->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));

// Medium area (200px radius) - default
auto wander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
wander->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("Wander", std::move(wander));

// Large area (450px radius)
auto largeWander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::LARGE_AREA, 2.5f);
largeWander->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("LargeWander", std::move(largeWander));

// Event target (150px radius)
auto eventWander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::EVENT_TARGET, 2.0f);
eventWander->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("EventWander", std::move(eventWander));
```

### Mode Specifications
| Mode | Radius | Change Freq | Offscreen % | Best For |
|------|--------|-------------|-------------|----------|
| SMALL_AREA | 75px | 1.5s | 5% | Guards at posts |
| MEDIUM_AREA | 200px | 2.5s | 10% | General NPCs |
| LARGE_AREA | 450px | 3.5s | 20% | Merchants, scouts |
| EVENT_TARGET | 150px | 2.0s | 5% | Objective guards |

## NPC Type Assignments

### Guards
```cpp
std::vector<std::string> guardBehaviors = {
    "Patrol", "RandomPatrol", "CirclePatrol", "SmallWander", "EventTarget"
};
```

### Villagers
```cpp
std::vector<std::string> villagerBehaviors = {
    "SmallWander", "Wander", "RandomPatrol"
};
```

### Merchants
```cpp
std::vector<std::string> merchantBehaviors = {
    "Wander", "LargeWander", "RandomPatrol", "CirclePatrol"
};
```

## Complete Setup Example

```cpp
void setupAIBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;
    
    // Register patrol behaviors
    auto patrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f);
    patrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Patrol", std::move(patrol));
    
    auto randomPatrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
    randomPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));
    
    // Register wander behaviors
    auto smallWander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 1.5f);
    smallWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));
    
    auto wander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
    wander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Wander", std::move(wander));
}

std::string determineBehaviorForNPCType(const std::string& npcType) {
    static std::unordered_map<std::string, size_t> counters;
    size_t index = counters[npcType]++;
    
    if (npcType == "Guard") {
        std::vector<std::string> behaviors = {"Patrol", "RandomPatrol", "SmallWander"};
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Villager") {
        std::vector<std::string> behaviors = {"SmallWander", "Wander"};
        return behaviors[index % behaviors.size()];
    }
    return "Wander";
}
```

## Entity Registration Pattern

```cpp
void createNPC(const std::string& npcType, const Vector2D& position) {
    auto npc = std::make_shared<NPC>(npcType, position, 64, 64);
    
    // Get behavior and priority for this NPC type
    std::string behaviorName = determineBehaviorForNPCType(npcType);
    int priority = getPriorityForNPCType(npcType);
    
    // Single call for registration and assignment
    AIManager::Instance().registerEntityForUpdates(npc, priority, behaviorName);
}

int getPriorityForNPCType(const std::string& npcType) {
    if (npcType == "Guard") return 7;
    else if (npcType == "Merchant") return 5;
    else if (npcType == "Villager") return 2;
    return 3; // Default
}
```

## Essential Setup Checklist

- [ ] Call `AIManager::Instance().init()` at engine startup
- [ ] Set screen dimensions on all behaviors with `setScreenDimensions()`
- [ ] Register behaviors once during game state setup
- [ ] Use static counters for behavior cycling per NPC type
- [ ] Set player reference with `setPlayerForDistanceOptimization()`
- [ ] Clean up entities properly during state transitions

## Performance Notes

- Mode-based behaviors have minimal overhead vs manual setup
- Auto-regeneration adds slight CPU cost for dynamic behavior
- Large area modes may need more frequent updates
- Each NPC gets independent behavior instance (no sharing conflicts)
- Threading automatically enabled when available (200+ entities)