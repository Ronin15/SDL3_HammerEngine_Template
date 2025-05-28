# EventManager Documentation

## Overview

The EventManager system provides a powerful, thread-safe way to manage game events such as weather changes, scene transitions, and NPC spawning. It follows a condition-based execution model where events are triggered when specific conditions are met. The EventManager integrates with the ThreadSystem component for efficient multi-threaded event processing with priority-based scheduling.

## Key Components

### Core Classes

- **EventManager**: Central singleton that manages all events and integrates with ThreadSystem
- **Event**: Abstract base class for all event types
- **EventSystem**: High-level integration with other game systems
- **EventFactory**: Factory for creating different types of events
- **ThreadSystem**: Provides thread pooling and task management for parallel event processing

### Event Types

The system includes several built-in event types:

1. **WeatherEvent**: Controls weather changes with transition effects
2. **SceneChangeEvent**: Handles scene/level transitions
3. **NPCSpawnEvent**: Manages NPC spawning based on various conditions

## Getting Started

### Initialization

```cpp
// Initialize the event manager
EventManager::Instance().init();

// Register default events (optional)
EventManager::Instance().registerDefaultEvents();
```

### Creating Events

```cpp
// Using EventFactory (recommended)
auto rainEvent = EventFactory::Instance().createWeatherEvent("HeavyRain", "Rainy", 0.8f);
EventManager::Instance().registerEvent("HeavyRain", rainEvent);

// Or manually
auto sceneEvent = std::make_shared<SceneChangeEvent>("ToMainMenu", "MainMenu");
sceneEvent->setTransitionType(TransitionType::Fade);
EventManager::Instance().registerEvent("ToMainMenu", sceneEvent);
```

### Triggering Events Directly

```cpp
// Trigger weather change
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);

// Trigger scene change
EventManager::Instance().triggerSceneChange("MainMenu", "fade", 2.0f);

// Spawn NPC at position
EventManager::Instance().triggerNPCSpawn("Guard", 100.0f, 200.0f);
```

### Condition-Based Events

Events can be configured with conditions that determine when they trigger:

```cpp
// Create a weather event that only occurs at night
auto nightRain = EventFactory::Instance().createWeatherEvent("NightRain", "Rainy", 0.5f);
static_cast<WeatherEvent*>(nightRain.get())->setTimeOfDay(20.0f, 6.0f);  // 8 PM to 6 AM
EventManager::Instance().registerEvent("NightRain", nightRain);

// Create an NPC spawn event that triggers when player approaches
auto guardSpawn = EventFactory::Instance().createNPCSpawnEvent("GuardSpawn", "Guard", 2, 5.0f);
static_cast<NPCSpawnEvent*>(guardSpawn.get())->setProximityTrigger(50.0f);  // Trigger within 50 units
static_cast<NPCSpawnEvent*>(guardSpawn.get())->setSpawnArea(100.0f, 100.0f, 10.0f);  // Spawn area
EventManager::Instance().registerEvent("GuardSpawn", guardSpawn);
```

### Update Loop

Ensure that the event system is updated each frame:

```cpp
// In your game loop
void update(float deltaTime) {
    // Update other systems...
    
    // Update event manager
    EventManager::Instance().update();
}
```

## Advanced Usage

### Creating Event Sequences

```cpp
// Create a weather sequence: Rain -> Storm -> Clear
std::vector<EventDefinition> weatherSequence = {
    {"Weather", "StartRain", {{"weatherType", "Rainy"}}, {{"intensity", 0.5f}}, {}},
    {"Weather", "Thunderstorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.9f}}, {}},
    {"Weather", "ClearSkies", {{"weatherType", "Clear"}}, {{"transitionTime", 8.0f}}, {}}
};

auto events = EventFactory::Instance().createEventSequence("WeatherSequence", weatherSequence, true);

// Register all events in the sequence
for (auto& event : events) {
    EventManager::Instance().registerEvent(event->getName(), event);
}
```

### Event Communication

Events can communicate with each other through messages:

```cpp
// Send message to specific event
EventManager::Instance().sendMessageToEvent("RainEvent", "intensify");

// Send message immediately (bypass queue)
EventManager::Instance().sendMessageToEvent("RainEvent", "intensify", true);

// Send message to all events of a type
EventManager::Instance().broadcastMessageToType("Weather", "stop");

// Send message to all events
EventManager::Instance().broadcastMessage("reset");

// Send immediate broadcast (useful for critical events)
EventManager::Instance().broadcastMessage("emergency_stop", true);
```

### Message Queuing System

The EventManager uses a sophisticated message queuing system that supports both immediate and queued message delivery:

```cpp
// Queued messages (default) - processed during next update cycle
EventManager::Instance().sendMessageToEvent("RainEvent", "start");
EventManager::Instance().broadcastMessageToType("Weather", "pause");
EventManager::Instance().broadcastMessage("save_state");

// Immediate messages - delivered synchronously, bypassing the queue
EventManager::Instance().sendMessageToEvent("CriticalEvent", "abort", true);
EventManager::Instance().broadcastMessageToType("Combat", "emergency_stop", true);
EventManager::Instance().broadcastMessage("system_shutdown", true);

// Manual queue processing (automatically called during update)
EventManager::Instance().processMessageQueue();
```

