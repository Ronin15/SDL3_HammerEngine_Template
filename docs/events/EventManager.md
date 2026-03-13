# EventManager

**Code:** `include/managers/EventManager.hpp`, `src/managers/EventManager.cpp`

## Overview

`EventManager` is the engine's dispatch hub. It no longer owns a registry of named events or a persistent event store. Systems either:

- trigger an event immediately
- enqueue a deferred event for the main-thread drain pass
- register handlers for a specific `EventTypeId`

The current design is optimized for branch-local behavior in `resource-combat-updates`:

- type-indexed handler lookup
- deferred FIFO queue for cross-system coordination
- batch enqueue for worker-thread producers
- token-based handler removal for state-scoped subscriptions
- deterministic queue draining for tests

## Responsibility Boundary

`EventManager` is responsible for:

- handler registration and removal
- immediate dispatch
- deferred dispatch queue processing
- pooled helpers for frequently emitted event types

`EventManager` is not responsible for:

- storing long-lived named events
- scene-transition registration
- query APIs like `getEventsByType()`
- event compaction or persistent event activation flags

## Core API

### Lifecycle

```cpp
auto& eventMgr = EventManager::Instance();
eventMgr.init();
eventMgr.update();                  // Drain deferred queue for this frame
eventMgr.drainAllDeferredEvents();  // Test helper
eventMgr.prepareForStateTransition();
eventMgr.clean();
```

### Handler Registration

```cpp
auto token = eventMgr.registerHandlerWithToken(
    EventTypeId::ResourceChange,
    [](const EventData& data) {
        // Inspect data.event here
    });

eventMgr.removeHandler(EventTypeId::ResourceChange, token);
```

Use `registerHandlerWithToken()` for state-owned subscriptions. `ControllerBase` and GameStates should store and remove tokens during teardown.

### Deferred Batch Enqueue

Worker threads should accumulate local events and flush once:

```cpp
std::vector<EventManager::DeferredEvent> deferred;
deferred.push_back({EventTypeId::Combat, combatData});
deferred.push_back({EventTypeId::BehaviorMessage, behaviorMsgData});

EventManager::Instance().enqueueBatch(std::move(deferred));
```

This is the preferred path for AI/combat worker code because it reduces lock acquisitions to one per batch.

## Dispatch Modes

- `DispatchMode::Immediate`: call handlers now
- `DispatchMode::Deferred`: enqueue and let `update()` drain later on the main thread

Use deferred mode when the producer is off-thread, when ordering should align with frame-end processing, or when multiple systems may mutate shared state in response.

## Event Types

Current `EventTypeId` values:

```cpp
Weather, SceneChange, NPCSpawn, ParticleEffect,
ResourceChange, World, Camera, Harvest,
Collision, WorldTrigger, CollisionObstacleChanged, Custom,
Time, Combat, Entity, BehaviorMessage
```

Branch-specific additions and changes worth documenting:

- `BehaviorMessage` covers inter-entity AI signaling such as `RAISE_ALERT`
- combat-facing events now route through `EventTypeId::Combat`
- theft/social flows emit normal event traffic instead of bespoke controller-only state
- `ResourceChangeEvent` is reused heavily by inventory, harvesting, and UI sync paths

## Common Patterns

### State-scoped subscription

```cpp
void SomeState::registerEventHandlers() {
    auto& eventMgr = EventManager::Instance();
    m_resourceToken = eventMgr.registerHandlerWithToken(
        EventTypeId::ResourceChange,
        [this](const EventData& data) { onResourceChanged(data); });
}

void SomeState::unregisterEventHandlers() {
    auto& eventMgr = EventManager::Instance();
    eventMgr.removeHandler(EventTypeId::ResourceChange, m_resourceToken);
}
```

### AI worker event production

```cpp
// In worker batch code
threadLocalDeferredEvents.clear();
// fill threadLocalDeferredEvents...
EventManager::Instance().enqueueBatch(std::move(threadLocalDeferredEvents));
```

### Test draining

```cpp
eventMgr.triggerResourceChange(..., EventManager::DispatchMode::Deferred);
eventMgr.drainAllDeferredEvents();
```

Use this in tests when assertions depend on deferred handlers having run.

## State Transition Guidance

Before a GameState tears down AI-, world-, or UI-heavy systems:

1. remove state-owned handler tokens
2. call `prepareForStateTransition()`
3. continue manager cleanup in dependency order

This prevents stale handler callbacks from firing during shutdown.

## Related Docs

- [EventManager Quick Reference](EventManager_QuickReference.md)
- [EventManager Advanced](EventManager_Advanced.md)
- [EventFactory](EventFactory.md)
- [AI Execution Pipeline](../ai/BehaviorExecutionPipeline.md)
