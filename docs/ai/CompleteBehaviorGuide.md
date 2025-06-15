# Complete AI Behavior Guide

## Overview

The Forge Game Engine features a comprehensive AI behavior system with 8 distinct behavior types, each designed for specific use cases. This guide covers all available behaviors, their modes, configuration options, and practical usage examples.

## Behavior Types Summary

| Behavior | Purpose | Complexity | Use Cases |
|----------|---------|------------|-----------|
| IdleBehavior | Minimal/stationary movement | Simple | NPCs at rest, guards on duty |
| WanderBehavior | Random exploration | Simple | Civilians, animals, background NPCs |
| PatrolBehavior | Waypoint-based movement | Medium | Guards, scouts, route following |
| ChaseBehavior | Target pursuit | Medium | Predators, aggressive NPCs |
| FleeBehavior | Escape and avoidance | Medium | Prey animals, panicked civilians |
| FollowBehavior | Target following with formation | Complex | Companions, escorts, formations |
| GuardBehavior | Area defense and threat detection | Complex | Sentries, defenders, alert systems |
| AttackBehavior | Combat and assault | Complex | Warriors, hostile NPCs, combat AI |

## IdleBehavior

### Description
Provides minimal movement for entities that should remain mostly stationary but show signs of life.

### Modes
- **STATIONARY**: Completely still
- **SUBTLE_SWAY**: Small swaying motion
- **OCCASIONAL_TURN**: Turn around occasionally
- **LIGHT_FIDGET**: Small random movements

### Configuration Options
```cpp
auto idle = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::SUBTLE_SWAY, 20.0f);
idle->setMovementFrequency(3.0f);    // Movement every 3 seconds
idle->setTurnFrequency(5.0f);        // Turn every 5 seconds
```

### Use Cases
- Guards at their posts
- Shopkeepers behind counters
- NPCs in conversation
- Stationary background characters

### Messages
- `"idle_stationary"` - Switch to stationary mode
- `"idle_sway"` - Switch to subtle sway
- `"idle_turn"` - Switch to occasional turning
- `"idle_fidget"` - Switch to light fidgeting
- `"reset_position"` - Reset to current position

## WanderBehavior

### Description
Random movement within defined areas with configurable wandering patterns.

### Modes
- **SMALL_AREA**: 75px radius, local movement
- **MEDIUM_AREA**: 200px radius, standard wandering
- **LARGE_AREA**: 450px radius, wide exploration
- **EVENT_TARGET**: 150px around specific target

### Configuration Options
```cpp
auto wander = std::make_shared<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
wander->setAreaRadius(300.0f);
wander->setChangeDirectionInterval(2500.0f);
wander->setOffscreenProbability(0.15f);
wander->setScreenDimensions(1280.0f, 720.0f);
```

### Use Cases
- Village civilians
- Wildlife and animals
- Background NPCs
- Exploration characters

## PatrolBehavior

### Description
Movement between waypoints with various patrol patterns and area coverage options.

### Modes
- **FIXED_WAYPOINTS**: Predefined waypoint sequences
- **RANDOM_AREA**: Dynamic patrol within rectangular area
- **CIRCULAR_AREA**: Patrol around central location
- **EVENT_TARGET**: Patrol around specific objectives

### Configuration Options
```cpp
// Fixed waypoints
std::vector<Vector2D> waypoints = {{0,0}, {100,0}, {100,100}, {0,100}};
auto patrol = std::make_shared<PatrolBehavior>(waypoints, 2.0f);

// Random area mode
auto randomPatrol = std::make_shared<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
randomPatrol->setRandomPatrolArea(Vector2D(0,0), Vector2D(500,400), 6);
randomPatrol->setAutoRegenerate(true);
randomPatrol->setMinWaypointDistance(80.0f);
```

### Use Cases
- Security guards
- Route-based NPCs
- Area coverage
- Perimeter defense

## ChaseBehavior

### Description
Aggressive pursuit of targets with line-of-sight tracking and persistence.

### Configuration Options
```cpp
auto chase = std::make_shared<ChaseBehavior>(3.0f, 500.0f, 50.0f);
chase->setChaseSpeed(4.0f);
chase->setMaxRange(600.0f);
chase->setMinRange(40.0f);
```

### Features
- Line of sight checking
- Last known position tracking
- Maximum chase distance
- Minimum approach distance

