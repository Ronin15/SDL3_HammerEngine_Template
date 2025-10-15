# Collision System Documentation

## Overview

The Hammer Engine collision system is a high-performance, 2D collision detection framework built around hierarchical spatial hash optimization and AABB (Axis-Aligned Bounding Box) primitives. The system is designed to handle 10,000+ collision bodies efficiently while providing precise collision detection, trigger events, and seamless integration with AI pathfinding. The two-tier hierarchical grid adapts dynamically to entity density for optimal performance.

## Architecture

### Core Components

#### AABB (Axis-Aligned Bounding Box)
The fundamental collision primitive used throughout the system.
```cpp
namespace HammerEngine {
    struct AABB {
        Vector2D center;    // World center position
        Vector2D halfSize;  // Half-width and half-height

        // Constructors
        AABB() = default;
        AABB(float cx, float cy, float hw, float hh);

        // Boundary methods
        float left() const;
        float right() const;
        float top() const;
        float bottom() const;

        // Collision detection
        bool intersects(const AABB& other) const;
        bool contains(const Vector2D& point) const;
        Vector2D closestPoint(const Vector2D& point) const;
    };
}
```

#### HierarchicalSpatialHash
High-performance two-tier hierarchical spatial partitioning system for broad-phase collision detection, optimized for 10K+ entities.
```cpp
class HierarchicalSpatialHash {
public:
    // Configuration constants
    static constexpr float COARSE_CELL_SIZE = 128.0f;    // Region-level culling
    static constexpr float FINE_CELL_SIZE = 32.0f;       // Precise collision detection
    static constexpr float MOVEMENT_THRESHOLD = 8.0f;    // Static body update threshold
    static constexpr size_t REGION_ACTIVE_THRESHOLD = 16; // Dynamic subdivision threshold

    explicit HierarchicalSpatialHash();

    // Core operations
    void insert(size_t bodyIndex, const AABB& aabb);
    void remove(size_t bodyIndex);
    void update(size_t bodyIndex, const AABB& oldAABB, const AABB& newAABB);
    void clear();

    // Query operations
    void queryRegion(const AABB& area, std::vector<size_t>& outBodyIndices) const;
    void queryRegionBounds(float minX, float minY, float maxX, float maxY,
                          std::vector<size_t>& outBodyIndices) const;

    // Batch operations for high performance
    void insertBatch(const std::vector<std::pair<size_t, AABB>>& bodies);
    void updateBatch(const std::vector<std::tuple<size_t, AABB, AABB>>& updates);
};
```

**Two-Tier Architecture:**
- **Coarse Grid (128 units)**: Fast region-level culling, eliminates distant bodies
- **Fine Grid (32 units)**: Precise collision detection within active regions
- **Adaptive Subdivision**: Fine grid only created when region exceeds 16 bodies
- **Performance**: Zero allocation during frame processing, vectorization-friendly SOA layout

#### CollisionBody
Complete collision object with type information and layer filtering.
```cpp
struct CollisionBody {
    EntityID id;
    AABB aabb;
    BodyType type;              // STATIC, KINEMATIC, DYNAMIC, TRIGGER
    uint32_t layerMask;         // What layer this body belongs to
    uint32_t collideMask;       // What layers this body collides with
    bool enabled;
    bool isTrigger;
    Vector2D velocity;          // For kinematic resolution
    HammerEngine::TriggerTag triggerTag;
};
```

### Collision Detection Pipeline

#### 1. Broad Phase (Hierarchical Spatial Hash)
```cpp
// Insert entities into hierarchical spatial hash
hierarchicalHash.insert(bodyIndex, entityAABB);

// Query for potential collision pairs using two-tier system
// 1. Coarse grid (128 units) - Fast region identification
// 2. Fine grid (32 units) - Precise body lookup within active regions
std::vector<size_t> candidates;
hierarchicalHash.queryRegion(entityAABB, candidates);

// Optimized bounds-based query (avoids AABB construction)
hierarchicalHash.queryRegionBounds(minX, minY, maxX, maxY, candidates);
```

