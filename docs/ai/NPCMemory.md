# NPC Memory

## Overview

NPC memory is stored in EDM and consumed by AI-layer logic. It tracks remembered entities, locations, emotional state, and combat/social history.

Primary data lives in `NPCMemoryData` and related memory entry structures inside `EntityDataManager`.

## What EDM Owns

EDM owns storage for:

- memory entries
- emotional state values
- location history
- combat totals such as damage dealt/received
- last attacker / last target bookkeeping

EDM should remain a storage and aggregation layer.

## What AI Owns

AI-layer behavior code owns interpretation:

- personality-scaled emotion changes
- witnessed-combat falloff
- alert/fear/aggression responses
- emotional contagion pre-pass in `AIManager::update()`

## Common Flows

### Direct combat

`Behaviors::processCombatEvent(...)`:

1. records factual combat data through EDM
2. updates emotions in AI logic
3. leaves persistent results in `NPCMemoryData`

### Witnessed combat

`Behaviors::processWitnessedCombat(...)`:

1. evaluates distance and composure
2. records a memory entry
3. applies fear/aggression/suspicion changes

### Social interactions

`SocialController` writes gifts, trade, theft, and other interactions into memory-backed relationship flows.

## Testing Coverage

The branch adds dedicated memory coverage in `tests/managers/NPCMemoryTests.cpp`, including:

- structure/layout assumptions
- add/find memory behavior
- emotional state decay and clamping
- combat statistics
- cleanup and state transition handling
