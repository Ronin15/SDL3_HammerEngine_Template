# AI Behavior Modes - Quick Reference

## All Available Behaviors

| Behavior | Complexity | Primary Use | Default Priority |
|----------|------------|-------------|------------------|
| IdleBehavior | Simple | Stationary NPCs | 1-3 |
| WanderBehavior | Simple | Background movement | 2-4 |
| PatrolBehavior | Medium | Route following | 5-7 |
| ChaseBehavior | Medium | Target pursuit | 6-8 |
| FleeBehavior | Medium | Escape behavior | 4-6 |
| FollowBehavior | Complex | Companion AI | 6-8 |
| GuardBehavior | Complex | Area defense | 7-9 |
| AttackBehavior | Complex | Combat AI | 8-9 |

## Quick Registration Examples

### Basic Behaviors
```cpp
// Idle - minimal movement
auto idle = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::SUBTLE_SWAY, 20.0f);
AIManager::Instance().registerBehavior("Idle", idle);

// Wander - random movement
auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
wander->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("Wander", wander);

// Chase - target pursuit
auto chase = std::make_shared<ChaseBehavior>(3.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("Chase", chase);

// Flee - escape behavior
auto flee = std::make_shared<FleeBehavior>(FleeBehavior::FleeMode::PANIC_FLEE, 4.0f, 400.0f);
flee->setScreenBounds(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("Flee", flee);
```

### Advanced Behaviors
```cpp
// Patrol - waypoint movement
std::vector<Vector2D> waypoints = {{100,100}, {200,100}, {200,200}, {100,200}};
auto patrol = std::make_shared<PatrolBehavior>(waypoints, 2.0f);
patrol->setScreenDimensions(1280.0f, 720.0f);
AIManager::Instance().registerBehavior("Patrol", patrol);

// Follow - companion AI
auto follow = std::make_shared<FollowBehavior>(FollowBehavior::FollowMode::LOOSE_FOLLOW, 2.5f);
AIManager::Instance().registerBehavior("Follow", follow);

// Guard - area defense
auto guard = std::make_shared<GuardBehavior>(Vector2D(150, 150), 200.0f, 300.0f);
guard->setThreatDetectionRange(250.0f);
AIManager::Instance().registerBehavior("Guard", guard);

// Attack - combat AI
auto attack = std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::MELEE_ATTACK, 80.0f, 15.0f);
attack->setAggression(0.8f);
AIManager::Instance().registerBehavior("Attack", attack);
```

## Behavior Modes Summary

### IdleBehavior Modes
| Mode | Movement | Use Case |
|------|----------|----------|
| STATIONARY | None | Guards on duty |
| SUBTLE_SWAY | Gentle sway | Shopkeepers |
| OCCASIONAL_TURN | Turn around | Lookouts |
| LIGHT_FIDGET | Small movements | Nervous NPCs |

### WanderBehavior Modes
| Mode | Radius | Change Freq | Best For |
|------|--------|-------------|----------|
| SMALL_AREA | 75px | 1.5s | Local NPCs |
| MEDIUM_AREA | 200px | 2.5s | General NPCs |
| LARGE_AREA | 450px | 3.5s | Roaming NPCs |
| EVENT_TARGET | 150px | 2.0s | Objective guards |

### PatrolBehavior Modes
| Mode | Area Type | Waypoints | Auto-Regen |
|------|-----------|-----------|------------|
| FIXED_WAYPOINTS | Manual | User-defined | No |
| RANDOM_AREA | Rectangle | 6 random | Yes |
| CIRCULAR_AREA | Circle | 5 random | Yes |
| EVENT_TARGET | Circle | 8 around target | No |

### FleeBehavior Modes
| Mode | Pattern | Speed | Use Case |
|------|---------|-------|----------|
| PANIC_FLEE | Erratic | 1.2x | Civilians |
| STRATEGIC_RETREAT | Calculated | 0.8x | Soldiers |
| EVASIVE_MANEUVER | Zigzag | 1.0x | Rogues |
| SEEK_COVER | To safe zones | 1.0x | Smart NPCs |

### FollowBehavior Modes
| Mode | Distance | Formation | Use Case |
|------|----------|-----------|----------|
| CLOSE_FOLLOW | 50px | None | Pets |
| LOOSE_FOLLOW | 120px | None | Companions |
| FLANKING_FOLLOW | 100px | Side | Escorts |
| REAR_GUARD | 150px | Behind | Protectors |
| ESCORT_FORMATION | 100px | Around | Guards |

