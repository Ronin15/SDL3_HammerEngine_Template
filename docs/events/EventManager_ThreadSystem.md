# EventManager ThreadSystem Integration

## Overview

The EventManager leverages the ThreadSystem for efficient parallel processing of events. This integration provides significant performance benefits for games with moderate to large event counts while maintaining realistic expectations for real-world scenarios.

## Performance Characteristics

### Realistic Event Scales
- **Small Games**: 10-50 events (82,000-95,000 events/second)
- **Medium Games**: 50-100 events (54,000-58,000 events/second)
- **Large Games**: 100-200 events (24,000-50,000 events/second)
- **Maximum Scale**: ~500 events (still performant)

### Threading Benefits
- **Batch Processing**: Events processed in batches, not individually
- **Type-Based Grouping**: Events grouped by type for cache efficiency
- **Configurable Thresholds**: Threading only when beneficial
- **Automatic Load Balancing**: Work distributed across available cores

## Threading Configuration

### Basic Setup
```cpp
// Initialize ThreadSystem first (required)
Forge::ThreadSystem::Instance().init();

// Initialize EventManager
EventManager::Instance().init();

// Configure threading
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(50); // Use threads if >50 events
```

### Threading Thresholds
- **Threshold < 50 events**: Single-threaded processing (optimal for small scales)
- **Threshold 50-100 events**: Multi-threaded beneficial for medium games
- **Threshold > 100 events**: Multi-threaded essential for large games

## Worker Budget System

The EventManager now implements a sophisticated worker budget allocation system to coordinate thread usage with other engine subsystems and prevent ThreadSystem overload.

### Budget Allocation Strategy

The EventManager receives **30% of available workers** after the GameEngine reserves its minimum allocation:

- **GameEngine**: Always reserves minimum 2 workers for critical operations
- **AIManager**: Gets 60% of remaining workers for entity behavior processing
- **EventManager**: Gets 30% of remaining workers for event processing
- **Buffer**: 10% left free for system responsiveness

### Example Allocations by Core Count

| Total Cores | Available Workers | Engine Reserved | Event Allocated | Max Event Batches |
|-------------|-------------------|-----------------|-----------------|-------------------|
| 4           | 3                 | 2               | 1               | 1                 |
| 8           | 7                 | 2               | 1               | 1                 |
| 12          | 11                | 2               | 2               | 2                 |
| 24          | 23                | 2               | 6               | 6                 |

### Implementation Details

The system automatically calculates optimal batch distribution:

```cpp
// Worker budget is calculated automatically
size_t availableWorkers = threadSystem.getThreadCount();
Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
size_t eventWorkerBudget = budget.eventAllocated;

// Event batches limited to worker budget
size_t maxEventBatches = eventWorkerBudget;
size_t eventsPerBatch = eventContainer.size() / maxEventBatches;
```

### Queue Pressure Management

The system monitors ThreadSystem queue pressure to prevent overload:

```cpp
// Check queue pressure before submitting batches
size_t currentQueueSize = threadSystem.getQueueSize();
size_t maxQueuePressure = availableWorkers * 2;

if (currentQueueSize < maxQueuePressure && eventContainer.size() > 10) {
    // Safe to use threaded processing with worker budget
    submitEventBatches();
} else {
    // Fall back to single-threaded processing
    processSingleThreaded();
}
```

### Performance Benefits

- **Prevents Thread Starvation**: EventManager respects its allocated worker budget
- **Hardware Adaptive**: Automatically scales with processor count
- **System Stability**: Eliminates ThreadSystem queue overflow
- **Coordinated Processing**: Works harmoniously with AIManager and GameEngine
- **Graceful Degradation**: Falls back to single-threaded under pressure

### Integration with WorkerBudget Utility

```cpp
#include "core/WorkerBudget.hpp"

// Calculate budget allocation
Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);
size_t eventWorkerBudget = budget.eventAllocated;

// Use budget for batch sizing
size_t maxConcurrentBatches = eventWorkerBudget;
```

