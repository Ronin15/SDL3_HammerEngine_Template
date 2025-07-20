# EventManager Documentation

**Where to find the code:**
- Implementation: `src/managers/EventManager.cpp`
- Header: `include/managers/EventManager.hpp`

**Singleton Access:** Use `EventManager::Instance()` to access the manager.

## Overview

The Hammer Game Engine EventManager provides a comprehensive, high-performance event management framework as the single source of truth for all event operations. The system features:

1. **Type-indexed storage** - Fast O(1) event lookups using EventTypeId enumeration
2. **Cache-friendly data structures** - Structure of Arrays (SoA) pattern with 32-byte alignment
3. **Queue pressure monitoring** - 90% capacity threshold with graceful degradation to single-threaded processing
4. **WorkerBudget integration** - 30% worker allocation with buffer scaling for high workloads
5. **Dynamic batch sizing** - Adjusts batch size (8-15 events) based on real-time queue pressure
6. **Threading optimization** - Automatic scaling with 50+ event threshold
7. **Performance monitoring** - Built-in statistics tracking per event type
8. **Type-safe handlers** - Fast event handler registration by EventTypeId
9. **Architectural consistency** - Same patterns as AIManager for system harmony
10. **Memory compaction** - Automatic storage optimization and cleanup

The system supports weather events, scene transitions, NPC spawning, and custom events with coordinated ThreadSystem resource management. This design ensures optimal performance while maintaining system stability through intelligent queue pressure management.

## Table of Contents

