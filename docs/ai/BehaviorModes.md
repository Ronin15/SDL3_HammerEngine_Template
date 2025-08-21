# AI Behavior Modes Documentation

**Where to find the code:**
- Implementation: `src/ai/behaviors/`
- Registered and managed by: `src/managers/AIManager.cpp`

## Overview

The Hammer Game Engine AI system includes comprehensive mode-based behavior configuration for all AI behaviors. These modes provide automatic setup for common patterns, eliminating manual configuration while ensuring consistent behavior across different NPC types. The system supports 8 distinct behavior types, each with multiple modes for specialized use cases.

See also:
- [AIManager](AIManager.md) — overview, architecture, and performance
- [ThreadSystem](../core/ThreadSystem.md) — priorities and WorkerBudget
- [GameEngine](../core/GameEngine.md) — update integration and buffering

## Complete Behavior System

| Behavior | Purpose | Complexity | Modes Available |
|----------|---------|------------|-----------------|
| IdleBehavior | Minimal/stationary movement | Simple | 4 modes |
| WanderBehavior | Random exploration | Simple | 4 modes |
| PatrolBehavior | Waypoint-based movement | Medium | 4 modes |
| ChaseBehavior | Target pursuit | Medium | No modes (single config) |
| FleeBehavior | Escape and avoidance | Medium | 4 modes |
| FollowBehavior | Target following with formation | Complex | 5 modes |
| GuardBehavior | Area defense and threat detection | Complex | 5 modes |
| AttackBehavior | Combat and assault | Complex | 7 modes |

## IdleBehavior Modes

IdleBehavior provides minimal movement for entities that should remain mostly stationary but show signs of life.

### Available Modes

#### `IdleMode::STATIONARY`
**Use Case**: Completely motionless NPCs
- **Configuration**:
  - Movement Frequency: 0.0f (no movement)
  - Turn Frequency: 0.0f (no turning)
- **Best For**: Statue-like NPCs, sleeping characters, disabled entities

#### `IdleMode::SUBTLE_SWAY`
**Use Case**: Gentle life-like movement
- **Configuration**:
  - Movement Frequency: 2.0f seconds (gentle swaying)
  - Turn Frequency: 8.0f seconds (occasional turns)
  - Radius: 20px default
- **Best For**: Shopkeepers, NPCs in conversation, relaxed guards

#### `IdleMode::OCCASIONAL_TURN`
**Use Case**: Alert but stationary NPCs
- **Configuration**:
  - Movement Frequency: 0.0f (no position movement)
  - Turn Frequency: 4.0f seconds (regular turns)
- **Best For**: Lookouts, sentries, alert guards at posts

#### `IdleMode::LIGHT_FIDGET`
**Use Case**: Nervous or restless NPCs
- **Configuration**:
  - Movement Frequency: 1.5f seconds (light fidgeting)
  - Turn Frequency: 3.0f seconds (frequent turns)
  - Radius: 20px default
- **Best For**: Nervous NPCs, anxious characters, waiting NPCs

### Registration Example

```cpp
void setupIdleBehaviors() {
    // Stationary - completely still
    auto stationary = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::STATIONARY);
    AIManager::Instance().registerBehavior("Stationary", stationary);

    // Subtle sway - gentle life-like movement
    auto subtleSway = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::SUBTLE_SWAY, 25.0f);
    AIManager::Instance().registerBehavior("IdleSway", subtleSway);

    // Occasional turn - alert but stationary
    auto occasionalTurn = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::OCCASIONAL_TURN);
    AIManager::Instance().registerBehavior("IdleTurn", occasionalTurn);

    // Light fidget - nervous movement
    auto lightFidget = std::make_shared<IdleBehavior>(IdleBehavior::IdleMode::LIGHT_FIDGET, 15.0f);
    AIManager::Instance().registerBehavior("IdleFidget", lightFidget);
}
```

## WanderBehavior Modes

WanderBehavior supports multiple wander modes that automatically configure different movement ranges and behaviors.

### Available Modes

#### `WanderMode::SMALL_AREA`
**Use Case**: NPCs that should stay in personal/local space
- **Configuration**:
  - Radius: 75px from center point
  - Direction Changes: Every 1.5 seconds
  - Offscreen Probability: 5%
- **Best For**: Guards at posts, shopkeepers, stationary NPCs with minimal movement

#### `WanderMode::MEDIUM_AREA`
**Use Case**: Standard wandering for most NPCs
- **Configuration**:
  - Radius: 200px from center point
  - Direction Changes: Every 2.5 seconds
  - Offscreen Probability: 10%
- **Best For**: Villagers, general NPCs, default behavior for most characters

#### `WanderMode::LARGE_AREA`
**Use Case**: NPCs that need to cover large areas
- **Configuration**:
  - Radius: 450px from center point
  - Direction Changes: Every 3.5 seconds
  - Offscreen Probability: 20%
- **Best For**: Merchants, scouts, explorers, roaming NPCs, wide-area coverage