#### 2. Narrow Phase (AABB Intersection)
```cpp
// Test actual intersection
if (aabb1.intersects(aabb2)) {
    // Generate collision info
    CollisionInfo collision{entityA, entityB, contactPoint, normal};
    handleCollision(collision);
}
```

#### 3. Response (Event Generation)
```cpp
// Trigger collision events
EventManager::Instance().triggerCollision(collisionInfo);

// Handle trigger events
if (body.isTrigger) {
    EventManager::Instance().triggerWorldTrigger(triggerEvent);
}
```

## Body Types and Behaviors

### Static Bodies
- **Purpose**: Immovable world geometry (walls, buildings, terrain)
- **Performance**: Cached in separate spatial hash for optimal query speed
- **Usage**: Set once during world initialization
```cpp
// Add static wall
CollisionManager::Instance().addBody(
    wallId,
    AABB(100.0f, 50.0f, 10.0f, 100.0f),  // x, y, halfWidth, halfHeight
    BodyType::STATIC
);
```

### Kinematic Bodies
- **Purpose**: Script/AI controlled movement (NPCs, moving platforms)
- **Physics**: Position controlled by game logic, not physics
- **Collision**: Generates collision events but doesn't bounce
```cpp
// Add kinematic NPC
CollisionManager::Instance().addBody(
    npcId,
    AABB(npc.x, npc.y, 16.0f, 16.0f),
    BodyType::KINEMATIC
);

// Update position
CollisionManager::Instance().setKinematicPose(npcId, newPosition);
```

### Dynamic Bodies
- **Purpose**: Physics-driven objects (reserved for future physics integration)
- **Current Status**: Framework exists, full physics implementation pending

### Trigger Bodies
- **Purpose**: Non-solid collision areas that generate events
- **Types**: Water zones, damage areas, teleport portals, pickup areas
```cpp
// Create water trigger
EntityID waterTrigger = CollisionManager::Instance().createTriggerArea(
    AABB(riverCenter.x, riverCenter.y, 50.0f, 200.0f),
    HammerEngine::TriggerTag::Water,
    CollisionLayer::Layer_Environment,
    CollisionLayer::Layer_Player | CollisionLayer::Layer_NPC
);
```

## Collision Layers and Filtering

### Layer System
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

### Layer Configuration
```cpp
// Player collides with NPCs and environment, but not other players
CollisionManager::Instance().setBodyLayer(
    playerId,
    CollisionLayer::Layer_Player,                    // This body's layer
    CollisionLayer::Layer_NPC | CollisionLayer::Layer_Environment  // What it collides with
);

// Projectile hits everything except triggers
CollisionManager::Instance().setBodyLayer(
    projectileId,
    CollisionLayer::Layer_Projectile,
    CollisionLayer::Layer_All & ~CollisionLayer::Layer_Trigger
);
```

## Hierarchical Spatial Hash Optimization

### Performance Characteristics
- **Coarse Cell Size**: 128.0f units (optimal for region-level culling)
- **Fine Cell Size**: 32.0f units (precise collision detection within active regions)
- **Movement Threshold**: 8.0f units (static body update optimization)
- **Subdivision Threshold**: 16 bodies per region (adaptive fine grid creation)
- **Query Complexity**: O(k) where k is entities in queried regions/cells
- **Memory Usage**: ~24 bytes per entity + hierarchical hash table overhead
- **Optimized For**: 10,000+ entities with 20-30% performance improvement

### Adaptive Subdivision System
```cpp
// Region automatically subdivides when body count exceeds threshold
struct Region {
    CoarseCoord coord;
    size_t bodyCount;
    bool hasFineSplit;  // true when bodyCount > REGION_ACTIVE_THRESHOLD (16)

    // Fine subdivision (only created for high-density regions)
    std::unordered_map<GridKey, std::vector<size_t>> fineCells;

    // Coarse body list (used for low-density regions)
    std::vector<size_t> bodyIndices;
};

// Low density region (≤16 bodies): Single coarse cell lookup
// High density region (>16 bodies): Fine grid subdivision for precision
```