### Use Cases
- Predatory animals
- Aggressive NPCs
- Security responses
- Combat initiation

### Messages
- Target automatically obtained from AIManager player reference

## FleeBehavior

### Description
Escape and avoidance behavior with panic states and tactical retreat options.

### Modes
- **PANIC_FLEE**: Fast, erratic escape
- **STRATEGIC_RETREAT**: Calculated withdrawal
- **EVASIVE_MANEUVER**: Zigzag escape pattern
- **SEEK_COVER**: Flee towards safe zones

### Configuration Options
```cpp
auto flee = std::make_shared<FleeBehavior>(FleeBehavior::FleeMode::PANIC_FLEE, 4.0f, 400.0f);
flee->setSafeDistance(600.0f);
flee->setPanicDuration(3.0f);
flee->setStaminaSystem(true, 100.0f, 10.0f);
flee->addSafeZone(Vector2D(100, 100), 50.0f);
flee->setScreenBounds(1280.0f, 720.0f);
```

### Features
- Stamina system
- Safe zone detection
- Boundary avoidance
- Panic duration control

### Use Cases
- Prey animals
- Civilian panic responses
- Tactical retreats
- Cowardly NPCs

### Messages
- `"panic"` - Enter panic state
- `"calm_down"` - Exit panic
- `"stop_fleeing"` - Stop flee behavior
- `"recover_stamina"` - Restore stamina

## FollowBehavior

### Description
Advanced target following with formation support and predictive movement.

### Modes
- **CLOSE_FOLLOW**: Stay very close (50px)
- **LOOSE_FOLLOW**: Maintain distance (120px)
- **FLANKING_FOLLOW**: Follow from sides
- **REAR_GUARD**: Follow from behind
- **ESCORT_FORMATION**: Maintain formation positions

### Configuration Options
```cpp
auto follow = std::make_shared<FollowBehavior>(FollowBehavior::FollowMode::ESCORT_FORMATION, 2.5f);
follow->setFollowDistance(100.0f);
follow->setMaxDistance(300.0f);
follow->setCatchUpSpeed(2.0f);
follow->setPredictiveFollowing(true, 0.5f);
follow->setPathSmoothing(true);
follow->setStopWhenTargetStops(true);
```

### Features
- Formation management
- Predictive following
- Path smoothing
- Catch-up mechanics
- Obstacle avoidance

### Use Cases
- Player companions
- Escort missions
- Formation combat
- Pet following
- Convoy systems

### Messages
- `"follow_close"` - Switch to close following
- `"follow_loose"` - Switch to loose following
- `"follow_flank"` - Switch to flanking
- `"follow_rear"` - Switch to rear guard
- `"follow_formation"` - Switch to formation
- `"stop_following"` - Stop following
- `"reset_formation"` - Reset formation position

## GuardBehavior

### Description
Comprehensive area defense with threat detection, alert systems, and patrol integration.

### Modes
- **STATIC_GUARD**: Stay at assigned position
- **PATROL_GUARD**: Patrol between waypoints
- **AREA_GUARD**: Guard specific area
- **ROAMING_GUARD**: Roam within guard zone
- **ALERT_GUARD**: High alert state

### Alert Levels
- **CALM**: Normal state
- **SUSPICIOUS**: Something detected
- **INVESTIGATING**: Actively searching
- **HOSTILE**: Threat confirmed
- **ALARM**: Maximum alert

### Configuration Options
```cpp
auto guard = std::make_shared<GuardBehavior>(Vector2D(100, 100), 200.0f, 300.0f);
guard->setGuardMode(GuardBehavior::GuardMode::PATROL_GUARD);
guard->setThreatDetectionRange(250.0f);
guard->setFieldOfView(120.0f);
guard->setCanCallForHelp(true);
guard->setHelpCallRadius(500.0f);

// Add patrol waypoints
guard->addPatrolWaypoint(Vector2D(50, 50));
guard->addPatrolWaypoint(Vector2D(150, 50));
guard->addPatrolWaypoint(Vector2D(150, 150));

// Set guard area
guard->setGuardArea(Vector2D(0, 0), 400.0f); // Circular
guard->setGuardArea(Vector2D(0, 0), Vector2D(200, 200)); // Rectangular
```

### Features
- Threat detection system
- Alert level escalation
- Investigation behavior
- Communication system
- Return to post behavior
- Line of sight checking

