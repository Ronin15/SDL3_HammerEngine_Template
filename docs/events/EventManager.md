# EventManager Documentation

## Overview

The EventManager is a high-performance, thread-safe event system designed for real-time games. It uses data-oriented design principles and type-indexed storage to achieve optimal performance while maintaining ease of use. The system is built around strongly-typed event categories with batch processing capabilities similar to the AIManager.

## Key Features

- **High Performance**: Type-indexed storage with batch processing for maximum efficiency
- **Thread-Safe**: Full thread safety with shared_mutex and atomic operations
- **Memory Efficient**: Event pooling and cache-friendly data structures
- **Type-Safe API**: Strongly typed event system with compile-time safety
- **Flexible Integration**: Works seamlessly with EventFactory for event creation
- **Performance Monitoring**: Built-in statistics tracking for optimization

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Event Types](#event-types)
- [API Reference](#api-reference)
- [Performance Features](#performance-features)
- [Threading](#threading)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Setup

```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"

// Initialize ThreadSystem first (required for threading)
Forge::ThreadSystem::Instance().init();

// Initialize EventManager
if (!EventManager::Instance().init()) {
    std::cerr << "Failed to initialize EventManager!" << std::endl;
    return -1;
}

// Configure threading (optional)
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(100);
```

### Creating and Registering Events

#### Method 1: Convenience Methods (Recommended)
```cpp
// Create and register in one call using EventFactory integration
EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.5f, 3.0f);
EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 25.0f);
```

#### Method 2: Manual Creation and Registration
```cpp
// Create events manually and register them
auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);
EventManager::Instance().registerWeatherEvent("Rain", rainEvent);

auto sceneEvent = std::make_shared<SceneChangeEvent>("ToShop", "ShopScene");
EventManager::Instance().registerSceneChangeEvent("ToShop", sceneEvent);
```

### Event Handlers

```cpp
// Register type-safe handlers for batch processing
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) {
        std::cout << "Weather event triggered!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::SceneChange,
    [](const EventData& data) {
        std::cout << "Scene transition initiated!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
    [](const EventData& data) {
        std::cout << "NPC spawned!" << std::endl;
    });
```

### Direct Event Triggering

```cpp
// High-level API for immediate event execution
EventManager::Instance().changeWeather("Rainy", 3.0f);
EventManager::Instance().changeScene("BattleScene", "fade", 2.0f);
EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);

// Alternative method names
EventManager::Instance().triggerWeatherChange("Stormy", 1.5f);
EventManager::Instance().triggerSceneChange("MainMenu", "dissolve", 1.0f);
EventManager::Instance().triggerNPCSpawn("Guard", 250.0f, 150.0f);
```

### Update Loop Integration

```cpp
void GameLoop::update() {
    // Single call processes all events efficiently with batch processing
    EventManager::Instance().update();
}
```

## Architecture

### Design Principles

The EventManager follows data-oriented design principles for optimal performance:

1. **Type-Indexed Storage**: Events are stored by type for cache efficiency
2. **Batch Processing**: All events of the same type are processed together
3. **Memory Pooling**: Event objects are reused to minimize allocations
4. **Lock-Free Operations**: Atomic operations where possible, minimal locking
5. **Handler-Based System**: Type-safe event handlers for flexible response

### Core Data Structures

```cpp
// Type-indexed event storage (cache-friendly)
std::array<std::vector<EventData>, static_cast<size_t>(EventTypeId::COUNT)> m_eventsByType;

// Fast name-to-event mapping
std::unordered_map<std::string, size_t> m_nameToIndex;
std::unordered_map<std::string, EventTypeId> m_nameToType;

// Type-indexed handler storage
std::array<std::vector<FastEventHandler>, static_cast<size_t>(EventTypeId::COUNT)> m_handlersByType;

// Event pools for memory efficiency
EventPool<WeatherEvent> m_weatherPool;
EventPool<SceneChangeEvent> m_sceneChangePool;
EventPool<NPCSpawnEvent> m_npcSpawnPool;
```

### EventData Structure

```cpp
struct EventData {
    EventPtr event;                    // Smart pointer to event
    EventTypeId typeId;               // Type for fast dispatch
    uint32_t flags;                   // Active, dirty, etc.
    float lastUpdateTime;             // For delta time calculations
    uint32_t priority;                // For priority-based processing
    
    // Flag constants
    static constexpr uint32_t FLAG_ACTIVE = 1 << 0;
    static constexpr uint32_t FLAG_DIRTY = 1 << 1;
    static constexpr uint32_t FLAG_PENDING_REMOVAL = 1 << 2;
    
    // Helper methods
    bool isActive() const;
    void setActive(bool active);
    bool isDirty() const;
    void setDirty(bool dirty);
};
```

## Event Types

### EventTypeId Enumeration

```cpp
enum class EventTypeId : uint8_t {
    Weather = 0,      // Weather system events (rain, snow, fog, etc.)
    SceneChange = 1,  // Scene transition events (fade, dissolve, etc.)
    NPCSpawn = 2,     // NPC creation and spawning events
    Custom = 3,       // User-defined custom events
    COUNT = 4         // Total count (internal use only)
};
```

### Event Types Overview

| Type | Description | Common Use Cases |
|------|-------------|------------------|
| **Weather** | Environmental weather changes | Rain, snow, fog, storms, day/night cycles |
| **SceneChange** | Level/scene transitions | Menu navigation, level loading, cutscenes |
| **NPCSpawn** | Character spawning | Enemy waves, merchant appearances, patrol groups |
| **Custom** | User-defined events | Game-specific mechanics, special effects |

## API Reference

### Core Management

```cpp
// Initialization and cleanup
bool init();
void clean();
bool isShutdown() const;

// Main update loop
void update();
```

### Event Registration

```cpp
// Generic event registration
bool registerEvent(const std::string& name, EventPtr event);

// Type-specific registration
bool registerWeatherEvent(const std::string& name, std::shared_ptr<WeatherEvent> event);
bool registerSceneChangeEvent(const std::string& name, std::shared_ptr<SceneChangeEvent> event);
bool registerNPCSpawnEvent(const std::string& name, std::shared_ptr<NPCSpawnEvent> event);
```

### Event Creation (Convenience Methods)

```cpp
// Create and register in one call
bool createWeatherEvent(const std::string& name, const std::string& weatherType, 
                       float intensity = 1.0f, float transitionTime = 5.0f);
bool createSceneChangeEvent(const std::string& name, const std::string& targetScene, 
                           const std::string& transitionType = "fade", float transitionTime = 1.0f);
bool createNPCSpawnEvent(const std::string& name, const std::string& npcType, 
                        int count = 1, float spawnRadius = 0.0f);
```

### Event Access and Control

```cpp
// Event retrieval
EventPtr getEvent(const std::string& name) const;
std::vector<EventPtr> getEventsByType(EventTypeId typeId) const;
std::vector<EventPtr> getEventsByType(const std::string& typeName) const;

// Event state management
bool setEventActive(const std::string& name, bool active);
bool isEventActive(const std::string& name) const;
bool removeEvent(const std::string& name);
bool hasEvent(const std::string& name) const;
```

### Event Execution

```cpp
// Direct execution
bool executeEvent(const std::string& eventName) const;
int executeEventsByType(EventTypeId typeId) const;
int executeEventsByType(const std::string& eventType) const;
```

### Handler Management

```cpp
// Type-safe handler registration
void registerHandler(EventTypeId typeId, FastEventHandler handler);
void removeHandlers(EventTypeId typeId);
void clearAllHandlers();
size_t getHandlerCount(EventTypeId typeId) const;
```

### High-Level Trigger Methods

```cpp
// Direct event triggering (creates temporary events)
bool changeWeather(const std::string& weatherType, float transitionTime = 5.0f) const;
bool changeScene(const std::string& sceneId, const std::string& transitionType = "fade", 
                float transitionTime = 1.0f) const;
bool spawnNPC(const std::string& npcType, float x, float y) const;

// Alternative method names
bool triggerWeatherChange(const std::string& weatherType, float transitionTime = 5.0f) const;
bool triggerSceneChange(const std::string& sceneId, const std::string& transitionType = "fade", 
                       float transitionTime = 1.0f) const;
bool triggerNPCSpawn(const std::string& npcType, float x, float y) const;
```

## Performance Features

### Threading Configuration

```cpp
// Enable/disable threading
void enableThreading(bool enable);
bool isThreadingEnabled() const;

// Set threshold for when to use threading
void setThreadingThreshold(size_t threshold); // Default: 1000 events
```

### Performance Monitoring

```cpp
// Get performance statistics by event type
PerformanceStats getPerformanceStats(EventTypeId typeId) const;
void resetPerformanceStats();

// Event count tracking
size_t getEventCount() const;
size_t getEventCount(EventTypeId typeId) const;
```

### Memory Management

```cpp
// Optimize memory usage
void compactEventStorage();
void clearEventPools(); // Use only during cleanup
```

### PerformanceStats Structure

```cpp
struct PerformanceStats {
    double totalTime{0.0};                               // Total time spent
    uint64_t callCount{0};                              // Number of calls
    double avgTime{0.0};                                // Average time per call
    double minTime{std::numeric_limits<double>::max()}; // Fastest call
    double maxTime{0.0};                                // Slowest call
    
    void addSample(double time);                        // Add timing sample
    void reset();                                       // Reset all stats
};
```

## Threading

### Threading Model

The EventManager uses intelligent threading based on event count:

- **Small Event Counts** (<1000): Single-threaded processing for better performance
- **Large Event Counts** (≥1000): Multi-threaded batch processing using ThreadSystem
- **Batch Processing**: Events of the same type are processed together in batches
- **Type-Based Distribution**: Different event types can be processed on different threads

### Threading Configuration

```cpp
// Configure threading behavior
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(500); // Thread if >500 events

// Check threading status
bool isThreaded = EventManager::Instance().isThreadingEnabled();
```

### Thread Safety

- **Shared Mutex**: Read-heavy operations use shared locks for concurrent access
- **Atomic Operations**: Event counts and flags use atomic variables
- **Handler Safety**: Handler registration/removal is mutex-protected
- **Pool Safety**: Event pools have their own synchronization

## Best Practices

### 1. Use Convenience Methods

```cpp
// Good: One-line event creation and registration
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.7f, 4.0f);

// Avoid: Manual creation unless you need custom configuration
auto event = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);
// ... configure event manually ...
EventManager::Instance().registerWeatherEvent("Rain", event);
```

### 2. Register Handlers Early

```cpp
void GameInit() {
    // Register all handlers during initialization
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData& data) { /* handle weather */ });
    
    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [](const EventData& data) { /* handle scene changes */ });
}
```

### 3. Use Direct Triggers for One-Off Events

```cpp
// Good: For immediate, one-time events
EventManager::Instance().changeWeather("Stormy", 2.0f);
EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);

// Good: For recurring events
EventManager::Instance().createWeatherEvent("RandomRain", "Rainy", 0.5f, 3.0f);
```

### 4. Monitor Performance

```cpp
void checkEventPerformance() {
    auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    
    if (stats.avgTime > 5.0) { // >5ms average
        std::cout << "Weather events are slow: " << stats.avgTime << "ms" << std::endl;
    }
    
    size_t eventCount = EventManager::Instance().getEventCount();
    if (eventCount > 1000) {
        std::cout << "High event count: " << eventCount << std::endl;
    }
}
```

### 5. Proper Cleanup

```cpp
class GameApplication {
public:
    ~GameApplication() {
        // Clear event-specific handlers first
        EventManager::Instance().clearAllHandlers();
        
        // Then clean up the manager
        EventManager::Instance().clean();
    }
};
```

## Examples

### Complete Event System Setup

```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"

class GameEventSystem {
private:
    bool m_initialized{false};
    
public:
    bool initialize() {
        // Initialize dependencies
        if (!Forge::ThreadSystem::Instance().init()) {
            return false;
        }
        
        if (!EventManager::Instance().init()) {
            return false;
        }
        
        // Configure threading
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(100);
        
        // Set up event handlers
        setupEventHandlers();
        
        // Create initial events
        createGameEvents();
        
        m_initialized = true;
        return true;
    }
    
    void setupEventHandlers() {
        // Weather event handler
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) {
                handleWeatherEvent(data);
            });
        
        // Scene change handler
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [this](const EventData& data) {
                handleSceneChange(data);
            });
        
        // NPC spawn handler
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [this](const EventData& data) {
                handleNPCSpawn(data);
            });
    }
    
    void createGameEvents() {
        // Weather events
        EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.3f, 5.0f);
        EventManager::Instance().createWeatherEvent("AfternoonRain", "Rainy", 0.7f, 3.0f);
        EventManager::Instance().createWeatherEvent("EveningClear", "Clear", 1.0f, 4.0f);
        
        // Scene transitions
        EventManager::Instance().createSceneChangeEvent("EnterTown", "TownScene", "fade", 2.0f);
        EventManager::Instance().createSceneChangeEvent("EnterDungeon", "DungeonScene", "dissolve", 1.5f);
        
        // NPC spawning
        EventManager::Instance().createNPCSpawnEvent("TownGuards", "Guard", 3, 50.0f);
        EventManager::Instance().createNPCSpawnEvent("Merchants", "Merchant", 2, 30.0f);
    }
    
    void update() {
        if (!m_initialized) return;
        
        // Process all events
        EventManager::Instance().update();
        
        // Monitor performance periodically
        static int frameCount = 0;
        if (++frameCount % 300 == 0) { // Every 5 seconds at 60 FPS
            monitorPerformance();
        }
    }
    
    void triggerWeatherChange(const std::string& weatherType) {
        // Immediate weather change
        EventManager::Instance().changeWeather(weatherType, 2.0f);
    }
    
    void triggerSceneTransition(const std::string& sceneId) {
        // Immediate scene change
        EventManager::Instance().changeScene(sceneId, "fade", 1.5f);
    }
    
private:
    void handleWeatherEvent(const EventData& data) {
        std::cout << "Weather event triggered!" << std::endl;
        // Update weather systems, particles, lighting, etc.
    }
    
    void handleSceneChange(const EventData& data) {
        std::cout << "Scene change initiated!" << std::endl;
        // Start transition effects, load new scene, etc.
    }
    
    void handleNPCSpawn(const EventData& data) {
        std::cout << "NPC spawn event!" << std::endl;
        // Create NPCs, set positions, initialize AI, etc.
    }
    
    void monitorPerformance() {
        // Check overall event count
        size_t totalEvents = EventManager::Instance().getEventCount();
        std::cout << "Total events: " << totalEvents << std::endl;
        
        // Check performance by type
        auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        if (weatherStats.callCount > 0) {
            std::cout << "Weather events: " << weatherStats.avgTime << "ms avg" << std::endl;
        }
        
        // Warn about performance issues
        if (weatherStats.avgTime > 5.0) {
            std::cout << "WARNING: Weather events are slow!" << std::endl;
        }
    }
};
```

### Dynamic Event Management

```cpp
class DynamicEventManager {
public:
    void createRandomEvents() {
        // Create weather events based on time of day
        int hour = getCurrentHour();
        if (hour >= 6 && hour <= 18) { // Daytime
            EventManager::Instance().createWeatherEvent("DayWeather", "Clear", 1.0f, 5.0f);
        } else { // Nighttime
            EventManager::Instance().createWeatherEvent("NightWeather", "Foggy", 0.4f, 3.0f);
        }
        
        // Create random encounters
        if (shouldSpawnRandomNPC()) {
            std::string npcType = getRandomNPCType();
            EventManager::Instance().createNPCSpawnEvent("RandomEncounter", npcType, 1, 20.0f);
        }
    }
    
    void handlePlayerAction(const std::string& action) {
        if (action == "enter_building") {
            EventManager::Instance().changeScene("InteriorScene", "fade", 1.0f);
        } else if (action == "cast_rain_spell") {
            EventManager::Instance().changeWeather("Rainy", 1.5f);
        } else if (action == "summon_helper") {
            EventManager::Instance().spawnNPC("Helper", getPlayerX(), getPlayerY());
        }
    }
    
    void cleanupOldEvents() {
        // Remove completed one-time events
        auto events = EventManager::Instance().getEventsByType(EventTypeId::Weather);
        for (auto& event : events) {
            if (event->isOneTime() && event->hasTriggered()) {
                EventManager::Instance().removeEvent(event->getName());
            }
        }
    }
    
private:
    int getCurrentHour() { return 12; } // Placeholder
    bool shouldSpawnRandomNPC() { return rand() % 100 < 10; } // 10% chance
    std::string getRandomNPCType() { return "Villager"; } // Placeholder
    float getPlayerX() { return 100.0f; } // Placeholder
    float getPlayerY() { return 200.0f; } // Placeholder
};
```

### Performance Optimization Example

```cpp
class OptimizedEventProcessor {
public:
    void optimizeEventPerformance() {
        // Check if we need to adjust threading
        size_t eventCount = EventManager::Instance().getEventCount();
        
        if (eventCount > 500 && !EventManager::Instance().isThreadingEnabled()) {
            EventManager::Instance().enableThreading(true);
            std::cout << "Enabled threading for " << eventCount << " events" << std::endl;
        } else if (eventCount < 100 && EventManager::Instance().isThreadingEnabled()) {
            EventManager::Instance().enableThreading(false);
            std::cout << "Disabled threading for better single-thread performance" << std::endl;
        }
        
        // Compact storage if we have many removed events
        static int compactCounter = 0;
        if (++compactCounter % 1000 == 0) { // Every 1000 frames
            EventManager::Instance().compactEventStorage();
        }
        
        // Monitor and log performance issues
        checkPerformanceIssues();
    }
    
private:
    void checkPerformanceIssues() {
        auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        auto sceneStats = EventManager::Instance().getPerformanceStats(EventTypeId::SceneChange);
        auto npcStats = EventManager::Instance().getPerformanceStats(EventTypeId::NPCSpawn);
        
        if (weatherStats.avgTime > 3.0) {
            std::cout << "Weather events slow: " << weatherStats.avgTime << "ms" << std::endl;
        }
        
        if (sceneStats.avgTime > 2.0) {
            std::cout << "Scene events slow: " << sceneStats.avgTime << "ms" << std::endl;
        }
        
        if (npcStats.avgTime > 5.0) {
            std::cout << "NPC events slow: " << npcStats.avgTime << "ms" << std::endl;
        }
    }
};
```

---

The EventManager provides a robust, high-performance foundation for game event processing with excellent integration with the broader Forge Game Engine systems. Its type-safe design and performance monitoring capabilities make it suitable for both simple indie games and complex AAA-style productions.