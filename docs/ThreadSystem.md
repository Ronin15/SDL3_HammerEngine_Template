# ThreadSystem Documentation

## Overview

The ThreadSystem is a core component of the Forge Game Engine, providing thread pool management and task-based concurrency. It allows game systems to enqueue work that gets processed by a pool of worker threads, enabling performance benefits from multi-core processors while maintaining a simplified programming model. The system automatically manages its capacity and is designed to efficiently handle hundreds of concurrent tasks (see [Defining a Task](ThreadSystem_Optimization.md) for details). ThreadSystem serves as the unified threading framework used by various engine components, including the [EventManager](EventManager.md) and [AIManager](ai/AIManager.md). It supports priority-based task scheduling to ensure critical operations receive appropriate processing time.

## Features

- Thread pool with automatic sizing based on available CPU cores
- Task-based programming model with simple enqueue interface
- Support for fire-and-forget tasks and tasks with return values
- Thread-safe operations with proper synchronization
- Queue capacity reservation for memory optimization
- Priority-based task scheduling (Critical, High, Normal, Low, Idle)
- Clean shutdown with proper resource management
- Integration with engine components such as EventManager and AIManager

## Basic Usage

### Initialization

```cpp
// Initialize the thread system with default settings
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}

// Access information about the thread system
unsigned int threadCount = Forge::ThreadSystem::Instance().getThreadCount();
std::cout << "Thread system initialized with " << threadCount << " threads" << std::endl;
```

### Submitting Tasks

```cpp
// Simple fire-and-forget task with default (Normal) priority
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // Task logic here
    std::cout << "Executing task on thread pool" << std::endl;
});

// Task with high priority
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // High-priority task logic
    std::cout << "Executing high-priority task" << std::endl;
}, Forge::TaskPriority::High);

// Task with a return value and specific priority
auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
    // Task logic here
    return 42;
}, Forge::TaskPriority::Normal);

// Wait for and use the result
int result = future.get();  // Blocks until the task completes
```

## Integration with Engine Components

ThreadSystem is designed to be the unified thread management solution for the Forge Game Engine. Multiple engine components integrate with ThreadSystem:

### EventManager Integration

The EventManager uses ThreadSystem for parallel event processing:

```cpp
// Initialize both systems
Forge::ThreadSystem::Instance().init();
EventManager::Instance().init();

// Configure EventManager to use ThreadSystem with default priority
EventManager::Instance().configureThreading(true);

// Or with specific priority
EventManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);

// EventManager will now process events in parallel through ThreadSystem
EventManager::Instance().update();
```

See [EventManager_ThreadSystem.md](EventManager_ThreadSystem.md) for detailed integration documentation.

### AIManager Integration

The AIManager similarly uses ThreadSystem for parallel AI behavior processing, allowing efficient scaling across available CPU cores:

```cpp
// Initialize both systems
Forge::ThreadSystem::Instance().init();
AIManager::Instance().init();

// Configure AIManager to use ThreadSystem with default priority
AIManager::Instance().configureThreading(true);

// Or with specific priority for AI tasks
AIManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);

// AIManager will now process AI behaviors in parallel through ThreadSystem
AIManager::Instance().update();
```

See [AIManager.md](ai/AIManager.md) for detailed integration documentation.

### Shutdown

```cpp
// Clean up the thread system when your application exits
Forge::ThreadSystem::Instance().clean();
```

## Queue Capacity Management

The ThreadSystem actively pre-allocates and manages memory for the task queue. This provides significant performance benefits by reducing memory fragmentation and eliminating allocation pauses during gameplay.

### Setting Initial Capacity (Optional)

```cpp
// Initialize with default capacity (recommended approach - 1024 tasks)
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}

// Or specify a custom initial capacity if you have specific requirements
if (!Forge::ThreadSystem::Instance().init(1000)) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}
```

### Adjusting Capacity at Runtime (Rarely Needed)

### Automatic Capacity Expansion

The queue automatically expands its capacity when it reaches 90% of the current limit. When this happens:
- The system temporarily stores existing tasks
- Doubles the capacity
- Reinserts the tasks in their proper priority order
- This happens without any task loss or API interruption

### Monitoring Queue Status

```cpp
// Check current capacity
size_t capacity = Forge::ThreadSystem::Instance().getQueueCapacity();

// Check current queue size
size_t queueSize = Forge::ThreadSystem::Instance().getQueueSize();

std::cout << "Queue usage: " << queueSize << "/" << capacity << std::endl;
```

## Memory Management Benefits

### Reduced Fragmentation

By pre-allocating the task queue's memory, the system significantly reduces memory fragmentation that would otherwise occur from frequent allocations and deallocations. This is particularly important for long-running applications like games.

### Improved Cache Locality

The contiguous memory layout of the pre-allocated queue improves cache performance when worker threads access tasks, leading to better throughput and reduced cache misses. The implementation specifically manages the underlying container to maintain this benefit.

### Consistent Performance

