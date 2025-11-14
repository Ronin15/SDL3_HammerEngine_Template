# ThreadSystem Documentation

**Where to find the code:**
- Header-only implementation: `include/core/ThreadSystem.hpp`

**Singleton Access:** Use `HammerEngine::ThreadSystem::Instance()` to access the thread system.

## Overview

The Hammer Engine ThreadSystem is a robust, production-ready thread pool implementation designed for high-performance game development. It provides efficient task-based concurrency with priority-based scheduling, WorkerBudget resource allocation, and comprehensive performance monitoring. The design emphasizes reliability, maintainability, and consistent performance across diverse hardware configurations.

## Architecture Overview

### Core Components Hierarchy

```
ThreadSystem (Singleton)
├── ThreadPool (Worker thread management)
│   ├── TaskQueue (Global priority-based queue)
│   │   ├── Priority Queues [0-4] (Critical → Idle)
│   │   ├── Per-Priority Mutexes (Reduced contention)
│   │   ├── Statistics Tracking
│   │   └── Profiling System
│   └── Worker Threads [N]
│       ├── Priority-Based Task Processing
│       ├── Exponential Backoff
│       └── Exception Handling
└── WorkerBudget System (Intelligent resource allocation)
    ├── AI: ~45% of remaining workers
    ├── Particles: ~25% of remaining workers
    ├── Events: ~20% of remaining workers
    ├── Engine: 1–2 workers reserved
    └── Buffer: Dynamic burst capacity
```

### Class Responsibilities

| Class | Responsibility | Key Features |
|-------|---------------|--------------|
| **ThreadSystem** | Singleton API manager | Initialization, cleanup, public interface |
| **ThreadPool** | Worker thread lifecycle | Thread creation, task distribution, graceful shutdown |
| **TaskQueue** | Priority-based queuing | 5 priority levels, per-priority mutexes, capacity management |
| **WorkerBudget** | Resource allocation | Hardware-adaptive allocation with dynamic buffer usage |
| **PrioritizedTask** | Task wrapper | Priority, timing, description, FIFO within priority |

### Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| **Memory Overhead** | <0.5KB | Minimal static system overhead |
| **CPU Overhead** | <0.05% | Per task processing cost |
| **Throughput** | 15,000-25,000 tasks/sec | Small tasks (100-1000 ops) |
| **Load Balance Efficiency** | 85%+ | Priority-based distribution |
| **Scalability** | 75-95% efficiency | 2-16+ cores, reliable scaling |

## Implementation Details

### Memory Management Strategy

**Queue Capacity Management:**
```cpp
// Dynamic growth strategy
if (queue.size() >= (m_desiredCapacity * 90) / 100) { // 90% threshold
    size_t newCapacity = queue.capacity() * 2;
    queue.reserve(newCapacity);
}
```

**Memory Efficiency Features:**
- **Pre-allocation**: `reserveQueueCapacity()` for known workloads
- **Exponential Growth**: Doubles capacity when 90% full
- **Move Semantics**: Extensive use of `std::move` to avoid copies
- **Minimal Overhead**: <1KB total system overhead

### Thread Safety Implementation

**Synchronization Primitives:**
- **Per-Priority Mutexes**: Reduces contention between priority levels
- **Atomic Counters**: Lock-free statistics tracking
- **Condition Variables**: Efficient thread wake-up
- **Memory Ordering**: Explicit memory ordering for performance

**Lock-Free Operations:**
```cpp
// Atomic task counting
m_totalTasksProcessed.fetch_add(1, std::memory_order_relaxed);

// Lock-free stopping check
bool isStopping() const {
    return stopping.load(std::memory_order_acquire);
}
```

## Key Features

```
┌─────────────────────────────────────────────────────────────┐
│                    ThreadSystem (Singleton)                 │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐    ┌─────────────────────────────────┐ │
│  │   ThreadPool    │    │        TaskQueue                │ │
│  │                 │    │  ┌─────────────────────────────┐│ │
│  │ ┌─────────────┐ │    │  │ Priority Queues (0-4)       ││ │
│  │ │ Worker 0    │ │    │  │ Critical │ High │ Normal    ││ │
│  │ │ Worker 1    │ │◄───┤  │ Low      │ Idle │           ││ │
│  │ │ Worker N    │ │    │  └─────────────────────────────┘│ │
│  │ └─────────────┘ │    └─────────────────────────────────┘ │
│  └─────────────────┘                                        │
│  ┌─────────────────────────────────────────────────────────┐│
│  │            WorkerBudget Allocation                      ││
│  │ Engine: 1–2 │ AI: 6 wt │ Collision: 3 wt │ Particles: 3 wt │ Events: 2 wt │ Buffer: 30% ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────
```

## Key Features

- **🔄 Automatic Thread Pool Management**: Optimal sizing based on CPU cores with intelligent worker allocation
- **⚡ Priority-Based Scheduling**: 5-level priority system with separate queues for minimal contention
- **🔀 Optimized Task Distribution**: Efficient batch processing with WorkerBudget-based load balancing
- **📊 Performance Monitoring**: Built-in profiling, statistics tracking, and performance analytics
- **🛡️ Thread Safety**: Lock-free operations where possible with comprehensive synchronization
- **🎯 Engine Integration**: Seamless integration with AIManager, EventManager, and core systems
- **⚙️ WorkerBudget System**: Weight-based resource allocation across engine subsystems (AI: 6, Collision: 3, Particles: 3, Events: 2 weights; 1–2 engine workers reserved; 30% buffer reserve for burst capacity)
- **🔧 Clean Shutdown**: Graceful termination with proper resource cleanup

## Quick Start

### Basic Initialization

```cpp
#include "core/ThreadSystem.hpp"
using namespace HammerEngine;

// Initialize with default settings (recommended)
if (!ThreadSystem::Instance().init()) {
    THREADSYSTEM_ERROR("Failed to initialize ThreadSystem!");
    return false;
}

// Initialize with custom parameters
bool success = ThreadSystem::Instance().init(
    4096,                    // Queue capacity
    8,                       // Custom thread count (0 = auto-detect)
    true                     // Enable profiling
);

// Verify initialization
if (success) {
    unsigned int threads = ThreadSystem::Instance().getThreadCount();
    THREADSYSTEM_INFO("ThreadSystem initialized with " + std::to_string(threads) + " threads");
}
```

