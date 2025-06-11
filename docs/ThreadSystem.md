# ThreadSystem Documentation

## Overview

The ThreadSystem is a high-performance thread pool implementation that provides task-based concurrency for the Forge Game Engine. It enables multi-core performance benefits while maintaining a simplified programming model, with automatic worker allocation, priority-based scheduling, and seamless integration with engine components.

## Key Features

- **Automatic Thread Pool**: Sizing based on available CPU cores with worker budget allocation
- **Priority-Based Scheduling**: Critical, High, Normal, Low, and Idle task priorities
- **Task-Based Programming**: Simple enqueue interface for fire-and-forget and result-returning tasks
- **Thread-Safe Operations**: Proper synchronization with shared_mutex and atomic operations
- **Engine Integration**: Used by EventManager, AIManager, and other core systems
- **Performance Monitoring**: Built-in statistics and queue management
- **Clean Shutdown**: Proper resource management and graceful worker termination

## Quick Start

### Basic Initialization

```cpp
#include "core/ThreadSystem.hpp"

// Initialize with default settings
if (!Forge::ThreadSystem::Instance().init()) {
    std::cerr << "Failed to initialize ThreadSystem!" << std::endl;
    return -1;
}

// Initialize with custom parameters
if (!Forge::ThreadSystem::Instance().init(2048, 4, true)) {  // Queue capacity, thread count, profiling
    std::cerr << "Failed to initialize ThreadSystem!" << std::endl;
    return -1;
}

// Check initialization status
unsigned int threadCount = Forge::ThreadSystem::Instance().getThreadCount();
std::cout << "ThreadSystem initialized with " << threadCount << " threads" << std::endl;
```

### Basic Task Submission

```cpp
// Fire-and-forget task with default priority
Forge::ThreadSystem::Instance().enqueueTask([]() {
    std::cout << "Executing task on thread pool" << std::endl;
});

// Task with high priority
Forge::ThreadSystem::Instance().enqueueTask([]() {
    // Critical game logic here
}, Forge::TaskPriority::High, "Critical Game Update");

// Task with return value
auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([](int value) -> int {
    return value * 2;
}, Forge::TaskPriority::Normal, "Math Calculation", 42);

int result = future.get();  // Will be 84
```

## Task Priority System

### Priority Levels

```cpp
enum class TaskPriority : int {
    Critical = 0,  // Mission-critical operations (engine core, critical AI)
    High = 1,      // Important game operations (combat AI, player interactions)
    Normal = 2,    // Standard game logic (background AI, standard processing)
    Low = 3,       // Background operations (resource loading, non-critical updates)
    Idle = 4       // Low-priority cleanup and maintenance tasks
};
```

### Priority Usage Examples

```cpp
// Critical: Engine core operations
threadSystem.enqueueTask([]() {
    updateCriticalGameState();
}, Forge::TaskPriority::Critical, "Game State Update");

// High: Important AI or player interactions
threadSystem.enqueueTask([]() {
    updatePlayerCombat();
}, Forge::TaskPriority::High, "Player Combat");

// Normal: Standard background processing
threadSystem.enqueueTask([]() {
    updateBackgroundNPCs();
}, Forge::TaskPriority::Normal, "Background AI");

// Low: Resource management
threadSystem.enqueueTask([]() {
    cleanupUnusedTextures();
}, Forge::TaskPriority::Low, "Texture Cleanup");
```

## Worker Budget System

### Allocation Strategy

The ThreadSystem implements a sophisticated worker budget allocation to prevent resource contention:

```
Total Available Workers (hardware_concurrency - 1)
├── GameEngine Reserved (1-2 workers for critical operations)
└── Remaining Workers
    ├── AIManager (60% of remaining)
    ├── EventManager (30% of remaining)
    └── Buffer (10% of remaining for system responsiveness)
```

### Budget Calculation

```cpp
// Automatic budget calculation based on hardware
struct WorkerBudget {
    size_t totalWorkers;      // Total available workers
    size_t engineReserved;    // Reserved for critical GameEngine operations
    size_t aiAllocated;       // Allocated for AIManager
    size_t eventAllocated;    // Allocated for EventManager
    size_t bufferReserved;    // Reserved for system responsiveness
};

// Example allocations by core count:
// 4 cores:  1 engine, 1 AI, 0 event, 1 buffer
// 8 cores:  2 engine, 3 AI, 1 event, 1 buffer  
// 12 cores: 2 engine, 5 AI, 3 event, 1 buffer
// 24 cores: 2 engine, 13 AI, 6 event, 2 buffer
```