The system eliminates unexpected memory allocation during normal operation by:
1. Pre-allocating memory during initialization
2. Performing controlled growth when needed (doubling capacity)
3. Managing reallocations during low-activity periods rather than during peak demand

This helps maintain consistent performance during gameplay, avoiding hitches that would otherwise occur during unpredictable memory reallocation.

## Best Practices

1. **Use Default Capacity When Possible**
   - For most games, the default capacity (1024) works well and adjusts automatically
   - You rarely need to manually set or adjust the capacity

2. **Focus on Task Design Instead of Capacity Management**
   - Create appropriately sized tasks that perform meaningful work
   - Let the thread system worry about queue management

3. **Add Error Handling to Tasks**
   - Always wrap task code in try-catch blocks to prevent crashes
   - Report errors but allow the system to continue running

4. **Monitor During Development**
   - Use `getQueueSize()` and `getQueueCapacity()` during development to understand usage patterns
   - If queue size regularly approaches capacity, consider optimizing your task design

5. **Consider Task Granularity**
   - Optimal task size is typically 0.1-1ms per task
   - Too small tasks (<0.05ms) create excessive overhead
   - Too large tasks (>5ms) can cause load imbalance
   - See [ThreadSystem Task](ThreadSystem_Optimization.md) for detailed guidance

6. **Add Proper Exception Handling**
   - Always handle exceptions that might occur in threaded tasks
   - Ensure your code is resilient to thread task failures

## Example Scenarios

### Game Entity Updates

```cpp
// Process game entities in parallel
void updateEntities(const std::vector<std::shared_ptr<Entity>>& entities) {
    // Submit update tasks - no need to manually reserve capacity
    for (auto entity : entities) {
        // Assign appropriate priority based on entity importance
        Forge::TaskPriority priority = Forge::TaskPriority::Normal;

        // Prioritize player-interactive entities
        if (entity->isInteractingWithPlayer()) {
            priority = Forge::TaskPriority::High;
        }
        // Use lower priority for distant/background entities
        else if (entity->isDistantFromPlayer()) {
            priority = Forge::TaskPriority::Low;
        }

        Forge::ThreadSystem::Instance().enqueueTask([entity]() {
            try {
                entity->update();
            } catch (const std::exception& e) {
                std::cerr << "Error updating entity: " << e.what() << std::endl;
            }
        }, priority);
    }
}
```

### Asset Loading

```cpp
// Load multiple assets in parallel
void loadAssets(const std::vector<std::string>& assetPaths) {
    // Pre-reserve the results vector (good practice)
    std::vector<std::future<bool>> results;
    results.reserve(assetPaths.size());

    // Submit asset loading tasks - the ThreadSystem manages queue capacity
    for (const auto& path : assetPaths) {
        try {
            results.push_back(
                Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                    [path]() -> bool {
                        try {
                            return loadAssetFromDisk(path);
                        } catch (const std::exception& e) {
                            std::cerr << "Error loading asset " << path << ": " << e.what() << std::endl;
                            return false;
                        }
                    }
                )
            );
        } catch (const std::exception& e) {
            std::cerr << "Failed to enqueue task for " << path << ": " << e.what() << std::endl;
        }
    }

    // Wait for and process results
    for (auto& future : results) {
        try {
            bool success = future.get();
            // Handle result
        } catch (const std::exception& e) {
            std::cerr << "Exception during task execution: " << e.what() << std::endl;
            // Handle failure
        }
    }
}
```

## Technical Details

The ThreadSystem uses a custom priority queue implementation with explicit pre-allocation of the underlying memory. This implementation provides significantly better memory management than standard containers. All operations are thread-safe with proper synchronization to ensure correct behavior in a multi-threaded environment.

The implementation efficiently handles:
- Task creation and enqueueing with priority-based ordering
- Intelligent memory pre-allocation and growth
- Thread-safe access to the task queue
- Proper propagation of exceptions and return values

Tasks are processed according to their priority level, with higher-priority tasks being executed before lower-priority ones, all while maintaining memory efficiency.

### Priority Levels

The ThreadSystem supports five priority levels for tasks:

- `Forge::TaskPriority::Critical` (0): For mission-critical operations that must execute immediately
- `Forge::TaskPriority::High` (1): For important tasks that need quick responses
- `Forge::TaskPriority::Normal` (2): Default priority for standard tasks
- `Forge::TaskPriority::Low` (3): For background or non-time-sensitive tasks
- `Forge::TaskPriority::Idle` (4): For very low-priority tasks that should only run when the system is idle

## Performance Characteristics

The system is designed to efficiently handle hundreds of concurrent tasks with dynamic memory management for optimal memory usage and processing efficiency. The task queue starts with a default capacity of 1024 tasks and can automatically grow as needed.

The priority-based scheduling ensures that critical tasks receive timely execution without being delayed by lower-priority work. This is especially important in games where some operations (like player input handling or AI for nearby enemies) need faster response times than others (like distant entity updates or background calculations).

## Thread Safety

All public methods of the ThreadSystem are thread-safe and can be called from any thread. The system handles proper synchronization internally, allowing game systems to enqueue tasks without additional synchronization.
