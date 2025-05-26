# EventManager and ThreadSystem Integration Guide

## Overview

This document details how the EventManager integrates with the ThreadSystem component to provide efficient, thread-safe event processing in the Forge Game Engine. The integration enables parallel processing of game events while maintaining a clean API and proper thread synchronization, with support for priority-based task scheduling.

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
- **Priority-Based Execution**: Events can be processed based on their importance using task priorities

## Integration Points

The EventManager integrates with ThreadSystem in several key areas:

1. **Initialization**: EventManager checks for ThreadSystem availability during initialization
2. **Threading Configuration**: Uses ThreadSystem for all thread management, including priority settings
3. **Batch Processing**: Submits event batches as tasks to ThreadSystem with appropriate priorities
4. **Resource Management**: Proper cleanup and waiting for task completion
5. **Priority Management**: Assigns and manages task priorities for different event types

## Initialization Sequence

```cpp
// Initialize ThreadSystem first
Forge::ThreadSystem::Instance().init();

// Then initialize EventManager
EventManager::Instance().init();

// Configure EventManager to use threading with default priority
EventManager::Instance().configureThreading(true);

// Or configure with specific priority
EventManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);
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
    // Process events in parallel through ThreadSystem with appropriate priorities
    for (const auto& [eventType, batch] : m_eventTypeBatches) {
        Forge::TaskPriority priority = getEventTypePriority(eventType);
        auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
            [this, eventType, batch]() {
                updateEventTypeBatch(eventType, batch);
            },
            priority
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

### Thread Count and Priority Control

```cpp
// Use default thread count (hardware-based) with normal priority
EventManager::Instance().configureThreading(true, 0);

// Use specific number of concurrent tasks with normal priority
EventManager::Instance().configureThreading(true, 4);

// Use default thread count with high priority
EventManager::Instance().configureThreading(true, 0, Forge::TaskPriority::High);

// Use specific number of concurrent tasks with high priority
EventManager::Instance().configureThreading(true, 4, Forge::TaskPriority::High);

// Disable threading
EventManager::Instance().configureThreading(false);
```

### Priority Levels

The EventManager supports the following priority levels for event processing:

```cpp
// Available priorities (from highest to lowest)
Forge::TaskPriority::Critical  // For mission-critical events (0)
Forge::TaskPriority::High      // For important events needing quick responses (1)
Forge::TaskPriority::Normal    // Default for standard events (2)
Forge::TaskPriority::Low       // For background events (3)
Forge::TaskPriority::Idle      // For non-essential events (4)
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
- **Task Priorities**: Critical and high-priority events are processed before lower-priority ones
- **Priority Balance**: Too many high-priority tasks can starve lower-priority tasks of CPU time

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
7. **Appropriate Priorities**: Use task priorities appropriately to balance system responsiveness:
   - `Critical`: Only for vital game progression events
   - `High`: For player-facing and immediate response events
   - `Normal`: For standard game events (default)
   - `Low`: For background or cosmetic events
   - `Idle`: For debugging or non-essential events
8. **Priority Balance**: Avoid creating too many high-priority tasks that could starve lower-priority events

## Compatibility Notes

- ThreadSystem is available on all platforms supported by the engine
- The EventManager will fall back to single-threaded mode on platforms with limited threading support
- Event handlers should avoid platform-specific code that might not be thread-safe

## Related Documentation

- [ThreadSystem.md](ThreadSystem.md) - Core documentation for ThreadSystem
- [ThreadSystem_API.md](ThreadSystem_API.md) - Detailed API reference
- [EventManager.md](EventManager.md) - Main EventManager documentation
- [QueueCapacity_Optimization.md](QueueCapacity_Optimization.md) - Memory optimization details
- [ThreadSystem_Optimization.md](ThreadSystem_Optimization.md) - Task optimization details