- [Quick Start](#quick-start)
- [Core Architecture](#core-architecture)
- [Event Types](#event-types)
- [API Reference](#api-reference)
- [Threading & Performance](#threading--performance)
- [Best Practices](#best-practices)
- [Examples](#examples)
- [Memory Management](#memory-management)
- [Performance Monitoring](#performance-monitoring)

## Quick Start

### Basic Setup
```cpp
#include "managers/EventManager.hpp"
#include "core/ThreadSystem.hpp"

// Initialize dependencies
HammerEngine::ThreadSystem::Instance().init();

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
    ParticleEffect = 3, // Particle effect events
    Custom = 4,       // User-defined events
    COUNT = 5         // Total count (internal use)
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

### Particle Effect Events
Control visual effects and particle systems through EventManager with ParticleManager integration:

```cpp
// Simple particle effect creation
EventManager::Instance().createParticleEffectEvent("Explosion", "Fire", 250.0f, 150.0f, 2.0f, 3.0f, "combat");

// Particle effect with Vector2D position
Vector2D position(400.0f, 300.0f);
EventManager::Instance().createParticleEffectEvent("MagicSmoke", "Smoke", position, 1.5f, -1.0f, "magic", "magic_cast");

// Minimal particle effect (position only)
EventManager::Instance().createParticleEffectEvent("Sparks", "Sparks", 100.0f, 200.0f);
```

**Effect Types**: Fire, Smoke, Sparks, Rain, Snow, Fog, Cloudy, Custom
**Features**: Position-based triggering, intensity control, duration settings, group tagging, sound integration

#### Particle Effect Parameters
- **effectName**: Name of the particle effect to trigger (must be registered with ParticleManager)
- **position**: World coordinates where the effect should appear
- **intensity**: Effect intensity multiplier (0.0 to 2.0+, default: 1.0)
- **duration**: Effect duration in seconds (-1 for infinite, default: -1.0)
- **groupTag**: Optional group identifier for batch operations (default: "")
- **soundEffect**: Optional sound effect name for SoundManager integration (default: "")

#### ParticleManager Integration
ParticleEffectEvents automatically integrate with the ParticleManager system:

```cpp
// EventManager coordinates with ParticleManager for effect execution
EventManager::Instance().executeEvent("Explosion"); // Triggers particle effect at specified position

// Check if particle effect is currently active
auto event = EventManager::Instance().getEvent("MagicSmoke");
auto particleEvent = std::dynamic_pointer_cast<ParticleEffectEvent>(event);
if (particleEvent && particleEvent->isEffectActive()) {
    std::cout << "Magic smoke effect is running!" << std::endl;
}
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

#### Event Registration
```cpp
bool registerEvent(const std::string& name, EventPtr event)
bool registerWeatherEvent(const std::string& name, std::shared_ptr<WeatherEvent> event)
bool registerSceneChangeEvent(const std::string& name, std::shared_ptr<SceneChangeEvent> event)
bool registerNPCSpawnEvent(const std::string& name, std::shared_ptr<NPCSpawnEvent> event)
```

#### Event Creation (Factory Methods)
```cpp
bool createWeatherEvent(const std::string& name, const std::string& weatherType,
                       float intensity = 1.0f, float transitionTime = 5.0f)
bool createSceneChangeEvent(const std::string& name, const std::string& targetScene,
                           const std::string& transitionType = "fade", float transitionTime = 1.0f)
bool createNPCSpawnEvent(const std::string& name, const std::string& npcType,
                        int count = 1, float spawnRadius = 0.0f)
bool createParticleEffectEvent(const std::string& name, const std::string& effectName,
                              float x, float y, float intensity = 1.0f, float duration = -1.0f,
                              const std::string& groupTag = "", const std::string& soundEffect = "")
bool createParticleEffectEvent(const std::string& name, const std::string& effectName,
                              const Vector2D& position, float intensity = 1.0f, float duration = -1.0f,
                              const std::string& groupTag = "", const std::string& soundEffect = "")
```

#### Handler Management
```cpp
void registerHandler(EventTypeId typeId, FastEventHandler handler)
void removeHandlers(EventTypeId typeId)
void clearAllHandlers()
size_t getHandlerCount(EventTypeId typeId) const
```

#### Event Execution
```cpp
bool executeEvent(const std::string& eventName) const
int executeEventsByType(EventTypeId typeId) const
int executeEventsByType(const std::string& eventType) const
```

#### Direct Event Triggering
```cpp
bool changeWeather(const std::string& weatherType, float transitionTime = 5.0f) const
bool changeScene(const std::string& sceneId, const std::string& transitionType = "fade", float transitionTime = 1.0f) const
bool spawnNPC(const std::string& npcType, float x, float y) const

// Alternative method names (aliases for compatibility)
bool triggerWeatherChange(const std::string& weatherType, float transitionTime = 5.0f) const
bool triggerSceneChange(const std::string& sceneId, const std::string& transitionType = "fade", float transitionTime = 1.0f) const
bool triggerNPCSpawn(const std::string& npcType, float x, float y) const
```

#### Event Management

```cpp
EventPtr getEvent(const std::string& name) const;
std::vector<EventPtr> getEventsByType(EventTypeId typeId) const;
std::vector<EventPtr> getEventsByType(const std::string& eventType) const;

bool setEventActive(const std::string& name, bool active);
bool isEventActive(const std::string& name) const;
bool removeEvent(const std::string& name);
bool hasEvent(const std::string& name) const
```

#### Threading Control
```cpp
void enableThreading(bool enable)
bool isThreadingEnabled() const
void setThreadingThreshold(size_t threshold)
```

#### Batch Processing
```cpp
void updateWeatherEvents()
void updateSceneChangeEvents()
void updateNPCSpawnEvents()
void updateCustomEvents()
```

#### Performance and Monitoring
```cpp
PerformanceStats getPerformanceStats(EventTypeId typeId) const
void resetPerformanceStats()
size_t getEventCount() const
size_t getEventCount(EventTypeId typeId) const
```

#### Memory Management
```cpp
void compactEventStorage()
void clearEventPools()
void prepareForStateTransition()
```

## Threading & Performance

### Threading Model
EventManager uses intelligent threading decisions with queue pressure monitoring and WorkerBudget integration:

- **Automatic Threading**: Enabled when event count exceeds threshold (50+ events)
- **Queue Pressure Monitoring**: 90% queue capacity threshold with graceful degradation
- **Type-Based Batching**: Events processed by type for optimal cache usage
- **WorkerBudget Integration**: Allocates 30% of available worker threads with buffer allocation
- **Dynamic Batch Sizing**: Adjusts batch size based on real-time queue pressure
- **Lock-Free Operations**: Minimal locking for high-performance concurrent access
- **Graceful Degradation**: Falls back to single-threaded processing under high queue pressure

### Threading Configuration
```cpp
// Threading control through EventManager
EventManager::Instance().enableThreading(true);
bool isThreaded = EventManager::Instance().isThreadingEnabled();

// Set threading threshold (default: 100 events)
EventManager::Instance().setThreadingThreshold(500);
```

### Performance Characteristics
- **Single-threaded**: Optimal for <50 events per frame (automatic fallback)
- **Multi-threaded**: Benefits with 50+ events, optimal with 100+ events
- **Queue Pressure Management**: Prevents ThreadSystem overload through monitoring
- **Dynamic Batch Sizing**: 8-15 events per batch based on queue pressure
- **Memory Efficiency**: Type-indexed storage minimizes cache misses
- **WorkerBudget Coordination**: Proper resource allocation with AIManager and GameEngine
- **Architectural Consistency**: Same patterns as AIManager for system harmony

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

    // Monitor queue pressure for system coordination
    if (HammerEngine::ThreadSystem::Exists()) {
        auto& threadSystem = HammerEngine::ThreadSystem::Instance();
        size_t queueSize = threadSystem.getQueueSize();
        size_t queueCapacity = threadSystem.getQueueCapacity();
        double queuePressure = static_cast<double>(queueSize) / queueCapacity;

        if (queuePressure > 0.75) {
            std::cout << "Warning: High queue pressure ("
                      << static_cast<int>(queuePressure * 100) << "%)" << std::endl;
        }
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
        if (!HammerEngine::ThreadSystem::Instance().init()) {
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
