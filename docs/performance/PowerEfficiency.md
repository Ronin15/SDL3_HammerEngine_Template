# Power Efficiency

**Code:** `include/core/ThreadSystem.hpp`, `include/core/WorkerBudget.hpp`, `src/core/WorkerBudget.cpp`, `tests/power_profiling/`

## Overview

HammerEngine uses a **race-to-idle** strategy optimized for battery-powered devices. The engine completes frame work as quickly as possible, then sleeps until the next vsync, keeping CPU cores in low-power C-states even during active gameplay.

## Test Hardware

- **CPU:** Apple M3 Pro (11-core)
- **Battery:** 70Wh
- **OS:** macOS
- **Build:** Debug (Release would be even more efficient)

**Note:** These benchmarks are updated with each major branch update to track performance over time.

## Measured Results

### Real-App Gameplay (Full Stack)

| Scenario | CPU Active | Idle Residency | Power Avg | Battery Life |
|----------|-----------|----------------|-----------|--------------|
| Scenario | CPU Active | Idle Residency | Power Avg | Battery Life |
|----------|-----------|----------------|-----------|--------------|
| Idle gameplay — software frame limit (vsync off) | ~10% | **~90%** | ~0.6W | ~116 hours |
| Idle gameplay — vsync on (full NPC memory + deterministic AI) | 13.5% | **86.5%** | **0.68W** | **103 hours** |
| Typical gameplay | 17-19% | 80%+ | 2.1-2.6W | 27-33 hours |
| Sustained combat/action | ~20% | 80%+ | 11-13W | 5-6 hours |
| Stress test (max entities) | All systems | 60-80% | 27-28W | 2.5 hours |

**Key takeaway:** The engine uses SDL3 GPU rendering exclusively. The primary efficiency lever is **synchronizing the render rate to the game update rate**. With vsync on, the renderer presents at the display refresh rate (e.g. 120Hz) while the game only updates at 60Hz — the GPU renders the same frame twice per game update, wasting energy on redundant work. The engine's software frame limiter enforces exactly one render per game update at 60 FPS, then idles, achieving up to **90% idle residency**. This is the critical advantage for battery-powered devices wanting to maximize play sessions. Vsync-on still achieves 86.5%, but pays a rendering tax for the mismatch. Both modes draw only **0.68–2W** during typical gameplay.

> **Entity scaling:** Tested range of ~200 entities shows no measurable power difference across varying counts — WorkerBudget keeps all simulation work within the frame budget at these counts.

### Headless Benchmarks (AI/Collision/Pathfinding Only)

| Entities | Power Avg | Battery Drain/Test | FPS | Throughput |
|----------|-----------|-------------------|-----|-----------|
| 0 (Idle) | 0.10W | 0.001% | 49.2 | N/A |
| 10,000 | 0.06W | 0.001% | 48.7 | 487K ops/sec |
| 50,000 | 0.13W | 0.003% | 49.0 | 2.45M ops/sec |

**Key:** All headless tests combined = <0.01% battery drain. The AI/collision systems are extremely efficient.

## Performance Targets vs Actual

| Metric | Target | Actual | Status |
|--------|--------|--------|--------|
| C-State Residency (headless) | >80% | 81%+ | ✅ EXCELLENT |
| C-State Residency (gameplay, vsync on) | >70% | **86.5%** | ✅ EXCEPTIONAL |
| C-State Residency (gameplay, software limit) | >70% | **~90%** | ✅ EXCEPTIONAL |
| Power Draw (idle, 0 entities) | <1W | **0.68W** (GPU) | ✅ EXCELLENT |
| Power Draw (typical gameplay) | <5W | 2.1-2.6W | ✅ EXCELLENT |
| Battery (typical gameplay) | >20 hours | **103 hours** (GPU idle) | ✅ EXCEPTIONAL |
| Power Draw (sustained action) | <15W | 11-13W | ✅ GOOD |
| Battery drain (50K entity test) | <1% | 0.003% | ✅ EXCEPTIONAL |

## How Race-to-Idle Works

```
Frame Timeline (16.67ms @ 60 FPS):

Headless (AI only):
  ├─ Work: 2-5ms ─────┤
  │                   ├─ Idle: 11-14ms (C-states) ────────────┤
  └───────────────────────────────────────────────────────────┘
  Result: 95% idle residency

Real App — vsync on (display at 120Hz, game at 60Hz):
  ├─ Update + Render ──┤ Render again (no new data) ──┤ sleep ─┤
  └───────────────────────────────────────────────────────────┘
  Result: 86.5% idle residency (GPU renders same frame twice per update)

Real App — software frame limit (render synced to game update, 60 FPS):
  ├─ Update + Render: 4-6ms ──┤
  │                           ├─ Deep sleep: 10-12ms ──────────┤
  └───────────────────────────────────────────────────────────┘
  Result: ~90% idle residency (one render per game update, then idle)
```

The core principle: **sync the render rate to the game update rate.** Vsync ties the renderer to the display refresh rate — if the display runs faster than the game updates, the GPU re-renders frames with no new game data, wasting energy. The software frame limiter matches render cadence to game logic cadence exactly, maximizing idle time per frame.

This is the critical efficiency lever for battery-powered devices wanting to maximize play sessions.

All modes maintain high C-state residency because:
1. **Sequential manager execution** - Each manager gets ALL workers, completes quickly
2. **Adaptive batch sizing** - WorkerBudget hill-climbing finds optimal throughput
3. **No busy-waiting** - Threads sleep between frames
4. **Software frame limiting** - Render rate locked to game update rate, CPU idles immediately after each frame

## Key Metrics Explained

### C-State Residency (Most Important for Battery)

