# EventFactory

Where to find the code:
- Header: `include/events/EventFactory.hpp`
- Implementation: `src/events/EventFactory.cpp`

Overview
- Centralized builder for events from simple definitions.
- Decouples data-driven definitions from concrete event classes.
- Provides helpers for common event families and supports custom creators.

Core Types
- `EventDefinition` struct encapsulates type/name/params for creation:
  - `type`: string like "Weather", "SceneChange", "NPCSpawn", "ParticleEffect", "WorldLoaded", "WorldUnloaded", "TileChanged", "WorldGenerated", "CameraMoved", "CameraModeChanged", "CameraShake", "ResourceChange", or any custom type you register.
  - `name`: unique event name.
  - `params`: map<string,string> of string parameters.
  - `numParams`: map<string,float> of numeric parameters.
  - `boolParams`: map<string,bool> of boolean parameters.

Quick Start
```cpp
#include "events/EventFactory.hpp"
#include "managers/EventManager.hpp"

// Create a weather event from a definition
EventDefinition def{
  .type = "Weather",
  .name = "EpicStorm",
  .params = {{"weatherType","Stormy"}},
  .numParams = {{"intensity",0.95f},{"transitionTime",1.5f}},
};

auto ev = EventFactory::Instance().createEvent(def);
if (ev) {
  // Optional: tune common Event properties
  ev->setPriority(7);
  ev->setOneTime(true);
  // Register with EventManager
  EventManager::Instance().registerEvent(def.name, ev);
}
```

Built-in Creators (Helpers)
- Weather: `createWeatherEvent(name, weatherType, intensity=0.5f, transitionTime=5.0f)`
- Scene: `createSceneChangeEvent(name, targetScene, transitionType="fade", duration=1.0f)`
- NPC: `createNPCSpawnEvent(name, npcType, count=1, spawnRadius=0.0f)`
- Particle: `createParticleEffectEvent(name, effectName, x, y, intensity=1.0f, duration=-1.0f, groupTag="", soundEffect="")`
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

Custom Event Creators
```cpp
// Register a custom type "Quest" so definitions with type="Quest" are supported
EventFactory::Instance().registerCustomEventCreator(
  "Quest",
  [](const EventDefinition& def) -> EventPtr {
    // Build your Event subclass from def.params/def.numParams/boolParams
    return std::make_shared<MyQuestEvent>(def.name, /* ... */);
  }
);

// Create via definition and register
EventDefinition q{.type="Quest", .name="FindTreasure",
                  .params={{"questId","treasure_hunt"}}, .numParams={{"reward",1000.0f}}};
auto quest = EventFactory::Instance().createEvent(q);
if (quest) EventManager::Instance().registerEvent(q.name, quest);
```

Event Sequences
```cpp
// Create a sequence that will execute in order if sequential=true
std::vector<EventDefinition> defs = {
  {.type="Weather", .name="StartRain", .params={{"weatherType","Rainy"}}, .numParams={{"intensity",0.5f}}},
  {.type="Weather", .name="GetStormy", .params={{"weatherType","Stormy"}}, .numParams={{"intensity",0.9f}}},
  {.type="Weather", .name="ClearUp", .params={{"weatherType","Clear"}}},
};
auto events = EventFactory::Instance().createEventSequence("StoryWeather", defs, /*sequential=*/true);
for (auto &e : events) { EventManager::Instance().registerEvent(e->getName(), e); }
```

Tips
- Prefer EventFactory for data/definition-driven creation and custom types.
- Use EventManager convenience methods for quick one-off creation when definitions aren’t needed.
- After creation, adjust common Event properties (`setPriority`, `setOneTime`, `setCooldown`) before registering.

