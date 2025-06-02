# EventFactory Quick Reference

## Basic Event Creation

### Weather Events
```cpp
auto event = EventFactory::Instance().createWeatherEvent("name", "weatherType", intensity, transitionTime);

// Examples
auto rain = EventFactory::Instance().createWeatherEvent("rain", "Rainy", 0.8f, 3.0f);
auto storm = EventFactory::Instance().createWeatherEvent("storm", "Stormy", 0.9f, 2.0f);
auto fog = EventFactory::Instance().createWeatherEvent("fog", "Foggy", 0.6f, 4.0f);
auto clear = EventFactory::Instance().createWeatherEvent("clear", "Clear", 1.0f, 1.0f);
```

### Scene Change Events
```cpp
auto event = EventFactory::Instance().createSceneChangeEvent("name", "targetScene", "transitionType", duration);

// Examples
auto fade = EventFactory::Instance().createSceneChangeEvent("fade_out", "NextLevel", "fade", 2.0f);
auto slide = EventFactory::Instance().createSceneChangeEvent("slide_up", "UpperFloor", "slide", 1.5f);
auto dissolve = EventFactory::Instance().createSceneChangeEvent("magic_portal", "MagicRealm", "dissolve", 2.5f);
auto wipe = EventFactory::Instance().createSceneChangeEvent("door_open", "Interior", "wipe", 1.0f);
```

### NPC Spawn Events
```cpp
auto event = EventFactory::Instance().createNPCSpawnEvent("name", "npcType", count, spawnRadius);

// Examples
auto guards = EventFactory::Instance().createNPCSpawnEvent("guards", "Guard", 3, 50.0f);
auto villagers = EventFactory::Instance().createNPCSpawnEvent("villagers", "Villager", 5, 75.0f);
auto merchant = EventFactory::Instance().createNPCSpawnEvent("merchant", "Merchant", 1, 25.0f);
```

## EventDefinition Structure

### Basic Structure
```cpp
EventDefinition def;
def.type = "EventType";           // "Weather", "SceneChange", "NPCSpawn", or custom
def.name = "unique_name";         // Unique identifier
def.params["key"] = "value";      // String parameters
def.numParams["key"] = 1.0f;      // Numeric parameters
def.boolParams["key"] = true;     // Boolean parameters

auto event = EventFactory::Instance().createEvent(def);
```

### Weather Event Definition
```cpp
EventDefinition weatherDef;
weatherDef.type = "Weather";
weatherDef.name = "epic_storm";
weatherDef.params["weatherType"] = "Stormy";
weatherDef.numParams["intensity"] = 0.95f;
weatherDef.numParams["transitionTime"] = 1.5f;
weatherDef.numParams["priority"] = 8;
weatherDef.numParams["cooldown"] = 30.0f;
weatherDef.boolParams["oneTime"] = false;
weatherDef.boolParams["active"] = true;
```

### Scene Change Event Definition
```cpp
EventDefinition sceneDef;
sceneDef.type = "SceneChange";
sceneDef.name = "boss_entrance";
sceneDef.params["targetScene"] = "BossArena";
sceneDef.params["transitionType"] = "wipe";
sceneDef.numParams["duration"] = 2.0f;
sceneDef.numParams["priority"] = 9;
sceneDef.boolParams["oneTime"] = true;
```

### NPC Spawn Event Definition
```cpp
EventDefinition npcDef;
npcDef.type = "NPCSpawn";
npcDef.name = "enemy_wave";
npcDef.params["npcType"] = "OrcWarrior";
npcDef.numParams["count"] = 5.0f;
npcDef.numParams["spawnRadius"] = 100.0f;
npcDef.numParams["priority"] = 7;
npcDef.boolParams["active"] = true;
```

## Event Sequences

### Sequential Events
```cpp
std::vector<EventDefinition> sequence = {
    {
        .type = "Weather",
        .name = "approaching_storm",
        .params = {{"weatherType", "Cloudy"}},
        .numParams = {{"intensity", 0.4f}, {"transitionTime", 3.0f}}
    },
    {
        .type = "Weather",
        .name = "full_storm",
        .params = {{"weatherType", "Stormy"}},
        .numParams = {{"intensity", 0.9f}, {"transitionTime", 1.5f}}
    },
    {
        .type = "NPCSpawn",
        .name = "storm_enemies",
        .params = {{"npcType", "StormElemental"}},
        .numParams = {{"count", 2.0f}, {"spawnRadius", 80.0f}}
    }
};

auto events = EventFactory::Instance().createEventSequence("storm_sequence", sequence, true);
```

