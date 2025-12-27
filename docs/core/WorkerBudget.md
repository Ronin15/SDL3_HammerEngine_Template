# WorkerBudget Documentation

**Where to find the code:**
- Header: `include/core/WorkerBudget.hpp`
- Implementation: `src/core/WorkerBudget.cpp`

## Overview

The WorkerBudget system provides **adaptive batch optimization** for parallel workloads in the Hammer Engine. It uses a **sequential execution model** where each manager gets ALL workers during its update window, combined with **throughput-based hill-climbing** to find optimal batch counts.

### Key Concepts

1. **Sequential Execution**: Managers don't run concurrently - each gets full worker pool access
2. **Batch Optimization**: WorkerBudget tunes batch COUNT, not worker allocation
3. **Adaptive Learning**: Throughput-based hill-climbing converges to optimal for your hardware
4. **Queue Pressure**: Automatic scaling when ThreadSystem is overloaded

---

## Sequential Execution Model

### Why Sequential?

The Hammer Engine uses a sequential manager update pattern:

```
GameEngine::update()
├── EventManager.update()      ← ALL workers available
├── GameStateManager.update()  ← ALL workers available
├── AIManager.update()         ← ALL workers available
├── ParticleManager.update()   ← ALL workers available
├── PathfinderManager.update() ← ALL workers available
└── CollisionManager.update()  ← ALL workers available
```

**Benefits:**
- **No contention**: Managers don't compete for workers
- **Full utilization**: Each manager uses 100% of available workers
- **Simpler design**: No complex allocation percentages to tune
- **Better cache locality**: Workers process related work together

### Comparison to Old Model

| Aspect | Old Model (Deprecated) | Current Model |
|--------|----------------------|---------------|
| Allocation | AI: 54%, Particles: 31%, Events: 15% | Each manager: 100% |
| Execution | Concurrent managers | Sequential managers |
| Tuning | Fixed percentages | Adaptive batch sizing |
| Complexity | Per-manager weights, buffer reserve | Just batch count tuning |

---

## WorkerBudgetManager API

### Singleton Access

```cpp
#include "core/WorkerBudget.hpp"

auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
```

### Core Methods

#### `getBudget()`

Returns the cached worker budget (total available workers).

```cpp
const WorkerBudget& getBudget();

// Usage
auto budget = budgetMgr.getBudget();
size_t totalWorkers = budget.totalWorkers;  // e.g., 7 on 8-core system
```

#### `getOptimalWorkers()`

Returns optimal worker count for a system. With sequential execution, this returns ALL workers for any active workload.

```cpp
size_t getOptimalWorkers(SystemType system, size_t workloadSize);

// Usage
size_t workers = budgetMgr.getOptimalWorkers(
    HammerEngine::SystemType::AI,
    entityCount);  // Returns totalWorkers if entityCount > 0
```

**Behavior:**
- Returns 0 if workloadSize is 0
- Returns all workers for any positive workload
- Scales back under critical queue pressure (>90%)

#### `getBatchStrategy()`

Returns optimal batch count and size based on workload and learned throughput.

```cpp
std::pair<size_t, size_t> getBatchStrategy(
    SystemType system,
    size_t workloadSize,
    size_t optimalWorkers);

// Usage
auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
    HammerEngine::SystemType::AI,
    10000,  // 10K entities
    7);     // 7 workers

// Result might be: batchCount=8, batchSize=1250
```

**Algorithm:**
1. Base calculation: `batchCount = optimalWorkers`
2. Apply learned multiplier: `batchCount *= multiplier` (0.25x to 2.5x range)
3. Ensure minimum items per batch (8 items)
4. Clamp to valid range

#### `reportBatchCompletion()`

Reports execution metrics for adaptive tuning. **Critical for learning!**

```cpp
void reportBatchCompletion(
    SystemType system,
    size_t workloadSize,
    size_t batchCount,
    double totalTimeMs);

// Usage
auto startTime = std::chrono::steady_clock::now();
// ... execute batches ...
auto elapsed = std::chrono::steady_clock::now() - startTime;

budgetMgr.reportBatchCompletion(
    HammerEngine::SystemType::AI,
    10000,  // Processed 10K items
    8,      // In 8 batches
    std::chrono::duration<double, std::milli>(elapsed).count());
```

---

## SystemType Enum

