# EventManager Quick Reference

**Where to find the code:**
- Implementation: `src/managers/EventManager.cpp`
- Header: `include/managers/EventManager.hpp`

**Singleton Access:** Use `EventManager::Instance()` to access the manager.

## Overview
Quick reference for the Hammer Game Engine EventManager as the single source of truth for all event operations. Features queue pressure monitoring, WorkerBudget integration, and architectural consistency with AIManager.

## Essential Includes
```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"
```

## Quick Setup
```cpp
// Initialize dependencies
HammerEngine::ThreadSystem::Instance().init();

// Initialize EventManager (single source of truth)
EventManager::Instance().init();

// Optional: Configure threading (automatic queue pressure monitoring)
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(50); // Lower threshold for events

// Queue pressure monitoring is automatic (90% capacity threshold)
// Dynamic batch sizing adjusts based on real-time queue pressure
```

## Event Types
```cpp
enum class EventTypeId : uint8_t {
    Weather = 0,
    SceneChange = 1,
    NPCSpawn = 2,
    ParticleEffect = 3,
    ResourceChange = 4,
    World = 5,
    Camera = 6,
    Harvest = 7,
    Custom = 8,
    COUNT = 9
};
```

## Simple Event Creation (Recommended)

### Weather Events
```cpp
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
EventManager::Instance().createWeatherEvent("Fog", "Foggy", 0.5f, 5.0f);
EventManager::Instance().createWeatherEvent("Storm", "Stormy", 0.9f, 2.0f);
EventManager::Instance().createWeatherEvent("Clear", "Clear", 1.0f, 4.0f);

// Parameters: (name, weatherType, intensity, transitionTime)
```

### Scene Change Events
```cpp
EventManager::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);
EventManager::Instance().createSceneChangeEvent("ToDungeon", "DungeonScene", "dissolve", 1.5f);
EventManager::Instance().createSceneChangeEvent("ToMenu", "MainMenu", "fade", 1.0f);

// Parameters: (name, targetScene, transitionType, duration)
```

### NPC Spawn Events
```cpp
EventManager::Instance().createNPCSpawnEvent("Guards", "Guard", 3, 50.0f);
EventManager::Instance().createNPCSpawnEvent("Villagers", "Villager", 5, 30.0f);
EventManager::Instance().createNPCSpawnEvent("Merchant", "Merchant", 1, 0.0f);

// Parameters: (name, npcType, count, spawnRadius)
```

## Factory-Based Creation

For advanced or definition-driven creation, use `EventFactory`:

```cpp
#include "events/EventFactory.hpp"

EventDefinition def{
    .type = "Weather",
    .name = "EpicStorm",
    .params = {{"weatherType", "Stormy"}},
    .numParams = {{"intensity", 0.95f}, {"transitionTime", 1.5f}},
};

auto ev = EventFactory::Instance().createEvent(def);
EventManager::Instance().registerEvent(def.name, ev);
```

## Direct Event Triggering (No Pre-Registration)
```cpp
// Immediate weather changes
EventManager::Instance().changeWeather("Stormy", 2.0f);
EventManager::Instance().triggerWeatherChange("Rainy", 3.0f);

// Immediate scene transitions
EventManager::Instance().changeScene("BattleScene", "fade", 1.5f);
EventManager::Instance().triggerSceneChange("MainMenu", "dissolve", 1.0f);

// Immediate NPC spawning
EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);
EventManager::Instance().triggerNPCSpawn("Guard", 250.0f, 150.0f);
```

## Event Handlers
```cpp
// Register type-safe handlers with EventManager
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

## Event Sequences (Factory)

```cpp
#include "events/EventFactory.hpp"

std::vector<EventDefinition> story = {
    {.type = "Weather", .name = "StartRain", .params={{"weatherType","Rainy"}}, .numParams={{"transitionTime",2.0f}}},
    {.type = "Weather", .name = "GetStormy", .params={{"weatherType","Stormy"}}, .numParams={{"intensity",0.9f}}},
    {.type = "Weather", .name = "ClearUp", .params={{"weatherType","Clear"}}},
};

auto events = EventFactory::Instance().createEventSequence("WeatherStory", story, true);
for (auto &e : events) { EventManager::Instance().registerEvent(e->getName(), e); }
```

## Custom Events (Factory)

```cpp
// Register a custom creator and create an instance
EventFactory::Instance().registerCustomEventCreator("Quest",
    [](const EventDefinition& def) -> EventPtr {
        std::string questId = def.params.count("questId") ? def.params.at("questId") : "";
        int reward = static_cast<int>(def.numParams.count("reward") ? def.numParams.at("reward") : 0.0f);
        return std::make_shared<QuestEvent>(def.name, questId, reward);
    });