### Use Cases
- Base security
- Checkpoint guards
- Perimeter defense
- Alert systems
- Investigation scenarios

### Messages
- `"go_on_duty"` / `"go_off_duty"` - Duty state
- `"raise_alert"` / `"clear_alert"` - Alert control
- `"investigate_position"` - Start investigation
- `"return_to_post"` - Return to assigned position
- `"patrol_mode"` / `"static_mode"` / `"roam_mode"` - Mode changes

## AttackBehavior

### Description
Comprehensive combat system with multiple attack modes, combo systems, and tactical options.

### Modes
- **MELEE_ATTACK**: Close combat (≤100px range)
- **RANGED_ATTACK**: Projectile attacks (≥200px range)
- **CHARGE_ATTACK**: Rush attack with damage bonus
- **AMBUSH_ATTACK**: Wait and strike with critical hits
- **COORDINATED_ATTACK**: Team-based attacks
- **HIT_AND_RUN**: Quick strike then retreat
- **BERSERKER_ATTACK**: Continuous aggressive assault

### Attack States
- **SEEKING**: Looking for targets
- **APPROACHING**: Moving towards target
- **POSITIONING**: Getting into attack position
- **ATTACKING**: Executing attack
- **RECOVERING**: Post-attack recovery
- **RETREATING**: Tactical withdrawal
- **COOLDOWN**: Waiting between attacks

### Configuration Options
```cpp
auto attack = std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::MELEE_ATTACK, 80.0f, 15.0f);
attack->setAttackSpeed(1.2f);
attack->setMovementSpeed(2.5f);
attack->setOptimalRange(60.0f);
attack->setMinimumRange(30.0f);

// Combat settings
attack->setCriticalHitChance(0.15f);
attack->setCriticalHitMultiplier(2.0f);
attack->setKnockbackForce(50.0f);
attack->setDamageVariation(0.2f);

// Tactical settings
attack->setAggression(0.8f);
attack->setRetreatThreshold(0.3f);
attack->setCircleStrafe(true, 100.0f);
attack->setFlankingEnabled(true);

// Special abilities
attack->setComboAttacks(true, 3);
attack->setSpecialAttackChance(0.1f);
attack->setAreaOfEffectRadius(60.0f);
```

### Features
- Multiple attack modes
- Combo attack system
- Critical hit mechanics
- Knockback effects
- Circle strafing
- Flanking maneuvers
- Team coordination
- Stamina integration
- Special attacks
- Area of effect damage

### Use Cases
- Combat NPCs
- Boss enemies
- Warrior classes
- Combat encounters
- PvP systems

### Messages
- `"attack_target"` - Begin attack
- `"retreat"` - Tactical retreat
- `"stop_attack"` - End combat
- `"enable_combo"` / `"disable_combo"` - Combo control
- `"heal"` - Restore health
- `"berserk"` - Activate berserk mode

## Integration Examples

### Basic Setup
```cpp
void setupAIBehaviors() {
    auto& aiManager = AIManager::Instance();
    
    // Register basic behaviors
    aiManager.registerBehavior("Idle", std::make_shared<IdleBehavior>());
    aiManager.registerBehavior("Wander", std::make_shared<WanderBehavior>());
    aiManager.registerBehavior("Chase", std::make_shared<ChaseBehavior>());
    aiManager.registerBehavior("Flee", std::make_shared<FleeBehavior>());
    aiManager.registerBehavior("Follow", std::make_shared<FollowBehavior>());
    
    // Register advanced behaviors
    std::vector<Vector2D> guardWaypoints = {{100,100}, {200,100}, {200,200}, {100,200}};
    aiManager.registerBehavior("Patrol", std::make_shared<PatrolBehavior>(guardWaypoints));
    
    Vector2D guardPos(150, 150);
    aiManager.registerBehavior("Guard", std::make_shared<GuardBehavior>(guardPos));
    aiManager.registerBehavior("Attack", std::make_shared<AttackBehavior>());
}
```

### NPC Type Configuration
```cpp
void createNPC(const std::string& npcType, const Vector2D& position) {
    auto npc = std::make_shared<Entity>(position);
    
    std::string behaviorName;
    int priority;
    
    if (npcType == "Civilian") {
        behaviorName = "Wander";
        priority = 2;
    } else if (npcType == "Guard") {
        behaviorName = "Guard";
        priority = 7;
    } else if (npcType == "Warrior") {
        behaviorName = "Attack";
        priority = 8;
    } else if (npcType == "Companion") {
        behaviorName = "Follow";
        priority = 6;
    }
    
    AIManager::Instance().registerEntityForUpdates(npc, priority, behaviorName);
}
```

