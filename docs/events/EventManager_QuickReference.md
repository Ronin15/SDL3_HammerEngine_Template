# EventManager Quick Reference

## Overview
Quick reference for the high-performance EventManager with type-indexed storage and batch processing.

## Essential Includes
```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"
```

## Quick Setup
```cpp
// Initialize dependencies
Forge::ThreadSystem::Instance().init();

// Initialize EventManager
EventManager::Instance().init();

// Configure threading (optional)
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(100);
```

## Event Types
```cpp
enum class EventTypeId : uint8_t {
    Weather = 0,      // Weather system events
    SceneChange = 1,  // Scene transition events
    NPCSpawn = 2,     // NPC creation events
    Custom = 3,       // User-defined events
    COUNT = 4         // Total count (internal use)
};
```

## One-Line Event Creation (Recommended)
```cpp
// Weather events
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
EventManager::Instance().createWeatherEvent("Fog", "Foggy", 0.5f, 5.0f);

// Scene transitions
EventManager::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);
EventManager::Instance().createSceneChangeEvent("ToDungeon", "DungeonScene", "dissolve", 1.5f);

// NPC spawning
EventManager::Instance().createNPCSpawnEvent("Guards", "Guard", 3, 50.0f);
EventManager::Instance().createNPCSpawnEvent("Villagers", "Villager", 5, 30.0f);
```

## Direct Event Triggering
```cpp
// Immediate event execution (no pre-registration needed)
EventManager::Instance().changeWeather("Stormy", 2.0f);
EventManager::Instance().changeScene("BattleScene", "fade", 1.5f);
EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);

// Alternative method names
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
EventManager::Instance().triggerSceneChange("MainMenu", "dissolve", 1.0f);
EventManager::Instance().triggerNPCSpawn("Guard", 250.0f, 150.0f);
```

## Event Handlers
```cpp
// Register type-safe handlers
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) {
        std::cout << "Weather event!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::SceneChange,
    [](const EventData& data) {
        std::cout << "Scene changing!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
    [](const EventData& data) {
        std::cout << "NPC spawned!" << std::endl;
    });
```

## Manual Event Registration
```cpp
// Create events manually if needed
auto rainEvent = std::make_shared<WeatherEvent>("Rain", WeatherType::Rainy);
EventManager::Instance().registerWeatherEvent("Rain", rainEvent);

auto sceneEvent = std::make_shared<SceneChangeEvent>("ToShop", "ShopScene");
EventManager::Instance().registerSceneChangeEvent("ToShop", sceneEvent);

auto npcEvent = std::make_shared<NPCSpawnEvent>("Guards", "Guard");
EventManager::Instance().registerNPCSpawnEvent("Guards", npcEvent);
```

## Event Management
```cpp
// Check events
bool exists = EventManager::Instance().hasEvent("MyEvent");
auto event = EventManager::Instance().getEvent("MyEvent");

// Control event state
EventManager::Instance().setEventActive("MyEvent", false);
bool isActive = EventManager::Instance().isEventActive("MyEvent");

// Execute events
EventManager::Instance().executeEvent("MyEvent");
int count = EventManager::Instance().executeEventsByType(EventTypeId::Weather);

// Remove events
EventManager::Instance().removeEvent("MyEvent");
```

## Event Queries
```cpp
// Get events by type
auto weatherEvents = EventManager::Instance().getEventsByType(EventTypeId::Weather);
auto sceneEvents = EventManager::Instance().getEventsByType("SceneChange");

// Event counts
size_t totalEvents = EventManager::Instance().getEventCount();
size_t weatherCount = EventManager::Instance().getEventCount(EventTypeId::Weather);
```

## Handler Management
```cpp
// Handler control
EventManager::Instance().removeHandlers(EventTypeId::Weather);
EventManager::Instance().clearAllHandlers();
size_t count = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
```

