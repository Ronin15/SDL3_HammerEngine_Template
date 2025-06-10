# EventFactory Quick Reference

## Overview
Quick reference for creating events using the EventFactory with intelligent defaults and configuration-driven approaches.

## Essential Includes
```cpp
#include "events/EventFactory.hpp"
#include "events/Event.hpp"
```

## Quick Setup
```cpp
// EventFactory is automatically initialized by EventManager
// No manual initialization required, but you can call explicitly:
EventFactory::Instance().init();
```

## Direct Event Creation (Recommended)

### Weather Events
```cpp
// Basic weather events
auto rain = EventFactory::Instance().createWeatherEvent("Rain", "Rainy", 0.8f, 3.0f);
auto fog = EventFactory::Instance().createWeatherEvent("Fog", "Foggy", 0.5f, 5.0f);
auto storm = EventFactory::Instance().createWeatherEvent("Storm", "Stormy", 0.9f, 2.0f);
auto clear = EventFactory::Instance().createWeatherEvent("Clear", "Clear", 1.0f, 4.0f);

// Parameters: (name, weatherType, intensity, transitionTime)
```

### Scene Change Events  
```cpp
// Basic scene transitions
auto toTown = EventFactory::Instance().createSceneChangeEvent("ToTown", "TownScene", "fade", 2.0f);
auto toDungeon = EventFactory::Instance().createSceneChangeEvent("ToDungeon", "DungeonScene", "dissolve", 1.5f);
auto toMenu = EventFactory::Instance().createSceneChangeEvent("ToMenu", "MainMenu", "fade", 1.0f);

// Parameters: (name, targetScene, transitionType, duration)
```

### NPC Spawn Events
```cpp
// Basic NPC spawning
auto guards = EventFactory::Instance().createNPCSpawnEvent("Guards", "Guard", 3, 50.0f);
auto villagers = EventFactory::Instance().createNPCSpawnEvent("Villagers", "Villager", 5, 30.0f);
auto merchant = EventFactory::Instance().createNPCSpawnEvent("Merchant", "Merchant", 1, 0.0f);

// Parameters: (name, npcType, count, spawnRadius)
```

## Configuration-Driven Creation

### EventDefinition Structure
```cpp
struct EventDefinition {
    std::string type;                                    // Event type
    std::string name;                                    // Unique name
    std::unordered_map<std::string, std::string> params;     // String parameters
    std::unordered_map<std::string, float> numParams;        // Numeric parameters
    std::unordered_map<std::string, bool> boolParams;        // Boolean parameters
};
```

### Weather Event Configuration
```cpp
EventDefinition stormDef;
stormDef.type = "Weather";
stormDef.name = "EpicStorm";
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
EventDefinition sceneDef;
sceneDef.type = "SceneChange";
sceneDef.name = "MagicPortal";
sceneDef.params["targetScene"] = "MagicRealm";
sceneDef.params["transitionType"] = "dissolve";
sceneDef.numParams["duration"] = 3.0f;
sceneDef.numParams["priority"] = 5;
sceneDef.boolParams["oneTime"] = true;

auto portalEvent = EventFactory::Instance().createEvent(sceneDef);
```

### NPC Spawn Configuration
```cpp
EventDefinition npcDef;
npcDef.type = "NPCSpawn";
npcDef.name = "OrcInvasion";
npcDef.params["npcType"] = "OrcWarrior";
npcDef.numParams["count"] = 10.0f;
npcDef.numParams["spawnRadius"] = 100.0f;
npcDef.numParams["priority"] = 9;
npcDef.boolParams["oneTime"] = true;

auto armyEvent = EventFactory::Instance().createEvent(npcDef);
```

## Event Sequences

### Sequential Events
```cpp
// Create events that execute in order
std::vector<EventDefinition> weatherSequence = {
    {"Weather", "StartRain", {{"weatherType", "Rainy"}}, {{"intensity", 0.5f}}, {}},
    {"Weather", "Thunderstorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.9f}}, {}},
    {"Weather", "ClearSkies", {{"weatherType", "Clear"}}, {{"transitionTime", 8.0f}}, {}}
};

auto sequence = EventFactory::Instance().createEventSequence(
    "WeatherSequence", weatherSequence, true);

// Events get automatic priority assignment for sequential execution
```

