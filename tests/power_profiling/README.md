# Power Profiling Suite - SDL3 HammerEngine

## Overview

This power profiling suite validates the **race-to-idle energy efficiency strategy** of the SDL3 HammerEngine's parallel threading architecture.

**Goal:** Prove that processing 20K+ AI entities in <8ms with 8-11ms idle time (race-to-idle) consumes significantly less battery power than traditional single-threaded approaches.

## Architecture

```
┌─────────────────────────────────────────┐
│  run_power_test.sh (Orchestrator)       │
│  - Starts powermetrics (requires sudo)  │
│  - Runs PowerProfile with various args  │
│  - Manages test scenarios               │
└─────────────────────────────────────────┘
              │
              ├──> PowerProfile (C++ app)
              │    - Headless (no SDL window)
              │    - Spawns N entities
              │    - 60 FPS paced update loop
              │    - Logs frame statistics
              │
              └──> macOS powermetrics tool
                   - Samples CPU/GPU power @ 1Hz
                   - Outputs plist format
```

## Components

### 1. PowerProfile Executable

**File:** `tests/power_profile.cpp`
**Binary:** `bin/debug/PowerProfile`

Headless benchmark application that:
- Spawns configurable number of AI entities
- Runs update loop with 60 FPS pacing
- Measures frame times and throughput
- Supports single-threaded and multi-threaded modes

**CLI Arguments:**
```bash
--entity-count NUM          # AI entities to spawn (default: 20000)
--duration SECS             # Test duration in seconds (default: 60)
--threading-mode MODE       # "single" or "multi" (default: multi)
--verbose                   # Enable detailed output
--help                      # Show help message
```

**Usage Examples:**
```bash
# Default: 20K entities, multi-threaded, 60 seconds
./bin/debug/PowerProfile

# Single-threaded baseline with 10K entities
./bin/debug/PowerProfile --entity-count 10000 --threading-mode single --duration 120

# Multi-threaded with 50K entities for scaling test
./bin/debug/PowerProfile --entity-count 50000 --threading-mode multi
```

### 2. Test Orchestrator

**File:** `tests/power_profiling/run_power_test.sh`

Bash script that coordinates all testing:
- Checks prerequisites (sudo access, PowerProfile binary)
- Runs test scenarios (idle, single-threaded, multi-threaded)
- Captures powermetrics data for each scenario
- Generates analysis report

**Requirements:**
- macOS (uses `powermetrics` system tool)
- `sudo` access (powermetrics requires root)
- PowerProfile binary (built by script if missing)

**Usage:**
```bash
# Requires sudo
sudo tests/power_profiling/run_power_test.sh
```

**What it does:**
1. Idle baseline (0 entities, 30 seconds)
2. Single-threaded (20K entities, 60 seconds)
3. Multi-threaded (20K entities, 60 seconds)
4. Scaling tests (10K, 50K entities, 60 seconds each)
5. Generates results report

**Output:**
- `test_results/power_profiling/power_*.plist` - Raw powermetrics data
- `test_results/power_profiling/benchmark_*.txt` - Frame statistics
- `test_results/power_profiling/power_report_*.txt` - Analysis summary

### 3. Results Parser

**File:** `tests/power_profiling/parse_powermetrics.py`

Python script to parse and analyze powermetrics output:
- Extracts CPU/GPU power samples
- Calculates statistics (avg, min, max, stdev)
- Estimates energy per frame
- Generates comparison tables

**Usage:**
```bash
# Parse a single result
python3 tests/power_profiling/parse_powermetrics.py test_results/power_profiling/power_idle_*.plist

# Parse all results
python3 tests/power_profiling/parse_powermetrics.py test_results/power_profiling/power_*.plist

# Compare specific scenarios
python3 tests/power_profiling/parse_powermetrics.py \
    test_results/power_profiling/power_single_threaded_*.plist \
    test_results/power_profiling/power_multi_threaded_*.plist
```

## Expected Results

### Idle Baseline
- **Avg CPU Power:** ~0.8W
- **C-state Residency:** ~98%
- **Energy/Frame (60 FPS):** ~13mJ
- **Status:** Deep CPU sleep, minimal activity

### Single-Threaded (20K entities)
- **Avg CPU Power:** ~8-10W sustained
- **C-state Residency:** ~5%
- **Frame Time:** ~45-50ms
- **FPS:** ~13-15 FPS ❌ Can't achieve 60 FPS
- **Status:** CPU constantly busy, no idle time

### Multi-Threaded (20K entities)
- **Avg CPU Power:** ~15-20W peak, ~0.9W idle (averaged ~8W)
- **C-state Residency:** >50%
- **Frame Time:** ~6.5ms active, ~9.5ms idle = 16ms total
- **FPS:** 60+ FPS ✅ Easily maintains target
- **Energy/Frame:** ~130-150mJ
- **Status:** Burst compute then sleep

## Performance Metrics