### Basic Task Submission

```cpp
// Simple fire-and-forget task
ThreadSystem::Instance().enqueueTask([]() {
    // Your task code here
    processGameLogic();
}, TaskPriority::Normal, "Game Logic Update");

// Task with return value
auto future = ThreadSystem::Instance().enqueueTaskWithResult([](int value) -> int {
    return calculateComplexValue(value);
}, TaskPriority::High, "Complex Calculation", 42);

// Retrieve result (blocks until complete)
int result = future.get();
```

### Batch Processing Example

```cpp
// Process large collections efficiently
std::vector<Entity*> entities = getActiveEntities();
size_t batchSize = entities.size() / ThreadSystem::Instance().getThreadCount();

for (size_t i = 0; i < entities.size(); i += batchSize) {
    ThreadSystem::Instance().enqueueTask([=]() {
        size_t end = std::min(i + batchSize, entities.size());
        for (size_t j = i; j < end; ++j) {
            entities[j]->update();
        }
    }, TaskPriority::Normal, "Entity Batch " + std::to_string(i / batchSize));
}
```

## Task Priority System

### Priority Levels

```cpp
enum class TaskPriority : int {
    Critical = 0,   // Must execute ASAP (rendering, input handling)
    High = 1,       // Important tasks (physics, animation)
    Normal = 2,     // Default priority for most tasks
    Low = 3,        // Background tasks (asset loading)
    Idle = 4        // Only execute when nothing else is pending
};
```

### Priority Guidelines

| Priority | Use Case | Examples | Queue Behavior |
|----------|----------|----------|----------------|
| **Critical** | Mission-critical operations | Input handling, rendering pipeline | Immediate processing, notify all threads |
| **High** | Important game operations | Physics updates, combat AI | High priority processing, notify all threads |
| **Normal** | Standard game logic | Entity updates, standard AI | Default processing, single notification |
| **Low** | Background operations | Asset loading, cleanup | Lower priority, single notification |
| **Idle** | Maintenance tasks | Memory cleanup, cache optimization | Lowest priority, execute when idle |

### Priority Usage Examples

```cpp
// Critical: Frame-critical operations
ThreadSystem::Instance().enqueueTask([]() {
    renderer.updateRenderQueue();
}, TaskPriority::Critical, "Render Queue Update");

// High: Player-visible operations
ThreadSystem::Instance().enqueueTask([]() {
    player.updateCombatSystem();
}, TaskPriority::High, "Player Combat");

// Normal: Background game logic
ThreadSystem::Instance().enqueueTask([]() {
    updateNPCAI(npcList);
}, TaskPriority::Normal, "NPC AI Update");

// Low: Resource management
ThreadSystem::Instance().enqueueTask([]() {
    assetManager.loadBackgroundAssets();
}, TaskPriority::Low, "Background Asset Loading");

// Idle: Cleanup operations
ThreadSystem::Instance().enqueueTask([]() {
    memoryManager.defragmentMemory();
}, TaskPriority::Idle, "Memory Defragmentation");
```



## WorkerBudget System

### Overview

The WorkerBudget system provides intelligent resource allocation across engine subsystems, ensuring optimal performance distribution while maintaining system responsiveness.

### Allocation Strategy

The system uses a **weight-based allocation strategy** that adapts to hardware capabilities:

**Worker Weights** (determines priority and base allocation):
- AI_WORKER_WEIGHT = 6 (highest priority, most complex workload)
- COLLISION_WORKER_WEIGHT = 3 (spatial queries and detection)
- PARTICLE_WORKER_WEIGHT = 3 (physics updates)
- EVENT_WORKER_WEIGHT = 2 (event processing)

**Tier 1 (≤1 workers)**: Ultra low-end systems
- GameEngine: 1 worker (all available)
- AI: 0 workers (single-threaded fallback)
- Collision: 0 workers (single-threaded fallback)
- Particles: 0 workers (single-threaded fallback)
- Events: 0 workers (single-threaded fallback)

**Tier 2 (2-4 workers)**: Low-end systems
- GameEngine: 1 worker (minimum required)
- AI: 1 worker if available
- Collision: 1 worker if ≥2 remaining workers
- Particles: 1 worker if ≥3 remaining workers
- Events: 0 workers (remains single-threaded)

**Tier 3 (5+ workers)**: High-end systems
- GameEngine: 2 workers (optimal for coordination)
- **30% Buffer Reserve**: 30% of remaining workers reserved for burst capacity
- **Weight-Based Distribution**: Remaining 70% distributed by worker weights:
  - AI: (6/14) × 70% ≈ 30% of total remaining workers
  - Collision: (3/14) × 70% ≈ 15% of total remaining workers
  - Particles: (3/14) × 70% ≈ 15% of total remaining workers
  - Events: (2/14) × 70% ≈ 10% of total remaining workers
- Buffer: 30% reserved for dynamic burst allocation

```cpp
struct WorkerBudget {
    size_t totalWorkers;       // Total available worker threads
    size_t engineReserved;     // Reserved for critical engine operations (1-2)
    size_t aiAllocated;        // AI worker allocation (weight: 6)
    size_t collisionAllocated; // Collision worker allocation (weight: 3)
    size_t particleAllocated;  // Particle worker allocation (weight: 3)
    size_t eventAllocated;     // Event worker allocation (weight: 2)
    size_t remaining;          // Buffer workers for burst capacity (30% reserve)

    // Helper methods
    size_t getOptimalWorkerCount(size_t baseAllocation, size_t workloadSize, size_t workloadThreshold) const;
    bool hasBufferCapacity() const { return remaining > 0; }
    size_t getMaxWorkerCount(size_t baseAllocation) const { return baseAllocation + remaining; }
};
```

### Hardware Tier Classification

| Hardware Tier | CPU Cores/Threads | Workers | AI | Collision | Particles | Events | Engine | Buffer |
|---------------|-------------------|---------|-----|-----------|-----------|--------|--------|--------|
| **Ultra Low-End** | 1-2 cores/2-4 threads | 1-3 | 0-1 | 0 | 0 | 0-1 | 1 | 0 |
| **Low-End** | 2-4 cores/4-8 threads | 3-7 | 1-3 | 0-1 | 0-1 | 0-1 | 1-2 | 0-1 |
| **Mid-Range** | 4-6 cores/8-12 threads | 7-11 | 3-4 | 1-2 | 1-2 | 1 | 2 | 1-2 |
| **High-End** | 6-8 cores/12-16 threads | 11-15 | 4-6 | 2-3 | 2-3 | 1-2 | 2 | 2-3 |
| **Enthusiast** | 8+ cores/16+ threads | 15+ | 6-8 | 3-4 | 3-4 | 2-3 | 2 | 3+ |

