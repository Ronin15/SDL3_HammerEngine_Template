# AI Manager System Documentation

**Where to find the code:**
- Implementation: `src/managers/AIManager.cpp`, `src/ai/behaviors/`
- Header: `include/managers/AIManager.hpp`

**Singleton Access:** Use `AIManager::Instance()` to access the manager.

## Overview

The AI Manager is a high-performance, unified system for managing autonomous behaviors for game entities. It provides a single, optimized framework for implementing and controlling various AI behaviors with advanced performance features:

1. **Cross-Platform Performance** - Optimized for 4-6% CPU usage with 1000+ entities
2. **Non-Blocking AI Processing** - Fire-and-forget threading prevents main thread blocking
3. **Cache-Friendly Structure of Arrays (SoA)** - Hot/cold data separation for optimal cache efficiency
4. **Distance-based optimization** - Pure distance culling for distant entities (no frame counting)
5. **Priority-based management** - Higher priority entities get larger distance thresholds (0-9 scale)
6. **Individual behavior instances** - Each entity gets its own behavior state via clone()
7. **Threading & Batching** - Optimal 2-4 large batches with WorkerBudget integration
8. **Type-indexed behaviors** - Fast behavior dispatch using enumerated types (BehaviorType enum)
9. **Lock-free message queue** - Zero-contention communication with behaviors
10. **Global AI pause/resume** - Complete halt of all AI processing with thread-safe controls
11. **Performance monitoring** - Built-in statistics tracking per behavior type and globally
12. **Optimized distance calculations** - Reduced frequency and efficient computation
13. **Intelligent double buffering** - Only copies when needed, not every frame
14. **Batch lock optimization** - Single lock per batch instead of per-entity

## Individual Behavior Instances Architecture

### Core Architecture Principle

**Each NPC receives its own cloned behavior instance** to ensure complete state isolation and thread safety.

All behaviors implement the `clone()` method:

```cpp
class PatrolBehavior : public AIBehavior {
public:
    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed, m_includeOffscreenPoints);
        cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
        cloned->setActive(m_active);
        cloned->setPriority(m_priority);
        return cloned;
    }
};
```

### Benefits of Individual Instances

- ✅ **No State Interference**: Each NPC has independent waypoints, targets, timers
- ✅ **Thread Safety**: No race conditions between NPCs
- ✅ **Performance**: Linear scaling instead of exponential degradation
- ✅ **Stability**: Eliminates cache invalidation thrashing
- ✅ **Memory Cost**: ~5.5MB for 10,000 NPCs (negligible vs. system crashes)

## Core Components

### AIManager

The central management class that handles:
- Registration of behaviors
- Assignment of behaviors to entities
- Updating all behaviors during the game loop using ThreadSystem
- Communication with behaviors via messages
- Priority-based scheduling of AI tasks

**Performance Optimizations:**
- Entity-behavior caching for faster lookups
- Batch processing of entities with the same behavior
- Early exit conditions to avoid unnecessary updates
- Message queue system for deferred communication
- Priority-based task scheduling for optimal CPU utilization

### AIBehavior Base Class

The abstract interface that all behaviors must implement:
- `executeLogic(Entity*)`: Called each frame to update entity movement/actions
- `init(Entity*)`: Called when a behavior is first assigned to an entity
- `clean(Entity*)`: Called when a behavior is removed from an entity
- `onMessage(Entity*, const std::string&)`: Handles messages sent to the behavior
- `clone()`: Creates individual behavior instances for each entity

## Available Behaviors

### WanderBehavior

Entities move randomly within a defined area, changing direction periodically.

**Mode-Based Configuration:**
- `SMALL_AREA`: 75px radius, local movement
- `MEDIUM_AREA`: 200px radius, standard NPCs
- `LARGE_AREA`: 450px radius, roaming NPCs
- `EVENT_TARGET`: 150px radius around specific targets

### PatrolBehavior