### GuardBehavior Modes
| Mode | Movement | Detection | Alert |
|------|----------|-----------|--------|
| STATIC_GUARD | Stationary | Standard | Normal |
| PATROL_GUARD | Waypoints | Enhanced | Normal |
| AREA_GUARD | Area roam | Standard | Normal |
| ROAMING_GUARD | Free roam | Standard | Normal |
| ALERT_GUARD | Aggressive | Enhanced | High |

### AttackBehavior Modes
| Mode | Range | Style | Damage Bonus |
|------|-------|-------|--------------|
| MELEE_ATTACK | ≤100px | Close combat | 1.0x |
| RANGED_ATTACK | ≥200px | Projectile | 1.0x |
| CHARGE_ATTACK | Variable | Rush | 2.0x |
| AMBUSH_ATTACK | 60px | Stealth | 1.5x + crit |
| COORDINATED_ATTACK | Standard | Team | 1.0x |
| HIT_AND_RUN | Standard | Strike & flee | 1.0x |
| BERSERKER_ATTACK | Standard | Continuous | 1.2x |

## NPC Type Assignments

### Civilian NPCs
```cpp
std::vector<std::string> civilianBehaviors = {
    "Idle", "Wander", "SmallWander"
};
// Priority: 1-3
```

### Guards & Security
```cpp
std::vector<std::string> guardBehaviors = {
    "Guard", "Patrol", "RandomPatrol", "Alert"
};
// Priority: 7-8
```

### Companions & Followers
```cpp
std::vector<std::string> companionBehaviors = {
    "Follow", "FollowClose", "FollowFormation"
};
// Priority: 6-7
```

### Combat NPCs
```cpp
std::vector<std::string> combatBehaviors = {
    "Attack", "AttackMelee", "AttackRanged", "Chase"
};
// Priority: 8-9
```

### Animals & Wildlife
```cpp
std::vector<std::string> animalBehaviors = {
    "Wander", "Flee", "Chase" // Predator vs Prey
};
// Priority: 3-5
```

## Complete Setup Function

```cpp
void setupAllAIBehaviors() {
    auto& aiManager = AIManager::Instance();
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;
    
    // Basic behaviors
    aiManager.registerBehavior("Idle", 
        std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::STATIONARY));
    
    auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
    wander->setScreenDimensions(worldWidth, worldHeight);
    aiManager.registerBehavior("Wander", wander);
    
    aiManager.registerBehavior("Chase", 
        std::make_shared<ChaseBehavior>(3.0f, 500.0f, 50.0f));
    
    auto flee = std::make_shared<FleeBehavior>(FleeBehavior::FleeMode::PANIC_FLEE, 4.0f, 400.0f);
    flee->setScreenBounds(worldWidth, worldHeight);
    aiManager.registerBehavior("Flee", flee);
    
    // Advanced behaviors
    std::vector<Vector2D> defaultWaypoints = {{100,100}, {200,100}, {200,200}, {100,200}};
    auto patrol = std::make_shared<PatrolBehavior>(defaultWaypoints, 2.0f);
    patrol->setScreenDimensions(worldWidth, worldHeight);
    aiManager.registerBehavior("Patrol", patrol);
    
    aiManager.registerBehavior("Follow", 
        std::make_shared<FollowBehavior>(FollowBehavior::FollowMode::LOOSE_FOLLOW, 2.5f));
    
    auto guard = std::make_shared<GuardBehavior>(Vector2D(150, 150), 200.0f, 300.0f);
    guard->setThreatDetectionRange(250.0f);
    aiManager.registerBehavior("Guard", guard);
    
    auto attack = std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::MELEE_ATTACK, 80.0f, 15.0f);
    attack->setAggression(0.8f);
    aiManager.registerBehavior("Attack", attack);
    
    // Behavior variants
    aiManager.registerBehavior("IdleFidget", 
        std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::LIGHT_FIDGET, 30.0f));
    
    auto smallWander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::SMALL_AREA, 1.5f);
    smallWander->setScreenDimensions(worldWidth, worldHeight);
    aiManager.registerBehavior("SmallWander", smallWander);
    
    auto randomPatrol = std::make_shared<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
    randomPatrol->setScreenDimensions(worldWidth, worldHeight);
    aiManager.registerBehavior("RandomPatrol", randomPatrol);
    
    aiManager.registerBehavior("FollowClose", 
        std::make_shared<FollowBehavior>(FollowBehavior::FollowMode::CLOSE_FOLLOW, 3.0f));
    
    auto alertGuard = std::make_shared<GuardBehavior>(GuardBehavior::GuardMode::ALERT_GUARD, Vector2D(0,0), 200.0f);
    aiManager.registerBehavior("AlertGuard", alertGuard);
    
    aiManager.registerBehavior("AttackRanged", 
        std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::RANGED_ATTACK, 250.0f, 12.0f));
}
```

