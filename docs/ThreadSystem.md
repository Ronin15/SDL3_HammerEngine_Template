# ThreadSystem Documentation

## Overview

The ThreadSystem is a core component of the Forge Game Engine, providing thread pool management and task-based concurrency. It allows game systems to enqueue work that gets processed by a pool of worker threads, enabling performance benefits from multi-core processors while maintaining a simplified programming model. The system automatically manages its capacity and is designed to efficiently handle hundreds of concurrent tasks (see [Defining a Task](ThreadSystem_Optimization.md) for details).

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

The ThreadSystem automatically manages memory for the task queue. In most cases, you shouldn't need to worry about queue capacity as the system handles this internally.

### Setting Initial Capacity (Optional)

```cpp
// Initialize with default capacity (recommended approach)
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

```cpp
// Note: In most cases, this is NOT necessary as the system manages capacity automatically
// Only use this if you have specific performance requirements
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

1. **Use Default Capacity When Possible**
   - For most games, the default capacity (512) works well and adjusts automatically
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
void updateEntities(const std::vector<Entity*>& entities) {
    // Submit update tasks - no need to manually reserve capacity
    for (Entity* entity : entities) {
        Forge::ThreadSystem::Instance().enqueueTask([entity]() {
            try {
                entity->update();
            } catch (const std::exception& e) {
                std::cerr << "Error updating entity: " << e.what() << std::endl;
            }
        });
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

The ThreadSystem uses a vector-based task queue with pre-allocation support, enabling better memory management than the standard queue implementation. All operations are thread-safe with proper synchronization to ensure correct behavior in a multi-threaded environment.

The implementation efficiently handles task creation, dispatch, and completion, with special care taken to ensure proper propagation of exceptions and return values.

## Performance Characteristics

The system is designed to efficiently handle hundreds of concurrent tasks with dynamic memory management for optimal memory usage and processing efficiency. The task queue starts with a default capacity of 512 tasks and can automatically grow as needed. This capacity supports rich game worlds with hundreds of active entities and complex simulations. For a detailed explanation of task design and performance implications, see the [ThreadSystem Task](ThreadSystem_Optimization.md) document.

## Thread Safety

All public methods of the ThreadSystem are thread-safe and can be called from any thread. The system handles proper synchronization internally, allowing game systems to enqueue tasks without additional synchronization.