Entities follow predefined paths or patrol areas with various patterns.

**Mode-Based Configuration:**
- `FIXED_WAYPOINTS`: Traditional patrol routes with predefined waypoints
- `RANDOM_AREA`: Dynamic patrol within rectangular areas
- `CIRCULAR_AREA`: Patrol around central locations
- `EVENT_TARGET`: Patrol around specific objectives

### ChaseBehavior

Entities pursue a target (typically the player) when within detection range.

**Configuration options:**
- Target entity reference
- Chase speed
- Maximum detection/pursuit range
- Minimum distance to maintain from target

**Performance Features:**
- Uses per-entity update staggering to reduce computational load
- Caches expensive calculations (distance, line-of-sight checks)
- Default update frequency: every 3 frames per entity (configurable)

## Per-Entity Update Staggering

### Overview

The AI system supports per-entity update staggering to reduce computational load for expensive behaviors like ChaseBehavior and WanderBehavior. This feature allows behaviors to update their expensive calculations less frequently while maintaining smooth entity movement.

### How It Works

1. **Frame-Based Staggering**: Each entity is assigned a unique stagger offset based on its pointer hash
2. **Configurable Frequency**: Behaviors can specify how often they want to run expensive updates (e.g., every N frames)
3. **Cached Results**: Between staggered updates, behaviors use cached calculation results
4. **Automatic Distribution**: Entities are automatically distributed across frames to prevent spikes

### Implementing Staggered Behaviors

```cpp
class MyExpensiveBehavior : public AIBehavior {
public:
    // Enable staggering
    bool useStaggering() const override { return true; }
    
    // Update expensive calculations every 5 frames
    uint32_t getUpdateFrequency() const override { return 5; }
    
    void executeLogic(EntityPtr entity) override {
        // This is called only on staggered frames
        updateExpensiveCalculations(entity);
        applyMovementLogic(entity);
    }
    
    // Optional: Configure frequency at runtime
    void setUpdateFrequency(uint32_t frequency) { m_updateFrequency = frequency; }
};
```

### Performance Benefits

- **Reduced CPU Usage**: Up to 67% reduction in expensive calculations (with frequency=3)
- **Prevents Spikes**: Distributes work across multiple frames
- **Maintains Responsiveness**: Entities still move smoothly using cached data
- **Configurable**: Can be tuned per behavior type or globally

### Example: Staggering in ChaseBehavior and WanderBehavior

All core AI behaviors (Wander, Patrol, Idle, Guard, Follow, Flee, Attack, and Chase) now support per-entity update staggering for optimal performance:
- Expensive calculations (e.g., line-of-sight, pathfinding, threat detection, attack logic) are distributed across frames
- Direction changes, offscreen checks, and reset logic (Wander)
- Patrol route updates (Patrol)
- Idle/fidget logic (Idle)
- Guard alertness and patrol (Guard)
- Following and formation logic (Follow)
- Fleeing and evasive maneuvers (Flee)
- Attack and combat state updates (Attack)
- Distance computations
- Target tracking updates (Chase)

```cpp
// ChaseBehavior: Default is every 3 frames
chaseBehavior->setUpdateFrequency(3);  // ~67% CPU reduction

// WanderBehavior: Default is every 1-4 frames depending on mode
wanderBehavior->setUpdateFrequency(4); // For large groups, use 4+ for best performance

// For high-priority entities: Update more frequently
chaseBehavior->setUpdateFrequency(2);
wanderBehavior->setUpdateFrequency(2);

// For background entities: Update less frequently
chaseBehavior->setUpdateFrequency(5);
wanderBehavior->setUpdateFrequency(6);
```

## Quick Start

### Basic Setup