### Simultaneous Events
```cpp
// Create events that execute at the same time
std::vector<EventDefinition> combatStart = {
    {"Weather", "BattleStorm", {{"weatherType", "Stormy"}}, {{"intensity", 0.8f}}, {}},
    {"NPCSpawn", "SpawnEnemies", {{"npcType", "Orc"}}, {{"count", 3.0f}}, {}},
    {"SceneChange", "CombatMode", {{"targetScene", "BattleUI"}}, {{"duration", 0.5f}}, {}}
};

auto combatEvents = EventFactory::Instance().createEventSequence(
    "StartCombat", combatStart, false); // false = simultaneous
```

## Custom Event Creators

### Registering Custom Creators
```cpp
// Register a custom event type
EventFactory::Instance().registerCustomEventCreator("Quest", 
    [](const EventDefinition& def) -> EventPtr {
        std::string questId = def.params.count("questId") ? def.params.at("questId") : "";
        std::string objective = def.params.count("objective") ? def.params.at("objective") : "";
        int reward = static_cast<int>(def.numParams.count("reward") ? def.numParams.at("reward") : 0.0f);
        
        return std::make_shared<QuestEvent>(def.name, questId, objective, reward);
    });
```

### Using Custom Creators
```cpp
EventDefinition questDef;
questDef.type = "Quest";
questDef.name = "FindTreasure";
questDef.params["questId"] = "treasure_hunt";
questDef.params["objective"] = "Find the hidden treasure";
questDef.numParams["reward"] = 1000.0f;

auto questEvent = EventFactory::Instance().createEvent(questDef);
```

## Built-in Event Types

### Weather Types
- **"Clear"**: Full visibility, no particles
- **"Cloudy"**: Slight visibility reduction
- **"Rainy"**: Rain particles, reduced visibility, ambient sounds
- **"Stormy"**: Heavy rain, lightning, thunder sounds
- **"Foggy"**: Dramatic visibility reduction, fog particles
- **"Snowy"**: Snow particles, reduced visibility, snow sounds
- **"Windy"**: Wind effects
- **Custom**: Use any string for custom weather types

### Transition Types
- **"fade"**: Standard fade transition
- **"dissolve"**: Dissolve effect
- **"slide"**: Sliding transition
- **"wipe"**: Wipe transition
- **Custom**: Use any string for custom transitions

## Common Event Properties

### All Events Support
```cpp
// Common numeric parameters
def.numParams["priority"] = 5.0f;        // Processing priority (higher = first)
def.numParams["updateFrequency"] = 1.0f; // Update frequency (1 = every frame)
def.numParams["cooldown"] = 10.0f;       // Cooldown time in seconds

// Common boolean parameters
def.boolParams["oneTime"] = true;        // Can only trigger once
def.boolParams["active"] = true;         // Initially active
```

## Intelligent Defaults

### Weather Event Defaults
```cpp
// Rainy/Stormy weather automatically gets:
// - Reduced visibility based on intensity
// - Rain particles (heavy_rain if intensity > 0.7)
// - Thunder sounds for storms

// Foggy weather automatically gets:
// - Dramatically reduced visibility
// - Fog particle effects

// Clear weather automatically gets:
// - Full visibility (overrides intensity)
// - No particles or sounds
```

### Scene Transition Defaults
```cpp
// Fade transitions automatically get:
// - Black fade color
// - Appropriate timing

// Dissolve transitions automatically get:
// - Smooth dissolve effect
// - Transition sounds
```

## Quick Patterns

### Weather System Setup
```cpp
void createWeatherEvents() {
    EventFactory::Instance().createWeatherEvent("ClearDay", "Clear", 1.0f, 4.0f);
    EventFactory::Instance().createWeatherEvent("CloudyDay", "Cloudy", 0.6f, 5.0f);
    EventFactory::Instance().createWeatherEvent("RainyDay", "Rainy", 0.7f, 3.0f);
    EventFactory::Instance().createWeatherEvent("StormyNight", "Stormy", 0.9f, 2.0f);
}
```

