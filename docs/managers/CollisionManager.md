# CollisionManager Documentation

## Overview

The CollisionManager is a high-performance collision detection and response system designed for the Hammer Engine. It provides efficient spatial partitioning, batch processing capabilities, and tight integration with the AI and pathfinding systems. The manager supports thousands of collision bodies while maintaining 60+ FPS performance.

## Architecture

### Design Patterns
- **Singleton Pattern**: Ensures single instance with global access
- **Hierarchical Spatial Hash**: Two-tier adaptive spatial hash system optimized for 10K+ bodies
- **Event-Driven**: Integrates with EventManager for collision notifications
- **Batch Processing**: Optimized batch updates for AI entity kinematics

### Core Components

#### Body Types
```cpp
enum class BodyType {
    STATIC,     // Immovable world geometry (walls, obstacles)
    KINEMATIC,  // Movable by script/AI (NPCs, moving platforms)
    DYNAMIC,    // Physics-driven bodies (reserved for future physics)
    TRIGGER     // Non-solid areas that generate events
};
```

#### Collision Layers
```cpp
namespace CollisionLayer {
    constexpr uint32_t Layer_Player = 0x01;
    constexpr uint32_t Layer_NPC = 0x02;
    constexpr uint32_t Layer_Environment = 0x04;
    constexpr uint32_t Layer_Projectile = 0x08;
    constexpr uint32_t Layer_Trigger = 0x10;
    constexpr uint32_t Layer_All = 0xFFFFFFFF;
}
```

## Public API Reference

### Initialization and Lifecycle

#### `bool init()`
Initializes the CollisionManager singleton.
- **Returns**: `true` if successful, `false` otherwise
- **Side Effects**: Clears all bodies, subscribes to world events, sets up collision callbacks

#### `void clean()`
Shuts down the CollisionManager and cleans up all resources.
- **Side Effects**: Clears all bodies, spatial hashes, and callbacks

#### `void prepareForStateTransition()`
Prepares the manager for game state transitions.
- **Side Effects**: Clears all collision state while keeping the manager initialized

### Body Management

#### `void addBody(EntityID id, const AABB& aabb, BodyType type)`
Adds a collision body to the system.
```cpp
// Add a static wall
CollisionManager::Instance().addBody(
    wallId,
    AABB(50.0f, 50.0f, 10.0f, 100.0f),
    BodyType::STATIC
);

// Add a kinematic NPC
CollisionManager::Instance().addBody(
    npcId,
    AABB(player.x, player.y, 16.0f, 16.0f),
    BodyType::KINEMATIC
);
```

#### `void removeBody(EntityID id)`
Removes a collision body from the system.
```cpp
CollisionManager::Instance().removeBody(entityId);
```

#### `void setBodyEnabled(EntityID id, bool enabled)`
Enables or disables collision detection for a body.

#### `void setBodyLayer(EntityID id, uint32_t layerMask, uint32_t collideMask)`
Sets collision layers and masks for filtering.
```cpp
// NPC that collides with players and environment
CollisionManager::Instance().setBodyLayer(
    npcId,
    CollisionLayer::Layer_NPC,
    CollisionLayer::Layer_Player | CollisionLayer::Layer_Environment
);
```

### Kinematic Body Updates

#### `void setKinematicPose(EntityID id, const Vector2D& center)`
Updates a kinematic body's position.

#### `void setVelocity(EntityID id, const Vector2D& velocity)`
Sets a body's velocity for collision resolution.

#### `void updateKinematicBatch(const std::vector<KinematicUpdate>& updates)`
**High-Performance Batch Update** - Optimized for AI systems managing hundreds of entities.
```cpp
std::vector<CollisionManager::KinematicUpdate> updates;
for (const auto& entity : aiEntities) {
    updates.emplace_back(entity.id, entity.position, entity.velocity);
}
CollisionManager::Instance().updateKinematicBatch(updates);
```

