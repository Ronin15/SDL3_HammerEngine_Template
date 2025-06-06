# EventFactory Documentation

## Overview
The EventFactory provides the most streamlined and flexible API for creating game events. It offers multiple creation patterns from simple direct methods to advanced configuration-driven approaches, making it suitable for both rapid prototyping and complex event systems.

## Key Features

- **Intelligent Defaults**: Automatically configures event parameters based on event type
- **Configuration-Driven**: Create events from structured definitions (JSON-compatible)
- **Batch Processing**: Create sequences of related events efficiently
- **Extensible**: Register custom event creators for new event types
- **Parameter Validation**: Type-safe parameter handling with fallback defaults
- **Memory Efficient**: Smart pointer management and object pooling

## Getting Started

### Basic Initialization
```cpp
// EventFactory is automatically initialized by EventManager
// No manual initialization required
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

## Advanced Features

### EventDefinition Structure
The most powerful and flexible way to create events using structured definitions:

```cpp
struct EventDefinition {
    std::string type;                                    // Event type identifier
    std::string name;                                    // Unique event name
    std::unordered_map<std::string, std::string> params;     // String parameters
    std::unordered_map<std::string, float> numParams;        // Numeric parameters
    std::unordered_map<std::string, bool> boolParams;        // Boolean parameters
};
```

### Configuration-Driven Event Creation

#### Weather Events
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

#### Scene Change Events
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

#### NPC Spawn Events
```cpp
EventDefinition armyDef;
armyDef.type = "NPCSpawn";
armyDef.name = "orc_invasion";
armyDef.params["npcType"] = "OrcWarrior";
armyDef.numParams["count"] = 10.0f;
armyDef.numParams["spawnRadius"] = 100.0f;
armyDef.numParams["priority"] = 9;
armyDef.boolParams["active"] = true;

auto armyEvent = EventFactory::Instance().createEvent(armyDef);
```

### Event Sequences
Create complex event chains that execute in order or simultaneously:

#### Sequential Events
```cpp
// Create a dramatic storm sequence
std::vector<EventDefinition> stormSequence = {
    {
        .type = "Weather",
        .name = "approaching_clouds",
        .params = {{"weatherType", "Cloudy"}},
        .numParams = {{"intensity", 0.3f}, {"transitionTime", 5.0f}}
    },
    {
        .type = "Weather",
        .name = "light_rain",
        .params = {{"weatherType", "Rainy"}},
        .numParams = {{"intensity", 0.5f}, {"transitionTime", 3.0f}}
    },
    {
        .type = "Weather",
        .name = "heavy_storm",
        .params = {{"weatherType", "Stormy"}},
        .numParams = {{"intensity", 0.9f}, {"transitionTime", 1.5f}}
    },
    {
        .type = "SceneChange",
        .name = "storm_atmosphere",
        .params = {{"targetScene", "StormyField"}, {"transitionType", "fade"}},
        .numParams = {{"duration", 2.0f}}
    }
};

// Create all events in sequence (higher priority = executes first)
auto stormEvents = EventFactory::Instance().createEventSequence(
    "epic_storm_sequence",  // base name
    stormSequence,          // event definitions
    true                    // sequential (false = simultaneous)
);
```

#### Simultaneous Events
```cpp
// Create a boss battle entrance
std::vector<EventDefinition> bossBattleStart = {
    {
        .type = "Weather",
        .name = "battle_storm",
        .params = {{"weatherType", "Stormy"}},
        .numParams = {{"intensity", 0.8f}, {"transitionTime", 1.0f}}
    },
    {
        .type = "SceneChange", 
        .name = "boss_arena",
        .params = {{"targetScene", "BossArena"}, {"transitionType", "wipe"}},
        .numParams = {{"duration", 1.5f}}
    },
    {
        .type = "NPCSpawn",
        .name = "boss_minions",
        .params = {{"npcType", "SkeletonArcher"}},
        .numParams = {{"count", 4.0f}, {"spawnRadius", 80.0f}}
    }
};

