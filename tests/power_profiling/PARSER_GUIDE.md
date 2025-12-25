# Power Profiling Parser Guide (v2.0)

The `parse_powermetrics.py` script analyzes power consumption data from macOS `powermetrics` tool. It automatically detects whether the data is from headless benchmarks (AI/Collision/Pathfinding only) or real-app gameplay (full game stack).

## Quick Start

```bash
# Auto-detect mode (recommended)
python3 tests/power_profiling/parse_powermetrics.py tests/test_results/power_profiling/power_*.plist

# Explicit headless mode
python3 tests/power_profiling/parse_powermetrics.py tests/test_results/power_profiling/power_*.plist --headless

# Explicit real-app mode
python3 tests/power_profiling/parse_powermetrics.py tests/test_results/power_profiling/power_realapp_*.plist --real-app
```

## How It Works

### Auto-Detection
The parser automatically detects the data source:
1. Checks if a corresponding `benchmark_*.txt` file exists
2. If found → treats as **headless benchmark** (correlates with performance metrics)
3. If not found → treats as **real-app gameplay** (shows CPU residency breakdown)

### Input Data Format
All `.plist` files from `powermetrics` are text format (not binary):
```
Combined Power (CPU + GPU + ANE): 1475 mW
```

The parser extracts these power samples and correlates them with:
- **Headless:** Benchmark performance logs (entity count, FPS, frame time)
- **Real-app:** CPU residency data from the same power sample file

## Headless Mode Analysis

### What It Measures
- **AI system efficiency** (no rendering, no SDL overhead)
- **Pure algorithm performance** (pathfinding, collision, behavior updates)
- **Scaling behavior** with entity count (10K, 20K, 50K, etc.)

### Output Breakdown

```
Performance Metrics:
  Entity Count:       50,000
  Threading Mode:     multi
  Active Workers:     10
  Total Frames:       2,938
  Avg Frame Time:     20.42ms
  Avg FPS:            49.0

Power Consumption (AI/Collision/Pathfinding only - NO RENDERING):
  Average:            0.13W
  Min:                0.01W
  Max:                0.97W
  Std Dev:            0.19W

Efficiency Metrics:
  Power per entity:   0.003mW/entity
  Throughput:         2,450,000 entity-updates/sec

Battery Impact (This Test):
  Test Duration:      1.0 minutes
  Energy Consumed:    0.0022Wh
  Battery Drain:      0.003%
```

### Interpreting Headless Results

| Metric | Meaning |
|--------|---------|
| **Power Avg** | Average watts during test (everything counts—work + idle) |
| **Power per entity** | Efficiency factor (lower = better scaling) |
| **Throughput** | Entity-updates per second (AI + collision + pathfinding) |
| **Battery Drain** | Actual battery consumed during test (0.003% = insanely efficient) |

**Important:** Headless power numbers seem unrealistic (535 hours continuous play) because rendering isn't included. Real battery life is 5-6 hours with rendering.

### Example Headless Analysis

```bash
python3 tests/power_profiling/parse_powermetrics.py \
  tests/test_results/power_profiling/power_idle_*.plist \
  tests/test_results/power_profiling/power_multi_10000_*.plist \
  tests/test_results/power_profiling/power_multi_50000_*.plist \
  --headless
```

**Expected output:** Three separate analyses, showing how power scales from 0 to 50K entities.

## Real-App Mode Analysis

### What It Measures
- **Complete game stack** (rendering, AI, collision, pathfinding, events, UI)
- **Actual battery impact** during gameplay
- **CPU vs GPU power distribution**
- **Idle residency** (proof of race-to-idle working)

### Output Breakdown

```
CPU Residency Statistics (440 CPUs):
  Active Residency:   17.46% avg (min: 0.00%, max: 100.00%)
  Idle Residency:     82.54% avg (min: 0.00%, max: 100.00%)

CPU Frequency Statistics:
  Average:  1544 MHz
  Min:      0 MHz
  Max:      4056 MHz

Per-CPU Breakdown:
  CPU 0: Active  28.67%, Idle  71.33%, Freq    1634 MHz
  CPU 1: Active  25.05%, Idle  74.95%, Freq    1637 MHz
  ...

Power Analysis:
Actual Power Consumption:
  Average:      2.11 W
  Min:          0.04 W (during sleep/vsync)
  Max:          27.94 W (peak gameplay)
  Std Dev:      5.73 W
  Samples:      40

Battery Life Estimates (M3 Pro 14": 70 Wh):
  Average Load:      2.1W → 33.2 hours (1990 min)
  Idle/Sleep:        0.04W → 1944 hours
  Peak Gameplay:     27.94W → 2.5 hours

Load Breakdown Estimate:
  81% idle time:     ~0.04W
  19% active time:   ~11.11W
  → Continuous gameplay (no idle): 6.3 hours
```

### Interpreting Real-App Results