### Scene Navigation Setup
```cpp
void createSceneEvents() {
    EventFactory::Instance().createSceneChangeEvent("ToMainMenu", "MainMenu", "fade", 1.5f);
    EventFactory::Instance().createSceneChangeEvent("ToGameWorld", "GameWorld", "dissolve", 2.0f);
    EventFactory::Instance().createSceneChangeEvent("ToBattle", "BattleScene", "fade", 1.0f);
    EventFactory::Instance().createSceneChangeEvent("ToShop", "ShopScene", "slide", 1.5f);
}
```

### NPC Population Setup
```cpp
void createNPCEvents() {
    EventFactory::Instance().createNPCSpawnEvent("TownGuards", "Guard", 3, 50.0f);
    EventFactory::Instance().createNPCSpawnEvent("Villagers", "Villager", 8, 40.0f);
    EventFactory::Instance().createNPCSpawnEvent("Merchants", "Merchant", 2, 20.0f);
    EventFactory::Instance().createNPCSpawnEvent("Enemies", "Bandit", 5, 60.0f);
}
```

## Batch Creation Pattern
```cpp
void createEventsBatch() {
    std::vector<std::tuple<std::string, std::string, float, float>> weatherEvents = {
        {"MorningFog", "Foggy", 0.4f, 5.0f},
        {"NoonSun", "Clear", 1.0f, 3.0f},
        {"EveningRain", "Rainy", 0.6f, 4.0f},
        {"NightStorm", "Stormy", 0.8f, 2.0f}
    };
    
    for (const auto& [name, type, intensity, time] : weatherEvents) {
        EventFactory::Instance().createWeatherEvent(name, type, intensity, time);
    }
}
```

## Configuration-Style Creation
```cpp
void createConfigurableEvents() {
    // Weather with custom properties
    EventDefinition epicStorm;
    epicStorm.type = "Weather";
    epicStorm.name = "EpicStorm";
    epicStorm.params["weatherType"] = "Stormy";
    epicStorm.numParams["intensity"] = 1.0f;
    epicStorm.numParams["priority"] = 10;
    epicStorm.numParams["cooldown"] = 120.0f;
    epicStorm.boolParams["oneTime"] = false;
    
    // Scene with custom timing
    EventDefinition portalTransition;
    portalTransition.type = "SceneChange";
    portalTransition.name = "MagicPortal";
    portalTransition.params["targetScene"] = "AnotherRealm";
    portalTransition.params["transitionType"] = "dissolve";
    portalTransition.numParams["duration"] = 5.0f;
    portalTransition.boolParams["oneTime"] = true;
    
    // Create events
    auto storm = EventFactory::Instance().createEvent(epicStorm);
    auto portal = EventFactory::Instance().createEvent(portalTransition);
}
```

## Error Handling
```cpp
// Always check creation success
auto event = EventFactory::Instance().createWeatherEvent("Test", "InvalidType", 0.5f, 3.0f);
if (!event) {
    std::cerr << "Failed to create weather event!" << std::endl;
}

// Check custom creator registration
EventDefinition def;
def.type = "UnknownType";
def.name = "Test";
auto unknownEvent = EventFactory::Instance().createEvent(def);
if (!unknownEvent) {
    std::cerr << "Unknown event type: " << def.type << std::endl;
}
```

## Cleanup
```cpp
void cleanupEventFactory() {
    // Factory cleans up automatically, but you can call explicitly
    EventFactory::Instance().clean();
}
```

## Tips
- Use direct methods (`createWeatherEvent`, etc.) for simple events
- Use `EventDefinition` and `createEvent` for complex configuration
- Use `createEventSequence` for related events that should execute together
- Register custom creators early in initialization
- Always check return values for null pointers
- Leverage intelligent defaults - many parameters are set automatically
- Use meaningful names for events to help with debugging

---

This quick reference covers the most common EventFactory usage patterns. See the full EventFactory documentation for detailed explanations and advanced features.