| Residency | Rating | Meaning |
|-----------|--------|---------|
| >80% | Excellent | Cores sleeping most of the time |
| 60-80% | Good | Significant idle periods remain |
| 40-60% | Moderate | More sustained work |
| <40% | Poor | CPU rarely sleeps, high battery drain |

### CPU Power Draw Guidelines

| Power | Scenario |
|-------|----------|
| <1W | Idle baseline (menu screens) |
| 1-5W | Light gameplay |
| 5-10W | Normal gameplay |
| 10-15W | Heavy workload |
| >15W | High load (watch battery) |

## Running Power Tests

### Quick Real-App Test

```bash
# Measure actual game for 30 seconds
sudo tests/power_profiling/run_power_test.sh --real-app

# Measure for 60 seconds
sudo tests/power_profiling/run_power_test.sh --real-app --duration 60
```

### Full Benchmark Suite

```bash
# Run complete headless benchmark (~30 minutes)
sudo tests/power_profiling/run_power_test.sh

# Parse results
python3 tests/power_profiling/parse_powermetrics.py \
  tests/test_results/power_profiling/power_*.plist
```

### Calculate Battery Life

```
Battery Hours = Battery Capacity (Wh) / Average Power (W)

Example (M3 Pro, 70Wh battery):
  Light play:     70Wh / 2.5W  = 28 hours
  Gameplay:       70Wh / 12W   = 5.8 hours
  Peak load:      70Wh / 28W   = 2.5 hours
```

## Optimization Tips

### What Makes Race-to-Idle Work

1. **Avoid per-frame allocations** - Reuse buffers, pre-allocate
2. **Use WorkerBudget** - Let the system find optimal batch sizes
3. **Don't busy-wait** - Use proper synchronization primitives
4. **Batch operations** - Process multiple items per task
5. **SIMD where applicable** - 4-wide processing with SIMDMath.hpp
6. **GPU rendering** - SDL3 GPU API is the only rendering backend; draw calls complete quickly for maximum idle time
7. **Sync render rate to game update rate** - Use the engine's software frame limiter rather than vsync. Vsync ties rendering to the display refresh rate; if the display runs at 120Hz and the game updates at 60Hz, the GPU renders every frame twice with no new data. The software limiter renders exactly once per game update then sleeps — the primary battery efficiency lever on portable devices

### Common Power Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Low idle residency (<40%) | Rendering loop too fast | Check vsync settings |
| High idle power (>3W) | Background work | Audit update loops |
| Spiky power draw | Uneven batching | Use WorkerBudget |
| Render spiral on macOS (window occluded) | macOS disables vsync when window is occluded, removing frame pacing and letting the loop run uncapped | **Fixed.** Software limiter now remains active regardless of window visibility/occlusion state. Observed once (Dec 25 2025): 5.57W avg, 2617 MHz — all subsequent sessions unaffected. |

## Comparison: Architecture Evolution

### December 2025: EDM Architecture Update

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Idle residency | 60-70% | 80%+ | +15-20% |
| Light play power | 4-5W | 2-3W | ~50% reduction |
| Frame work time | 8-10ms | 4-6ms | ~50% faster |

Key changes:
- Removed GameLoop class (eliminated thread contention)
- Sequential manager execution (no concurrent manager overhead)
- WorkerBudget hill-climbing (optimal batch sizing)
- Dual-path collision threading (efficient scaling)

### January 2026: SDL3 GPU Rendering (SDL_Renderer Removed)

Migrated exclusively to the SDL3 GPU API, removing the SDL_Renderer path entirely.

| Metric | SDL_Renderer (retired) | SDL3 GPU | Improvement |
|--------|------------------------|----------|-------------|
| Avg Power | 0.87W | 0.69W | **-21%** |
| Idle Residency | 85.8% | 86.7% | +0.9% |
| Battery Life | 80 hours | 101 hours | **+27%** |

Key changes:
- SDL_Renderer path removed; SDL3 GPU API is the only rendering backend
- Draw calls complete faster, more CPU idle time
- Best idle residency at the time: 86.7%

### April 2026: Deterministic AI + Full NPC Memory

| Addition | Avg Power Delta | Idle Residency Delta |
|----------|----------------|---------------------|
| Full NPC memory (emotions, combat history, threat tracking) | +0.10W total | +0.29% |
| Deterministic AI manager | ~0W measured | — |
| Deterministic attack system | ~0W measured | — |
| **Net vs early DOD baseline** | **+0.10W** | **+0.29%** |

Key changes:
- Per-entity `NPCMemoryData`: emotional state, combat history, witnessed events, threat tracking
- `lastCombatTime` delta semantics with per-frame emotional decay
- Guard behavior: multi-tier alert system with calm-rate polling
- Flee behavior: crowd analysis with threat re-evaluation
- Emotional contagion pre-pass in `AIManager::update()`
- Behavior message queues (deferred + immediate thread paths)
- Fully deterministic AI manager and attack system

**Entity scaling (measured):** No measurable power difference across tested entity counts (~200 range) — the WorkerBudget adaptive threshold keeps all simulation work within the frame budget, with the CPU returning to idle on schedule. Upper scaling limits have not yet been profiled.

Std dev (1.14W) is the lowest recorded across all 19 profiling sessions — the tightest, most consistent frame behavior on record. All sessions are **debug builds**; release builds would improve efficiency further.

## See Also

- [ThreadSystem](../core/ThreadSystem.md) - Thread pool and task scheduling
- [WorkerBudget](../core/WorkerBudget.md) - Adaptive batch optimization
- [Power Profiling Tools](../../tests/power_profiling/README.md) - Full profiling documentation
- [GPU Rendering](../gpu/GPURendering.md) - GPU rendering system documentation
- [Power Profile Analysis](../performance_reports/power_profile_edm_comparison_2026-01-29.md) - Detailed power analysis with GPU comparison