#### `WanderMode::EVENT_TARGET`
**Use Case**: Wander around specific objectives or targets
- **Configuration**:
  - Radius: 150px around target position
  - Direction Changes: Every 2.0 seconds
  - Offscreen Probability: 5%
  - Target: Must be set via `setCenterPoint()` after creation
- **Best For**: Objective guards, followers with loose formation, area protection

### Registration Example

```cpp
void setupWanderBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;

    // Small area wander - minimal local movement
    auto smallWander = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 1.5f
    );
    smallWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("SmallWander", smallWander);

    // Medium area wander - standard NPC behavior
    auto mediumWander = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f
    );
    mediumWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Wander", mediumWander);

    // Large area wander - wide exploration
    auto largeWander = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::LARGE_AREA, 2.5f
    );
    largeWander->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("LargeWander", largeWander);

    // Event target wander - objective-based movement
    auto eventWander = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::EVENT_TARGET, 2.0f
    );
    eventWander->setScreenDimensions(worldWidth, worldHeight);
    // Set target position after assignment to entity
    eventWander->setCenterPoint(Vector2D(400, 300)); // Custom target location
    AIManager::Instance().registerBehavior("EventWander", eventWander);
}
```

## PatrolBehavior Modes

PatrolBehavior supports multiple patrol modes that automatically configure different types of patrol patterns.

### Available Modes

#### `PatrolMode::FIXED_WAYPOINTS`
**Use Case**: Traditional patrol routes with predefined waypoints
- **Configuration**: Uses manually defined waypoint sequences
- **Auto-regenerate**: Disabled by default
- **Best For**: Guards with specific routes, scripted patrol paths, precise movement requirements

#### `PatrolMode::RANDOM_AREA`
**Use Case**: Dynamic patrol within a rectangular area
- **Configuration**:
  - Area: Left 40% of screen (50px margin from edges)
  - Waypoints: 6 random points within rectangle
  - Min Distance: 80px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Area guards, flexible coverage, dynamic patrol routes

#### `PatrolMode::CIRCULAR_AREA`
**Use Case**: Patrol around a central location
- **Configuration**:
  - Area: Right 75% of screen horizontally, centered vertically
  - Radius: 120px from center point
  - Waypoints: 5 random points within circle
  - Min Distance: 60px between waypoints
  - Auto-regenerate: Enabled
- **Best For**: Perimeter guards, defensive positions, circular coverage patterns

#### `PatrolMode::EVENT_TARGET`
**Use Case**: Patrol around specific objectives or targets
- **Configuration**:
  - Target: Screen center by default (configurable via `setEventTarget()`)
  - Radius: 150px around target
  - Waypoints: 8 points in circular pattern with randomized distances
  - Pattern: Circular with 70-100% radius variation for natural movement
  - Auto-regenerate: Disabled (static around target)
- **Best For**: Objective guards, VIP protection, point defense

### Registration Example

```cpp
void setupPatrolBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;

    // Fixed waypoint patrol - traditional predefined routes
    auto fixedPatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::FIXED_WAYPOINTS, 1.5f
    );
    fixedPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Patrol", fixedPatrol);

    // Random area patrol - dynamic rectangular coverage
    auto randomPatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f
    );
    randomPatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("RandomPatrol", randomPatrol);

    // Circular area patrol - perimeter defense
    auto circlePatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 1.8f
    );
    circlePatrol->setScreenDimensions(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("CirclePatrol", circlePatrol);

    // Event target patrol - objective protection
    auto eventPatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::EVENT_TARGET, 2.2f
    );
    eventPatrol->setScreenDimensions(worldWidth, worldHeight);
    // Optional: Set custom event target position
    eventPatrol->setEventTarget(Vector2D(640, 360), 200.0f, 6);
    AIManager::Instance().registerBehavior("EventTarget", eventPatrol);
}
```

## FleeBehavior Modes

FleeBehavior provides intelligent escape and avoidance patterns with different tactical approaches.

### Available Modes

#### `FleeMode::PANIC_FLEE`
**Use Case**: Erratic, frightened escape behavior
- **Configuration**:
  - Speed: 1.2x base speed (faster in panic)
  - Panic Duration: 2000ms (2 seconds)
  - Pattern: Erratic, unpredictable movement
- **Best For**: Civilians under attack, scared animals, panicked NPCs

#### `FleeMode::STRATEGIC_RETREAT`
**Use Case**: Calculated, tactical withdrawal
- **Configuration**:
  - Speed: 0.8x base speed (slower, calculated)
  - Safe Distance: 1.8x detection range
  - Pattern: Planned, efficient retreat
- **Best For**: Soldiers, tactical NPCs, intelligent enemies

#### `FleeMode::EVASIVE_MANEUVER`
**Use Case**: Zigzag pattern while fleeing
- **Configuration**:
  - Zigzag Interval: 300ms (frequent direction changes)
  - Speed: 1.0x base speed
  - Pattern: Serpentine, hard to predict
- **Best For**: Rogues, agile characters, evasive enemies

#### `FleeMode::SEEK_COVER`
**Use Case**: Flee towards cover or safe zones
- **Configuration**:
  - Safe Distance: 1.5x detection range
  - Speed: 1.0x base speed
  - Pattern: Direct path to nearest safe zone