// All events execute simultaneously
auto bossBattleEvents = EventFactory::Instance().createEventSequence(
    "boss_battle_intro",
    bossBattleStart,
    false  // simultaneous execution
);
```

### Custom Event Creators
Extend the factory to support new event types:

#### Registering Custom Creators
```cpp
// Register a quest event creator
EventFactory::Instance().registerCustomEventCreator("QuestEvent",
    [](const EventDefinition& def) -> EventPtr {
        std::string questId = def.params.at("questId");
        std::string questType = def.params.count("questType") ? 
                               def.params.at("questType") : "main";
        bool isOptional = def.boolParams.count("optional") ? 
                         def.boolParams.at("optional") : false;
        int questLevel = static_cast<int>(def.numParams.count("level") ? 
                                        def.numParams.at("level") : 1.0f);
        
        return std::make_shared<QuestEvent>(def.name, questId, questType, isOptional, questLevel);
    });

// Register a dialogue event creator
EventFactory::Instance().registerCustomEventCreator("DialogueEvent",
    [](const EventDefinition& def) -> EventPtr {
        std::string characterId = def.params.at("characterId");
        std::string dialogueFile = def.params.count("dialogueFile") ? 
                                  def.params.at("dialogueFile") : "";
        bool autoAdvance = def.boolParams.count("autoAdvance") ? 
                          def.boolParams.at("autoAdvance") : false;
        
        return std::make_shared<DialogueEvent>(def.name, characterId, dialogueFile, autoAdvance);
    });
```

#### Using Custom Events
```cpp
// Create quest events using the custom creator
EventDefinition questDef;
questDef.type = "QuestEvent";
questDef.name = "rescue_princess";
questDef.params["questId"] = "quest_001";
questDef.params["questType"] = "main";
questDef.numParams["level"] = 5.0f;
questDef.boolParams["optional"] = false;

auto questEvent = EventFactory::Instance().createEvent(questDef);

// Create dialogue events
EventDefinition dialogueDef;
dialogueDef.type = "DialogueEvent";
dialogueDef.name = "king_greeting";
dialogueDef.params["characterId"] = "king_arthur";
dialogueDef.params["dialogueFile"] = "king_intro.json";
dialogueDef.boolParams["autoAdvance"] = false;

auto dialogueEvent = EventFactory::Instance().createEvent(dialogueDef);
```

## Intelligent Defaults

### Weather Events
The factory automatically configures weather-specific parameters:

```cpp
auto fogEvent = EventFactory::Instance().createWeatherEvent("morning_fog", "Foggy", 0.8f, 3.0f);
// Automatically sets:
// - visibility = 0.8 - (0.8 * 0.7) = 0.24
// - particleEffect = "fog"
// - soundEffect = "" (fog is silent)

auto stormEvent = EventFactory::Instance().createWeatherEvent("thunderstorm", "Stormy", 0.9f, 2.0f);
// Automatically sets:
// - visibility = 0.7 - (0.9 * 0.4) = 0.34
// - particleEffect = "heavy_rain" (intensity > 0.7)
// - soundEffect = "thunder_storm"

auto clearEvent = EventFactory::Instance().createWeatherEvent("sunshine", "Clear");
// Automatically overrides:
// - intensity = 0.0 (always for clear weather)
// - visibility = 1.0
// - particleEffect = "" (no particles)
// - soundEffect = "" (no sound)
```

### Scene Transitions
Transition-specific defaults are applied automatically:

```cpp
auto fadeTransition = EventFactory::Instance().createSceneChangeEvent(
    "fade_to_black", "NextScene", "fade", 2.0f);
// Automatically sets:
// - colorR/G/B = 0.0f (black fade)
// - colorA = 1.0f (fully opaque)
// - soundEffect = "transition_fade"

auto wipeTransition = EventFactory::Instance().createSceneChangeEvent(
    "door_opening", "Interior", "wipe", 1.0f);
// Automatically sets:
// - direction = 0.0f (right to left)
// - soundEffect = "transition_wipe"

auto slideTransition = EventFactory::Instance().createSceneChangeEvent(
    "elevator_up", "UpperFloor", "slide", 1.5f);
