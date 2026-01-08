# AI Behavior Quick Reference

**Where to find the code:**
- Implementation: `src/ai/behaviors/`
- Registered and managed by: `src/managers/AIManager.cpp`

**EDM Integration:** All behaviors access entity data through `BehaviorContext` which pre-fetches data from EntityDataManager. Behavior state (paths, timers, targets) persists in EDM between frames. See [AIManager EDM Integration](AIManager.md#entitydatamanager-integration) for details.

## All Available Behaviors

| Behavior | Modes | Complexity | Priority Range | Primary Use |
|----------|-------|------------|----------------|-------------|
| **IdleBehavior** | 4 | Simple | 1-3 | Stationary NPCs |
| **WanderBehavior** | 4 | Simple | 2-4 | Background movement |
| **PatrolBehavior** | 4 | Medium | 5-7 | Route following |
| **ChaseBehavior** | 0 | Medium | 6-8 | Target pursuit |
| **FleeBehavior** | 4 | Medium | 4-6 | Escape behavior |
| **FollowBehavior** | 5 | Complex | 6-8 | Companion AI |
| **GuardBehavior** | 5 | Complex | 7-9 | Area defense |
| **AttackBehavior** | 7 | Complex | 8-9 | Combat AI |

See also:
- [AIManager](AIManager.md) — behavior registration and update flow
- [Behavior Modes](BehaviorModes.md) — detailed configuration per behavior
- [ThreadSystem](../core/ThreadSystem.md) — priorities and batching

## Mode Quick Reference

### IdleBehavior Modes
- `STATIONARY` - No movement (guards on duty)
- `SUBTLE_SWAY` - Gentle swaying (shopkeepers)
- `OCCASIONAL_TURN` - Turn around (lookouts)
- `LIGHT_FIDGET` - Small movements (nervous NPCs)

### WanderBehavior Modes
- `SMALL_AREA` - 75px radius, 1.5s changes (local NPCs)
- `MEDIUM_AREA` - 200px radius, 2.5s changes (general NPCs)
- `LARGE_AREA` - 450px radius, 3.5s changes (roaming NPCs)
- `EVENT_TARGET` - 150px radius, 2.0s changes (objective guards)

### PatrolBehavior Modes
- `FIXED_WAYPOINTS` - User-defined routes (scripted paths)
- `RANDOM_AREA` - 6 waypoints, rectangular area (flexible coverage)
- `CIRCULAR_AREA` - 5 waypoints, circular area (perimeter defense)
- `EVENT_TARGET` - 8 waypoints around target (objective protection)

### FleeBehavior Modes
- `PANIC_FLEE` - 1.2x speed, erratic (civilians)
- `STRATEGIC_RETREAT` - 0.8x speed, calculated (soldiers)
- `EVASIVE_MANEUVER` - 1.0x speed, zigzag (rogues)
- `SEEK_COVER` - 1.0x speed, to safe zones (smart NPCs)

### FollowBehavior Modes
- `CLOSE_FOLLOW` - 50px distance (pets)
- `LOOSE_FOLLOW` - 120px distance (companions)
- `FLANKING_FOLLOW` - 100px distance, side formation (escorts)
- `REAR_GUARD` - 150px distance, behind formation (protectors)
- `ESCORT_FORMATION` - 100px distance, circular formation (guards)

### GuardBehavior Modes
- `STATIC_GUARD` - Stationary (gate guards)
- `PATROL_GUARD` - Waypoint patrol (perimeter guards)
- `AREA_GUARD` - Zone coverage (area security)
- `ROAMING_GUARD` - Free roam (mobile security)
- `ALERT_GUARD` - Enhanced detection (high-security)

### AttackBehavior Modes
- `MELEE_ATTACK` - ≤100px range, close combat
- `RANGED_ATTACK` - ≥200px range, projectile attacks
- `CHARGE_ATTACK` - Rush attack, 2.0x damage
- `AMBUSH_ATTACK` - Stealth strike, 30% crit chance
- `COORDINATED_ATTACK` - Team tactics
- `HIT_AND_RUN` - Strike and retreat
- `BERSERKER_ATTACK` - Continuous assault, combo attacks

## Quick Setup Templates

### Basic Registration
```cpp
void setupAIBehaviors() {
    auto& ai = AIManager::Instance();
    float w = 1280.0f, h = 720.0f;
    
    // Simple behaviors
    ai.registerBehavior("Idle", std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::SUBTLE_SWAY));
    
    auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
    wander->setScreenDimensions(w, h);
    ai.registerBehavior("Wander", wander);
    
    // Complex behaviors
    auto guard = std::make_shared<GuardBehavior>(GuardBehavior::GuardMode::STATIC_GUARD, Vector2D(100,100), 150.0f);
    ai.registerBehavior("Guard", guard);
    
    auto attack = std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::MELEE_ATTACK, 80.0f, 15.0f);
    ai.registerBehavior("Attack", attack);
}
```

### NPC Type Assignment
```cpp
std::string getBehaviorForNPCType(const std::string& type, const std::string& context = "") {
    static std::unordered_map<std::string, size_t> counters;
    size_t idx = counters[type]++;
    
    if (type == "Guard") {
        if (context == "alert") return "AlertGuard";
        std::vector<std::string> behaviors = {"Guard", "Patrol", "RandomPatrol"};
        return behaviors[idx % behaviors.size()];
    }
    else if (type == "Civilian") {
        std::vector<std::string> behaviors = {"Idle", "Wander", "SmallWander"};
        return behaviors[idx % behaviors.size()];
    }
    else if (type == "Enemy") {
        std::vector<std::string> behaviors = {"Chase", "Attack", "Melee"};
        return behaviors[idx % behaviors.size()];
    }
    return "Wander";
}

int getPriorityForNPCType(const std::string& type, const std::string& context = "") {
    if (type == "Enemy") return 8;
    else if (type == "Guard") return (context == "alert") ? 9 : 7;
    else if (type == "Companion") return 6;
    else if (type == "Civilian") return 2;
    return 3;
}
```

### Entity Creation Pattern
```cpp
void createNPC(const std::string& type, const Vector2D& pos, const std::string& context = "") {
    auto npc = std::make_shared<NPC>(type, pos, 64, 64);
    std::string behavior = getBehaviorForNPCType(type, context);
    int priority = getPriorityForNPCType(type, context);
    
    AIManager::Instance().registerEntityForUpdates(npc, priority, behavior);
    m_npcs.push_back(npc);
}
```

## Performance Guidelines

### Entity Limits
- **Simple** (Idle, Wander): 200+ entities
- **Medium** (Patrol, Chase, Flee): 50-100 entities  
- **Complex** (Follow, Guard, Attack): 20-50 entities

### Priority Assignment
- **0-2**: Background NPCs
- **3-5**: Standard NPCs
- **6-8**: Important NPCs
- **9**: Critical NPCs

## Essential Messages

### Common Messages
```cpp
// Global alerts
AIManager::Instance().broadcastMessage("global_alert", false);

// Entity-specific
AIManager::Instance().sendMessageToEntity(entity, "raise_alert", true);
```

### Behavior-Specific Messages
- **Idle**: `"idle_stationary"`, `"idle_sway"`, `"reset_position"`
- **Guard**: `"raise_alert"`, `"clear_alert"`, `"investigate_position"`
- **Follow**: `"follow_close"`, `"follow_loose"`, `"stop_following"`
- **Attack**: `"attack_target"`, `"retreat"`, `"enable_combo"`
- **Flee**: `"panic"`, `"calm_down"`, `"recover_stamina"`

## Setup Checklist

- [ ] Call `AIManager::Instance().init()` at startup
- [ ] Register all behaviors before creating NPCs
- [ ] Set `setPlayerForDistanceOptimization()` with player entity
- [ ] Set screen dimensions on area-based behaviors
- [ ] Use appropriate priorities based on NPC importance
- [ ] Clean up during state transitions with `prepareForStateTransition()`

## Common Patterns

### Behavior Switching
```cpp
void switchBehaviorMode(EntityPtr entity, const std::string& newBehavior) {
    AIManager::Instance().removeEntityFromUpdates(entity);
    int priority = getPriorityForNPCType(entity->getType());
    AIManager::Instance().registerEntityForUpdates(entity, priority, newBehavior);
}
```

### Context-Aware Assignment
```cpp
// Usage examples
createNPC("Guard", Vector2D(100, 100), "patrol");
createNPC("Civilian", Vector2D(200, 200));
createNPC("Enemy", Vector2D(300, 300), "aggressive");
```

For detailed configuration options and advanced usage, see the complete [AI Behavior Modes Documentation](BehaviorModes.md).