- **Best For**: Smart NPCs, cover-aware enemies, tactical retreat

### Registration Example

```cpp
void setupFleeBehaviors() {
    float worldWidth = 1280.0f;
    float worldHeight = 720.0f;

    // Panic flee - erratic escape
    auto panicFlee = std::make_shared<FleeBehavior>(
        FleeBehavior::FleeMode::PANIC_FLEE, 4.0f, 400.0f
    );
    panicFlee->setScreenBounds(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("PanicFlee", panicFlee);

    // Strategic retreat - calculated escape
    auto strategicRetreat = std::make_shared<FleeBehavior>(
        FleeBehavior::FleeMode::STRATEGIC_RETREAT, 3.5f, 350.0f
    );
    strategicRetreat->setScreenBounds(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Retreat", strategicRetreat);

    // Evasive maneuver - zigzag escape
    auto evasiveManeuver = std::make_shared<FleeBehavior>(
        FleeBehavior::FleeMode::EVASIVE_MANEUVER, 4.2f, 400.0f
    );
    evasiveManeuver->setScreenBounds(worldWidth, worldHeight);
    AIManager::Instance().registerBehavior("Evade", evasiveManeuver);

    // Seek cover - tactical escape
    auto seekCover = std::make_shared<FleeBehavior>(
        FleeBehavior::FleeMode::SEEK_COVER, 3.8f, 380.0f
    );
    seekCover->setScreenBounds(worldWidth, worldHeight);
    // Add safe zones
    seekCover->addSafeZone(Vector2D(100, 100), 50.0f);
    seekCover->addSafeZone(Vector2D(600, 400), 75.0f);
    AIManager::Instance().registerBehavior("SeekCover", seekCover);
}
```

## FollowBehavior Modes

FollowBehavior provides sophisticated target following with different formation and distance configurations.

### Available Modes

#### `FollowMode::CLOSE_FOLLOW`
**Use Case**: Stay very close to target
- **Configuration**:
  - Follow Distance: 50px
  - Max Distance: 150px
  - Catch-up Speed: 2.0x multiplier
- **Best For**: Pets, close companions, bodyguards

#### `FollowMode::LOOSE_FOLLOW`
**Use Case**: Maintain comfortable distance
- **Configuration**:
  - Follow Distance: 120px
  - Max Distance: 300px
  - Catch-up Speed: 1.5x multiplier
- **Best For**: Standard companions, followers, party members

#### `FollowMode::FLANKING_FOLLOW`
**Use Case**: Follow from the sides
- **Configuration**:
  - Follow Distance: 100px
  - Max Distance: 250px
  - Formation Offset: 80px to the side
- **Best For**: Escorts, tactical formations, side guards

#### `FollowMode::REAR_GUARD`
**Use Case**: Follow from behind
- **Configuration**:
  - Follow Distance: 150px
  - Max Distance: 350px
  - Formation Offset: 120px behind
- **Best For**: Rear guards, protectors, covering fire

#### `FollowMode::ESCORT_FORMATION`
**Use Case**: Maintain formation around target
- **Configuration**:
  - Follow Distance: 100px
  - Max Distance: 250px
  - Formation Radius: 100px
  - Pattern: Circular formation with multiple positions
- **Best For**: VIP protection, ceremonial escorts, formation combat

### Registration Example

```cpp
void setupFollowBehaviors() {
    // Close follow - pets and close companions
    auto closeFollow = std::make_shared<FollowBehavior>(
        FollowBehavior::FollowMode::CLOSE_FOLLOW, 2.5f
    );
    AIManager::Instance().registerBehavior("FollowClose", closeFollow);

    // Loose follow - standard companions
    auto looseFollow = std::make_shared<FollowBehavior>(
        FollowBehavior::FollowMode::LOOSE_FOLLOW, 2.2f
    );
    AIManager::Instance().registerBehavior("Follow", looseFollow);

    // Flanking follow - tactical positioning
    auto flankingFollow = std::make_shared<FollowBehavior>(
        FollowBehavior::FollowMode::FLANKING_FOLLOW, 2.3f
    );
    AIManager::Instance().registerBehavior("FollowFlank", flankingFollow);

    // Rear guard - protection from behind
    auto rearGuard = std::make_shared<FollowBehavior>(
        FollowBehavior::FollowMode::REAR_GUARD, 2.0f
    );
    AIManager::Instance().registerBehavior("RearGuard", rearGuard);

    // Escort formation - multiple guard positions
    auto escortFormation = std::make_shared<FollowBehavior>(
        FollowBehavior::FollowMode::ESCORT_FORMATION, 2.4f
    );
    AIManager::Instance().registerBehavior("EscortFormation", escortFormation);
}
```

## GuardBehavior Modes

GuardBehavior provides comprehensive area defense with different alert levels and movement patterns.

### Available Modes

#### `GuardMode::STATIC_GUARD`
**Use Case**: Stationary guard at specific position
- **Configuration**:
  - Movement Speed: 0.0f (no movement unless investigating)
  - Alert Radius: 1.5x guard radius
  - Pattern: Stay at assigned position