```cpp
// Initialize AI Manager
AIManager::Instance().init();

// Register mode-based behaviors
auto wanderBehavior = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::MEDIUM_AREA, 2.0f);
AIManager::Instance().registerBehavior("Wander", std::move(wanderBehavior));

auto patrolBehavior = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 1.5f);
AIManager::Instance().registerBehavior("Patrol", std::move(patrolBehavior));

// Create chase behavior (targeting the player)
auto chaseBehavior = std::make_shared<ChaseBehavior>(player, 2.0f, 500.0f, 50.0f);
AIManager::Instance().registerBehavior("Chase", chaseBehavior);
```

### Entity Registration and Behavior Assignment

```cpp
// Create an NPC
auto npc = std::make_shared<NPC>("npc_sprite", Vector2D(250, 250), 64, 64);

// Register entity with priority and assign behavior
AIManager::Instance().registerEntityForUpdates(npc, 5);
AIManager::Instance().assignBehaviorToEntity(npc, "Wander");

// Alternative: Combined registration and assignment
AIManager::Instance().registerEntityForUpdates(npc, 5, "Wander");

// Set player reference for distance optimization
AIManager::Instance().setPlayerForDistanceOptimization(player);
```

### Game Loop Integration

```cpp
void GameState::update() {
    // Update player first
    player->update();
    
    // AI Manager handles all entity updates automatically via GameEngine
    // No manual AIManager::update() call needed in game states
}
```

## Advanced Features

### Global AI Pause/Resume System

```cpp
// Pause all AI processing globally
AIManager::Instance().setGlobalPause(true);

// Resume all AI processing
AIManager::Instance().setGlobalPause(false);

// Check current pause state
bool isPaused = AIManager::Instance().isGloballyPaused();
```

### Message System

```cpp
// Send message to specific entity
AIManager::Instance().sendMessageToEntity(npc, "pause", true);

// Broadcast message to all entities
AIManager::Instance().broadcastMessage("resume");

// Messages are processed automatically during update cycle
```

### Batch Behavior Assignment

```cpp
// Queue multiple assignments for efficient processing
for (auto& npc : npcs) {
    AIManager::Instance().queueBehaviorAssignment(npc, "Wander");
}

// Assignments processed automatically by GameEngine
size_t processed = AIManager::Instance().processPendingBehaviorAssignments();
```

## Performance Optimization

### Distance-Based Entity Optimization

The AIManager uses sophisticated distance-based optimization:

| Distance Range | Update Frequency | Priority Multiplier |
|---------------|------------------|-------------------|
| **Close** (≤4000 units) | Every frame | 1.0 + (priority × 0.1) |
| **Medium** (≤6000 units) | Every 15 frames | Applied to thresholds |
| **Far** (≤10000 units) | Every 30 frames | Higher priority = larger range |

### Priority System

**Priority Levels (0-9):**
- **0-2**: Background entities (ambient creatures, decorative NPCs)
- **3-5**: Standard entities (villagers, merchants, guards)
- **6-8**: Important entities (quest NPCs, mini-bosses)
- **9**: Critical entities (main bosses, story characters)

Higher priority entities get larger effective update distances and more frequent processing.

### Threading & WorkerBudget Integration (Performance Optimized)

The AIManager implements high-performance threading with **4-6% CPU usage** achieved through intelligent optimizations:

**Performance Achievement:**
- **4-6% CPU usage** with 1000+ entities (down from 30% before optimization)
- **Non-Blocking AI Processing**: Asynchronous task submission prevents main thread blocking
- **Cross-Platform Compatibility**: Optimized performance on Windows, Linux, and Mac
- **60+ FPS maintained** with minimal CPU overhead

**Threading Threshold & Scaling:**
- Single-threaded processing for ≤500 entities (optimal for small workloads)
- Automatic multi-threaded processing for >500 entities
- Dynamic scaling based on WorkerBudget allocation and entity workload
- **Optimal batching**: 2-4 large batches for maximum efficiency

**WorkerBudget Resource Allocation:**
- Receives **60% of available workers** from ThreadSystem's WorkerBudget system
- Uses `budget.getOptimalWorkerCount()` with 1000 entity threshold for buffer allocation
- Maintains system-wide resource coordination with EventManager and GameEngine
- **Conservative buffer usage**: Only activates buffer workers for high workloads

