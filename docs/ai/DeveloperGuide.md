# AI Behavior Modes - Developer Integration Guide

## Quick Setup for Game States

### 1. Basic AI Behavior Registration

```cpp
void GameState::setupAIBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;
    
    // Register patrol behaviors
    auto fixedPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f, true
    );
    fixedPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Patrol", std::move(fixedPatrol));
    
    auto randomPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f
    );
    randomPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("RandomPatrol", std::move(randomPatrol));
    
    auto circlePatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f
    );
    circlePatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("CirclePatrol", std::move(circlePatrol));
    
    auto eventPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f
    );
    eventPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("EventTarget", std::move(eventPatrol));
    
    // Register wander behaviors
    auto smallWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 1.5f
    );
    smallWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("SmallWander", std::move(smallWander));
    
    auto mediumWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f
    );
    mediumWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Wander", std::move(mediumWander));
    
    auto largeWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 2.5f
    );
    largeWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("LargeWander", std::move(largeWander));
    
    auto eventWander = std::make_unique<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 2.0f
    );
    eventWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("EventWander", std::move(eventWander));
}
```

### 2. NPC Behavior Assignment

```cpp
std::string GameState::determineBehaviorForNPCType(const std::string& npcType) {
    static std::unordered_map<std::string, size_t> npcTypeCounters;
    size_t npcCount = npcTypeCounters[npcType]++;
    
    if (npcType == "Guard") {
        std::vector<std::string> guardBehaviors = {
            "Patrol", "RandomPatrol", "CirclePatrol", "SmallWander", "EventTarget"
        };
        return guardBehaviors[npcCount % guardBehaviors.size()];
    }
    else if (npcType == "Villager") {
        std::vector<std::string> villagerBehaviors = {
            "SmallWander", "Wander", "RandomPatrol", "CirclePatrol"
        };
        return villagerBehaviors[npcCount % villagerBehaviors.size()];
    }
    else if (npcType == "Merchant") {
        std::vector<std::string> merchantBehaviors = {
            "Wander", "LargeWander", "RandomPatrol", "CirclePatrol"
        };
        return merchantBehaviors[npcCount % merchantBehaviors.size()];
    }
    else if (npcType == "Warrior") {
        std::vector<std::string> warriorBehaviors = {
            "EventWander", "EventTarget", "LargeWander", "Chase"
        };
        return warriorBehaviors[npcCount % warriorBehaviors.size()];
    }
    
    return "Wander"; // Default
}
```

### 3. NPC Creation and Assignment

```cpp
void GameState::createNPCAtPosition(const std::string& npcType, float x, float y) {
    // Create NPC
    Vector2D position(x, y);
    auto npc = std::make_shared<NPC>(getTextureID(npcType), position, 64, 64);
    
    if (npc) {
        // Configure NPC properties
        npc->setWanderArea(0.0f, 0.0f, worldWidth, worldHeight);
        npc->setBoundsCheckEnabled(false);
        
        // Get behavior for this NPC type
        std::string behaviorName = determineBehaviorForNPCType(npcType);
        
        // Set priority based on NPC type
        int priority = getPriorityForNPCType(npcType);
        
        // Register with AIManager
        AIManager::Instance().registerEntityForUpdates(npc, priority);
        AIManager::Instance().queueBehaviorAssignment(npc, behaviorName);
        
        // Track the NPC
        m_spawnedNPCs.push_back(npc);
    }
}

private:
std::string getTextureID(const std::string& npcType) {
    if (npcType == "Guard") return "guard";
    else if (npcType == "Villager") return "villager";
    else if (npcType == "Merchant") return "merchant";
    else if (npcType == "Warrior") return "warrior";
    return "npc";
}

int getPriorityForNPCType(const std::string& npcType) {
    if (npcType == "Guard") return 7;
    else if (npcType == "Warrior") return 8;
    else if (npcType == "Merchant") return 5;
    return 2; // Villagers
}
```

## Common Integration Patterns

### Pattern 1: Event-Driven NPC Spawning

```cpp
void GameState::onNPCSpawnEvent(const std::string& npcType) {
    // Calculate spawn position
    Vector2D playerPos = getPlayerPosition();
    float spawnX = playerPos.getX() + getRandomOffset();
    float spawnY = playerPos.getY() + getRandomOffset();
    
    // Create NPC with automatic behavior assignment
    createNPCAtPosition(npcType, spawnX, spawnY);
}
```

### Pattern 2: Batch NPC Creation

```cpp
void GameState::createNPCGroup(const std::string& npcType, int count, const Vector2D& centerPos) {
    for (int i = 0; i < count; ++i) {
        Vector2D offset = generateRandomOffset(i);
        Vector2D spawnPos = centerPos + offset;
        createNPCAtPosition(npcType, spawnPos.getX(), spawnPos.getY());
    }
    
    // All behavior assignments are queued and processed automatically
}
```

