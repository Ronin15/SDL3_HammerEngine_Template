# Thread System Optimized for "1000+ Tasks"

## What Defines a Task

In the Forge Engine ThreadSystem, a task is:
- A discrete unit of work encapsulated in a `std::function<void()>` (or a function returning a value for tasks with results)
- Typically implemented as a lambda function capturing necessary data
- An independent operation that executes on one of the worker threads

For example, this represents a single task:
```cpp
Forge::ThreadSystem::Instance().enqueueTask([entity]() {
    entity->update();
}, Forge::TaskPriority::Normal, "EntityUpdate");
```

## Capacity vs. Throughput

When we discuss handling 1000+ tasks, we're addressing two distinct aspects:

1. **Queue Capacity** - The ability to store 1000+ tasks in the queue at once
   - Uses separate priority queues (default: 1024 total capacity distributed across 5 priority levels)
   - Each priority level gets ~205 initial slots (1024 ÷ 5)
   - Relates to memory allocation and management
   - Prevents fragmentation and reallocation pauses

2. **Processing Throughput** - The ability to execute 1000+ tasks efficiently
   - Determined by thread count (auto-detects as hardware_concurrency - 1)
   - Task complexity and priority-based scheduling
   - Relates to CPU processing power and lock contention reduction

## Practical Examples in a Game Context

### Entity Updates

A game with 800-1000 active entities, each updated in its own task:
```cpp
// Enqueue an update task for each entity
for (auto entity : activeEntities) { // ~1000 entities (std::shared_ptr<Entity>)
    Forge::ThreadSystem::Instance().enqueueTask([entity, deltaTime]() {
        try {
            entity->update(deltaTime);
        } catch (const std::exception& e) {
            std::cerr << "Entity update error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::Normal, "EntityUpdate");
}
```

### Physics Simulation

Breaking physics calculations into manageable chunks:
```cpp
// Break collision detection into tasks of 50 objects each
for (int i = 0; i < collisionObjects.size(); i += 50) { // 20 tasks for 1000 objects
    int endIndex = std::min(i + 50, static_cast<int>(collisionObjects.size()));
    Forge::ThreadSystem::Instance().enqueueTask([i, endIndex]() {
        try {
            processCollisionsForRange(i, endIndex);
        } catch (const std::exception& e) {
            std::cerr << "Collision processing error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::Normal, "CollisionDetection");
}
```

### Particle Systems

Updating thousands of particles in parallel batches:
```cpp
// Update particle batches in parallel (1000 tasks handling ~100,000 particles)
for (int i = 0; i < particles.size(); i += 100) { // 1000 tasks for 100,000 particles
    int endIndex = std::min(i + 100, static_cast<int>(particles.size()));
    Forge::ThreadSystem::Instance().enqueueTask([i, endIndex]() {
        try {
            updateParticleBatch(i, endIndex);
        } catch (const std::exception& e) {
            std::cerr << "Particle update error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::Normal, "ParticleUpdate");
}
```

### Mixed Workload

A typical frame might include a combination of different systems:
- 500 entity updates
- 100 AI decision tasks
- 200 physics calculation tasks
- 150 animation update tasks
- 50 miscellaneous background tasks

## System Visualization

Here's what the task processing might look like visually in a running game:

```
Thread Pool (4 worker threads)
│
├── Thread 1: [Entity Update] → [Physics Task] → [AI Task] → [Entity Update] → ...
│
├── Thread 2: [Animation Update] → [Entity Update] → [Physics Task] → [Entity Update] → ...
│
├── Thread 3: [Entity Update] → [AI Task] → [Animation Update] → [Physics Task] → ...
│
└── Thread 4: [Physics Task] → [Entity Update] → [Animation Update] → [Background Task] → ...

Task Queue: [Tasks waiting to be processed... up to 1000+ at peak load]
```

## Performance Implications

### Processing Time Estimates

With a modern CPU with 4 worker threads (auto-detected as hardware_concurrency - 1):

