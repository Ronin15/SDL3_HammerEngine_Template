# Architecture Update

**Branch:** `architecture_update`
**Date:** 2025-12-26
**Review Status:** ⚠️ PENDING (No architect review requested)
**Overall Grade:** TBD

---

## Executive Summary

What began as a **combat system update** evolved into a **major architectural overhaul** when it became clear that supporting robust combat required fixing several architectural loose ends. The pivotal change was removing the GameLoop class, which served no purpose other than causing threading contention and engine stuttering. Returning to a sequential update loop controlled by GameEngine, combined with a complete rework of the WorkerBudget system to match this model, unlocked phenomenal performance gains. Additionally, rewriting the CollisionManager with dual-path threading for both broadphase and narrowphase phases contributed significantly to these improvements—the engine now runs with barely any idle resource usage during normal gameplay.

The secret to these performance and efficiency gains was **stripping away complexity** and focusing on **smart adaptive batching** combined with **parallel processing using SIMD**. The WorkerBudget system now uses throughput-based hill-climbing for adaptive batch optimization. The controller pattern has been modernized from singletons to state-owned instances, improving testability and lifecycle management.

The entity state machine system has been expanded with complete NPC and Player state implementations. The CombatController has been added as a new system for managing combat interactions. Documentation has been extensively updated to reflect all architectural changes, with 30+ documentation files modified.

**Impact:**
- ✅ **Sequential Execution Model** - Each manager gets ALL workers during its update window
- ✅ **Dual-Path Collision Threading** - Both broadphase (500+) and narrowphase (100+) support multi-threading
- ✅ **WorkerBudget Hill-Climbing** - Throughput-based adaptive batch optimization
- ✅ **State-Owned Controllers** - Improved testability and lifecycle management
- ✅ **Entity State Machines** - Complete NPC and Player state implementations
- ✅ **SMT/Hyperthreading Aware** - Detects logical cores and scales workers accordingly

---

## Changes Overview

### Files Changed Summary

| Category | Added | Deleted | Modified | Total |
|----------|-------|---------|----------|-------|
| **Documentation** | 8 | 3 | 20 | 31 |
| **Core Systems** | 1 | 2 | 8 | 11 |
| **Managers** | 0 | 0 | 12 | 12 |
| **Controllers** | 3 | 2 | 3 | 8 |
| **Entities/States** | 12 | 0 | 6 | 18 |
| **Events** | 2 | 0 | 10 | 12 |
| **AI/Behaviors** | 0 | 0 | 12 | 12 |
| **Tests** | 15 | 1 | 25 | 41 |
| **Other** | 15 | 0 | 66 | 81 |
| **Total** | **56** | **8** | **162** | **226** |

**Net Change:** +18,025 / -10,044 lines (+7,981 net)

---

## Major Architectural Changes

### 1. GameLoop Removal

**Problem:**
GameLoop was a separate class that consumed a worker thread and added complexity without clear benefit.

**Solution:**
Removed `GameLoop.hpp`/`GameLoop.cpp`. Functionality consolidated into `GameEngine`.

**Files Deleted:**
- `include/core/GameLoop.hpp`
- `src/core/GameLoop.cpp`
- `docs/core/GameLoop.md`

**Impact:**
- Regained 1 worker thread for task processing
- Simplified engine architecture
- Eliminated engine stuttering from thread contention

---

### 2. WorkerBudget System Rework

**Problem:**
Old percentage-based allocation model (AI: 54%, Particles: 31%, Events: 15%) was complex and inefficient for the actual sequential manager execution pattern.

**Solution:**
Complete rewrite with sequential execution model:
- Each manager gets ALL workers during its update window
- Throughput-based hill-climbing for adaptive batch optimization
- Queue pressure monitoring (90% threshold)

**Files Modified:**
- `include/core/WorkerBudget.hpp` - Complete API redesign
- `src/core/WorkerBudget.cpp` - New implementation

**New API:**
```cpp
// Get ALL workers for a system (sequential model)
size_t getOptimalWorkers(SystemType system, size_t workloadSize);

// Adaptive batch strategy with hill-climbing
std::pair<size_t, size_t> getBatchStrategy(SystemType system,
                                            size_t workloadSize,
                                            size_t optimalWorkers);

// Learning feedback for throughput optimization
void reportBatchCompletion(SystemType system, size_t workloadSize,
                           size_t batchCount, double totalTimeMs);
```

