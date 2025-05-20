# ThreadSystem Documentation

## Overview

The ThreadSystem is a core component of the Forge Game Engine, providing thread pool management and task-based concurrency. It allows game systems to enqueue work that gets processed by a pool of worker threads, enabling performance benefits from multi-core processors while maintaining a simplified programming model. The system is optimized to efficiently handle up to 500 concurrent tasks (see [Defining a Task](ThreadSystem_Optimization.md) for details).

## Features

- Thread pool with automatic sizing based on available CPU cores
- Task-based programming model with simple enqueue interface
- Support for fire-and-forget tasks and tasks with return values
- Thread-safe operations with proper synchronization
- Queue capacity reservation for memory optimization
- Clean shutdown with proper resource management

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
// Simple fire-and-forget task
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // Task logic here
    std::cout << "Executing task on thread pool" << std::endl;
});

// Task with a return value
auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
    // Task logic here
    return 42;
});

// Wait for and use the result
int result = future.get();  // Blocks until the task completes
```

### Shutdown

```cpp
// Clean up the thread system when your application exits
Forge::ThreadSystem::Instance().clean();
```

## Queue Capacity Management

The ThreadSystem supports pre-allocation of memory for the task queue, which improves performance and reduces memory fragmentation.

### Setting Initial Capacity

```cpp
// Initialize with capacity for 500 tasks
if (!Forge::ThreadSystem::Instance().init(500)) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}
```

### Adjusting Capacity at Runtime

```cpp
// Reserve capacity for upcoming tasks
Forge::ThreadSystem::Instance().reserveQueueCapacity(1000);
```

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

By pre-allocating the task queue, the system reduces memory fragmentation that would otherwise occur from frequent allocations and deallocations. This is particularly important for long-running applications like games.

### Improved Cache Locality

The contiguous memory layout of the reserved queue improves cache performance when worker threads access tasks, leading to better throughput and reduced cache misses.

### Consistent Performance

Eliminating dynamic resizing of the task queue helps maintain consistent performance during gameplay, avoiding hitches that might otherwise occur during memory reallocation.

## Best Practices

1. **Set Appropriate Initial Capacity**
   - For most games, set capacity to the maximum expected concurrent task count
   - The default of 512 is suitable for games with moderate parallelism (around 500 active entities)

2. **Reserve Before Batch Submissions**
   - Before submitting a large batch of tasks, ensure the queue has sufficient capacity
   - Example: Reserve capacity before level loading or particle system updates

3. **Monitor During Development**
   - Use `getQueueSize()` and `getQueueCapacity()` during development to understand usage patterns
   - Adjust capacity based on actual usage

4. **Balance Memory Usage**
   - Setting too large a capacity wastes memory
   - Setting too small a capacity causes reallocations
   - Aim for 1.5-2x your typical peak usage

5. **Consider Task Granularity**
   - Optimal task size is typically 0.1-1ms per task
   - Too small tasks (<0.05ms) create excessive overhead
   - Too large tasks (>5ms) can cause load imbalance
   - See [ThreadSystem Task](ThreadSystem_Optimization.md) for detailed guidance

## Example Scenarios

### Game Entity Updates

```cpp
// Process 500 game entities in parallel
void updateEntities(const std::vector<Entity*>& entities) {
    // Reserve capacity for all entity tasks
    Forge::ThreadSystem::Instance().reserveQueueCapacity(entities.size());

    // Submit update tasks
    for (Entity* entity : entities) {
        Forge::ThreadSystem::Instance().enqueueTask([entity]() {
            entity->update();
        });
    }
}
```

### Asset Loading

```cpp
// Load multiple assets in parallel
void loadAssets(const std::vector<std::string>& assetPaths) {
    // Reserve capacity for all asset loading tasks
    Forge::ThreadSystem::Instance().reserveQueueCapacity(assetPaths.size());

    std::vector<std::future<bool>> results;

    // Submit asset loading tasks
    for (const auto& path : assetPaths) {
        results.push_back(
            Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                [path]() -> bool {
                    return loadAssetFromDisk(path);
                }
            )
        );
    }

    // Wait for and process results
    for (auto& future : results) {
        bool success = future.get();
        // Handle result
    }
}
```

## Technical Details

The ThreadSystem uses a vector-based task queue with pre-allocation support, enabling better memory management than the standard queue implementation. All operations are thread-safe with proper synchronization to ensure correct behavior in a multi-threaded environment.

The implementation efficiently handles task creation, dispatch, and completion, with special care taken to ensure proper propagation of exceptions and return values.

## Performance Characteristics

The system is designed to handle approximately 500 concurrent tasks with optimal memory usage and processing efficiency. This capacity supports rich game worlds with hundreds of active entities and complex simulations. For a detailed explanation of what "500 tasks" means in practice and how it translates to game features, see the [ThreadSystem Task](ThreadSystem_Optimization.md) document.

## Thread Safety

All public methods of the ThreadSystem are thread-safe and can be called from any thread. The system handles proper synchronization internally, allowing game systems to enqueue tasks without additional synchronization.