### Pattern 3: Dynamic Behavior Switching

```cpp
void GameState::switchNPCBehavior(std::shared_ptr<NPC> npc, const std::string& newBehaviorName) {
    // Unassign current behavior
    AIManager::Instance().unassignBehaviorFromEntity(npc);
    
    // Assign new behavior
    AIManager::Instance().queueBehaviorAssignment(npc, newBehaviorName);
}
```

## Advanced Usage

### Custom Mode Configuration

```cpp
void GameState::setupCustomPatrolBehavior() {
    // Create custom patrol with specific settings
    auto customPatrol = std::make_unique<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 3.0f
    );
    
    // Override default settings
    customPatrol->setScreenDimensions(2560.0f, 1440.0f); // 4K screen
    customPatrol->setAutoRegenerate(false); // Static waypoints
    customPatrol->setMinWaypointDistance(120.0f); // Larger spacing
    
    AIManager::Instance().registerBehavior("CustomPatrol", std::move(customPatrol));
}
```

### Event Target Updates

```cpp
void GameState::updateEventTargets() {
    Vector2D newTarget = getObjectivePosition();
    
    // Update all event target behaviors
    if (AIManager::Instance().hasBehavior("EventTarget")) {
        auto eventBehavior = std::dynamic_pointer_cast<PatrolBehavior>(
            AIManager::Instance().getBehavior("EventTarget")
        );
        if (eventBehavior) {
            eventBehavior->updateEventTarget(newTarget);
        }
    }
}
```

### Performance Monitoring

```cpp
void GameState::checkAIPerformance() {
    size_t entityCount = AIManager::Instance().getManagedEntityCount();
    size_t behaviorCount = AIManager::Instance().getBehaviorCount();
    
    if (entityCount > 1000) {
        // Consider reducing update frequencies for distant NPCs
        AIManager::Instance().setDistanceBasedUpdates(true);
    }
    
    if (behaviorCount > 20) {
        // Log warning about too many behavior types
        std::cout << "Warning: " << behaviorCount << " behavior types registered" << std::endl;
    }
}
```

## Error Handling

### Safe NPC Creation

```cpp
bool GameState::safeCreateNPC(const std::string& npcType, const Vector2D& position) {
    try {
        // Validate position
        if (!isValidPosition(position)) {
            std::cerr << "Invalid spawn position for " << npcType << std::endl;
            return false;
        }
        
        // Check entity limits
        if (m_spawnedNPCs.size() >= MAX_NPCS) {
            std::cerr << "Maximum NPC limit reached" << std::endl;
            return false;
        }
        
        // Create NPC
        createNPCAtPosition(npcType, position.getX(), position.getY());
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error creating NPC: " << e.what() << std::endl;
        return false;
    }
}
```

### Behavior Registration Validation

```cpp
bool GameState::validateBehaviorRegistration() {
    std::vector<std::string> requiredBehaviors = {
        "Patrol", "RandomPatrol", "CirclePatrol", "EventTarget",
        "SmallWander", "Wander", "LargeWander", "EventWander"
    };
    
    for (const auto& behaviorName : requiredBehaviors) {
        if (!AIManager::Instance().hasBehavior(behaviorName)) {
            std::cerr << "Missing required behavior: " << behaviorName << std::endl;
            return false;
        }
    }
    
    return true;
}
```

## Cleanup and Shutdown

### Proper Resource Cleanup

```cpp
void GameState::cleanupAI() {
    // Clean up NPCs
    for (auto& npc : m_spawnedNPCs) {
        if (npc) {
            AIManager::Instance().unassignBehaviorFromEntity(npc);
            AIManager::Instance().unregisterEntityFromUpdates(npc);
        }
    }
    m_spawnedNPCs.clear();
    
    // Note: Don't unregister behaviors - they may be used by other states
}

GameState::~GameState() {
    cleanupAI();
}
```

## Testing Integration

### Behavior Verification

```cpp
void GameState::testBehaviorAssignment() {
    // Create test NPCs
    for (int i = 0; i < 10; ++i) {
        createNPCAtPosition("Guard", 100.0f + i * 50.0f, 100.0f);
    }
    
    // Wait for assignments to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify different behaviors were assigned
    std::set<std::string> assignedBehaviors;
    for (const auto& npc : m_spawnedNPCs) {
        if (AIManager::Instance().entityHasBehavior(npc)) {
            // Track which behaviors were assigned
            assignedBehaviors.insert("assigned"); // Implementation dependent
        }
    }
    
    // Should have multiple different behaviors
    assert(assignedBehaviors.size() > 1);
}
```

This guide provides all the essential patterns for integrating AI behavior modes into your game states while maintaining clean architecture and proper resource management.