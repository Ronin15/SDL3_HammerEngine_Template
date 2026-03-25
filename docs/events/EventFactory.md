# EventFactory

## Purpose

`EventFactory` constructs event objects from definitions or helper inputs. It is not the runtime event registry.

On this branch:

- `EventFactory` is useful for tests, scripted definitions, and helper construction
- runtime delivery goes through `EventManager`
- old flows that created an event and then called `EventManager::registerEvent(...)` are obsolete

## Current Boundary

Use `EventFactory` when you need an event instance.

Use `EventManager` when you need to dispatch that event or trigger a gameplay reaction.

## Practical Guidance

- prefer `EventManager` trigger helpers for normal gameplay paths
- prefer `EventFactory` when creating richer event objects from data-driven definitions
- if you build an event object manually, wrap it in `EventData` and dispatch through current `EventManager` APIs instead of relying on removed registration/storage APIs
