# PathfinderManager Documentation

## Overview

The PathfinderManager is a high-performance, centralized pathfinding service designed for the Hammer Engine. It provides thread-safe pathfinding capabilities that scale to support 10,000+ AI entities while maintaining 60+ FPS performance. The system features intelligent caching, dynamic obstacle integration, and seamless ThreadSystem integration.

## Architecture

### Design Patterns
- **Singleton Pattern**: Thread-safe singleton with proper lifecycle management
- **Producer-Consumer**: Asynchronous request processing with callback-based results
- **Cache-Aside**: Intelligent path caching with automatic invalidation
- **Event-Driven**: Dynamic obstacle updates from CollisionManager

### Core Features
- **High Performance**: Optimized A* implementation with SIMD-friendly data structures
- **Thread Safety**: Lock-free request queuing with minimal contention
- **Smart Caching**: Path result caching with collision-aware invalidation
- **Dynamic Obstacles**: Real-time integration with CollisionManager changes
- **Priority Scheduling**: WorkerBudget integration for performance-critical requests

## Public API Reference

### Initialization and Lifecycle

#### `static PathfinderManager& Instance()`
Gets the thread-safe singleton instance.
```cpp
PathfinderManager& pathfinder = PathfinderManager::Instance();
```

#### `bool init()`
Initializes the PathfinderManager and subscribes to collision events.
- **Returns**: `true` if successful, `false` otherwise
- **Thread Safety**: Safe to call from any thread

#### `bool isInitialized() const`
Checks if the manager has been properly initialized.

#### `void clean()`
Shuts down the pathfinder and releases all resources.

#### `void prepareForStateTransition()`
Prepares for game state transitions by clearing cached paths and pending requests.

### Request Management

#### Priority Levels
```cpp
enum class Priority : int {
    Critical = 0,  // Player movement, emergency AI
    High     = 1,  // Combat AI, important NPCs
    Normal   = 2,  // General AI movement
    Low      = 3   // Background/idle AI
};
```

#### `void requestPath(EntityID entityId, const Vector2D& start, const Vector2D& goal, PathCallback callback, Priority priority = Priority::Normal)`
Asynchronous pathfinding request with callback-based result delivery.
```cpp
// Request path with callback
PathfinderManager::Instance().requestPath(
    npcId,
    npc.getPosition(),
    targetPosition,
    [npcId](const std::vector<Vector2D>& path, PathfindingResult result) {
        if (result == PathfindingResult::Success) {
            // Apply path to NPC
            NPCManager::Instance().setPath(npcId, path);
        } else {
            // Handle pathfinding failure
            NPCManager::Instance().handlePathfindingFailure(npcId, result);
        }
    },
    PathfinderManager::Priority::High
);
```

#### `PathfindingResult findPathSync(const Vector2D& start, const Vector2D& goal, std::vector<Vector2D>& outPath, Priority priority = Priority::Normal)`
Synchronous pathfinding for immediate results.
```cpp
std::vector<Vector2D> path;
PathfindingResult result = PathfinderManager::Instance().findPathSync(
    startPos, goalPos, path, PathfinderManager::Priority::Critical
);

if (result == PathfindingResult::Success) {
    // Use path immediately
    entity.followPath(path);
}
```

### Grid Configuration

#### `void setCellSize(float cellSize)`
Sets the pathfinding grid resolution.
```cpp
// Higher resolution for precision (slower)
PathfinderManager::Instance().setCellSize(32.0f);

// Lower resolution for performance (faster)
PathfinderManager::Instance().setCellSize(128.0f);
```

#### `void setDiagonalMovement(bool enabled)`
Enables or disables diagonal movement in pathfinding.

#### `void setMaxIterations(int maxIterations)`
Sets the maximum A* iterations to prevent infinite loops.

### Dynamic Weight Fields

#### `void addWeightField(const std::string& name, const Vector2D& center, float radius, float weight)`
Adds temporary movement cost modifiers.
```cpp
// Create danger zone around explosion
PathfinderManager::Instance().addWeightField(
    "explosion_danger",
    explosionCenter,
    100.0f,     // radius
    10.0f       // high cost multiplier
);

// Create slow zone for water
PathfinderManager::Instance().addWeightField(
    "water_slow",
    waterCenter,
    50.0f,      // radius
    2.0f        // moderate cost increase
);
```