### Two-Tier Coordinate System
```cpp
// Coarse grid coordinate calculation (128 unit regions)
CoarseCoord getCoarseCoord(const AABB& aabb) const {
    return {
        static_cast<int32_t>(std::floor(aabb.center.x / COARSE_CELL_SIZE)),
        static_cast<int32_t>(std::floor(aabb.center.y / COARSE_CELL_SIZE))
    };
}

// Fine grid coordinate calculation (32 unit cells within regions)
FineCoord getFineCoord(const AABB& aabb, const CoarseCoord& region) const {
    Vector2D regionOrigin(region.x * COARSE_CELL_SIZE, region.y * COARSE_CELL_SIZE);
    return {
        static_cast<int32_t>(std::floor((aabb.center.x - regionOrigin.x) / FINE_CELL_SIZE)),
        static_cast<int32_t>(std::floor((aabb.center.y - regionOrigin.y) / FINE_CELL_SIZE))
    };
}
```

### Batch Operations for High Performance
```cpp
// Batch insertion for initial world setup
std::vector<std::pair<size_t, AABB>> bodies;
for (auto& entity : worldEntities) {
    bodies.emplace_back(entity.index, entity.aabb);
}
hierarchicalHash.insertBatch(bodies);

// Batch updates for dynamic entities
std::vector<std::tuple<size_t, AABB, AABB>> updates;
for (auto& entity : dynamicEntities) {
    updates.emplace_back(entity.index, entity.oldAABB, entity.newAABB);
}
hierarchicalHash.updateBatch(updates);
```

## Trigger System

### Trigger Tags
```cpp
enum class TriggerTag {
    None,
    Water,       // Movement speed reduction, visual effects
    Fire,        // Damage over time
    Ice,         // Reduced friction, sliding
    Portal,      // Teleportation between areas
    Checkpoint,  // Save game state
    Pickup,      // Item collection
    Obstacle     // AI pathfinding penalties
};
```

### Trigger Events
```cpp
struct WorldTriggerEvent {
    EntityID triggerId;
    EntityID entityId;
    HammerEngine::TriggerTag tag;
    Vector2D triggerCenter;
    bool isEntering;  // true = enter, false = exit
    float deltaTime;
};
```

### Cooldown System
```cpp
// Prevent trigger spam
CollisionManager::Instance().setTriggerCooldown(triggerId, 2.0f);
CollisionManager::Instance().setDefaultTriggerCooldown(1.0f);

// Cooldown tracking (internal)
std::unordered_map<EntityID, float> m_triggerCooldownUntil;
```

## Performance Optimization

### Batch Processing
```cpp
// Batch kinematic updates for AI systems
std::vector<CollisionManager::KinematicUpdate> updates;
for (const auto& entity : aiEntities) {
    updates.emplace_back(entity.id, entity.position, entity.velocity);
}
CollisionManager::Instance().updateKinematicBatch(updates);
```

### Cache Management
```cpp
// Static body cache optimization
class BroadphaseCache {
public:
    void invalidateStaticCache();  // Call when static geometry changes
    void resetFrame();             // Call each frame for dynamic queries

private:
    std::vector<EntityID> m_staticCache;
    bool m_staticCacheValid;
};
```

### Movement Threshold Optimization
```cpp
// Only update spatial hash if static entity moved significantly (8.0f units)
// Note: Dynamic bodies are rebuilt every frame, so threshold only applies to static bodies
bool hasMovedSignificantly(const AABB& oldAABB, const AABB& newAABB) const {
    float dx = std::abs(newAABB.center.x - oldAABB.center.x);
    float dy = std::abs(newAABB.center.y - oldAABB.center.y);
    return (dx > MOVEMENT_THRESHOLD) || (dy > MOVEMENT_THRESHOLD); // 8.0f units
}
```