### Simultaneous Events
```cpp
std::vector<EventDefinition> simultaneous = {
    {
        .type = "Weather",
        .name = "battle_atmosphere",
        .params = {{"weatherType", "Stormy"}},
        .numParams = {{"intensity", 0.8f}}
    },
    {
        .type = "SceneChange",
        .name = "arena_transition",
        .params = {{"targetScene", "Arena"}, {"transitionType", "fade"}},
        .numParams = {{"duration", 1.5f}}
    },
    {
        .type = "NPCSpawn",
        .name = "arena_enemies",
        .params = {{"npcType", "Gladiator"}},
        .numParams = {{"count", 3.0f}}
    }
};

auto events = EventFactory::Instance().createEventSequence("arena_start", simultaneous, false);
```

## Custom Event Creators

### Register Custom Creator
```cpp
EventFactory::Instance().registerCustomEventCreator("QuestEvent",
    [](const EventDefinition& def) -> EventPtr {
        std::string questId = def.params.at("questId");
        std::string questType = def.params.count("questType") ? def.params.at("questType") : "main";
        bool optional = def.boolParams.count("optional") ? def.boolParams.at("optional") : false;
        int level = static_cast<int>(def.numParams.count("level") ? def.numParams.at("level") : 1.0f);
        
        return std::make_shared<QuestEvent>(def.name, questId, questType, optional, level);
    });
```

### Use Custom Event
```cpp
EventDefinition questDef;
questDef.type = "QuestEvent";
questDef.name = "rescue_princess";
questDef.params["questId"] = "quest_001";
questDef.params["questType"] = "main";
questDef.numParams["level"] = 5.0f;
questDef.boolParams["optional"] = false;

auto questEvent = EventFactory::Instance().createEvent(questDef);
```

## Intelligent Defaults

### Weather Types
- **"Clear"**: intensity=0.0, visibility=1.0, no particles/sounds
- **"Rainy"**: visibility=0.7-(intensity*0.4), particles="rain", sound="rain_ambient"
- **"Stormy"**: visibility=0.7-(intensity*0.4), particles="heavy_rain", sound="thunder_storm"
- **"Foggy"**: visibility=0.8-(intensity*0.7), particles="fog", no sound
- **"Snowy"**: visibility=0.8-(intensity*0.3), particles="snow", sound="snow_ambient"

### Transition Types
- **"fade"**: Black fade, colorRGB=0.0, colorA=1.0, sound="transition_fade"
- **"dissolve"**: sound="transition_dissolve"
- **"wipe"**: direction=0.0 (right-to-left), sound="transition_wipe"
- **"slide"**: direction=270.0 (bottom-to-top), sound="transition_slide"
- **"instant"**: duration=0.0, no sound

### NPC Spawn Defaults
- **fadeIn**: true
- **fadeTime**: 0.5f
- **playSpawnEffect**: false
- **spawnSoundID**: ""
- **spawn area**: circular around origin

## Common Parameters

### Event Parameters (all types)
```cpp
def.numParams["priority"] = 5.0f;        // Event priority (0-10)
def.numParams["updateFrequency"] = 1.0f; // Update frequency
def.numParams["cooldown"] = 10.0f;       // Cooldown time in seconds
def.boolParams["oneTime"] = true;        // Execute only once
def.boolParams["active"] = true;         // Initially active
```

### Weather Parameters
```cpp
def.params["weatherType"] = "Rainy";     // Weather type string
def.numParams["intensity"] = 0.8f;       // Intensity (0.0-1.0)
def.numParams["transitionTime"] = 3.0f;  // Transition duration
```

### Scene Change Parameters
```cpp
def.params["targetScene"] = "NextLevel"; // Target scene ID
def.params["transitionType"] = "fade";   // Transition type
def.numParams["duration"] = 2.0f;        // Transition duration
```

### NPC Spawn Parameters
```cpp
def.params["npcType"] = "Guard";         // NPC type to spawn
def.numParams["count"] = 3.0f;           // Number to spawn
def.numParams["spawnRadius"] = 50.0f;    // Spawn radius
```

## Validation

### Required Parameters
- **All Events**: `type`, `name`
- **Weather**: `weatherType` (optional, defaults to "Clear")
- **SceneChange**: `targetScene` (required)
- **NPCSpawn**: `npcType` (required)

### Valid Values
- **Weather Types**: "Clear", "Cloudy", "Rainy", "Stormy", "Foggy", "Snowy", "Windy"
- **Transition Types**: "fade", "dissolve", "wipe", "slide", "instant"
- **Intensity**: 0.0 - 1.0
- **Duration/Time**: >= 0.0
- **Count**: >= 1
- **Priority**: 0 - 10