- **Best For**: Gate guards, post sentries, checkpoint guards

#### `GuardMode::PATROL_GUARD`
**Use Case**: Guard with predefined patrol route
- **Configuration**:
  - Movement Speed: 1.5f
  - Alert Radius: 1.8x guard radius
  - Pattern: Move between waypoints
- **Best For**: Perimeter guards, route security, patrol officers

#### `GuardMode::AREA_GUARD`
**Use Case**: Guard a specific rectangular or circular area
- **Configuration**:
  - Movement Speed: 1.2f
  - Alert Radius: 2.0x guard radius
  - Pattern: Move within defined area
- **Best For**: Area security, zone protection, territory guards

#### `GuardMode::ROAMING_GUARD`
**Use Case**: Free-roaming guard within zone
- **Configuration**:
  - Movement Speed: 1.8f
  - Alert Radius: 1.6x guard radius
  - Roam Interval: 6.0f seconds
- **Best For**: Mobile security, wandering guards, flexible coverage

#### `GuardMode::ALERT_GUARD`
**Use Case**: High alert state with enhanced response
- **Configuration**:
  - Movement Speed: 2.5f
  - Alert Speed: 4.0f
  - Alert Radius: 2.5x guard radius
  - Threat Detection Range: 2.0x guard radius
- **Best For**: Combat situations, high-security areas, emergency response

### Registration Example

```cpp
void setupGuardBehaviors() {
    Vector2D guardPost(200, 200);
    float guardRadius = 150.0f;

    // Static guard - stationary post duty
    auto staticGuard = std::make_shared<GuardBehavior>(
        GuardBehavior::GuardMode::STATIC_GUARD, guardPost, guardRadius
    );
    AIManager::Instance().registerBehavior("PostGuard", staticGuard);

    // Patrol guard - route-based security
    auto patrolGuard = std::make_shared<GuardBehavior>(
        GuardBehavior::GuardMode::PATROL_GUARD, guardPost, guardRadius
    );
    // Add patrol waypoints
    patrolGuard->addPatrolWaypoint(Vector2D(100, 100));
    patrolGuard->addPatrolWaypoint(Vector2D(300, 100));
    patrolGuard->addPatrolWaypoint(Vector2D(300, 300));
    patrolGuard->addPatrolWaypoint(Vector2D(100, 300));
    AIManager::Instance().registerBehavior("PatrolGuard", patrolGuard);

    // Area guard - zone protection
    auto areaGuard = std::make_shared<GuardBehavior>(
        GuardBehavior::GuardMode::AREA_GUARD, guardPost, guardRadius
    );
    areaGuard->setGuardArea(Vector2D(50, 50), Vector2D(350, 350)); // Rectangular area
    AIManager::Instance().registerBehavior("AreaGuard", areaGuard);

    // Roaming guard - mobile security
    auto roamingGuard = std::make_shared<GuardBehavior>(
        GuardBehavior::GuardMode::ROAMING_GUARD, guardPost, guardRadius
    );
    AIManager::Instance().registerBehavior("RoamingGuard", roamingGuard);

    // Alert guard - high-security response
    auto alertGuard = std::make_shared<GuardBehavior>(
        GuardBehavior::GuardMode::ALERT_GUARD, guardPost, guardRadius
    );
    alertGuard->setThreatDetectionRange(300.0f);
    AIManager::Instance().registerBehavior("AlertGuard", alertGuard);
}
```

## AttackBehavior Modes

AttackBehavior provides comprehensive combat AI with different attack styles and tactical approaches.

### Available Modes

#### `AttackMode::MELEE_ATTACK`
**Use Case**: Close combat attacks
- **Configuration**:
  - Attack Range: Max 100px
  - Optimal Range: 80% of attack range
  - Attack Speed: 1.2f
  - Movement Speed: 2.5f
- **Best For**: Warriors, knights, melee fighters

#### `AttackMode::RANGED_ATTACK`
**Use Case**: Projectile-based attacks
- **Configuration**:
  - Attack Range: Min 200px
  - Optimal Range: 70% of attack range
  - Attack Speed: 0.8f
  - Movement Speed: 2.0f
- **Best For**: Archers, mages, ranged units

#### `AttackMode::CHARGE_ATTACK`
**Use Case**: Rush towards target with powerful strike
- **Configuration**:
  - Attack Range: 1.5x base range
  - Movement Speed: 3.5f
  - Attack Speed: 0.5f (slower but powerful)
  - Damage Multiplier: 2.0x
- **Best For**: Cavalry, berserkers, charging units

#### `AttackMode::AMBUSH_ATTACK`
**Use Case**: Wait and strike with stealth
- **Configuration**:
  - Optimal Range: 60% of attack range
  - Attack Speed: 2.0f
  - Critical Hit Chance: 30%
  - Movement Speed: 1.5f (steal
thy)
- **Best For**: Assassins, rogues, stealth units