```cpp
enum class SystemType : uint8_t {
    AI = 0,         // AIManager
    Particle = 1,   // ParticleManager
    Pathfinding = 2,// PathfinderManager
    Event = 3,      // EventManager
    Collision = 4,  // CollisionManager
    COUNT = 5
};
```

Each system maintains its own learning state (multiplier, throughput history).

---

## Adaptive Batch Tuning (Hill-Climbing)

### How It Works

WorkerBudgetManager uses **throughput-based hill-climbing** to find optimal batch counts:

```
┌─────────────────────────────────────────────────────────────────┐
│                   Hill-Climbing Algorithm                        │
├─────────────────────────────────────────────────────────────────┤
│  1. Start: multiplier = 1.0 (batchCount = workerCount)          │
│                                                                 │
│  2. Measure: throughput = workloadSize / totalTimeMs            │
│                                                                 │
│  3. Smooth: smoothedThroughput = 0.88 * prev + 0.12 * current   │
│                                                                 │
│  4. Compare to previous smoothed throughput:                    │
│     - Better (>6%): continue in same direction                  │
│     - Worse (<-6%): reverse direction                           │
│     - Same (±6%): maintain position (dead band)                 │
│                                                                 │
│  5. Adjust: multiplier += direction * 0.02 (2% step)            │
│                                                                 │
│  6. Clamp: multiplier ∈ [0.25, 2.5]                             │
└─────────────────────────────────────────────────────────────────┘
```

### Multiplier Range

| Multiplier | Meaning | When Used |
|------------|---------|-----------|
| 0.25x | 4x consolidation (fewer, larger batches) | High per-batch overhead |
| 1.0x | Default (batchCount = workerCount) | Starting point |
| 2.5x | 2.5x expansion (more, smaller batches) | Very fast workers |

### Convergence Example

```
8-core system, 10,000 AI entities:

Frame 1: multiplier=1.0, batchCount=8, throughput=1200/ms
Frame 2: multiplier=1.02 (+), batchCount=8, throughput=1280/ms (better!)
Frame 3: multiplier=1.04 (+), batchCount=8, throughput=1310/ms (better!)
Frame 4: multiplier=1.06 (+), batchCount=9, throughput=1290/ms (worse)
Frame 5: multiplier=1.04 (-), batchCount=8, throughput=1305/ms (converged)
Frame 6+: stable around multiplier=1.04, batchCount=8
```

### Tuning Constants

```cpp
struct BatchTuningState {
    static constexpr float MIN_MULTIPLIER = 0.25f;   // Allow 4x consolidation
    static constexpr float MAX_MULTIPLIER = 2.5f;    // Allow 2.5x expansion
    static constexpr float ADJUST_RATE = 0.02f;      // 2% adjustment per frame

    static constexpr double THROUGHPUT_TOLERANCE = 0.06;   // 6% dead band
    static constexpr double THROUGHPUT_SMOOTHING = 0.12;   // 12% smoothing weight

    static constexpr size_t MIN_ITEMS_PER_BATCH = 8;  // Minimum batch size
};
```

---

## Queue Pressure Handling

WorkerBudgetManager monitors ThreadSystem queue utilization to prevent overflow:

```cpp
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;  // 90% threshold
```

### Behavior Under Pressure

| Queue Utilization | Response |
|-------------------|----------|
| < 90% | Full workers, normal batching |
| ≥ 90% | Reduced workers, larger batches |

```cpp
double queuePressure = getQueuePressure();
if (queuePressure >= QUEUE_PRESSURE_CRITICAL) {
    // Scale back workers proportionally
    size_t scaled = static_cast<size_t>(
        totalWorkers * (1.0 - queuePressure)
    );
    return std::max(scaled, size_t(1));
}
```

---

## Integration Guide

### Standard Manager Integration

All managers follow this pattern:

```cpp
void Manager::update(float deltaTime) {
    size_t workloadSize = getActiveItemCount();
    if (workloadSize == 0) return;

    // 1. Get optimal configuration
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
    size_t optimalWorkers = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::MyType, workloadSize);

    auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::MyType,
        workloadSize,
        optimalWorkers);

    // 2. Execute batches
    auto startTime = std::chrono::steady_clock::now();

    for (size_t i = 0; i < batchCount; ++i) {
        size_t start = i * batchSize;
        size_t end = std::min(start + batchSize, workloadSize);

        ThreadSystem::Instance().enqueueTask([this, start, end]() {
            processBatch(start, end);
        }, TaskPriority::High, "Manager_Batch");
    }

    // 3. Wait for completion
    // ... (implementation-specific) ...

    // 4. Report metrics for adaptive tuning
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    budgetMgr.reportBatchCompletion(
        HammerEngine::SystemType::MyType,
        workloadSize,
        batchCount,
        std::chrono::duration<double, std::milli>(elapsed).count());
}
```

