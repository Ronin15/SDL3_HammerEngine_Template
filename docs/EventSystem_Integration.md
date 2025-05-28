# EventManager Integration Guide

## Quick Reference

This document provides a quick reference for developers on how the EventSystem is integrated into the game engine and how to use it in their own game states.

## GameEngine Integration

The EventSystem is automatically managed by the GameEngine with no additional setup required:

### Initialization
```cpp
// GameEngine.cpp - Thread #6 during startup
// GameEngine.cpp - Initialization (Thread #6)
initTasks.push_back(
    Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        std::cout << "Forge Game Engine - Creating Event Manager\n";
        if (!EventManager::Instance().init()) {
            std::cerr << "Forge Game Engine - Failed to initialize Event Manager!" << std::endl;
            return false;
        }
        std::cout << "Forge Game Engine - Event Manager initialized successfully\n";
        return true;
    }));
```

### Update Loop
```cpp
// GameEngine.cpp - Every frame before game state updates
// GameEngine.cpp - Update (Every Frame)
void GameEngine::update() {
    EventManager::Instance().update();  // Events processed first
    mp_gameStateManager->update();      // Then game states
}
```

### Cleanup
```cpp
// GameEngine.cpp - During shutdown, before AI Manager cleanup
void GameEngine::clean() {
    std::cout << "Forge Game Engine - Cleaning up Event Manager...\n";
    EventManager::Instance().clean();
}
```

## Using EventSystem in Game States

### Basic Usage Pattern

```cpp
// In your GameState header
#include "events/EventSystem.hpp"

class MyGameState : public GameState {
private:
    EventSystem* m_eventSystem{nullptr};
    
public:
    bool enter() override;
    void update() override;
    // ... other methods
};
```

```cpp
// In your GameState implementation
bool MyGameState::enter() {
    // Get EventManager instance (already initialized by GameEngine)
    m_eventManager = &EventManager::Instance();
    
    // Register event handlers
    m_eventManager->registerEventHandler("Weather", 
        [this](const std::string& message) { onWeatherChanged(message); });
    
    // Register specific events
    m_eventManager->registerWeatherEvent("rain_event", "Rainy", 0.8f);
    m_eventManager->registerNPCSpawnEvent("guard_spawn", "Guard", 2, 100.0f);
    
    return true;
}

void MyGameState::update() {
    // EventSystem is automatically updated by GameEngine
    // Your game state logic here
}
```

### Event Handler Registration

```cpp
// Weather events
m_eventSystem->registerEventHandler("Weather", 
    [this](const std::string& message) {
        std::cout << "Weather changed: " << message << std::endl;
    });

// NPC spawn events  
m_eventSystem->registerEventHandler("NPCSpawn",
    [this](const std::string& message) {
        std::cout << "NPC spawned: " << message << std::endl;
    });

// Scene change events
m_eventSystem->registerEventHandler("SceneChange",
    [this](const std::string& message) {
        std::cout << "Scene changed: " << message << std::endl;
    });
```

### Creating Events

```cpp
// Weather events
m_eventSystem->registerWeatherEvent("sunny_day", "Clear", 1.0f);
m_eventSystem->registerWeatherEvent("storm", "Stormy", 1.0f);

// NPC spawn events
m_eventSystem->registerNPCSpawnEvent("village_guards", "Guard", 3, 50.0f);
m_eventSystem->registerNPCSpawnEvent("random_encounter", "Bandit", 1, 0.0f);

// Scene transition events
m_eventSystem->registerSceneChangeEvent("enter_dungeon", "Dungeon", "fade");
m_eventSystem->registerSceneChangeEvent("exit_forest", "Village", "dissolve");
```

### Triggering Events

```cpp
// Immediate event triggers
m_eventSystem->triggerWeatherChange("Rainy", 3.0f);  // 3 second transition
m_eventSystem->triggerNPCSpawn("Guard", 100.0f, 200.0f);  // At specific position
m_eventSystem->triggerSceneChange("Forest", "fade", 2.0f);  // 2 second fade
```

## Architecture Overview

