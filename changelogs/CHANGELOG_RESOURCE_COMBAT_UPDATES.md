# Resource Combat Updates - Gameplay, Resource, and Event Pipeline Refactor

**Branch:** `resource-combat-updates`
**Date:** 2026-03-24
**Review Status:** PENDING
**Overall Grade:** TBD
**Commits:** 200
**Files Changed:** 298 (+24,183 / -41,422 lines)

---

## Executive Summary

This branch is a broad engine update centered on gameplay plumbing, resource flow, event coordination, and repository workflow cleanup. The largest changes landed in AI behavior execution, combat/social/harvest controllers, event dispatch, world/resource ownership, and the render/UI/game-state path. In parallel, the repository's docs and contributor guidance were moved toward the current `AGENTS.md` and public skill layout, replacing older Claude-specific workflow files.

The result is a more data-oriented and controller-driven engine structure. AI logic now runs through behavior executors and command routing, combat and social actions are handled by dedicated controllers, event handling is more centralized and batched, and resource/world state has been pushed deeper into EDM and world registries. The GPU and UI layers were also tightened for frame lifecycle safety, text rendering, and layout consistency.

### Impact Highlights

- AI behavior execution was rewritten around behavior executors, config data, and `AICommandBus`.
- Combat, social, and harvest flows were split into dedicated controllers with clearer ownership.
- Event dispatch moved toward a centralized, deferred, and pooled pipeline.
- Player, resource, and world state became more EDM-backed and registry-driven.
- GPU rendering, UI text, and game-state flow received lifecycle and layout fixes.
- Repository guidance moved from `.claude` workflow files to `AGENTS.md` and public skills.
- Test coverage and benchmark runners were expanded to match the new runtime surface area.

---

## AI, Combat, and Resource Systems

### AI Behavior Pipeline

The AI layer was heavily reworked away from the old virtual behavior style and toward executor-based behavior functions with explicit config data.

- `AIBehavior` inheritance was replaced by `Behaviors::execute*` and `Behaviors::init*` style flow.
- `BehaviorConfigData` now drives behavior setup and tuning.
- `AICommandBus` was added for queued behavior messages, transitions, and faction updates.
- `AISpatialGrid` and cached query paths support guard, faction, and active-entity lookups.
- `AIManager` now batches work more explicitly and commits deferred behavior commands after worker completion.

### Combat, Social, and Harvest

Combat and interaction logic were moved into controller-owned flows instead of being spread across gameplay states and entity code.

- `CombatController` centralizes attack cooldown, stamina, target tracking, and damage dispatch.
- `SocialController` handles trade sessions, item transfer, relationship changes, and guard alerts.
- `HarvestController` manages progress-based harvesting, movement cancellation, completion, and fallback item spawning.
- `Player` now treats EDM as the source of truth for key gameplay values such as inventory, gold, health, stamina, and combat stats.

### World, Resource, and EDM Changes

World and resource ownership were pushed further into EDM and world registries.

- `EntityDataManager` expanded around inventory, containers, harvestables, dropped items, and world registration.
- `WorldResourceManager` now owns per-world registries and spatial indices with explicit transition cleanup.
- `WorldManager` registers worlds before resource initialization and defers loaded notifications through `ThreadSystem`.
- `BackgroundSimulationManager` and `PathfinderManager` now have clearer state-transition prep and lifecycle cleanup.
- `WorkerBudget` was simplified around the sequential manager model so each manager can use the full worker pool during its slice.

---

## Event Manager and Dispatch

The event system was reworked into a more centralized coordination hub with batching and pooling.

- `EventManager` now supports deferred batch enqueueing and pooled event acquisition/release.
- Combat events use a more distinct throughput path, which reduces ad hoc handling in gameplay code.
- `EventFactory` was updated to build the newer event types used by the refactored runtime.
- `ParticleEffectEvent` and `WeatherEvent` act more like data carriers, with handler code doing the work.
- Event-related tests and benchmarks were expanded to cover the new coordination model and scaling behavior.