### Adding a New SystemType

1. Add to enum in `WorkerBudget.hpp`:
```cpp
enum class SystemType : uint8_t {
    AI = 0,
    Particle = 1,
    Pathfinding = 2,
    Event = 3,
    Collision = 4,
    MyNewSystem = 5,  // Add here
    COUNT = 6         // Update count
};
```

2. Use in your manager:
```cpp
size_t workers = budgetMgr.getOptimalWorkers(
    HammerEngine::SystemType::MyNewSystem, workloadSize);
```

---

## Performance Characteristics

### Memory Footprint

| Component | Size |
|-----------|------|
| WorkerBudget struct | 8 bytes |
| BatchTuningState (per system) | ~64 bytes |
| Total (5 systems) | ~350 bytes |

### Overhead

| Operation | Time |
|-----------|------|
| `getBudget()` (cached) | < 10ns |
| `getOptimalWorkers()` | < 50ns |
| `getBatchStrategy()` | < 100ns |
| `reportBatchCompletion()` | < 200ns |

### Convergence Time

| Scenario | Frames to Converge |
|----------|-------------------|
| Stable workload | 10-20 frames |
| Changing workload | 20-50 frames |
| After restart | 10-20 frames |

---

## Configuration Constants

### In WorkerBudget.hpp

```cpp
// Queue pressure threshold
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;

// BatchTuningState constants
static constexpr float MIN_MULTIPLIER = 0.25f;      // Allow 4x consolidation
static constexpr float MAX_MULTIPLIER = 2.5f;       // Allow 2.5x expansion
static constexpr float ADJUST_RATE = 0.02f;         // 2% adjustment per frame
static constexpr double THROUGHPUT_TOLERANCE = 0.06; // 6% dead band
static constexpr double THROUGHPUT_SMOOTHING = 0.12; // 12% smoothing weight
static constexpr size_t MIN_ITEMS_PER_BATCH = 8;    // Minimum batch size
```

### When to Adjust

| Constant | Increase When | Decrease When |
|----------|--------------|---------------|
| `ADJUST_RATE` | Slow convergence | Oscillation |
| `THROUGHPUT_TOLERANCE` | Too much jitter | Slow adaptation |
| `THROUGHPUT_SMOOTHING` | Too much noise | Slow response |
| `MIN_ITEMS_PER_BATCH` | Tiny batches inefficient | Large batches underutilize |

---

## Thread Safety

WorkerBudgetManager is fully thread-safe:

- **Atomics**: All mutable state uses `std::atomic`
- **Double-Checked Locking**: Cache invalidation uses proper synchronization
- **No Locks in Hot Path**: `getBatchStrategy()` and `getOptimalWorkers()` are lock-free

```cpp
// Safe to call from any thread
auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();
size_t workers = budgetMgr.getOptimalWorkers(SystemType::AI, 1000);
```

---

## Troubleshooting

### Poor Throughput

**Symptom**: Low items/ms reported
**Cause**: Not calling `reportBatchCompletion()`
**Fix**: Ensure every batch execution reports metrics

### Oscillating Batch Count

**Symptom**: Batch count changes every frame
**Cause**: Noisy workload or timing
**Fix**: Increase `THROUGHPUT_TOLERANCE` or `THROUGHPUT_SMOOTHING`

### Queue Overflow

**Symptom**: Tasks queuing up, high latency
**Cause**: Submitting too many small batches
**Fix**: System automatically handles via queue pressure detection

### Slow Convergence

**Symptom**: Takes too long to find optimal
**Cause**: `ADJUST_RATE` too small
**Fix**: Increase `ADJUST_RATE` (with caution - may cause oscillation)

---

## See Also

- [ThreadSystem](ThreadSystem.md) - Thread pool and task submission
- [AIManager](../ai/AIManager.md) - AI system WorkerBudget integration
- [CollisionManager](../managers/CollisionManager.md) - Collision threading
- [ParticleManager](../managers/ParticleManager.md) - Particle threading
