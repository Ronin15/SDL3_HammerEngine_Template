# Collisions & Pathfinding Update Changelog

## Overview
The `collisions&pathfinding_update` branch introduces HammerEngine's first dedicated collision detection and pathfinding stacks. Prior to this work the engine shipped without core collision or navigation systems, so the effort delivers the initial implementation along with supporting assets, documentation, and validation harnesses required for large-scale simulations.

## 🚧 Collision System Foundations
- Added the inaugural `CollisionManager` featuring dual spatial hashes, pooled broadphase buffers, and kinematic batch updates to keep 10K+ movers responsive at 60 FPS.
- Introduced trigger support (cooldowns, tags, convenience factories) and automatic world-driven trigger generation for water/obstacle tiles so gameplay systems can react to world changes.
- Implemented static obstacle rebuilding, per-body layer masks, and resize helpers ensuring the world grid, entities, and events stay synchronized from day one.
- Authored reusable primitives (`CollisionBody`, `CollisionInfo`, `SpatialHash`, `AABB`, `TriggerTag`) that establish the shared collision vocabulary for the rest of the engine.
- Published collision documentation across `docs/collisions/` and `docs/managers/CollisionManager.md`, bootstrapping developer onboarding for the brand-new subsystem.

## 🧭 Pathfinding & AI Navigation
- Delivered the first `PathfinderManager` singleton with async request queue, WorkerBudget-backed prioritization, and cache-aside path reuse to orchestrate navigation workloads.
- Implemented hierarchical `PathfindingGrid`, `PathSmoother`, request queue utilities, and weight-field configuration so AI can navigate dynamic maps efficiently.
- Simplified AI behavior interfaces (`AIBehavior.hpp` and behavior headers) to plug directly into the new pathfinding responses and collision data surfaced by this branch.
- Updated NPC/player entities and resource carriers with velocity, collider ownership, and trigger awareness for consistent navigation feedback.

## 🤖 Event, Manager, and Entity Alignment
- Enhanced `EventManager`, `CollisionEvent`, `WorldTriggerEvent`, and scene events to broadcast collision/path updates across states, enabling the first round of gameplay integration.
- Tightened integrations between `AIManager`, `ParticleManager`, `ResourceFactory`, and `WorldManager` so state transitions rebuild collisions and invalidate cached paths correctly.
- Added `UniqueID` utilities, extended `WorkerBudget`, and refreshed logging to trace collision/path workloads in debug builds.

## 🌍 World, Assets, and Data
- Extended world data generators to emit collision bodies, obstacle triggers, and navigation weights whenever tiles change or maps load.
- Added city building obstacle sprites and refreshed item/material JSON to align with trigger penalties and rewards that depend on the new systems.
- Authored new manuals under `docs/ai/`, `docs/collisions/`, and manager guides to document the freshly created workflows.

## 🧪 Testing & Tooling
- Shipped inaugural suites: `CollisionSystemTests`, `PathfindingSystemTests`, `PathfinderManagerTests`, plus AI threading regression coverage to validate the new foundations.
- Added mock behaviors/entities for deterministic navigation tests and refreshed resource/particle/valgrind scripts.
- Introduced dedicated collision/pathfinding test runners and benchmarking scripts for both POSIX and Windows shells.

## 📚 Developer Experience
- Expanded `AGENTS.md` / `tests/TESTING.md` with collision/pathfinding playbooks for the newly introduced systems.

## ⚠️ Breaking Changes & Migration Notes
- Gameplay code must now configure collision layers, trigger tags, and cooldowns when registering entities to participate in the new system.
- Navigation flows should adopt `PathfinderManager::requestPath` / `findPathSync` and stop relying on ad-hoc movement helpers that existed before this branch.
- Event factory/type IDs were reorganized; audit custom events for the new enums and factory wiring.

## 📊 Impact Summary
- **Files touched**: 169
- **Lines added**: ~20,000
- **Lines removed**: ~3,100
- Collision and pathfinding systems, along with their documentation and tests, now exist in HammerEngine for the first time.

This branch establishes HammerEngine's baseline collision and navigation pillars, pairing high-performance spatial processing with testable, well-documented APIs ready for future gameplay expansion.