**Key Features:**
- **Thread-Safe Double Buffering**: Messages are queued in one buffer while another is being processed
- **Timestamp Tracking**: All queued messages include high-precision timestamps
- **Priority Handling**: Immediate messages bypass the queue for critical scenarios
- **Automatic Processing**: Message queue is processed automatically during each update cycle
- **Performance Monitoring**: Built-in performance statistics for message processing

**When to Use Immediate vs Queued:**
- **Queued (default)**: Standard game events, UI updates, non-critical notifications
- **Immediate**: Emergency stops, critical state changes, system shutdowns, debug commands

## Performance Monitoring

The EventManager includes comprehensive performance monitoring capabilities for optimization and debugging:

### Built-in Performance Statistics

```cpp
// Performance data is automatically collected for:
// - Individual event execution times
// - Event type batch processing times
// - Message queue processing performance
// - Cache hit/miss ratios for optimization systems

// Access event count metrics
size_t totalEvents = EventManager::Instance().getEventCount();
size_t activeEvents = EventManager::Instance().getActiveEventCount();

// Performance data is logged in debug builds and available for profiling
```

**Tracked Metrics:**
- **Total Update Time**: Cumulative time spent processing events
- **Average Update Time**: Mean execution time per update cycle
- **Max/Min Update Times**: Performance bounds for optimization analysis
- **Update Count**: Total number of update cycles processed
- **Event Type Performance**: Per-type execution statistics for batch optimization
- **Message Queue Stats**: Processing time and throughput metrics

### Performance Optimization Features

**Event Caching System:**
- Active events are cached for faster iteration
- Event type batches are pre-computed for efficient parallel processing
- String views are used to avoid unnecessary string copies
- Cache invalidation occurs only when events are added/removed

**Threading Optimizations:**
- Events are grouped by type into batches for better cache locality
- ThreadSystem integration provides parallel processing with priority scheduling
- Automatic fallback to single-threaded mode if ThreadSystem is unavailable
- Thread-safe double buffering for message queues reduces contention

**Memory Management:**
- Flat maps used for O(log n) lookup performance
- Weak pointers prevent memory leaks in event references
- High-precision timing using nanosecond resolution
- Minimal memory allocations during runtime execution

## Utility Methods and Event Management

### Event Query and Management

```cpp
// Check if specific events exist
bool hasWeatherEvent = EventManager::Instance().hasEvent("RainStorm");

// Get specific events for direct manipulation
auto weatherEvent = EventManager::Instance().getEvent("RainStorm");
if (weatherEvent) {
    weatherEvent->setActive(true);
}

// Get all events of a specific type
auto allWeatherEvents = EventManager::Instance().getEventsByType("Weather");
for (auto& event : allWeatherEvents) {
    event->setActive(false);  // Disable all weather events
}

// Get event counts for monitoring
size_t totalEvents = EventManager::Instance().getEventCount();
size_t activeEvents = EventManager::Instance().getActiveEventCount();
```

### Event State Management

```cpp
// Activate/deactivate specific events
EventManager::Instance().setEventActive("RainStorm", true);
bool isActive = EventManager::Instance().isEventActive("RainStorm");

// Remove events that are no longer needed
EventManager::Instance().removeEvent("TemporaryEvent");

// Reset all events without shutting down the manager
// Useful for scene transitions or game state changes
EventManager::Instance().resetEvents();

// Force execute events regardless of conditions
EventManager::Instance().executeEvent("EmergencyEvent");
int executed = EventManager::Instance().executeEventsByType("Combat");
```

### Bulk Event Operations

```cpp
// Execute all events of a specific type
int weatherEventsExecuted = EventManager::Instance().executeEventsByType("Weather");

// Send messages to multiple events efficiently
EventManager::Instance().broadcastMessageToType("Combat", "end_combat");

// Reset all events while keeping the manager running
EventManager::Instance().resetEvents();  // Clears events but keeps system initialized
```

**Key Methods:**
- `hasEvent(name)`: Check if an event exists
- `getEvent(name)`: Retrieve a specific event for direct access
- `getEventsByType(type)`: Get all events of a particular type
- `resetEvents()`: Clear all events while keeping the manager initialized
- `executeEventsByType(type)`: Force execute all events of a type
- `getEventCount()` / `getActiveEventCount()`: Monitor event system usage

### Custom Event Types

You can create custom event types by inheriting from the Event base class:

```cpp
class CustomEvent : public Event {
public:
    CustomEvent(const std::string& name) : m_name(name) {}
    
    void update() override { /* Implementation */ }
    void execute() override { /* Implementation */ }
    void reset() override { /* Implementation */ }
    void clean() override { /* Implementation */ }
    
    std::string getName() const override { return m_name; }
    std::string getType() const override { return "Custom"; }
    
    bool checkConditions() override { /* Implementation */ }

private:
    std::string m_name;
};
```