The event system follows this hierarchy in the Forge Game Engine:

```
GameEngine Lifecycle:
├── init() → EventManager::init()
├── update() → EventManager::update() → Event::execute()
└── clean() → EventManager::clean()

Event Flow:
GameState → EventManager → Individual Events
    ↓              ↓              ↓
Register      Process      Execute/Trigger
Events        Queue        Actions
```

## Thread Safety

- EventSystem is thread-safe via EventManager backend
- Events are processed on main thread for SDL compatibility
- Event registration should be done during GameState::enter()
- Event triggering can be done from any thread

## Best Practices

### 1. Register Events During State Entry
```cpp
bool MyGameState::enter() {
    // Register all events needed for this state
    setupEvents();
    return true;
}
```

### 2. Use Descriptive Event Names
```cpp
// Good
m_eventSystem->registerWeatherEvent("morning_fog", "Foggy", 0.6f);
m_eventSystem->registerNPCSpawnEvent("tavern_encounter", "Villager", 2, 25.0f);

// Avoid
m_eventSystem->registerWeatherEvent("event1", "Foggy", 0.6f);
```

### 3. Handle Events Gracefully
```cpp
m_eventSystem->registerEventHandler("Weather", 
    [this](const std::string& message) {
        try {
            handleWeatherChange(message);
        } catch (const std::exception& e) {
            std::cerr << "Weather event error: " << e.what() << std::endl;
        }
    });
```

### 4. Clean Up in Exit
```cpp
bool MyGameState::exit() {
    // EventSystem cleanup is handled automatically by GameEngine
    // Just clean up your state-specific resources
    return true;
}
```

## Common Patterns

### Weather System Integration
```cpp
void setupWeatherSystem() {
    // Register weather events for different times/conditions
    m_eventSystem->registerWeatherEvent("dawn_clear", "Clear", 1.0f);
    m_eventSystem->registerWeatherEvent("midday_sun", "Clear", 1.0f);
    m_eventSystem->registerWeatherEvent("evening_fog", "Foggy", 0.7f);
    m_eventSystem->registerWeatherEvent("night_storm", "Stormy", 0.9f);
    
    // Handler updates game environment
    m_eventSystem->registerEventHandler("Weather", 
        [this](const std::string& weatherType) {
            updateEnvironment(weatherType);
            updateLighting(weatherType);
            updateSoundscape(weatherType);
        });
}
```

### Dynamic NPC Spawning
```cpp
void setupDynamicSpawning() {
    // Different spawn types for different situations
    m_eventSystem->registerNPCSpawnEvent("combat_reinforcements", "Guard", 5, 200.0f);
    m_eventSystem->registerNPCSpawnEvent("peaceful_travelers", "Villager", 2, 150.0f);
    
    // Handler manages NPC lifecycle
    m_eventSystem->registerEventHandler("NPCSpawn",
        [this](const std::string& npcType) {
            trackSpawnedNPC(npcType);
            updateGameDifficulty();
        });
}
```

### Scene Transition Management
```cpp
void setupSceneTransitions() {
    // Pre-configured scene transitions
    m_eventSystem->registerSceneChangeEvent("enter_boss_room", "BossArena", "fade");
    m_eventSystem->registerSceneChangeEvent("return_to_town", "Town", "dissolve");
    
    // Handler prepares new scene
    m_eventSystem->registerEventHandler("SceneChange",
        [this](const std::string& sceneName) {
            saveGameState();
            preloadSceneAssets(sceneName);
            updateMinimap(sceneName);
        });
}
```

## Debugging and Monitoring

The EventDemoState (accessible via 'E' from main menu) provides:
- Real-time event monitoring
- Event log display
- Performance metrics
- Interactive event testing
- Visual feedback for all event types

Use it as a reference for implementing events in your own game states.

## See Also

- `docs/EventDemo.md` - Complete EventDemoState documentation
- `docs/EventManager.md` - EventManager API and usage
- `docs/EventManager_ThreadSystem.md` - Threading integration details
- `docs/EventManagerExamples.cpp` - Code examples and patterns