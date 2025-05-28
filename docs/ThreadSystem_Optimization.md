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
   - Optimized with queue capacity reservation feature (default: 1024)
   - Relates to memory allocation and management
   - Prevents fragmentation and reallocation pauses

2. **Processing Throughput** - The ability to execute 1000+ tasks efficiently
   - Determined by thread count and task complexity
   - Relates to CPU processing power and scheduling

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

With a modern CPU with 4 worker threads (5 cores with 1 reserved for the main thread):

1. **Simple Tasks (0.1ms each)**:
   - 1000 tasks ÷ 4 threads = ~250 tasks per thread
   - 250 tasks × 0.1ms = ~25ms total processing time
   - Completes within two frames at 60 FPS (16.7ms per frame)

2. **Medium Tasks (0.5ms each)**:
   - 1000 tasks ÷ 4 threads = ~250 tasks per thread
   - 250 tasks × 0.5ms = ~125ms total processing time
   - Spans approximately 7-8 frames at 60 FPS

3. **Complex Tasks (2ms each)**:
   - 1000 tasks ÷ 4 threads = ~250 tasks per thread
   - 250 tasks × 2ms = ~500ms total processing time
   - Best used for background processing or spread across multiple frames

### Memory Usage

With the optimized queue capacity:

- Approximately 200 bytes per task (function object + captures)
- 1000 tasks × 200 bytes = ~200 KB of memory
- Actually pre-allocated in a contiguous block for better cache performance
- Memory is reserved at initialization and managed throughout system lifetime

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
    Forge::ThreadSystem::Instance().enqueueTask([entity]() {
        try {
            entity->update();  // Entity guaranteed to be alive
        } catch (const std::exception& e) {
            std::cerr << "Entity update error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::Normal, "EntityUpdate");
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

// SAFE: Capture smart pointer by value
for (auto entity : entities) {
    Forge::ThreadSystem::Instance().enqueueTask([entity]() {
        entity->update();  // Safe: shared_ptr keeps object alive
    });
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

```cpp
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
2. **Provide Responsive Gameplay**: Process many game systems in parallel without stalling
3. **Efficiently Use Modern Hardware**: Scale across multiple CPU cores for better performance
4. **Manage Memory Effectively**: Handle substantial workloads without memory fragmentation through true pre-allocation

The implementation uses explicit memory pre-allocation with a custom priority queue that provides significantly better memory characteristics than standard containers.

For modern games, this processing capacity enables complex simulations and rich game worlds while maintaining good performance and memory efficiency. The system's automatic capacity expansion (doubling at 90% utilization) ensures that even unexpected bursts of tasks are handled smoothly without compromising memory efficiency.