This system ensures that EventManager threading works efficiently alongside other engine subsystems without overwhelming the ThreadSystem.

## Batch Processing Architecture

### Event Type Batching
The EventManager groups events by type for optimal processing:

```cpp
// Internal batch processing (automatic)
void EventManager::update() {
    updateWeatherEvents();      // Batch process all weather events
    updateSceneChangeEvents();  // Batch process all scene events
    updateNPCSpawnEvents();     // Batch process all NPC events
    updateCustomEvents();       // Batch process all custom events
}
```

### Threading Decision Logic
```cpp
void EventManager::updateEventTypeBatch(EventTypeId typeId) {
    auto& eventBatch = m_eventsByType[static_cast<size_t>(typeId)];

    if (m_threadingEnabled.load() && eventBatch.size() > m_threadingThreshold) {
        updateEventTypeBatchThreaded(typeId);  // Use ThreadSystem
    } else {
        // Process single-threaded for efficiency
        for (auto& eventData : eventBatch) {
            if (eventData.isActive()) {
                processEventDirect(eventData);
            }
        }
    }
}
```

## Performance Benefits

### Threading Advantages
1. **Parallel Processing**: Multiple event types processed simultaneously
2. **Cache Efficiency**: Type-based batching improves memory access patterns
3. **Load Distribution**: Work spread across available CPU cores
4. **Scalability**: Performance scales with event count and hardware

### When Threading Helps Most
- **Mixed Event Types**: Different event types can be processed in parallel
- **CPU-Intensive Events**: Events with significant processing requirements
- **High Event Counts**: 100+ events benefit most from parallel processing
- **Multi-Core Systems**: More cores = better parallel performance

## Thread Safety Features

### Lock-Free Operations
```cpp
// Atomic flags for thread-safe state management
std::atomic<bool> m_threadingEnabled{true};
std::atomic<bool> m_initialized{false};
std::atomic<uint64_t> m_lastUpdateTime{0};
```

### Minimal Locking
```cpp
// Shared mutex for read-heavy operations
mutable std::shared_mutex m_eventsMutex;

// Standard mutex only for critical sections
mutable std::mutex m_handlersMutex;
mutable std::mutex m_perfMutex;
```

### Data-Oriented Design
- **Type-Indexed Storage**: Events stored by type for cache efficiency
- **Flat Array Access**: Minimal pointer chasing
- **Batch Processing**: Contiguous memory access patterns

## Performance Monitoring

### Threading Metrics
```cpp
// Monitor threading effectiveness
auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Weather processing time: " << stats.avgTime << "ms" << std::endl;

// Check if threading is beneficial
bool usingThreads = EventManager::Instance().isThreadingEnabled();
size_t eventCount = EventManager::Instance().getEventCount();
std::cout << "Threading enabled: " << usingThreads
          << " for " << eventCount << " events" << std::endl;
```

### Performance Optimization
```cpp
// Adjust threading threshold based on profiling
if (averageUpdateTime > targetTime && eventCount > 50) {
    EventManager::Instance().setThreadingThreshold(30); // Lower threshold
} else if (averageUpdateTime < targetTime && eventCount < 100) {
    EventManager::Instance().setThreadingThreshold(100); // Higher threshold
}
```

## Best Practices

### Threading Guidelines
1. **Initialize ThreadSystem First**: Always call `ThreadSystem::Instance().init()` before EventManager
2. **Set Appropriate Thresholds**: Use 50 events as starting point, adjust based on profiling
3. **Monitor Performance**: Use built-in stats to validate threading benefits
4. **Profile Your Game**: Optimal thresholds vary by hardware and event complexity