**Hill-Climbing Parameters:**
- Multiplier range: 0.25x (consolidation) to 2.5x (expansion)
- Adjustment rate: 2% per frame
- Throughput tolerance: 6% dead band
- Smoothing: 12% weight

---

### 3. CollisionManager Dual-Path Threading

**Problem:**
Collision processing was single-threaded, limiting performance with large entity counts.

**Solution:**
Implemented dual-path threading for BOTH phases:
- **Broadphase:** Single-threaded below 500 movable bodies, multi-threaded above
- **Narrowphase:** Single-threaded below 100 collision pairs, multi-threaded above
- SOA storage with 64-byte cache-aligned HotData
- SIMD 4-wide AABB intersection testing

**Files Modified:**
- `include/managers/CollisionManager.hpp`
- `src/managers/CollisionManager.cpp` (+1,409 lines of parallel processing)
- `include/collisions/HierarchicalSpatialHash.hpp`

**Performance:**
- 2-4x speedup for 10K+ bodies
- Zero-contention per-batch output buffers
- Efficient tile-to-region scaling

---

### 4. Controller Pattern Modernization

**Problem:**
Controllers used singleton pattern (`Controller::Instance()`), making testing difficult and lifecycle unclear.

**Solution:**
Controllers are now owned by GameState as member variables:
```cpp
// OLD (Singleton)
DayNightController::Instance().subscribe();

// NEW (State-owned)
class GamePlayState : public GameState {
private:
    DayNightController m_dayNightController;  // Owned by state
};
```

**Files Added:**
- `include/controllers/ControllerBase.hpp` - Base class for controllers
- `include/controllers/combat/CombatController.hpp`
- `src/controllers/combat/CombatController.cpp`

**Files Deleted:**
- `include/controllers/world/TimeController.hpp`
- `src/controllers/world/TimeController.cpp`
- `docs/controllers/TimeController.md`

**Files Modified:**
- `include/controllers/world/DayNightController.hpp`
- `include/controllers/world/WeatherController.hpp`
- `src/controllers/world/DayNightController.cpp`
- `src/controllers/world/WeatherController.cpp`

---

### 5. Entity State Machine Implementation

**Problem:**
No formal state machine for entity behaviors.

**Solution:**
Complete state machine implementation with:
- `EntityState` base class with `enter()`, `update(float deltaTime)`, `exit()` interface
- `EntityStateManager` for state transitions
- NPC states: Idle, Walking, Attacking, Hurt, Dying, Recovering
- Player states: Idle, Running, Attacking, Hurt, Dying

**Files Added:**
- `include/entities/npcStates/NPCIdleState.hpp`
- `include/entities/npcStates/NPCWalkingState.hpp`
- `include/entities/npcStates/NPCAttackingState.hpp`
- `include/entities/npcStates/NPCHurtState.hpp`
- `include/entities/npcStates/NPCDyingState.hpp`
- `include/entities/npcStates/NPCRecoveringState.hpp`
- `include/entities/playerStates/PlayerAttackingState.hpp`
- `include/entities/playerStates/PlayerHurtState.hpp`
- `include/entities/playerStates/PlayerDyingState.hpp`
- Corresponding `.cpp` implementations

**Files Modified:**
- `include/entities/Entity.hpp`
- `include/entities/NPC.hpp`
- `include/entities/Player.hpp`
- `src/entities/NPC.cpp`
- `src/entities/Player.cpp`

---

### 6. GameTime → GameTimeManager Rename

**Problem:**
Inconsistent naming with other manager classes.

**Solution:**
Renamed `GameTime` to `GameTimeManager` for consistency.

**Files Renamed:**
- `include/core/GameTime.hpp` → `include/managers/GameTimeManager.hpp`
- `src/core/GameTime.cpp` → `src/managers/GameTimeManager.cpp`
- `docs/core/GameTime.md` → `docs/managers/GameTimeManager.md`

---

### 7. CombatController Addition

**Problem:**
No centralized combat logic management.

