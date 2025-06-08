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
// Initialize the thread system with default settings (1024 queue capacity, auto thread count)
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}

// Initialize with custom parameters
if (!Forge::ThreadSystem::Instance().init(2048, 4, true)) {  // 2048 queue capacity, 4 threads, profiling enabled
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

// EventManager automatically uses ThreadSystem when available
// No explicit configuration needed - it detects ThreadSystem automatically

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

// AIManager automatically uses ThreadSystem when available
// No explicit configuration needed - it detects ThreadSystem automatically

// AIManager will now process AI behaviors in parallel through ThreadSystem
AIManager::Instance().updateManagedEntities();
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
// Initialize with default capacity (recommended approach - 4096 tasks)
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}

// Specify custom initial capacity, thread count, and profiling
if (!Forge::ThreadSystem::Instance().init(8192, 0, false)) {  // 8192 capacity, auto threads, no profiling
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}
```

// Initialize with specific thread count
if (!Forge::ThreadSystem::Instance().init(4096, 6, true)) {  // 4096 capacity, 6 threads, profiling enabled
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}
```

### Adjusting Capacity at Runtime (Rarely Needed)

```cpp
// Reserve additional capacity if you know you'll submit many tasks
bool success = Forge::ThreadSystem::Instance().reserveQueueCapacity(8192);
if (!success) {
    std::cerr << "Failed to reserve queue capacity (system may be shut down)" << std::endl;
}
```

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

// Get task processing statistics
size_t processed = Forge::ThreadSystem::Instance().getTotalTasksProcessed();
size_t enqueued = Forge::ThreadSystem::Instance().getTotalTasksEnqueued();

// Enable debug logging for detailed task information
Forge::ThreadSystem::Instance().setDebugLogging(true);

std::cout << "Queue usage: " << queueSize << "/" << capacity << std::endl;
std::cout << "Tasks processed: " << processed << ", enqueued: " << enqueued << std::endl;
```

### Debug Logging

```cpp
// Enable debug logging during development
Forge::ThreadSystem::Instance().setDebugLogging(true);

// Now all task enqueuing will be logged with descriptions
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // Task logic
}, Forge::TaskPriority::High, "Update player AI");

// Output: "Forge Game Engine - Enqueuing task: Update player AI"

// Disable for production
Forge::ThreadSystem::Instance().setDebugLogging(false);
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
   - Use `getQueueSize()`, `getQueueCapacity()`, `getTotalTasksProcessed()`, and `getTotalTasksEnqueued()` during development to understand usage patterns
   - If queue size regularly approaches capacity, consider optimizing your task design
   - Enable profiling during development with `init(capacity, threadCount, true)` for detailed performance insights

5. **Consider Task Granularity**
   - Optimal task size is typically 0.1-1ms per task
   - Too small tasks (<0.05ms) create excessive overhead
   - Too large tasks (>5ms) can cause load imbalance
   - See [ThreadSystem Task](ThreadSystem_Optimization.md) for detailed guidance

6. **Add Proper Exception Handling**
   - Always handle exceptions that might occur in threaded tasks
   - Ensure your code is resilient to thread task failures

7. **Use Debug Logging for Development**
   - Enable debug logging with `Forge::ThreadSystem::Instance().setDebugLogging(true)` during development
   - Provides detailed information about task enqueueing and execution
   - Remember to disable in production builds for performance

8. **Check System Status**
   - Use `Forge::ThreadSystem::Exists()` to verify ThreadSystem is available before enqueueing tasks
   - Useful for systems that may operate both with and without ThreadSystem

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

The ThreadSystem uses a custom priority queue implementation with separate queues for each priority level. This design reduces lock contention and provides better memory management than standard containers. All operations are thread-safe with proper synchronization to ensure correct behavior in a multi-threaded environment.

### Architecture Features
- **Separate Priority Queues**: Each priority level has its own queue to minimize lock contention
- **Automatic Thread Detection**: Uses `std::thread::hardware_concurrency() - 1` threads by default
- **Graceful Shutdown**: Proper cleanup with timeout handling and pending task reporting
- **Memory Pre-allocation**: Each priority queue reserves capacity to reduce runtime allocations
- **Lock-free Checks**: Fast existence and shutdown detection without acquiring locks

The implementation efficiently handles:
- Task creation and enqueueing with priority-based ordering using separate priority queues
- Intelligent memory pre-allocation and automatic growth when reaching 90% capacity
- Thread-safe access to the task queue with minimal lock contention
- Proper propagation of exceptions and return values
- Automatic thread pool scaling based on available CPU cores

Tasks are processed according to their priority level, with higher-priority tasks being executed before lower-priority ones. The system uses separate queues for each priority level to improve performance and reduce lock contention.

### Priority Levels

The ThreadSystem supports five priority levels for tasks:

- `Forge::TaskPriority::Critical` (0): For mission-critical operations that must execute immediately
- `Forge::TaskPriority::High` (1): For important tasks that need quick responses
- `Forge::TaskPriority::Normal` (2): Default priority for standard tasks
- `Forge::TaskPriority::Low` (3): For background or non-time-sensitive tasks
- `Forge::TaskPriority::Idle` (4): For very low-priority tasks that should only run when the system is idle

## Performance Characteristics

The system is designed to efficiently handle hundreds of concurrent tasks with dynamic memory management for optimal memory usage and processing efficiency. The task queue starts with a default capacity of 1024 tasks distributed across priority levels and can automatically grow as needed.

The priority-based scheduling with separate queues ensures that critical tasks receive timely execution without being delayed by lower-priority work. This is especially important in games where some operations (like player input handling or AI for nearby enemies) need faster response times than others (like distant entity updates or background calculations).