EventDefinition q{.type="Quest", .name="FindTreasure", .params={{"questId","treasure_hunt"}}, .numParams={{"reward",1000.0f}}};
auto quest = EventFactory::Instance().createEvent(q);
EventManager::Instance().registerEvent(q.name, quest);
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

## Handlers & Dispatch
```cpp
// Type-safe handlers
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) { /* handle weather */ });

// Per-name handlers
auto nameTok = EventManager::Instance().registerHandlerForName("demo_rainy",
    [](const EventData& data) { /* handle named demo */ });

// Remove per-name handlers in bulk
EventManager::Instance().removeNameHandlers("demo_rainy");

// Remove by token
EventManager::Instance().removeHandler(nameTok);
```

### Dispatch Modes and Fallbacks
- Immediate: Handlers invoked on the calling thread.
- Deferred: Enqueued and drained during `EventManager::update()` with a small time budget.
- No-handler fallback: For `changeWeather`, `changeScene`, `spawnNPC`, and `triggerParticleEffect`, sensible defaults execute when no handlers are registered (useful for demos and tests).

## Factory Quick Guide
- Particle: `EventFactory::Instance().createParticleEffectEvent(name, effectName, x, y, intensity, duration, groupTag, soundEffect)`
- World:
  - `createWorldLoadedEvent(name, worldId, width, height)`
  - `createWorldUnloadedEvent(name, worldId)`
  - `createTileChangedEvent(name, x, y, changeType)`
  - `createWorldGeneratedEvent(name, worldId, width, height, generationTime)`
- Camera:
  - `createCameraMovedEvent(name, newX, newY, oldX, oldY)`
  - `createCameraModeChangedEvent(name, newMode, oldMode)`
  - `createCameraShakeEvent(name, duration, intensity)`
- Resource: `createResourceChangeEvent(name, resourceId, resourceGen, oldQuantity, newQuantity, reason)`

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
// Handler control through EventManager
EventManager::Instance().removeHandlers(EventTypeId::Weather);
EventManager::Instance().clearAllHandlers();
size_t count = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
```

## Performance Monitoring
```cpp
// Get performance stats through EventManager
auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
std::cout << "Weather events: " << stats.avgTime << "ms avg" << std::endl;
std::cout << "Call count: " << stats.callCount << std::endl;

// Reset tracking
EventManager::Instance().resetPerformanceStats();

// Event counts
size_t eventCount = EventManager::Instance().getEventCount();
if (eventCount > 1000) {
    std::cout << "High event count: " << eventCount << std::endl;
}
```

## Threading Configuration
```cpp
// Threading control through EventManager
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(500); // Thread if >500 events
bool isThreaded = EventManager::Instance().isThreadingEnabled();

// Recommended thresholds:
// - 50-100: Light threading benefit
// - 200-500: Moderate threading benefit
// - 1000+: Significant threading benefit
```

## Memory Management
```cpp
// Optimize memory usage through EventManager
EventManager::Instance().compactEventStorage();
EventManager::Instance().clearEventPools(); // Use only during cleanup
```

## Main Update Loop
```cpp
void gameUpdate() {
    // Single call to EventManager processes all events efficiently
    EventManager::Instance().update();
}
```

## EventData Structure
```cpp
struct EventData {
    EventPtr event;           // Smart pointer to event
    EventTypeId typeId;       // Type for fast dispatch
    uint32_t flags;           // Active, dirty, etc.
    uint32_t priority;        // Processing priority

