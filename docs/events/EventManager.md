# EventManager Documentation

## Overview
The EventManager is a high-performance, thread-safe event system designed for real-time games. It uses data-oriented design principles and batch processing to achieve optimal performance while maintaining ease of use.

## Key Features

- **High Performance**: 24,000-95,000 events/second depending on game scale
- **Realistic Scale**: Optimized for 10-500 events (typical game scenarios)
- **Thread-Safe**: Full thread safety with minimal locking overhead
- **Batch Processing**: AIManager-style batch updates for maximum efficiency
- **Smart Memory Management**: Event pooling and cache-friendly data structures
- **Type-Safe API**: Strongly typed event system with compile-time safety

## Performance Characteristics

### Realistic Event Counts by Game Size
- **Small Games**: 10-50 events (82,000-95,000 events/second)
- **Medium Games**: 50-100 events (54,000-58,000 events/second)
- **Large Games**: 100-200 events (24,000-50,000 events/second)
- **Maximum Scale**: ~500 events (still performant)

### Threading Implementation
- Proper batch processing (not individual tasks per event)
- ThreadSystem integration with configurable thresholds
- Automatic load balancing across available cores

## Getting Started

### Initialization
```cpp
// Initialize ThreadSystem first (required for threading)
Forge::ThreadSystem::Instance().init();

// Initialize EventManager
if (!EventManager::Instance().init()) {
    std::cerr << "Failed to initialize EventManager!" << std::endl;
}

// Optional: Configure threading
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(100); // Thread if >100 events
```

### Creating Events - New Convenience Methods (Recommended)

The new API provides one-line event creation and registration:

```cpp
// Weather events - create and register in one call
EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.5f, 3.0f);
EventManager::Instance().createWeatherEvent("HeavyStorm", "Stormy", 0.9f, 2.0f);

// Scene change events - create and register in one call  
EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
EventManager::Instance().createSceneChangeEvent("ToShop", "ShopScene", "slide", 2.0f);

// NPC spawn events - create and register in one call
EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 25.0f);
EventManager::Instance().createNPCSpawnEvent("VillagerGroup", "Villager", 5, 40.0f);
```

### Direct Event Triggering (High-Level API)

For immediate event execution without pre-registration:

```cpp
// Direct weather changes
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
EventManager::Instance().changeWeather("Stormy", 1.5f); // Alternative name

// Direct scene transitions
EventManager::Instance().triggerSceneChange("BattleScene", "fade", 2.0f);
EventManager::Instance().changeScene("MainMenu", "dissolve", 1.0f); // Alternative name

// Direct NPC spawning
EventManager::Instance().triggerNPCSpawn("Merchant", 100.0f, 200.0f);
EventManager::Instance().spawnNPC("Guard", 250.0f, 150.0f); // Alternative name
```

### Event Handlers

Register handlers for batch processing:

```cpp
// Register handlers by event type for optimal performance
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) {
        // Handle weather events
        std::cout << "Weather changed!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
    [](const EventData& data) {
        // Handle NPC spawn events
        std::cout << "NPC spawned!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::SceneChange,
    [](const EventData& data) {
        // Handle scene changes
        std::cout << "Scene changed!" << std::endl;
    });
```

### Update Loop

The EventManager uses batch processing for maximum performance:

```cpp
void GameLoop::update() {
    // Single call processes all events efficiently
    EventManager::Instance().update();
    
    // The update() method internally calls:
    // - updateWeatherEvents()    (batch processes weather events)
    // - updateSceneChangeEvents() (batch processes scene events)  
    // - updateNPCSpawnEvents()   (batch processes NPC events)
    // - updateCustomEvents()     (batch processes custom events)
}
```

## Advanced Usage

### Performance Monitoring

```cpp
// Get performance statistics by event type
auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Weather events: " << weatherStats.avgTime << "ms average" << std::endl;
std::cout << "Processed: " << weatherStats.callCount << " events" << std::endl;

// Get event counts
size_t totalEvents = EventManager::Instance().getEventCount();
size_t weatherEvents = EventManager::Instance().getEventCount(EventTypeId::Weather);
std::cout << "Total events: " << totalEvents << ", Weather: " << weatherEvents << std::endl;

// Reset performance tracking
EventManager::Instance().resetPerformanceStats();
```

### Event Management

```cpp
// Query events
bool hasEvent = EventManager::Instance().hasEvent("MyEvent");
auto event = EventManager::Instance().getEvent("MyEvent");
auto weatherEvents = EventManager::Instance().getEventsByType(EventTypeId::Weather);

// Control event state
EventManager::Instance().setEventActive("MyEvent", false);
bool isActive = EventManager::Instance().isEventActive("MyEvent");

// Remove events
EventManager::Instance().removeEvent("MyEvent");
```

### Memory Management

```cpp
// Optimize memory usage periodically
EventManager::Instance().compactEventStorage();
EventManager::Instance().clearEventPools(); // Only call during cleanup
```

## Event Types

### EventTypeId Enumeration
```cpp
enum class EventTypeId : uint8_t {
    Weather = 0,      // Weather system events
    SceneChange = 1,  // Scene transition events
    NPCSpawn = 2,     // NPC creation events
    Custom = 3,       // User-defined events
    COUNT = 4         // Total count (do not use)
};
```

