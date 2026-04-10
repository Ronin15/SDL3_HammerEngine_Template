/* Copyright (c) 2025 Hammer Forged Games, All rights reserved. Licensed under the MIT License - see LICENSE file for details */

# Projectiles & HUD Refactor Update

**Branch:** `projectiles`
**Date:** 2026-04-03 → 2026-04-10

---

## Executive Summary

The `projectiles` branch focuses on **projectile correctness + performance**, a **HUD ownership refactor**, and several codebase-wide safety/consistency passes (notably `[[nodiscard]]` enforcement and state-name enum standardization).

The headline change is a new `ProjectileManager` that owns projectile integration, lifetime, and the collision-to-damage bridge through the existing deferred `EventManager` pipeline. On the gameplay/UI side, HUD event/display concerns are extracted from `CombatController` into a centrally owned `GameplayHUDController`, keeping controllers state-scoped while making HUD behavior consistent across gameplay states.

**Impact:**
- ✅ `ProjectileManager`: SIMD-friendly integration + `WorkerBudget`-driven adaptive threading for projectile simulation
- ✅ Projectile damage correctness fixes (including static-collision edge cases)
- ✅ HUD refactor: `CombatController` emits gameplay signals; `GameplayHUDController` owns transient HUD display state
- ✅ Removal of string-based state names in favor of a proper enum system
- ✅ `[[nodiscard]]` sweep for important bool-returning initialization APIs + aligned production/test callsites
- ✅ Documentation and test-script updates for the new systems

---

## Changes Overview

### Scale

| Metric | Value |
|--------|-------|
| Commits | 43 |
| Files changed | 408 |
| Lines added | 8,015 |
| Lines removed | 4,528 |
| Net change | +3,487 |

### Systems Touched (High Level)

| System | Change Type | Notes |
|--------|-------------|------|
| Projectile simulation | **New manager + fixes** | `ProjectileManager`, projectile→damage wiring, SIMD and `WorkerBudget` integration |
| Controllers / HUD | Refactor | `GameplayHUDController` added; `CombatController` responsibilities clarified |
| GameState infrastructure | Standardization | string state names replaced by enum system |
| Safety / correctness | Sweep | `[[nodiscard]]` on critical bool returns; callsites updated |
| Tests & scripts | Expanded | new tests + benchmarks; new runner scripts; docs updated |
| Docs | Updated | new pages + hub/index wiring for discoverability |
| Dev tooling | Updated | architecture drift skill + local agent workflow updates |

---

## Detailed Changes

### 1. ProjectileManager (Simulation + Lifetime + Event Bridge)

**Role:**
- Integrates projectile movement (SIMD 4-wide where possible)
- Manages projectile lifetime (timeouts + embedded projectiles)
- Bridges `Collision` → `Damage` using the existing deferred `EventManager` pipeline

**Event flow (Collision → Damage):**
1. `CollisionManager` produces a deferred `CollisionEvent`.
2. `ProjectileManager` consumes `EventTypeId::Collision` and converts projectile hits into deferred `DamageEvent`.

**Threading model:**
- Uses `WorkerBudget` (`SystemType::ProjectileSim`) to decide whether to thread.
- Batches over the active projectile index list; each batch uses its own destroy queue.
- Futures complete before main-thread merge and destroy application.

### 2. Projectile Damage Correctness Fixes

Fixes focus on ensuring damage is applied **only** for the intended projectile-hit cases and does not spuriously apply when other collision scenarios occur (including static collision interactions).

### 3. HUD Ownership Refactor (CombatController → GameplayHUDController)

`GameplayHUDController` is added as a **state-scoped** controller that centralizes HUD-facing combat target display state:
- Subscribes to `EventTypeId::Combat`
- Tracks transient “current target” display state (label/health) with an auto-expire duration
- GameStates query it during update/render; it does not directly mutate UI components

This keeps controller responsibilities clean and prevents gameplay controllers from turning into implicit UI state owners.

### 4. State Name Standardization (Enum Migration)

String-based state identifiers are replaced with a proper enum system to reduce brittleness and improve compile-time correctness across state transitions and test wiring.

### 5. `[[nodiscard]]` Safety Sweep (Production + Tests)

Important bool-returning APIs (init/create/load-style calls) are annotated as `[[nodiscard]]` and updated callsites are required to handle failures explicitly (production guards; `BOOST_REQUIRE` in tests). This aligns with the repo guidance to avoid silently ignoring critical initialization failures.

### 6. Tests, Benchmarks, and Runner Scripts

New/updated test suites and benchmarks validate projectile behavior and scaling, and the `tests/test_scripts/` directory gains additional runners for targeted workflows (projectile manager tests, projectile benchmark, pathfinder manager tests).

### 7. Documentation Updates

Documentation is updated to reflect the new systems and improved discoverability:
- Added `docs/managers/ProjectileManager.md`
- Added `docs/controllers/GameplayHUDController.md`
- Updated doc hub/index pages and teardown ordering notes
- Updated `tests/TESTING.md` to include the new runner scripts

---

## Testing & Validation

This changelog does not assert a full CI run. Suggested targeted validation:

- Projectile manager tests: `./bin/debug/projectile_manager_tests`
- Projectile scaling benchmark: `./bin/debug/projectile_scaling_benchmark`
- Scripted runs:
  - `./tests/test_scripts/run_projectile_manager_tests.sh`
  - `./tests/test_scripts/run_projectile_benchmark.sh`

