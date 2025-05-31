# Forge Game Engine Documentation

## Overview

The Forge Game Engine is a high-performance game development framework built on SDL3, featuring advanced AI systems, event management, threading capabilities, and more.

## Core Systems

### AI System
The AI system provides flexible, thread-safe behavior management for game entities with individual behavior instances and mode-based configuration.

- **[AI System Overview](ai/README.md)** - Complete AI system documentation
- **[🔥 NEW: Behavior Modes](ai/BehaviorModes.md)** - PatrolBehavior and WanderBehavior mode-based system
- **[Behavior Modes Quick Reference](ai/BehaviorModes_QuickReference.md)** - Quick setup guide for behavior modes
- **[AIManager API](ai/AIManager.md)** - Complete API reference
- **[Batched Behavior Assignment](ai/BATCHED_BEHAVIOR_ASSIGNMENT.md)** - Global batched assignment system
- **[AI Optimizations](ai/OPTIMIZATIONS.md)** - Performance optimization techniques
- **[Entity Update Management](ai/EntityUpdateManagement.md)** - Entity update system details

### Event System
Robust event management system supporting weather events, NPC spawning, scene transitions, and custom events.

- **[Event Manager](EventManager.md)** - Core event management system
- **[🔥 NEW: EventManager Quick Reference](EventManager_QuickReference.md)** - Convenience methods guide
- **[Event Demo](EventDemo.md)** - Interactive event system demonstration
- **[Event System Integration](EventSystem_Integration.md)** - Integration guidelines
- **[Event Manager Performance](EventManager_Performance_Improvements.md)** - Performance optimizations
- **[Event Manager Threading](EventManager_ThreadSystem.md)** - Threading integration

### Threading System
High-performance multithreading framework with priority-based task scheduling.

- **[ThreadSystem Overview](ThreadSystem.md)** - Core threading system
- **[ThreadSystem API](ThreadSystem_API.md)** - Complete API reference
- **[ThreadSystem Optimization](ThreadSystem_Optimization.md)** - Performance tuning guide
- **[Queue Capacity Optimization](QueueCapacity_Optimization.md)** - Memory optimization techniques

## Quick Start Guides

### AI Behavior Setup
```cpp
// Register mode-based behaviors
auto patrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
AIManager::Instance().registerBehavior("RandomPatrol", std::move(patrol));

auto wander = std::make_unique<WanderBehavior>(WanderBehavior::WanderMode::LARGE_AREA, 2.5f);
AIManager::Instance().registerBehavior("LargeWander", std::move(wander));

// Assign to NPCs
AIManager::Instance().queueBehaviorAssignment(npc, "RandomPatrol");
```

### Event System Setup
```cpp
// Create and register events in one call (NEW convenience methods)
EventManager::Instance().createWeatherEvent("MorningRain", "Rainy", 0.8f);
EventManager::Instance().createSceneChangeEvent("ToMenu", "MainMenu", "fade");
EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 30.0f);

// Register event handlers
EventManager::Instance().registerEventHandler("Weather", 
    [](const std::string& message) { handleWeatherChange(message); });
```

### Threading Setup
```cpp
// Initialize threading with priority
ThreadSystem::Instance().init();
AIManager::Instance().configureThreading(true, 4, TaskPriority::High);
```

## New Features (Latest Updates)

### 🔥 EventManager Convenience Methods
- **One-Line Event Creation**: Create and register events with a single call
- **Complete Coverage**: Weather, Scene Change, and NPC Spawn events supported
- **50% Less Boilerplate**: Streamlined API for common use cases
- **Backward Compatible**: Traditional methods still fully supported
- **Built-in Error Handling**: Automatic validation and logging

### 🔥 AI Behavior Modes System
- **PatrolBehavior Modes**: FIXED_WAYPOINTS, RANDOM_AREA, CIRCULAR_AREA, EVENT_TARGET
- **WanderBehavior Modes**: SMALL_AREA, MEDIUM_AREA, LARGE_AREA, EVENT_TARGET
- **Automatic Configuration**: No manual setup required for common patterns
- **Clean Architecture**: Behavior logic stays in behavior classes

