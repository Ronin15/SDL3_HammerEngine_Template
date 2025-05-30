# ThreadSystem API Reference

## Class Overview

The Forge engine ThreadSystem provides a robust thread pool implementation with automatic task queue management, priority-based task scheduling, and synchronization features. This document serves as a complete API reference.

## Namespace

All ThreadSystem classes and functions are within the `Forge` namespace.

## Core Classes

### ThreadSystem

Singleton class that manages the thread pool and task queue.

#### Static Methods

| Method | Description |
|--------|-------------|
| `static ThreadSystem& Instance()` | Returns the singleton instance of ThreadSystem |

#### Initialization and Cleanup

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY, unsigned int customThreadCount = 0, bool enableProfiling = false)` | `queueCapacity`: Initial task queue capacity (optional)<br>`customThreadCount`: Custom thread count (0 for auto-detect)<br>`enableProfiling`: Enable detailed task profiling | `bool` | Initializes the thread system and pre-allocates memory for the task queue. The initial capacity determines how many tasks can be stored before requiring reallocation. |
| `void clean()` | None | `void` | Cleans up and releases all thread system resources |

#### Task Submission

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void enqueueTask(std::function<void()> task, TaskPriority priority = TaskPriority::Normal, const std::string& description = "")` | `task`: Function to execute<br>`priority`: Task priority level (optional)<br>`description`: Optional description for debugging | `void` | Adds a fire-and-forget task to the queue with specified priority |
| `template<class F, class... Args> auto enqueueTaskWithResult(F&& f, TaskPriority priority = TaskPriority::Normal, const std::string& description = "", Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>` | `f`: Function to execute<br>`priority`: Task priority level (optional)<br>`description`: Optional description for debugging<br>`args`: Arguments to pass to the function | `std::future<T>` | Adds a task that returns a result with specified priority |

#### Status and Information

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `static bool Exists()` | None | `bool` | Returns true if the ThreadSystem has been initialized and not shut down |
| `unsigned int getThreadCount() const` | None | `unsigned int` | Returns the number of worker threads |
| `bool isBusy() const` | None | `bool` | Returns true if the thread pool has pending tasks |
| `bool isShutdown() const` | None | `bool` | Returns true if the thread system has been shut down |
| `void setDebugLogging(bool enable)` | `enable`: Whether to enable debug logging | `void` | Enables or disables detailed debug logging for task operations |

#### Queue Management

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `size_t getQueueCapacity() const` | None | `size_t` | Returns the current capacity of the task queue |
| `size_t getQueueSize() const` | None | `size_t` | Returns the current number of tasks in the queue |
| `bool reserveQueueCapacity(size_t capacity)` | `capacity`: New capacity to reserve | `bool` | Pre-allocates memory for the specified number of tasks. Returns false if system is shut down. |
| `size_t getTotalTasksProcessed() const` | None | `size_t` | Returns the total number of tasks that have been processed |
| `size_t getTotalTasksEnqueued() const` | None | `size_t` | Returns the total number of tasks that have been enqueued |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `DEFAULT_QUEUE_CAPACITY` | `static constexpr size_t` | 1024 | Default capacity for the task queue |

#### Enumerations

| Enumeration | Values | Description |
|------------|--------|-------------|
| `TaskPriority` | `Critical` (0)<br>`High` (1)<br>`Normal` (2)<br>`Low` (3)<br>`Idle` (4) | Priority levels for task execution |

### ThreadPool

Internal class that manages the pool of worker threads.

#### Constructor and Destructor

| Method | Parameters | Description |
|--------|------------|-------------|
| `ThreadPool(size_t numThreads, size_t queueCapacity = 256, bool enableProfiling = false)` | `numThreads`: Number of worker threads<br>`queueCapacity`: Initial task queue capacity<br>`enableProfiling`: Enable detailed performance metrics | Constructs a thread pool with specified thread count and pre-allocates memory for the task queue |
| `~ThreadPool()` | None | Cleans up the thread pool and joins all worker threads |