1. **Simple Tasks (0.1ms each)**:
   - 1000 tasks distributed by priority across 4 threads
   - High-priority tasks execute first, then normal, then low priority
   - ~25ms total processing time for uniform distribution
   - Completes within two frames at 60 FPS (16.7ms per frame)

2. **Medium Tasks (0.5ms each)**:
   - Priority-based scheduling ensures critical tasks finish first
   - ~125ms total processing time
   - Critical/High priority tasks complete in first few frames

3. **Complex Tasks (2ms each)**:
   - ~500ms total processing time
   - Best scheduled as Low or Idle priority to avoid blocking important tasks
   - System automatically balances load across worker threads

### Memory Usage

With the separate priority queues architecture:

- Approximately 200 bytes per task (function object + captures + priority info)
- 1000 tasks × 200 bytes = ~200 KB of memory total
- Memory pre-allocated per priority level (~40 KB per priority initially)
- Each priority queue expands independently when reaching 90% capacity
- Better cache locality due to separate queues reducing lock contention
- Memory managed throughout system lifetime with automatic expansion

## Game Scenarios That Benefit from 1000+ Tasks

1. **Open World Games**:
   - Large number of simultaneously active entities
   - Dynamic environment with many interactive elements
   - Extensive AI calculations for NPCs

2. **Strategy Games**:
   - Many units requiring pathfinding and decision making
   - Economy and resource simulations
   - Procedural terrain or environment processing

3. **Simulation Games**:
   - Physics-heavy environments with many colliding objects
   - Environmental systems (weather, erosion, growth)
   - Agent-based simulations with autonomous entities

4. **Content Loading and Generation**:
   - Level streaming with many assets loading in parallel
   - Procedural content generation
   - Dynamic LOD (Level of Detail) processing

## Implementation Considerations

### Smart Pointer Best Practices

When working with the ThreadSystem, proper memory management is crucial for thread safety:

#### Using Smart Pointers in Tasks

```cpp
// Recommended: Use shared_ptr for entities that might be accessed by multiple tasks
std::vector<std::shared_ptr<Entity>> entities;

// Safe capture by value - increments reference count
for (auto entity : entities) {
    // Check if ThreadSystem is available before enqueueing
    if (Forge::ThreadSystem::Exists()) {
        Forge::ThreadSystem::Instance().enqueueTask([entity]() {
            try {
                entity->update();  // Entity guaranteed to be alive
            } catch (const std::exception& e) {
                std::cerr << "Entity update error: " << e.what() << std::endl;
            }
        }, Forge::TaskPriority::Normal, "EntityUpdate");
    }
}
```

#### Avoiding Unsafe Reference Captures

```cpp
// UNSAFE: Reference might become invalid before task executes
for (auto& entity : entities) {
    Forge::ThreadSystem::Instance().enqueueTask([&entity]() {
        entity.update();  // DANGER: entity reference might be invalid
    });
}

// SAFE: Capture smart pointer by value with proper error handling
for (auto entity : entities) {
    if (Forge::ThreadSystem::Exists()) {
        Forge::ThreadSystem::Instance().enqueueTask([entity]() {
            try {
                entity->update();  // Safe: shared_ptr keeps object alive
            } catch (const std::exception& e) {
                std::cerr << "Entity update error: " << e.what() << std::endl;
            }
        }, Forge::TaskPriority::Normal, "EntityUpdate");
    }
}
```

#### Thread-Safe Data Access

```cpp
// For data that multiple tasks might access simultaneously
class ThreadSafeGameData {
private:
    mutable std::shared_mutex dataMutex;
    GameState gameState;

public:
    void updateSafely() {
        std::unique_lock<std::shared_mutex> lock(dataMutex);
        // Modify data safely
        gameState.update();
    }
    
    GameState readSafely() const {
        std::shared_lock<std::shared_mutex> lock(dataMutex);
        // Read data safely
        return gameState;
    }
};
```

### Optimal Task Granularity