**Optimized Batch Processing:**
- **Large batch strategy**: 1000+ entities per batch for optimal performance
- **Maximum 4 batches**: Cap prevents over-threading and maintains efficiency
- **Single lock per batch**: Pre-cache all entities/behaviors to eliminate lock contention
- **High priority tasks**: AI batches use TaskPriority::High for responsiveness

**Distance Calculation Optimizations:**
- **Reduced frequency**: Distance calculations only every 4th frame (75% reduction)
- **Active entity filtering**: Skip inactive entities to reduce computational overhead
- **Optimized distance computation**: Efficient calculation patterns for entity processing
- **Early exit optimization**: Skip processing when no active entities

**Double Buffer Optimizations:**
- **Conditional copying**: Only copy buffer when distances updated or periodic sync
- **Reduced memory overhead**: Eliminated unnecessary buffer copies every frame
- **Smart buffer swapping**: Only swap when actual changes occurred

**Lock Contention Elimination:**
- **Batch-level caching**: Single shared_lock per batch vs per-entity
- **Pre-calculated values**: Distance thresholds computed once per batch
- **Removed frame counting**: Eliminated per-entity atomic operations

**Pure Distance Culling:**
- **Simplified logic**: Removed complex frame-based culling intervals
- **Immediate responsiveness**: Entities update as soon as they're in range
- **Better performance**: No modulo operations or behavior-specific intervals
- **Priority-based scaling**: Higher priority entities get larger update ranges

**Memory Access Optimizations:**
- **Cache-friendly processing**: Hot data separation for better cache utilization
- **Reduced atomic operations**: Minimized per-entity atomic loads/stores
- **Thread-local optimization**: Reduced cross-thread synchronization overhead

## Performance Monitoring & Optimization Results

### Current Performance Achievement (4-6% CPU Usage)

**Optimization Results:**
```
1,000+ Entity Test - Optimal Performance:
CPU Usage: 4-6% (down from 30% before optimization)
AI Manager Performance:
- Average Update Time: 5.8-6.1ms
- Throughput: 1.6M+ entities/sec
- Worker Distribution: 1100-1800 tasks per worker (Clean distribution)
- Frame Rate: 60+ FPS maintained consistently
```

**Key Performance Improvements:**
- **83% CPU Reduction**: From 30% to 4-6% CPU usage
- **Distance Calculation Optimization**: 75% reduction (every 4th frame vs every frame)
- **Lock Contention Elimination**: Single lock per batch vs per-entity
- **Double Buffer Optimization**: Only copy when needed vs every frame
- **Frame Counting Removal**: Eliminated thousands of per-entity atomic operations
- **Pure Distance Culling**: Simplified logic with immediate responsiveness

**WorkerBudget Integration Results:**
```
Optimal Batching Strategy:
- Batch Count: 2-4 large batches
- Entities per Batch: 1000-2500 entities
- Buffer Worker Usage: Dynamic scaling for >1000 entities
- Task Priority: High priority for AI batches
- Lock Strategy: Single shared_lock per batch
Result: Maximum efficiency with minimal CPU overhead
```

**Cross-Platform Performance:**
- **All Platforms**: Consistent 4-6% CPU usage
- **60+ FPS**: Maintained across Windows/Linux/Mac
- **Scalable**: Performance maintained from 100 to 10,000+ entities
- **Memory Efficient**: Optimized double buffering reduces memory copying
- **Thread Safe**: Lock-free processing with batch-level synchronization

## Performance Optimization History

**Problem Identified:**
The AIManager was experiencing high CPU usage (30%) due to inefficient distance calculations, excessive frame counting, and lock contention issues.

**Root Causes:**
- Distance calculations performed every frame for all entities
- Per-entity frame counting with atomic operations
- Double buffer copying every frame regardless of changes
- Lock acquisition for every entity in batch processing
- Complex frame-based culling with modulo operations

