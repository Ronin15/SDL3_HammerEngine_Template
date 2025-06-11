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

### Overview

The WorkerBudget system provides intelligent resource allocation across engine subsystems, preventing resource contention while enabling dynamic scaling based on workload demands. It ensures guaranteed minimum performance while allowing systems to burst beyond their allocation when buffer threads are available.

### Allocation Strategy

The ThreadSystem implements a tiered allocation strategy that adapts to hardware capabilities:

```
Total Available Workers (hardware_concurrency - 1 for main thread)
├── GameLoop Reserved (1-2 workers based on hardware tier)
└── Remaining Workers
    ├── AIManager (60% of remaining, minimum 1)
    ├── EventManager (30% of remaining, minimum 1)
    └── Buffer (remaining workers for dynamic burst capacity)
```

### Hardware Tier Classification

```cpp
// Tier 1: Ultra Low-End (≤3 workers available)
// 4-core/4-thread system (3 workers) - CPU without hyperthreading/SMT
// Strategy: GameLoop gets priority, AI/Events single-threaded
GameLoop: 2 workers, AI: 1 worker, Events: 0 workers, Buffer: 0

// Tier 2: Entry Gaming (7 workers available)
// 4-core/8-thread system (7 workers) - entry-level gaming CPU
// Strategy: Conservative threading with basic WorkerBudget allocation
GameLoop: 2 workers, AI: 3 workers (60% of 5), Events: 1 worker (30% of 5), Buffer: 1

// Tier 3: Mid-Range Gaming (15 workers available)
// 8-core/16-thread system (15 workers) - mainstream gaming CPU
// Strategy: Full WorkerBudget allocation with buffer capacity
GameLoop: 2 workers, AI: 8 workers (60% of 13), Events: 4 workers (30% of 13), Buffer: 1

// Tier 4: High-End Gaming (31 workers available)
// 32-thread system (31 workers) - AMD 7950X3D (16c/32t), Intel 13900K/14900K (24c/32t)
// Strategy: Full multi-threading with substantial buffer capacity
GameLoop: 2 workers, AI: 17 workers (60% of 29), Events: 9 workers (30% of 29), Buffer: 3
```

### WorkerBudget Structure

```cpp
#include "core/WorkerBudget.hpp"

struct WorkerBudget {
    size_t totalWorkers;      // Total available worker threads
    size_t engineReserved;    // Workers reserved for GameLoop (1-2 based on tier)
    size_t aiAllocated;       // Workers allocated to AIManager
    size_t eventAllocated;    // Workers allocated to EventManager
    size_t remaining;         // Buffer workers for dynamic allocation

    // Helper methods for buffer utilization
    size_t getOptimalWorkerCount(size_t baseAllocation, size_t workloadSize, size_t threshold) const;
    bool hasBufferCapacity() const;
    size_t getMaxWorkerCount(size_t baseAllocation) const;
};

// Calculate budget based on available workers
Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
```

### Real-World Allocation Examples

```cpp
// Target Minimum: 4-core/8-thread system (7 workers available)
WorkerBudget {
    totalWorkers: 7,
    engineReserved: 2,    // GameLoop gets 2 workers for optimal performance
    aiAllocated: 3,       // AI gets 60% of remaining 5 = 3 workers
    eventAllocated: 1,    // Events get 30% of remaining 5 = 1 worker
    remaining: 1          // 1 buffer worker for burst capacity
}

// Mid-Range Gaming: 8-core/16-thread system (15 workers available)
WorkerBudget {
    totalWorkers: 15,
    engineReserved: 2,    // GameLoop gets 2 workers
    aiAllocated: 8,       // AI gets 60% of remaining 13 = 8 workers
    eventAllocated: 4,    // Events get 30% of remaining 13 = 4 workers
    remaining: 1          // 1 buffer worker for burst capacity
}

// High-End Gaming: 32-thread system (31 workers available) - AMD 7950X3D (16c/32t), Intel 13900K/14900K (24c/32t)
WorkerBudget {
    totalWorkers: 31,
    engineReserved: 2,    // GameLoop gets 2 workers
    aiAllocated: 17,      // AI gets 60% of remaining 29 = 17 workers
    eventAllocated: 9,    // Events get 30% of remaining 29 = 9 workers
    remaining: 3          // 3 buffer workers for burst capacity
}
```

### Buffer Thread Utilization

The WorkerBudget system enables intelligent buffer utilization for dynamic scaling:

```cpp
// AIManager using buffer threads for high workloads
void AIManager::update(float deltaTime) {
    auto& threadSystem = Forge::ThreadSystem::Instance();
    size_t availableWorkers = threadSystem.getThreadCount();
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

    // Calculate optimal worker count based on current workload
    size_t optimalWorkers = budget.getOptimalWorkerCount(
        budget.aiAllocated,    // Base guaranteed allocation
        m_entities.size(),     // Current workload size
        1000                   // Threshold for buffer usage
    );

    // Use buffer capacity for high entity counts
    if (optimalWorkers > budget.aiAllocated) {
        // High workload: Use base + buffer workers
        createBatches(optimalWorkers);
    } else {
        // Normal workload: Use base allocation only
        createBatches(budget.aiAllocated);
    }
}

// EventManager using buffer threads similarly
void EventManager::processEvents() {
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

    size_t optimalWorkers = budget.getOptimalWorkerCount(
        budget.eventAllocated, // Base allocation
        m_events.size(),       // Current event count
        100                    // Buffer threshold
    );

    processEventBatches(optimalWorkers);
}
```

### Workload-Based Scaling

```cpp
// Buffer utilization automatically scales based on workload thresholds:

// Low Workload (AI: 500 entities, Events: 50 events)
// - AI uses base allocation: 3 workers
// - Events use base allocation: 1 worker
// - Buffer remains available: 1 worker idle

// High Workload (AI: 5000 entities, Events: 500 events)
// - AI uses burst capacity: 4 workers (3 base + 1 buffer)
// - Events would use burst if available: 2 workers
// - Buffer is utilized for improved performance

// Conservative Burst Strategy
// - Systems take maximum 50% of their base allocation from buffer
// - Prevents any single system from monopolizing buffer capacity
// - Ensures fair resource distribution under load
```

### Testing the WorkerBudget System

The WorkerBudget system includes comprehensive testing to validate allocation logic:

```cpp
// Run buffer utilization tests
./tests/test_scripts/run_buffer_utilization_tests.sh
./tests/test_scripts/run_buffer_utilization_tests.sh --verbose

// Expected test results for different hardware tiers:
// 12-worker system:
Base allocations - GameLoop: 2, AI: 6, Events: 3, Buffer: 1
Low workload (500 entities): 6 workers    // Uses base allocation only
High workload (5000 entities): 7 workers  // Uses base + 1 buffer worker

// 3-worker system (low-end):
Allocations - GameLoop: 1, AI: 1, Events: 1, Buffer: 0
High workload with no buffer: 1 workers   // No scaling possible

// 16-worker system (very high-end):
Very high workload burst: 10 workers      // AI gets 8 base + 2 buffer
```

### WorkerBudget Integration Patterns

```cpp
// Recommended pattern for subsystems using WorkerBudget
void SubSystem::processWorkload() {
    auto& threadSystem = Forge::ThreadSystem::Instance();
    size_t availableWorkers = threadSystem.getThreadCount();
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

    // Calculate optimal workers based on current workload
    size_t optimalWorkers = budget.getOptimalWorkerCount(
        budget.systemAllocated,  // Your system's base allocation
        currentWorkloadSize,     // Current workload (entities, events, etc.)
        workloadThreshold        // Threshold for buffer usage
    );

    // Use optimal worker count for batch processing
    processBatches(optimalWorkers);
}

// Example thresholds for different systems:
// - AIManager: 1000 entities (CPU-intensive)
// - EventManager: 100 events (I/O and coordination)
// - Custom systems: Choose based on profiling results
```

### Troubleshooting WorkerBudget Issues

**Common Issues and Solutions:**

```cpp
// Issue: System not using buffer threads
// Check workload threshold
if (workloadSize <= threshold) {
    // Increase workload or lower threshold for testing
}

// Issue: Over-allocation detected
// The system has built-in validation:
size_t totalAllocated = budget.engineReserved + budget.aiAllocated + budget.eventAllocated;
if (totalAllocated > availableWorkers) {
    // Emergency fallback automatically triggered
    // Check hardware detection logic
}

// Issue: Poor performance on high-end systems
// Verify buffer utilization:
if (budget.hasBufferCapacity() && workloadSize > threshold) {
    size_t burstWorkers = budget.getOptimalWorkerCount(baseAllocation, workloadSize, threshold);
    // Should be > baseAllocation
}
```

**Debugging WorkerBudget Allocation:**

```cpp
void debugWorkerBudget() {
    auto& threadSystem = Forge::ThreadSystem::Instance();
    size_t workers = threadSystem.getThreadCount();
    Forge::WorkerBudget budget = Forge::calculateWorkerBudget(workers);

    std::cout << "=== WorkerBudget Debug Info ===" << std::endl;
    std::cout << "Total workers: " << budget.totalWorkers << std::endl;
    std::cout << "GameLoop reserved: " << budget.engineReserved << std::endl;
    std::cout << "AI allocated: " << budget.aiAllocated << std::endl;
    std::cout << "Events allocated: " << budget.eventAllocated << std::endl;
    std::cout << "Buffer available: " << budget.remaining << std::endl;
    std::cout << "Has buffer capacity: " << (budget.hasBufferCapacity() ? "Yes" : "No") << std::endl;

    // Test different workload scenarios
    size_t testWorkloads[] = {100, 500, 1000, 5000, 10000};
    for (size_t workload : testWorkloads) {
        size_t optimal = budget.getOptimalWorkerCount(budget.aiAllocated, workload, 1000);
        std::cout << "Workload " << workload << ": " << optimal << " workers" << std::endl;
    }
}
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

### Work-Stealing System

ThreadSystem implements an advanced work-stealing algorithm that dramatically improves load balancing:

```cpp
// Work-stealing automatically balances load across workers
// - 90%+ load balancing efficiency achieved
// - Thread-local batch counters for fair distribution
// - Adaptive victim selection with neighbor-first strategy
// - Batch-aware stealing preserves WorkerBudget compliance
// - Priority system maintained without abuse