---

## GPU, UI, and Game State Updates

### Rendering and Frame Lifecycle

The GPU path was tightened around clearer frame lifecycle handling and more explicit swapchain control.

- Swapchain acquisition and frame submission now have clearer failure handling.
- VSync and present mode setup is handled directly on the GPU swapchain path.
- GPU mode now avoids continuing after failed frame setup.
- Shader and text handling were adjusted to fit the atlas-backed GPU text workflow.

### UI and Layout

UI rendering and positioning were normalized for fullscreen, DPI, and controller-owned screens.

- UI text now uses atlas-backed SDL3_ttf GPU sequences instead of per-string texture churn.
- Separate GPU text pipelines were added for alpha, color, and SDF text paths.
- `UIManager::removeComponentsWithPrefix()` now follows the normal cleanup path.
- `setComponentPositioning()` usage was expanded so fullscreen and resize behavior stay consistent.

### Game State Flow

Gameplay states were updated to fit the newer controller/event ownership model.

- `GamePlayState` wires in the new combat, harvest, and social controller flows.
- `GameOverState` was added for the player death path.
- `AdvancedAIDemoState`, `AIDemoState`, and `EventDemoState` were aligned with the new behavior and event handling model.
- `LogoState` and camera/scene projection logic were adjusted to reduce coordinate mismatch and jitter.
- `SettingsMenuState` now applies positioning consistently to its controls.

---

## Docs, Tooling, and Workflow Migration

The repository guidance and supporting docs were updated to match the current engine structure.

- `AGENTS.md` became the primary repo guidance file.
- Older `.claude` agent, skill, and workflow files were removed.
- Public skills were added for quality checks and performance regression checks.
- `README.md`, `CMakeLists.txt`, and the docs tree were updated to reflect the current build, testing, and shader workflow.
- The atlas workflow was rewritten to use the command-driven `extract`, `map`, and `pack` flow instead of the removed browser-based session artifact.
- Documentation was added or refreshed for AI execution, controllers, event flow, game states, managers, and resource migration.

### Build and Shader Notes

- `USE_SDL3_GPU` builds now rely on stricter platform-specific shader tooling.
- Shader output selection is now more explicit across Windows, Linux, and macOS.
- The README now documents the sprite atlas workflow more directly.

---

## Testing and Validation

Test coverage was expanded to match the new runtime surface area.

- New controller tests cover harvest, item, social, and resource rendering behavior.
- AI behavior tests were rewritten around the new executor and command routing model.
- Event manager tests and scaling benchmarks were updated for the new dispatch path.
- GPU tests gained frame timing coverage and broader renderer/shader verification.
- Runner scripts and `tests/CMakeLists.txt` were updated to expose the new executables.
- `tests/TESTING.md` was refreshed to document the larger test inventory and the updated runner usage.

---

## Migration Notes

- Code that depended on the old AI behavior class hierarchy will need to migrate to behavior executors and `AICommandBus` flow.
- Controller ownership is now more explicit, so code that assumed singleton-style combat, social, or harvest helpers should follow the new state-owned pattern.
- Event consumers should expect more deferred and batched dispatch behavior.
- Player, resource, and world state should be treated as EDM or registry-backed data rather than local gameplay-only state.
- `USE_SDL3_GPU` builds have stricter shader-tool prerequisites than before.
- Any internal references to removed `.claude` workflow files or the old atlas session page should be updated to the new repo guidance and tool path.

---

## Summary

`resource-combat-updates` is a large cross-cutting refactor that pulls gameplay logic, event flow, and resource ownership into clearer controller and data-oriented boundaries while also cleaning up the GPU/UI path and modernizing repo guidance. The branch adds substantial validation coverage and brings the repository's docs and tooling into alignment with the current engine architecture.