#### `AttackMode::COORDINATED_ATTACK`
**Use Case**: Attack in formation with teamwork
- **Configuration**:
  - Teamwork: Enabled
  - Flanking: Enabled
  - Movement Speed: 2.2f
  - Pattern: Coordinate with nearby allies
- **Best For**: Squad-based combat, tactical units

#### `AttackMode::HIT_AND_RUN`
**Use Case**: Quick strike then retreat
- **Configuration**:
  - Attack Speed: 1.5f
  - Movement Speed: 3.0f
  - Retreat Threshold: 80% health
  - Pattern: Strike and withdraw
- **Best For**: Skirmishers, guerrilla fighters, mobile units

#### `AttackMode::BERSERKER_ATTACK`
**Use Case**: Aggressive continuous assault
- **Configuration**:
  - Attack Speed: 1.8f
  - Movement Speed: 2.8f
  - Aggression: 100%
  - Retreat Threshold: 10% health
  - Combo Attacks: Enabled
- **Best For**: Berserkers, enraged enemies, boss fights

### Registration Example

```cpp
void setupAttackBehaviors() {
    // Melee attack - close combat
    auto meleeAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::MELEE_ATTACK, 80.0f, 15.0f
    );
    AIManager::Instance().registerBehavior("Melee", meleeAttack);

    // Ranged attack - projectile combat
    auto rangedAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::RANGED_ATTACK, 250.0f, 12.0f
    );
    AIManager::Instance().registerBehavior("Ranged", rangedAttack);

    // Charge attack - powerful rush
    auto chargeAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::CHARGE_ATTACK, 120.0f, 20.0f
    );
    AIManager::Instance().registerBehavior("Charge", chargeAttack);

    // Ambush attack - stealth strike
    auto ambushAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::AMBUSH_ATTACK, 60.0f, 18.0f
    );
    AIManager::Instance().registerBehavior("Ambush", ambushAttack);

    // Coordinated attack - team combat
    auto coordinatedAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::COORDINATED_ATTACK, 90.0f, 14.0f
    );
    AIManager::Instance().registerBehavior("Coordinated", coordinatedAttack);

    // Hit and run - mobile combat
    auto hitAndRun = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::HIT_AND_RUN, 100.0f, 13.0f
    );
    AIManager::Instance().registerBehavior("HitAndRun", hitAndRun);

    // Berserker attack - aggressive assault
    auto berserkerAttack = std::make_shared<AttackBehavior>(
        AttackBehavior::AttackMode::BERSERKER_ATTACK, 85.0f, 16.0f
    );
    AIManager::Instance().registerBehavior("Berserker", berserkerAttack);
}
```

## ChaseBehavior

ChaseBehavior provides target pursuit without modes (single configuration approach).

### Configuration

```cpp
void setupChaseBehavior() {
    auto chase = std::make_shared<ChaseBehavior>(
        3.0f,    // Chase speed
        500.0f,  // Max range
        50.0f    // Min range
    );
    AIManager::Instance().registerBehavior("Chase", chase);
}
```

## NPC Type Assignment

### Enhanced Assignment Logic

```cpp
std::string getBehaviorForNPCType(const std::string& npcType, const std::string& context = "") {
    static std::unordered_map<std::string, size_t> counters;
    size_t index = counters[npcType]++;

    if (npcType == "Guard") {
        if (context == "alert") return "AlertGuard";
        if (context == "patrol") return "PatrolGuard";
        if (context == "post") return "PostGuard";
        std::vector<std::string> behaviors = {
            "PostGuard", "PatrolGuard", "AreaGuard", "SmallWander", "CirclePatrol"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Villager") {
        std::vector<std::string> behaviors = {
            "IdleSway", "SmallWander", "Wander", "IdleTurn"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Merchant") {
        std::vector<std::string> behaviors = {
            "Wander", "LargeWander", "RandomPatrol", "IdleSway"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Companion") {
        if (context == "combat") return "FollowClose";
        if (context == "escort") return "EscortFormation";
        std::vector<std::string> behaviors = {
            "Follow", "FollowClose", "FollowFlank"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Enemy") {
        if (context == "melee") return "Melee";
        if (context == "ranged") return "Ranged";
        if (context == "aggressive") return "Berserker";
        std::vector<std::string> behaviors = {
            "Chase", "Melee", "Ranged", "Coordinated"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Animal") {
        if (context == "predator") return "Chase";
        if (context == "prey") return "PanicFlee";
        if (context == "neutral") return "Wander";
        std::vector<std::string> behaviors = {
            "Wander", "SmallWander", "PanicFlee"
        };
        return behaviors[index % behaviors.size()];
    }
    else if (npcType == "Civilian") {
        if (context == "panic") return "PanicFlee";
        std::vector<std::string> behaviors = {
            "IdleSway", "SmallWander", "Wander", "Stationary"
        };
        return behaviors[index % behaviors.size()];
    }

    return "Wander"; // Default fallback
}
```

### Complete Integration Example