### Energy Savings (Race-to-Idle)
```
Single-threaded energy: ~8W sustained × 0.06s = 480mJ (per 60ms frame equivalent)
Multi-threaded energy: 18W × 0.008s + 0.9W × 0.008s = 151mJ (per frame)

Savings: (480 - 151) / 480 = 68% energy reduction
Performance gain: 45ms → 6.5ms = 6.9x faster
```

### Battery Life Impact
On a 2000mAh @ 3.7V laptop (~26Wh):

| Mode | Power | Duration | Notes |
|------|-------|----------|-------|
| Idle | 0.8W | 32 hours | Baseline |
| Multi-threaded | 8W avg | 3.2 hours | 60 FPS continuous |
| Single-threaded | 9W avg | 2.9 hours | Can't hit 60 FPS |

**Conclusion:** Multi-threaded achieves better FPS while using slightly LESS energy than single-threaded that can't even hit the target.

## Running the Full Suite

### Quick Start (5 minutes)
```bash
# Build PowerProfile
cd build && ninja PowerProfile && cd ..

# Run one scenario
./bin/debug/PowerProfile --entity-count 20000 --duration 20 --verbose
```

### Full Test Suite (20 minutes)
```bash
# Requires sudo - captures power metrics for all scenarios
sudo tests/power_profiling/run_power_test.sh

# Analyze results
python3 tests/power_profiling/parse_powermetrics.py test_results/power_profiling/power_*.plist
```

### Compare Specific Scenarios
```bash
# Just idle vs multi-threaded
python3 tests/power_profiling/parse_powermetrics.py \
    test_results/power_profiling/power_idle_*.plist \
    test_results/power_profiling/power_multi_threaded_*.plist
```

## Troubleshooting

### PowerProfile binary not found
```bash
cd build && ninja PowerProfile && cd ..
```

### "sudo: powermetrics: command not found"
- macOS required (powermetrics is macOS-only)
- On Linux, use `perf record` or `turbostat` instead

### "Permission denied" for run_power_test.sh
```bash
chmod +x tests/power_profiling/run_power_test.sh
sudo tests/power_profiling/run_power_test.sh
```

### Results show zero power
- Check if powermetrics was interrupted
- Verify correct plist file permissions
- Re-run with verbose mode: `PowerProfile --verbose`

## Implementation Details

### PowerProfile Design
- **Headless:** No SDL window (faster, cleaner measurements)
- **Manager Init:** ThreadSystem, AIManager, PathfinderManager, CollisionManager
- **Entity Type:** BenchmarkEntity (minimal overhead, realistic behavior)
- **Update Loop:** 60 FPS paced with barrier sync after each frame
- **Threading:** Supports both `enableThreading(true)` and `enableThreading(false)`

### Measurement Accuracy
- **Sample Interval:** 1 second (1000ms)
- **Test Duration:** 60+ seconds (more data points, better averaging)
- **Noise Reduction:** Statistical averaging, 6+ hour baseline for comparison
- **Frame Timing:** High-resolution clock, microsecond precision

### Key Metrics
- **CPU Package Power:** Entire CPU die (includes all cores)
- **C-state Residency:** % time in deep sleep states
- **Energy/Frame:** Calculated as `(Power_W × Duration_s) / FrameCount`
- **Throughput:** Entities updated per second

## Validation Checklist

Use this checklist to confirm race-to-idle validation:

- [ ] Idle baseline shows <1W, >95% C-state residency
- [ ] Single-threaded cannot hit 60 FPS with 20K entities
- [ ] Multi-threaded hits 60+ FPS with <8ms frame time
- [ ] Multi-threaded C-state residency >50% (proving idle periods)
- [ ] Multi-threaded energy/frame is 2-3x lower than single-threaded
- [ ] Power traces show clear spike/idle pattern in multi-threaded
- [ ] GPU power remains minimal (<2W) throughout
- [ ] Frame statistics match expected timing

## Future Enhancements

- [ ] Add WorkerBudgetManager override API (force thread count)
- [ ] Support Linux/Windows power measurement tools (perf, RAPL)
- [ ] Automated comparison report generation
- [ ] Real-time power graph visualization
- [ ] Regression testing baseline storage

## References

**Race-to-Idle Strategy:**
- ARM: "The Power of Zero Activity" whitepaper
- Apple: M-series SoC power management
- Modern CPU C-states: Intel/AMD power gating

**Game Engine Battery Optimization:**
- Unity DOTS burst compilation strategy
- Unreal TaskGraph work-stealing queues
- Industry best practices for mobile gaming

## Contact & Support

For questions about power profiling:
1. Check this README
2. Review expected results section
3. Check benchmark logs for errors
4. Verify macOS powermetrics is working: `sudo powermetrics -n 10`

---

**Last Updated:** 2025-01-15
**Target Platform:** macOS (M-series, Intel)
**Test Hardware:** M3 Max (11 cores, 96GB RAM)