    // Helper methods (internal use)
    bool isActive() const;
    void setActive(bool active);
    bool isDirty() const;
    void setDirty(bool dirty);
};
```

## Built-in Types and Values

### Weather Types
- **"Clear"**: Full visibility, no particles
- **"Cloudy"**: Slight visibility reduction
- **"Rainy"**: Rain particles, reduced visibility
- **"Stormy"**: Heavy rain, lightning, thunder
- **"Foggy"**: Dramatic visibility reduction
- **"Snowy"**: Snow particles and effects
- **"Windy"**: Wind effects
- **Custom**: Any string for custom weather

### Transition Types
- **"fade"**: Standard fade transition
- **"dissolve"**: Dissolve effect
- **"slide"**: Sliding transition
- **"wipe"**: Wipe transition
- **Custom**: Any string for custom transitions

## Common Patterns

### Initialize and Setup
```cpp
void initEventSystem() {
    // Initialize dependencies
    HammerEngine::ThreadSystem::Instance().init();

    // Initialize EventManager (single source of truth)
    EventManager::Instance().init();
    EventManager::Instance().enableThreading(true);

    // Register handlers through EventManager
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData& data) { /* handle weather */ });

    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [](const EventData& data) { /* handle scene change */ });
}
```

### Create Game Events
```cpp
void createGameEvents() {
    // All creation through EventManager
    EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.3f, 5.0f);
    EventManager::Instance().createWeatherEvent("AfternoonRain", "Rainy", 0.7f, 3.0f);

    // Scene transitions
    EventManager::Instance().createSceneChangeEvent("EnterTown", "TownScene", "fade", 2.0f);
    EventManager::Instance().createAdvancedSceneChangeEvent("EnterDungeon", "DungeonScene", "dissolve", 1.5f, 7, false);

    // NPC spawning
    EventManager::Instance().createNPCSpawnEvent("TownGuards", "Guard", 3, 50.0f);
    EventManager::Instance().createAdvancedNPCSpawnEvent("Merchants", "Merchant", 2, 30.0f, 5, false);
}
```

### Dynamic Event Triggering
```cpp
void handlePlayerAction(const std::string& action) {
    // All triggering through EventManager
    if (action == "cast_rain_spell") {
        EventManager::Instance().changeWeather("Rainy", 2.0f);
    } else if (action == "enter_building") {
        EventManager::Instance().changeScene("InteriorScene", "fade", 1.0f);
    } else if (action == "call_for_help") {
        EventManager::Instance().spawnNPC("Helper", getPlayerX(), getPlayerY());
    }
}
```

### Batch Creation Pattern
```cpp
void createEventsBatch() {
    // Create multiple weather events through EventManager
    std::vector<std::tuple<std::string, std::string, float, float>> weatherEvents = {
        {"MorningFog", "Foggy", 0.4f, 5.0f},
        {"NoonSun", "Clear", 1.0f, 3.0f},
        {"EveningRain", "Rainy", 0.6f, 4.0f},
        {"NightStorm", "Stormy", 0.8f, 2.0f}
    };

    for (const auto& [name, type, intensity, time] : weatherEvents) {
        EventManager::Instance().createWeatherEvent(name, type, intensity, time);
    }

    // Or use weather sequence
    EventManager::Instance().createWeatherSequence("DailyWeather", weatherEvents, true);
}
```

### Performance Monitoring
```cpp
void checkEventPerformance() {
    // All monitoring through EventManager
    size_t eventCount = EventManager::Instance().getEventCount();
    if (eventCount > 1000) {
        std::cout << "High event count: " << eventCount << std::endl;
        EventManager::Instance().compactEventStorage();
    }

    auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    if (weatherStats.avgTime > 5.0) {
        std::cout << "Weather events slow: " << weatherStats.avgTime << "ms" << std::endl;
    }
}
```

### Complete Setup Example
```cpp
class GameEventSystem {
public:
    bool initialize() {
        // Initialize dependencies
        if (!HammerEngine::ThreadSystem::Instance().init()) return false;

        // Initialize EventManager (single source of truth)
        if (!EventManager::Instance().init()) return false;

        // Configure through EventManager
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(200);

        // Setup everything through EventManager
        setupHandlers();
        createEvents();

        return true;
    }

private:
    void setupHandlers() {
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [](const EventData& data) { /* weather logic */ });
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [](const EventData& data) { /* scene logic */ });
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [](const EventData& data) { /* NPC logic */ });
    }

    void createEvents() {
        // Simple events
        EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.7f, 3.0f);
        EventManager::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);

        // Advanced events
        EventManager::Instance().createAdvancedWeatherEvent("EpicStorm", "Stormy", 0.95f, 1.5f, 10, 60.0f, true, true);

        // Custom events
        std::unordered_map<std::string, std::string> params = {{"questId", "tutorial"}};
        std::unordered_map<std::string, float> numParams = {{"reward", 100.0f}};
        EventManager::Instance().createCustomEvent("Quest", "Tutorial", params, numParams, {});
    }
};
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

## Cleanup
```cpp
void gameShutdown() {
    // All cleanup through EventManager
    EventManager::Instance().clearAllHandlers();
    EventManager::Instance().clean();
}
```

## Key Design Principle
**EventManager is the single source of truth** - all event operations go through EventManager:

✅ **Correct Usage:**
```cpp
EventManager::Instance().createWeatherEvent(...);
EventManager::Instance().changeWeather(...);
EventManager::Instance().registerHandler(...);
```

❌ **Avoid Direct Access:**
```cpp
// Don't access other components directly
EventFactory::Instance().createEvent(...);  // Don't do this
```

## Tips
- Use EventManager for ALL event operations - it's the single interface
- Use simple creation methods for basic events, advanced methods for complex ones
- Register handlers during initialization for best performance
- Use direct triggers for immediate one-off events
- Monitor performance through EventManager's built-in statistics
- Enable threading when you have many events (>100)
- Use event sequences for related events that should execute together
- Always check return values for error handling
- Clean up through EventManager before shutdown

---

This quick reference covers EventManager as the single source of truth for all event operations. See [EventManager.md](EventManager.md) for detailed documentation and [EventManager_Advanced.md](EventManager_Advanced.md) for advanced topics.