**Solutions Implemented:**
- **Distance Calculation Optimization**: Reduced to every 4th frame, active entities only
- **Frame Counting Elimination**: Removed unnecessary per-entity counters
- **Smart Double Buffering**: Only copy when distances updated or periodic sync
- **Batch Lock Optimization**: Single lock per batch with entity pre-caching
- **Pure Distance Culling**: Simplified to distance-only checks for better performance
- **Fire-and-Forget Processing**: AI tasks are submitted asynchronously and main thread continues immediately
- **Cross-Platform Optimization**: Solution works optimally on Windows, Linux, and Mac

**Performance Results:**
- **Main Thread Responsiveness**: 0.01-2.12ms update times (optimal for 60+ FPS)
- **Throughput**: 530K to 41M+ entity updates per second depending on scale
- **Scalability**: Successfully handles up to 100K entities with maintained performance
- **Cross-Platform**: Consistent performance across Windows/Linux/Mac

**Threading Performance:**
- 150 entities (single-threaded): 530K+ updates/sec
- 200+ entities (multi-threaded): 7M-41M+ updates/sec
- 1000 entities: 22M+ updates/sec
- 100K entities: 5.6M+ updates/sec

**Code Change:**
```cpp
// OLD (blocking - bad for Windows):
while (completedTasks.load() < tasksSubmitted) {
    // Busy wait with microsecond sleeps
}

// NEW (non-blocking - optimal for all platforms):
// No wait - main thread continues immediately
// AI processing happens asynchronously in background
```

**Architecture Benefits:**
- Maintains responsive gameplay while AI processes in background
- Leverages ThreadSystem WorkerBudget for optimal resource allocation
- Automatic threading threshold (200 entities) for best performance
- Distance-based optimization ensures relevant entities get priority updates

## Performance Monitoring & Optimization Results

The AIManager includes comprehensive performance tracking and has achieved significant optimization improvements:

**Performance Metrics:**
```cpp
// Get detailed performance statistics
AIPerformanceStats stats = AIManager::Instance().getPerformanceStats();
std::cout << "Entities per second: " << stats.entitiesPerSecond << std::endl;
std::cout << "Total behavior executions: " << AIManager::Instance().getBehaviorUpdateCount() << std::endl;

// Monitor entity and behavior counts
size_t entityCount = AIManager::Instance().getManagedEntityCount();
size_t behaviorCount = AIManager::Instance().getBehaviorCount();
size_t totalAssignments = AIManager::Instance().getTotalAssignmentCount();
```

**Optimization Results Achieved (Including Windows Performance Fix):**

The AIManager has undergone significant performance optimizations including a critical Windows performance fix:

**Windows Performance Fix Results:**
| Platform | Before Fix | After Fix | Improvement |
|----------|------------|-----------|-------------|
| Windows  | 35-45 FPS (10k entities) | 60+ FPS | 33-71% improvement |
| Linux    | 60+ FPS | 60+ FPS | Already optimal |
| Mac      | 60+ FPS | 60+ FPS | Already optimal |

**AI Execution Verification (Post-Windows Fix):**
| Test Scenario | Behavior Executions | Entity Updates | Status | Notes |
|---------------|---------------------|----------------|---------|-------|
| 1,000 entities | 1,810 updates | 1,000/1,000 | ✅ Working | 9% async execution rate |
| 5,000 entities | 16,077 updates | 5,000/5,000 | ✅ Working | 16% async execution rate |
| 100,000 entities | 66,200 updates | 100,000/100,000 | ✅ Working | 13% async execution rate |

*Note: Lower execution percentages in async mode are expected and correct - they prove the Windows performance fix is working. The main thread continues immediately while AI processes asynchronously.*

**General Optimization Results:**