### Zero-Allocation Frame Processing
```cpp
// Persistent buffers eliminate per-query allocations (20-30% performance boost)
class HierarchicalSpatialHash {
private:
    // Reusable buffers for query operations (single-threaded safe)
    mutable std::unordered_set<size_t> m_tempSeenBodies;
    mutable std::vector<CoarseCoord> m_tempQueryRegions;
    mutable std::vector<FineCoord> m_tempQueryFineCells;

    // These are cleared and reused each query - zero allocations during steady state
};
```

## Integration Examples

### World Generation Integration
```cpp
void WorldState::loadLevel(const std::string& levelFile) {
    // Load world data
    WorldManager::Instance().loadWorld(levelFile);

    // Build collision system from world
    CollisionManager::Instance().rebuildStaticFromWorld();

    // Create environmental triggers
    size_t waterTriggers = CollisionManager::Instance().createTriggersForWaterTiles();
    size_t obstacleTriggers = CollisionManager::Instance().createTriggersForObstacles();

    GAMEENGINE_INFO("Level loaded: " + std::to_string(waterTriggers) +
                   " water triggers, " + std::to_string(obstacleTriggers) +
                   " obstacle triggers created");
}
```

### AI Integration
```cpp
// Check for obstacles before moving
bool AIBehavior::canMoveToPosition(const Vector2D& targetPos) {
    AABB testAABB(targetPos.x, targetPos.y, m_entity->getHalfWidth(), m_entity->getHalfHeight());

    std::vector<EntityID> obstacles;
    CollisionManager::Instance().queryArea(testAABB, obstacles);

    for (EntityID obstacleId : obstacles) {
        if (CollisionManager::Instance().isDynamic(obstacleId) ||
            CollisionManager::Instance().isKinematic(obstacleId)) {
            continue; // Ignore dynamic entities
        }

        // Check if this is a blocking obstacle
        if (!CollisionManager::Instance().isTrigger(obstacleId)) {
            return false; // Blocked by static geometry
        }
    }

    return true;
}
```

### Player Movement Integration
```cpp
void Player::handleMovementInput(const Vector2D& inputDirection, float deltaTime) {
    Vector2D desiredPosition = m_position + inputDirection * m_speed * deltaTime;

    // Update collision body
    CollisionManager::Instance().setKinematicPose(m_entityId, desiredPosition);
    CollisionManager::Instance().setVelocity(m_entityId, inputDirection * m_speed);

    // Position will be resolved by collision system
    m_position = desiredPosition;
}

// Listen for collision events
void Player::onCollisionEvent(const CollisionEvent& event) {
    if (event.entityA == m_entityId || event.entityB == m_entityId) {
        EntityID otherId = (event.entityA == m_entityId) ? event.entityB : event.entityA;

        // Handle collision with other entity
        if (CollisionManager::Instance().isTrigger(otherId)) {
            handleTriggerCollision(otherId);
        } else {
            handleSolidCollision(otherId);
        }
    }
}
```

## Testing and Debugging

### Performance Testing
```cpp
// Stress test with many entities
void CollisionStressTest() {
    const int entityCount = 10000;
    const float worldSize = 1000.0f;

    auto start = std::chrono::high_resolution_clock::now();

    // Insert entities
    for (int i = 0; i < entityCount; ++i) {
        float x = (rand() % 1000) - 500.0f;
        float y = (rand() % 1000) - 500.0f;
        CollisionManager::Instance().addBody(
            i, AABB(x, y, 8.0f, 8.0f), BodyType::KINEMATIC
        );
    }

    auto insertTime = std::chrono::high_resolution_clock::now();

    // Query test
    for (int i = 0; i < 1000; ++i) {
        float x = (rand() % 1000) - 500.0f;
        float y = (rand() % 1000) - 500.0f;

        std::vector<EntityID> results;
        CollisionManager::Instance().queryArea(AABB(x, y, 50.0f, 50.0f), results);
    }

    auto queryTime = std::chrono::high_resolution_clock::now();

    // Report performance
    auto insertMs = std::chrono::duration_cast<std::chrono::milliseconds>(insertTime - start).count();
    auto queryMs = std::chrono::duration_cast<std::chrono::milliseconds>(queryTime - insertTime).count();

    GAMEENGINE_INFO("Collision Performance: " + std::to_string(entityCount) +
                   " entities inserted in " + std::to_string(insertMs) + "ms, " +
                   "1000 queries in " + std::to_string(queryMs) + "ms");
}
```