### Trigger System

#### Trigger Creation
```cpp
// Create water trigger area
EntityID waterTriggerId = CollisionManager::Instance().createTriggerArea(
    AABB(100.0f, 100.0f, 50.0f, 50.0f),
    HammerEngine::TriggerTag::Water,
    CollisionLayer::Layer_Environment,
    CollisionLayer::Layer_Player | CollisionLayer::Layer_NPC
);

// Create trigger at specific coordinates
EntityID triggerId = CollisionManager::Instance().createTriggerAreaAt(
    x, y, halfWidth, halfHeight,
    HammerEngine::TriggerTag::Portal,
    CollisionLayer::Layer_Trigger,
    CollisionLayer::Layer_Player
);
```

#### Trigger Tags
```cpp
enum class TriggerTag {
    None,
    Water,       // Slows movement, special effects
    Fire,        // Damage over time
    Ice,         // Slippery surfaces
    Portal,      // Teleportation points
    Checkpoint,  // Save points
    Pickup,      // Item collection
    Obstacle     // Movement penalties
};
```

#### Trigger Cooldowns
```cpp
// Set cooldown for specific trigger
CollisionManager::Instance().setTriggerCooldown(triggerId, 2.0f);

// Set default cooldown for all new triggers
CollisionManager::Instance().setDefaultTriggerCooldown(1.0f);
```

### World Integration

#### `size_t createTriggersForWaterTiles(TriggerTag tag = TriggerTag::Water)`
Automatically creates trigger areas for all water tiles in the world.
- **Returns**: Number of water triggers created
- **Use Case**: Called during world initialization

#### `size_t createTriggersForObstacles()`
Creates triggers for movement-affecting obstacles (rocks, trees).
- **Returns**: Number of obstacle triggers created

#### `size_t createStaticObstacleBodies()`
Creates static collision bodies for solid world obstacles.
- **Returns**: Number of static bodies created

#### `void rebuildStaticFromWorld()`
Rebuilds all static collision bodies from the current WorldManager state.

#### `void onTileChanged(int x, int y)`
Updates collision state when a world tile changes.

### Queries and Collision Detection

#### `bool overlaps(EntityID a, EntityID b) const`
Tests if two bodies overlap.

#### `void queryArea(const AABB& area, std::vector<EntityID>& out) const`
Finds all bodies within a specified area.
```cpp
std::vector<EntityID> nearbyBodies;
AABB searchArea(playerX, playerY, 50.0f, 50.0f);
CollisionManager::Instance().queryArea(searchArea, nearbyBodies);
```

#### `bool getBodyCenter(EntityID id, Vector2D& outCenter) const`
Gets the center position of a body.

#### Type Checking
```cpp
bool isDynamic(EntityID id) const;
bool isKinematic(EntityID id) const;
bool isTrigger(EntityID id) const;
```

### Performance Monitoring

#### `void update(float deltaTime)`
Main update loop - processes collision detection and triggers.
- **Performance**: Handles 10,000+ bodies at 60+ FPS
- **Threading**: Thread-safe, can be called from update thread

## Integration Examples

### AI System Integration
```cpp
// In AIManager update loop
std::vector<CollisionManager::KinematicUpdate> kinematicUpdates;
kinematicUpdates.reserve(m_activeEntities.size());

for (const auto& entity : m_activeEntities) {
    Vector2D newPosition = entity->getPosition() + entity->getVelocity() * deltaTime;
    kinematicUpdates.emplace_back(entity->getId(), newPosition, entity->getVelocity());
}

// Batch update all kinematic bodies
CollisionManager::Instance().updateKinematicBatch(kinematicUpdates);
```

### Event System Integration
```cpp
// Collision callbacks are automatically forwarded to EventManager
// Listen for collision events in your game systems:
EventManager::Instance().subscribe<CollisionEvent>(
    [](const CollisionEvent& event) {
        // Handle collision between event.entityA and event.entityB
        handleEntityCollision(event.entityA, event.entityB);
    }
);
```