Finding the right balance is important:
- **Too Fine-Grained**: If tasks are too small (< 0.05ms), thread management overhead can exceed benefits
- **Too Coarse-Grained**: If tasks are too large (> 5ms), can cause load imbalance and responsiveness issues
- **Sweet Spot**: Tasks that take 0.1-1ms to execute provide good parallelism without excessive overhead

### Task Dependencies

When tasks depend on each other, consider:
- Using `enqueueTaskWithResult()` and futures to establish clear dependencies
- Batching related tasks together when appropriate
- Using continuation-style patterns for task chains
- Priority-based scheduling to ensure important dependencies execute first

```cpp
// Check system availability before creating task chains
if (!Forge::ThreadSystem::Exists()) {
    std::cerr << "ThreadSystem not available for task dependencies!" << std::endl;
    return;
}

auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() {
    try {
        return loadLevel();
    } catch (const std::exception& e) {
        std::cerr << "Level loading error: " << e.what() << std::endl;
        throw;
    }
}, Forge::TaskPriority::High, "LoadLevel");

// Then later:
Forge::ThreadSystem::Instance().enqueueTask([future]() {
    try {
        auto level = future.get();  // Wait for previous task
        populateEntities(level);
    } catch (const std::exception& e) {
        std::cerr << "Entity population error: " << e.what() << std::endl;
    }
}, Forge::TaskPriority::Normal, "PopulateEntities");
```

## Conclusion: Capabilities at 1000+ Tasks

The ability to efficiently handle 1000+ tasks allows the Forge Engine to:

1. **Support Rich, Dynamic Worlds**: Maintain hundreds of active, independently updated entities
2. **Provide Responsive Gameplay**: Priority-based scheduling ensures critical tasks execute first
3. **Efficiently Use Modern Hardware**: Automatic thread detection and separate priority queues reduce contention
4. **Manage Memory Effectively**: Per-priority pre-allocation with automatic expansion prevents fragmentation
5. **Graceful Degradation**: Safe shutdown handling and existence checks prevent crashes

The implementation uses separate priority queues with individual memory pre-allocation that provides significantly better performance characteristics than single-queue systems by reducing lock contention.

Key architectural advantages:
- **Separate Priority Queues**: Each priority level has its own queue and memory allocation
- **Automatic Scaling**: Thread count auto-detected as hardware_concurrency - 1
- **Lock-Free Checks**: Fast existence and shutdown detection without acquiring locks
- **Safe Shutdown**: Tasks enqueued after shutdown are silently ignored instead of crashing

For modern games, this processing capacity enables complex simulations and rich game worlds while maintaining excellent performance and memory efficiency. The system's per-priority automatic capacity expansion (at 90% utilization) ensures that even unexpected bursts of tasks are handled smoothly without compromising overall system performance.

## Monitoring and Debugging

### Real-time Performance Monitoring

```cpp
// Function to monitor ThreadSystem performance during gameplay
void monitorThreadSystemPerformance() {
    if (!Forge::ThreadSystem::Exists()) {
        std::cout << "ThreadSystem not available for monitoring" << std::endl;
        return;
    }
    
    auto& ts = Forge::ThreadSystem::Instance();
    
    // Get basic queue information
    size_t queueSize = ts.getQueueSize();
    size_t queueCapacity = ts.getQueueCapacity();
    size_t processed = ts.getTotalTasksProcessed();
    size_t enqueued = ts.getTotalTasksEnqueued();
    
    // Calculate utilization percentages
    double queueUtilization = (static_cast<double>(queueSize) / queueCapacity) * 100.0;
    double processingEfficiency = queueSize > 0 ? 
        (static_cast<double>(processed) / enqueued) * 100.0 : 100.0;
    
    std::cout << "=== ThreadSystem Performance ===" << std::endl;
    std::cout << "Threads: " << ts.getThreadCount() << std::endl;
    std::cout << "Queue: " << queueSize << "/" << queueCapacity 
              << " (" << std::fixed << std::setprecision(1) << queueUtilization << "% full)" << std::endl;
    std::cout << "Tasks Processed: " << processed << std::endl;
    std::cout << "Tasks Enqueued: " << enqueued << std::endl;
    std::cout << "Busy: " << (ts.isBusy() ? "Yes" : "No") << std::endl;
    std::cout << "=================================" << std::endl;
}
```