### Real-World Allocation Examples

```cpp
// 4-core/8-thread system (7 workers available)
WorkerBudget budget = {
    .totalWorkers = 7,
    .engineReserved = 1,         // Low-end: 1 worker for engine
    .aiAllocated = 1,            // Conservative allocation
    .collisionAllocated = 1,     // Basic collision processing
    .particleAllocated = 1,      // Basic particle processing
    .eventAllocated = 0,         // Single-threaded fallback
    .remaining = 3               // ~43% buffer for burst capacity
};

// 8-core/16-thread system (15 workers available) - High-end
// Remaining: 13 workers, Buffer: 30% × 13 = 4, Allocate: 9 workers
// Total weight: 6+3+3+2 = 14
WorkerBudget budget = {
    .totalWorkers = 15,
    .engineReserved = 2,         // 13% - Enhanced engine capacity
    .aiAllocated = 4,            // (6/14) × 9 ≈ 4 workers (weight: 6)
    .collisionAllocated = 2,     // (3/14) × 9 ≈ 2 workers (weight: 3)
    .particleAllocated = 2,      // (3/14) × 9 ≈ 2 workers (weight: 3)
    .eventAllocated = 1,         // (2/14) × 9 ≈ 1 worker  (weight: 2)
    .remaining = 4               // 30% - Buffer for burst workloads
};

// 2-core/4-thread system (3 workers available) - Low-end
WorkerBudget budget = {
    .totalWorkers = 3,
    .engineReserved = 1,         // 33% - Critical engine operations
    .aiAllocated = 1,            // 33% - Minimal AI processing
    .collisionAllocated = 1,     // 33% - Minimal collision processing
    .particleAllocated = 0,      // 0%  - Single-threaded fallback
    .eventAllocated = 0,         // 0%  - Single-threaded fallback
    .remaining = 0               // No buffer available
};

// 12-core/24-thread system (23 workers available) - Enthusiast
// Remaining: 21 workers, Buffer: 30% × 21 = 6, Allocate: 15 workers
WorkerBudget budget = {
    .totalWorkers = 23,
    .engineReserved = 2,         // 9% - Enhanced engine capacity
    .aiAllocated = 6,            // (6/14) × 15 ≈ 6 workers (weight: 6)
    .collisionAllocated = 3,     // (3/14) × 15 ≈ 3 workers (weight: 3)
    .particleAllocated = 3,      // (3/14) × 15 ≈ 3 workers (weight: 3)
    .eventAllocated = 3,         // (2/14) × 15 ≈ 2-3 workers (weight: 2, rounded up)
    .remaining = 6               // 30% - Buffer for burst workloads
};
```

### Buffer Thread Utilization

**Current Performance**: Achieves 4-6% CPU usage with optimized WorkerBudget allocation and batch processing.

```cpp
void AIManager::update() {
    size_t entityCount = getActiveEntityCount();
    if (entityCount == 0) return;

    // Calculate optimal worker allocation using WorkerBudget system
    auto& threadSystem = ThreadSystem::Instance();
    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
    WorkerBudget budget = calculateWorkerBudget(availableWorkers);

    // Use WorkerBudget's intelligent buffer allocation
    size_t optimalWorkerCount = budget.getOptimalWorkerCount(budget.aiAllocated, entityCount, 1000);

    // Optimal batching: 2-4 large batches for best performance
    size_t minEntitiesPerBatch = 1000;
    size_t batchCount = std::min(optimalWorkerCount, entityCount / minEntitiesPerBatch);
    batchCount = std::max(size_t(1), std::min(batchCount, size_t(4))); // Cap at 4 batches

    size_t entitiesPerBatch = entityCount / batchCount;
    size_t remainingEntities = entityCount % batchCount;

    // Submit optimized batches
    for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * entitiesPerBatch;
        size_t end = start + entitiesPerBatch;

        // Add remaining entities to last batch
        if (i == batchCount - 1) {
            end += remainingEntities;
        }

        threadSystem.enqueueTask([this, start, end, deltaTime]() {
            processBatch(start, end, deltaTime);
        }, TaskPriority::High, "AI_OptimalBatch");
    }
}
```

**Key Optimizations:**
- **Threshold-based buffer allocation**: Uses 1000 entity threshold for buffer worker activation
- **Optimal batch sizing**: 2-4 large batches (1000+ entities each) for maximum efficiency
- **Lock-free processing**: Pre-cached entity data eliminates lock contention
- **Distance calculation optimization**: Only every 4th frame, active entities only
- **Pure distance culling**: Removed unnecessary frame counting for better performance

### BatchConfig and Adaptive Tuning

The WorkerBudget system includes **BatchConfig** structures and **adaptive tuning** for optimal batch sizing across different manager types:

#### Batch Configuration Per Manager Type

```cpp
// AI Manager Configuration (Complex behavior updates)
static constexpr BatchConfig AI_BATCH_CONFIG = {
    8,      // baseDivisor: threshold/8 for finer-grained parallelism
    128,    // minBatchSize: minimum 128 entities per batch
    2,      // minBatchCount: at least 2 batches for parallel execution
    8,      // maxBatchCount: max 8 batches for better load balancing
    0.5     // targetUpdateTimeMs: 500µs target for AI updates
};

// Particle Manager Configuration (Simple physics updates)
static constexpr BatchConfig PARTICLE_BATCH_CONFIG = {
    4,      // baseDivisor: threshold/4 for better parallelism
    128,    // minBatchSize: minimum 128 particles
    2,      // minBatchCount: at least 2 batches
    8,      // maxBatchCount: up to 8 batches
    0.3     // targetUpdateTimeMs: 300µs target for particle updates
};

// Event Manager Configuration (Mixed complexity)
static constexpr BatchConfig EVENT_BATCH_CONFIG = {
    2,      // baseDivisor: threshold/2 for moderate parallelism
    4,      // minBatchSize: minimum 4 events per batch
    2,      // minBatchCount: at least 2 batches
    4,      // maxBatchCount: up to 4 batches
    0.2     // targetUpdateTimeMs: 200µs target for event updates
};
```