Register a custom event creator with the EventFactory:

```cpp
EventFactory::Instance().registerCustomEventCreator("Custom", [](const EventDefinition& def) {
    auto event = std::make_shared<CustomEvent>(def.name);
    // Configure event based on definition
    return event;
});
```

## Integration with Other Systems

The EventSystem class provides integration with other game systems:

```cpp
// Register event handlers for system events
EventManager::Instance().registerEventHandler("WeatherChange", [](const std::string& params) {
    // Update particle systems, lighting, etc.
});

EventManager::Instance().registerEventHandler("SceneChange", [](const std::string& params) {
    // Notify the GameStateManager
});

EventManager::Instance().registerEventHandler("NPCSpawn", [](const std::string& params) {
    // Create entities through EntityFactory
});
```

## Performance Considerations

- Use appropriate update frequencies for events that don't need to be checked every frame
- Consider batching similar event types together for better cache locality
- For large numbers of events, the EventManager uses ThreadSystem for parallel processing
- The EventManager automatically optimizes thread usage based on available hardware
- Use batch processing for optimal cache utilization and thread efficiency
- Assign appropriate task priorities for different event types:
  - Use `Critical` for vital game progression events
  - Use `High` for player-facing and immediate response events
  - Use `Normal` for standard game events (default)
  - Use `Low` for background or cosmetic events
  - Use `Idle` for debugging or non-essential events

## Thread Safety

The EventManager is designed to be thread-safe and integrates with the ThreadSystem for efficient multi-threaded event processing:

```cpp
// Initialize ThreadSystem first
Forge::ThreadSystem::Instance().init();

// Then initialize EventManager
EventManager::Instance().init();

// Enable multi-threaded event processing with ThreadSystem
EventManager::Instance().configureThreading(true, 4);

// Enable multi-threaded event processing with specific priority
EventManager::Instance().configureThreading(true, 4, Forge::TaskPriority::High);
```

The EventManager uses ThreadSystem internally to:
- Process event batches in parallel
- Ensure proper synchronization of shared resources
- Optimize thread usage based on system capabilities
- Handle task scheduling and completion with priority-based execution
- Manage error handling and recovery for failed tasks

See [ThreadSystem Documentation](ThreadSystem.md) for more details on the underlying thread pool implementation and [EventManager_ThreadSystem.md](EventManager_ThreadSystem.md) for the specific integration details.

## Best Practices

1. **Use Factory Methods**: Prefer using the EventFactory to create events
2. **Event Naming**: Use consistent naming conventions for events
3. **Cleanup**: Make sure to clean up events when no longer needed
4. **Conditions**: Keep event conditions simple and efficient
5. **Cooldowns**: Use cooldowns to prevent events from triggering too frequently
6. **One-Time Events**: For story events, set them as one-time events using `setOneTime(true)`
7. **ThreadSystem Integration**: Configure threading early in application initialization
8. **Graceful Shutdown**: Ensure proper cleanup by disabling threading before shutting down

## Debug Tips

To debug event issues:

1. Check if the event is active (`isEventActive`)
2. Verify that conditions are being met (`checkConditions`)
3. Use `executeEvent` to force an event to trigger regardless of conditions
4. Inspect the number of active events (`getActiveEventCount`)
5. For threading issues, temporarily disable threading with `configureThreading(false)`
6. Add timeouts when waiting for event completion in time-sensitive scenarios
7. Check if ThreadSystem is properly initialized with `Forge::ThreadSystem::Exists()`

## ThreadSystem Integration

The EventManager leverages the ThreadSystem component for efficient parallel processing:

```cpp
// Initialize the ThreadSystem first
Forge::ThreadSystem::Instance().init();

// Initialize the EventManager with ThreadSystem support
EventManager::Instance().init();

// Enable threading with a specific number of concurrent tasks
EventManager::Instance().configureThreading(true, 4);

// Enable threading with specific priority
EventManager::Instance().configureThreading(true, 4, Forge::TaskPriority::High);

// Disable threading for debugging or shutdown
EventManager::Instance().configureThreading(false);

// The EventManager automatically checks if ThreadSystem exists
// and falls back to single-threaded mode if necessary
```

When threading is enabled, the EventManager:
1. Groups events by type into batches for better cache locality
2. Submits batches as tasks to the ThreadSystem thread pool with appropriate priorities
3. Waits for all tasks to complete with proper error handling
4. Provides detailed performance metrics in debug mode

Available priority levels (from highest to lowest):
- `Forge::TaskPriority::Critical` (0): For mission-critical events
- `Forge::TaskPriority::High` (1): For important events needing quick responses
- `Forge::TaskPriority::Normal` (2): Default for standard events
- `Forge::TaskPriority::Low` (3): For background events
- `Forge::TaskPriority::Idle` (4): For non-essential events

See [ThreadSystem_API.md](ThreadSystem_API.md) for details on the underlying thread pool API and [EventManager_ThreadSystem.md](EventManager_ThreadSystem.md) for more details on this integration.