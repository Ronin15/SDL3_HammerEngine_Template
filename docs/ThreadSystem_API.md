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
| `bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY)` | `queueCapacity`: Initial task queue capacity (optional) | `bool` | Initializes the thread system and pre-allocates memory for the task queue. The initial capacity determines how many tasks can be stored before requiring reallocation. |
| `void clean()` | None | `void` | Cleans up and releases all thread system resources |

#### Task Submission

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void enqueueTask(std::function<void()> task, TaskPriority priority = TaskPriority::Normal)` | `task`: Function to execute<br>`priority`: Task priority level (optional) | `void` | Adds a fire-and-forget task to the queue with specified priority |
| `template<class F, class... Args> auto enqueueTaskWithResult(F&& f, Args&&... args, TaskPriority priority = TaskPriority::Normal) -> std::future<typename std::invoke_result<F, Args...>::type>` | `f`: Function to execute<br>`args`: Arguments to pass to the function<br>`priority`: Task priority level (optional) | `std::future<T>` | Adds a task that returns a result with specified priority |

#### Status and Information

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `unsigned int getThreadCount() const` | None | `unsigned int` | Returns the number of worker threads |
| `bool isBusy() const` | None | `bool` | Returns true if the thread pool has pending tasks |
| `bool isShutdown() const` | None | `bool` | Returns true if the thread system has been shut down |

#### Queue Management

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `size_t getQueueCapacity() const` | None | `size_t` | Returns the current capacity of the task queue |
| `size_t getQueueSize() const` | None | `size_t` | Returns the current number of tasks in the queue |
| `bool reserveQueueCapacity(size_t capacity)` | `capacity`: New capacity to reserve | `bool` | Actually pre-allocates memory for the specified number of tasks. This operation will safely migrate existing tasks to the newly allocated memory. |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `DEFAULT_QUEUE_CAPACITY` | `static constexpr size_t` | 512 | Default capacity for the task queue |

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
| `void enqueue(std::function<void()> task)` | `task`: Function to execute | `void` | Adds a task to the queue |
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
| `void push(std::function<void()> task)` | `task`: Function to execute | `void` | Adds a task to the queue |
| `bool pop(std::function<void()>& task)` | `task`: Reference to store the popped task | `bool` | Removes a task from the queue |
| `void stop()` | None | `void` | Stops the queue and clears all pending tasks |
| `bool isEmpty()` | None | `bool` | Returns true if the queue is empty |
| `void reserve(size_t capacity)` | `capacity`: New capacity to reserve | `void` | Pre-allocates memory for the specified number of tasks, migrating existing tasks to the new memory allocation |
| `size_t capacity() const` | None | `size_t` | Returns the current capacity of the queue |
| `size_t size() const` | None | `size_t` | Returns the current number of tasks in the queue |

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

// Use the thread system...

// Clean up when done
Forge::ThreadSystem::Instance().clean();
```

### Task Submission

```cpp
### Fire-and-forget task with error handling and priority
try {
    // Normal priority (default)
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
            // Perform work that doesn't return a value
            processData();
        } catch (const std::exception& e) {
            std::cerr << "Task execution error: " << e.what() << std::endl;
        }
    });
    
    // High priority for critical operations
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
            // Perform critical work
            processImportantData();
        } catch (const std::exception& e) {
            std::cerr << "Critical task execution error: " << e.what() << std::endl;
        }
    }, Forge::TaskPriority::High);
} catch (const std::exception& e) {
    std::cerr << "Failed to enqueue task: " << e.what() << std::endl;
}

// Task with result, error handling, and priority
try {
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
        try {
            // Perform work and return a value
            return calculateResult();
        } catch (const std::exception& e) {
            std::cerr << "Task execution error: " << e.what() << std::endl;
            return -1; // Return error code
        }
    }, Forge::TaskPriority::High); // Set high priority for this task

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

### Queue Capacity Management

```cpp
// Typically, you should use the default capacity (recommended)
Forge::ThreadSystem::Instance().init();

// Monitor usage (for debugging or performance analysis)
size_t currentSize = Forge::ThreadSystem::Instance().getQueueSize();
size_t capacity = Forge::ThreadSystem::Instance().getQueueCapacity();

// The system automatically expands capacity when needed (at 90% utilization)
// Custom capacity is only needed when you know a large burst is coming:
if (tasksToProcess > 1000 && tasksToProcess > capacity) {
    // Pre-allocate memory for all tasks to avoid mid-burst reallocation
    Forge::ThreadSystem::Instance().reserveQueueCapacity(tasksToProcess);
}
```

## Thread Safety

All public methods of the ThreadSystem, ThreadPool, and TaskQueue classes are thread-safe and can be called concurrently from multiple threads.

## Error Handling

- The `init()` method returns false if initialization fails
- The `enqueueTaskWithResult()` method throws `std::runtime_error` if called after shutdown
- Task exceptions are propagated to the caller via `std::future::get()`

## Performance Considerations

- The thread system pre-allocates memory and manages capacity for optimal performance
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
