# EventManager Quick Reference

## Initialization
```cpp
// Initialize ThreadSystem first (required for threading)
Forge::ThreadSystem::Instance().init();

// Initialize EventManager
EventManager::Instance().init();

// Optional: Configure threading
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(100);
```

## Creating Events (New Convenience Methods)

### Weather Events
```cpp
// Create and register in one call
EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.5f, 3.0f);
EventManager::Instance().createWeatherEvent("HeavyStorm", "Stormy", 0.9f, 2.0f);
EventManager::Instance().createWeatherEvent("ClearDay", "Clear", 1.0f, 1.0f);
```

### Scene Change Events
```cpp
// Create and register in one call
EventManager::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
EventManager::Instance().createSceneChangeEvent("ToShop", "ShopScene", "slide", 2.0f);
EventManager::Instance().createSceneChangeEvent("ToBattle", "BattleScene", "dissolve", 2.5f);
```

### NPC Spawn Events
```cpp
// Create and register in one call
EventManager::Instance().createNPCSpawnEvent("GuardPatrol", "Guard", 2, 25.0f);
EventManager::Instance().createNPCSpawnEvent("VillagerGroup", "Villager", 5, 40.0f);
EventManager::Instance().createNPCSpawnEvent("MerchantSpawn", "Merchant", 1, 15.0f);
```

## Direct Event Triggering

### Weather Changes
```cpp
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
EventManager::Instance().changeWeather("Stormy", 1.5f);  // Alternative
```

### Scene Transitions
```cpp
EventManager::Instance().triggerSceneChange("BattleScene", "fade", 2.0f);
EventManager::Instance().changeScene("MainMenu", "dissolve", 1.0f);  // Alternative
```

### NPC Spawning
```cpp
EventManager::Instance().triggerNPCSpawn("Merchant", 100.0f, 200.0f);
EventManager::Instance().spawnNPC("Guard", 250.0f, 150.0f);  // Alternative
```

## Event Handlers
```cpp
// Register handlers by event type
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) {
        std::cout << "Weather changed!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
    [](const EventData& data) {
        std::cout << "NPC spawned!" << std::endl;
    });

EventManager::Instance().registerHandler(EventTypeId::SceneChange,
    [](const EventData& data) {
        std::cout << "Scene changed!" << std::endl;
    });
```

## Update Loop
```cpp
void GameLoop::update() {
    // Single call processes all events efficiently
    EventManager::Instance().update();
}
```

## Event Management
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

## Performance Monitoring
```cpp
// Get performance statistics by event type
auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Weather events: " << weatherStats.avgTime << "ms average" << std::endl;

// Get event counts
size_t totalEvents = EventManager::Instance().getEventCount();
size_t weatherEvents = EventManager::Instance().getEventCount(EventTypeId::Weather);

// Reset performance tracking
EventManager::Instance().resetPerformanceStats();
```

## Event Types
```cpp
enum class EventTypeId : uint8_t {
    Weather = 0,      // Weather system events
    SceneChange = 1,  // Scene transition events
    NPCSpawn = 2,     // NPC creation events
    Custom = 3,       // User-defined events
    COUNT = 4         // Total count (do not use)
};
```

## Performance Characteristics
- **Small Games** (10-50 events): 82,000-95,000 events/second
- **Medium Games** (50-100 events): 54,000-58,000 events/second
- **Large Games** (100-200 events): 24,000-50,000 events/second
- **Maximum Scale**: ~500 events (still performant)

## Threading Configuration
```cpp
// Enable threading with threshold
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(50);  // Use threads if >50 events

// Check threading status
bool usingThreads = EventManager::Instance().isThreadingEnabled();
```

## Memory Management
```cpp
// Optimize memory usage periodically
EventManager::Instance().compactEventStorage();

// Clear event pools (only during cleanup)
EventManager::Instance().clearEventPools();
```

## Best Practices
1. **Use convenience methods** for creating events (one-line creation + registration)
2. **Keep realistic scales** (10-500 events total)
3. **Monitor performance** using built-in statistics
4. **Initialize ThreadSystem first** before EventManager
5. **Set appropriate threading thresholds** (50+ events for threading)
6. **Use batch processing** for maximum efficiency
7. **Call `update()` once per frame** for all event processing

## Complete Example
```cpp
class GameState {
public:
    void init() {
        // Initialize systems
        Forge::ThreadSystem::Instance().init();
        EventManager::Instance().init();
        EventManager::Instance().enableThreading(true);
        
        // Create weather events
        EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
        EventManager::Instance().createWeatherEvent("Storm", "Stormy", 0.9f, 2.0f);
        
        // Register handler
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) { onWeatherChanged(); });
    }
    
    void update() {
        EventManager::Instance().update();  // Process all events
    }
    
    void triggerWeatherChange() {
        EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);
    }
    
private:
    void onWeatherChanged() {
        std::cout << "Weather system updated!" << std::endl;
    }
};
```