| Metric | Meaning |
|--------|---------|
| **Active Residency** | % of time CPU is doing work (rendering, AI, etc.) |
| **Idle Residency** | % of time CPU is sleeping (C-states) |
| **Power Avg** | Average watts during gameplay |
| **Power Min** | Power during vsync wait (CPU sleeping) |
| **Power Max** | Peak power during intense work |
| **Continuous gameplay** | Realistic hours if playing non-stop |

**Key Insight:** Even with 19% active work time, the CPU spends 81% idle, which is excellent for battery life.

## Comparing Results

### Headless vs Real-App Overhead

```bash
# Get headless baseline
python3 tests/power_profiling/parse_powermetrics.py \
  tests/test_results/power_profiling/power_multi_50000_*.plist --headless

# Get real-app with 50K entities
python3 tests/power_profiling/parse_powermetrics.py \
  tests/test_results/power_profiling/power_realapp_*.plist --real-app

# Calculate rendering overhead:
# Real-app average (e.g., 2.11W) - Headless average (0.13W) = 1.98W rendering overhead
```

### Multiple Real-App Runs

```bash
python3 tests/power_profiling/parse_powermetrics.py \
  tests/test_results/power_profiling/power_realapp_gameplay_20251225_061937.plist \
  tests/test_results/power_profiling/power_realapp_gameplay_20251225_062957.plist \
  --real-app
```

This runs analysis on both files and shows any variance between runs.

## Battery Calculations

### Given M3 Pro 14" with 70Wh battery:

**Light Gameplay (2.1W average):**
```
Battery hours = 70Wh / 2.1W = 33.2 hours
```
(But this assumes continuous play with idle periods—realistic for extended sessions)

**Continuous Play (11W active, 19% of time):**
```
Actual power = (11W × 0.19) + (0.04W × 0.81) = 2.11W (matches measured!)
Battery hours = 70Wh / 11W = 6.3 hours (all systems maxed)
```

**Peak Load (28W):**
```
Battery hours = 70Wh / 28W = 2.5 hours
```

### For Your Device

If your device has a different battery:
```
Battery hours = (Battery capacity Wh) / (Average Power W)

Example (80Wh battery):
  Light play: 80 / 2.1 = 38 hours
  Continuous: 80 / 11 = 7.3 hours
  Peak: 80 / 28 = 2.9 hours
```

## Command Reference

```bash
# Parse all results with auto-detection
python3 tests/power_profiling/parse_powermetrics.py tests/test_results/power_profiling/*.plist

# Parse with explicit mode
python3 tests/power_profiling/parse_powermetrics.py <files> [--headless | --real-app]

# Compare specific files
python3 tests/power_profiling/parse_powermetrics.py file1.plist file2.plist file3.plist

# Single file analysis
python3 tests/power_profiling/parse_powermetrics.py tests/test_results/power_profiling/power_multi_50000_*.plist
```

## Troubleshooting

### "Error parsing plist" message
- **This is normal.** The script tries binary plist first, then falls back to text format (which is what `powermetrics` outputs).
- You'll see this message but analysis still works.

### No results being parsed
- Check file exists: `ls tests/test_results/power_profiling/power_*.plist`
- Check file has content: `wc -l tests/test_results/power_profiling/power_*.plist`
- Verify powermetrics actually ran and captured data

### Battery life seems unrealistic
- **For headless:** Rendering isn't included, so numbers are theoretical. Use real-app data instead.
- **For real-app:** Check idle residency. >80% means race-to-idle is working. Lower means rendering loop isn't idle-friendly.

### "No matching benchmark log found"
- The script is looking for `benchmark_*.txt` corresponding to `power_*.plist`
- Make sure both files exist in the same directory
- If parsing real-app, this is expected (there's no benchmark log for gameplay)

## Advanced Usage

### Extract Just Power Numbers

```python
# Programmatically in your own script:
import re
from pathlib import Path

def get_power_samples(plist_file):
    samples = []
    with open(plist_file) as f:
        for line in f:
            if line.startswith('Combined Power'):
                power_mW = float(line.split(': ')[1].replace(' mW', ''))
                samples.append(power_mW / 1000)  # Convert to watts
    return samples
```

### Batch Analysis

```bash
# Process all headless benchmarks
for file in tests/test_results/power_profiling/power_multi_*.plist; do
  python3 tests/power_profiling/parse_powermetrics.py "$file" --headless
done
```

## Performance Targets Summary

**These metrics validate the race-to-idle strategy:**

- ✅ **Idle residency >80%** = Race-to-idle working
- ✅ **50K entities = 0.13W average** = Insanely efficient AI system
- ✅ **All headless tests <0.01% battery drain** = Minimal overhead
- ✅ **Real-app idle residency 80%+** = Rendering completes quickly
- ✅ **5-6 hours continuous play** = Acceptable for games

If any metric is out of spec, investigate:
- Low idle residency? → Check rendering loop or vsync settings
- High power per entity? → Profile AI algorithms or collision checks
- Power drain increasing with entities? → Look for per-entity allocations

---

**Last Updated:** 2025-12-25
**Parser Version:** 2.0 (Dual-mode auto-detection)
**Tested On:** M3 Pro 14" (12 cores, 70Wh battery)