#### Adaptive Performance-Based Tuning

The system includes an **AdaptiveBatchState** that dynamically adjusts batch counts based on measured completion times:

```cpp
struct AdaptiveBatchState {
    std::atomic<float> batchMultiplier{1.0f};     // Dynamic adjustment (0.5x to 1.5x)
    std::atomic<double> lastUpdateTimeMs{0.0};    // Previous frame's completion time

    static constexpr float MIN_MULTIPLIER = 0.5f;  // Never below 50%
    static constexpr float MAX_MULTIPLIER = 1.5f;  // Never above 150%
    static constexpr float ADAPT_RATE = 0.1f;      // 10% adjustment per frame
};
```

**Adaptive Behavior:**
- If completion time > target × 1.15: Reduce batches (less sync overhead)
- If completion time < target × 0.85: Increase batches (more parallelism)
- Otherwise: Maintain current multiplier (within tolerance)
- Smooth 10% adjustments prevent oscillation

#### Queue Pressure Management

The system adapts batch strategy based on ThreadSystem queue pressure:

```cpp
static constexpr float QUEUE_PRESSURE_WARNING = 0.70f;       // Early adaptation (70%)
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;     // Fallback trigger (90%)
static constexpr float QUEUE_PRESSURE_PATHFINDING = 0.75f;  // PathfinderManager (75%)
```

**Queue Pressure Adaptation:**
- **High pressure (>70%)**: Fewer, larger batches to reduce queue overhead
- **Low pressure (<30%)**: More, smaller batches for better parallelism
- **Normal pressure**: Use base configuration values

#### Unified Batch Calculation

The `calculateBatchStrategy()` function provides consistent batch calculation across all managers:

```cpp
// Standard batch calculation
auto [batchCount, batchSize] = calculateBatchStrategy(
    AI_BATCH_CONFIG,          // Manager-specific config
    entityCount,              // Total items to process
    threadingThreshold,       // Threading threshold for this manager
    optimalWorkers,           // From getOptimalWorkerCount()
    queuePressure            // Current ThreadSystem queue pressure
);

// With adaptive tuning
auto [batchCount, batchSize] = calculateBatchStrategy(
    AI_BATCH_CONFIG,
    entityCount,
    threadingThreshold,
    optimalWorkers,
    queuePressure,
    adaptiveState,           // Tracks performance and multiplier
    lastUpdateTimeMs         // Previous frame's completion time
);
```

**Jitter Reduction Strategy:**
- More, smaller batches reduce variance (old: 4 batches → 0.5-1.5ms jitter)
- New config: 8 batches → 0.5-0.8ms variance (smooth frame times)
- Tradeoff: Slightly more overhead, but consistent performance

## API Reference

### Core Initialization Methods

```cpp
class ThreadSystem {
public:
    // Singleton access
    static ThreadSystem& Instance();
    static bool Exists();  // Returns false if shutdown

    // Initialization and cleanup
    bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY,
              unsigned int customThreadCount = 0,
              bool enableProfiling = false);
    void clean();  // Graceful shutdown with pending task logging

    // System status
    bool isShutdown() const;
    unsigned int getThreadCount() const;
    int64_t getTimeSinceLastEnqueue() const;  // Low-activity detection (ms)
};
```

### Task Management Methods

```cpp
// Basic task submission
void enqueueTask(std::function<void()> task,
                 TaskPriority priority = TaskPriority::Normal,
                 const std::string& description = "");

// Task with result
template<class F, class... Args>
auto enqueueTaskWithResult(F&& f,
                          TaskPriority priority = TaskPriority::Normal,
                          const std::string& description = "",
                          Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type>;
```

### Queue Management Methods

```cpp
// Queue status
bool isBusy() const;
size_t getQueueSize() const;
size_t getQueueCapacity() const;
bool reserveQueueCapacity(size_t capacity);

// Statistics
size_t getTotalTasksProcessed() const;
size_t getTotalTasksEnqueued() const;

// Debug and profiling
void setDebugLogging(bool enable);
bool isDebugLoggingEnabled() const;
```

## Engine Integration

### AIManager Integration

```cpp
class AIManager {
private:
    void updateEntitiesWithOptimalBatching(float deltaTime) {
        size_t entityCount = m_storage.size();
        if (entityCount == 0) return;

        // Use WorkerBudget system for optimal resource allocation
        auto& threadSystem = ThreadSystem::Instance();
        size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
        WorkerBudget budget = calculateWorkerBudget(availableWorkers);

        // Get optimal worker count with buffer allocation
        size_t optimalWorkerCount = budget.getOptimalWorkerCount(budget.aiAllocated, entityCount, 1000);

        // Optimal batching: 2-4 large batches for maximum efficiency
        size_t minEntitiesPerBatch = 1000;
        size_t batchCount = std::min(optimalWorkerCount, entityCount / minEntitiesPerBatch);
        batchCount = std::max(size_t(1), std::min(batchCount, size_t(4)));

        size_t entitiesPerBatch = entityCount / batchCount;
        size_t remainingEntities = entityCount % batchCount;

        // Submit optimized batches with High priority
        for (size_t i = 0; i < batchCount; ++i) {
            size_t start = i * entitiesPerBatch;
            size_t end = start + entitiesPerBatch;

            if (i == batchCount - 1) {
                end += remainingEntities;
            }

            threadSystem.enqueueTask([this, start, end, deltaTime]() {
                processBatch(start, end, deltaTime);
            }, TaskPriority::High, "AI_OptimalBatch");
        }
    }

    // Optimized batch processing with lock-free entity caching
    void processBatch(size_t start, size_t end, float deltaTime) {
        // Pre-cache entities and behaviors to reduce lock contention
        std::vector<EntityPtr> batchEntities;
        std::vector<std::shared_ptr<AIBehavior>> batchBehaviors;

        // Single lock acquisition for entire batch
        {
            std::shared_lock<std::shared_mutex> lock(m_entitiesMutex);
            for (size_t i = start; i < end && i < m_storage.size(); ++i) {
                batchEntities.push_back(m_storage.entities[i]);
                batchBehaviors.push_back(m_storage.behaviors[i]);
            }
        }

        // Process entities without locks using pre-calculated distance thresholds
        float maxDistSquared = m_maxUpdateDistance.load() * m_maxUpdateDistance.load();

        for (size_t idx = 0; idx < batchEntities.size(); ++idx) {
            EntityPtr entity = batchEntities[idx];
            auto behavior = batchBehaviors[idx];

            if (entity && behavior) {
                // Pure distance-based culling (no frame counting)
                bool shouldUpdate = true;
                if (hasPlayer) {
                    float distanceSquared = calculateDistanceSquared(entity->getPosition());
                    shouldUpdate = (distanceSquared <= maxDistSquared);
                }

                if (shouldUpdate) {
                    behavior->executeLogic(entity);
                    entity->update(deltaTime);
                }
            }
        }
    }
};
```

