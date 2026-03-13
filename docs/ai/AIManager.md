# AIManager

**Code:** `include/managers/AIManager.hpp`, `src/managers/AIManager.cpp`

## Overview

`AIManager` is now a data processor and orchestrator. It does not own per-entity polymorphic behavior objects. Behavior logic lives in free functions under `Behaviors::`, while persistent state and configuration live in `EntityDataManager` (EDM).

Current responsibilities:

- register known behavior names
- assign or remove behavior configs on entities
- build `BehaviorContext` for batch execution
- choose single-threaded vs multi-threaded execution through `WorkerBudget`
- collect deferred events from worker batches
- apply command-bus results and faction changes on the main thread
- maintain fast spatial scans for active entities, factions, and guards

## Architecture

### Source of Truth

- `EntityDataManager`: transform, hot data, behavior config/state, path data, NPC memory, character data
- `AIManager`: orchestration, batching, scans, player cache, update sequencing
- `BehaviorExecutors`: per-behavior execute/init functions
- `AICommandBus`: queued behavior messages, transitions, and faction changes

### Update Pipeline

1. gather active EDM indices
2. cache per-frame player position, world bounds, and game time
3. run emotional contagion / combat-memory pre-pass where needed
4. ask `WorkerBudgetManager` for threading decision
5. execute behaviors with `BehaviorContext`
6. flush deferred `EventManager::DeferredEvent` batches
7. drain and commit command-bus outputs on the main thread

## Behavior Assignment

Register built-in names once:

```cpp
AIManager::Instance().registerDefaultBehaviors();
```

Assign by name:

```cpp
aiMgr.assignBehavior(handle, "Guard");
```

Assign by full config:

```cpp
HammerEngine::BehaviorConfigData cfg{};
cfg.type = BehaviorType::Attack;
aiMgr.assignBehavior(handle, cfg);
```

There is no longer a `clone()`-based behavior instance model.

## Spatial Queries

Branch-local helper APIs:

- `scanActiveHandlesInRadius(...)`
- `scanActiveIndicesInRadius(...)`
- `scanGuardsInRadius(...)`
- `scanFactionInRadius(...)`

Prefer EDM indices in behavior code to avoid repeated handle-to-index lookups.

## Combat and Memory Integration

Combat and social reactions now rely on EDM-backed memory data plus AI-layer logic:

- `handleCombatEvent(...)` routes combat information into current AI processing
- `Behaviors::processCombatEvent(...)` records EDM facts and applies AI-layer emotional changes
- `Behaviors::processWitnessedCombat(...)` records witnessed combat and distance-scaled reactions
- `AIManager::update()` runs emotional contagion on the main thread before worker batches

See [NPC Memory](NPCMemory.md).

## Threading Model

`AIManager` follows the repository threading pattern:

```cpp
auto decision = WorkerBudgetManager::Instance().shouldUseThreading(SystemType::AI, count);
// execute
WorkerBudgetManager::Instance().reportExecution(SystemType::AI, count,
    decision.shouldThread, batchCount, elapsedMs);
```

Worker threads do not mutate global AI ownership structures directly. They operate on EDM-backed entity data and emit deferred outputs.

## State Transition

Call `prepareForStateTransition()` before tearing down AI-heavy states. This prevents stale work, clears transient orchestration state, and aligns with the expected manager shutdown order.

## Related Docs

- [Behavior Execution Pipeline](BehaviorExecutionPipeline.md)
- [Behavior Modes](BehaviorModes.md)
- [Behavior Quick Reference](BehaviorQuickReference.md)
- [NPC Memory](NPCMemory.md)
