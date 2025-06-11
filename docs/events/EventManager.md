# EventManager Documentation

## Overview

The Forge Game Engine EventManager provides a comprehensive, high-performance event management framework as the single source of truth for all event operations. The system supports weather events, scene transitions, NPC spawning, and custom events with intelligent threading, type-safe handlers, and optimized batch processing.

## Table of Contents

- [Quick Start](#quick-start)
- [Core Architecture](#core-architecture)
- [Event Types](#event-types)
- [API Reference](#api-reference)
- [Threading & Performance](#threading--performance)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Setup
```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"

// Initialize dependencies
Forge::ThreadSystem::Instance().init();

// Initialize EventManager (handles all event creation and management)
EventManager::Instance().init();

// Optional: Enable threading for high event counts
EventManager::Instance().enableThreading(true);
EventManager::Instance().setThreadingThreshold(100);
```

### Creating Events (Single API)
```cpp
// One-line event creation - EventManager handles everything
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
EventManager::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);
EventManager::Instance().createNPCSpawnEvent("Guards", "Guard", 3, 50.0f);
```

### Event Handlers
```cpp
// Register type-safe handlers with EventManager
EventManager::Instance().registerHandler(EventTypeId::Weather,
    [](const EventData& data) {
        std::cout << "Weather changed!" << std::endl;
    });
```

### Direct Event Triggering
```cpp
// Immediate event execution through EventManager
EventManager::Instance().changeWeather("Stormy", 2.0f);
EventManager::Instance().changeScene("BattleScene", "fade", 1.5f);
EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);
```

### Update Loop Integration
```cpp
void gameUpdate() {
    // Single call to EventManager processes all events
    EventManager::Instance().update();
}
```

## Core Architecture

### EventManager - Single Source of Truth
The EventManager is the central and only public interface for all event operations. It internally manages:

- **Event Creation**: All event creation goes through EventManager methods
- **Event Storage**: Type-indexed storage with optimized batch processing
- **Event Execution**: Handles all event processing and handler invocation
- **Threading**: Automatic threading decisions based on workload
- **Performance**: Built-in monitoring and optimization
- **Memory Management**: Efficient storage with compaction support

**Key Design Principle**: EventManager encapsulates all event functionality - no other components should be directly accessed for event operations.

### Internal Architecture
```cpp
EventManager
├── Event Creation (internal factory methods)
├── Event Storage (type-indexed containers)
├── Event Processing (batch operations)
├── Handler Management (type-safe registration)
├── Threading Integration (WorkerBudget coordination)
└── Performance Monitoring (built-in statistics)
```

### EventData Structure
Core data structure managed by EventManager:

```cpp
struct EventData {
    EventPtr event;           // Smart pointer to event
    EventTypeId typeId;       // Type for fast dispatch
    uint32_t flags;           // Active, dirty, pending removal
    float lastUpdateTime;     // For delta time calculations
    uint32_t priority;        // Processing priority
    
    // Helper methods (internal use)
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
    Weather = 0,      // Weather system events
    SceneChange = 1,  // Scene transition events
    NPCSpawn = 2,     // NPC creation events
    Custom = 3,       // User-defined events
    COUNT = 4         // Total count (internal use)
};
```

### Weather Events
Control game atmosphere and environmental conditions through EventManager:

```cpp
// Simple weather event creation
EventManager::Instance().createWeatherEvent("Storm", "Stormy", 0.9f, 2.0f);

// Advanced weather event with custom properties
EventManager::Instance().createAdvancedWeatherEvent("EpicStorm", "Stormy", 0.95f, 1.5f, 8, 30.0f, false, true);
// Parameters: (name, weatherType, intensity, transitionTime, priority, cooldown, oneTime, active)
```

**Weather Types**: Clear, Cloudy, Rainy, Stormy, Foggy, Snowy, Windy, Custom
**Automatic Features**: Visibility adjustment, particle effects, ambient sounds

### Scene Change Events
Handle scene transitions and navigation through EventManager:

```cpp
// Simple scene change
EventManager::Instance().createSceneChangeEvent("ToShop", "ShopScene", "dissolve", 1.5f);

// Advanced scene change with custom properties
EventManager::Instance().createAdvancedSceneChangeEvent("MagicPortal", "MagicRealm", "dissolve", 3.0f, 5, true);
// Parameters: (name, targetScene, transitionType, duration, priority, oneTime)
```

**Transition Types**: fade, dissolve, slide, wipe, custom

### NPC Spawn Events
Manage dynamic NPC creation through EventManager:

```cpp
// Simple NPC spawn
EventManager::Instance().createNPCSpawnEvent("Villagers", "Villager", 5, 30.0f);

// Advanced NPC spawn with custom properties
EventManager::Instance().createAdvancedNPCSpawnEvent("OrcInvasion", "OrcWarrior", 10, 100.0f, 9, true);
// Parameters: (name, npcType, count, spawnRadius, priority, oneTime)
```

### Custom Events
EventManager provides extensible custom event support:

```cpp
// Register custom event type with EventManager
EventManager::Instance().registerCustomEventType("Quest", 
    [](const std::string& name, const std::unordered_map<std::string, std::string>& params,
       const std::unordered_map<std::string, float>& numParams,
       const std::unordered_map<std::string, bool>& boolParams) -> EventPtr {
        std::string questId = params.count("questId") ? params.at("questId") : "";
        int reward = static_cast<int>(numParams.count("reward") ? numParams.at("reward") : 0.0f);
        return std::make_shared<QuestEvent>(name, questId, reward);
    });

// Create custom event through EventManager
std::unordered_map<std::string, std::string> questParams = {{"questId", "treasure_hunt"}};
std::unordered_map<std::string, float> questNumParams = {{"reward", 1000.0f}};
std::unordered_map<std::string, bool> questBoolParams = {};

EventManager::Instance().createCustomEvent("Quest", "FindTreasure", questParams, questNumParams, questBoolParams);
```

## API Reference

### EventManager Core Methods

#### Initialization
```cpp
bool init()                    // Initialize the event manager
void clean()                   // Clean shutdown
bool isShutdown()             // Check shutdown state
void update()                 // Process all events (call each frame)
```

#### Simple Event Creation
```cpp
bool createWeatherEvent(const std::string& name, const std::string& weatherType, 
                       float intensity, float transitionTime)
bool createSceneChangeEvent(const std::string& name, const std::string& targetScene,
                           const std::string& transitionType, float duration)
bool createNPCSpawnEvent(const std::string& name, const std::string& npcType,
                        int count, float spawnRadius)
```

#### Advanced Event Creation
```cpp
bool createAdvancedWeatherEvent(const std::string& name, const std::string& weatherType,
                               float intensity, float transitionTime, uint32_t priority = 5,
                               float cooldown = 10.0f, bool oneTime = false, bool active = true)
bool createAdvancedSceneChangeEvent(const std::string& name, const std::string& targetScene,
                                   const std::string& transitionType, float duration,
                                   uint32_t priority = 5, bool oneTime = false)
bool createAdvancedNPCSpawnEvent(const std::string& name, const std::string& npcType,
                                int count, float spawnRadius, uint32_t priority = 5, bool oneTime = false)
```

#### Custom Event Support
```cpp
void registerCustomEventType(const std::string& typeName,
                            std::function<EventPtr(const std::string&, 
                                                  const std::unordered_map<std::string, std::string>&,
                                                  const std::unordered_map<std::string, float>&,
                                                  const std::unordered_map<std::string, bool>&)> creator)
bool createCustomEvent(const std::string& typeName, const std::string& name,
                      const std::unordered_map<std::string, std::string>& params = {},
                      const std::unordered_map<std::string, float>& numParams = {},
                      const std::unordered_map<std::string, bool>& boolParams = {})
```

#### Event Sequences
```cpp
bool createEventSequence(const std::string& sequenceName,
                        const std::vector<std::string>& eventNames,
                        bool sequential = true)
bool createWeatherSequence(const std::string& sequenceName,
                          const std::vector<std::tuple<std::string, std::string, float, float>>& weatherEvents,
                          bool sequential = true)
```

#### Direct Event Triggering
```cpp
bool changeWeather(const std::string& weatherType, float transitionTime)
bool changeScene(const std::string& targetScene, const std::string& transitionType, float duration)
bool spawnNPC(const std::string& npcType, float x, float y)

// Alternative method names
bool triggerWeatherChange(const std::string& weatherType, float transitionTime)
bool triggerSceneChange(const std::string& targetScene, const std::string& transitionType, float duration)
bool triggerNPCSpawn(const std::string& npcType, float x, float y)
```

#### Event Management
```cpp
EventPtr getEvent(const std::string& name)
std::vector<EventPtr> getEventsByType(EventTypeId typeId)
std::vector<EventPtr> getEventsByType(const std::string& typeName)

bool setEventActive(const std::string& name, bool active)
bool isEventActive(const std::string& name)
bool removeEvent(const std::string& name)
bool hasEvent(const std::string& name)
```

#### Event Execution
```cpp
bool executeEvent(const std::string& name)
int executeEventsByType(EventTypeId typeId)
int executeEventsByType(const std::string& typeName)
```

#### Handler Management
```cpp
void registerHandler(EventTypeId typeId, std::function<void(const EventData&)> handler)
void removeHandlers(EventTypeId typeId)
void clearAllHandlers()
size_t getHandlerCount(EventTypeId typeId)
```

#### Performance and Monitoring
```cpp
PerformanceStats getPerformanceStats(EventTypeId typeId)
void resetPerformanceStats()
size_t getEventCount()
size_t getEventCount(EventTypeId typeId)

void compactEventStorage()    // Optimize memory layout
void clearEventPools()        // Clear cached objects (shutdown only)
```

## Threading & Performance

### Threading Model
EventManager uses intelligent threading decisions based on workload:

- **Automatic Threading**: Enabled when event count exceeds threshold
- **Type-Based Batching**: Events processed by type for optimal cache usage
- **WorkerBudget Integration**: Allocates 30% of available worker threads to events
- **Lock-Free Operations**: Minimal locking for high-performance concurrent access

### Threading Configuration
```cpp
// Threading control through EventManager
EventManager::Instance().enableThreading(true);
bool isThreaded = EventManager::Instance().isThreadingEnabled();

// Set threading threshold (default: 100 events)
EventManager::Instance().setThreadingThreshold(500);
```

### Performance Characteristics
- **Single-threaded**: Optimal for <100 events per frame
- **Multi-threaded**: Significant benefits with >500 events per frame
- **Batch Processing**: Linear performance scaling up to 10,000+ events
- **Memory Efficiency**: Type-indexed storage minimizes cache misses

### Performance Monitoring
```cpp
void monitorEventPerformance() {
    auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    std::cout << "Weather events: " << stats.avgTime << "ms avg, " 
              << stats.callCount << " calls" << std::endl;
    
    size_t totalEvents = EventManager::Instance().getEventCount();
    if (totalEvents > 1000) {
        std::cout << "High event count detected: " << totalEvents << std::endl;
        EventManager::Instance().compactEventStorage();
    }
}
```

## Best Practices

### 1. Use EventManager as Single Interface
```cpp
// ✅ Correct: Use EventManager for all event operations
EventManager::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
EventManager::Instance().changeWeather("Stormy", 2.0f);

// ❌ Avoid: Don't access internal components directly
// EventFactory::Instance().createWeatherEvent(...); // Don't do this
```

### 2. Register Handlers Early
```cpp
void gameInit() {
    // Initialize EventManager first
    EventManager::Instance().init();
    
    // Register handlers during initialization
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [](const EventData& data) { handleWeatherChange(data); });
    
    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [](const EventData& data) { handleSceneTransition(data); });
}
```

### 3. Use Direct Triggers for Immediate Events
```cpp
void handlePlayerAction(const std::string& action) {
    if (action == "cast_weather_spell") {
        // Direct triggering through EventManager - no pre-registration needed
        EventManager::Instance().changeWeather("Stormy", 2.0f);
    }
}
```

### 4. Leverage Advanced Creation for Complex Events
```cpp
void createComplexEvents() {
    // Use advanced methods for events with custom properties
    EventManager::Instance().createAdvancedWeatherEvent(
        "EpicStorm", "Stormy", 0.95f, 1.5f, 10, 60.0f, true, true);
        // High priority, 60s cooldown, one-time event, initially active
}
```

### 5. Monitor Performance Through EventManager
```cpp
void checkEventPerformance() {
    size_t eventCount = EventManager::Instance().getEventCount();
    if (eventCount > 1000) {
        // EventManager handles optimization
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().compactEventStorage();
    }
}
```

### 6. Use Event Sequences for Related Events
```cpp
void createStorySequence() {
    // Create weather sequence through EventManager
    std::vector<std::tuple<std::string, std::string, float, float>> weatherEvents = {
        {"StartRain", "Rainy", 0.5f, 3.0f},
        {"GetStormy", "Stormy", 0.9f, 2.0f},
        {"ClearUp", "Clear", 1.0f, 4.0f}
    };
    
    EventManager::Instance().createWeatherSequence("StoryWeather", weatherEvents, true);
}
```

### 7. Proper Cleanup
```cpp
class GameApplication {
public:
    ~GameApplication() {
        // Clean up through EventManager
        EventManager::Instance().clearAllHandlers();
        EventManager::Instance().clean();
    }
};
```

## Examples

### Complete Event System Setup
```cpp
class GameEventSystem {
private:
    bool m_initialized = false;

public:
    bool initialize() {
        // Initialize dependencies
        if (!Forge::ThreadSystem::Instance().init()) {
            return false;
        }
        
        // Initialize EventManager (single source of truth)
        if (!EventManager::Instance().init()) {
            return false;
        }
        
        // Configure threading through EventManager
        EventManager::Instance().enableThreading(true);
        EventManager::Instance().setThreadingThreshold(200);
        
        // Setup through EventManager
        setupEventHandlers();
        createGameEvents();
        
        m_initialized = true;
        return true;
    }
    
    void setupEventHandlers() {
        // All handler registration through EventManager
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) {
                handleWeatherEvent(data);
            });
        
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [this](const EventData& data) {
                handleSceneChange(data);
            });
        
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [this](const EventData& data) {
                handleNPCSpawn(data);
            });
    }
    
    void createGameEvents() {
        // All event creation through EventManager
        EventManager::Instance().createWeatherEvent("MorningFog", "Foggy", 0.4f, 5.0f);
        EventManager::Instance().createWeatherEvent("DayRain", "Rainy", 0.7f, 3.0f);
        EventManager::Instance().createAdvancedWeatherEvent("NightStorm", "Stormy", 0.9f, 2.0f, 8, 30.0f, false, true);
        
        EventManager::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);
        EventManager::Instance().createAdvancedSceneChangeEvent("ToBattle", "BattleScene", "dissolve", 1.5f, 7, false);
        
        EventManager::Instance().createNPCSpawnEvent("Guards", "Guard", 3, 50.0f);
        EventManager::Instance().createAdvancedNPCSpawnEvent("Villagers", "Villager", 8, 40.0f, 5, false);
    }
    
    void update() {
        if (!m_initialized) return;
        
        // Single call to EventManager processes everything
        EventManager::Instance().update();
        
        // Monitor performance through EventManager
        static int frameCount = 0;
        if (++frameCount % 300 == 0) { // Every 5 seconds at 60fps
            monitorPerformance();
        }
    }
    
    void handleWeatherEvent(const EventData& data) {
        std::cout << "Weather event triggered!" << std::endl;
        // Update weather system, lighting, particles, etc.
    }
    
    void handleSceneChange(const EventData& data) {
        std::cout << "Scene changing..." << std::endl;
        // Handle scene transition logic
    }
    
    void handleNPCSpawn(const EventData& data) {
        std::cout << "NPCs spawned!" << std::endl;
        // Create and initialize NPCs
    }
    
    void monitorPerformance() {
        // Performance monitoring through EventManager
        size_t totalEvents = EventManager::Instance().getEventCount();
        std::cout << "Total events: " << totalEvents << std::endl;
        
        auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        std::cout << "Weather events: " << weatherStats.avgTime << "ms avg" << std::endl;
        
        if (totalEvents > 1000) {
            std::cout << "High event count - optimizing through EventManager" << std::endl;
            EventManager::Instance().compactEventStorage();
        }
    }
};
```

### Dynamic Event Management
```cpp
class DynamicEventManager {
public:
    void createRandomEvents() {
        // All creation through EventManager
        std::vector<std::string> weatherTypes = {"Clear", "Cloudy", "Rainy", "Stormy"};
        for (const auto& weather : weatherTypes) {
            std::string eventName = weather + "Weather" + std::to_string(rand() % 100);
            EventManager::Instance().createWeatherEvent(eventName, weather, 
                                                       0.3f + (rand() % 70) / 100.0f, 
                                                       2.0f + (rand() % 60) / 10.0f);
        }
        
        // Random NPC spawning through EventManager
        if (shouldSpawnRandomNPC()) {
            std::string npcType = getRandomNPCType();
            EventManager::Instance().spawnNPC(npcType, 
                                             getPlayerX() + (rand() % 200 - 100), 
                                             getPlayerY() + (rand() % 200 - 100));
        }
    }
    
    void handlePlayerAction(const std::string& action) {
        // All direct triggering through EventManager
        if (action == "weather_spell") {
            EventManager::Instance().changeWeather("Stormy", 1.5f);
        } else if (action == "teleport") {
            EventManager::Instance().changeScene("RandomLocation", "dissolve", 2.0f);
        } else if (action == "summon_help") {
            EventManager::Instance().spawnNPC("Ally", getPlayerX(), getPlayerY());
        }
    }
    
    void createEventSequence() {
        // Event sequences through EventManager
        std::vector<std::string> storyEvents = {"StartRain", "GetStormy", "ClearUp"};
        EventManager::Instance().createEventSequence("WeatherStory", storyEvents, true);
        
        // Weather sequence through EventManager
        std::vector<std::tuple<std::string, std::string, float, float>> weatherSequence = {
            {"MorningMist", "Foggy", 0.3f, 4.0f},
            {"NoonStorm", "Stormy", 0.8f, 2.0f},
            {"EveningClear", "Clear", 1.0f, 3.0f}
        };
        EventManager::Instance().createWeatherSequence("DailyWeather", weatherSequence, true);
    }
    
private:
    bool shouldSpawnRandomNPC() { return rand() % 100 < 20; } // 20% chance
    std::string getRandomNPCType() { 
        std::vector<std::string> types = {"Guard", "Villager", "Merchant"};
        return types[rand() % types.size()];
    }
    float getPlayerX() { return 100.0f; } // Placeholder
    float getPlayerY() { return 100.0f; } // Placeholder
};
```

### Custom Event Integration
```cpp
class CustomEventExample {
public:
    void setupCustomEvents() {
        // Register custom event type with EventManager
        EventManager::Instance().registerCustomEventType("Quest", 
            [](const std::string& name, 
               const std::unordered_map<std::string, std::string>& params,
               const std::unordered_map<std::string, float>& numParams,
               const std::unordered_map<std::string, bool>& boolParams) -> EventPtr {
                std::string questId = params.count("questId") ? params.at("questId") : "";
                std::string objective = params.count("objective") ? params.at("objective") : "";
                int reward = static_cast<int>(numParams.count("reward") ? numParams.at("reward") : 0.0f);
                return std::make_shared<QuestEvent>(name, questId, objective, reward);
            });
        
        // Register handler for custom events
        EventManager::Instance().registerHandler(EventTypeId::Custom,
            [this](const EventData& data) {
                handleQuestEvent(data);
            });
    }
    
    void createQuests() {
        // Create custom events through EventManager
        std::unordered_map<std::string, std::string> questParams = {
            {"questId", "treasure_hunt"},
            {"objective", "Find the hidden treasure"}
        };
        std::unordered_map<std::string, float> questNumParams = {
            {"reward", 1000.0f}
        };
        std::unordered_map<std::string, bool> questBoolParams = {
            {"repeatable", false}
        };
        
        EventManager::Instance().createCustomEvent("Quest", "FindTreasure", 
                                                  questParams, questNumParams, questBoolParams);
    }
    
    void handleQuestEvent(const EventData& data) {
        std::cout << "Quest event triggered!" << std::endl;
        // Handle quest logic
    }
};
```

---

**Key Takeaway**: EventManager is your single interface for all event operations. It handles creation, execution, threading, performance, and cleanup internally. Never access other event components directly - EventManager provides all the functionality you need through a clean, unified API.

For quick API reference, see [EventManager Quick Reference](EventManager_QuickReference.md).
For advanced topics like detailed threading integration and performance optimization, see [EventManager Advanced](EventManager_Advanced.md).
For comprehensive code examples, see [EventManager Examples](EventManager_Examples.cpp).