```cpp
void GameState::createNPC(const std::string& npcType, const Vector2D& position, const std::string& context = "") {
    auto npc = std::make_shared<NPC>(npcType, position, 64, 64);

    // Get behavior and priority for this NPC type
    std::string behaviorName = getBehaviorForNPCType(npcType, context);
    int priority = getPriorityForNPCType(npcType, context);

    // Register with AI system
    AIManager::Instance().registerEntityForUpdates(npc, priority, behaviorName);

    // Track the NPC
    m_npcs.push_back(npc);
}

int getPriorityForNPCType(const std::string& npcType, const std::string& context = "") {
    if (npcType == "Enemy") return (context == "aggressive") ? 9 : 8;
    else if (npcType == "Guard") return (context == "alert") ? 9 : 7;
    else if (npcType == "Companion") return (context == "combat") ? 7 : 6;
    else if (npcType == "Merchant") return 5;
    else if (npcType == "Animal") return (context == "predator") ? 6 : 3;
    else if (npcType == "Civilian") return (context == "panic") ? 5 : 2;
    else if (npcType == "Villager") return 2;
    return 3; // Default
}
```

## Complete Mode Comparison Tables

### IdleBehavior Mode Comparison

| Mode | Movement Freq | Turn Freq | Radius | Use Case |
|------|---------------|-----------|--------|----------|
| STATIONARY | 0.0f | 0.0f | N/A | Statue-like NPCs |
| SUBTLE_SWAY | 2.0f seconds | 8.0f seconds | 20px | Shopkeepers, relaxed NPCs |
| OCCASIONAL_TURN | 0.0f | 4.0f seconds | N/A | Alert guards, lookouts |
| LIGHT_FIDGET | 1.5f seconds | 3.0f seconds | 20px | Nervous NPCs, waiting characters |

### WanderBehavior Mode Comparison

| Mode | Radius | Change Freq | Offscreen % | Use Case |
|------|--------|-------------|-------------|----------|
| SMALL_AREA | 75px | 1.5s | 5% | Local/stationary |
| MEDIUM_AREA | 200px | 2.5s | 10% | Standard NPCs |
| LARGE_AREA | 450px | 3.5s | 20% | Roaming NPCs |
| EVENT_TARGET | 150px | 2.0s | 5% | Objective-based |

### PatrolBehavior Mode Comparison

| Mode | Area Type | Waypoints | Auto-Regen | Min Distance | Use Case |
|------|-----------|-----------|------------|--------------|----------|
| FIXED_WAYPOINTS | Manual | User-defined | No | N/A | Scripted routes |
| RANDOM_AREA | Rectangle (left 40%) | 6 random | Yes | 80px | Flexible coverage |
| CIRCULAR_AREA | Circle (right 75%) | 5 random | Yes | 60px | Perimeter defense |
| EVENT_TARGET | Circle (150px radius) | 8 around target | No | Variable | Objective protection |

### FleeBehavior Mode Comparison

| Mode | Speed Modifier | Special Feature | Pattern | Use Case |
|------|----------------|-----------------|---------|----------|
| PANIC_FLEE | 1.2x | 2s panic duration | Erratic | Civilians, scared animals |
| STRATEGIC_RETREAT | 0.8x | 1.8x safe distance | Calculated | Soldiers, tactical NPCs |
| EVASIVE_MANEUVER | 1.0x | 300ms zigzag | Serpentine | Rogues, agile characters |
| SEEK_COVER | 1.0x | Safe zone seeking | Direct to cover | Smart NPCs, tactical retreat |

### FollowBehavior Mode Comparison

| Mode | Follow Distance | Max Distance | Catch-up Speed | Formation |
|------|-----------------|--------------|----------------|-----------|
| CLOSE_FOLLOW | 50px | 150px | 2.0x | None |
| LOOSE_FOLLOW | 120px | 300px | 1.5x | None |
| FLANKING_FOLLOW | 100px | 250px | 1.5x | Side offset |
| REAR_GUARD | 150px | 350px | 1.5x | Behind offset |
| ESCORT_FORMATION | 100px | 250px | 1.5x | Circular formation |

### GuardBehavior Mode Comparison

| Mode | Movement Speed | Alert Radius | Special Feature | Use Case |
|------|----------------|--------------|-----------------|----------|
| STATIC_GUARD | 0.0f | 1.5x | Stationary | Gate guards, posts |
| PATROL_GUARD | 1.5f | 1.8x | Waypoint patrol | Perimeter guards |
| AREA_GUARD | 1.2f | 2.0x | Zone coverage | Area security |
| ROAMING_GUARD | 1.8f | 1.6x | 6s roam interval | Mobile security |
| ALERT_GUARD | 2.5f | 2.5x | Enhanced detection | High-security areas |

### AttackBehavior Mode Comparison

| Mode | Range | Speed | Damage | Special Feature |
|------|-------|-------|--------|-----------------|
| MELEE_ATTACK | ≤100px | 2.5f | 1.0x | Close combat |
| RANGED_ATTACK | ≥200px | 2.0f | 1.0x | Projectile attacks |
| CHARGE_ATTACK | 1.5x | 3.5f | 2.0x | Rush attack |
| AMBUSH_ATTACK | 0.6x optimal | 1.5f | 1.0x + 30% crit | Stealth strike |
| COORDINATED_ATTACK | Standard | 2.2f | 1.0x | Team tactics |
| HIT_AND_RUN | Standard | 3.0f | 1.0x | Strike and retreat |
| BERSERKER_ATTACK | Standard | 2.8f | 1.0x | Combo attacks |

