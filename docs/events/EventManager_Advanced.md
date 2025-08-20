# EventManager Advanced Documentation

Note: API examples that reference “createAdvanced*” methods on EventManager reflect older drafts. In the current architecture, advanced/definition-driven creation should use `EventFactory` (see EventManager.md and EventManager_QuickReference.md). Trigger helpers and handler/dispatch semantics described here remain valid.

**Where to find the code:**
- Implementation: `src/managers/EventManager.cpp`
- Header: `include/managers/EventManager.hpp`

**Singleton Access:** Use `EventManager::Instance()` to access the manager.

## Overview

This document covers advanced EventManager topics including detailed threading integration, performance optimization, complex event patterns, and architectural considerations. The EventManager is the single source of truth for all operations discussed here.

## Current API Snapshot

For accuracy, the public EventManager API relevant to this document includes:

- Initialization: `init()`, `isInitialized()`, `clean()`, `prepareForStateTransition()`, `update()`, `isShutdown()`
- Registration: `registerEvent(...)`, and typed registrations for Weather/SceneChange/NPCSpawn/ResourceChange/World/Camera
- Convenience creation: `createWeatherEvent(...)`, `createSceneChangeEvent(...)`, `createNPCSpawnEvent(...)`, particle/world/camera helpers
- Triggers (no pre-registration): `changeWeather(...)`, `changeScene(...)`, `spawnNPC(...)`, `triggerParticleEffect(...)`, `triggerWorld*`, `triggerCamera*`, `triggerResourceChange(...)`
- Handlers: type-safe `registerHandler(...)`, token-based `registerHandlerWithToken(...)`, per-name `registerHandlerForName(...)`, `removeHandler(token)`, `removeNameHandlers(...)`, `clearAllHandlers()`
- Queries/management: `getEvent(...)`, `getEventsByType(...)`, `setEventActive(...)`, `isEventActive(...)`, `removeEvent(...)`, `hasEvent(...)`
- Batch helpers: `updateWeatherEvents()`, `updateSceneChangeEvents()`, `updateNPCSpawnEvents()`, `updateResourceChangeEvents()`, `updateWorldEvents()`, `updateCameraEvents()`, `updateHarvestEvents()`, `updateCustomEvents()`
- Performance/memory: `getPerformanceStats(...)`, `resetPerformanceStats()`, `getEventCount(...)`, `compactEventStorage()`, `clearEventPools()`

Custom and definition-driven creation flows should use `EventFactory` via `EventFactory::Instance().createEvent(def)` and `registerCustomEventCreator(...)`.

Migration note: earlier drafts referenced `createAdvanced*` and `createCustomEvent` methods on EventManager. Those have been replaced by the convenience methods above and by EventFactory definitions for advanced scenarios.

## Table of Contents