### EventManager Integration

```cpp
class EventManager {
private:
    void processEventsParallel() {
        auto eventBatches = partitionEventsByType(pendingEvents);

        for (const auto& [eventType, events] : eventBatches) {
            ThreadSystem::Instance().enqueueTask([=]() {
                for (const auto& event : events) {
                    processEvent(event);
                }
            }, getEventPriority(eventType),
               "Event_Batch_" + std::to_string(static_cast<int>(eventType)));
        }
    }

    TaskPriority getEventPriority(EventType type) {
        switch (type) {
            case EventType::Input: return TaskPriority::Critical;
            case EventType::Physics: return TaskPriority::High;
            case EventType::Audio: return TaskPriority::Normal;
            case EventType::UI: return TaskPriority::Low;
            default: return TaskPriority::Normal;
        }
    }
};
```

## Performance Optimization

### Best Practices

#### ✅ Optimal Usage Patterns

```cpp
// 1. Batch similar operations
std::vector<Entity*> entities = getEntities();
size_t batchSize = entities.size() / ThreadSystem::Instance().getThreadCount();

for (size_t i = 0; i < entities.size(); i += batchSize) {
    ThreadSystem::Instance().enqueueTask([=]() {
        processBatch(entities, i, std::min(batchSize, entities.size() - i));
    }, TaskPriority::Normal, "Entity Processing Batch");
}

// 2. Use appropriate priorities
ThreadSystem::Instance().enqueueTask(criticalRenderTask,
                                   TaskPriority::Critical, "Render Update");
ThreadSystem::Instance().enqueueTask(backgroundLoadTask,
                                   TaskPriority::Low, "Asset Loading");

// 3. Process large workloads with optimal batching
for (int i = 0; i < 10000; ++i) {
    ThreadSystem::Instance().enqueueTask([=]() {
        processEntity(i);
    }, TaskPriority::Normal, "Entity_" + std::to_string(i));
}
// WorkerBudget system provides optimal resource allocation
```

#### ⚠️ Anti-Patterns to Avoid

```cpp
// ❌ Don't create too many tiny tasks
for (int i = 0; i < 100000; ++i) {
    ThreadSystem::Instance().enqueueTask([=]() {
        simpleOperation(i);  // Overhead > benefit
    });
}

// ❌ Don't block worker threads
ThreadSystem::Instance().enqueueTask([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Wastes worker
});

// ❌ Don't use high priority for non-critical tasks
ThreadSystem::Instance().enqueueTask(backgroundTask,
                                   TaskPriority::Critical); // Wrong priority
```

### Performance Guidelines

| Workload Size | Recommendation | Expected Efficiency |
|---------------|----------------|-------------------|
| **1-100 tasks** | Single batch or sequential | 70-85% |
| **100-1,000 tasks** | Worker-count batches | 85-90% |
| **1,000+ tasks** | Individual task submission | 90%+ with optimal batching |
| **10,000+ tasks** | WorkerBudget allocation scenario | 95%+ efficiency |

### Memory Optimization

```cpp
// Reserve capacity for known workloads
ThreadSystem::Instance().reserveQueueCapacity(expectedTaskCount);

// Use move semantics for large captures
auto largeData = generateLargeDataSet();
ThreadSystem::Instance().enqueueTask([data = std::move(largeData)]() {
    processLargeData(data);
}, TaskPriority::Normal, "Large Data Processing");

// Avoid excessive task descriptions in release builds
#ifdef DEBUG
    std::string description = "Detailed debug info: " + generateDescription();
#else
    std::string description = "";
#endif
ThreadSystem::Instance().enqueueTask(task, priority, description);
```

## Thread Safety

### Safe Patterns

```cpp
// ✅ Thread-safe singleton access
auto& threadSystem = ThreadSystem::Instance();

// ✅ Atomic operations for shared state
std::atomic<int> sharedCounter{0};
ThreadSystem::Instance().enqueueTask([&]() {
    sharedCounter.fetch_add(1, std::memory_order_relaxed);
});

// ✅ Mutex-protected critical sections
std::mutex dataMutex;
std::vector<int> sharedData;

ThreadSystem::Instance().enqueueTask([&]() {
    std::lock_guard<std::mutex> lock(dataMutex);
    sharedData.push_back(42);
});

// ✅ Thread-local storage for worker-specific data
thread_local int workerSpecificData = 0;
ThreadSystem::Instance().enqueueTask([]() {
    workerSpecificData++; // Safe: each worker has its own copy
});
```

### Thread-Safe Operations

| Operation | Thread Safety | Notes |
|-----------|---------------|-------|
| `enqueueTask()` | ✅ Fully thread-safe | Can be called from any thread |
| `enqueueTaskWithResult()` | ✅ Fully thread-safe | Returns thread-safe future |
| `getQueueSize()` | ✅ Thread-safe read | May be slightly outdated |
| `isBusy()` | ✅ Thread-safe read | Atomic operation |
| `clean()` | ⚠️ Single thread only | Call only during shutdown |

## Error Handling

### Exception Safety

```cpp
// ThreadSystem provides strong exception safety
ThreadSystem::Instance().enqueueTask([]() {
    try {
        riskyOperation();
    } catch (const std::exception& e) {
        THREADSYSTEM_ERROR("Task failed: " + std::string(e.what()));
        handleError(e);
    }
}, TaskPriority::Normal, "Risky Operation");

// Worker threads are protected from task exceptions
ThreadSystem::Instance().enqueueTask([]() {
    throw std::runtime_error("Task exception");
    // Worker thread continues running normally
});
```