### Enhanced Performance
- **Linear AI Scaling**: Supports 5000+ NPCs efficiently
- **Priority-Based Threading**: Critical tasks get higher priority
- **Batched Operations**: Improved performance for bulk operations
- **Memory Optimizations**: Reduced overhead and better cache locality

## Architecture Highlights

### Individual Behavior Instances
Each NPC gets its own behavior instance, ensuring complete state isolation:
```cpp
AIManager::Instance().assignBehaviorToEntity(npc1, "Patrol"); // → Clone 1
AIManager::Instance().assignBehaviorToEntity(npc2, "Patrol"); // → Clone 2
// npc1 and npc2 have completely independent states
```

### Mode-Based Configuration
Behaviors automatically configure themselves based on selected modes:
```cpp
// Old way: Manual configuration
auto patrol = std::make_unique<PatrolBehavior>(waypoints, 2.0f);
patrol->setRandomPatrolArea(topLeft, bottomRight, 6);
patrol->setAutoRegenerate(true);

// New way: Mode-based
auto patrol = std::make_unique<PatrolBehavior>(PatrolBehavior::PatrolMode::RANDOM_AREA, 2.0f);
// All configuration handled automatically
```

### Thread-Safe Operations
All systems support concurrent access without race conditions:
```cpp
// Safe to call from any thread
AIManager::Instance().queueBehaviorAssignment(npc, "Wander");
EventManager::Instance().fireEvent("CustomEvent", "data");
ThreadSystem::Instance().submitTask(task, TaskPriority::Normal);
```

## Performance Characteristics

### AI System Scaling
| NPCs | Memory | CPU Usage | Performance |
|------|--------|-----------|-------------|
| 100  | ~150KB | Minimal   | Excellent   |
| 1000 | ~1.5MB | Low       | Excellent   |
| 5000 | ~2.5MB | Moderate  | Excellent   |

### Threading Performance
- **Automatic Scaling**: Uses available CPU cores efficiently
- **Priority Scheduling**: Critical tasks processed first
- **Low Overhead**: Minimal synchronization costs
- **Graceful Degradation**: Works well on single-core systems

## Best Practices

### AI Behavior Design
1. Use mode-based behaviors for common patterns
2. Implement proper `clone()` methods for custom behaviors
3. Use batched assignment for multiple NPCs
4. Set appropriate priorities for different behavior types

### Event System Usage
1. Register handlers once during initialization
2. Use immediate events sparingly (performance cost)
3. Batch related events when possible
4. Clean up handlers when components are destroyed

### Threading Guidelines
1. Submit CPU-intensive tasks to ThreadSystem
2. Use appropriate task priorities
3. Avoid blocking operations in worker threads
4. Test with both single and multi-core configurations

## Migration Guides

### From Manual to Mode-Based Behaviors
See [Behavior Modes Migration](ai/BehaviorModes.md#migration-from-manual-configuration)

### From Shared to Individual Behavior Instances
See [AI System Migration](ai/README.md#migration-from-v20)

## Troubleshooting

### Common Issues
- **[General Troubleshooting](TROUBLESHOOTING.md)** - Common problems and solutions
- **AI Performance Issues**: Check behavior priorities and update frequencies
- **Threading Problems**: Verify task submission and priority settings
- **Event System Issues**: Ensure handlers are registered before events fire

### Debug Tools
```cpp
// AI System diagnostics
size_t entityCount = AIManager::Instance().getManagedEntityCount();
size_t behaviorCount = AIManager::Instance().getBehaviorCount();

// Threading diagnostics
ThreadSystem::Instance().printStats();

// Event system diagnostics
EventManager::Instance().getEventCount();
```

## Contributing

When adding new behaviors or systems:
1. Follow the individual instance pattern for AI behaviors
2. Implement proper cleanup in destructors
3. Add comprehensive documentation
4. Include performance tests for scalability
5. Ensure thread safety for concurrent access

## Documentation Standards

- All public APIs must have documentation
- Include usage examples for complex features
- Performance characteristics should be documented
- Migration guides required for breaking changes
- Quick reference guides for commonly used features

---

For specific system details, see the individual documentation files linked above.