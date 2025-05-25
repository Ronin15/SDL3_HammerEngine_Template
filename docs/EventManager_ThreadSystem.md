# EventManager and ThreadSystem Integration Guide

## Overview

This document details how the EventManager integrates with the ThreadSystem component to provide efficient, thread-safe event processing in the Forge Game Engine. The integration enables parallel processing of game events while maintaining a clean API and proper thread synchronization.

## Architecture

The EventManager uses ThreadSystem as its underlying threading mechanism, creating a layered architecture:

```
┌─────────────────┐
│    Game Logic   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   EventManager  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   ThreadSystem  │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  System Threads │
└─────────────────┘
```

## Key Benefits

- **Simplified API**: Game code interacts only with the EventManager, which handles all thread management details
- **Consistent Threading Model**: All components use the same ThreadSystem for concurrent operations
- **Resource Efficiency**: Shared thread pool reduces overhead compared to each system creating its own threads
- **Optimized Performance**: EventManager batches similar events for better cache locality
- **Robust Error Handling**: Comprehensive timeout and error recovery mechanisms

## Integration Points

The EventManager integrates with ThreadSystem in several key areas:

1. **Initialization**: EventManager checks for ThreadSystem availability during initialization
2. **Threading Configuration**: Uses ThreadSystem for all thread management
3. **Batch Processing**: Submits event batches as tasks to ThreadSystem
4. **Resource Management**: Proper cleanup and waiting for task completion

## Initialization Sequence

```cpp
// Initialize ThreadSystem first
Forge::ThreadSystem::Instance().init();

// Then initialize EventManager
EventManager::Instance().init();

// Configure EventManager to use threading
EventManager::Instance().configureThreading(true);
```

## Batch Processing Implementation

The EventManager organizes events into type-based batches for efficient processing:

1. Events of the same type are grouped together
2. Each batch is submitted to ThreadSystem as a separate task
3. Tasks are processed in parallel on the thread pool
4. The EventManager waits for all tasks to complete before continuing

```cpp
// From EventManager::update()
if (m_useThreading.load() && Forge::ThreadSystem::Exists()) {
    // Process events in parallel through ThreadSystem
    for (const auto& [eventType, batch] : m_eventTypeBatches) {
        auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
            [this, eventType, batch]() {
                updateEventTypeBatch(eventType, batch);
            }
        );
        futures.push_back(std::move(future));
    }
    
    // Wait for all futures with proper timeout handling
    for (auto& future : futures) {
        auto status = future.wait_for(std::chrono::seconds(1));
        // Handle task completion or timeout
    }
}
```

## Configuration Options

### Thread Count Control

```cpp
// Use default thread count (hardware-based)
EventManager::Instance().configureThreading(true, 0);

// Use specific number of concurrent tasks
EventManager::Instance().configureThreading(true, 4);

// Disable threading
EventManager::Instance().configureThreading(false);
```

### Queue Capacity Management

The EventManager optimizes ThreadSystem's task queue capacity:

```cpp
// Reserve capacity for batched event processing
Forge::ThreadSystem::Instance().reserveQueueCapacity(
    std::min(static_cast<size_t>(batchCount * 2), 
             static_cast<size_t>(1024))
);
```

## Error Handling and Recovery

The EventManager implements robust error handling for ThreadSystem operations:

1. **Initialization Fallback**: Falls back to single-threaded mode if ThreadSystem isn't available
2. **Task Submission Failures**: Catches exceptions and processes tasks on the main thread
3. **Task Execution Errors**: Catches and logs exceptions without crashing
4. **Task Timeouts**: Implements timeouts to prevent hanging on long-running tasks

```cpp
try {
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(...);
    auto status = future.wait_for(std::chrono::seconds(1));
    if (status != std::future_status::ready) {
        // Handle timeout
    }
} catch (const std::exception& e) {
    // Log error and continue with fallback
}
```

## Shutdown Sequence

Proper shutdown is essential for clean operation:

```cpp
// First disable threading in EventManager
EventManager::Instance().configureThreading(false);

// Then clean up EventManager resources
EventManager::Instance().clean();

// Finally clean up ThreadSystem
// (usually done by the main application)
Forge::ThreadSystem::Instance().clean();
```

## Performance Considerations

- **Batch Size**: The EventManager automatically adjusts thread allocation based on batch size
- **Task Granularity**: Events are batched by type to ensure efficient task size
- **Thread Count**: The default configuration uses (hardware_concurrency - 1) threads
- **Debug Logging**: Performance metrics are available in debug builds

## Debug Tips

When troubleshooting ThreadSystem integration:

1. **Enable Verbose Logging**: Set `EVENT_DEBUG_LOGGING` to see detailed threading info
2. **Disable Threading**: Temporarily use `configureThreading(false)` to isolate threading issues
3. **Check ThreadSystem Status**: Use `Forge::ThreadSystem::Exists()` to verify availability
4. **Monitor Task Queue**: Check `Forge::ThreadSystem::Instance().getQueueSize()` for bottlenecks
5. **Add Timeouts**: Always use timeouts when waiting for tasks to complete

## Best Practices

1. **Initialize Early**: Set up ThreadSystem before other systems during application startup
2. **Thread Safety**: Ensure all event handlers are thread-safe if using threading
3. **Avoid Thread Blocking**: Never block threads with long-running operations
4. **Resource Sharing**: Use proper synchronization for resources shared between events
5. **Clean Shutdown**: Always disable threading before cleanup to prevent issues
6. **Selective Threading**: Use threading only for computationally intensive event processing

## Compatibility Notes

- ThreadSystem is available on all platforms supported by the engine
- The EventManager will fall back to single-threaded mode on platforms with limited threading support
- Event handlers should avoid platform-specific code that might not be thread-safe

## Related Documentation

- [ThreadSystem.md](ThreadSystem.md) - Core documentation for ThreadSystem
- [ThreadSystem_API.md](ThreadSystem_API.md) - Detailed API reference
- [EventManager.md](EventManager.md) - Main EventManager documentation
- [QueueCapacity_Optimization.md](QueueCapacity_Optimization.md) - Memory optimization details