// Automatically sets:
// - direction = 270.0f (bottom to top)
// - soundEffect = "transition_slide"
```

### NPC Spawning
Spawn-specific defaults ensure proper NPC behavior:

```cpp
auto guardSpawn = EventFactory::Instance().createNPCSpawnEvent(
    "castle_guards", "Guard", 3, 40.0f);
// Automatically sets:
// - fadeIn = true
// - fadeTime = 0.5f
// - playSpawnEffect = false
// - spawnSoundID = ""
// - circular spawn area around origin
```

## JSON Integration

### Loading from JSON
EventDefinitions can be easily loaded from JSON configuration files:

```json
{
  "events": [
    {
      "type": "Weather",
      "name": "storm_sequence_1",
      "params": {
        "weatherType": "Cloudy"
      },
      "numParams": {
        "intensity": 0.4,
        "transitionTime": 4.0,
        "priority": 3
      },
      "boolParams": {
        "active": true,
        "oneTime": false
      }
    },
    {
      "type": "SceneChange",
      "name": "dramatic_entrance", 
      "params": {
        "targetScene": "ThroneRoom",
        "transitionType": "dissolve"
      },
      "numParams": {
        "duration": 2.5,
        "priority": 5
      },
      "boolParams": {
        "oneTime": true
      }
    }
  ]
}
```

### JSON Loading Example
```cpp
// Load events from JSON (pseudo-code - actual JSON parsing depends on library)
void loadEventsFromJSON(const std::string& filename) {
    auto jsonData = loadJSONFile(filename);
    
    for (const auto& eventJson : jsonData["events"]) {
        EventDefinition def;
        def.type = eventJson["type"];
        def.name = eventJson["name"];
        
        // Load string parameters
        for (const auto& [key, value] : eventJson["params"].items()) {
            def.params[key] = value;
        }
        
        // Load numeric parameters
        for (const auto& [key, value] : eventJson["numParams"].items()) {
            def.numParams[key] = value;
        }
        
        // Load boolean parameters
        for (const auto& [key, value] : eventJson["boolParams"].items()) {
            def.boolParams[key] = value;
        }
        
        // Create the event
        auto event = EventFactory::Instance().createEvent(def);
        if (event) {
            EventManager::Instance().registerEvent(def.name, event);
        }
    }
}
```

## Performance Optimization

### Event Templates
Create reusable event templates for common patterns:

```cpp
class EventTemplates {
public:
    static EventDefinition createWeatherTemplate(const std::string& name, 
                                                const std::string& weatherType,
                                                float intensity = 0.7f) {
        EventDefinition def;
        def.type = "Weather";
        def.name = name;
        def.params["weatherType"] = weatherType;
        def.numParams["intensity"] = intensity;
        def.numParams["transitionTime"] = 3.0f;
        def.boolParams["active"] = true;
        return def;
    }
    
    static EventDefinition createCombatSpawnTemplate(const std::string& name,
                                                    const std::string& npcType,
                                                    int count) {
        EventDefinition def;
        def.type = "NPCSpawn";
        def.name = name;
        def.params["npcType"] = npcType;
        def.numParams["count"] = static_cast<float>(count);
        def.numParams["spawnRadius"] = 75.0f;
        def.numParams["priority"] = 8; // High priority for combat
        def.boolParams["active"] = true;
        return def;
    }
};

// Usage
auto rainEvent = EventFactory::Instance().createEvent(
    EventTemplates::createWeatherTemplate("battle_rain", "Rainy", 0.8f));
auto enemySpawn = EventFactory::Instance().createEvent(
    EventTemplates::createCombatSpawnTemplate("orc_reinforcements", "OrcWarrior", 5));
