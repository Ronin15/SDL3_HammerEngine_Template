# ThreadSystem Optimization Guide

## Current System Limitations

The ThreadSystem in our engine has been observed to handle up to about 4,000 tasks before encountering performance degradation or failure. This document outlines the likely bottlenecks and suggests optimizations to increase the system's capacity, reliability, and performance.

## Root Causes of Current Limitations

### 1. Single Queue Bottleneck

The current implementation uses a single task queue protected by a single mutex:

```cpp
std::queue<std::function<void()>> tasks;
std::mutex queueMutex;
```

This creates contention when:
- Multiple worker threads attempt to dequeue tasks simultaneously
- Tasks are being enqueued while workers are trying to dequeue
- The main thread is rapidly enqueuing multiple tasks

As task count increases, lock contention becomes exponentially worse, leading to performance degradation.

### 2. Dynamic Memory Allocation

Every `std::function` object may allocate memory on the heap, especially when:
- Capturing variables in lambda expressions
- Using complex function objects
- Wrapping member functions

With thousands of tasks, these allocations can cause:
- Memory fragmentation
- Allocation failures
- Poor cache locality

### 3. Resource Exhaustion

The system lacks mechanisms to:
- Limit maximum pending tasks
- Control memory usage
- Provide backpressure when overloaded

### 4. Shutdown Behavior

The current shutdown process waits for all tasks to complete, which can cause delays when:
- Many tasks are queued
- Long-running tasks are still processing
- System is under heavy load

## Optimization Strategies

### 1. Work-Stealing Queue Design

Replace the single-queue design with a work-stealing approach:

```cpp
class ThreadPool {
private:
    struct Worker {
        std::deque<std::function<void()>> localQueue;
        std::mutex queueMutex;
        std::thread thread;
    };
    
    std::vector<Worker> workers;
    // ...
};
```

Benefits:
- Reduces lock contention
- Improves locality of tasks
- Balances work automatically
- Scales better with thread count

Implementation:
1. Give each worker thread its own task queue
2. When a worker's queue is empty, it "steals" from other queues
3. When enqueuing, distribute tasks or use thread affinity hints

### 2. Memory Optimization

Reduce allocation overhead with these techniques:

```cpp
// Pre-allocate space
std::vector<std::function<void()>> tasks;
tasks.reserve(initialCapacity);

// Use small function optimization
using TaskFunc = std::function<void()>;
boost::container::small_vector<TaskFunc, 128> taskBatch;
```

Additional approaches:
- Custom allocator for task functions
- Task object pooling
- Avoid capturing large objects in lambdas
- Use shared_ptr for large shared state instead of copying

### 3. Bounded Queue Implementation

Add capacity limits to prevent resource exhaustion:

```cpp
class BoundedTaskQueue {
private:
    size_t maxQueueSize;
    // ...
    
public:
    bool push(std::function<void()> task, bool blockIfFull = false) {
        std::unique_lock<std::mutex> lock(queueMutex);
        if (tasks.size() >= maxQueueSize) {
            if (blockIfFull) {
                queueNotFullCondition.wait(lock, [this] { 
                    return tasks.size() < maxQueueSize || stopping; 
                });
            } else {
                return false; // Queue full, task rejected
            }
        }
        // ...
    }
};
```

Benefits:
- Prevents unbounded memory growth
- Provides backpressure
- Allows client code to adapt to system capacity

### 4. Task Batching

Add support for submitting multiple tasks at once:

```cpp
void enqueueBatch(std::vector<std::function<void()>> taskBatch) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        for (auto& task : taskBatch) {
            tasks.push(std::move(task));
        }
    }
    condition.notify_all(); // Wake up all threads at once
}
```

Benefits:
- Reduces lock acquisitions
- More efficient worker thread wakeup
- Lower per-task overhead

### 5. Priority Support

Add task prioritization:

```cpp
enum class TaskPriority { High, Normal, Low };

void enqueueWithPriority(std::function<void()> task, TaskPriority priority) {
    // ...
}
```

Implementation options:
- Multiple priority queues
- Priority queue data structure
- Priority-based work stealing

### 6. Improved Diagnostics

Add monitoring capabilities:

```cpp
struct ThreadPoolStats {
    size_t pendingTasks;
    size_t completedTasks;
    size_t rejectedTasks;
    size_t maxQueueLength;
    double avgProcessingTimeMs;
    // ...
};

ThreadPoolStats getStats() const;
```