### Event Data Structure
```cpp
struct EventData {
    EventPtr event;           // Smart pointer to actual event
    EventTypeId typeId;       // Type for fast dispatch
    uint32_t flags;           // Active, dirty, etc.
    float lastUpdateTime;     // For delta time calculations
    uint32_t priority;        // Processing priority
    
    // Helper methods
    bool isActive() const;
    void setActive(bool active);
    bool isDirty() const;
    void setDirty(bool dirty);
};
```

## Threading Configuration

### Basic Threading Setup
```cpp
// Enable threading with default settings
EventManager::Instance().enableThreading(true);

// Configure when to use threading (events count threshold)
EventManager::Instance().setThreadingThreshold(50); // Use threads if >50 events

// Check threading status
bool usingThreads = EventManager::Instance().isThreadingEnabled();
```

### Threading Best Practices
1. **Initialize ThreadSystem first** - Always call `ThreadSystem::Instance().init()` before EventManager
2. **Set appropriate thresholds** - Use threading for 50+ events, single-threaded for smaller counts
3. **Monitor performance** - Use performance stats to find optimal threading threshold
4. **Graceful shutdown** - Disable threading before cleanup: `enableThreading(false)`

## Performance Best Practices

### Optimal Event Counts
1. **Keep realistic scales**: 10-500 events total
2. **Batch similar operations**: Use convenience methods for multiple events
3. **Avoid event spam**: Don't create thousands of events unnecessarily
4. **Monitor performance**: Use built-in stats to track performance

### Memory Efficiency
1. **Use event pools**: Events are automatically pooled and reused
2. **Compact storage**: Call `compactEventStorage()` periodically
3. **Smart pointers**: Events use shared_ptr for automatic memory management
4. **Type-indexed storage**: Events stored by type for cache efficiency

### Threading Optimization
1. **Batch processing**: Events processed in batches, not individually
2. **Configurable thresholds**: Threading only when beneficial
3. **Minimal locking**: Lock-free operations where possible
4. **Work verification**: All operations properly verified for correctness

## Debug and Monitoring

### Debug Logging
```cpp
// Enable debug logging (compile-time flag)
#define EVENT_DEBUG_LOGGING
// Provides detailed logging of event operations
```

### Performance Monitoring
```cpp
// Monitor event processing performance
auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Min: " << stats.minTime << "ms, Max: " << stats.maxTime << "ms" << std::endl;
std::cout << "Average: " << stats.avgTime << "ms over " << stats.callCount << " calls" << std::endl;

// Monitor event counts by type
for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
    auto typeId = static_cast<EventTypeId>(i);
    size_t count = EventManager::Instance().getEventCount(typeId);
    std::cout << "Type " << i << ": " << count << " events" << std::endl;
}
```

## Integration with Game Systems

### With AI System
```cpp
// EventManager and AIManager work together efficiently
void GameState::update() {
    EventManager::Instance().update(); // Process events first
    AIManager::Instance().update();    // Then update AI behaviors
}
```

### With Save System
```cpp
// Events can trigger save operations
EventManager::Instance().registerHandler(EventTypeId::Custom,
    [](const EventData& data) {
        // Trigger autosave on important events
        SaveManager::Instance().autoSave();
    });
```

## Error Handling

### Common Issues and Solutions

1. **Event creation fails**
   ```cpp
   if (!EventManager::Instance().createWeatherEvent("Test", "Rainy")) {
       std::cerr << "Failed to create weather event" << std::endl;
   }
   ```

2. **Threading issues**
   ```cpp
   // Ensure ThreadSystem is initialized first
   if (!Forge::ThreadSystem::Exists()) {
       Forge::ThreadSystem::Instance().init();
   }
   ```

3. **Performance problems**
   ```cpp
   // Check if you have too many events
   size_t eventCount = EventManager::Instance().getEventCount();
   if (eventCount > 1000) {
       std::cout << "Warning: High event count may impact performance" << std::endl;
   }
   ```

## Migration from Old API

### Old vs New Patterns

#### Creating Events
```cpp
// OLD WAY (still supported but not recommended)
auto event = EventFactory::Instance().createWeatherEvent("Rain", "Rainy", 0.8f);
EventManager::Instance().registerEvent("Rain", event);

// NEW WAY (recommended)
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
```

#### Triggering Events
```cpp
// OLD WAY (manual event lookup and execution)
auto event = EventManager::Instance().getEvent("WeatherChange");
if (event) {
    event->execute();
}

// NEW WAY (direct convenience methods)
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
```

## Example: Complete Weather System

```cpp
class WeatherManager {
private:
    std::vector<std::string> m_weatherTypes = {"Clear", "Cloudy", "Rainy", "Stormy"};
    size_t m_currentIndex = 0;

public:
    void init() {
        // Create weather events for each type
        for (const auto& weather : m_weatherTypes) {
            EventManager::Instance().createWeatherEvent(
                "weather_" + weather, weather, 1.0f, 2.0f);
        }
        
        // Register handler for weather changes
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) { onWeatherChanged(data); });
    }
    
    void cycleWeather() {
        std::string nextWeather = m_weatherTypes[m_currentIndex];
        m_currentIndex = (m_currentIndex + 1) % m_weatherTypes.size();
        
        // Use convenience method for immediate weather change
        EventManager::Instance().triggerWeatherChange(nextWeather, 3.0f);
    }
    
private:
    void onWeatherChanged(const EventData& data) {
        std::cout << "Weather system responding to change" << std::endl;
        // Update weather-dependent game systems
    }
};
```

This documentation reflects the optimized EventManager's realistic performance characteristics and modern convenience API, providing both ease of use and maximum performance for real-world game development scenarios.