# Behavior Execution Pipeline

## Overview

The branch replaces per-entity virtual behavior objects with a data-oriented pipeline:

1. `AIManager` gathers active EDM indices
2. it builds a `BehaviorContext` per entity
3. `Behaviors::execute(...)` dispatches by `BehaviorType`
4. worker code emits command-bus changes and deferred events
5. the main thread commits transitions and drains deferred event batches

## BehaviorContext

`BehaviorContext` provides lock-free access to the data a behavior needs for one update:

- transform and hot entity data
- EDM index
- cached player handle, position, and velocity
- pre-fetched `BehaviorData`, `PathData`, `NPCMemoryData`, and `CharacterData`
- cached world bounds
- cached game time

The goal is to avoid repeated singleton lookups and scattered map access during the hot loop.

## Dispatch and Initialization

- `Behaviors::execute(ctx, configData)` routes to the correct free function
- `Behaviors::init(edmIndex, configData)` initializes EDM-backed state when a behavior is assigned

Behavior switching must preserve EDM state correctly; see the transition tests in `tests/BehaviorFunctionalityTest.cpp`.

## AICommandBus

`AICommandBus` queues:

- behavior messages
- behavior transitions
- faction changes

This keeps worker-thread logic from mutating shared orchestration state directly. Commands are drained and committed on the main thread in deterministic sequence order.

## Event Emission

Behavior code can produce deferred gameplay events by building `EventManager::DeferredEvent` values and flushing them in one batch. This is the preferred path for combat and alert propagation generated inside worker batches.

## Threading Rules

- main-thread code may queue behavior messages directly
- worker-thread code should defer messages and event batches
- data that must survive frames belongs in EDM, never in temporary locals