```

### Batch Creation
Efficiently create multiple related events:

```cpp
void createDungeonEvents() {
    std::vector<EventDefinition> dungeonEvents;
    
    // Create weather progression
    std::vector<std::pair<std::string, float>> weatherProgression = {
        {"Clear", 0.0f}, {"Cloudy", 0.3f}, {"Foggy", 0.6f}, {"Stormy", 0.9f}
    };
    
    for (size_t i = 0; i < weatherProgression.size(); ++i) {
        EventDefinition weatherDef;
        weatherDef.type = "Weather";
        weatherDef.name = "dungeon_weather_" + std::to_string(i);
        weatherDef.params["weatherType"] = weatherProgression[i].first;
        weatherDef.numParams["intensity"] = weatherProgression[i].second;
        weatherDef.numParams["transitionTime"] = 2.0f;
        weatherDef.numParams["priority"] = static_cast<float>(weatherProgression.size() - i);
        dungeonEvents.push_back(weatherDef);
    }
    
    // Create enemy waves
    std::vector<std::pair<std::string, int>> enemyWaves = {
        {"SkeletonWarrior", 3}, {"SkeletonArcher", 2}, {"SkeletonMage", 1}
    };
    
    for (size_t i = 0; i < enemyWaves.size(); ++i) {
        EventDefinition spawnDef;
        spawnDef.type = "NPCSpawn";
        spawnDef.name = "enemy_wave_" + std::to_string(i);
        spawnDef.params["npcType"] = enemyWaves[i].first;
        spawnDef.numParams["count"] = static_cast<float>(enemyWaves[i].second);
        spawnDef.numParams["spawnRadius"] = 60.0f;
        spawnDef.numParams["priority"] = static_cast<float>(enemyWaves.size() - i);
        dungeonEvents.push_back(spawnDef);
    }
    
    // Create all events as a sequence
    auto createdEvents = EventFactory::Instance().createEventSequence(
        "dungeon_progression", dungeonEvents, true);
    
    // Register all events with EventManager
    for (size_t i = 0; i < createdEvents.size() && i < dungeonEvents.size(); ++i) {
        EventManager::Instance().registerEvent(dungeonEvents[i].name, createdEvents[i]);
    }
}
```

## Error Handling

### Validation and Fallbacks
```cpp
EventPtr createSafeEvent(const EventDefinition& def) {
    try {
        // Validate required parameters
        if (def.type.empty()) {
            std::cerr << "Error: Event type is required" << std::endl;
            return nullptr;
        }
        
        if (def.name.empty()) {
            std::cerr << "Error: Event name is required" << std::endl;
            return nullptr;
        }
        
        // Type-specific validation
        if (def.type == "Weather") {
            if (def.params.find("weatherType") == def.params.end()) {
                std::cerr << "Warning: No weatherType specified, defaulting to 'Clear'" << std::endl;
            }
        }
        
        // Create the event
        auto event = EventFactory::Instance().createEvent(def);
        if (!event) {
            std::cerr << "Error: Failed to create event '" << def.name << "' of type '" << def.type << "'" << std::endl;
            return nullptr;
        }
        
        return event;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception creating event '" << def.name << "': " << e.what() << std::endl;
        return nullptr;
    }
}
```

### Parameter Validation
```cpp
bool validateEventDefinition(const EventDefinition& def) {
    // Check required fields
    if (def.type.empty() || def.name.empty()) {
        return false;
    }
    
    // Type-specific validation
    if (def.type == "Weather") {
        auto it = def.params.find("weatherType");
        if (it != def.params.end()) {
            std::vector<std::string> validTypes = {"Clear", "Cloudy", "Rainy", "Stormy", "Foggy", "Snowy", "Windy"};
            if (std::find(validTypes.begin(), validTypes.end(), it->second) == validTypes.end()) {
                std::cerr << "Invalid weather type: " << it->second << std::endl;
                return false;
            }
        }
    }
    
    // Validate numeric ranges
    for (const auto& [key, value] : def.numParams) {
        if (key == "intensity" && (value < 0.0f || value > 1.0f)) {
            std::cerr << "Intensity must be between 0.0 and 1.0" << std::endl;
            return false;
        }
        if (key == "duration" && value < 0.0f) {
            std::cerr << "Duration must be non-negative" << std::endl;
            return false;
        }
    }
    
    return true;
}
```

## Integration Examples

### Complete Weather System
```cpp
class AdvancedWeatherSystem {
private:
    std::vector<EventPtr> m_weatherEvents;
    size_t m_currentEventIndex = 0;
    
public:
    void initializeWeatherSystem() {
        // Create weather cycle using EventDefinitions
        std::vector<EventDefinition> weatherCycle = {
            {
                .type = "Weather",
                .name = "dawn_clear",
                .params = {{"weatherType", "Clear"}},
                .numParams = {{"intensity", 1.0f}, {"transitionTime", 2.0f}}
            },
            {
                .type = "Weather", 
                .name = "morning_fog",
                .params = {{"weatherType", "Foggy"}},
                .numParams = {{"intensity", 0.4f}, {"transitionTime", 3.0f}}
            },
            {
                .type = "Weather",
                .name = "afternoon_clouds",
                .params = {{"weatherType", "Cloudy"}},
                .numParams = {{"intensity", 0.6f}, {"transitionTime", 4.0f}}
            },
            {
                .type = "Weather",
                .name = "evening_rain",
                .params = {{"weatherType", "Rainy"}},
                .numParams = {{"intensity", 0.7f}, {"transitionTime", 2.0f}}
            },
            {
                .type = "Weather",
                .name = "night_storm",
                .params = {{"weatherType", "Stormy"}},
                .numParams = {{"intensity", 0.9f}, {"transitionTime", 1.5f}}
            }
        };
        
        // Create all weather events
        for (const auto& weatherDef : weatherCycle) {
            auto event = EventFactory::Instance().createEvent(weatherDef);
            if (event) {
                m_weatherEvents.push_back(event);
                EventManager::Instance().registerEvent(weatherDef.name, event);
            }
        }
    }
    