This helps identify:
- Bottlenecks
- Resource limitations
- Thread imbalances
- Performance issues

### 7. Graceful Overload Handling

Implement strategies for handling overload conditions:

- Task shedding (reject least important tasks)
- Dynamic thread count adjustment
- Execution throttling
- Cooperative task cancellation

## Implementation Plan

### Phase 1: Core Architecture Improvements

1. Implement work-stealing queue
2. Add queue size limits
3. Optimize memory usage
4. Add basic diagnostics

### Phase 2: Advanced Features

1. Implement task prioritization
2. Add support for task cancellation
3. Implement task batching
4. Enhance diagnostics and monitoring

### Phase 3: Fine-tuning

1. Benchmark with different workloads
2. Optimize for specific use cases
3. Add adaptive tuning capabilities
4. Add stress testing and verification tools

## Performance Goals

After implementing these optimizations, the system should be able to:

- Handle at least 20,000 concurrent tasks reliably
- Maintain consistent performance under heavy load
- Scale effectively with available CPU cores
- Provide predictable behavior when overloaded
- Gracefully handle memory pressure

## Example: Work-Stealing Implementation

```cpp
class WorkStealingThreadPool {
public:
    WorkStealingThreadPool(size_t numThreads)
        : workers(numThreads), isRunning(true), nextWorkerId(0) {
        
        // Create and start worker threads
        for (size_t i = 0; i < numThreads; ++i) {
            workers[i].thread = std::thread(&WorkStealingThreadPool::workerFunction, 
                                           this, i);
        }
    }
    
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> result = task->get_future();
        
        // Distribute tasks round-robin to worker queues
        size_t workerId = (nextWorkerId++) % workers.size();
        
        {
            std::lock_guard<std::mutex> lock(workers[workerId].mutex);
            workers[workerId].tasks.emplace_back([task]() { (*task)(); });
        }
        
        workers[workerId].cv.notify_one();
        return result;
    }
    
    // ... (destructor, cleanup methods, etc.)
    
private:
    struct Worker {
        std::thread thread;
        std::deque<std::function<void()>> tasks;
        std::mutex mutex;
        std::condition_variable cv;
    };
    
    std::vector<Worker> workers;
    std::atomic<bool> isRunning;
    std::atomic<size_t> nextWorkerId;
    
    void workerFunction(size_t id) {
        while (isRunning) {
            std::function<void()> task;
            bool taskFound = false;
            
            // Try to get task from own queue first
            {
                std::unique_lock<std::mutex> lock(workers[id].mutex);
                if (!workers[id].tasks.empty()) {
                    task = std::move(workers[id].tasks.front());
                    workers[id].tasks.pop_front();
                    taskFound = true;
                }
            }
            
            // If no task in own queue, try to steal from others
            if (!taskFound) {
                for (size_t i = 0; i < workers.size(); ++i) {
                    if (i == id) continue; // Skip own queue
                    
                    std::unique_lock<std::mutex> lock(workers[i].mutex);
                    if (!workers[i].tasks.empty()) {
                        // Steal from the back (different from own queue)
                        task = std::move(workers[i].tasks.back());
                        workers[i].tasks.pop_back();
                        taskFound = true;
                        break;
                    }
                }
            }
            
            // If we found a task, execute it
            if (taskFound) {
                task();
            } else {
                // No tasks found, wait on own condition variable
                std::unique_lock<std::mutex> lock(workers[id].mutex);
                workers[id].cv.wait_for(lock, std::chrono::milliseconds(100),
                    [this, id]() { return !workers[id].tasks.empty() || !isRunning; });
            }
        }
    }
};
```

## Testing Strategy

1. **Progressive Load Testing**: Gradually increase task count until failure
2. **Memory Pressure Testing**: Use large captured objects to test memory behavior
3. **Exception Handling**: Ensure exceptions don't crash the system
4. **Shutdown Testing**: Verify clean shutdown under load
5. **Long-Running Task Testing**: Test with tasks of varied duration

## Conclusion

With these optimizations, the ThreadSystem should be able to handle a significantly higher number of tasks while maintaining better performance characteristics. The work-stealing design in particular should allow for near-linear scaling with the number of cores available in the system.