### Development-time Debugging

```cpp
// Enable detailed logging during development
void enableThreadSystemDebugging() {
    if (Forge::ThreadSystem::Exists()) {
        Forge::ThreadSystem::Instance().setDebugLogging(true);
        std::cout << "ThreadSystem debug logging enabled" << std::endl;
    }
}

// Example of detailed task submission with monitoring
void submitTasksWithMonitoring(const std::vector<std::shared_ptr<Entity>>& entities) {
    if (!Forge::ThreadSystem::Exists()) {
        std::cerr << "Cannot submit tasks - ThreadSystem not available" << std::endl;
        return;
    }
    
    auto& ts = Forge::ThreadSystem::Instance();
    
    // Get initial state
    size_t initialQueueSize = ts.getQueueSize();
    size_t initialEnqueued = ts.getTotalTasksEnqueued();
    
    // Submit tasks with priority and descriptions
    for (size_t i = 0; i < entities.size(); ++i) {
        auto entity = entities[i];
        
        // Determine priority based on entity importance
        Forge::TaskPriority priority = Forge::TaskPriority::Normal;
        if (entity->isPlayerControlled()) {
            priority = Forge::TaskPriority::High;
        } else if (entity->isDistantFromPlayer()) {
            priority = Forge::TaskPriority::Low;
        }
        
        // Create descriptive task name for debugging
        std::string taskDescription = "EntityUpdate_" + std::to_string(i) + "_" + entity->getType();
        
        ts.enqueueTask([entity]() {
            try {
                entity->update();
            } catch (const std::exception& e) {
                std::cerr << "Entity update failed: " << e.what() << std::endl;
            }
        }, priority, taskDescription);
    }
    
    // Report submission results
    size_t finalEnqueued = ts.getTotalTasksEnqueued();
    size_t tasksSubmitted = finalEnqueued - initialEnqueued;
    
    std::cout << "Submitted " << tasksSubmitted << " entity update tasks" << std::endl;
    std::cout << "Queue size increased from " << initialQueueSize 
              << " to " << ts.getQueueSize() << std::endl;
}
```

### Capacity Planning

```cpp
// Function to determine if queue capacity needs adjustment
bool shouldIncreaseQueueCapacity() {
    if (!Forge::ThreadSystem::Exists()) {
        return false;
    }
    
    auto& ts = Forge::ThreadSystem::Instance();
    size_t queueSize = ts.getQueueSize();
    size_t queueCapacity = ts.getQueueCapacity();
    
    // Check if we're consistently using > 80% capacity
    double utilization = static_cast<double>(queueSize) / queueCapacity;
    
    if (utilization > 0.8) {
        std::cout << "WARNING: Queue utilization at " 
                  << (utilization * 100.0) << "% - consider reserving more capacity" << std::endl;
        return true;
    }
    
    return false;
}

// Pre-allocate capacity for known heavy workloads
void prepareForHeavyWorkload(size_t expectedTasks) {
    if (!Forge::ThreadSystem::Exists()) {
        std::cerr << "Cannot prepare capacity - ThreadSystem not available" << std::endl;
        return;
    }
    
    auto& ts = Forge::ThreadSystem::Instance();
    size_t currentCapacity = ts.getQueueCapacity();
    
    if (expectedTasks > currentCapacity) {
        // Reserve 25% more than expected for safety margin
        size_t newCapacity = static_cast<size_t>(expectedTasks * 1.25);
        
        if (ts.reserveQueueCapacity(newCapacity)) {
            std::cout << "Reserved capacity increased from " << currentCapacity 
                      << " to " << ts.getQueueCapacity() << " for heavy workload" << std::endl;
        } else {
            std::cerr << "Failed to reserve additional capacity" << std::endl;
        }
    }
}
```