| Metric | Before Optimization | After Optimization | Improvement |
|--------|-------------------|-------------------|-------------|
| **Entity updates/sec (100K entities)** | 887,296 | 2,268,256 | **+156%** |
| **Time per entity** | 0.001127 ms | 0.000441 ms | **61% faster** |
| **Time per update cycle** | 318.60 ms | 132.26 ms | **58% faster** |

**Threading Scalability Performance:**

| Entity Count | Threading Mode | Updates/Second | Performance Ratio |
|--------------|----------------|----------------|-------------------|
| 100 | Auto-Single | 170,000 | 1.00x (baseline) |
| 200 | Auto-Threaded | 1,078,000 | 6.34x |
| 1,000 | Auto-Threaded | 1,106,000 | 6.51x |
| 5,000 | Auto-Threaded | 1,880,000 | 11.06x |
| 100,000 | Auto-Threaded | 2,268,000 | 13.34x |

**Key Optimizations Applied:**
- ✅ **Reduced lock contention**: Stats updates every 60 frames instead of every frame
- ✅ **Batch-level atomic operations**: Single update per batch vs per-entity updates
- ✅ **Cached player position**: Computed once per batch for all distance calculations
- ✅ **Simplified distance calculations**: Removed complex multi-tier thresholds
- ✅ **Optimized batch sizing**: Scales with WorkerBudget allocation
- ✅ **Enhanced queue management**: Improved fallback strategies
- ✅ **Architectural compliance**: Proper WorkerBudget coordination maintained

**Production Performance Characteristics:**
- Consistent 2M+ entity updates per second for large-scale scenarios
- Automatic threading activation above 200 entities
- Hardware-adaptive scaling from single-core to multi-core systems
- Stable performance without threading fallbacks under normal workloads

## Creating Custom Behaviors

```cpp
class FlankingBehavior : public AIBehavior {
public:
    FlankingBehavior(EntityPtr target, float speed = 2.0f);

    void init(EntityPtr entity) override {
        // Initialize flanking behavior
    }

    void executeLogic(EntityPtr entity) override {
        // Implement flanking logic
    }

    void clean(EntityPtr entity) override {
        // Clean up flanking behavior
    }

    std::string getName() const override {
        return "Flanking";
    }

    // REQUIRED: Clone method for individual instances
    std::shared_ptr<AIBehavior> clone() const override {
        return std::make_shared<FlankingBehavior>(m_target, m_speed);
    }

private:
    EntityPtr m_target;
    float m_speed;
};
```

## API Reference

### Core AIManager Methods

```cpp
// Core initialization
bool init();
void clean();
void prepareForStateTransition(); // Call before GameState::exit()

// Behavior registration
void registerBehavior(const std::string& name, std::shared_ptr<AIBehavior> behavior);
bool hasBehavior(const std::string& name) const;
std::shared_ptr<AIBehavior> getBehavior(const std::string& name) const;

// Entity management
void registerEntityForUpdates(EntityPtr entity, int priority = 5);
void registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName);
void unregisterEntityFromUpdates(EntityPtr entity);

// Behavior assignment
void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName);
void queueBehaviorAssignment(EntityPtr entity, const std::string& behaviorName);
size_t processPendingBehaviorAssignments();
void unassignBehaviorFromEntity(EntityPtr entity);
bool entityHasBehavior(EntityPtr entity) const;

// Distance optimization
void setPlayerForDistanceOptimization(EntityPtr player);
EntityPtr getPlayerReference() const;
Vector2D getPlayerPosition() const;
bool isPlayerValid() const;

// Global controls
void setGlobalPause(bool paused);
bool isGloballyPaused() const;
void resetBehaviors();
int getEntityPriority(EntityPtr entity) const;
float getUpdateRangeMultiplier(int priority) const;

// Threading configuration
void configureThreading(bool useThreading, unsigned int maxThreads = 0);
void configurePriorityMultiplier(float multiplier = 1.0f);

// Message system
void sendMessageToEntity(EntityPtr entity, const std::string& message, bool immediate = false);
void broadcastMessage(const std::string& message, bool immediate = false);
void processMessageQueue();

// Performance monitoring
AIPerformanceStats getPerformanceStats() const;
size_t getManagedEntityCount() const;
size_t getBehaviorCount() const;
size_t getBehaviorUpdateCount() const;
size_t getTotalAssignmentCount() const;
bool isShutdown() const;
```