## Smart NPC Assignment

```cpp
std::string getBehaviorForNPCType(const std::string& npcType, const std::string& context = "") {
    static std::unordered_map<std::string, size_t> counters;
    size_t index = counters[npcType]++;
    
    if (npcType == "Guard") {
        if (context == "alert") return "AlertGuard";
        if (context == "patrol") return "RandomPatrol";
        std::vector<std::string> behaviors = {"Guard", "Patrol", "RandomPatrol"};
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Civilian") {
        std::vector<std::string> behaviors = {"Idle", "Wander", "SmallWander"};
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Companion") {
        if (context == "combat") return "FollowClose";
        std::vector<std::string> behaviors = {"Follow", "FollowClose"};
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Enemy") {
        if (context == "aggressive") return "Attack";
        if (context == "defensive") return "Guard";
        std::vector<std::string> behaviors = {"Chase", "Attack"};
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Animal") {
        if (context == "predator") return "Chase";
        if (context == "prey") return "Flee";
        return "Wander";
    }
    
    return "Wander"; // Default fallback
}

int getPriorityForNPCType(const std::string& npcType, const std::string& context = "") {
    if (npcType == "Guard") return (context == "alert") ? 9 : 7;
    else if (npcType == "Enemy") return 8;
    else if (npcType == "Companion") return 6;
    else if (npcType == "Civilian") return 2;
    else if (npcType == "Animal") return 3;
    return 3; // Default
}
```

## Entity Creation Pattern

```cpp
void createNPC(const std::string& npcType, const Vector2D& position, const std::string& context = "") {
    auto npc = std::make_shared<NPC>(npcType, position, 64, 64);
    
    std::string behaviorName = getBehaviorForNPCType(npcType, context);
    int priority = getPriorityForNPCType(npcType, context);
    
    // Single call for registration and assignment
    AIManager::Instance().registerEntityForUpdates(npc, priority, behaviorName);
    
    // Store reference
    m_npcs.push_back(npc);
}

// Usage examples:
createNPC("Guard", Vector2D(100, 100), "patrol");
createNPC("Civilian", Vector2D(200, 200));
createNPC("Companion", Vector2D(50, 50), "combat");
createNPC("Enemy", Vector2D(300, 300), "aggressive");
createNPC("Animal", Vector2D(150, 300), "prey");
```

## Performance Guidelines

### Priority Assignment
- **0-2**: Background, low-importance NPCs
- **3-5**: Standard NPCs, animals, merchants
- **6-8**: Important NPCs, companions, guards
- **9**: Critical NPCs, bosses, active threats

### Behavior Complexity Limits
- **Simple (Idle, Wander)**: Unlimited
- **Medium (Patrol, Chase, Flee)**: 100-200 entities
- **Complex (Follow, Guard, Attack)**: 20-50 entities

### Essential Setup Checklist
- [ ] Call `AIManager::Instance().init()` at startup
- [ ] Set `setPlayerForDistanceOptimization()` with player entity
- [ ] Set screen dimensions on area-based behaviors
- [ ] Register all behaviors before creating NPCs
- [ ] Use appropriate priorities based on NPC importance
- [ ] Clean up during state transitions with `prepareForStateTransition()`

## Message System Quick Reference

### Global Messages
```cpp
// Broadcast to all entities
AIManager::Instance().broadcastMessage("global_alert", false);
AIManager::Instance().broadcastMessage("combat_start", true); // immediate
```

### Entity-Specific Messages
```cpp
// Send to specific entity
AIManager::Instance().sendMessageToEntity(npcEntity, "investigate_position", false);
AIManager::Instance().sendMessageToEntity(guard, "raise_alert", true); // immediate
```

### Common Messages by Behavior
- **Idle**: `"idle_stationary"`, `"idle_sway"`, `"reset_position"`
- **Guard**: `"raise_alert"`, `"clear_alert"`, `"investigate_position"`
- **Follow**: `"follow_close"`, `"follow_loose"`, `"stop_following"`
- **Attack**: `"attack_target"`, `"retreat"`, `"enable_combo"`
- **Flee**: `"panic"`, `"calm_down"`, `"recover_stamina"`

This quick reference provides immediate access to all behavior configuration options and common usage patterns for the Forge Game Engine AI system.