    void advanceWeather() {
        if (m_weatherEvents.empty()) return;
        
        auto currentEvent = m_weatherEvents[m_currentEventIndex];
        currentEvent->execute();
        
        m_currentEventIndex = (m_currentEventIndex + 1) % m_weatherEvents.size();
    }
    
    void triggerRandomWeather() {
        if (m_weatherEvents.empty()) return;
        
        size_t randomIndex = rand() % m_weatherEvents.size();
        m_weatherEvents[randomIndex]->execute();
    }
};
```

### Dynamic Quest System
```cpp
class QuestEventManager {
public:
    void initializeQuestSystem() {
        // Register quest event creator
        EventFactory::Instance().registerCustomEventCreator("QuestEvent",
            [](const EventDefinition& def) -> EventPtr {
                return createQuestEvent(def);
            });
        
        // Register dialogue event creator  
        EventFactory::Instance().registerCustomEventCreator("DialogueEvent",
            [](const EventDefinition& def) -> EventPtr {
                return createDialogueEvent(def);
            });
    }
    
    void createMainQuestLine() {
        std::vector<EventDefinition> mainQuest = {
            {
                .type = "DialogueEvent",
                .name = "king_introduction",
                .params = {{"characterId", "king"}, {"dialogueFile", "intro.json"}},
                .boolParams = {{"oneTime", true}}
            },
            {
                .type = "QuestEvent", 
                .name = "rescue_mission",
                .params = {{"questId", "main_001"}, {"questType", "rescue"}},
                .numParams = {{"level", 1.0f}},
                .boolParams = {{"optional", false}}
            },
            {
                .type = "NPCSpawn",
                .name = "quest_enemies",
                .params = {{"npcType", "Bandit"}},
                .numParams = {{"count", 5.0f}, {"spawnRadius", 100.0f}}
            },
            {
                .type = "SceneChange",
                .name = "travel_to_hideout",
                .params = {{"targetScene", "BanditHideout"}, {"transitionType", "fade"}},
                .numParams = {{"duration", 2.0f}}
            }
        };
        
        auto questEvents = EventFactory::Instance().createEventSequence(
            "main_questline", mainQuest, true);
    }
    
private:
    static EventPtr createQuestEvent(const EventDefinition& def) {
        // Implementation for quest event creation
        return std::make_shared<QuestEvent>(def.name, 
                                          def.params.at("questId"),
                                          def.params.at("questType"));
    }
    
    static EventPtr createDialogueEvent(const EventDefinition& def) {
        // Implementation for dialogue event creation
        return std::make_shared<DialogueEvent>(def.name,
                                             def.params.at("characterId"), 
                                             def.params.at("dialogueFile"));
    }
};
```

## Best Practices

### 1. Use Appropriate Creation Methods
```cpp
// For simple, one-off events - use direct methods
auto quickRain = EventFactory::Instance().createWeatherEvent("quick_rain", "Rainy", 0.5f);

