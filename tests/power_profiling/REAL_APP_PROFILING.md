# Real Application Power Profiling

You can now measure the actual SDL3_Template game's power consumption with all systems running, not just the headless AI benchmark.

## Overview

**Two measurement modes:**

1. **Headless Mode (default)** - `PowerProfile` executable
   - AI system only (no rendering)
   - Establishes baseline for AI scaling
   - Fast iteration for tuning threading
   - ~30 minutes for full test suite

2. **Real App Mode (new)** - Full `SDL3_Template` game
   - All systems active: rendering, collision, pathfinding, events, UI
   - Includes GPU power draw
   - Realistic battery impact on actual devices
   - ~5-10 minutes per measurement

## Quick Start

```bash
# Measure real game for 30 seconds (default)
sudo tests/power_profiling/run_power_test.sh --real-app

# Measure for 60 seconds
sudo tests/power_profiling/run_power_test.sh --real-app --duration 60

# Measure for 2 minutes
sudo tests/power_profiling/run_power_test.sh --real-app --duration 120
```

## What Gets Measured in Real App Mode

When you run with `--real-app`, the following are active:

- **Rendering** (SDL3 2D graphics pipeline)
- **Collision Detection** (spatial hash queries)
- **Pathfinding** (A* calculations in background threads)
- **Event System** (game events, input handling)
- **AI Behaviors** (behavior tree execution)
- **Particle System** (if any active)
- **Game State Management** (all managers running)
- **GPU Power Draw** (captured in measurements)

## Interpreting Results

### Expected Power Consumption

**Idle Game (main menu):**
- CPU: 1-2W
- GPU: 0.5-1W
- C-state residency: 95%+
- Battery friendly: Just waiting for input

**Gameplay (1000 entities):**
- CPU: 5-10W (active simulation + rendering)
- GPU: 2-5W (2D scene rendering)
- C-state residency: 60-80%
- Race-to-idle still effective with rendering

**Heavy Gameplay (10K+ entities):**
- CPU: 10-15W
- GPU: 5-10W
- C-state residency: 40-60%
- More sustained work, fewer idle windows

### Comparing Modes

Use both modes to understand overhead:

```
Real App Power = AI System Power + Rendering Overhead + GPU Power

Example calculation:
  PowerProfile (AI only):     12W average
  Real App (full game):       18W average
  ─────────────────────────
  Rendering overhead:         6W
  (5W CPU + 1W GPU extra)
```

## Advanced: Comparing Scenarios

### Measurement 1: Headless AI Baseline

```bash
sudo tests/power_profiling/run_power_test.sh
# Generates: power_idle_*.plist, power_single_threaded_*.plist, power_multi_threaded_*.plist
```

### Measurement 2: Real Game Idle

```bash
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30
# Let game sit at main menu for 30 seconds
# Generates: power_realapp_gameplay_*.plist
```

### Measurement 3: Real Game Active Play

```bash
sudo tests/power_profiling/run_power_test.sh --real-app --duration 60
# Play the game normally for 60 seconds with various entity counts
# Generates: power_realapp_gameplay_*.plist
```

### Analysis: Compare All Results

```bash
python3 tests/power_profiling/parse_powermetrics.py \
  power_multi_threaded_*.plist \
  power_realapp_gameplay_*.plist
```

This shows:
1. AI system efficiency (PowerProfile data)
2. Full-stack efficiency (Real App data)
3. Rendering impact on race-to-idle
4. GPU contribution to power draw

## Performance Tips While Measuring

When running real app mode, you can:

- **Let it idle** - Sit at menu to measure baseline
- **Play normally** - Walk around, interact with world
- **Spawn entities** - If game has entity spawning mechanics
- **Run scenarios** - Test different gameplay situations

The powermetrics data captures everything that happens during the duration.

## Parsing Results

After running measurements:

```bash
# Parse all real app results
python3 tests/power_profiling/parse_powermetrics.py power_realapp_*.plist

# Parse specific measurement
python3 tests/power_profiling/parse_powermetrics.py power_realapp_gameplay_20250101_120000.plist

# Parse both headless and real app
python3 tests/power_profiling/parse_powermetrics.py power_*.plist power_realapp_*.plist
```