### World Loading Integration
```cpp
// After loading a new world/level
void GameState::loadLevel() {
    // Load world data first
    WorldManager::Instance().loadWorld("level1.json");

    // Rebuild collision system from world
    CollisionManager::Instance().rebuildStaticFromWorld();

    // Create environmental triggers
    size_t waterTriggers = CollisionManager::Instance().createTriggersForWaterTiles();
    size_t obstacleTriggers = CollisionManager::Instance().createTriggersForObstacles();

    GAMEENGINE_INFO("Created " + std::to_string(waterTriggers) + " water triggers, " +
                   std::to_string(obstacleTriggers) + " obstacle triggers");
}
```

## Performance Considerations

### Hierarchical Spatial Hash Optimization
- **Two-Tier Grid System**: Coarse grid (128 units) for region culling, fine grid (32 units) for precise detection
- **Adaptive Subdivision**: Fine grid only created when region exceeds 16 bodies (20-30% performance boost)
- **Cache-Friendly**: SOA layout and zero-allocation frame processing for optimal performance
- **Automatic Sizing**: Hash automatically sizes based on world bounds and entity density

### Batch Processing Benefits
- **Reduced Lock Contention**: Single lock acquisition for batch updates
- **Cache Efficiency**: Sequential memory access patterns
- **SIMD Optimizations**: Active use of SIMDMath.hpp for AABB calculations

### SIMD Optimizations

CollisionManager leverages cross-platform SIMD operations for high-performance AABB (Axis-Aligned Bounding Box) calculations and layer mask filtering.

**SIMD Abstraction Layer:**
- **Cross-Platform**: Uses `SIMDMath.hpp` for unified SIMD operations
- **x86-64 Support**: SSE2 (baseline), AVX2 (advanced)
- **ARM64 Support**: NEON (Apple Silicon M1/M2/M3)
- **Scalar Fallback**: Automatic fallback for unsupported platforms

**AABB Bounds Calculation:**
```cpp
// Compute AABB bounds for 4 collision bodies simultaneously
using namespace HammerEngine::SIMD;

// Load centers and halfsizes (Structure-of-Arrays layout)
Float4 cx = load4(centerX);  // 4 center X coordinates
Float4 cy = load4(centerY);  // 4 center Y coordinates
Float4 hx = load4(halfsizeX); // 4 halfsize X values
Float4 hy = load4(halfsizeY); // 4 halfsize Y values

// Compute bounds: minX = cx - hx, maxX = cx + hx
Float4 minX = sub(cx, hx);
Float4 minY = sub(cy, hy);
Float4 maxX = add(cx, hx);
Float4 maxY = add(cy, hy);

// Store results
store4(boundsMinX, minX);
store4(boundsMinY, minY);
store4(boundsMaxX, maxX);
store4(boundsMaxY, maxY);
```

**Layer Mask Filtering:**
```cpp
// Check collision layer masks for 4 bodies simultaneously
Int4 layers = load4_int(layerMasks);
Int4 collides = load4_int(collideMasks);

// Bitwise AND to test overlap
Int4 overlap = bitwise_and_int(layers, collides);

// Check if any bits set (non-zero means can collide)
Int4 zero = broadcast_int(0);
Int4 mask = cmpgt_int(overlap, zero);

// Extract results as bitmask
int movemask = movemask_int(mask);
bool canCollide = (movemask != 0);
```

**Performance Benefits:**
- **2-3x speedup** on AABB calculations for 10,000+ bodies
- **Better cache locality**: Process 4 bodies per SIMD instruction
- **Reduced memory bandwidth**: Fewer loads/stores vs scalar code
- **Platform-optimized**: Automatic selection of best SIMD path