### Advanced Configuration Presets
```cpp
// Combat preset
std::shared_ptr<AttackBehavior> createWarriorPreset() {
    auto attack = std::make_shared<AttackBehavior>(AttackBehavior::AttackMode::MELEE_ATTACK, 60.0f, 20.0f);
    attack->setAggression(0.9f);
    attack->setComboAttacks(true, 4);
    attack->setCriticalHitChance(0.2f);
    attack->setFlankingEnabled(true);
    return attack;
}

// Guard preset
std::shared_ptr<GuardBehavior> createSentryPreset(const Vector2D& position) {
    auto guard = std::make_shared<GuardBehavior>(position, 150.0f, 200.0f);
    guard->setGuardMode(GuardBehavior::GuardMode::STATIC_GUARD);
    guard->setThreatDetectionRange(180.0f);
    guard->setFieldOfView(180.0f);
    guard->setCanCallForHelp(true);
    return guard;
}

// Companion preset
std::shared_ptr<FollowBehavior> createCompanionPreset() {
    auto follow = std::make_shared<FollowBehavior>(FollowBehavior::FollowMode::CLOSE_FOLLOW, 3.0f);
    follow->setFollowDistance(80.0f);
    follow->setCatchUpSpeed(2.0f);
    follow->setPredictiveFollowing(true, 0.8f);
    follow->setPathSmoothing(true);
    return follow;
}
```

### Dynamic Behavior Switching
```cpp
void switchBehaviorBasedOnState(EntityPtr entity, const std::string& currentState) {
    auto& aiManager = AIManager::Instance();
    
    if (currentState == "peaceful") {
        aiManager.assignBehaviorToEntity(entity, "Wander");
    } else if (currentState == "alert") {
        aiManager.assignBehaviorToEntity(entity, "Guard");
    } else if (currentState == "combat") {
        aiManager.assignBehaviorToEntity(entity, "Attack");
    } else if (currentState == "fleeing") {
        aiManager.assignBehaviorToEntity(entity, "Flee");
    }
}
```

## Performance Considerations

### Priority Assignment Guidelines
- **Priority 0-2**: Background NPCs, civilians (1.0x-1.2x update range)
- **Priority 3-5**: Standard NPCs, merchants (1.3x-1.5x update range)  
- **Priority 6-8**: Important NPCs, guards, companions (1.6x-1.8x update range)
- **Priority 9**: Critical NPCs, bosses, combat entities (1.9x update range)

### Behavior Complexity Impact
- **Simple** (Idle, Wander): Minimal CPU impact, suitable for large numbers
- **Medium** (Patrol, Chase, Flee): Moderate CPU, good for standard NPCs
- **Complex** (Follow, Guard, Attack): Higher CPU, use sparingly or with lower priorities

### Memory Usage
- Each behavior maintains per-entity state data
- Complex behaviors use more memory per entity
- Consider pooling or limiting complex behaviors for performance

### Threading Integration
- All behaviors are thread-safe and work with AIManager's threading system
- Performance scales with available CPU cores
- Large numbers of entities benefit from multi-threading

## Best Practices

### 1. Behavior Selection
- Match behavior complexity to NPC importance
- Use simple behaviors for background NPCs
- Reserve complex behaviors for key characters

### 2. Configuration
- Always set screen dimensions for area-based behaviors
- Use appropriate speeds for each behavior type
- Configure detection ranges based on gameplay needs

### 3. Message Handling
- Use messages for dynamic behavior control
- Implement state machines for complex behavior switching
- Handle edge cases in message processing

### 4. Performance Optimization
- Assign appropriate priorities based on NPC importance
- Limit the number of high-priority complex behaviors
- Use distance-based updates for background NPCs

### 5. Debugging
- Enable AI_DEBUG_LOGGING for development
- Monitor performance stats through AIManager
- Test behavior transitions thoroughly

This comprehensive behavior system provides the foundation for sophisticated AI in the Forge Game Engine, supporting everything from simple background NPCs to complex combat encounters and formation-based gameplay.