#### Methods

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void enqueue(std::function<void()> task, TaskPriority priority = TaskPriority::Normal, const std::string& description = "")` | `task`: Function to execute<br>`priority`: Task priority level<br>`description`: Optional description | `void` | Adds a task to the queue with specified priority |
| `bool busy()` | None | `bool` | Returns true if the thread pool has pending tasks |
| `TaskQueue& getTaskQueue()` | None | `TaskQueue&` | Returns a reference to the task queue for management operations |

### TaskQueue

Internal class that manages the queue of pending tasks.

#### Constructor

| Method | Parameters | Description |
|--------|------------|-------------|
| `TaskQueue(size_t initialCapacity = 256, bool enableProfiling = false)` | `initialCapacity`: Initial capacity for the task queue<br>`enableProfiling`: Enable detailed performance metrics | Constructs a task queue with the specified initial capacity and pre-allocates memory for efficient task storage |

#### Methods

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void push(std::function<void()> task, TaskPriority priority = TaskPriority::Normal, const std::string& description = "")` | `task`: Function to execute<br>`priority`: Task priority level<br>`description`: Optional description | `void` | Adds a task to the queue with specified priority |
| `bool pop(std::function<void()>& task)` | `task`: Reference to store the popped task | `bool` | Removes a task from the queue |
| `void stop()` | None | `void` | Stops the queue and clears all pending tasks |
| `bool isEmpty()` | None | `bool` | Returns true if the queue is empty |
| `bool isStopping() const` | None | `bool` | Returns true if the queue is in stopping state |
| `void reserve(size_t capacity)` | `capacity`: New capacity to reserve | `void` | Pre-allocates memory for the specified number of tasks |
| `size_t capacity() const` | None | `size_t` | Returns the current capacity of the queue |
| `size_t size() const` | None | `size_t` | Returns the current number of tasks in the queue |
| `size_t getTotalTasksProcessed() const` | None | `size_t` | Returns total tasks processed by this queue |
| `size_t getTotalTasksEnqueued() const` | None | `size_t` | Returns total tasks enqueued to this queue |
| `void setProfilingEnabled(bool enabled)` | `enabled`: Whether to enable profiling | `void` | Enables or disables detailed task profiling |
| `void notifyAllThreads()` | None | `void` | Wakes up all waiting worker threads |

## Usage Examples

### Basic Initialization and Cleanup

```cpp
// Initialize with default settings (recommended approach)
try {
    if (!Forge::ThreadSystem::Instance().init()) {
        std::cerr << "Failed to initialize thread system!" << std::endl;
        return -1;
    }
} catch (const std::exception& e) {
    std::cerr << "Exception during thread system initialization: " << e.what() << std::endl;
    return -1;
}

// Initialize with custom parameters (queue capacity, thread count, profiling)
if (!Forge::ThreadSystem::Instance().init(2048, 6, true)) {
    std::cerr << "Failed to initialize thread system with custom settings!" << std::endl;
    return -1;
}

// Check if ThreadSystem is available
if (!Forge::ThreadSystem::Exists()) {
    std::cerr << "ThreadSystem is not available!" << std::endl;
    return -1;
}

// Use the thread system...

// Clean up when done
Forge::ThreadSystem::Instance().clean();
```

### Task Submission

```cpp
// Fire-and-forget task with error handling and priority
try {
    // Normal priority (default) with description
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
            // Perform work that doesn't return a value
            processData();
        } catch (const std::exception& e) {
            std::cerr << "Task execution error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::Normal, "ProcessData");
    
    // High priority for critical operations
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
            // Perform critical work
            processImportantData();
        } catch (const std::exception& e) {
            std::cerr << "Critical task execution error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::High, "ProcessCriticalData");
} catch (const std::exception& e) {
    std::cerr << "Failed to enqueue task: " << e.what() << std::endl;
}

// Task with result, error handling, and priority
try {
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
        []() -> int {
            try {
                // Perform work and return a value
                return calculateResult();
            } catch (const std::exception& e) {
                std::cerr << "Task execution error: " << e.what() << std::endl;
                return -1; // Return error code
            }
        }, 
        Forge::TaskPriority::High, 
        "CalculateResult"
    );

    // Wait for and use the result
    try {
        int result = future.get();
    } catch (const std::exception& e) {
        std::cerr << "Exception from task: " << e.what() << std::endl;
    }
} catch (const std::exception& e) {
    std::cerr << "Failed to enqueue task: " << e.what() << std::endl;
}
```

### Smart Pointer Usage with ThreadSystem