// For complex, configurable events - use EventDefinition
EventDefinition complexStorm;
complexStorm.type = "Weather";
complexStorm.name = "epic_storm";
complexStorm.params["weatherType"] = "Stormy";
complexStorm.numParams["intensity"] = 0.95f;
complexStorm.numParams["priority"] = 9;
complexStorm.boolParams["oneTime"] = true;
auto epicStorm = EventFactory::Instance().createEvent(complexStorm);

// For related events - use sequences
auto eventChain = EventFactory::Instance().createEventSequence("boss_intro", definitions, true);
```

### 2. Leverage Intelligent Defaults
```cpp
// Let the factory configure details automatically
auto fogEvent = EventFactory::Instance().createWeatherEvent("fog", "Foggy", 0.8f);
// Automatically gets: reduced visibility, fog particles, no sound

auto fadeScene = EventFactory::Instance().createSceneChangeEvent("fade_out", "NextLevel", "fade");
// Automatically gets: black fade, transition sound, proper timing
```

### 3. Create Reusable Templates
```cpp
class EventPatterns {
public:
    static std::vector<EventDefinition> createCombatSequence(const std::string& enemyType, int waves) {
        std::vector<EventDefinition> sequence;
        
        // Add pre-combat weather
        sequence.push_back({
            .type = "Weather",
            .name = "pre_combat_atmosphere",
            .params = {{"weatherType", "Stormy"}},
            .numParams = {{"intensity", 0.7f}}
        });
        
        // Add enemy waves
        for (int i = 0; i < waves; ++i) {
            sequence.push_back({
                .type = "NPCSpawn",
                .name = "wave_" + std::to_string(i),
                .params = {{"npcType", enemyType}},
                .numParams = {{"count", 3.0f + i}, {"spawnRadius", 80.0f}}
            });
        }
        
        return sequence;
    }
};
```

### 4. Validate Configurations
```cpp
void createEventSafely(const EventDefinition& def) {
    if (!validateEventDefinition(def)) {
        std::cerr << "Invalid event definition for: " << def.name << std::endl;
        return;
    }
    
    auto event = EventFactory::Instance().createEvent(def);
    if (event) {
        EventManager::Instance().registerEvent(def.name, event);
    }
}
```

### 5. Use JSON for Complex Scenarios
```cpp
// Store complex event scenarios in JSON files
// Load and modify them without recompiling
void loadScenarioEvents(const std::string& scenarioName) {
    std::string filename = "scenarios/" + scenarioName + ".json";
    loadEventsFromJSON(filename);
}
```

## Migration Guide

### From Direct EventManager to EventFactory

#### Old Approach
```cpp
// Old: Create event, then register separately
auto event = std::make_shared<WeatherEvent>("rain", WeatherType::Rainy);
WeatherParams params;
params.intensity = 0.8f;
params.transitionTime = 3.0f;
event->setWeatherParams(params);
EventManager::Instance().registerEvent("rain", event);
```

#### New Approach
```cpp
// New: Use EventFactory convenience method
auto event = EventFactory::Instance().createWeatherEvent("rain", "Rainy", 0.8f, 3.0f);
EventManager::Instance().registerEvent("rain", event);

// Or even better: Use EventManager convenience method
EventManager::Instance().createWeatherEvent("rain", "Rainy", 0.8f, 3.0f);
```

#### Advanced Approach
```cpp
// Advanced: Use EventDefinition for maximum flexibility
EventDefinition rainDef;
rainDef.type = "Weather";
rainDef.name = "rain";
rainDef.params["weatherType"] = "Rainy";
rainDef.numParams["intensity"] = 0.8f;
rainDef.numParams["transitionTime"] = 3.0f;
rainDef.numParams["priority"] = 5;
rainDef.boolParams["oneTime"] = false;

auto event = EventFactory::Instance().createEvent(rainDef);
EventManager::Instance().registerEvent("rain", event);
```

The EventFactory provides the most comprehensive and flexible event creation system, suitable for everything from simple prototypes to complex, data-driven event systems. Choose the approach that best fits your project's complexity and requirements.