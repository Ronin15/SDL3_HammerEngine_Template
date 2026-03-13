# EventManager Quick Reference

## Core Calls

```cpp
auto& eventMgr = EventManager::Instance();

eventMgr.init();
eventMgr.update();
eventMgr.drainAllDeferredEvents();
eventMgr.prepareForStateTransition();
eventMgr.clean();
```

## Handler API

```cpp
uint64_t registerHandlerWithToken(EventTypeId, FastEventHandler);
void registerHandler(EventTypeId, FastEventHandler);
void removeHandler(EventTypeId, uint64_t token);
```

Use tokens for GameStates, controllers, and any subscription with explicit teardown.

## Deferred Queue API

```cpp
struct EventManager::DeferredEvent {
    EventTypeId typeId;
    EventData data;
};

void enqueueBatch(std::vector<DeferredEvent>&& events);
void update();
void drainAllDeferredEvents();
```

## EventTypeId

```cpp
Weather, SceneChange, NPCSpawn, ParticleEffect,
ResourceChange, World, Camera, Harvest,
Collision, WorldTrigger, CollisionObstacleChanged, Custom,
Time, Combat, Entity, BehaviorMessage
```

## Current Usage Rules

- Do not use removed APIs such as `registerEvent`, `createSceneChangeEvent`, `getEventsByType`, or compaction helpers.
- Use deferred dispatch for worker-thread producers and cross-system frame coordination.
- Use immediate dispatch only when the caller owns timing and thread-safety.
- Use `drainAllDeferredEvents()` only in tests or controlled synchronization points.