```cpp
// Example with smart pointers (recommended for thread safety)
void updateEntities(const std::vector<std::shared_ptr<Entity>>& entities) {
    for (auto entity : entities) {
        try {
            Forge::ThreadSystem::Instance().enqueueTask([entity]() {
                try {
                    entity->update();
                } catch (const std::exception& e) {
                    std::cerr << "Entity update error: " << e.what() << std::endl;
                }
            }, Forge::TaskPriority::Normal, "EntityUpdate");
        } catch (const std::exception& e) {
            std::cerr << "Failed to enqueue entity task: " << e.what() << std::endl;
        }
    }
}

// Task with smart pointer result
try {
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
        []() -> std::shared_ptr<GameData> {
            try {
                return std::make_shared<GameData>(loadGameData());
            } catch (const std::exception& e) {
                std::cerr << "Data loading error: " << e.what() << std::endl;
                return nullptr;
            }
        },
        Forge::TaskPriority::High,
        "LoadGameData"
    );

    // Use the smart pointer result
    try {
        auto gameData = future.get();
        if (gameData) {
            // Use the loaded data
            gameData->process();
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception from data loading task: " << e.what() << std::endl;
    }
} catch (const std::exception& e) {
    std::cerr << "Failed to enqueue data loading task: " << e.what() << std::endl;
}
```

### Queue Capacity Management

```cpp
// Typically, you should use the default capacity (recommended)
Forge::ThreadSystem::Instance().init();

// Monitor usage (for debugging or performance analysis)
size_t currentSize = Forge::ThreadSystem::Instance().getQueueSize();
size_t capacity = Forge::ThreadSystem::Instance().getQueueCapacity();
size_t processed = Forge::ThreadSystem::Instance().getTotalTasksProcessed();
size_t enqueued = Forge::ThreadSystem::Instance().getTotalTasksEnqueued();

std::cout << "Queue: " << currentSize << "/" << capacity 
          << ", Processed: " << processed << ", Enqueued: " << enqueued << std::endl;

// Enable debug logging for detailed task information
Forge::ThreadSystem::Instance().setDebugLogging(true);

// The system automatically expands capacity when needed (at 90% utilization)
// Custom capacity is only needed when you know a large burst is coming:
if (tasksToProcess > 1000 && tasksToProcess > capacity) {
    // Pre-allocate memory for all tasks to avoid mid-burst reallocation
    if (!Forge::ThreadSystem::Instance().reserveQueueCapacity(tasksToProcess)) {
        std::cerr << "Failed to reserve capacity - system may be shut down" << std::endl;
    }
}
```

## Thread Safety

All public methods of the ThreadSystem, ThreadPool, and TaskQueue classes are thread-safe and can be called concurrently from multiple threads.

### Safe Shutdown Handling
The ThreadSystem gracefully handles shutdown scenarios:
- Tasks enqueued after shutdown are silently ignored (no crashes)
- `enqueueTaskWithResult()` returns futures with default values instead of throwing exceptions
- Pending tasks are reported during cleanup
- Worker threads are properly joined with timeout protection
- The system can be safely destroyed at application exit

### Thread Pool Architecture
- Uses separate priority queues to reduce lock contention
- Lock-free existence checks for performance
- Automatic capacity expansion when queues reach 90% utilization
- Thread count automatically determined as `hardware_concurrency - 1`

## Error Handling

- The `init()` method returns false if initialization fails or system is already shut down
- The `enqueueTask()` method silently ignores tasks after shutdown (check with `Exists()` first)
- The `enqueueTaskWithResult()` method returns futures with default values after shutdown
- The `reserveQueueCapacity()` method returns false if system is shut down
- Task exceptions are propagated to the caller via `std::future::get()`
- All methods handle shutdown state gracefully without throwing exceptions

## Performance Considerations

- The thread system uses separate priority queues to reduce lock contention
- Manual capacity management with `reserveQueueCapacity()` is only needed for anticipating large bursts of tasks
- Always include proper exception handling in your tasks
- Avoid creating many tiny tasks; batch related work when possible
- The default thread pool size (cores-1) is optimal for most applications
- Focus on task design rather than capacity management
- Use task priorities appropriately:
  - `Critical`: Only for vital system operations that must not be delayed
  - `High`: For important operations needing quick responses
  - `Normal`: For standard operations (default)
  - `Low`: For background operations
  - `Idle`: For non-essential operations
- Avoid creating too many high-priority tasks that could starve lower-priority tasks

### Memory Management
- Each priority queue pre-allocates memory independently (initial capacity / 5 per priority)
- Automatic expansion at 90% utilization for each priority level
- Lock-free checks for better performance in high-throughput scenarios
- Debug logging can be enabled for development but should be disabled in production

### Best Practices for High Performance
- Use `Forge::ThreadSystem::Exists()` to check availability before enqueueing tasks
- Enable profiling during development: `init(capacity, threadCount, true)`
- Monitor queue usage with `getQueueSize()`, `getTotalTasksProcessed()`, `getTotalTasksEnqueued()`
- Use appropriate task descriptions for debugging and profiling
- Consider task granularity: optimal tasks take 0.1-1ms to execute