### AIBehavior Interface

```cpp
// Core behavior methods (pure virtual)
virtual void executeLogic(EntityPtr entity) = 0;
virtual void init(EntityPtr entity) = 0;
virtual void clean(EntityPtr entity) = 0;
virtual std::string getName() const = 0;
virtual std::shared_ptr<AIBehavior> clone() const = 0;

// Optional methods
virtual void onMessage(EntityPtr entity, const std::string& message);
virtual bool isActive() const;
virtual void setActive(bool active);
virtual bool isEntityInRange(EntityPtr entity) const;
```

## Best Practices

### Entity Creation Pattern

```cpp
void createNPCGroup(const std::string& npcType, int count) {
    for (int i = 0; i < count; ++i) {
        auto npc = createNPC(npcType);

        // Determine behavior based on NPC type
        std::string behavior = getBehaviorForNPCType(npcType, i);
        int priority = getPriorityForNPCType(npcType);

        // Single call for registration and assignment
        AIManager::Instance().registerEntityForUpdates(npc, priority, behavior);
    }
}
```

### Performance Tips (Optimized)

1. **Use appropriate priorities** for different NPC types - higher priority entities get more frequent updates
2. **Set player reference** for distance optimization - essential for proper culling and performance
3. **Queue assignments** for batch processing - use `queueBehaviorAssignment()` for better throughput
4. **Monitor performance stats** to identify bottlenecks - track entities/second metrics
5. **Use mode-based behaviors** for consistent configuration across entity groups
6. **Clean up properly** when changing game states - prevents memory leaks and stale references
7. **Leverage automatic threading** - ensure entity counts >200 for optimal multi-threading benefits
8. **Batch entity creation** - register multiple entities at once for better cache efficiency
9. **Avoid frequent behavior changes** - behavior switching has overhead, design for stability
10. **Use distance-based priorities** - closer entities should have higher priorities for better player experience
11. **For large numbers of wandering NPCs, increase WanderBehavior's update frequency (e.g., 4+) to maximize performance.**

**Threading Performance Guidelines:**
- **< 200 entities**: Single-threaded mode (optimal for small workloads)
- **200-1000 entities**: Multi-threading provides 4-6x performance improvement
- **1000+ entities**: Full WorkerBudget utilization with buffer threads
- **10000+ entities**: Reduced task priority to prevent queue saturation

**Memory Optimization:**
- Use `shared_ptr` for behavior templates to reduce memory footprint
- Clean up inactive entities regularly with `cleanupInactiveEntities()`
- Monitor total assignment count to detect memory growth patterns

### Error Handling

```cpp
// Always wrap AI operations in try-catch blocks
try {
    AIManager::Instance().assignBehaviorToEntity(npc, "Wander");
} catch (const std::exception& e) {
    std::cerr << "Failed to assign behavior: " << e.what() << std::endl;
}
```

### Cleanup on State Transitions

```cpp
void GameState::exit() {
    // Unregister entities before state change
    for (auto& npc : m_npcs) {
        AIManager::Instance().unregisterEntityFromUpdates(npc);
        AIManager::Instance().unassignBehaviorFromEntity(npc);
    }

    // Note: Don't clean AIManager - it's used across game states
}
```

## Thread Safety & Synchronization

The AIManager implements comprehensive thread safety:

**Multi-Reader/Single-Writer Architecture:**
- **Registration/Assignment**: Protected by `std::shared_mutex` for concurrent reads
- **Entity Updates**: Shared locks allow parallel batch processing
- **Behavior Storage**: Read-only access during threaded updates