### Optimization Tips
1. **Group Similar Events**: Create events of the same type together
2. **Avoid Threading Overhead**: Don't use threading for <50 events
3. **Batch Event Creation**: Use convenience methods for bulk event creation
4. **Monitor Memory Usage**: Call `compactEventStorage()` periodically

### Error Handling
```cpp
// Graceful fallback if ThreadSystem unavailable
if (!Forge::ThreadSystem::Exists()) {
    EventManager::Instance().enableThreading(false);
    std::cout << "ThreadSystem not available, using single-threaded mode" << std::endl;
}
```

## Integration Examples

### Game State Integration
```cpp
class GameState {
    void init() {
        // Initialize threading first
        Forge::ThreadSystem::Instance().init();
        EventManager::Instance().init();

        // Configure for game scale
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(getExpectedEventCount() / 2);
    }

    void update() {
        // Single call processes all events efficiently
        EventManager::Instance().update();
    }

private:
    size_t getExpectedEventCount() {
        // Return expected event count based on game scale
        return 100; // Medium game
    }
};
```

### Performance Profiling
```cpp
void profileEventPerformance() {
    // Create test events
    for (int i = 0; i < 100; ++i) {
        EventManager::Instance().createWeatherEvent("test_" + std::to_string(i), "Rainy");
    }

    // Test single-threaded
    EventManager::Instance().enableThreading(false);
    auto start = std::chrono::high_resolution_clock::now();
    EventManager::Instance().update();
    auto singleThreadTime = std::chrono::high_resolution_clock::now() - start;

    // Test multi-threaded
    EventManager::Instance().enableThreading(true);
    start = std::chrono::high_resolution_clock::now();
    EventManager::Instance().update();
    auto multiThreadTime = std::chrono::high_resolution_clock::now() - start;

    // Compare results
    auto singleMs = std::chrono::duration_cast<std::chrono::microseconds>(singleThreadTime).count() / 1000.0;
    auto multiMs = std::chrono::duration_cast<std::chrono::microseconds>(multiThreadTime).count() / 1000.0;

    std::cout << "Single-threaded: " << singleMs << "ms" << std::endl;
    std::cout << "Multi-threaded: " << multiMs << "ms" << std::endl;
    std::cout << "Speedup: " << (singleMs / multiMs) << "x" << std::endl;
}
```

## Shutdown and Cleanup

### Proper Shutdown Sequence
```cpp
void cleanupEventSystems() {
    // Disable threading first
    EventManager::Instance().enableThreading(false);

    // Clean up EventManager
    EventManager::Instance().clean();

    // ThreadSystem cleanup handled by application
    // Forge::ThreadSystem::Instance().clean();
}
```

### Debug Mode Features
```cpp
#ifdef EVENT_DEBUG_LOGGING
    // Additional threading debug information
    std::cout << "Threading enabled: " << EventManager::Instance().isThreadingEnabled() << std::endl;
    std::cout << "Threading threshold: " << threadingThreshold << std::endl;
    std::cout << "ThreadSystem available: " << Forge::ThreadSystem::Exists() << std::endl;
#endif
```

## Troubleshooting

### Common Issues
1. **ThreadSystem Not Initialized**: Always init ThreadSystem before EventManager
2. **Threading Overhead**: Lower threshold if performance decreases with threading
3. **Memory Issues**: Call `compactEventStorage()` if memory usage grows
4. **Performance Regression**: Profile before/after enabling threading

### Performance Validation
```cpp
// Validate threading is beneficial
void validateThreadingPerformance() {
    size_t eventCount = EventManager::Instance().getEventCount();
    auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);

    if (stats.avgTime > 1.0 && eventCount > 50) {
        std::cout << "Consider lowering threading threshold" << std::endl;
    } else if (stats.avgTime < 0.1 && eventCount < 30) {
        std::cout << "Consider raising threading threshold or disabling" << std::endl;
    }
}
```

This integration provides optimal performance for real-world game scenarios while maintaining the simplicity and reliability needed for production game development.