## Error Handling

### Safe Event Creation
```cpp
EventPtr createSafeEvent(const EventDefinition& def) {
    if (def.type.empty() || def.name.empty()) {
        std::cerr << "Error: type and name are required" << std::endl;
        return nullptr;
    }
    
    try {
        return EventFactory::Instance().createEvent(def);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return nullptr;
    }
}
```

### Validation Function
```cpp
bool isValidEventDefinition(const EventDefinition& def) {
    if (def.type.empty() || def.name.empty()) return false;
    
    if (def.type == "Weather") {
        auto it = def.params.find("weatherType");
        if (it != def.params.end()) {
            std::vector<std::string> valid = {"Clear", "Cloudy", "Rainy", "Stormy", "Foggy", "Snowy", "Windy"};
            return std::find(valid.begin(), valid.end(), it->second) != valid.end();
        }
    }
    
    return true;
}
```

## Complete Examples

### Weather System
```cpp
void setupWeatherCycle() {
    std::vector<EventDefinition> cycle = {
        {.type = "Weather", .name = "dawn", .params = {{"weatherType", "Clear"}}, .numParams = {{"intensity", 1.0f}}},
        {.type = "Weather", .name = "morning_fog", .params = {{"weatherType", "Foggy"}}, .numParams = {{"intensity", 0.4f}}},
        {.type = "Weather", .name = "afternoon_clouds", .params = {{"weatherType", "Cloudy"}}, .numParams = {{"intensity", 0.6f}}},
        {.type = "Weather", .name = "evening_rain", .params = {{"weatherType", "Rainy"}}, .numParams = {{"intensity", 0.7f}}},
        {.type = "Weather", .name = "night_storm", .params = {{"weatherType", "Stormy"}}, .numParams = {{"intensity", 0.9f}}}
    };
    
    auto weatherEvents = EventFactory::Instance().createEventSequence("daily_cycle", cycle, true);
}
```

### Combat Encounter
```cpp
void createCombatEncounter() {
    std::vector<EventDefinition> combat = {
        {
            .type = "Weather",
            .name = "battle_storm",
            .params = {{"weatherType", "Stormy"}},
            .numParams = {{"intensity", 0.8f}, {"transitionTime", 1.0f}}
        },
        {
            .type = "NPCSpawn",
            .name = "first_wave",
            .params = {{"npcType", "OrcWarrior"}},
            .numParams = {{"count", 3.0f}, {"spawnRadius", 60.0f}}
        },
        {
            .type = "NPCSpawn",
            .name = "second_wave",
            .params = {{"npcType", "OrcArcher"}},
            .numParams = {{"count", 2.0f}, {"spawnRadius", 80.0f}}
        },
        {
            .type = "SceneChange",
            .name = "victory_scene",
            .params = {{"targetScene", "Victory"}, {"transitionType", "fade"}},
            .numParams = {{"duration", 2.0f}}
        }
    };
    
    auto combatEvents = EventFactory::Instance().createEventSequence("orc_battle", combat, true);
}
```

### Boss Fight Intro
```cpp
void createBossFight() {
    // Simultaneous atmospheric effects
    std::vector<EventDefinition> atmosphere = {
        {
            .type = "Weather",
            .name = "boss_weather",
            .params = {{"weatherType", "Stormy"}},
            .numParams = {{"intensity", 0.95f}}
        },
        {
            .type = "SceneChange",
            .name = "boss_arena",
            .params = {{"targetScene", "BossArena"}, {"transitionType", "wipe"}},
            .numParams = {{"duration", 2.0f}}
        }
    };
    
    auto atmosphereEvents = EventFactory::Instance().createEventSequence("boss_atmosphere", atmosphere, false);
    
    // Sequential enemy spawns
    std::vector<EventDefinition> spawns = {
        {
            .type = "NPCSpawn",
            .name = "boss_minions",
            .params = {{"npcType", "SkeletonArcher"}},
            .numParams = {{"count", 4.0f}, {"spawnRadius", 100.0f}}
        },
        {
            .type = "NPCSpawn",
            .name = "boss_spawn",
            .params = {{"npcType", "DragonLord"}},
            .numParams = {{"count", 1.0f}, {"spawnRadius", 0.0f}}
        }
    };
    
    auto spawnEvents = EventFactory::Instance().createEventSequence("boss_spawns", spawns, true);
}
```