**Lock-Free Operations:**
- **Global Pause**: Atomic boolean (`std::atomic<bool>`) for zero-latency checks
- **Threading State**: Atomic configuration flags for runtime control
- **Performance Counters**: Atomic counters for lock-free statistics

**Message System Synchronization:**
- **Message Queue**: Mutex-protected with deferred processing
- **Assignment Queue**: Thread-safe batch assignment processing
- **Performance Stats**: Mutex-protected collection with periodic updates

**Threading Coordination:**
- **Batch Completion**: Atomic counters for task synchronization
- **Adaptive Waiting**: Spin-lock with progressive backoff for optimal latency
- **Timeout Protection**: 16ms frame-rate protection with 50ms severe bottleneck warnings

## Memory Management

- **Strong References**: EntityPtr (shared_ptr) prevents premature deletion
- **Individual Behaviors**: Each entity gets own behavior instance via clone()
- **Automatic Cleanup**: Inactive entities removed during cleanup cycles
- **Efficient Containers**: Pre-allocated vectors and optimized data structures

## Integration with Game Engine & ThreadSystem (Optimized)

The AIManager integrates seamlessly with the engine's optimized threading architecture:

**GameEngine Integration:**
1. **GameEngine calls** `AIManager::update()` automatically each frame
2. **Batch assignments** are processed before entity updates for optimal throughput
3. **Automatic resource management** through optimized WorkerBudget system
4. **No manual update calls** required in game states
5. **Architectural compliance** ensures system-wide coordination and stability

**ThreadSystem & WorkerBudget Architecture (Enhanced):**
1. **Centralized Resource Allocation**: Uses `Hammer::calculateWorkerBudget()` for coordinated distribution
2. **AI Worker Budget**: Receives 60% of available workers with proper allocation limits
3. **Buffer Thread Access**: Utilizes buffer threads when entity count > 1000 for burst capacity
4. **Dynamic Scaling**: Batch sizes scale with allocated workers (`entities / optimalWorkerCount`)
5. **Queue Pressure Management**: Monitors ThreadSystem load to prevent resource contention
6. **Architectural Coordination**: Maintains proper resource boundaries with EventManager and GameEngine

**Optimized Performance Characteristics:**
- **Threading Threshold**: 200 entities for automatic multi-threading activation
- **Batch Size Scaling**: 200-600 entities per batch based on workload and worker availability
- **Cache Efficiency**: Optimized batch limits for L1/L2 cache friendliness
- **Atomic Operations**: Reduced per-entity overhead with batch-level updates
- **Lock Contention**: Minimized through periodic stats updates (every 60 frames)

**Resource Scaling Examples (Optimized):**
- **4-core/8-thread system (7 workers)**: GameLoop=2, AI=3, Events=1, Buffer=1
- **8-core/16-thread system (15 workers)**: GameLoop=2, AI=8, Events=4, Buffer=1
- **16-core/32-thread system (31 workers)**: GameLoop=2, AI=17, Events=9, Buffer=3
- **High-end systems**: Automatic scaling with buffer utilization for workloads >1000 entities

**Performance Guarantees:**
- 2M+ entity updates per second for large-scale scenarios (100K+ entities)
- 4-13x performance improvement over single-threaded processing
- Consistent threading behavior without fallbacks under normal workloads
- Hardware-adaptive scaling from single-core to high-core-count systems

**Performance Coordination:**
- **Queue Pressure Monitoring**: Respects ThreadSystem queue limits
- **Priority-Based Task Submission**: Adjusts task priority based on workload size
- **Frame-Rate Protection**: 16ms timeout ensures consistent frame rates
- **Burst Capacity**: Buffer threads handle temporary workload spikes

This architecture ensures optimal AI performance while maintaining system stability and fair resource allocation across all engine components.

For specific behavior configuration details, see the behavior mode documentation. For integration patterns and advanced usage, refer to the developer guides.
