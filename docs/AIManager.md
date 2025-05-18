# AI Manager System Documentation

## Overview

The AI Manager is a centralized system for creating and managing autonomous behaviors for game entities. It provides a flexible framework for implementing and controlling various AI behaviors, allowing game entities to exhibit different movement patterns and reactions.

## Core Components

### AIManager

The central management class that handles:
- Registration of behaviors
- Assignment of behaviors to entities
- Updating all behaviors during the game loop
- Communication with behaviors via messages

### AIBehavior Base Class

The abstract interface that all behaviors must implement:
- `update(Entity*)`: Called each frame to update entity movement/actions
- `init(Entity*)`: Called when a behavior is first assigned to an entity
- `clean(Entity*)`: Called when a behavior is removed from an entity
- `onMessage(Entity*, const std::string&)`: Handles messages sent to the behavior

## Available Behaviors

### WanderBehavior

Entities move randomly within a defined area, changing direction periodically.

**Configuration options:**
- Movement speed
- Direction change interval
- Wander area radius

### PatrolBehavior

Entities follow a predefined path of waypoints, moving from point to point in sequence.

**Configuration options:**
- Array of waypoint positions
- Movement speed
- Waypoint radius (how close to get before moving to next point)

### ChaseBehavior

Entities pursue a target (typically the player) when within detection range.

**Configuration options:**
- Target entity reference
- Chase speed
- Maximum detection/pursuit range
- Minimum distance to maintain from target

## Example Usage

### Basic Setup

```cpp
// Initialize the AI Manager (typically done in your game state's enter() method)
AIManager::Instance().init();

// Create and register behaviors
auto wanderBehavior = std::make_unique<WanderBehavior>(2.0f, 3000.0f, 200.0f);
AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));

// Create patrol points
std::vector<Vector2D> patrolPoints;
patrolPoints.push_back(Vector2D(100, 100));
patrolPoints.push_back(Vector2D(500, 100));
patrolPoints.push_back(Vector2D(500, 400));
patrolPoints.push_back(Vector2D(100, 400));
auto patrolBehavior = std::make_unique<PatrolBehavior>(patrolPoints, 1.5f);
AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));

// Create chase behavior (targeting the player)
auto chaseBehavior = std::make_unique<ChaseBehavior>(player, 2.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("Chase", std::move(chaseBehavior));
```

### Assigning Behaviors to Entities

```cpp
// Create an NPC
auto npc = std::make_unique<NPC>("npc_sprite", Vector2D(250, 250), 64, 64);

// Assign an initial behavior
AIManager::Instance().assignBehaviorToEntity(npc.get(), "Wander");

// Later, switch to a different behavior
AIManager::Instance().assignBehaviorToEntity(npc.get(), "Patrol");

// Respond to player detection by switching to chase
if (isPlayerDetected) {
    AIManager::Instance().assignBehaviorToEntity(npc.get(), "Chase");
}
```

### Controlling Behaviors with Messages

```cpp
// Pause a specific entity's behavior
AIManager::Instance().sendMessageToEntity(npc.get(), "pause");

// Resume a specific entity's behavior
AIManager::Instance().sendMessageToEntity(npc.get(), "resume");

// Pause all AI entities
AIManager::Instance().broadcastMessage("pause");

// Reverse the patrol route for a specific entity
AIManager::Instance().sendMessageToEntity(npc.get(), "reverse");
```

### Integration with Game Loop

To ensure behaviors are updated each frame, call the AIManager's update method in your game state's update method:

```cpp
void GamePlayState::update() {
    // Update all AI behaviors
    AIManager::Instance().update();
    
    // Your other game update code...
    player->update();
    checkCollisions();
    updateUI();
}
```

### Cleanup

When switching game states or shutting down, clean up the AI system:

```cpp
void GamePlayState::exit() {
    // Clean up AI Manager
    AIManager::Instance().clean();
    
    // Your other cleanup code...
}
```

## Creating Custom Behaviors

To create a custom behavior, inherit from the AIBehavior base class and implement the required methods:

```cpp
class FlankingBehavior : public AIBehavior {
public:
    FlankingBehavior(Entity* target, float speed = 2.0f, float flankDistance = 100.0f)
        : m_target(target), m_speed(speed), m_flankDistance(flankDistance) {}
        
    void init(Entity* entity) override {
        // Initialize behavior state
    }
    
    void update(Entity* entity) override {
        // Implement flanking movement logic
    }
    
    void clean(Entity* entity) override {
        // Clean up resources
    }
    
    std::string getName() const override {
        return "Flanking";
    }
    
private:
    Entity* m_target;
    float m_speed;
    float m_flankDistance;
};
```

## Threading Considerations

The AIManager optionally utilizes the ThreadSystem to distribute AI updates across multiple CPU cores. This is enabled by default but can be controlled through the AIManager's initialization:

```cpp
// Disable threading for AI updates (if needed for debugging)
AIManager::Instance().setUseThreading(false);
```

When threading is enabled, be careful about accessing shared resources from behavior update methods. Consider using locks or designing behaviors to be thread-safe.

## Performance Tips

1. **Limit active behaviors**: Only register and assign behaviors you're actively using.
2. **Optimize waypoints**: Use fewer waypoints for simple patrol routes.
3. **Adjust update frequency**: For distant or less important entities, consider updating AI less frequently.
4. **Cull inactive entities**: Unassign behaviors from entities that are far from the player or inactive.
5. **Batch similar behaviors**: Group entities with the same behavior type to improve cache coherency.

## API Reference

For the complete API, see the following header files:
- `include/AIManager.hpp`
- `include/AIBehavior.hpp`
- `include/WanderBehavior.hpp`
- `include/PatrolBehavior.hpp`
- `include/ChaseBehavior.hpp`