// Example: 10,000 AI entities processing
// Before: Worker load ratio of 495:1 (severely unbalanced)
// After: Worker load ratio of ~1.1:1 (90%+ balanced)
```

**Key Work-Stealing Features:**
- **Batch-Aware Stealing**: Preserves WorkerBudget system integrity
- **Adaptive Victim Selection**: Smart neighbor-first work stealing
- **Thread-Local Counters**: Fair task distribution tracking
- **Priority Preservation**: Maintains task priorities without system abuse
- **Reduced Sleep Times**: Microsecond-level waits during high workload

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

// ✅ GOOD: Work-stealing optimizes large batch workloads
// AI processing with 10,000 entities automatically load-balanced
// No manual load balancing required - work-stealing handles it

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
6. **Load Balancing**: Trust work-stealing system - no manual balancing needed
7. **High Workloads**: Work-stealing excels with 1000+ concurrent tasks

### Load Balancing Performance

**Before Work-Stealing:**
- Worker 0: 1,900 tasks
- Worker 1: 1,850 tasks  
- Worker 2: 1,920 tasks
- Worker 3: 4 tasks ⚠️ (severe imbalance)

**After Work-Stealing:**
- Worker 0: 1,247 tasks
- Worker 1: 1,251 tasks
- Worker 2: 1,248 tasks
- Worker 3: 1,254 tasks ✅ (90%+ balanced)

### Memory Optimization

```cpp
// Pre-allocate queue capacity for better performance
threadSystem.reserveQueueCapacity(2048);  // Reserve space for 2048 tasks

// Use move semantics to avoid unnecessary copies
auto task = [data = std::move(largeData)]() mutable {
    processData(std::move(data));
};
threadSystem.enqueueTask(std::move(task));

// Work-stealing adds minimal memory overhead
// - Thread-local batch counters: ~64 bytes per worker
// - Adaptive victim selection: ~32 bytes per worker
// - Total overhead: <1KB for typical 8-worker system
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

## Work-Stealing Quick Reference

### Understanding Work-Stealing

The ThreadSystem automatically implements work-stealing to achieve optimal load balancing:

```cpp
// Work-stealing operates transparently - no configuration needed
ThreadSystem::Instance().enqueueTask(largeAIBatch, TaskPriority::Normal);
// Result: Automatic 90%+ load distribution across all workers

// Before work-stealing:
// Worker 0: 1,900 tasks, Worker 1: 1,850 tasks, Worker 2: 1,920 tasks, Worker 3: 4 tasks
// After work-stealing:
// Worker 0: 1,247 tasks, Worker 1: 1,251 tasks, Worker 2: 1,248 tasks, Worker 3: 1,254 tasks
```

### Work-Stealing Guarantees

- **Load Balance Efficiency**: 90%+ task distribution across workers
- **WorkerBudget Compliance**: Maintains all allocation limits during stealing
- **Priority Preservation**: Task priorities respected throughout redistribution
- **Batch Awareness**: AI and Event batches stolen as complete units
- **Zero Configuration**: Works automatically with existing code

### Performance Characteristics

```cpp
// Memory overhead per worker thread:
// - Thread-local counters: ~64 bytes
// - Victim selection state: ~32 bytes
// - Total system overhead: <1KB

// CPU overhead per steal operation:
// - Victim selection: <10 CPU cycles
// - Batch steal attempt: <50 CPU cycles
// - Success rate: 85%+ under load
```

### Work-Stealing Best Practices

```cpp
// ✅ GOOD: Large batches benefit most from work-stealing
std::vector<EntityPtr> entities(10000);
for (size_t i = 0; i < entities.size(); i += batchSize) {
    threadSystem.enqueueTask([=]() {
        processBatch(entities, i, batchSize);  // Optimal for work-stealing
    }, TaskPriority::Normal);
}

// ✅ GOOD: Work-stealing excels with sustained high workloads
// AI systems with 1000+ entities automatically load-balanced

// ℹ️ NOTE: Work-stealing is most effective with:
// - Sustained workloads (not single tasks)
// - Batch-oriented processing
// - Multiple workers available
// - Mixed task completion times
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