### Error Recovery Patterns

```cpp
// Retry pattern for critical operations
void executeWithRetry(std::function<void()> task, int maxRetries = 3) {
    ThreadSystem::Instance().enqueueTask([=]() {
        for (int attempt = 0; attempt < maxRetries; ++attempt) {
            try {
                task();
                return; // Success
            } catch (const std::exception& e) {
                if (attempt == maxRetries - 1) {
                    THREADSYSTEM_ERROR("Task failed after " + std::to_string(maxRetries) + " attempts");
                    throw;
                }
                THREADSYSTEM_WARN("Task attempt " + std::to_string(attempt + 1) + " failed, retrying...");
            }
        }
    }, TaskPriority::High, "Retry Task");
}
```

## Shutdown and Low-Activity Detection

### Graceful Shutdown Process

The ThreadSystem provides a robust shutdown mechanism that ensures clean termination and proper resource cleanup:

**Shutdown Sequence:**
```cpp
void ThreadSystem::clean() {
    // 1. Set shutdown flag to reject new task submissions
    m_isShutdown.store(true, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    // 2. Notify all worker threads to check shutdown flag
    m_threadPool->getTaskQueue().notifyAllThreads();

    // 3. Brief delay for threads to notice shutdown signal
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 4. Log pending tasks before cancellation
    size_t pendingTasks = m_threadPool->getTaskQueue().size();
    if (pendingTasks > 0) {
        THREADSYSTEM_INFO("Canceling " + std::to_string(pendingTasks) +
                          " pending tasks during shutdown...");
    }

    // 5. Reset thread pool (triggers destructor for clean thread termination)
    m_threadPool.reset();

    // 6. Final delay for thread message propagation
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
```

**Shutdown Features:**
- **Atomic Shutdown Flag**: Prevents new task submissions after shutdown begins
- **Memory Fence**: Ensures shutdown flag visibility across all threads
- **Pending Task Logging**: Reports number of canceled tasks for debugging
- **Clean Thread Termination**: Worker threads exit gracefully without forced termination
- **Double-Shutdown Protection**: Destructor checks shutdown flag to prevent duplicate cleanup
- **Re-initialization Prevention**: `init()` rejects calls after shutdown

**Shutdown Safety Example:**
```cpp
void GameEngine::cleanup() {
    // Wait for critical tasks to complete
    while (ThreadSystem::Instance().isBusy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Clean shutdown
    ThreadSystem::Instance().clean();

    // Attempting to use ThreadSystem after shutdown
    if (ThreadSystem::Exists()) {
        // Safe to use
        ThreadSystem::Instance().enqueueTask(/*...*/);
    } else {
        // ThreadSystem is shutdown - this branch will execute
        GAMEENGINE_WARN("ThreadSystem shutdown, skipping task");
    }
}
```

### Low-Activity Detection

The ThreadSystem tracks thread pool activity to enable intelligent system optimizations and diagnostics:

**Activity Tracking:**
```cpp
// Automatic tracking on every task enqueue
void TaskQueue::pushTask(PrioritizedTask task, ...) {
    // Update activity timestamp atomically
    m_lastEnqueueTime.store(std::chrono::steady_clock::now(),
                           std::memory_order_relaxed);
    // ... enqueue task
}

// Query time since last activity
int64_t idleTime = ThreadSystem::Instance().getTimeSinceLastEnqueue();
```

**Use Cases:**

1. **Idle Detection for Power Management:**
```cpp
void GameEngine::update(float deltaTime) {
    auto& ts = ThreadSystem::Instance();
    int64_t idleTimeMs = ts.getTimeSinceLastEnqueue();

    if (idleTimeMs > 5000) {  // 5 seconds idle
        GAMEENGINE_INFO("ThreadSystem idle for " + std::to_string(idleTimeMs) +
                       "ms - reducing update frequency");
        // Reduce update rate or enter low-power mode
        updateFrequency = 30;  // 30 FPS instead of 60
    }
}
```

2. **Performance Monitoring:**
```cpp
void monitorThreadActivity() {
    int64_t idleTime = ThreadSystem::Instance().getTimeSinceLastEnqueue();

    if (idleTime > 1000) {
        THREADSYSTEM_INFO("ThreadSystem idle for " + std::to_string(idleTime) + "ms");
    } else {
        THREADSYSTEM_DEBUG("Active workload - last task " + std::to_string(idleTime) + "ms ago");
    }
}
```

3. **Load Balancing Decisions:**
```cpp
void AIManager::update(float deltaTime) {
    auto& ts = ThreadSystem::Instance();
    int64_t idleTime = ts.getTimeSinceLastEnqueue();

    // If thread pool is idle, safe to submit more granular batches
    if (idleTime > 100) {
        // Thread pool has capacity - use smaller batches for better parallelism
        batchCount = optimalWorkerCount;
    } else {
        // Thread pool is busy - use larger batches to reduce overhead
        batchCount = std::min(optimalWorkerCount, size_t(4));
    }
}
```

4. **Diagnostic Logging:**
```cpp
void logThreadSystemHealth() {
    auto& ts = ThreadSystem::Instance();

    std::stringstream ss;
    ss << "ThreadSystem Health Report:\n";
    ss << "  Queue Size: " << ts.getQueueSize() << "\n";
    ss << "  Busy: " << (ts.isBusy() ? "Yes" : "No") << "\n";
    ss << "  Time Since Last Enqueue: " << ts.getTimeSinceLastEnqueue() << "ms\n";
    ss << "  Tasks Processed: " << ts.getTotalTasksProcessed() << "\n";

    THREADSYSTEM_INFO(ss.str());
}
```

**Activity Detection Best Practices:**
- **Idle Threshold**: 100-1000ms is a reasonable idle threshold for most games
- **Power Management**: Use longer thresholds (5000ms+) for battery optimization
- **Load Balancing**: Check activity before submitting large batch workloads
- **Diagnostic Logging**: Include activity metrics in performance reports

## Integration Examples

### Complete Game Loop Integration

