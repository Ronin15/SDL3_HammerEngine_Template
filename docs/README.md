# Hammer Game Engine Documentation

## Overview

This documentation hub documents the engine architecture and major subsystems. The engine uses event-driven systems with `EventManager` as the central event processing hub, EDM-backed AI state, controller-owned harvesting and social flows, world-resource orchestration, and the SDL3_GPU rendering path.

## Core Systems

- [GameEngine](core/GameEngine.md)
- [ThreadSystem](core/ThreadSystem.md)
- [WorkerBudget](core/WorkerBudget.md)
- [TimestepManager](core/TimestepManager.md)
- [ARCHITECTURE](ARCHITECTURE.md)

## AI and Events

- [AIManager](ai/AIManager.md)
- [Behavior Execution Pipeline](ai/BehaviorExecutionPipeline.md)
- [Behavior Modes](ai/BehaviorModes.md)
- [Behavior Quick Reference](ai/BehaviorQuickReference.md)
- [NPC Memory](ai/NPCMemory.md)
- [EventManager](events/EventManager.md)
- [EventManager Quick Reference](events/EventManager_QuickReference.md)
- [EventManager Advanced](events/EventManager_Advanced.md)
- [EventFactory](events/EventFactory.md)

## Controllers and World Systems

- [Controllers Overview](controllers/README.md)
- [CombatController](controllers/CombatController.md)
- [HarvestController](controllers/HarvestController.md)
- [SocialController](controllers/SocialController.md)
- [WorldManager](managers/WorldManager.md)
- [WorldResourceManager](managers/WorldResourceManager.md)
- [JSON Resource Loading Guide](utils/JSON_Resource_Loading_Guide.md)

## Rendering and UI

- [GPU Rendering](gpu/GPURendering.md)
- [UIManager Guide](ui/UIManager_Guide.md)
- [Camera](utils/Camera.md)
- [FrameProfiler](utils/FrameProfiler.md)
- [WorldRenderPipeline](utils/WorldRenderPipeline.md)

## Entities and GameStates

- [Entity System](entities/README.md)
- [GameState Documentation](gameStates/README.md)
- [LoadingState](gameStates/LoadingState.md)
- [SettingsMenuState](gameStates/SettingsMenuState.md)
- [GameOverState](gameStates/GameOverState.md)

## Testing and Validation

- [`tests/TESTING.md`](../tests/TESTING.md)
  - includes coverage for behavior transitions, NPC memory, controller tests, and GPU frame timing benchmarks

## Notes

- WorkerBudget docs now describe threshold learning and hysteresis rather than the old throughput-comparison model.
- `TimestepManager` documentation now lives under `docs/core/` and covers display-refresh-aware cadence snapping.
- Performance reports under `docs/performance_reports/` should be treated as dated measurements unless their metadata explicitly matches the current branch and commit.