## Performance Monitoring
```cpp
// Get performance stats
auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Weather events: " << stats.avgTime << "ms avg" << std::endl;
std::cout << "Call count: " << stats.callCount << std::endl;

// Reset tracking
EventManager::Instance().resetPerformanceStats();
```

## Threading Configuration
```cpp
// Threading control
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(500); // Thread if >500 events
bool isThreaded = EventManager::Instance().isThreadingEnabled();
```

## Memory Management
```cpp
// Optimize memory usage
EventManager::Instance().compactEventStorage();
EventManager::Instance().clearEventPools(); // Use only during cleanup
```

## Main Update Loop
```cpp
void gameUpdate() {
    // Single call processes all events efficiently
    EventManager::Instance().update();
}
```

## Cleanup
```cpp
void gameShutdown() {
    // Clear handlers first
    EventManager::Instance().clearAllHandlers();
    
    // Clean up manager
    EventManager::Instance().clean();
}
```

## EventData Structure
```cpp
struct EventData {
    EventPtr event;           // Smart pointer to event
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

## Common Patterns

### Initialize and Setup
```cpp
void initEventSystem() {
    Forge::ThreadSystem::Instance().init();
    EventManager::Instance().init();
    EventManager::Instance().enableThreading(true);
    
    // Register handlers
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData& data) { /* handle weather */ });
}
```

### Create Game Events
```cpp
void createGameEvents() {
    // Weather system
    EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.3f, 5.0f);
    EventManager::Instance().createWeatherEvent("AfternoonRain", "Rainy", 0.7f, 3.0f);
    
    // Scene transitions
    EventManager::Instance().createSceneChangeEvent("EnterTown", "TownScene", "fade", 2.0f);
    EventManager::Instance().createSceneChangeEvent("EnterDungeon", "DungeonScene", "dissolve", 1.5f);
    
    // NPC spawning
    EventManager::Instance().createNPCSpawnEvent("TownGuards", "Guard", 3, 50.0f);
    EventManager::Instance().createNPCSpawnEvent("Merchants", "Merchant", 2, 30.0f);
}
```

### Dynamic Event Triggering
```cpp
void handlePlayerAction(const std::string& action) {
    if (action == "cast_rain_spell") {
        EventManager::Instance().changeWeather("Rainy", 2.0f);
    } else if (action == "enter_building") {
        EventManager::Instance().changeScene("InteriorScene", "fade", 1.0f);
    } else if (action == "call_for_help") {
        EventManager::Instance().spawnNPC("Helper", getPlayerX(), getPlayerY());
    }
}
```

### Performance Monitoring
```cpp
void checkEventPerformance() {
    size_t eventCount = EventManager::Instance().getEventCount();
    if (eventCount > 1000) {
        std::cout << "High event count: " << eventCount << std::endl;
    }
    
    auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    if (weatherStats.avgTime > 5.0) {
        std::cout << "Weather events slow: " << weatherStats.avgTime << "ms" << std::endl;
    }
}
```

## Error Handling
```cpp
// Always check creation success
if (!EventManager::Instance().createWeatherEvent("Test", "Rainy", 0.5f, 3.0f)) {
    std::cerr << "Failed to create weather event!" << std::endl;
}

// Check event existence
if (!EventManager::Instance().hasEvent("MyEvent")) {
    std::cerr << "Event 'MyEvent' not found!" << std::endl;
}

// Verify handler registration
size_t handlerCount = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
if (handlerCount == 0) {
    std::cout << "No weather handlers registered!" << std::endl;
}
```

## Tips
- Use convenience methods (`createWeatherEvent`, etc.) for most cases
- Register handlers during initialization for best performance
- Use direct triggers (`changeWeather`, etc.) for immediate one-off events
- Monitor performance with built-in statistics
- Enable threading only when you have many events (>100)
- Clean up handlers before shutting down the manager

---

This quick reference covers the most commonly used EventManager features. See the full EventManager documentation for detailed explanations and advanced usage patterns.