```cpp
class GameEngine {
private:
    bool init() {
        // Initialize ThreadSystem early in engine startup
        if (!ThreadSystem::Instance().init(4096, 0, true)) {
            THREADSYSTEM_ERROR("Failed to initialize ThreadSystem");
            return false;
        }

        // Initialize other systems
        if (!initRenderer() || !initAIManager() || !initEventManager()) {
            return false;
        }

        THREADSYSTEM_INFO("Game engine initialized successfully");
        return true;
    }

    void update(float deltaTime) {
        // Update systems using ThreadSystem
        updateInput();      // Main thread

        // Parallel updates
        ThreadSystem::Instance().enqueueTask([=]() {
            aiManager.update(deltaTime);
        }, TaskPriority::High, "AI Update");

        ThreadSystem::Instance().enqueueTask([=]() {
            physicsManager.update(deltaTime);
        }, TaskPriority::High, "Physics Update");

        ThreadSystem::Instance().enqueueTask([=]() {
            audioManager.update(deltaTime);
        }, TaskPriority::Normal, "Audio Update");

        // Wait for critical updates before rendering
        while (ThreadSystem::Instance().isBusy()) {
            std::this_thread::yield();
        }

        render(); // Main thread
    }

    void cleanup() {
        // Clean shutdown - process remaining tasks
        while (ThreadSystem::Instance().isBusy()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        ThreadSystem::Instance().clean();
        THREADSYSTEM_INFO("Game engine shutdown complete");
    }
};
```

## Monitoring and Debugging

### Performance Monitoring

```cpp
void monitorThreadSystemPerformance() {
    auto& ts = ThreadSystem::Instance();

    // Basic statistics
    size_t queueSize = ts.getQueueSize();
    size_t processed = ts.getTotalTasksProcessed();
    size_t enqueued = ts.getTotalTasksEnqueued();

    // Performance metrics
    double throughput = static_cast<double>(processed) / getUptimeSeconds();
    double queueUtilization = static_cast<double>(queueSize) / ts.getQueueCapacity();

    THREADSYSTEM_INFO("Performance - Throughput: " + std::to_string(throughput) +
                     " tasks/sec, Queue: " + std::to_string(queueUtilization * 100) + "%");

    // Alert on performance issues
    if (queueUtilization > 0.8) {
        THREADSYSTEM_WARN("High queue utilization detected: " +
                         std::to_string(queueUtilization * 100) + "%");
    }
}
```

### Debug Information

```cpp
void debugThreadSystem() {
    auto& ts = ThreadSystem::Instance();

    // Enable detailed logging
    ts.setDebugLogging(true);

    // System status
    THREADSYSTEM_DEBUG("ThreadSystem Status:");
    THREADSYSTEM_DEBUG("  Workers: " + std::to_string(ts.getThreadCount()));
    THREADSYSTEM_DEBUG("  Queue Size: " + std::to_string(ts.getQueueSize()));
    THREADSYSTEM_DEBUG("  Queue Capacity: " + std::to_string(ts.getQueueCapacity()));
    THREADSYSTEM_DEBUG("  Is Busy: " + std::string(ts.isBusy() ? "Yes" : "No"));
    THREADSYSTEM_DEBUG("  Is Shutdown: " + std::string(ts.isShutdown() ? "Yes" : "No"));

    // Task statistics
    THREADSYSTEM_DEBUG("Task Statistics:");
    THREADSYSTEM_DEBUG("  Enqueued: " + std::to_string(ts.getTotalTasksEnqueued()));
    THREADSYSTEM_DEBUG("  Processed: " + std::to_string(ts.getTotalTasksProcessed()));

    size_t pending = ts.getTotalTasksEnqueued() - ts.getTotalTasksProcessed();
    THREADSYSTEM_DEBUG("  Pending: " + std::to_string(pending));
}
```

## Troubleshooting

### Common Issues

| Issue | Symptoms | Solution |
|-------|----------|----------|
| **High queue utilization** | Tasks queuing up, performance degradation | Increase worker count or optimize task granularity |
| **Worker thread starvation** | Some workers idle while others busy | Optimize task batching and WorkerBudget allocation |
| **Memory growth** | Increasing memory usage | Reserve queue capacity, optimize task captures |
| **Deadlocks** | System hanging | Avoid blocking operations in tasks |
| **Poor performance** | Low throughput | Check task granularity, use appropriate priorities |

### Performance Validation

```cpp
void validateThreadSystemPerformance() {
    // Test load balancing with large workload
    const size_t taskCount = 10000;
    std::atomic<size_t> completedTasks{0};

    auto startTime = std::chrono::steady_clock::now();

    for (size_t i = 0; i < taskCount; ++i) {
        ThreadSystem::Instance().enqueueTask([&]() {
            // Simulate work
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            completedTasks.fetch_add(1, std::memory_order_relaxed);
        }, TaskPriority::Normal, "Load Test Task");
    }

    // Wait for completion
    while (completedTasks.load() < taskCount) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Expected: <2000ms for 10,000 tasks on 8-core system
    THREADSYSTEM_INFO("Load test completed in " + std::to_string(duration.count()) + "ms");

    // Validate load balancing effectiveness
    if (duration.count() < 2000) {
        THREADSYSTEM_INFO("✅ Excellent load balancing performance");
    } else {
        THREADSYSTEM_WARN("⚠️ Suboptimal performance - check WorkerBudget allocation");
    }
}
```

## Advanced Usage

### Custom WorkerBudget Integration

```cpp
class CustomSubsystem {
private:
    WorkerBudget calculateOptimalBudget() {
        size_t totalWorkers = ThreadSystem::Instance().getThreadCount();

        // Use the engine's weight-based allocation function
        return HammerEngine::calculateWorkerBudget(totalWorkers);

        /* Weight-based allocation logic (from WorkerBudget.hpp):
         * - Engine: 1-2 workers (adaptive based on system tier)
         * - Buffer: 30% of remaining workers reserved
         * - Base allocation: 70% distributed by weights
         *   - AI: weight 6 → (6/14) of base = ~43%
         *   - Collision: weight 3 → (3/14) of base = ~21%
         *   - Particles: weight 3 → (3/14) of base = ~21%
         *   - Events: weight 2 → (2/14) of base = ~14%
         */
    }

public:
    void processWithBudget() {
        auto budget = calculateOptimalBudget();

        // Use allocated workers efficiently
        size_t workersToUse = budget.aiAllocated;
        if (getWorkloadSize() > 1000 && budget.hasBufferCapacity()) {
            workersToUse += std::min(budget.remaining, 2UL);
        }

        distributeTasks(workersToUse);
    }
};
```