Key performance features:
- Separate priority queues reduce lock contention
- Automatic capacity expansion at 90% utilization
- Lock-free existence checks for shutdown detection
- Efficient memory pre-allocation per priority level
- Thread pool sizing based on available CPU cores (hardware_concurrency - 1)

## Thread Safety

All public methods of the ThreadSystem are thread-safe and can be called from any thread. The system handles proper synchronization internally, allowing game systems to enqueue tasks without additional synchronization.

### Safe Shutdown Handling
The ThreadSystem gracefully handles shutdown scenarios:
- Tasks enqueued after shutdown are silently ignored (no crashes)
- Pending tasks are reported during cleanup
- Worker threads are properly joined with timeout protection
- The system can be safely destroyed at application exit

### Available Methods Summary
- `init(queueCapacity, customThreadCount, enableProfiling)` - Initialize with custom parameters
- `enqueueTask(task, priority, description)` - Submit fire-and-forget tasks
- `enqueueTaskWithResult(function, priority, description, args...)` - Submit tasks with return values
- `getThreadCount()` - Get number of worker threads
- `getQueueSize()` / `getQueueCapacity()` - Monitor queue status
- `getTotalTasksProcessed()` / `getTotalTasksEnqueued()` - Get processing statistics
- `reserveQueueCapacity(capacity)` - Pre-allocate additional queue space
- `setDebugLogging(enabled)` - Enable/disable detailed logging
- `clean()` - Explicit cleanup (automatic in destructor)
- `Exists()` - Check if system is available and not shut down

## Worker Budget System

The ThreadSystem now includes a sophisticated worker budget allocation system to prevent resource contention between game subsystems. This system ensures fair thread distribution and prevents any single component from overwhelming the thread pool.

### Architecture Overview

The worker budget system divides available threads among major engine subsystems:
- **GameEngine**: Reserved workers for critical game loop operations
- **AIManager**: Allocated workers for entity behavior processing
- **EventManager**: Allocated workers for event processing
- **Buffer**: Remaining workers for other tasks and system responsiveness

### Budget Allocation Strategy

The system uses a hierarchical allocation strategy:

1. **GameEngine Reservation**: Always gets minimum required workers for critical operations
2. **Percentage-Based Distribution**: Remaining workers distributed by percentage
3. **Minimum Guarantees**: Each subsystem gets at least 1 worker
4. **Buffer Maintenance**: Some workers left free for responsiveness

### Worker Budget by Processor Count

| Logical Cores | Total Workers | Engine | AI (60%) | Events (30%) | Buffer (10%) |
|---------------|---------------|--------|----------|--------------|-------------- |
| 2             | 1             | 1      | 1        | 0            | 0             |
| 4             | 3             | 2      | 1        | 1            | 0             |
| 6             | 5             | 2      | 1        | 1            | 1             |
| 8             | 7             | 2      | 3        | 1            | 1             |
| 12            | 11            | 2      | 5        | 2            | 2             |
| 16            | 15            | 2      | 7        | 3            | 3             |
| 24            | 23            | 2      | 13       | 6            | 2             |
| 32            | 31            | 2      | 17       | 8            | 4             |

*Note: ThreadSystem automatically reserves 1 core for the main thread (hardware_concurrency - 1)*

### Implementation Details

#### WorkerBudget Utility
```cpp
#include "utils/WorkerBudget.hpp"

// Calculate optimal allocation for current hardware
Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

// Access allocated worker counts
size_t aiWorkers = budget.aiAllocated;      // 60% of remaining
size_t eventWorkers = budget.eventAllocated; // 30% of remaining
size_t engineWorkers = budget.engineReserved; // Minimum 2
```

#### Queue Pressure Management
Each subsystem monitors queue pressure to prevent overload:

```cpp
size_t currentQueueSize = threadSystem.getQueueSize();
size_t maxQueuePressure = availableWorkers * 2;

if (currentQueueSize < maxQueuePressure) {
    // Safe to submit batches
    submitThreadedTasks();
} else {
    // Fall back to single-threaded processing
    processSingleThreaded();
}
```

### Benefits

1. **Prevents Thread Starvation**: No single subsystem can monopolize workers
2. **Hardware Adaptive**: Automatically scales with available processor cores
3. **Performance Optimized**: Maintains high throughput while ensuring responsiveness
4. **Graceful Degradation**: Falls back to single-threaded under pressure
5. **System Stability**: Prevents ThreadSystem queue overflow

### Performance Impact

The worker budget system delivers significant performance improvements:
- **AI Threading**: Up to 12x speedup on large entity counts
- **Event Processing**: Efficient parallel processing without contention
- **System Responsiveness**: GameEngine always has reserved workers
- **Scalability**: Performance scales naturally with hardware

### Best Practices for Subsystems

1. **Respect Budget Limits**: Never submit more batches than allocated workers
2. **Monitor Queue Pressure**: Check queue size before submitting large batches
3. **Use Appropriate Priorities**: Critical tasks use `TaskPriority::Critical`
4. **Graceful Fallback**: Implement single-threaded fallback paths
5. **Batch Sizing**: Size batches appropriately for worker count

### Configuration

The worker budget percentages can be adjusted in `utils/WorkerBudget.hpp`:

```cpp
static constexpr size_t AI_WORKER_PERCENTAGE = 60;     // AI gets 60%
static constexpr size_t EVENT_WORKER_PERCENTAGE = 30;  // Events get 30%
static constexpr size_t ENGINE_MIN_WORKERS = 2;        // Engine minimum
```

This system ensures optimal performance across all hardware configurations while maintaining system stability and responsiveness.
