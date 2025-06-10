# EventSystem Integration Guide

## Overview

This guide explains how the EventSystem integrates with the Forge Game Engine and provides practical patterns for using events in your game states and systems. The EventSystem consists of EventManager (high-performance event processing) and EventFactory (intelligent event creation).

## Table of Contents

- [Overview](#overview)
- [GameEngine Integration](#gameengine-integration)
- [Using Events in Game States](#using-events-in-game-states)
- [Event Handler Patterns](#event-handler-patterns)
- [Performance Considerations](#performance-considerations)
- [Integration Examples](#integration-examples)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)

## GameEngine Integration

The EventSystem is automatically integrated into the GameEngine lifecycle with no manual setup required.

### Automatic Initialization

```cpp
// GameEngine.cpp - Background thread initialization during startup
bool GameEngine::init() {
    // ... other initialization ...
    
    // Initialize Event Manager in a separate thread - #6
    initTasks.push_back(
        Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
            GAMEENGINE_INFO("Creating Event Manager");
            EventManager& eventMgr = EventManager::Instance();
            if (!eventMgr.init()) {
                GAMEENGINE_CRITICAL("Failed to initialize Event Manager");
                return false;
            }
            GAMEENGINE_INFO("Event Manager initialized successfully");
            return true;
        })
    );
    
    // ... wait for all tasks to complete ...
    
    // Cache manager reference for performance
    mp_eventManager = &EventManager::Instance();
    
    return true;
}
```

### Update Loop Integration

```cpp
// GameEngine.cpp - Every frame in the main update loop
void GameEngine::update(float deltaTime) {
    // ... other updates ...
    
    // Event system - global game events (cached reference access)
    if (mp_eventManager) {
        try {
            mp_eventManager->update();
        } catch (const std::exception& e) {
            std::cerr << "EventManager exception: " << e.what() << std::endl;
        }
    }
    
    // Update game states - states handle their specific system needs
    mp_gameStateManager->update(deltaTime);
}
```

### Automatic Cleanup

```cpp
// GameEngine.cpp - During shutdown
void GameEngine::clean() {
    GAMEENGINE_INFO("Cleaning up Event Manager...");
    EventManager::Instance().clean();
    
    // Clear manager cache references
    mp_eventManager = nullptr;
}
```

## Using Events in Game States

### Basic Game State Integration

```cpp
// MyGameState.hpp
#include "managers/EventManager.hpp"
#include "events/EventFactory.hpp"

class MyGameState : public GameState {
private:
    bool m_eventHandlersRegistered{false};
    
public:
    bool enter() override;
    void exit() override;
    void update(float deltaTime) override;
    
private:
    void setupEventHandlers();
    void createGameEvents();
    void handleWeatherEvent(const EventData& data);
    void handleSceneChange(const EventData& data);
    void handleNPCSpawn(const EventData& data);
};
```

```cpp
// MyGameState.cpp
bool MyGameState::enter() {
    // Set up event handlers for this state
    setupEventHandlers();
    
    // Create state-specific events
    createGameEvents();
    
    return true;
}

void MyGameState::setupEventHandlers() {
    if (m_eventHandlersRegistered) return;
    
    // Register type-safe handlers
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [this](const EventData& data) { handleWeatherEvent(data); });
    
    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [this](const EventData& data) { handleSceneChange(data); });
    
    EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
        [this](const EventData& data) { handleNPCSpawn(data); });
    
    m_eventHandlersRegistered = true;
}

void MyGameState::createGameEvents() {
    // Create and register events using convenience methods
    EventManager::Instance().createWeatherEvent("StateFog", "Foggy", 0.3f, 5.0f);
    EventManager::Instance().createSceneChangeEvent("ExitState", "MainMenu", "fade", 2.0f);
    EventManager::Instance().createNPCSpawnEvent("StateGuards", "Guard", 2, 40.0f);
}

void MyGameState::exit() {
    // Remove state-specific events
    EventManager::Instance().removeEvent("StateFog");
    EventManager::Instance().removeEvent("ExitState");
    EventManager::Instance().removeEvent("StateGuards");
    
    // Handlers remain registered for other states to use
}

void MyGameState::update(float deltaTime) {
    // EventManager is automatically updated by GameEngine
    // Focus on game state specific logic here
    
    // Example: Trigger events based on game state conditions
    static float timeSinceLastWeatherChange = 0.0f;
    timeSinceLastWeatherChange += deltaTime;
    
    if (timeSinceLastWeatherChange > 30.0f) {
        // Trigger random weather change
        EventManager::Instance().changeWeather("Rainy", 3.0f);
        timeSinceLastWeatherChange = 0.0f;
    }
}
```

## Event Handler Patterns

### Type-Safe Handler Registration

```cpp
void MyGameState::setupAdvancedHandlers() {
    // Weather handler with game logic integration
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [this](const EventData& data) {
            // Update weather-dependent game systems
            updateLighting();
            updateParticleEffects();
            updateAudioAmbience();
            
            // Log for debugging
            GAMESTATE_INFO("Weather event processed in " + getStateName());
        });
    
    // Scene change handler with transition management
    EventManager::Instance().registerHandler(EventTypeId::SceneChange,
        [this](const EventData& data) {
            // Prepare for scene transition
            savePlayerState();
            pauseGameSystems();
            startTransitionEffects();
            
            GAMESTATE_INFO("Scene change initiated from " + getStateName());
        });
    
    // NPC spawn handler with game world integration
    EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
        [this](const EventData& data) {
            // Integrate with AI and game world systems
            updateNPCManager();
            refreshMiniMap();
            updateQuestObjectives();
            
            GAMESTATE_INFO("NPC spawn processed in " + getStateName());
        });
}
```

### Conditional Event Handling

```cpp
void MyGameState::setupConditionalHandlers() {
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [this](const EventData& data) {
            // Only handle weather events if we're in the main game world
            if (getStateName() == "GamePlayState") {
                handleGameWorldWeather(data);
            } else if (getStateName() == "BattleState") {
                handleBattleWeather(data);
            }
            // Ignore weather events in menu states
        });
}
```

## Performance Considerations

### Handler Registration Strategy

```cpp
class GameStateManager {
private:
    bool m_globalHandlersRegistered{false};
    
public:
    void initializeGlobalHandlers() {
        if (m_globalHandlersRegistered) return;
        
        // Register handlers once for all states
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) { getCurrentState()->handleWeatherEvent(data); });
        
        EventManager::Instance().registerHandler(EventTypeId::SceneChange,
            [this](const EventData& data) { handleSceneTransition(data); });
        
        m_globalHandlersRegistered = true;
    }
    
    void handleSceneTransition(const EventData& data) {
        // Centralized scene transition logic
        if (auto currentState = getCurrentState()) {
            currentState->prepareForExit();
        }
        
        // Perform actual transition
        transitionToNextState();
    }
};
```

### Efficient Event Creation

```cpp
void MyGameState::createEventsEfficiently() {
    // Batch create similar events
    std::vector<std::tuple<std::string, std::string, float, float>> weatherEvents = {
        {"MorningFog", "Foggy", 0.4f, 5.0f},
        {"AfternoonSun", "Clear", 1.0f, 3.0f},
        {"EveningRain", "Rainy", 0.6f, 4.0f}
    };
    
    for (const auto& [name, type, intensity, time] : weatherEvents) {
        EventManager::Instance().createWeatherEvent(name, type, intensity, time);
    }
    
    // Use direct triggers for one-off events
    EventManager::Instance().changeWeather("Stormy", 2.0f);
    EventManager::Instance().spawnNPC("Merchant", getPlayerX(), getPlayerY());
}
```

## Integration Examples

### Complete Game State with Events

```cpp
class BattleState : public GameState {
private:
    bool m_battleStarted{false};
    float m_battleTimer{0.0f};
    
public:
    bool enter() override {
        setupBattleEvents();
        createBattleAtmosphere();
        return true;
    }
    
    void update(float deltaTime) override {
        m_battleTimer += deltaTime;
        
        // Trigger battle events based on time
        if (!m_battleStarted && m_battleTimer > 2.0f) {
            startBattle();
            m_battleStarted = true;
        }
        
        // End battle after 60 seconds
        if (m_battleStarted && m_battleTimer > 60.0f) {
            endBattle();
        }
    }
    
    void exit() override {
        cleanupBattleEvents();
    }
    
private:
    void setupBattleEvents() {
        EventManager::Instance().registerHandler(EventTypeId::Weather,
            [this](const EventData& data) {
                // Battle-specific weather handling
                adjustBattleVisibility();
                updateCombatEffects();
            });
        
        EventManager::Instance().registerHandler(EventTypeId::NPCSpawn,
            [this](const EventData& data) {
                // Handle enemy spawns
                addEnemyToBattleQueue();
                updateBattleUI();
            });
    }
    
    void createBattleAtmosphere() {
        // Create dramatic battle weather
        EventManager::Instance().createWeatherEvent("BattleStorm", "Stormy", 0.8f, 2.0f);
        
        // Set up enemy waves
        EventManager::Instance().createNPCSpawnEvent("FirstWave", "OrcWarrior", 3, 50.0f);
        EventManager::Instance().createNPCSpawnEvent("SecondWave", "OrcArcher", 2, 60.0f);
    }
    
    void startBattle() {
        // Trigger battle start events
        EventManager::Instance().executeEvent("BattleStorm");
        EventManager::Instance().executeEvent("FirstWave");
        
        GAMESTATE_INFO("Battle started!");
    }
    
    void endBattle() {
        // Trigger victory weather
        EventManager::Instance().changeWeather("Clear", 3.0f);
        
        // Transition to victory screen
        EventManager::Instance().changeScene("VictoryScreen", "fade", 2.0f);
    }
    
    void cleanupBattleEvents() {
        EventManager::Instance().removeEvent("BattleStorm");
        EventManager::Instance().removeEvent("FirstWave");
        EventManager::Instance().removeEvent("SecondWave");
    }
};
```

### Event-Driven Game World

```cpp
class GameWorldState : public GameState {
private:
    float m_timeOfDay{8.0f}; // 8 AM start
    
public:
    void update(float deltaTime) override {
        // Update time of day
        m_timeOfDay += deltaTime / 60.0f; // 1 real second = 1 game minute
        if (m_timeOfDay >= 24.0f) {
            m_timeOfDay = 0.0f;
        }
        
        // Trigger time-based events
        handleTimeOfDayEvents();
        
        // Handle player actions
        handlePlayerEventTriggers();
    }
    
private:
    void handleTimeOfDayEvents() {
        static float lastHour = -1.0f;
        int currentHour = static_cast<int>(m_timeOfDay);
        
        if (currentHour != static_cast<int>(lastHour)) {
            // Hour changed, trigger time-based events
            switch (currentHour) {
                case 6:  // Dawn
                    EventManager::Instance().changeWeather("Clear", 5.0f);
                    break;
                case 12: // Noon
                    EventManager::Instance().spawnNPC("Merchant", 100.0f, 200.0f);
                    break;
                case 18: // Dusk
                    EventManager::Instance().changeWeather("Foggy", 4.0f);
                    break;
                case 22: // Night
                    EventManager::Instance().spawnNPC("NightGuard", 150.0f, 150.0f);
                    break;
            }
            lastHour = m_timeOfDay;
        }
    }
    
    void handlePlayerEventTriggers() {
        // Check for player-triggered events
        if (isPlayerNearLocation("TownEntrance")) {
            static bool townEntered = false;
            if (!townEntered) {
                EventManager::Instance().changeScene("TownScene", "fade", 2.0f);
                townEntered = true;
            }
        }
        
        if (isPlayerCastingSpell("RainSpell")) {
            EventManager::Instance().changeWeather("Rainy", 1.5f);
        }
    }
};
```

### Event Sequence Integration

```cpp
class CutsceneState : public GameState {
public:
    bool enter() override {
        createCutsceneSequence();
        return true;
    }
    
private:
    void createCutsceneSequence() {
        // Create a dramatic cutscene sequence
        std::vector<EventDefinition> cutsceneEvents = {
            {"Weather", "DramaticStorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.9f}}, {}},
            {"SceneChange", "CloseUpShot", {{"targetScene", "CloseUpView"}}, {{"duration", 1.0f}}, {}},
            {"NPCSpawn", "Villain", {{"npcType", "FinalBoss"}}, {{"count", 1}}, {}},
            {"SceneChange", "BattleArena", {{"targetScene", "FinalBattle"}}, {{"duration", 2.0f}}, {}}
        };
        
        auto sequence = EventFactory::Instance().createEventSequence(
            "FinalBossCutscene", cutsceneEvents, true);
        
        // Execute the first event to start the sequence
        if (!sequence.empty()) {
            EventManager::Instance().registerEvent("FinalBossCutscene_0", sequence[0]);
            EventManager::Instance().executeEvent("FinalBossCutscene_0");
        }
    }
};
```

## Best Practices

### 1. Handler Registration Strategy

```cpp
// Good: Register handlers once during initialization
void GameManager::initializeEventSystem() {
    EventManager::Instance().registerHandler(EventTypeId::Weather,
        [this](const EventData& data) { handleGlobalWeatherChange(data); });
    
    // Register state-specific handlers in each state as needed
}

// Avoid: Re-registering handlers repeatedly
void SomeState::update() {
    // Don't do this every frame!
    EventManager::Instance().registerHandler(EventTypeId::Weather, handler);
}
```

### 2. Event Naming Conventions

```cpp
// Good: Descriptive, hierarchical names
EventManager::Instance().createWeatherEvent("Level1_MorningFog", "Foggy", 0.4f, 5.0f);
EventManager::Instance().createSceneChangeEvent("Battle_Victory_Transition", "VictoryScreen", "fade", 2.0f);
EventManager::Instance().createNPCSpawnEvent("Town_Guard_Patrol", "Guard", 3, 50.0f);

// Avoid: Generic or unclear names
EventManager::Instance().createWeatherEvent("Event1", "Rainy", 0.5f, 3.0f);
EventManager::Instance().createSceneChangeEvent("Scene", "SomeScene", "fade", 1.0f);
```

### 3. Event Lifecycle Management

```cpp
class ResponsibleGameState : public GameState {
public:
    bool enter() override {
        // Create events when entering
        EventManager::Instance().createWeatherEvent("StateWeather", "Clear", 1.0f, 3.0f);
        return true;
    }
    
    void exit() override {
        // Clean up events when exiting
        EventManager::Instance().removeEvent("StateWeather");
    }
};
```

### 4. Performance Monitoring

```cpp
void GameManager::monitorEventPerformance() {
    static int frameCount = 0;
    if (++frameCount % 300 == 0) { // Every 5 seconds at 60 FPS
        auto stats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
        if (stats.avgTime > 3.0) {
            GAMEMANAGER_WARN("Weather events slow: " + std::to_string(stats.avgTime) + "ms");
        }
        
        size_t eventCount = EventManager::Instance().getEventCount();
        if (eventCount > 500) {
            GAMEMANAGER_WARN("High event count: " + std::to_string(eventCount));
        }
    }
}
```

## Troubleshooting

### Common Issues and Solutions

#### Events Not Triggering

```cpp
// Check if event exists
if (!EventManager::Instance().hasEvent("MyEvent")) {
    std::cerr << "Event 'MyEvent' was not created!" << std::endl;
}

// Check if event is active
if (!EventManager::Instance().isEventActive("MyEvent")) {
    std::cerr << "Event 'MyEvent' is inactive!" << std::endl;
    EventManager::Instance().setEventActive("MyEvent", true);
}

// Check if handlers are registered
size_t handlerCount = EventManager::Instance().getHandlerCount(EventTypeId::Weather);
if (handlerCount == 0) {
    std::cerr << "No weather handlers registered!" << std::endl;
}
```

#### Performance Issues

```cpp
void diagnosePerformanceIssues() {
    // Check event counts
    size_t totalEvents = EventManager::Instance().getEventCount();
    std::cout << "Total events: " << totalEvents << std::endl;
    
    if (totalEvents > 1000) {
        std::cout << "Consider enabling threading:" << std::endl;
        EventManager::Instance().enableThreading(true);
    }
    
    // Check individual type performance
    auto weatherStats = EventManager::Instance().getPerformanceStats(EventTypeId::Weather);
    if (weatherStats.avgTime > 5.0) {
        std::cout << "Weather events are slow: " << weatherStats.avgTime << "ms" << std::endl;
    }
}
```

#### Memory Issues

```cpp
void optimizeEventMemory() {
    // Compact storage periodically
    static int compactCounter = 0;
    if (++compactCounter % 1000 == 0) {
        EventManager::Instance().compactEventStorage();
    }
    
    // Monitor event counts by type
    for (int i = 0; i < static_cast<int>(EventTypeId::COUNT); ++i) {
        auto typeId = static_cast<EventTypeId>(i);
        size_t count = EventManager::Instance().getEventCount(typeId);
        if (count > 200) {
            std::cout << "High count for type " << i << ": " << count << std::endl;
        }
    }
}
```

#### Threading Issues

```cpp
void handleThreadingIssues() {
    // Ensure ThreadSystem is initialized before EventManager
    if (!Forge::ThreadSystem::Exists()) {
        std::cerr << "ThreadSystem not initialized!" << std::endl;
        Forge::ThreadSystem::Instance().init();
    }
    
    // Check if threading is beneficial
    size_t eventCount = EventManager::Instance().getEventCount();
    bool shouldUseThreading = eventCount > 100;
    
    if (shouldUseThreading && !EventManager::Instance().isThreadingEnabled()) {
        EventManager::Instance().enableThreading(true);
        std::cout << "Enabled threading for " << eventCount << " events" << std::endl;
    }
}
```

---

The EventSystem provides a powerful foundation for game event management with seamless GameEngine integration. By following these patterns and best practices, you can create responsive, event-driven game experiences with excellent performance characteristics.