### Task Pipeline Pattern

```cpp
class TaskPipeline {
public:
    template<typename T>
    auto stage1(const std::vector<T>& input) -> std::future<std::vector<T>> {
        return ThreadSystem::Instance().enqueueTaskWithResult([=]() {
            std::vector<T> result;
            for (const auto& item : input) {
                result.push_back(processStage1(item));
            }
            return result;
        }, TaskPriority::High, "Pipeline Stage 1");
    }

    template<typename T>
    auto stage2(std::future<std::vector<T>>&& input) -> std::future<std::vector<T>> {
        return ThreadSystem::Instance().enqueueTaskWithResult([input = std::move(input)]() mutable {
            auto data = input.get(); // Wait for previous stage
            std::vector<T> result;
            for (const auto& item : data) {
                result.push_back(processStage2(item));
            }
            return result;
        }, TaskPriority::Normal, "Pipeline Stage 2");
    }

    template<typename T>
    void execute(const std::vector<T>& input) {
        auto stage1Result = stage1(input);
        auto stage2Result = stage2(std::move(stage1Result));

        // Final processing
        ThreadSystem::Instance().enqueueTask([stage2Result = std::move(stage2Result)]() mutable {
            auto finalResult = stage2Result.get();
            processFinalResult(finalResult);
        }, TaskPriority::Normal, "Pipeline Final");
    }
};
```

## Production Readiness & Best Practices

### ✅ Optimal Usage Patterns

**Large Batch Processing:**
```cpp
// ✅ Process entities in large batches (1000+ items)
void AIManager::updateOptimal() {
    size_t workerCount = ThreadSystem::Instance().getThreadCount();
    size_t batchSize = entities.size() / workerCount;

    for (size_t i = 0; i < workerCount; ++i) {
        ThreadSystem::Instance().enqueueTask([=]() {
            processBatch(i * batchSize, batchSize);
        }, TaskPriority::Normal, "AI_Batch_" + std::to_string(i));
    }
}
```

**Priority-Based Task Submission:**
```cpp
// ✅ Use appropriate priorities
ThreadSystem::Instance().enqueueTask(renderCriticalTask, TaskPriority::Critical);
ThreadSystem::Instance().enqueueTask(gameLogicTask, TaskPriority::Normal);
ThreadSystem::Instance().enqueueTask(backgroundTask, TaskPriority::Low);
```

**WorkerBudget-Aware Processing:**
```cpp
// ✅ Adapt to available workers
WorkerBudget budget = calculateWorkerBudget(totalWorkers);
size_t optimalWorkers = budget.getOptimalWorkerCount(
    budget.aiAllocated, entityCount, 1000);
```

### ❌ Anti-Patterns to Avoid

**Excessive Small Tasks:**
```cpp
// ❌ Don't create tiny tasks
for (auto& entity : entities) {
    ThreadSystem::Instance().enqueueTask([&entity]() {
        entity.update(); // Too small!
    });
}
```

**Blocking Operations in Tasks:**
```cpp
// ❌ Don't block worker threads
ThreadSystem::Instance().enqueueTask([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Bad!
    loadResourceFromDisk(); // Also bad!
});
```

**Ignoring Task Priorities:**
```cpp
// ❌ Don't use Normal priority for everything
ThreadSystem::Instance().enqueueTask(task, TaskPriority::Normal); // Consider priority!
```

### Performance Validation

**System Health Check:**
```cpp
void validateThreadSystemHealth() {
    auto& ts = ThreadSystem::Instance();

    // Check queue utilization
    double utilization = static_cast<double>(ts.getQueueSize()) / ts.getQueueCapacity();
    if (utilization > 0.8) {
        THREADSYSTEM_WARN("Queue utilization high: " + std::to_string(utilization * 100) + "%");
    }

    // Check worker efficiency
    size_t processed = ts.getTotalTasksProcessed();
    size_t enqueued = ts.getTotalTasksEnqueued();
    if (processed < enqueued * 0.95) {
        THREADSYSTEM_WARN("Worker efficiency below 95%");
    }
}
```

### Production Deployment Checklist

- [ ] **Memory Validation**: Confirm <1KB overhead on target hardware
- [ ] **Performance Testing**: Validate 90%+ efficiency under peak loads
- [ ] **Error Handling**: Test graceful degradation under high stress
- [ ] **Resource Monitoring**: Implement queue and worker utilization tracking
- [ ] **Platform Testing**: Verify behavior across all target platforms
- [ ] **Load Testing**: Test with 10,000+ tasks over extended periods

## Conclusion

The ThreadSystem provides a robust, high-performance foundation for multi-threaded game development. With intelligent WorkerBudget allocation, priority-based scheduling, and comprehensive performance monitoring, it enables scalable, maintainable concurrent programming.

### Key Takeaways

- **Simple API**: Fire-and-forget or result-returning task submission
- **Automatic Optimization**: WorkerBudget allocation and priority scheduling require no configuration
- **Production Ready**: Comprehensive error handling, thread safety, and monitoring
- **Engine Integrated**: Seamless integration with all engine subsystems
- **Performance Focused**: Optimized for game development workloads

The ThreadSystem transforms complex concurrent programming into simple task submission, enabling developers to focus on game logic while achieving optimal multi-core performance automatically.

## See Also

**Core Systems:**
- [GameEngine](GameEngine.md) - Central engine coordination and integration
- [GameLoop](GameLoop.md) - Fixed timestep timing and update/render separation
- [WorkerBudget](../utils/WorkerBudget.md) - Resource allocation across engine subsystems

**Manager Integration:**
- [AIManager](../ai/AIManager.md) - AI system threading with optimal batching
- [ParticleManager](../managers/ParticleManager.md) - Particle system WorkerBudget integration
- [EventManager](../events/EventManager.md) - Event system parallel processing
- [CollisionManager](../managers/CollisionManager.md) - Collision detection threading

**Performance:**
- [SIMDMath](../utils/SIMDMath.md) - SIMD optimizations for parallel workloads
- [Performance Notes](../../hammer_engine_performance.md) - Engine-wide performance benchmarks
