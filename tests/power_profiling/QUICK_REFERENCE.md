# Power Profiling Quick Reference

## One-Liner Commands

```bash
# Measure real game for 30 seconds
sudo tests/power_profiling/run_power_test.sh --real-app

# Measure for 60 seconds
sudo tests/power_profiling/run_power_test.sh --real-app --duration 60

# Run headless benchmarks (default)
sudo tests/power_profiling/run_power_test.sh

# Show help
tests/power_profiling/run_power_test.sh --help
```

## Two Measurement Modes

### Headless (AI Only - Default)
```bash
sudo tests/power_profiling/run_power_test.sh
```
- **What's measured:** AI system only
- **Duration:** ~30 minutes for full suite
- **Best for:** Algorithm tuning, baseline
- **Files:** `power_idle_*.plist`, `power_multi_threaded_*.plist`

### Real App (Full Game - New)
```bash
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30
```
- **What's measured:** Complete game stack (rendering, collision, pathfinding, events, UI, GPU)
- **Duration:** 5-10 minutes per measurement
- **Best for:** Battery impact, real-world data
- **Files:** `power_realapp_gameplay_*.plist`

## Expected Results

| Scenario | CPU Power | GPU Power | C-State Residency | Battery Impact |
|----------|-----------|-----------|-------------------|-----------------|
| Idle menu | 1-2W | 0.5-1W | 95%+ | ✓ Excellent |
| Normal gameplay (1K entities) | 5-10W | 2-5W | 60-80% | ✓ Good |
| Heavy gameplay (10K+ entities) | 10-15W | 5-10W | 40-60% | ⚠ Moderate |

## Key Metrics

### C-State Residency (Most Important)
- **>80%** = Excellent (cores sleeping)
- **60-80%** = Good (idle periods remain)
- **40-60%** = Moderate (sustained work)
- **<40%** = Poor (CPU rarely sleeps)

### CPU Power
- **<5W** = Idle baseline
- **5-10W** = Light gameplay
- **10-15W** = Heavy workload
- **>15W** = High load (watch battery)

### GPU Power (Real App Only)
- **<1W** = Minimal rendering
- **1-5W** = Normal 2D rendering
- **>5W** = Heavy effects (particles, etc.)

## Parse Results

```bash
# Parse real app results
python3 tests/power_profiling/parse_powermetrics.py power_realapp_*.plist

# Compare headless vs real app
python3 tests/power_profiling/parse_powermetrics.py \
  power_multi_threaded_*.plist \
  power_realapp_gameplay_*.plist
```

## Calculate Rendering Overhead

```
Rendering Overhead = Real App Power - Headless Power

Example:
  Headless (AI):     12W
  Real App (game):   18W
  Overhead:          6W (50% increase)
```

## Calculate Battery Life

```
Battery Hours = (Battery mAh × 3.7V / 1000) / Avg Power (W)

Example:
  5000 mAh battery
  18W average power
  = (5000 × 3.7 / 1000) / 18
  = 18.5 Wh / 18W
  = ~1 hour gameplay
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Permission denied | Use `sudo` |
| Game won't launch | Run `ninja -C build SDL3_Template` first |
| No output files | Check sudo access and disk space |
| Results look wrong | Verify game ran entire duration |
| High power (>30W) | Check for particle effects or rendering issues |
| Low idle residency (<40%) | Investigate rendering loop or vsync settings |

## Workflow Example

```bash
# 1. Build
cmake -B build -G Ninja && ninja -C build

# 2. Run headless baseline
sudo tests/power_profiling/run_power_test.sh

# 3. Run real app (30 sec)
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30

# 4. Parse both
python3 tests/power_profiling/parse_powermetrics.py power_*.plist power_realapp_*.plist

# 5. Analyze
# Check idle residency (should be >60%)
# Calculate rendering overhead
# Validate battery life estimate
```

## Files Generated

### Headless Mode
- `power_idle_TIMESTAMP.plist` - Idle baseline
- `power_single_threaded_TIMESTAMP.plist` - Single-threaded comparison
- `power_multi_threaded_TIMESTAMP.plist` - Multi-threaded (10 workers)
- `power_report_TIMESTAMP.txt` - Summary report

### Real App Mode
- `power_realapp_gameplay_TIMESTAMP.plist` - Full game measurement
- `power_report_TIMESTAMP.txt` - Summary report

## Race-to-Idle Validation

```
Expected frame timeline (16.67ms @ 60 FPS):

Headless:
  Work: 2-5ms
  Idle: 11-14ms
  Result: 95% idle residency ✓

Real App (with rendering):
  Work: 4-6ms (AI + rendering)
  Idle: 10-12ms (vsync wait)
  Result: 70% idle residency ✓ (still excellent!)
```

Both modes show strong C-state residency because rendering completes quickly!

## Common Scenarios

### Measure Pure Rendering Cost
```bash
# Let game sit at menu (no AI work)
sudo tests/power_profiling/run_power_test.sh --real-app --duration 30
# Compare with headless baseline
# Difference = rendering overhead
```

### Measure Entity Impact
```bash
# Play with different entity densities
sudo tests/power_profiling/run_power_test.sh --real-app --duration 120
```

### Full Battery Drain Test
```bash
# 5-minute continuous gameplay
sudo tests/power_profiling/run_power_test.sh --real-app --duration 300
```

## Performance Targets

| Metric | Target | Status |
|--------|--------|--------|
| C-State Residency (gameplay) | >60% | Track this! |
| Power Draw (idle) | <2W | Expected |
| Power Draw (1K entities) | <10W | Expected |
| Power Draw (10K entities) | <20W | Expected |
| Frame time (1K entities) | <10ms | Expected |
| Idle time per frame | >10ms | Needed for C-states |

---

For detailed docs, see `REAL_APP_PROFILING.md`