## Advanced Configuration

### Dynamic Target Updates

```cpp
// Update patrol targets during gameplay
void updatePatrolTarget(std::shared_ptr<PatrolBehavior> patrol, const Vector2D& newTarget) {
    if (patrol->getPatrolMode() == PatrolBehavior::PatrolMode::EVENT_TARGET) {
        patrol->updateEventTarget(newTarget);
        // Waypoints automatically regenerated around new target
    }
}

// Update wander centers during gameplay
void updateWanderCenter(std::shared_ptr<WanderBehavior> wander, const Vector2D& newCenter) {
    wander->setCenterPoint(newCenter);
    // Entity will now wander around new center point
}

// Update guard positions during gameplay
void updateGuardPost(std::shared_ptr<GuardBehavior> guard, const Vector2D& newPost) {
    guard->setGuardPosition(newPost);
    // Guard will patrol around new position
}
```

### Behavior Switching at Runtime

```cpp
// Switch behavior modes during gameplay
void switchBehaviorMode(EntityPtr entity, const std::string& newBehaviorName) {
    // Remove from current behavior
    AIManager::Instance().removeEntityFromUpdates(entity);

    // Assign new behavior
    int priority = getPriorityForNPCType(entity->getType());
    AIManager::Instance().registerEntityForUpdates(entity, priority, newBehaviorName);
}

// Context-aware behavior switching
void handleContextChange(EntityPtr entity, const std::string& newContext) {
    std::string npcType = entity->getType();
    std::string newBehavior = getBehaviorForNPCType(npcType, newContext);

    if (newBehavior != entity->getCurrentBehavior()) {
        switchBehaviorMode(entity, newBehavior);
    }
}
```

### Custom Area Configuration

```cpp
// Override mode defaults with custom settings
void setupCustomPatrolArea() {
    auto customPatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f
    );
    customPatrol->setScreenDimensions(1280.0f, 720.0f);

    // Override default area settings
    Vector2D customTopLeft(100, 100);
    Vector2D customBottomRight(600, 500);
    customPatrol->setRandomPatrolArea(customTopLeft, customBottomRight, 8);
    customPatrol->setMinWaypointDistance(120.0f);
    customPatrol->setAutoRegenerate(false); // Static waypoints

    AIManager::Instance().registerBehavior("CustomPatrol", customPatrol);
}

// Configure safe zones for flee behavior
void setupSafeZones() {
    auto fleeBehavior = std::make_shared<FleeBehavior>(
        FleeBehavior::FleeMode::SEEK_COVER, 4.0f, 400.0f
    );

    // Add multiple safe zones
    fleeBehavior->addSafeZone(Vector2D(100, 100), 50.0f);  // Town center
    fleeBehavior->addSafeZone(Vector2D(600, 400), 75.0f);  // Guard post
    fleeBehavior->addSafeZone(Vector2D(300, 600), 60.0f);  // Shelter
    fleeBehavior->setScreenBounds(1280.0f, 720.0f);

    AIManager::Instance().registerBehavior("FleeToSafety", fleeBehavior);
}
```

### Performance Optimization

```cpp
// Optimize for different performance scenarios
void setupPerformanceOptimizedBehaviors() {
    // Low-performance mode - reduced computation
    auto lightWander = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::SMALL_AREA, 1.0f
    );
    lightWander->setChangeDirectionInterval(5000.0f); // Less frequent updates
    lightWander->setOffscreenProbability(0.0f);       // Never go offscreen
    AIManager::Instance().registerBehavior("LightWander", lightWander);

    // High-performance mode - enhanced activity
    auto activePatrol = std::make_shared<PatrolBehavior>(
        PatrolBehavior::PatrolMode::CIRCULAR_AREA, 3.0f
    );
    activePatrol->setAutoRegenerate(true);
    activePatrol->setMinWaypointDistance(40.0f);       // Tighter patterns
    AIManager::Instance().registerBehavior("ActivePatrol", activePatrol);

    // Memory-efficient idle behavior
    auto efficientIdle = std::make_shared<IdleBehavior>(
        IdleBehavior::IdleMode::STATIONARY
    );
    efficientIdle->setMovementFrequency(0.0f);         // No movement calculations
    AIManager::Instance().registerBehavior("EfficientIdle", efficientIdle);
}
```

## Best Practices

### 1. Mode Selection Guidelines

#### By NPC Type
- **Guards**: Use STATIC_GUARD for posts, PATROL_GUARD for routes, ALERT_GUARD for combat
- **Villagers**: Prefer SUBTLE_SWAY idle and SMALL_AREA/MEDIUM_AREA wander
- **Merchants**: Use MEDIUM_AREA wander and OCCASIONAL_TURN idle for market presence
- **Companions**: Use LOOSE_FOLLOW for general following, ESCORT_FORMATION for protection
- **Enemies**: Use MELEE_ATTACK for warriors, RANGED_ATTACK for archers, BERSERKER_ATTACK for bosses
- **Animals**: Use WANDER for neutral, PANIC_FLEE for prey, CHASE for predators

