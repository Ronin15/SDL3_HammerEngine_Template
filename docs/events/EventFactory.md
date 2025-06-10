# EventFactory Documentation

## Overview

The EventFactory provides a streamlined and flexible API for creating game events with intelligent defaults and configuration-driven approaches. It serves as the primary creation interface for all event types in the Forge Game Engine, offering both simple direct methods and advanced definition-based creation patterns.

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Event Creation Methods](#event-creation-methods)
- [Configuration-Driven Creation](#configuration-driven-creation)
- [Custom Event Creators](#custom-event-creators)
- [Event Sequences](#event-sequences)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Initialization

```cpp
#include "events/EventFactory.hpp"

// EventFactory is automatically initialized by EventManager
// No manual initialization required, but you can call init() explicitly
if (!EventFactory::Instance().init()) {
    std::cerr << "Failed to initialize EventFactory!" << std::endl;
}
```

### Simple Event Creation

```cpp
// Direct method calls - simplest approach
auto rainEvent = EventFactory::Instance().createWeatherEvent(
    "morning_rain",    // event name
    "Rainy",          // weather type
    0.7f,             // intensity
    4.0f              // transition time
);

auto sceneEvent = EventFactory::Instance().createSceneChangeEvent(
    "enter_dungeon",  // event name
    "Dungeon",        // target scene
    "fade",           // transition type
    2.5f              // duration
);

auto npcEvent = EventFactory::Instance().createNPCSpawnEvent(
    "guard_patrol",   // event name
    "Guard",          // NPC type
    3,                // count
    50.0f             // spawn radius
);
```

## Architecture

### Design Principles

The EventFactory follows these key principles:

1. **Intelligent Defaults**: Automatically configures parameters based on event type
2. **Configuration-Driven**: Support for structured event definitions
3. **Extensible**: Custom event creators for new event types
4. **Type Safety**: Parameter validation with fallback defaults
5. **Memory Efficient**: Smart pointer management throughout

### Core Components

```cpp
class EventFactory {
    // Custom event creator functions
    std::unordered_map<std::string, std::function<EventPtr(const EventDefinition&)>> m_eventCreators;
    
    // Built-in creators for standard event types
    // - Weather events
    // - Scene change events  
    // - NPC spawn events
};
```

### EventDefinition Structure

```cpp
struct EventDefinition {
    std::string type;                                    // Event type identifier
    std::string name;                                    // Unique event name
    std::unordered_map<std::string, std::string> params;     // String parameters
    std::unordered_map<std::string, float> numParams;        // Numeric parameters
    std::unordered_map<std::string, bool> boolParams;        // Boolean parameters
};
```

## Event Creation Methods

### Weather Events

```cpp
EventPtr createWeatherEvent(const std::string& name, 
                           const std::string& weatherType,
                           float intensity = 0.5f, 
                           float transitionTime = 5.0f);
```

**Parameters:**
- `name`: Unique identifier for the event
- `weatherType`: Type of weather ("Clear", "Rainy", "Stormy", "Foggy", "Snowy", "Windy", or custom)
- `intensity`: Weather intensity (0.0-1.0)
- `transitionTime`: Time in seconds for weather transition

**Automatic Configuration:**
- **Rainy/Stormy**: Reduced visibility, rain particles, thunder sounds
- **Foggy**: Drastically reduced visibility, fog particles
- **Snowy**: Reduced visibility, snow particles, ambient snow sounds
- **Clear**: Full visibility, no particles

### Scene Change Events

```cpp
EventPtr createSceneChangeEvent(const std::string& name, 
                              const std::string& targetScene,
                              const std::string& transitionType = "fade", 
                              float duration = 1.0f);
```

**Parameters:**
- `name`: Unique identifier for the event
- `targetScene`: ID of the target scene to transition to
- `transitionType`: Transition effect ("fade", "dissolve", "slide", etc.)
- `duration`: Duration of transition in seconds

### NPC Spawn Events

```cpp
EventPtr createNPCSpawnEvent(const std::string& name, 
                           const std::string& npcType,
                           int count = 1, 
                           float spawnRadius = 0.0f);
```

**Parameters:**
- `name`: Unique identifier for the event
- `npcType`: Type of NPC to spawn
- `count`: Number of NPCs to spawn
- `spawnRadius`: Radius around spawn point for random placement

## Configuration-Driven Creation

### Using EventDefinition

The most powerful and flexible way to create events:

```cpp
EventPtr createEvent(const EventDefinition& def);
```

### Weather Event Configuration

```cpp
EventDefinition stormDef;
stormDef.type = "Weather";
stormDef.name = "epic_storm";
stormDef.params["weatherType"] = "Stormy";
stormDef.numParams["intensity"] = 0.95f;
stormDef.numParams["transitionTime"] = 1.5f;
stormDef.numParams["priority"] = 8;
stormDef.numParams["cooldown"] = 30.0f;
stormDef.boolParams["oneTime"] = false;
stormDef.boolParams["active"] = true;

auto stormEvent = EventFactory::Instance().createEvent(stormDef);
```

### Scene Change Configuration

```cpp
EventDefinition portalDef;
portalDef.type = "SceneChange";
portalDef.name = "magic_portal";
portalDef.params["targetScene"] = "MagicRealm";
portalDef.params["transitionType"] = "dissolve";
portalDef.numParams["duration"] = 3.0f;
portalDef.numParams["priority"] = 5;
portalDef.boolParams["oneTime"] = true;

auto portalEvent = EventFactory::Instance().createEvent(portalDef);
```

### NPC Spawn Configuration

```cpp
EventDefinition armyDef;
armyDef.type = "NPCSpawn";
armyDef.name = "orc_invasion";
armyDef.params["npcType"] = "OrcWarrior";
armyDef.numParams["count"] = 10.0f;
armyDef.numParams["spawnRadius"] = 100.0f;
armyDef.numParams["priority"] = 9;
armyDef.boolParams["oneTime"] = true;

auto armyEvent = EventFactory::Instance().createEvent(armyDef);
```

### Common Event Properties

All events support these common properties in EventDefinition:

| Parameter | Type | Description |
|-----------|------|-------------|
| `priority` | float | Event processing priority (higher = first) |
| `updateFrequency` | float | How often to update (1 = every frame) |
| `cooldown` | float | Cooldown time in seconds |
| `oneTime` | bool | Whether event can only trigger once |
| `active` | bool | Initial active state |

## Custom Event Creators

### Registering Custom Creators

```cpp
void registerCustomEventCreator(const std::string& eventType,
                               std::function<EventPtr(const EventDefinition&)> creatorFunc);
```

### Example: Custom Quest Event

```cpp
// Register a custom quest event creator
EventFactory::Instance().registerCustomEventCreator("Quest", 
    [](const EventDefinition& def) -> EventPtr {
        std::string questId = def.params.count("questId") ? def.params.at("questId") : "";
        std::string objective = def.params.count("objective") ? def.params.at("objective") : "";
        int reward = static_cast<int>(def.numParams.count("reward") ? def.numParams.at("reward") : 0.0f);
        
        // Create your custom quest event
        return std::make_shared<QuestEvent>(def.name, questId, objective, reward);
    });

// Use the custom creator
EventDefinition questDef;
questDef.type = "Quest";
questDef.name = "find_treasure";
questDef.params["questId"] = "treasure_hunt";
questDef.params["objective"] = "Find the hidden treasure";
questDef.numParams["reward"] = 1000.0f;

auto questEvent = EventFactory::Instance().createEvent(questDef);
```

## Event Sequences

### Creating Event Sequences

```cpp
std::vector<EventPtr> createEventSequence(const std::string& name,
                                         const std::vector<EventDefinition>& events,
                                         bool sequential = true);
```

### Weather Sequence Example

```cpp
// Create a dynamic weather sequence: Rain -> Lightning -> Clear
std::vector<EventDefinition> weatherSequence = {
    {"Weather", "StartRain", {{"weatherType", "Rainy"}}, {{"intensity", 0.5f}}, {}},
    {"Weather", "Thunderstorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.9f}}, {}},
    {"Weather", "ClearSkies", {{"weatherType", "Clear"}}, {{"transitionTime", 8.0f}}, {}}
};

auto sequence = EventFactory::Instance().createEventSequence(
    "WeatherSequence", weatherSequence, true);

// Events are automatically assigned decreasing priorities for sequential execution
// StartRain gets highest priority, ClearSkies gets lowest
```

### Story Sequence Example

```cpp
// Create a story sequence
std::vector<EventDefinition> storySequence = {
    {"SceneChange", "EnterVillage", {{"targetScene", "Village"}, {"transitionType", "fade"}}, {{"duration", 2.0f}}, {}},
    {"NPCSpawn", "SpawnMayor", {{"npcType", "Mayor"}}, {{"count", 1.0f}}, {}},
    {"SceneChange", "ShowDialogue", {{"targetScene", "DialogueScene"}}, {{"duration", 1.0f}}, {}}
};

auto storyEvents = EventFactory::Instance().createEventSequence(
    "VillageIntro", storySequence, true);
```

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
bool init();
void clean();

// Direct event creation
EventPtr createWeatherEvent(const std::string& name, const std::string& weatherType,
                           float intensity = 0.5f, float transitionTime = 5.0f);
EventPtr createSceneChangeEvent(const std::string& name, const std::string& targetScene,
                               const std::string& transitionType = "fade", float duration = 1.0f);
EventPtr createNPCSpawnEvent(const std::string& name, const std::string& npcType,
                            int count = 1, float spawnRadius = 0.0f);

// Configuration-driven creation
EventPtr createEvent(const EventDefinition& def);

// Event sequences
std::vector<EventPtr> createEventSequence(const std::string& name,
                                        const std::vector<EventDefinition>& events,
                                        bool sequential = true);

// Custom event creators
void registerCustomEventCreator(const std::string& eventType,
                               std::function<EventPtr(const EventDefinition&)> creatorFunc);
```

### Built-in Event Creators

The EventFactory comes with these built-in creators:

| Event Type | Creator Key | Parameters |
|------------|-------------|------------|
| Weather | "Weather" | weatherType, intensity, transitionTime |
| Scene Change | "SceneChange" | targetScene, transitionType, duration |
| NPC Spawn | "NPCSpawn" | npcType, count, spawnRadius |

### Helper Methods

```cpp
// Internal helper methods (implementation-specific)
WeatherType getWeatherTypeFromString(const std::string& weatherType);
TransitionType getTransitionTypeFromString(const std::string& transitionType);
```

## Best Practices

### 1. Use Direct Methods for Simple Events

```cpp
// Good: For straightforward events
auto rainEvent = EventFactory::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);

// Avoid: EventDefinition for simple cases (unnecessary complexity)
EventDefinition def;
def.type = "Weather";
def.name = "Rain";
def.params["weatherType"] = "Rainy";
def.numParams["intensity"] = 0.8f;
def.numParams["transitionTime"] = 3.0f;
auto rainEvent = EventFactory::Instance().createEvent(def);
```

### 2. Use EventDefinition for Complex Configuration

```cpp
// Good: When you need multiple properties
EventDefinition complexDef;
complexDef.type = "Weather";
complexDef.name = "EpicStorm";
complexDef.params["weatherType"] = "Stormy";
complexDef.numParams["intensity"] = 1.0f;
complexDef.numParams["priority"] = 10;
complexDef.numParams["cooldown"] = 60.0f;
complexDef.boolParams["oneTime"] = true;

auto event = EventFactory::Instance().createEvent(complexDef);
```

### 3. Register Custom Creators Early

```cpp
void initializeCustomEvents() {
    // Register all custom creators during initialization
    EventFactory::Instance().registerCustomEventCreator("Quest", questCreator);
    EventFactory::Instance().registerCustomEventCreator("Combat", combatCreator);
    EventFactory::Instance().registerCustomEventCreator("Dialogue", dialogueCreator);
}
```

### 4. Use Sequences for Related Events

```cpp
// Good: For events that should happen in order
std::vector<EventDefinition> cutsceneEvents = {
    {"SceneChange", "FadeOut", {{"targetScene", "Black"}}, {{"duration", 1.0f}}, {}},
    {"NPCSpawn", "SpawnBoss", {{"npcType", "FinalBoss"}}, {}, {}},
    {"SceneChange", "FadeIn", {{"targetScene", "BossArena"}}, {{"duration", 2.0f}}, {}}
};

auto sequence = EventFactory::Instance().createEventSequence("BossIntro", cutsceneEvents, true);
```

### 5. Validate Event Creation

```cpp
auto event = EventFactory::Instance().createWeatherEvent("Test", "InvalidType", 0.5f, 3.0f);
if (!event) {
    std::cerr << "Failed to create weather event!" << std::endl;
    return false;
}
```

## Examples

### Complete Event System Setup

```cpp
#include "events/EventFactory.hpp"
#include "events/QuestEvent.hpp" // Custom event type

class GameEventInitializer {
public:
    bool initialize() {
        // Initialize the factory
        if (!EventFactory::Instance().init()) {
            return false;
        }
        
        // Register custom event creators
        registerCustomCreators();
        
        // Create initial game events
        createInitialEvents();
        
        return true;
    }
    
private:
    void registerCustomCreators() {
        // Register quest event creator
        EventFactory::Instance().registerCustomEventCreator("Quest",
            [](const EventDefinition& def) -> EventPtr {
                std::string questId = def.params.count("questId") ? def.params.at("questId") : "";
                std::string description = def.params.count("description") ? def.params.at("description") : "";
                int reward = static_cast<int>(def.numParams.count("reward") ? def.numParams.at("reward") : 0.0f);
                
                return std::make_shared<QuestEvent>(def.name, questId, description, reward);
            });
        
        // Register dialogue event creator
        EventFactory::Instance().registerCustomEventCreator("Dialogue",
            [](const EventDefinition& def) -> EventPtr {
                std::string speaker = def.params.count("speaker") ? def.params.at("speaker") : "";
                std::string text = def.params.count("text") ? def.params.at("text") : "";
                
                return std::make_shared<DialogueEvent>(def.name, speaker, text);
            });
    }
    
    void createInitialEvents() {
        // Create weather events
        createWeatherEvents();
        
        // Create scene transition events
        createSceneEvents();
        
        // Create quest events
        createQuestEvents();
        
        // Create event sequences
        createEventSequences();
    }
    
    void createWeatherEvents() {
        // Simple weather events
        auto morningFog = EventFactory::Instance().createWeatherEvent(
            "MorningFog", "Foggy", 0.4f, 5.0f);
        
        auto afternoonRain = EventFactory::Instance().createWeatherEvent(
            "AfternoonRain", "Rainy", 0.7f, 3.0f);
        
        // Complex weather event using EventDefinition
        EventDefinition stormDef;
        stormDef.type = "Weather";
        stormDef.name = "EpicStorm";
        stormDef.params["weatherType"] = "Stormy";
        stormDef.numParams["intensity"] = 0.95f;
        stormDef.numParams["transitionTime"] = 2.0f;
        stormDef.numParams["priority"] = 8;
        stormDef.numParams["cooldown"] = 120.0f;
        stormDef.boolParams["oneTime"] = false;
        
        auto epicStorm = EventFactory::Instance().createEvent(stormDef);
    }
    
    void createSceneEvents() {
        // Scene transitions
        auto enterTown = EventFactory::Instance().createSceneChangeEvent(
            "EnterTown", "TownScene", "fade", 2.0f);
        
        auto enterDungeon = EventFactory::Instance().createSceneChangeEvent(
            "EnterDungeon", "DungeonScene", "dissolve", 1.5f);
    }
    
    void createQuestEvents() {
        // Quest using custom creator
        EventDefinition questDef;
        questDef.type = "Quest";
        questDef.name = "FindArtifact";
        questDef.params["questId"] = "artifact_hunt";
        questDef.params["description"] = "Find the ancient artifact in the ruins";
        questDef.numParams["reward"] = 500.0f;
        questDef.boolParams["oneTime"] = true;
        
        auto questEvent = EventFactory::Instance().createEvent(questDef);
    }
    
    void createEventSequences() {
        // Town entrance sequence
        std::vector<EventDefinition> townSequence = {
            {"SceneChange", "FadeToTown", {{"targetScene", "TownScene"}, {"transitionType", "fade"}}, {{"duration", 2.0f}}, {}},
            {"Weather", "SetTownWeather", {{"weatherType", "Clear"}}, {{"intensity", 1.0f}}, {}},
            {"NPCSpawn", "SpawnTownspeople", {{"npcType", "Villager"}}, {{"count", 5.0f}, {"spawnRadius", 50.0f}}, {}}
        };
        
        auto townEvents = EventFactory::Instance().createEventSequence(
            "TownEntrance", townSequence, true);
        
        // Combat sequence
        std::vector<EventDefinition> combatSequence = {
            {"Weather", "BattleStorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.8f}}, {}},
            {"NPCSpawn", "SpawnEnemies", {{"npcType", "Orc"}}, {{"count", 3.0f}}, {}},
            {"SceneChange", "CombatMode", {{"targetScene", "BattleUI"}}, {{"duration", 0.5f}}, {}}
        };
        
        auto combatEvents = EventFactory::Instance().createEventSequence(
            "StartCombat", combatSequence, false); // Simultaneous execution
    }
};
```

### JSON-Style Event Configuration

```cpp
class EventConfigLoader {
public:
    void loadEventsFromConfig() {
        // Weather events configuration
        std::vector<EventDefinition> weatherConfigs = {
            {"Weather", "DayRain", {{"weatherType", "Rainy"}}, {{"intensity", 0.6f}, {"transitionTime", 4.0f}}, {{"active", true}}},
            {"Weather", "NightFog", {{"weatherType", "Foggy"}}, {{"intensity", 0.8f}, {"transitionTime", 6.0f}}, {{"active", true}}},
            {"Weather", "Storm", {{"weatherType", "Stormy"}}, {{"intensity", 0.9f}, {"priority", 5}, {"cooldown", 60.0f}}, {{"oneTime", false}}}
        };
        
        // Scene events configuration
        std::vector<EventDefinition> sceneConfigs = {
            {"SceneChange", "MainMenu", {{"targetScene", "Menu"}, {"transitionType", "fade"}}, {{"duration", 1.5f}}, {}},
            {"SceneChange", "GameStart", {{"targetScene", "GameWorld"}, {"transitionType", "dissolve"}}, {{"duration", 3.0f}}, {}},
            {"SceneChange", "GameOver", {{"targetScene", "GameOverScene"}}, {{"duration", 2.0f}, {"priority", 10}}, {{"oneTime", true}}}
        };
        
        // NPC events configuration
        std::vector<EventDefinition> npcConfigs = {
            {"NPCSpawn", "Guards", {{"npcType", "Guard"}}, {{"count", 2}, {"spawnRadius", 30.0f}}, {}},
            {"NPCSpawn", "Merchants", {{"npcType", "Merchant"}}, {{"count", 1}, {"spawnRadius", 0.0f}}, {}},
            {"NPCSpawn", "Enemies", {{"npcType", "Bandit"}}, {{"count", 4}, {"spawnRadius", 50.0f}, {"priority", 7}}, {}}
        };
        
        // Create all events
        createEventsFromConfigs(weatherConfigs);
        createEventsFromConfigs(sceneConfigs);
        createEventsFromConfigs(npcConfigs);
    }
    
private:
    void createEventsFromConfigs(const std::vector<EventDefinition>& configs) {
        for (const auto& config : configs) {
            auto event = EventFactory::Instance().createEvent(config);
            if (event) {
                std::cout << "Created event: " << config.name << " (type: " << config.type << ")" << std::endl;
            } else {
                std::cerr << "Failed to create event: " << config.name << std::endl;
            }
        }
    }
};
```

### Performance Optimization

```cpp
class OptimizedEventCreation {
public:
    void createEventsEfficiently() {
        // Batch create similar events
        createWeatherEventBatch();
        createNPCEventBatch();
        
        // Use sequences for related events
        createStorySequences();
    }
    
private:
    void createWeatherEventBatch() {
        std::vector<std::pair<std::string, std::string>> weatherTypes = {
            {"ClearDay", "Clear"},
            {"CloudyDay", "Cloudy"},
            {"RainyDay", "Rainy"},
            {"SnowyDay", "Snowy"}
        };
        
        for (const auto& [name, type] : weatherTypes) {
            auto event = EventFactory::Instance().createWeatherEvent(name, type, 0.7f, 4.0f);
            // Events are automatically optimized by the factory
        }
    }
    
    void createNPCEventBatch() {
        std::vector<std::tuple<std::string, std::string, int>> npcConfigs = {
            {"TownGuards", "Guard", 3},
            {"Villagers", "Villager", 8},
            {"Merchants", "Merchant", 2}
        };
        
        for (const auto& [name, type, count] : npcConfigs) {
            auto event = EventFactory::Instance().createNPCSpawnEvent(name, type, count, 40.0f);
        }
    }
    
    void createStorySequences() {
        // Efficient sequence creation
        std::vector<EventDefinition> intro = {
            {"SceneChange", "OpeningScene", {{"targetScene", "Intro"}}, {{"duration", 3.0f}}, {}},
            {"Weather", "SetMood", {{"weatherType", "Cloudy"}}, {{"intensity", 0.5f}}, {}},
            {"NPCSpawn", "IntroCharacters", {{"npcType", "Narrator"}}, {{"count", 1}}, {}}
        };
        
        auto introSequence = EventFactory::Instance().createEventSequence("GameIntro", intro, true);
        
        // Sequences automatically optimize priority and execution order
    }
};
```

---

The EventFactory provides a powerful, flexible foundation for event creation in the Forge Game Engine. Its combination of simple direct methods and sophisticated configuration-driven approaches makes it suitable for both rapid prototyping and complex event systems. The built-in intelligent defaults ensure that events work correctly out-of-the-box, while the extensible custom creator system allows for unlimited customization.