#### `void removeWeightField(const std::string& name)`
Removes a named weight field.

#### `void clearWeightFields()`
Removes all temporary weight fields.

### Performance Monitoring

#### `void update()`
Main update loop - call once per frame from game update thread.
- **Performance**: Minimal overhead - primarily for statistics and cache management
- **Thread Safety**: Safe to call from update thread

#### `PathfindingStats getStatistics() const`
Gets performance statistics.
```cpp
auto stats = PathfinderManager::Instance().getStatistics();
GAMEENGINE_INFO("Pathfinding: " + std::to_string(stats.requestsPerSecond) + " req/sec, " +
               std::to_string(stats.cacheHitRate * 100.0f) + "% cache hit rate");
```

## Integration Examples

### AI System Integration
```cpp
// In AIManager - batch pathfinding requests
class AIManager {
private:
    void updatePathfinding() {
        for (auto& entity : m_entities) {
            if (entity.needsNewPath()) {
                PathfinderManager::Instance().requestPath(
                    entity.getId(),
                    entity.getPosition(),
                    entity.getTargetPosition(),
                    [this, entityId = entity.getId()](const std::vector<Vector2D>& path, PathfindingResult result) {
                        handlePathResult(entityId, path, result);
                    },
                    getEntityPriority(entity)
                );
            }
        }
    }

    void handlePathResult(EntityID entityId, const std::vector<Vector2D>& path, PathfindingResult result) {
        auto entity = findEntity(entityId);
        if (!entity) return;

        switch (result) {
            case PathfindingResult::Success:
                entity->setPath(path);
                break;
            case PathfindingResult::NoPathFound:
                entity->handleNoPath();
                break;
            case PathfindingResult::StartBlocked:
                entity->handleBlockedStart();
                break;
            case PathfindingResult::GoalBlocked:
                entity->findAlternativeGoal();
                break;
        }
    }

    PathfinderManager::Priority getEntityPriority(const AIEntity& entity) {
        if (entity.isInCombat()) return PathfinderManager::Priority::High;
        if (entity.isPlayerVisible()) return PathfinderManager::Priority::Normal;
        return PathfinderManager::Priority::Low;
    }
};
```

### Collision Integration
```cpp
// PathfinderManager automatically receives collision events and invalidates cache
// No manual integration required - handled internally

// However, you can add custom obstacle avoidance:
void createDynamicObstacle(const Vector2D& center, float radius) {
    // Add temporary weight field for dynamic obstacle
    PathfinderManager::Instance().addWeightField(
        "dynamic_obstacle_" + std::to_string(obstacleId),
        center,
        radius,
        5.0f  // Make area expensive to traverse
    );

    // Remove after obstacle is gone
    timer.scheduleCallback(obstacleLifetime, [obstacleId]() {
        PathfinderManager::Instance().removeWeightField(
            "dynamic_obstacle_" + std::to_string(obstacleId)
        );
    });
}
```

### Player Movement Integration
```cpp
// High-priority pathfinding for player click-to-move
void handlePlayerMovement(const Vector2D& clickPosition) {
    PathfinderManager::Instance().requestPath(
        playerId,
        player.getPosition(),
        clickPosition,
        [this](const std::vector<Vector2D>& path, PathfindingResult result) {
            if (result == PathfindingResult::Success) {
                player.startMovingAlongPath(path);
            } else {
                // Show "can't move there" indicator
                ui.showInvalidMoveIndicator(clickPosition);
            }
        },
        PathfinderManager::Priority::Critical  // Player input is highest priority
    );
}
```

## Performance Considerations

### Threading Model
- **Request Thread**: Any thread can submit pathfinding requests
- **Worker Threads**: ThreadSystem processes requests in background
- **Callback Thread**: Results delivered on ThreadSystem worker threads
- **Update Thread**: Statistics and cache management on main update thread

### Cache System
- **Automatic Invalidation**: Cache entries invalidated when collision obstacles change
- **Expiration**: Cached paths expire after configurable timeout
- **Memory Management**: LRU eviction prevents unbounded memory growth

### Performance Metrics
- **Target Performance**: 10,000+ entities at 60+ FPS
- **Request Throughput**: 1,000+ pathfinding requests per second
- **Cache Hit Rate**: 70-90% typical hit rate in production scenarios
- **Memory Usage**: ~50KB per 100x100 grid, ~500KB for path cache