### Queue Pressure Management

```cpp
// System monitors queue pressure to prevent overload
size_t queueSize = getQueueSize();
size_t workerCount = getThreadCount();

if (queueSize > workerCount * 3) {
    // High pressure - fall back to single-threaded processing
    processTasksSingleThreaded();
} else {
    // Normal load - use full parallel processing
    processTasksParallel();
}
```

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
bool init(size_t queueCapacity = 1024, unsigned int customThreadCount = 0, bool enableProfiling = false);
void clean();
static bool Exists();

// Task submission
void enqueueTask(std::function<void()> task, TaskPriority priority = TaskPriority::Normal, const std::string& description = "");

template<class F, class... Args>
auto enqueueTaskWithResult(F&& f, TaskPriority priority = TaskPriority::Normal, const std::string& description = "", Args&&... args) 
    -> std::future<typename std::invoke_result<F, Args...>::type>;

// Status and information
unsigned int getThreadCount() const;
bool isBusy() const;
bool isShutdown() const;
size_t getQueueSize() const;
size_t getCompletedTaskCount() const;

// Configuration
void setDebugLogging(bool enable);
void reserveQueueCapacity(size_t capacity);
```

### Task Management

```cpp
// Queue management
void reserveQueueCapacity(size_t capacity);  // Pre-allocate queue memory
size_t getQueueSize() const;                 // Current queue size
bool isBusy() const;                         // Check if tasks are pending

// Performance monitoring
size_t getCompletedTaskCount() const;        // Total completed tasks
void setDebugLogging(bool enable);           // Enable detailed logging
```

## Engine Integration

### AIManager Integration

```cpp
// AIManager uses ThreadSystem for entity batch processing
class AIManager {
    void updateEntitiesParallel(const std::vector<EntityData>& entities) {
        size_t batchSize = calculateOptimalBatchSize(entities.size());
        
        for (size_t i = 0; i < entities.size(); i += batchSize) {
            size_t end = std::min(i + batchSize, entities.size());
            
            Forge::ThreadSystem::Instance().enqueueTask([this, i, end, &entities]() {
                processBatch(entities, i, end);
            }, Forge::TaskPriority::Normal, "AI Batch Processing");
        }
    }
};
```

### EventManager Integration

```cpp
// EventManager uses ThreadSystem for event processing
class EventManager {
    void processEventsParallel() {
        if (m_eventQueue.size() > m_threadingThreshold) {
            // Batch process events using ThreadSystem
            Forge::ThreadSystem::Instance().enqueueTask([this]() {
                processBatchedEvents();
            }, Forge::TaskPriority::High, "Event Processing");
        } else {
            // Process on main thread for small batches
            processEventsSequential();
        }
    }
};
```

## Performance Optimization

### Best Practices

```cpp
// ✅ GOOD: Group related tasks into batches
std::vector<EntityPtr> entities = getEntitiesNeedingUpdate();
size_t batchSize = entities.size() / threadCount;

for (size_t i = 0; i < entities.size(); i += batchSize) {
    threadSystem.enqueueTask([entities, i, batchSize]() {
        processBatch(entities, i, batchSize);
    }, Forge::TaskPriority::Normal, "Entity Batch");
}

// ✅ GOOD: Use appropriate priorities
threadSystem.enqueueTask(criticalTask, Forge::TaskPriority::Critical);
threadSystem.enqueueTask(backgroundTask, Forge::TaskPriority::Low);

// ❌ BAD: Don't create excessive small tasks
for (auto& entity : entities) {  // Creates thousands of tiny tasks
    threadSystem.enqueueTask([&entity]() {
        entity.update();
    });
}
```

### Performance Guidelines

1. **Batch Size**: Create batches of 25-1000 items for optimal cache performance
2. **Priority Usage**: Use Critical sparingly, Normal for most tasks, Low for cleanup
3. **Queue Management**: Reserve queue capacity for known workloads
4. **Memory Access**: Design tasks to minimize shared memory access
5. **Task Granularity**: Balance between parallelism and overhead

### Memory Optimization

```cpp
// Pre-allocate queue capacity for better performance
threadSystem.reserveQueueCapacity(2048);  // Reserve space for 2048 tasks