#### By Context
- **Combat**: Switch to ALERT_GUARD, BERSERKER_ATTACK, or PANIC_FLEE
- **Exploration**: Use LARGE_AREA wander or ROAMING_GUARD
- **Social**: Use SUBTLE_SWAY idle or CLOSE_FOLLOW for companions
- **Stealth**: Use AMBUSH_ATTACK or EVASIVE_MANEUVER

### 2. Configuration Best Practices

```cpp
// Always set screen dimensions first
void setupBehavior() {
    auto behavior = std::make_shared<WanderBehavior>(
        WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f
    );
    behavior->setScreenDimensions(worldWidth, worldHeight);
    // Additional configuration...
}

// Use appropriate speeds for different scenarios
// Idle/Stationary: 0.0-1.0f
// Casual movement: 1.0-2.0f
// Active movement: 2.0-3.0f
// Combat/Chase: 3.0-4.0f
// Emergency: 4.0f+
```

### 3. Performance Considerations

#### Priority Assignment
- **0-2**: Background NPCs, decorative entities
- **3-5**: Standard NPCs, animals, merchants
- **6-8**: Important NPCs, companions, guards
- **9**: Critical NPCs, bosses, active threats

#### Entity Limits by Complexity
- **Simple (Idle, Wander)**: 200+ entities
- **Medium (Patrol, Chase, Flee)**: 50-100 entities
- **Complex (Follow, Guard, Attack)**: 20-50 entities

### 4. Memory Management

```cpp
// Clean up behaviors during state transitions
void transitionToNewState() {
    AIManager::Instance().prepareForStateTransition();

    // Clear old NPCs and behaviors
    m_npcs.clear();

    // Set up new behaviors for new state
    setupBehaviorsForNewState();
}
```

## Troubleshooting Guide

### Common Issues and Solutions

#### Issue: NPCs Not Moving
- **Cause**: Screen dimensions not set or entity not registered
- **Solution**: Call `setScreenDimensions()` and verify entity registration

#### Issue: Erratic Movement Patterns
- **Cause**: Conflicting behaviors or incorrect priority settings
- **Solution**: Check for multiple behavior assignments and priority conflicts

#### Issue: Performance Degradation
- **Cause**: Too many complex behaviors or high update frequencies
- **Solution**: Use appropriate priorities and simpler behaviors for background NPCs

#### Issue: NPCs Stuck at Screen Edges
- **Cause**: High offscreen probability or boundary detection issues
- **Solution**: Reduce offscreen probability or add boundary avoidance

#### Issue: Behaviors Not Switching
- **Cause**: Incorrect behavior names or registration issues
- **Solution**: Verify behavior names match registered behaviors

### Debug Helpers

```cpp
// Debug behavior state
void debugBehaviorState(EntityPtr entity) {
    std::cout << "Entity " << entity->getId()
              << " behavior: " << entity->getCurrentBehavior()
              << " priority: " << entity->getPriority()
              << " position: " << entity->getPosition().toString()
              << std::endl;
}

// Monitor behavior performance
void monitorBehaviorPerformance() {
    auto& aiManager = AIManager::Instance();
    std::cout << "Active entities: " << aiManager.getActiveEntityCount()
              << " Update time: " << aiManager.getLastUpdateTime()
              << "ms" << std::endl;
}
```

## Complete Setup Template

```cpp
void setupCompleteAISystem() {
    // Initialize AI Manager
    AIManager::Instance().init();

    // Set up all behavior types
    setupIdleBehaviors();
    setupWanderBehaviors();
    setupPatrolBehaviors();
    setupFleeBehaviors();
    setupFollowBehaviors();
    setupGuardBehaviors();
    setupAttackBehaviors();
    setupChaseBehavior();

    // Set player reference for distance optimization
    if (playerEntity) {
        AIManager::Instance().setPlayerForDistanceOptimization(playerEntity);
    }

    // Create diverse NPC population
    createNPC("Guard", Vector2D(100, 100), "patrol");
    createNPC("Villager", Vector2D(200, 200));
    createNPC("Merchant", Vector2D(300, 300));
    createNPC("Companion", Vector2D(50, 50), "escort");
    createNPC("Enemy", Vector2D(400, 400), "melee");
    createNPC("Animal", Vector2D(500, 500), "neutral");
    createNPC("Civilian", Vector2D(150, 150));
}
```

This comprehensive mode-based AI system provides:
- **Flexibility**: 8 behavior types with 32 total modes
- **Performance**: Optimized for different entity counts and complexity levels
- **Maintainability**: Clean separation of concerns and easy configuration
- **Scalability**: Support for hundreds of entities with appropriate priority management
- **Customization**: Override any mode defaults while maintaining automatic setup benefits

The system eliminates manual configuration complexity while preserving full customization capabilities, making it ideal for both rapid prototyping and production games.