**Implementation Details:**
- AABB calculations batch processed in groups of 4
- Scalar tail loop handles remaining bodies (count % 4)
- SIMD path automatically enabled on supported platforms
- See [SIMDMath Documentation](../utils/SIMDMath.md) for complete API reference

### Performance Metrics

**Measured Performance (from performance reports):**
- **Average Update Time**: 0.25ms with 10,300 collision bodies
- **Throughput**: 10,000+ bodies at 60+ FPS consistently
- **Memory**: ~200 bytes per collision body
- **CPU Usage**: 2-4% on modern hardware for typical game scenarios

**Scaling Characteristics:**
- **100 bodies**: <0.1ms average update time
- **1,000 bodies**: 0.05-0.08ms average update time
- **10,000+ bodies**: 0.2-0.3ms average update time
- **Spatial Hash Efficiency**: O(1) average lookup, O(n) worst case

**SIMD Performance Impact:**
- **AABB Calculations**: 2-3x faster with SIMD vs scalar
- **Layer Mask Filtering**: 2-3x faster with SIMD vs scalar
- **Overall Collision System**: 20-30% improvement with SIMD optimizations

### Best Practices
1. **Use Batch Updates**: Always prefer `updateKinematicBatch()` over individual updates
2. **Layer Filtering**: Use collision layers to avoid unnecessary checks
3. **Static Body Caching**: Static bodies are cached - prefer them for immovable geometry
4. **Trigger Cooldowns**: Use cooldowns to prevent trigger spam

## Threading Model

### Design Decision: Single-Threaded Collision Detection

**CollisionManager uses single-threaded collision detection and is NOT included in WorkerBudget calculations.**

**Rationale:**
- Collision detection requires complex synchronization for spatial hash updates
- Current SoA implementation is optimized for cache-friendly single-threaded access
- Parallelization would require per-batch spatial hashes (significant memory overhead)
- Broadphase is already highly optimized (SIMD, spatial hashing, culling)

**Performance:** Handles 27,000+ bodies @ 60 FPS single-threaded on Apple Silicon. Threading is not a bottleneck for current game scale.

**Future Considerations:** If profiling shows collision as a bottleneck, consider parallel narrowphase after single-threaded broadphase.

### Thread Safety
- **Update Thread**: Main collision detection runs on update thread
- **Render Thread**: Query operations are thread-safe for rendering
- **Background Threads**: PathfinderManager can safely query collision state

### Synchronization
- **Shared Mutex**: Read-heavy operations use shared locks
- **Batch Locking**: Batch operations acquire single lock for efficiency
- **Lock-Free Queries**: Spatial hash queries avoid locks where possible

## Error Handling

### Common Issues
1. **Invalid EntityID**: Operations on non-existent bodies are safely ignored
2. **Duplicate Bodies**: Adding duplicate IDs overwrites existing bodies
3. **State Transitions**: Use `prepareForStateTransition()` to avoid dangling references

### Debug Features
```cpp
// Enable verbose collision logging
CollisionManager::Instance().setVerboseLogging(true);

// Performance statistics
auto stats = CollisionManager::Instance().getPerformanceStats();
GAMEENGINE_INFO("Collision checks: " + std::to_string(stats.collisionChecks));
```

## Testing

### Unit Tests
- **AABB Tests**: Collision boundary validation
- **SpatialHash Tests**: Insertion, removal, and query validation
- **Performance Tests**: Scaling tests with 10K+ entities

### Integration Tests
- **World Integration**: Automatic body creation from world data
- **AI Integration**: Batch update performance and correctness
- **Event Integration**: Collision event forwarding validation

### Benchmark Tests
```bash
# Run collision system benchmarks
./tests/test_scripts/run_collision_tests.sh
./tests/test_scripts/run_collision_benchmark.sh      # Collision performance benchmarks
./tests/test_scripts/run_pathfinder_benchmark.sh     # Pathfinding performance benchmarks
```

For more details on testing, see [TESTING.md](../../tests/TESTING.md).