// Use move semantics to avoid unnecessary copies
auto task = [data = std::move(largeData)]() mutable {
    processData(std::move(data));
};
threadSystem.enqueueTask(std::move(task));
```

## Thread Safety

### Safe Patterns

```cpp
// ✅ SAFE: Capture by value or move
int value = 42;
threadSystem.enqueueTask([value]() {  // Copy capture is safe
    processValue(value);
});

// ✅ SAFE: Shared pointer for shared data
auto sharedData = std::make_shared<GameData>();
threadSystem.enqueueTask([sharedData]() {
    processGameData(*sharedData);
});

// ❌ UNSAFE: Raw pointer or reference capture
SomeObject obj;
threadSystem.enqueueTask([&obj]() {  // obj might be destroyed
    obj.process();  // Potential use-after-free
});
```

### Thread-Safe Operations

- **Task Submission**: All `enqueueTask` methods are thread-safe
- **Status Queries**: All getter methods are thread-safe
- **Configuration**: `setDebugLogging` and `reserveQueueCapacity` are thread-safe
- **Shutdown**: `clean()` safely waits for all tasks to complete

## Error Handling

### Exception Safety

```cpp
// ThreadSystem handles exceptions in tasks gracefully
threadSystem.enqueueTask([]() {
    try {
        riskyOperation();
    } catch (const std::exception& e) {
        // Log error but don't crash the thread pool
        std::cerr << "Task failed: " << e.what() << std::endl;
    }
});

// Future-based tasks propagate exceptions
auto future = threadSystem.enqueueTaskWithResult([]() -> int {
    throw std::runtime_error("Something went wrong");
    return 42;
});

try {
    int result = future.get();  // Will throw the exception
} catch (const std::exception& e) {
    std::cerr << "Task exception: " << e.what() << std::endl;
}
```

## Integration Examples

### Complete Game Loop Integration

```cpp
class GameEngine {
public:
    bool init() {
        // Initialize ThreadSystem first
        if (!Forge::ThreadSystem::Instance().init(2048, 0, true)) {
            return false;
        }
        
        // Initialize other systems that use ThreadSystem
        if (!EventManager::Instance().init()) return false;
        if (!AIManager::Instance().init()) return false;
        
        return true;
    }
    
    void update(float deltaTime) {
        // Submit background tasks
        Forge::ThreadSystem::Instance().enqueueTask([this, deltaTime]() {
            updateAI(deltaTime);
        }, Forge::TaskPriority::High, "AI Update");
        
        Forge::ThreadSystem::Instance().enqueueTask([this]() {
            processEvents();
        }, Forge::TaskPriority::Normal, "Event Processing");
        
        // Continue with main thread work
        updateMainSystems(deltaTime);
    }
    
    void cleanup() {
        // Clean up in reverse order
        AIManager::Instance().clean();
        EventManager::Instance().clean();
        Forge::ThreadSystem::Instance().clean();  // Clean up last
    }
};
```

## Troubleshooting

### Common Issues

**Tasks not executing:**
- Verify ThreadSystem is initialized with `init()`
- Check that the system isn't shut down with `isShutdown()`
- Ensure proper exception handling in tasks

**Performance issues:**
- Monitor queue size with `getQueueSize()`
- Use appropriate task priorities
- Batch small operations together
- Avoid excessive task creation

**Memory issues:**
- Use `reserveQueueCapacity()` for known workloads
- Avoid capturing large objects by reference
- Use move semantics for large data

### Debug Information

```cpp
// Monitor ThreadSystem performance
void debugThreadSystem() {
    auto& ts = Forge::ThreadSystem::Instance();
    
    std::cout << "Thread count: " << ts.getThreadCount() << std::endl;
    std::cout << "Queue size: " << ts.getQueueSize() << std::endl;
    std::cout << "Completed tasks: " << ts.getCompletedTaskCount() << std::endl;
    std::cout << "Is busy: " << (ts.isBusy() ? "Yes" : "No") << std::endl;
}

// Enable detailed logging
Forge::ThreadSystem::Instance().setDebugLogging(true);
```

## Conclusion

The ThreadSystem provides a robust foundation for multi-threaded game development while maintaining simplicity and safety. Its integration with the worker budget system ensures optimal resource allocation across all engine components, while the priority system allows for fine-grained control over task execution order.

The system scales from 2 to 32+ cores automatically and provides the performance foundation for AI processing, event handling, and other parallel operations throughout the engine.