Output shows:
- Average CPU and GPU power (W)
- CPU idle residency (% time in C-states)
- Per-CPU frequency and activity breakdown
- Comparison summary if multiple files provided

## Key Metrics to Look For

### C-State Residency (Most Important for Battery)
- **>80%** = Excellent battery life, cores sleeping most of time
- **60-80%** = Good, still significant idle periods
- **40-60%** = Moderate, more sustained work
- **<40%** = Poor battery life, CPU constantly active

### Power Draw During Idle
- **<1W** = Perfect (deep sleep)
- **1-3W** = Good
- **3-5W** = Acceptable
- **>5W** = Should investigate (maybe rendering loop too fast)

### Power Draw During Gameplay
- **<10W** = Excellent efficiency
- **10-15W** = Good for busy gameplay
- **15-25W** = Heavy workload, watch battery drain
- **>25W** = Concerning for portable devices

## Troubleshooting

### Game won't launch
```bash
# Build game first
cd build
ninja SDL3_Template
cd ..
sudo tests/power_profiling/run_power_test.sh --real-app
```

### Powermetrics permission denied
```bash
# Always use sudo
sudo tests/power_profiling/run_power_test.sh --real-app
```

### Results seem wrong
```bash
# Check file was created
ls -lh test_results/power_profiling/power_realapp_*.plist

# Check parsing
python3 tests/power_profiling/parse_powermetrics.py power_realapp_*.plist

# Verify idle residency is >80% for inactive game
```

## Examples: Real-World Scenarios

### Scenario 1: Measure Rendering Impact

```bash
# Headless baseline (AI only)
sudo tests/power_profiling/run_power_test.sh

# Real app with paused simulation
# (game running but frozen - pure rendering)
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30

# Compare:
# power_multi_threaded_20000_*.plist (AI work)
# power_realapp_gameplay_*.plist (rendering impact)
```

### Scenario 2: Measure Entity Count Impact

```bash
# Headless with high entity count
sudo tests/power_profiling/run_power_test.sh

# Real app with varying entity density
# (play through areas with different population)
sudo tests/power_profiling/run_power_test.sh --real-app --duration 120
```

### Scenario 3: Mobile Battery Test

```bash
# Measure laptop battery usage during gameplay
sudo tests/power_profiling/run_power_test.sh --real-app --duration 300
# (5 minute measurement simulates typical gameplay session)

# Battery drain rate = Avg Power × Time / Battery Capacity
# Example: 15W × 300s / 50Wh battery = 90 seconds to drain 1%
```

## Complete Workflow Example

**Goal:** Measure rendering overhead on M3 Max

```bash
# Step 1: Build the game
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build

# Step 2: Run headless benchmark (AI only baseline)
sudo tests/power_profiling/run_power_test.sh
# This will run full test suite (~30 minutes)
# Output: power_idle_*.plist, power_multi_threaded_*.plist, etc.

# Step 3: Run real app measurement (30 seconds)
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30
# Game window opens, measurement runs
# Output: power_realapp_gameplay_*.plist

# Step 4: Parse and analyze all results
python3 tests/power_profiling/parse_powermetrics.py \
  power_multi_threaded_*.plist \
  power_realapp_gameplay_*.plist

# Step 5: Compare results
# AI system only:      12W avg, 95% idle residency
# Real app (gameplay): 18W avg, 70% idle residency
# ────────────────────────────────────────────────
# Rendering overhead:  6W (5W CPU + 1W GPU extra)
# Idle time lost:      25% (but race-to-idle still works!)
```

## Next Steps

After collecting data:

1. **Parse results** with parse_powermetrics.py
2. **Compare modes** to understand overhead
3. **Identify bottlenecks** (CPU vs GPU vs rendering)
4. **Optimize weak areas** (if needed)
5. **Validate improvements** by re-running
6. **Document findings** in project wiki

---

Questions? Check the main `tests/power_profiling/README.md` for more details.