**Solution:**
New CombatController handles:
- Hit detection coordination
- Damage calculation
- Status effect application
- Combat event dispatching

**Files Added:**
- `include/controllers/combat/CombatController.hpp`
- `src/controllers/combat/CombatController.cpp`
- `include/events/CombatEvent.hpp`
- `src/events/CombatEvent.cpp`
- `docs/controllers/CombatController.md`
- `docs/events/CombatEvent.md`

---

### 8. SIMD Unification

**Problem:**
SIMD code scattered across multiple files with inconsistent patterns.

**Solution:**
Unified all SIMD operations to `SIMDMath.hpp`:
- Cross-platform (SSE2/AVX2 for x86-64, NEON for ARM64)
- Consistent 4-wide processing pattern
- Scalar tail handling
- Scalar fallbacks

**Files Modified:**
- `include/utils/SIMDMath.hpp` - Consolidated SIMD operations
- `src/managers/AIManager.cpp` - Uses SIMDMath
- `src/managers/CollisionManager.cpp` - Uses SIMDMath

---

## Documentation Updates

### New Documentation Created

| File | Description |
|------|-------------|
| `docs/core/WorkerBudget.md` | Comprehensive 9-section WorkerBudget documentation |
| `docs/entities/EntityStates.md` | Entity state machine documentation |
| `docs/entities/README.md` | Entity system overview |
| `docs/controllers/CombatController.md` | Combat controller documentation |
| `docs/events/CombatEvent.md` | Combat event documentation |

### Documentation Deleted

| File | Reason |
|------|--------|
| `docs/core/GameLoop.md` | GameLoop removed |
| `docs/controllers/TimeController.md` | TimeController removed |
| `docs/collisions/CollisionSystem.md` | Merged into CollisionManager.md |

### Major Documentation Rewrites

| File | Changes |
|------|---------|
| `docs/core/ThreadSystem.md` | Rewritten for thread pool focus, removed percentage allocation |
| `docs/managers/CollisionManager.md` | Merged CollisionSystem.md, added threading architecture |
| `docs/controllers/DayNightController.md` | Removed singleton pattern |
| `docs/controllers/WeatherController.md` | Removed singleton pattern |
| `docs/README.md` | Updated threading section, removed deleted file refs |
| `README.md` | Updated threading description, SMT awareness |

---

## Threading Architecture

### Before (Percentage Allocation)
```
WorkerBudget allocation:
- AI: 54% of workers
- Particles: 31% of workers
- Events: 15% of workers
- Buffer: 30% reserve
```

### After (Sequential Execution)
```
GameEngine::update()
├── EventManager.update()      ← ALL workers
├── GameStateManager.update()  ← ALL workers
├── AIManager.update()         ← ALL workers
├── ParticleManager.update()   ← ALL workers
├── PathfinderManager.update() ← ALL workers
└── CollisionManager.update()  ← ALL workers

Each manager:
1. Gets optimal workers from WorkerBudgetManager
2. Gets batch strategy (count, size)
3. Submits batches to ThreadSystem
4. Reports completion for hill-climbing
```

**Benefits:**
- No contention between managers
- Full worker utilization per manager
- Simpler design
- Better cache locality

---

## Test Changes

### New Tests Added

| Test File | Purpose |
|-----------|---------|
| `tests/EntityStateManagerTests.cpp` | Entity state machine tests |
| `tests/SIMDCorrectnessTests.cpp` | SIMD operation verification |
| `tests/AIOptimizationTest.cpp` | AI optimization verification |

### Test Infrastructure

| File | Purpose |
|------|---------|
| `tests/clang-tidy/.clang-tidy` | Clang-tidy configuration |
| `tests/clang-tidy/clang_tidy_focused.sh` | Focused clang-tidy script |
| `tests/clang-tidy/clang_tidy_suppressions.txt` | Clang-tidy suppressions |
| `tests/power_profiling/*` | Power profiling infrastructure |

### Tests Removed

| Test File | Reason |
|-----------|--------|
| `tests/controllers/TimeControllerTests.cpp` | TimeController removed |

---

## Files Modified