### Debug Visualization
```cpp
void CollisionDebugRenderer::render(SDL_Renderer* renderer, const Camera& camera) {
    // Render all collision bodies
    CollisionManager::Instance().forEachBody([&](EntityID id, const CollisionBody& body) {
        SDL_Color color;
        switch (body.type) {
            case BodyType::STATIC:    color = {255, 0, 0, 128};   break; // Red
            case BodyType::KINEMATIC: color = {0, 255, 0, 128};   break; // Green
            case BodyType::DYNAMIC:   color = {0, 0, 255, 128};   break; // Blue
            case BodyType::TRIGGER:   color = {255, 255, 0, 128}; break; // Yellow
        }

        renderAABB(renderer, body.aabb, color, camera);
    });

    // Render spatial hash grid (debug only)
    if (m_showSpatialGrid) {
        renderSpatialHashGrid(renderer, camera);
    }
}
```

### Unit Tests
```bash
# Run collision system tests
./tests/test_scripts/run_collision_tests.sh

# Individual test executables
./bin/debug/collision_system_tests      # AABB and hierarchical spatial hash tests
./bin/debug/collision_config_tests      # Configuration and setup tests

# Performance benchmarks
./tests/test_scripts/run_collision_benchmark.sh
./bin/debug/collision_benchmark         # Hierarchical hash performance tests
```

## Common Patterns

### Dynamic Obstacle Avoidance
```cpp
// Create temporary collision bodies for dynamic obstacles
void createTemporaryObstacle(const Vector2D& center, float radius, float duration) {
    EntityID obstacleId = UniqueID::generate();

    CollisionManager::Instance().addBody(
        obstacleId,
        AABB(center.x, center.y, radius, radius),
        BodyType::STATIC
    );

    // Schedule removal
    Timer::schedule(duration, [obstacleId]() {
        CollisionManager::Instance().removeBody(obstacleId);
    });
}
```

### Area-of-Effect Collision
```cpp
// Check all entities in explosion radius
void handleExplosion(const Vector2D& center, float radius, float damage) {
    AABB explosionArea(center.x, center.y, radius, radius);

    std::vector<EntityID> affectedEntities;
    CollisionManager::Instance().queryArea(explosionArea, affectedEntities);

    for (EntityID entityId : affectedEntities) {
        Vector2D entityCenter;
        if (CollisionManager::Instance().getBodyCenter(entityId, entityCenter)) {
            float distance = (entityCenter - center).magnitude();
            if (distance <= radius) {
                float damageMultiplier = 1.0f - (distance / radius);
                applyDamage(entityId, damage * damageMultiplier);
            }
        }
    }
}
```

### Trigger Chains
```cpp
// Chain multiple triggers for complex interactions
class TriggerChain {
public:
    void addTrigger(EntityID triggerId, std::function<void()> callback) {
        m_triggerCallbacks[triggerId] = callback;

        EventManager::Instance().subscribe<WorldTriggerEvent>(
            [this](const WorldTriggerEvent& event) {
                if (event.isEntering && m_triggerCallbacks.count(event.triggerId)) {
                    m_triggerCallbacks[event.triggerId]();
                }
            }
        );
    }

private:
    std::unordered_map<EntityID, std::function<void()>> m_triggerCallbacks;
};
```

For more information on collision management and API usage, see [CollisionManager.md](../managers/CollisionManager.md).