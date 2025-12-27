# Power Efficiency Documentation

**Where to find the code:**
- ThreadSystem: `include/core/ThreadSystem.hpp`
- WorkerBudget: `include/core/WorkerBudget.hpp`, `src/core/WorkerBudget.cpp`
- Power profiling tools: `tests/power_profiling/`

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
| Typical gameplay | 17-19% | 80%+ | 2.1-2.6W | **27-33 hours** |
| Sustained combat/action | ~20% | 80%+ | 11-13W | 5-6 hours |
| Stress test (max entities) | All systems | 60-80% | 27-28W | 2.5 hours |

**Key takeaway:** During typical gameplay (exploration, menus, light combat), the engine draws only **2-3W** with **80%+ idle residency**—barely above system idle. Even sustained heavy action maintains excellent efficiency. The stress test numbers represent artificial worst-case scenarios, not realistic gameplay.

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
| C-State Residency (gameplay) | >70% | 80%+ | ✅ EXCELLENT |
| Power Draw (idle, 0 entities) | <1W | 0.10W | ✅ EXCELLENT |
| Power Draw (typical gameplay) | <5W | 2.1-2.6W | ✅ EXCELLENT |
| Battery (typical gameplay) | >20 hours | **27-33 hours** | ✅ EXCEPTIONAL |
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

Real App (with rendering):
  ├─ Work: 4-6ms ─────────┤
  │                       ├─ Idle: 10-12ms (vsync wait) ──────┤
  └───────────────────────────────────────────────────────────┘
  Result: 80%+ idle residency (still excellent!)
```

Both modes maintain high C-state residency because:
1. **Sequential manager execution** - Each manager gets ALL workers, completes quickly
2. **Adaptive batch sizing** - WorkerBudget hill-climbing finds optimal throughput
3. **No busy-waiting** - Threads sleep between frames
4. **VSync alignment** - Rendering waits for display refresh

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

### Common Power Issues

| Problem | Cause | Solution |
|---------|-------|----------|
| Low idle residency (<40%) | Rendering loop too fast | Check vsync settings |
| High idle power (>3W) | Background work | Audit update loops |
| Spiky power draw | Uneven batching | Use WorkerBudget |

## Comparison: Before vs After Architecture Update

The architecture update (December 2025) significantly improved power efficiency:

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

## See Also

- [ThreadSystem](../core/ThreadSystem.md) - Thread pool and task scheduling
- [WorkerBudget](../core/WorkerBudget.md) - Adaptive batch optimization
- [Power Profiling Tools](../../tests/power_profiling/README.md) - Full profiling documentation