### Optimization Guidelines

#### Grid Resolution
```cpp
// For different game types:
PathfinderManager::Instance().setCellSize(32.0f);  // High precision (RTS, tactical)
PathfinderManager::Instance().setCellSize(64.0f);  // Balanced (most games) - DEFAULT
PathfinderManager::Instance().setCellSize(128.0f); // Fast performance (action games)
```

#### Request Prioritization
```cpp
// Use appropriate priorities to ensure smooth gameplay
Priority::Critical  // Player movement, emergency AI (< 1% of requests)
Priority::High      // Combat AI, important NPCs (< 10% of requests)
Priority::Normal    // General AI movement (60-80% of requests)
Priority::Low       // Background AI, decorative NPCs (10-30% of requests)
```

#### Batch Processing
```cpp
// Avoid submitting many requests in single frame
void schedulePathfindingRequests() {
    // Spread requests across multiple frames
    static int frameCounter = 0;
    int requestsThisFrame = std::min(5, m_pendingPathRequests.size());

    for (int i = 0; i < requestsThisFrame; ++i) {
        submitPathRequest(m_pendingPathRequests.front());
        m_pendingPathRequests.pop();
    }
}
```

## Error Handling

### Pathfinding Results
```cpp
enum class PathfindingResult {
    Success,           // Path found successfully
    NoPathFound,       // No valid path exists
    StartBlocked,      // Starting position is blocked
    GoalBlocked,       // Goal position is blocked
    GridNotReady,      // Pathfinding grid not initialized
    MaxIterationsHit,  // A* hit iteration limit
    InvalidInput,      // Invalid start/goal coordinates
    SystemShutdown     // PathfinderManager is shutting down
};
```

### Error Recovery Strategies
```cpp
void handlePathfindingError(EntityID entityId, PathfindingResult result) {
    switch (result) {
        case PathfindingResult::NoPathFound:
            // Try finding nearest reachable position
            requestPathToNearestReachable(entityId);
            break;

        case PathfindingResult::StartBlocked:
            // Move entity to nearest open cell
            moveToNearestOpenPosition(entityId);
            break;

        case PathfindingResult::GoalBlocked:
            // Find alternative goal nearby
            findAlternativeGoal(entityId);
            break;

        case PathfindingResult::MaxIterationsHit:
            // Reduce path distance or increase iteration limit
            requestShorterPath(entityId);
            break;

        default:
            // Fallback: use simple direct movement
            useDirectMovement(entityId);
            break;
    }
}
```

## Testing

### Unit Tests
```bash
# Run pathfinding system tests
./tests/test_scripts/run_pathfinding_tests.sh

# Individual test suites
./bin/debug/pathfinding_system_tests
./bin/debug/pathfinder_manager_tests
```

### Performance Tests
```bash
# Pathfinding performance benchmarks
./tests/test_scripts/run_pathfinder_benchmark.sh
./bin/debug/pathfinder_benchmark

# Collision system benchmarks (separate)
./tests/test_scripts/run_collision_benchmark.sh
./bin/debug/collision_benchmark
```

### Integration Tests
- **AI Integration**: Validates batch pathfinding with AIManager
- **Collision Integration**: Tests dynamic obstacle response
- **Threading**: Validates thread safety under load

## Advanced Features

### Custom Heuristics
```cpp
// The system uses optimized A* with Manhattan + Diagonal heuristic
// Grid automatically optimizes for different movement patterns
```

### Memory Management
- **Pool Allocation**: Internal path storage uses object pools
- **Cache Management**: Automatic cache size management prevents memory leaks
- **Grid Reuse**: Pathfinding grid reused across state transitions

### Debug Features
```cpp
// Enable detailed pathfinding logging
PathfinderManager::Instance().setVerboseLogging(true);

// Get detailed statistics
auto stats = PathfinderManager::Instance().getDetailedStatistics();
for (const auto& [priority, count] : stats.requestsByPriority) {
    GAMEENGINE_DEBUG("Priority " + std::to_string(static_cast<int>(priority)) +
                    ": " + std::to_string(count) + " requests");
}
```

For more information on pathfinding algorithms and grid implementation, see [PathfindingSystem.md](../ai/PathfindingSystem.md).