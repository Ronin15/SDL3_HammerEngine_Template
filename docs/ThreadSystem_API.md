# ThreadSystem API Reference

## Class Overview

The Forge engine ThreadSystem provides a robust thread pool implementation with task queuing and synchronization features. This document serves as a complete API reference.

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
| `bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY)` | `queueCapacity`: Initial task queue capacity | `bool` | Initializes the thread system with specified queue capacity |
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
| `bool reserveQueueCapacity(size_t capacity)` | `capacity`: New capacity to reserve | `bool` | Reserves memory for the specified number of tasks |

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
// Initialize with default settings
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize thread system!" << std::endl;
    return -1;
}

// Use the thread system...

// Clean up when done
Forge::ThreadSystem::Instance().clean();
```

### Task Submission

```cpp
// Fire-and-forget task
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // Perform work that doesn't return a value
    processData();
});

// Task with result
auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
    // Perform work and return a value
    return calculateResult();
});

// Wait for and use the result
int result = future.get();
```

### Queue Capacity Management

```cpp
// Initialize with custom capacity
Forge::ThreadSystem::Instance().init(1000);

// Adjust capacity at runtime
Forge::ThreadSystem::Instance().reserveQueueCapacity(2000);

// Monitor usage
size_t currentSize = Forge::ThreadSystem::Instance().getQueueSize();
size_t capacity = Forge::ThreadSystem::Instance().getQueueCapacity();
```

## Thread Safety

All public methods of the ThreadSystem, ThreadPool, and TaskQueue classes are thread-safe and can be called concurrently from multiple threads.

## Error Handling

- The `init()` method returns false if initialization fails
- The `enqueueTaskWithResult()` method throws `std::runtime_error` if called after shutdown
- Task exceptions are propagated to the caller via `std::future::get()`

## Performance Considerations

- Use `reserveQueueCapacity()` before submitting large batches of tasks
- The default capacity (512) is suitable for most applications
- Avoid creating many small tasks; batch related work when possible
- Set thread pool size appropriately for your hardware (default is cores-1)