- [Advanced Threading Architecture](#advanced-threading-architecture)
- [Performance Optimization](#performance-optimization)
- [Complex Event Patterns](#complex-event-patterns)
- [Game State Integration](#game-state-integration)
- [Memory Management](#memory-management)
- [Debugging and Profiling](#debugging-and-profiling)
- [Architecture Patterns](#architecture-patterns)
- [Troubleshooting](#troubleshooting)

## Advanced Threading Architecture

### ThreadSystem Integration Details

The EventManager integrates deeply with the ThreadSystem using WorkerBudget allocation and queue pressure monitoring:

```cpp
// EventManager automatically allocates 30% of available worker threads
// Buffer threads are used when event count exceeds thresholds (100+ events)
// Queue pressure monitoring prevents ThreadSystem overload

class EventManager {
private:
    // Threading with queue pressure monitoring
    void updateEventTypeBatchThreaded(EventTypeId typeId) {
        auto& threadSystem = ThreadSystem::Instance();

        // Check queue pressure before submitting tasks
        size_t queueSize = threadSystem.getQueueSize();
        size_t queueCapacity = threadSystem.getQueueCapacity();
        size_t pressureThreshold = (queueCapacity * 9) / 10; // 90% threshold

        if (queueSize > pressureThreshold) {
            // Graceful degradation: fallback to single-threaded processing
            updateEventTypeBatch(typeId);
            return;
        }

        // WorkerBudget allocation with buffer scaling
        size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
        WorkerBudget budget = calculateWorkerBudget(availableWorkers);
        size_t optimalWorkerCount = budget.getOptimalWorkerCount(budget.eventAllocated, eventCount, 100);

        // Dynamic batch sizing based on queue pressure
        double queuePressure = static_cast<double>(queueSize) / queueCapacity;
        size_t minEventsPerBatch = (queuePressure > 0.5) ? 15 : 8;
        size_t maxBatches = (queuePressure > 0.5) ? 2 : 4;
    }

    bool shouldUseThreading() const {
        return m_threadingEnabled &&
               getTotalEventCount() > 50 && // Lower threshold for events
               ThreadSystem::Instance().hasAvailableWorkers();
    }
};
```

### Dynamic Threading Adjustment with Queue Pressure

EventManager automatically adjusts threading based on system load and queue pressure:

```cpp
void EventManager::update() {
    size_t eventCount = getTotalEventCount();

    // Queue pressure aware threading decisions
    if (ThreadSystem::Exists()) {
        auto& threadSystem = ThreadSystem::Instance();
        size_t queueSize = threadSystem.getQueueSize();
        size_t queueCapacity = threadSystem.getQueueCapacity();
        double queuePressure = static_cast<double>(queueSize) / queueCapacity;

        // Adjust threading threshold based on queue pressure
        size_t dynamicThreshold = 50; // Base threshold
        if (queuePressure > 0.75) {
            dynamicThreshold = 100; // Higher threshold under pressure
        } else if (queuePressure < 0.25) {
            dynamicThreshold = 30;  // Lower threshold when queue is light
        }

        // Use threading for medium+ event counts with queue coordination
        if (m_threadingEnabled && eventCount > dynamicThreshold) {
            updateAllTypesThreaded();
        } else {
            updateAllTypesSingleThreaded();
        }
    }
}
```

### Thread Safety Guarantees with Queue Coordination

EventManager provides thread-safe operations with ThreadSystem coordination:

```cpp
// Lock-free event counting with queue pressure awareness
std::atomic<size_t> getEventCount() const {
    std::atomic<size_t> total{0};
    for (const auto& typeContainer : m_eventStorage) {
        total += typeContainer.size();
    }
    return total;
}

// Minimal locking for handler invocation with queue pressure monitoring
void invokeHandlers(EventTypeId typeId, const EventData& data) {
    // Check queue pressure before potentially adding more tasks
    if (ThreadSystem::Exists()) {
        auto& threadSystem = ThreadSystem::Instance();
        size_t queueSize = threadSystem.getQueueSize();
        size_t queueCapacity = threadSystem.getQueueCapacity();

        if (queueSize > (queueCapacity * 8) / 10) { // 80% threshold for handlers
            // Process handlers synchronously under pressure
            std::shared_lock<std::shared_mutex> lock(m_handlersMutex);
            auto it = m_handlers.find(typeId);
            if (it != m_handlers.end()) {
                for (const auto& handler : it->second) {
                    handler(data);
                }
            }
            return;
        }
    }

    // Normal asynchronous handler processing
    std::shared_lock<std::shared_mutex> lock(m_handlersMutex);
    auto it = m_handlers.find(typeId);
    if (it != m_handlers.end()) {
        for (const auto& handler : it->second) {
            handler(data);
        }
    }
}
```

## Performance Optimization

### Event Storage Optimization

EventManager uses type-indexed storage for optimal cache performance:

```cpp
class EventManager {
private:
    // Type-indexed storage for cache-friendly access
    std::array<std::vector<EventData>, static_cast<size_t>(EventTypeId::COUNT)> m_eventStorage;

    // Pre-allocated pools for common event types
    std::unordered_map<EventTypeId, std::queue<EventPtr>> m_eventPools;

public:
    void compactEventStorage() {
        for (auto& typeContainer : m_eventStorage) {
            // Remove inactive events and compact storage
            typeContainer.erase(
                std::remove_if(typeContainer.begin(), typeContainer.end(),
                    [](const EventData& data) {
                        return data.flags & EventData::FLAG_PENDING_REMOVAL;
                    }),
                typeContainer.end()
            );
            typeContainer.shrink_to_fit();
        }
    }
};
```

### Batch Processing Patterns

```cpp
// EventManager automatically batches similar operations
void EventManager::processBatchedWeatherEvents() {
    auto& weatherEvents = m_eventStorage[static_cast<size_t>(EventTypeId::Weather)];

    if (weatherEvents.empty()) return;

    // Group by weather type for efficient processing
    std::unordered_map<std::string, std::vector<EventData*>> weatherGroups;
    for (auto& eventData : weatherEvents) {
        if (eventData.isActive()) {
            auto weatherEvent = std::dynamic_pointer_cast<WeatherEvent>(eventData.event);
            if (weatherEvent) {
                weatherGroups[weatherEvent->getWeatherType()].push_back(&eventData);
            }
        }
    }

    // Process each weather type as a batch
    for (auto& [weatherType, eventGroup] : weatherGroups) {
        processSingleWeatherTypeBatch(weatherType, eventGroup);
    }
}
```

### Performance Monitoring Integration

```cpp
class AdvancedPerformanceMonitor {
private:
    EventManager& m_eventManager;

public:
    void analyzeEventPerformance() {
        // Collect detailed statistics
        PerformanceReport report;

        for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
            EventTypeId typeId = static_cast<EventTypeId>(i);
            auto stats = m_eventManager.getPerformanceStats(typeId);

            report.typeStats[typeId] = stats;

            // Identify performance bottlenecks
            if (stats.avgTime > 5.0) {
                report.bottlenecks.push_back({typeId, stats.avgTime});
            }
        }

        // Generate optimization recommendations
        generateOptimizationSuggestions(report);
    }

private:
    void generateOptimizationSuggestions(const PerformanceReport& report) {
        for (const auto& bottleneck : report.bottlenecks) {
            switch (bottleneck.typeId) {
                case EventTypeId::Weather:
                    if (bottleneck.avgTime > 10.0) {
                        std::cout << "Consider reducing weather event frequency or complexity" << std::endl;
                    }
                    break;
                case EventTypeId::NPCSpawn:
                    if (bottleneck.avgTime > 8.0) {
                        std::cout << "Consider batching NPC spawning operations" << std::endl;
                    }
                    break;
            }
        }
    }
};
```

## Complex Event Patterns

### Event State Machines

```cpp
class WeatherStateMachine {
private:
    EventManager& m_eventManager;
    std::string m_currentWeather = "Clear";
    float m_stateTimer = 0.0f;

public:
    void update(float deltaTime) {
        m_stateTimer += deltaTime;

        // State transitions based on time and conditions
        if (m_currentWeather == "Clear" && m_stateTimer > 300.0f) { // 5 minutes
            transitionToWeather("Cloudy", 2.0f);
        } else if (m_currentWeather == "Cloudy" && m_stateTimer > 180.0f) { // 3 minutes
            float random = static_cast<float>(rand()) / RAND_MAX;
            if (random < 0.3f) {
                transitionToWeather("Rainy", 3.0f);
            } else if (random < 0.1f) {
                transitionToWeather("Stormy", 2.0f);
            }
        }
    }

private:
    void transitionToWeather(const std::string& newWeather, float transitionTime) {
        // Create transition event via EventFactory and register with priority
        std::string transitionName = "Transition_" + m_currentWeather + "_to_" + newWeather;
        EventDefinition def{.type="Weather", .name=transitionName,
            .params={{"weatherType", newWeather}},
            .numParams={{"intensity", getWeatherIntensity(newWeather)}, {"transitionTime", transitionTime}}};
        if (auto ev = EventFactory::Instance().createEvent(def)) {
            ev->setPriority(7);
            m_eventManager.registerEvent(def.name, ev);
        }

        m_eventManager.executeEvent(transitionName);

        m_currentWeather = newWeather;
        m_stateTimer = 0.0f;
    }

    float getWeatherIntensity(const std::string& weather) {
        if (weather == "Clear") return 1.0f;
        if (weather == "Cloudy") return 0.6f;
        if (weather == "Rainy") return 0.4f;
        if (weather == "Stormy") return 0.2f;
        return 0.8f;
    }
};
```

### Conditional Event Chains

```cpp
class ConditionalEventChain {
private:
    EventManager& m_eventManager;
    std::unordered_map<std::string, std::function<bool()>> m_conditions;

public:
    void setupQuestChain() {
        // Register conditions
        m_conditions["player_in_town"] = []() { return getPlayerLocation() == "Town"; };
        m_conditions["has_sword"] = []() { return getPlayerInventory().hasItem("Sword"); };
        m_conditions["night_time"] = []() { return getTimeOfDay() > 20.0f || getTimeOfDay() < 6.0f; };

        // Register conditional handler
        m_eventManager.registerHandler(EventTypeId::Custom,
            [this](const EventData& data) {
                handleConditionalEvent(data);
            });

        // Create conditional events
        createConditionalEvent("town_quest_start", {"player_in_town"},
                              "Quest", {{"questId", "town_mystery"}});

        createConditionalEvent("night_encounter", {"night_time", "player_in_town"},
                              "NPCSpawn", {{"npcType", "MysteriousStranger"}});
    }

private:
    void createConditionalEvent(const std::string& name,
                               const std::vector<std::string>& conditions,
                               const std::string& eventType,
                               const std::unordered_map<std::string, std::string>& params) {

        // Store condition information in custom event
        std::unordered_map<std::string, std::string> conditionalParams = params;
        conditionalParams["event_type"] = eventType;
        conditionalParams["conditions"] = joinStrings(conditions, ",");

        // Build and register custom event via EventFactory
        EventDefinition def{.type="Conditional", .name=name, .params=conditionalParams};
        if (auto ev = EventFactory::Instance().createEvent(def)) {
            m_eventManager.registerEvent(def.name, ev);
        }
    }

    void handleConditionalEvent(const EventData& data) {
        auto event = std::dynamic_pointer_cast<ConditionalEvent>(data.event);
        if (!event) return;

        // Check all conditions
        std::string conditionsStr = event->getParameter("conditions");
        auto conditions = splitString(conditionsStr, ',');

        bool allConditionsMet = true;
        for (const auto& condition : conditions) {
            if (m_conditions.find(condition) != m_conditions.end()) {
                if (!m_conditions[condition]()) {
                    allConditionsMet = false;
                    break;
                }
            }
        }

        if (allConditionsMet) {
            executeConditionalEvent(event);
        }
    }

    void executeConditionalEvent(std::shared_ptr<ConditionalEvent> event) {
        std::string eventType = event->getParameter("event_type");

        if (eventType == "Quest") {
            std::string questId = event->getParameter("questId");
            // Trigger quest via EventFactory + registration
            EventDefinition q{.type="Quest", .name=questId + "_active",
                              .params={{"questId", questId}}, .numParams={{"reward", 500.0f}}};
            if (auto ev = EventFactory::Instance().createEvent(q)) {
                m_eventManager.registerEvent(q.name, ev);
            }
        } else if (eventType == "NPCSpawn") {
            std::string npcType = event->getParameter("npcType");
            m_eventManager.spawnNPC(npcType, getPlayerX(), getPlayerY());
        }
    }
};
```

### Event Priority Systems

```cpp
class PriorityEventManager {
private:
    EventManager& m_eventManager;

public:
    void createPriorityBasedEvents() {
        // Critical event (priority 10)
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="Weather", .name="Emergency_Storm",
                 .params={{"weatherType","Stormy"}}, .numParams={{"intensity",1.0f},{"transitionTime",0.5f}}})) {
            ev->setPriority(10);
            ev->setOneTime(true);
            m_eventManager.registerEvent("Emergency_Storm", ev);
        }

        // High priority events (9 and 8)
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="SceneChange", .name="Combat_Start",
                 .params={{"targetScene","BattleScene"},{"transitionType","fade"}}, .numParams={{"duration",0.8f}}})) {
            ev->setPriority(9);
            m_eventManager.registerEvent("Combat_Start", ev);
        }
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="NPCSpawn", .name="Boss_Spawn",
                 .params={{"npcType","DragonBoss"}}, .numParams={{"count",1.0f},{"spawnRadius",0.0f}}})) {
            ev->setPriority(8);
            m_eventManager.registerEvent("Boss_Spawn", ev);
        }

        // Normal priority events (6 and 5)
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="Weather", .name="Weather_Change",
                 .params={{"weatherType","Rainy"}}, .numParams={{"intensity",0.7f},{"transitionTime",3.0f}}})) {
            ev->setPriority(6);
            m_eventManager.registerEvent("Weather_Change", ev);
        }
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="SceneChange", .name="Area_Transition",
                 .params={{"targetScene","ForestScene"},{"transitionType","dissolve"}}, .numParams={{"duration",2.0f}}})) {
            ev->setPriority(5);
            m_eventManager.registerEvent("Area_Transition", ev);
        }

        // Low priority spawn (3)
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="NPCSpawn", .name="Ambient_NPCs",
                 .params={{"npcType","Villager"}}, .numParams={{"count",5.0f},{"spawnRadius",50.0f}}})) {
            ev->setPriority(3);
            m_eventManager.registerEvent("Ambient_NPCs", ev);
        }

        // Ambient weather (default priority)
        m_eventManager.createWeatherEvent("Ambient_Weather", "Cloudy", 0.8f, 8.0f);
    }

    void monitorPriorityExecution() {
        // Events are automatically processed by priority within EventManager
        // Monitor execution order
        m_eventManager.registerHandler(EventTypeId::Weather,
            [this](const EventData& data) {
                std::cout << "Weather event executed with priority: " << data.priority << std::endl;
            });
    }
};
```

## Game State Integration

### State-Aware Event Management

```cpp
class GameStateEventManager {
private:
    EventManager& m_eventManager;
    std::string m_currentState;
    std::unordered_map<std::string, std::vector<std::string>> m_stateEvents;

public:
    void setupStateBasedEvents() {
        // Define events for each game state
        m_stateEvents["MainMenu"] = {"MenuMusic", "MenuParticles"};
        m_stateEvents["GameWorld"] = {"AmbientWeather", "RandomNPCs", "DynamicMusic"};
        m_stateEvents["BattleState"] = {"BattleMusic", "CombatEffects", "EnemySpawns"};
        m_stateEvents["CutsceneState"] = {"CutsceneAudio", "SequentialEvents"};

        // Create events for all states
        createStateEvents();
    }

    void transitionToState(const std::string& newState) {
        // Deactivate current state events
        deactivateStateEvents(m_currentState);

        // Create scene change event via EventFactory and register
        std::string name = "StateTransition_" + m_currentState + "_to_" + newState;
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="SceneChange", .name=name,
                 .params={{"targetScene",newState},{"transitionType","fade"}}, .numParams={{"duration",1.5f}}})) {
            ev->setPriority(8);
            m_eventManager.registerEvent(name, ev);
        }

        // Activate new state events
        activateStateEvents(newState);

        m_currentState = newState;
    }

private:
    void createStateEvents() {
        for (const auto& [state, events] : m_stateEvents) {
            for (const auto& eventName : events) {
                createEventForState(state, eventName);
            }
        }
    }

    void createEventForState(const std::string& state, const std::string& eventName) {
        if (state == "GameWorld") {
            if (eventName == "AmbientWeather") {
                if (auto ev = EventFactory::Instance().createEvent(
                        {.type="Weather", .name=eventName,
                         .params={{"weatherType","Clear"}}, .numParams={{"intensity",1.0f},{"transitionTime",5.0f}}})) {
                    ev->setPriority(4);
                    m_eventManager.registerEvent(eventName, ev);
                }
            } else if (eventName == "RandomNPCs") {
                if (auto ev = EventFactory::Instance().createEvent(
                        {.type="NPCSpawn", .name=eventName,
                         .params={{"npcType","Villager"}}, .numParams={{"count",3.0f},{"spawnRadius",100.0f}}})) {
                    ev->setPriority(3);
                    m_eventManager.registerEvent(eventName, ev);
                }
            }
        } else if (state == "BattleState") {
            if (eventName == "EnemySpawns") {
                if (auto ev = EventFactory::Instance().createEvent(
                        {.type="NPCSpawn", .name=eventName,
                         .params={{"npcType","Enemy"}}, .numParams={{"count",5.0f},{"spawnRadius",80.0f}}})) {
                    ev->setPriority(7);
                    m_eventManager.registerEvent(eventName, ev);
                }
            }
        }
        // More state-specific event creation...
    }

    void activateStateEvents(const std::string& state) {
        if (m_stateEvents.find(state) != m_stateEvents.end()) {
            for (const auto& eventName : m_stateEvents[state]) {
                m_eventManager.setEventActive(eventName, true);
            }
        }
    }

    void deactivateStateEvents(const std::string& state) {
        if (m_stateEvents.find(state) != m_stateEvents.end()) {
            for (const auto& eventName : m_stateEvents[state]) {
                m_eventManager.setEventActive(eventName, false);
            }
        }
    }
};
```

### Cross-State Event Communication

```cpp
class CrossStateEventBridge {
private:
    EventManager& m_eventManager;
    std::unordered_map<std::string, std::string> m_stateTransitions;

public:
    void setupCrossStateEvents() {
        // Define state transition triggers
        m_stateTransitions["player_dies"] = "GameOverState";
        m_stateTransitions["level_complete"] = "VictoryState";
        m_stateTransitions["pause_game"] = "PauseState";

        // Register cross-state handler
        m_eventManager.registerHandler(EventTypeId::Custom,
            [this](const EventData& data) {
                handleCrossStateEvent(data);
            });

        // Create bridge events
        createBridgeEvents();
    }

private:
    void createBridgeEvents() {
        for (const auto& [trigger, targetState] : m_stateTransitions) {
            std::unordered_map<std::string, std::string> params = {
                {"trigger", trigger},
                {"target_state", targetState}
            };

            // Create via EventFactory and register
            EventDefinition def{.type="StateBridge", .name=trigger + "_bridge", .params=params};
            if (auto ev = EventFactory::Instance().createEvent(def)) {
                m_eventManager.registerEvent(def.name, ev);
            }
        }
    }

    void handleCrossStateEvent(const EventData& data) {
        auto bridgeEvent = std::dynamic_pointer_cast<StateBridgeEvent>(data.event);
        if (!bridgeEvent) return;

        std::string trigger = bridgeEvent->getParameter("trigger");
        std::string targetState = bridgeEvent->getParameter("target_state");

        // Trigger state transition with appropriate scene change
        std::string transitionType = getTransitionTypeForState(targetState);
        float transitionTime = getTransitionTimeForState(targetState);

        std::string name = "CrossState_" + trigger;
        if (auto ev = EventFactory::Instance().createEvent(
                {.type="SceneChange", .name=name,
                 .params={{"targetScene",targetState},{"transitionType",transitionType}}, .numParams={{"duration",transitionTime}}})) {
            ev->setPriority(9);
            m_eventManager.registerEvent(name, ev);
        }

        m_eventManager.executeEvent(name);
    }

    std::string getTransitionTypeForState(const std::string& state) {
        if (state == "GameOverState") return "fade";
        if (state == "VictoryState") return "dissolve";
        if (state == "PauseState") return "slide";
        return "fade";
    }

    float getTransitionTimeForState(const std::string& state) {
        if (state == "GameOverState") return 2.0f;
        if (state == "VictoryState") return 3.0f;
        if (state == "PauseState") return 0.5f;
        return 1.5f;
    }
};
```

## Memory Management

### Advanced Memory Optimization

```cpp
class EventMemoryManager {
private:
    EventManager& m_eventManager;
    size_t m_memoryThreshold;

public:
    EventMemoryManager(EventManager& manager)
        : m_eventManager(manager), m_memoryThreshold(1024 * 1024) {} // 1MB threshold

    void optimizeMemoryUsage() {
        // Monitor memory usage
        size_t currentMemory = estimateEventMemoryUsage();

        if (currentMemory > m_memoryThreshold) {
            performMemoryOptimization();
        }
    }

private:
    size_t estimateEventMemoryUsage() {
        size_t totalMemory = 0;

        for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
            EventTypeId typeId = static_cast<EventTypeId>(i);
            size_t typeCount = m_eventManager.getEventCount(typeId);

            // Estimate memory per event type
            size_t memoryPerEvent = getMemoryEstimateForType(typeId);
            totalMemory += typeCount * memoryPerEvent;
        }

        return totalMemory;
    }

    size_t getMemoryEstimateForType(EventTypeId typeId) {
        switch (typeId) {
            case EventTypeId::Weather: return 256; // bytes
            case EventTypeId::SceneChange: return 512;
            case EventTypeId::NPCSpawn: return 384;
            case EventTypeId::Custom: return 1024;
            default: return 256;
        }
    }

    void performMemoryOptimization() {
        std::cout << "Performing event memory optimization..." << std::endl;

        // Compact event storage
        m_eventManager.compactEventStorage();

        // Remove one-time events that have been executed
        removeExecutedOneTimeEvents();

        // Optimize event pools
        optimizeEventPools();
    }

    void removeExecutedOneTimeEvents() {
        // EventManager handles this internally, but we can suggest cleanup
        std::cout << "Suggesting cleanup of executed one-time events" << std::endl;
    }

    void optimizeEventPools() {
        // Clear unused event pools periodically
        static int optimizationCounter = 0;
        if (++optimizationCounter % 100 == 0) {
            m_eventManager.clearEventPools();
        }
    }
};
```

## Debugging and Profiling

### Event System Profiler

```cpp
class EventSystemProfiler {
private:
    EventManager& m_eventManager;
    std::unordered_map<EventTypeId, std::vector<float>> m_executionTimes;
    bool m_profilingEnabled = false;

public:
    void enableProfiling() {
        m_profilingEnabled = true;

        // Register profiling handlers for each event type
        for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
            EventTypeId typeId = static_cast<EventTypeId>(i);

            m_eventManager.registerHandler(typeId,
                [this, typeId](const EventData& data) {
                    profileEventExecution(typeId, data);
                });
        }
    }

    void generatePerformanceReport() {
        std::cout << "=== Event System Performance Report ===" << std::endl;

        for (const auto& [typeId, times] : m_executionTimes) {
            if (times.empty()) continue;

            float totalTime = std::accumulate(times.begin(), times.end(), 0.0f);
            float avgTime = totalTime / times.size();
            float minTime = *std::min_element(times.begin(), times.end());
            float maxTime = *std::max_element(times.begin(), times.end());

            std::cout << "Event Type " << static_cast<int>(typeId) << ":" << std::endl;
            std::cout << "  Count: " << times.size() << std::endl;
            std::cout << "  Avg Time: " << avgTime << "ms" << std::endl;
            std::cout << "  Min Time: " << minTime << "ms" << std::endl;
            std::cout << "  Max Time: " << maxTime << "ms" << std::endl;
            std::cout << "  Total Time: " << totalTime << "ms" << std::endl;
            std::cout << std::endl;
        }

        // Generate recommendations
        generateOptimizationRecommendations();
    }

private:
    void profileEventExecution(EventTypeId typeId, const EventData& data) {
        if (!m_profilingEnabled) return;

        auto startTime = std::chrono::high_resolution_clock::now();

        // Measure execution time (this would normally be in the actual handler)
        std::this_thread::sleep_for(std::chrono::microseconds(rand() % 1000)); // Simulate work

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        float executionTime = duration.count() / 1000.0f; // Convert to milliseconds
        m_executionTimes[typeId].push_back(executionTime);
    }

    void generateOptimizationRecommendations() {
        std::cout << "=== Optimization Recommendations ===" << std::endl;

        for (const auto& [typeId, times] : m_executionTimes) {
            if (times.empty()) continue;

            float avgTime = std::accumulate(times.begin(), times.end(), 0.0f) / times.size();

            if (avgTime > 5.0f) {
                std::cout << "Event Type " << static_cast<int>(typeId)
                         << " has high average execution time (" << avgTime << "ms)" << std::endl;
                std::cout << "  - Consider optimizing event handlers" << std::endl;
                std::cout << "  - Consider reducing event frequency" << std::endl;
                std::cout << "  - Consider enabling threading for this type" << std::endl;
            }

            if (times.size() > 1000) {
                std::cout << "Event Type " << static_cast<int>(typeId)
                         << " has high event count (" << times.size() << ")" << std::endl;
                std::cout << "  - Consider event pooling or batching" << std::endl;
                std::cout << "  - Consider reducing event creation frequency" << std::endl;
            }
        }
    }
};
```

### Event Debug Visualization

```cpp
class EventDebugVisualizer {
private:
    EventManager& m_eventManager;

public:
    void visualizeEventFlow() {
        std::cout << "=== Current Event Flow ===" << std::endl;

        // Display current events by type
        for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
            EventTypeId typeId = static_cast<EventTypeId>(i);
            size_t count = m_eventManager.getEventCount(typeId);

            if (count > 0) {
                std::cout << "Event Type " << static_cast<int>(typeId)
                         << ": " << count << " events" << std::endl;
            }
        }

        // Display threading status
        std::cout << "Threading Enabled: "
                 << (m_eventManager.isThreadingEnabled() ? "Yes" : "No") << std::endl;
    }

    void visualizeEventTimeline() {
        std::cout << "=== Event Timeline Visualization ===" << std::endl;

        // This would show event execution order and timing
        auto weatherStats = m_eventManager.getPerformanceStats(EventTypeId::Weather);
        auto sceneStats = m_eventManager.getPerformanceStats(EventTypeId::SceneChange);
        auto npcStats = m_eventManager.getPerformanceStats(EventTypeId::NPCSpawn);

        std::cout << "Weather Events: " << weatherStats.callCount
                 << " calls, " << weatherStats.avgTime << "ms avg" << std::endl;
        std::cout << "Scene Events: " << sceneStats.callCount
                 << " calls, " << sceneStats.avgTime << "ms avg" << std::endl;
        std::cout << "NPC Events: " << npcStats.callCount
                 << " calls, " << npcStats.avgTime << "ms avg" << std::endl;
    }
};

## Troubleshooting

### Common Issues and Solutions

#### 1. Events Not Triggering
```cpp
void diagnoseEventIssues() {
    // Check if EventManager is initialized
    if (EventManager::Instance().isShutdown()) {
        std::cout << "EventManager is not initialized!" << std::endl;
        return;
    }

    // Check if events exist
    if (!EventManager::Instance().hasEvent("MyEvent")) {
        std::cout << "Event 'MyEvent' does not exist!" << std::endl;
        return;
    }

    // Check if events are active
    if (!EventManager::Instance().isEventActive("MyEvent")) {
        std::cout << "Event 'MyEvent' is not active!" << std::endl;
        EventManager::Instance().setEventActive("MyEvent", true);
    }

    // Check if handlers are registered
    size_t handlerCount = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
    if (handlerCount == 0) {
        std::cout << "No handlers registered for Weather events!" << std::endl;
    }
}
```

#### 2. Performance Issues
```cpp
void diagnosePerformanceIssues() {
    size_t totalEvents = EventManager::Instance().getEventCount();

    if (totalEvents > 1000) {
        std::cout << "High event count detected: " << totalEvents << std::endl;
        std::cout << "Consider enabling threading or optimizing event usage" << std::endl;

        // Enable threading if not already enabled
        if (!EventManager::Instance().isThreadingEnabled()) {
            EventManager::Instance().enableThreading(true);
        }

        // Compact storage
        EventManager::Instance().compactEventStorage();
    }

    // Check individual type performance
    for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
        EventTypeId typeId = static_cast<EventTypeId>(i);
        auto stats = EventManager::Instance().getPerformanceStats(typeId);

        if (stats.avgTime > 5.0) {
            std::cout << "Event Type " << static_cast<int>(typeId)
                     << " has high execution time: " << stats.avgTime << "ms" << std::endl;
        }
    }
}
```

#### 3. Memory Issues
```cpp
void diagnoseMemoryIssues() {
    std::cout << "=== Memory Diagnostic ===" << std::endl;

    // Check event counts
    size_t totalEvents = EventManager::Instance().getEventCount();
    std::cout << "Total events: " << totalEvents << std::endl;

    for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
        EventTypeId typeId = static_cast<EventTypeId>(i);
        size_t count = EventManager::Instance().getEventCount(typeId);

        if (count > 0) {
            std::cout << "Event Type " << static_cast<int>(typeId)
                     << ": " << count << " events" << std::endl;
        }
    }

    // Suggest memory optimization
    if (totalEvents > 500) {
        std::cout << "Consider memory optimization:" << std::endl;
        std::cout << "- Call compactEventStorage() periodically" << std::endl;
        std::cout << "- Remove unused events" << std::endl;
        std::cout << "- Use one-time events where appropriate" << std::endl;
    }
}
```

#### 4. Threading Issues
```cpp
void diagnoseThreadingIssues() {
    std::cout << "=== Threading Diagnostic ===" << std::endl;

    // Check ThreadSystem status
    if (!HammerEngine::ThreadSystem::Instance().isInitialized()) {
        std::cout << "ThreadSystem is not initialized!" << std::endl;
        std::cout << "Call HammerEngine::ThreadSystem::Instance().init() first" << std::endl;
        return;
    }

    // Check EventManager threading status
    bool threadingEnabled = EventManager::Instance().isThreadingEnabled();
    std::cout << "EventManager threading enabled: "
             << (threadingEnabled ? "Yes" : "No") << std::endl;

    if (threadingEnabled) {
        size_t eventCount = EventManager::Instance().getEventCount();
        std::cout << "Current event count: " << eventCount << std::endl;

        if (eventCount < 100) {
            std::cout << "Event count is low - threading may not provide benefits" << std::endl;
        }
    }
}
```

---

**Key Takeaway**: EventManager provides comprehensive advanced features while maintaining its role as the single source of truth. All advanced patterns, optimization techniques, and troubleshooting should go through EventManager's unified interface.

For basic usage, see [EventManager.md](EventManager.md).
For quick reference, see [EventManager Quick Reference](EventManager_QuickReference.md).
For code examples, see [EventManager Examples](EventManager_Examples.cpp).