### Core Systems
```
include/core/GameEngine.hpp        (101 lines modified)
├─ Sequential manager update order
└─ GameLoop functionality merged

include/core/ThreadSystem.hpp      (40 lines modified)
├─ SMT/hyperthreading awareness
└─ Hardware concurrency - 1 workers

include/core/WorkerBudget.hpp      (650 lines, major rewrite)
├─ Sequential execution model
├─ Hill-climbing algorithm
└─ Queue pressure monitoring
```

### Managers
```
src/managers/CollisionManager.cpp  (+1,409 lines)
├─ Dual-path broadphase threading
├─ Dual-path narrowphase threading
├─ SOA storage (HotData/ColdData)
└─ SIMD 4-wide intersection

src/managers/AIManager.cpp         (-765 lines, refactored)
├─ WorkerBudget integration
└─ Simplified batch processing

src/managers/ParticleManager.cpp   (-422 lines, refactored)
├─ WorkerBudget integration
└─ Performance optimizations
```

### Controllers
```
src/controllers/combat/CombatController.cpp  (NEW, 189 lines)
├─ Hit detection
├─ Damage calculation
└─ Status effects

src/controllers/world/DayNightController.cpp (modified)
└─ State-owned pattern

src/controllers/world/WeatherController.cpp  (modified)
└─ State-owned pattern
```

---

## Total Changes

**Lines:**
- Added: ~18,025
- Removed: ~10,044
- Net: **+7,981 lines**

**Files:**
- Added: 56
- Deleted: 8
- Modified: 162
- Total: **226 files changed**

---

## Commit History

```
bdca257 updated documentation
ae05444 clang tidy suppression updates
ba92b09 clang tidy and cppcheck updates
d924cef STL changes
2a9fc24 fixed buffer utilization test
04e6e9c logging tweaks for debug and release
9a87c52 Collision manager threaded threshold set to benchmark levels
639cd16 re-worked collision manager to be more parallel
c1710bb multithreaded collision manager 2
24f62f5 narrowphase collision manager re-work 1
892be52 added -Wpedantic to debug builds
a4e68a5 update buffer test for workerbudget algorithm
fd87337 Merge branch 'combat_update'
f6c6888 tuned WorkerBudget working well
e725961 WorkerBudget System re-work for maximum utilization
93c916f Entity State Manager tests and runners created
605f45d particle manager performance fixes
faca048 Unified SIMD usage to SIMDMath.hpp
```

---

## Migration Notes

### Breaking Changes

1. **GameLoop Removed** - Code referencing `GameLoop` must be updated
2. **TimeController Removed** - Use WeatherController/DayNightController instead
3. **Controller Pattern** - Controllers are now state-owned, not singletons
4. **WorkerBudget API** - Complete API change from percentage to sequential model
5. **GameTime Renamed** - Now `GameTimeManager`

### API Changes

```cpp
// OLD WorkerBudget API
budget.aiAllocated
budget.getOptimalWorkerCount()

// NEW WorkerBudget API
WorkerBudgetManager::Instance().getOptimalWorkers(SystemType::AI, count);
WorkerBudgetManager::Instance().getBatchStrategy(system, count, workers);
WorkerBudgetManager::Instance().reportBatchCompletion(system, count, batches, ms);
```

### Controller Migration

```cpp
// OLD (Singleton)
DayNightController::Instance().subscribe();

// NEW (State-owned)
// In header
DayNightController m_dayNightController;

// In enter()
m_dayNightController.subscribe();

// In exit()
m_dayNightController.unsubscribe();
```

---

## References

**Related Documentation:**
- [ThreadSystem.md](../docs/core/ThreadSystem.md)
- [WorkerBudget.md](../docs/core/WorkerBudget.md)
- [CollisionManager.md](../docs/managers/CollisionManager.md)
- [EntityStates.md](../docs/entities/EntityStates.md)
- [CombatController.md](../docs/controllers/CombatController.md)

**Related Performance Reports:**
- [Collision Benchmark Report 2025-12-25](../docs/performance_reports/collision_benchmark_report_2025-12-25.md)

---

## Changelog Version

**Document Version:** 1.0
**Last Updated:** 2025-12-26
**Status:** Draft - Ready for Review

---

**END OF CHANGELOG**
