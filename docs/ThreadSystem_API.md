# ThreadSystem API Reference

## Class Overview

The Forge engine ThreadSystem provides a robust thread pool implementation with automatic task queue management and synchronization features. This document serves as a complete API reference.

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
| `bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY)` | `queueCapacity`: Initial task queue capacity (optional) | `bool` | Initializes the thread system with automatic capacity management. The initial capacity parameter is optional and rarely needed. |
| `void clean()` | None | `void` | Cleans up and releases all thread system resources |

#### Task Submission

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void enqueueTask(std::function<void()> task)` | `task`: Function to execute | `void` | Adds a fire-and-forget task to the queue |
| `template<class F, class... Args> auto enqueueTaskWithResult(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>` | `f`: Function to execute<br>`args`: Arguments to pass to the function | `std::future<T>` | Adds a task that returns a result |

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
| `bool reserveQueueCapacity(size_t capacity)` | `capacity`: New capacity to reserve | `bool` | Reserves memory for the specified number of tasks. Note: This is rarely needed as capacity is managed automatically. |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `DEFAULT_QUEUE_CAPACITY` | `static constexpr size_t` | 512 | Default capacity for the task queue |

### ThreadPool

Internal class that manages the pool of worker threads.

#### Constructor and Destructor

| Method | Parameters | Description |
|--------|------------|-------------|
| `ThreadPool(size_t numThreads, size_t queueCapacity = 256)` | `numThreads`: Number of worker threads<br>`queueCapacity`: Initial task queue capacity | Constructs a thread pool with specified thread count and queue capacity |
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
| `TaskQueue(size_t initialCapacity = 256)` | `initialCapacity`: Initial capacity for the task queue | Constructs a task queue with the specified initial capacity |

#### Methods

| Method | Parameters | Return Type | Description |
|--------|------------|-------------|-------------|
| `void push(std::function<void()> task)` | `task`: Function to execute | `void` | Adds a task to the queue |
| `bool pop(std::function<void()>& task)` | `task`: Reference to store the popped task | `bool` | Removes a task from the queue |
| `void stop()` | None | `void` | Stops the queue and clears all pending tasks |
| `bool isEmpty()` | None | `bool` | Returns true if the queue is empty |
| `void reserve(size_t capacity)` | `capacity`: New capacity to reserve | `void` | Reserves memory for the specified number of tasks |
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
// Fire-and-forget task with error handling
try {
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        try {
            // Perform work that doesn't return a value
            processData();
        } catch (const std::exception& e) {
            std::cerr << "Task execution error: " << e.what() << std::endl;
        }
    });
} catch (const std::exception& e) {
    std::cerr << "Failed to enqueue task: " << e.what() << std::endl;
}

// Task with result and error handling
try {
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
        try {
            // Perform work and return a value
            return calculateResult();
        } catch (const std::exception& e) {
            std::cerr << "Task execution error: " << e.what() << std::endl;
            return -1; // Return error code
        }
    });

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

// Custom capacity is only needed in rare cases with extreme requirements
// For example, when handling thousands of tasks simultaneously:
if (tasksToProcess > 5000) {
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

- The thread system automatically manages capacity for most workloads
- Manual capacity management with `reserveQueueCapacity()` is rarely needed
- Always include proper exception handling in your tasks
- Avoid creating many tiny tasks; batch related work when possible
- The default thread pool size (cores-1) is optimal for most applications
- Focus on task design rather than capacity management
