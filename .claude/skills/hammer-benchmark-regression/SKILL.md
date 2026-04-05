---
name: hammer-benchmark-regression
description: Runs performance benchmarks for SDL3 HammerEngine and detects regressions by comparing metrics against baseline. Use when testing performance-sensitive changes to AI, collision, pathfinding, particle systems, or before merging features to ensure no performance degradation.
allowed-tools: [Bash, Read, Write, Grep]
---

# HammerEngine Performance Regression Detection

This Skill is **critical** for SDL3 HammerEngine's performance requirements. The engine must maintain 10,000+ entity support at 60+ FPS with minimal CPU usage. This Skill detects performance regressions before they reach production.

## Cross-Platform Considerations

**IMPORTANT:** Absolute performance numbers vary significantly across platforms (CPU, memory, OS).
Regression detection should always compare against **platform-specific baselines**, not hard-coded
absolute values. Example outputs in this document show representative values from one platform.

**Baseline Strategy:**
- Each platform maintains its own baseline in `test_results/baseline/`
- Compare percentage change from baseline, not absolute numbers
- Thresholds (>15% = CRITICAL, etc.) apply universally across platforms

## Performance Requirements (from CLAUDE.md)

- **AI System:** 10,000+ entities at 60+ FPS with <6% CPU
- **Collision System:** Spatial hash with efficient AABB detection
- **Pathfinding:** A* pathfinding with dynamic weights
- **Event System:** 1K-10K event throughput
- **Particle System:** Camera-aware batched rendering

## Workflow Overview

**⚠️ CRITICAL: AI Scaling Benchmark is MANDATORY**
The AI System is the most performance-critical component. Always run `./tests/test_scripts/run_ai_benchmark.sh` as part of the regression check. DO NOT proceed to report generation without AI benchmark results.

1. **Identify or Create Baseline** - Store previous metrics
2. **Run Benchmark Suite** - Execute ALL 11 performance tests (including AI)
3. **Extract Metrics** - Parse results from test outputs
4. **Compare vs Baseline** - Calculate percentage changes
5. **Flag Regressions** - Alert on performance degradation
6. **Generate Report** - Detailed analysis with recommendations

**Checklist before generating report:**
- [ ] AI Scaling Benchmark completed
- [ ] Collision System Benchmark completed
- [ ] **Pathfinder Benchmark completed** ← CRITICAL: Always verify metrics extracted!
- [ ] Event Manager Scaling completed
- [ ] Particle Manager Benchmark completed
- [ ] GPU Frame Timing Benchmark completed
- [ ] SIMD Performance Benchmark completed
- [ ] Integrated System Benchmark completed
- [ ] Background Simulation Benchmark completed
- [ ] Adaptive Threading Analysis completed
- [ ] Projectile Scaling Benchmark completed

**Metrics Extraction Verification (MANDATORY):**
- [ ] AI: Entity scaling metrics and updates/sec extracted
- [ ] **Pathfinding: Async throughput metrics extracted (NOT immediate timing)** ← PRODUCTION METRIC ONLY!
- [ ] Collision: SOA timing and efficiency extracted
- [ ] **Trigger Detection: Detector count, overlaps, method (spatial/sweep) extracted**
- [ ] Event: Throughput and latency extracted
- [ ] Particle: Update time extracted
- [ ] SIMD: Speedup factors extracted for all 4 operations (AI Distance, Bounds, Layer Mask, Particle Physics)
- [ ] Integrated: Frame time statistics and scaling summary extracted
- [ ] Background Sim: Scaling data and threading threshold extracted
- [ ] Adaptive Threading: MIN_WORKLOAD enforcement, threshold learning, hysteresis, batch multiplier extracted
- [ ] Projectile: Entity scaling (entities/ms, ns/entity), SIMD 4-wide throughput extracted

## Benchmark Test Suites

### Available Benchmarks (ALL REQUIRED)

**Working Directory:** Use absolute path to project root or set `$PROJECT_ROOT` environment variable.
All paths below are relative to project root.

**IMPORTANT: All 11 benchmarks MUST be run for complete regression analysis.**

1. **AI Scaling Benchmark** (`./bin/debug/ai_scaling_benchmark`) **[REQUIRED - CRITICAL]**
   - Script: `./tests/test_scripts/run_ai_benchmark.sh`
   - Tests: Entity scaling (100-10000), threading comparison, behavior mix
   - Metrics: Entity updates/sec, time per update, threading speedup
   - Target: Compare against platform baseline (no regression >15%)
   - Duration: ~3 minutes
   - **Status: CRITICAL - Engine core performance benchmark**

2. **Collision Scaling Benchmark** (`./bin/debug/collision_scaling_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_collision_scaling_benchmark.sh`
   - Tests: SAP (Sweep-and-Prune) for MM, Spatial Hash for MS, Trigger Detection scaling
   - Metrics: MM/MS time, throughput, pair counts, trigger detection overlaps, sub-quadratic scaling
   - **Trigger Detection:** Tests spatial query (<50 entities) and sweep-and-prune (>=50 entities) paths
   - Duration: ~2 minutes

3. **Pathfinder Benchmark** (`./bin/debug/pathfinder_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_pathfinder_benchmark.sh`
   - Tests: Async pathfinding throughput at scale
   - Metrics: **Async throughput (paths/sec), batch processing performance, success rate**
   - **Note:** Immediate pathfinding deprecated - only track async metrics
   - Duration: ~5 minutes

4. **Event Manager Scaling** (`./bin/debug/event_manager_scaling_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_event_scaling_benchmark.sh`
   - Tests: Event throughput 10-4000 events, concurrency
   - Metrics: Events/sec, dispatch latency, queue depth
   - Duration: ~2 minutes

5. **Particle Manager Benchmark** (`./bin/debug/particle_manager_performance_tests`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_particle_manager_benchmark.sh`
   - Tests: Batch rendering performance, particle updates
   - Metrics: Particles/frame, update time, batch count
   - Duration: ~2 minutes

6. **GPU Frame Timing Benchmark** (`./bin/debug/gpu_frame_timing_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_gpu_frame_benchmark.sh`
   - Tests: GPU rendering pipeline performance (Vulkan/SPIR-V)
   - Metrics: Avg frame time, swapchain time, GPU upload time, GPU submit time
   - Duration: ~1 minute
   - **Note:** Run from a desktop session for meaningful swapchain/VSync timings.

7. **SIMD Performance Benchmark** (`./bin/debug/simd_performance_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_simd_benchmark.sh`
   - Tests: SIMD vs scalar performance across AI, Collision, Particle operations
   - Metrics: Speedup factor (scalar time / SIMD time), platform detection (SSE2/AVX2/NEON)
   - Target: SIMD ≥1.0x in Release builds (Debug may show SIMD slower due to no optimization)
   - Duration: ~1 minute
   - **Note:** Debug builds lack -O3 optimization, so SIMD intrinsic overhead may exceed benefit.
     Run in Release mode for accurate SIMD performance measurement.

8. **Integrated System Benchmark** (`./bin/debug/integrated_system_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_integrated_benchmark.sh`
   - Tests: Realistic game simulation (10K AI + 5K particles), scaling 1K-20K entities
   - Metrics: Frame time avg/P95/P99, frame drop %, max sustainable entity count, coordination overhead
   - Target (Release): <16.67ms average (60 FPS), <5% frame drops, <2ms coordination overhead
   - Target (Debug): ~5K entities @ 60 FPS (Debug overhead significant)
   - Duration: ~3 minutes

9. **Background Simulation Benchmark** (`./bin/debug/background_simulation_manager_benchmark`) **[REQUIRED]**
   - Script: `./tests/test_scripts/run_background_simulation_manager_benchmark.sh`
   - Tests: Background tier entity scaling (100-10K), threading threshold, adaptive tuning
   - Metrics: Update time, throughput (entities/ms), batch count, threading mode
   - Target: Sub-linear scaling, ~50K+ items/ms throughput (EDM architecture)
   - Duration: ~2 minutes

10. **Adaptive Threading Analysis** (`./bin/debug/adaptive_threading_analysis`) **[REQUIRED]**
    - Script: `./tests/test_scripts/run_adaptive_threading_analysis.sh`
    - Tests: WorkerBudget adaptive logic validation across AI, Collision, Particle, Event systems
    - **Test Cases:**
      - `MinWorkloadEnforcement`: All 4 systems respect MIN_WORKLOAD=100 (forced SINGLE below 100)
      - `AIManager_ThresholdLearning`: AI learns threading threshold when single-threaded time ≥0.9ms
      - `CollisionManager_ThresholdLearning`: Collision learns threshold (typically hits at 2000 entities)
      - `ParticleManager_ThresholdLearning`: Particle learns threshold (typically ~16K-17K particles)
      - `HysteresisRelearning`: Threshold reset and re-learned when workload drops below 95% band
      - `BatchMultiplierTuning`: Batch multiplier validated in range [0.4, 2.0]
      - `ThreadingStateSummary`: Final state summary for all systems
    - Metrics: Learned threshold per system, hysteresis behavior, batch multiplier range
    - **Key Thresholds:**
      - `MIN_WORKLOAD=100`: Always single-threaded below 100 entities (all systems)
      - `LEARNING_TIME_THRESHOLD_MS=0.9ms`: Threading threshold learned when single time ≥ 0.9ms
      - `HYSTERESIS_FACTOR=0.95`: Re-learning triggered when workload drops to 95% of learned threshold
    - Target: All 8 MIN_WORKLOAD tests PASS, thresholds learned, hysteresis working
    - Duration: ~2 minutes

11. **Projectile Scaling Benchmark** (`./bin/debug/projectile_scaling_benchmark`) **[REQUIRED]**
    - Script: `./tests/test_scripts/run_projectile_benchmark.sh`
    - Tests: Projectile entity throughput scaling, SIMD 4-wide position integration
    - **Test Cases:**
      - `ProjectileScaling`: Entity counts 100-5000, entities/ms and ns/entity
      - `ThreadingModeComparison`: Single vs multi-threaded at different counts
      - `SIMDThroughput`: SIMD 4-wide batching efficiency (4-4000 projectiles)
    - Metrics: Entities/ms, ns/entity, SIMD throughput/ms, threading mode
    - Target: ~17K entities/ms peak throughput, SIMD shows improving efficiency 4→256 projectiles
    - Duration: ~1 minute

### Total Benchmark Duration: ~26 minutes

## Execution Steps

### Step 1: Identify Baseline

**Baseline Storage Location:**
```
$PROJECT_ROOT/test_results/baseline/
├── thread_safe_ai_baseline.txt         (AI system baseline)
├── collision_benchmark_baseline.txt
├── pathfinder_benchmark_baseline.txt
├── event_benchmark_baseline.txt
├── particle_manager_baseline.txt
├── buffer_utilization_baseline.txt
├── resource_tests_baseline.txt
├── serialization_baseline.txt
└── baseline_metadata.txt
```

**Baseline Creation Logic:**
```bash
# Check if baseline exists (requires PROJECT_ROOT to be set)
if [ ! -d "$PROJECT_ROOT/test_results/baseline/" ]; then
    echo "No baseline found. Creating baseline from current run..."
    mkdir -p "$PROJECT_ROOT/test_results/baseline/"
    CREATING_BASELINE=true
fi
```

**When to Create New Baseline:**
- No baseline exists (first run)
- User explicitly requests baseline refresh
- Major optimization work completed (intentional performance change)
- After validating improvements (new baseline for future comparisons)

### Step 2: Run Benchmark Suite

**CRITICAL: ALL 11 benchmarks must be run. DO NOT skip the AI benchmark.**

**⚠️ SEQUENTIAL EXECUTION ONLY:** Benchmarks MUST be run one at a time, waiting for each to complete before starting the next. NEVER run benchmarks in parallel (background tasks, concurrent shells, etc.) — parallel execution causes CPU/memory contention that skews timing results and produces unreliable metrics. Use `run_in_background: false` (foreground) for every benchmark invocation.

**Run individually and sequentially (REQUIRED):**
```bash
# IMPORTANT: Set PROJECT_ROOT and run from project directory
# Example: cd /path/to/SDL3_HammerEngine_Template && export PROJECT_ROOT=$(pwd)

# 1. AI Scaling Benchmark (REQUIRED - 3 minutes)
./tests/test_scripts/run_ai_benchmark.sh

# 2. Collision Scaling Benchmark (REQUIRED - 2 minutes)
./tests/test_scripts/run_collision_scaling_benchmark.sh

# 3. Pathfinder Benchmark (REQUIRED - 5 minutes)
./tests/test_scripts/run_pathfinder_benchmark.sh

# 4. Event Manager Scaling (REQUIRED - 2 minutes)
./tests/test_scripts/run_event_scaling_benchmark.sh

# 5. Particle Manager Benchmark (REQUIRED - 2 minutes)
./tests/test_scripts/run_particle_manager_benchmark.sh

# 6. GPU Frame Timing Benchmark (REQUIRED - 1 minute)
./tests/test_scripts/run_gpu_frame_benchmark.sh

# 7. SIMD Performance Benchmark (REQUIRED - 1 minute)
./tests/test_scripts/run_simd_benchmark.sh

# 8. Integrated System Benchmark (REQUIRED - 3 minutes)
./tests/test_scripts/run_integrated_benchmark.sh

# 9. Background Simulation Benchmark (REQUIRED - 2 minutes)
./tests/test_scripts/run_background_simulation_manager_benchmark.sh

# 10. Adaptive Threading Analysis (REQUIRED - 2 minutes)
./tests/test_scripts/run_adaptive_threading_analysis.sh

# 11. Projectile Scaling Benchmark (REQUIRED - 1 minute)
./tests/test_scripts/run_projectile_benchmark.sh
```

**Timeout Protection:**
Each benchmark has timeout protection:
- AI Scaling: 600 seconds (10 minutes)
- Others: 300 seconds (5 minutes)

If timeout occurs, flag as potential infinite loop or performance catastrophe.

**Progress Tracking:**
```
Running benchmarks (this will take ~25 minutes)...
[1/10] AI Scaling Benchmark... ✓ (3m 00s) - CRITICAL
[2/10] Collision Scaling Benchmark... ✓ (2m 05s)
[3/10] Pathfinder Benchmark... ✓ (5m 05s)
[4/10] Event Manager Scaling... ✓ (2m 10s)
[5/10] Particle Manager Benchmark... ✓ (2m 05s)
[6/10] GPU Frame Timing Benchmark... ✓ (1m 02s)
[7/10] SIMD Performance Benchmark... ✓ (1m 00s)
[8/10] Integrated System Benchmark... ✓ (3m 15s)
[9/10] Background Simulation Benchmark... ✓ (2m 00s)
[10/10] Adaptive Threading Analysis... ✓ (2m 00s)
[11/11] Projectile Scaling Benchmark... ✓ (1m 00s)
Total: 26m 42s
```

**Execution Order:**
Run benchmarks **sequentially** in the order listed above, one at a time. AI benchmark should always be run first as it's the most critical system and longest-running test. Wait for each benchmark to fully complete before launching the next — this ensures no resource contention between benchmarks.

### Step 3: Extract Metrics

**Metrics Extraction Patterns:**

#### AI System Metrics

**Extract scaling performance:**
```bash
# Parse tabular output from AIEntityScaling test
grep -A 20 "AI Entity Scaling" test_results/ai_scaling_benchmark_*.txt | \
  grep -E "^\s+[0-9]+\s+[0-9.]+"

# Extract summary metrics (primary regression detection)
grep -E "Entity updates per second:|Threading mode:|Threading threshold" \
  test_results/ai_scaling_benchmark_*.txt

# Or use the current run file
cat test_results/ai_scaling_current.txt | grep -A 5 "SCALABILITY SUMMARY"
```

**Example Output (EDM Architecture - values are platform-specific):**
```
--- AI Entity Scaling ---
  Entities   Time (ms)   Updates/sec   Threading     Status
       100        0.02       4998406      single        OK
       500        0.10       7740798      single        OK
      1000        0.18      11275361      single        OK
      2000        0.43      13993477      single        OK
      5000        0.30     100413620       multi        OK
     10000        0.30     201148289       multi        OK

SCALABILITY SUMMARY:
Entity updates per second: 201148289 (at 10000 entities)
Threading mode: WorkerBudget Multi-threaded
```

**Note:** The EDM architecture (pure data-driven behaviors) achieves ~200M+ updates/sec due to:
- No behavior class instances or virtual dispatch
- Contiguous EDM arrays with excellent cache locality
- Free-function dispatch via `Behaviors::execute(ctx, config)`

**Baseline Key Format:** `Entity_<count>_UpdatesPerSec`

**Key Metrics:**
- **Updates/sec:** Primary performance metric (higher is better)
- **Threading threshold:** Adaptive via WorkerBudget (typically 2000-5000 entities for multi-threading benefit)
- **Scaling efficiency:** Updates/sec should increase sub-linearly with entity count

#### Collision Scaling Metrics
```bash
# Extract metrics from collision scaling benchmark
grep -E "Movables|Statics|Time \(ms\)|Throughput|Scenario" test_results/collision_scaling_current.txt
```

**Example Output:**
```
--- MM Scaling (SAP) ---
  Movables   Time (ms)    MM Pairs    Throughput
       100        0.02           5        5151/ms
       500        0.14          31        3559/ms
      1000        0.22          59        4596/ms
      2000        0.41          97        4893/ms
      5000        1.11         282        4509/ms
     10000        2.26         527        4434/ms

--- MS Scaling (Spatial Hash) ---
   Statics    Movables   Time (ms)    MS Pairs          Mode
       100         200        0.15         155          hash
       500         200        0.14          93          hash
      2000         200        0.15          83          hash
      5000         200        0.15          79          hash
     10000         200        0.15          70          hash
     20000         200        0.15          72          hash

--- Combined Scaling ---
       Scenario   Time (ms)        MM        MS      Total
    Small (500)        0.11        14        15          29
  Medium (1500)        0.19        35        36          71
   Large (3000)        0.34        64        65         129
      XL (6000)        0.65       164       164         328
    XXL (12000)        1.31       305       305         610
```

**Key Metrics:**
- MM SAP: O(n log n) - time grows sub-quadratically with movable count
- MS Hash: O(n) - time stays FLAT as static count increases (spatial hash effectiveness)
- Combined: Sub-quadratic scaling verified up to 12K entities

#### Trigger Detection Metrics
```bash
# Extract trigger detection scaling from collision benchmark
grep -E "Detectors|Triggers|Overlaps|Method" test_results/collision_scaling_current.txt | \
  grep -v "^--"
```

**Example Output:**
```
--- Trigger Detection Scaling ---
   Detectors    Triggers   Time (ms)    Overlaps        Method
           1         100       0.143           0        spatial
           1         400       0.138           0        spatial
          10         200       0.145           1        spatial
          25         200       0.148           4        spatial
          50         200       0.228           3          sweep
         100         200       0.255           9          sweep
         200         400       0.433          44          sweep
```

**Key Metrics:**
- **Detectors:** Entities with NEEDS_TRIGGER_DETECTION flag (Player + enabled NPCs)
- **Triggers:** EventOnly triggers in the world (water, area markers, etc.)
- **Method:** Spatial query (<50 entities) or sweep-and-prune (>=50 entities)
- **Performance Target:** <0.5ms for typical scenarios (1-50 detectors, 100-400 triggers)

**Adaptive Strategy Thresholds:**
- < 50 entities: Spatial queries O(N × ~k nearby triggers)
- ≥ 50 entities: Sweep-and-prune O((N+T) log (N+T))

#### Pathfinder Metrics **[ASYNC THROUGHPUT ONLY]**

**⚠️ IMPORTANT:** PathfinderManager uses **async-only pathfinding** in production. Immediate (synchronous) pathfinding is deprecated and should NOT be tracked in regression analysis.

**Production Metrics Extraction:**
```bash
# Extract async pathfinding throughput - PRIMARY METRIC
grep -E "Async.*Throughput|paths/sec" test_results/pathfinder_benchmark_results.txt | \
  grep -E "Throughput:"

# Example output format:
#   Throughput: 3e+02 paths/sec
#   Throughput: 4e+02 paths/sec
#   Throughput: 4e+02 paths/sec
```

**REQUIRED Metrics to Extract:**
1. **Async throughput** (paths/second) - Production metric
2. **Success rate** (must be 100%)
3. **Batch processing performance** (if high-volume scenarios tested)

**DEPRECATED Metrics (DO NOT TRACK):**
- ❌ Immediate pathfinding timing (deprecated, not used in production)
- ❌ Path calculation time by distance (legacy synchronous metric)
- ❌ Per-path latency measurements (not relevant for async architecture)

**Baseline Comparison Keys:**
- `Pathfinding_Async_Throughput_PathsPerSec`
- `Pathfinding_Batch_Processing_Enabled`
- `Pathfinding_SuccessRate`

**Example Baseline Comparison:**
```
| Metric | Baseline | Current | Change | Status |
|--------|----------|---------|--------|--------|
| Async Throughput | 300-400 paths/sec | 300-400 paths/sec | 0% | ⚪ Stable |
| Batch Processing | 50K paths/sec | 100K paths/sec | +100% | 🟢 Major Improvement |
| Success Rate | 100% | 100% | 0% | ✓ Maintained |
```

**What to Report:**
- Always include a dedicated "Pathfinding System" section in regression reports
- Focus on async throughput as primary metric
- Highlight batch processing performance for high-volume scenarios
- Note success rate (failures are critical regressions)
- Exclude deprecated immediate pathfinding metrics from analysis

#### Event Manager Metrics
```bash
# Extract key throughput metrics from deferred event benchmark
grep -E "Events/sec:|Total time:|Time per event:" test_results/event_scaling_benchmark_output.txt

# Extract threading threshold detection
grep -A 10 "THREADING THRESHOLD DETECTION" test_results/event_scaling_benchmark_output.txt

# Extract concurrency benchmark
grep -A 5 "CONCURRENCY BENCHMARK" test_results/event_scaling_benchmark_output.txt

# Extract batch vs single enqueue comparison
grep -A 5 "BATCH ENQUEUE vs SINGLE" test_results/event_scaling_benchmark_output.txt
```

**Example Output:**
```
===== BASIC HANDLER PERFORMANCE TEST =====
  Config: 3 types, 1 handlers per type, 10 deferred events
  Events/sec: 163436
  Time per event: 0.0061 ms

===== CONCURRENCY BENCHMARK =====
  Config: 23 threads, 173 events/thread = 3979 total deferred events
  Total time: 8.18 ms
  Events/sec: 486696

===== BATCH ENQUEUE vs SINGLE ENQUEUE BENCHMARK =====
  Single + alloc:     583,736 events/sec
  Single (no alloc): 1,805,799 events/sec
  Batch (no alloc):  7,857,195 events/sec
```

**Key Metrics:**
- **Events/sec per config:** Throughput at different handler/event counts
- **Concurrency throughput:** Multi-threaded event processing (23 threads)
- **Batch vs Single:** Enqueue method comparison (batch should be 5-10x faster)
- **Threading threshold:** WorkerBudget mode selection for event processing
- **Time per event:** Dispatch latency (should be <0.01ms)

#### Particle Manager Metrics
```bash
grep -E "Particles/frame:|Render Time:|Batch Count:" test_results/particle_benchmark/performance_metrics.txt
```

**Example Output:**
```
Particles/frame: 5000
Render Time: 3.2ms
Batch Count: 12
Culling Efficiency: 88%
```

#### GPU Frame Timing Metrics
```bash
grep -E "Avg frame time:|Avg swapchain:|Avg GPU upload:|Avg GPU submit:" test_results/gpu/gpu_frame_timing_benchmark_debug.txt
```

**Example Output:**
```
  Avg frame time:   8.343 ms
  Avg swapchain:    8.039 ms
  Avg GPU upload:   0.001 ms
  Avg GPU submit:   0.045 ms
```

**Key Metrics:**
- **Frame time:** Total CPU+GPU frame time (lower is better)
- **Swapchain:** VSync/present wait time (display-dependent)
- **GPU upload:** Vertex data upload overhead (should be <0.1ms)
- **GPU submit:** Draw call submission overhead (should be <0.5ms)

#### SIMD Performance Metrics
```bash
# Extract speedup factors
grep -E "Speedup:|SIMD Time:|Scalar Time:|Status:" test_results/simd_benchmark_current.txt

# Platform detection
grep -E "Detected SIMD:|Platform:" test_results/simd_benchmark_current.txt
```

**Example Output:**
```
=== AIManager Distance Calculation ===
Platform: NEON (ARM64)
SIMD Time:   12.345 ms
Scalar Time: 45.678 ms
Speedup:     3.70x
Status: PASS (SIMD faster than scalar)

=== ParticleManager Physics Update ===
Platform: NEON (ARM64)
SIMD Time:   10.234 ms
Scalar Time: 38.456 ms
Speedup:     3.76x
Status: PASS (SIMD faster than scalar)
```

**Key Metrics:**
- **Speedup factor:** Must be ≥1.0x (SIMD faster than scalar)
- **Platform:** SSE2/AVX2/NEON (should not be "Scalar (no SIMD)")
- **Status:** PASS/FAIL per operation

#### Integrated System Benchmark Metrics
```bash
# Extract frame statistics
grep -E "Average:|P95:|P99:|Frame drops|Max:" test_results/integrated_benchmark_current.txt

# Scaling summary
grep -A 15 "Scaling Summary" test_results/integrated_benchmark_current.txt

# Coordination overhead
grep -E "Coordination overhead:" test_results/integrated_benchmark_current.txt
```

**Example Output:**
```
=== Integrated System Load Benchmark ===
Frame Time Statistics:
  Average: 8.45ms ✓ (target < 16.67ms)
  P95: 12.32ms ✓ (target < 20ms)
  P99: 15.67ms ✓ (target < 25ms)
  Max: 18.45ms
  Min: 6.12ms
  Frame drops (>16.67ms): 12/600 (2.0%) ✓

=== Scaling Summary ===
Entities    Avg (ms)    P95 (ms)    Drops (%)   Status
1000        2.15        3.21        0.0         ✓ 60+ FPS
5000        5.82        8.45        1.2         ✓ 60+ FPS
10000       10.34       14.56       3.8         ✓ 60+ FPS
15000       16.23       22.34       8.5         ~ 40-60 FPS
20000       24.56       35.67       18.2        ✗ < 40 FPS

Maximum sustainable entity count @ 60 FPS: 10000

Coordination Overhead Analysis:
  Coordination overhead: 1.2ms (3.5%)
✓ PASS: Coordination overhead < 2ms
```

**Key Metrics:**
- **Average frame time:** Target <16.67ms (60 FPS)
- **P95 frame time:** Target <20ms
- **Frame drop %:** Target <5%
- **Max sustainable entities:** Highest count maintaining 60 FPS
- **Coordination overhead:** Target <2ms

#### Background Simulation Manager Metrics
```bash
# Extract scaling performance
grep -E "Entities|Avg \(ms\)|Threaded|Batches" test_results/bgsim_benchmark_current.txt

# Threading recommendation
grep -A 5 "THREADING RECOMMENDATION" test_results/bgsim_benchmark_current.txt

# Adaptive tuning summary
grep -A 10 "ADAPTIVE TUNING SUMMARY" test_results/bgsim_benchmark_current.txt
```

**Example Output (EDM Architecture - values are platform-specific):**
```
===== BACKGROUND SIMULATION SCALING TEST =====
    Entities       Avg (ms)       Min (ms)       Max (ms)    Threaded   Batches
         100          0.003          0.002          0.003          no         1
         500          0.011          0.011          0.011          no         1
        1000          0.022          0.020          0.029          no         1
        2500          0.051          0.050          0.055          no         1
        5000          0.115          0.111          0.131          no         1
        7500          0.169          0.151          0.228          no         1
       10000          0.204          0.200          0.212          no         1

=== THREADING RECOMMENDATION ===
Single throughput: 51733.15 items/ms
Multi throughput:  0.00 items/ms
Batch multiplier:  1.00

=== ADAPTIVE TUNING SUMMARY ===
Batch sizing:       PASS
```

**Note:** With EDM architecture, background simulation is so fast (~50K items/ms) that threading
overhead exceeds the benefit. Single-threaded processing handles 10K entities in ~0.2ms.

**Key Metrics:**
- **Update time:** Should scale sub-linearly with entity count
- **Threading mode:** Adaptive via WorkerBudget (may stay single-threaded if fast enough)
- **Batches:** WorkerBudget batch sizing effectiveness
- **Throughput:** Single throughput ~50K+ items/ms expected with EDM architecture

#### Adaptive Threading Analysis Metrics

**Note:** This benchmark validates WorkerBudgetManager adaptive logic across AI, Collision, Particle, and Event systems.

```bash
# Extract MIN_WORKLOAD enforcement results
grep -A 15 "MIN_WORKLOAD ENFORCEMENT" test_results/adaptive_threading_current.txt

# Extract per-system threshold learning
grep -E "Learned threshold:|Threshold active:" test_results/adaptive_threading_current.txt

# Extract hysteresis re-learning result
grep -A 5 "Hysteresis Band" test_results/adaptive_threading_current.txt

# Extract batch multiplier validation
grep -A 10 "BATCH MULTIPLIER TUNING" test_results/adaptive_threading_current.txt

# Extract final WBM state summary
grep -A 10 "WORKERBUDGET STATE SUMMARY" test_results/adaptive_threading_current.txt
```

**Example Output:**
```
===== MIN_WORKLOAD ENFORCEMENT (ALL SYSTEMS) =====
Testing MIN_WORKLOAD=100 enforcement:
  System      Workload  Expected  Actual    Result
  ----------  --------  --------  ------    ------
  AI                50    SINGLE  SINGLE    PASS
  AI                99    SINGLE  SINGLE    PASS
  Collision         50    SINGLE  SINGLE    PASS
  Collision         99    SINGLE  SINGLE    PASS
  Particle          50    SINGLE  SINGLE    PASS
  Particle          99    SINGLE  SINGLE    PASS
  Event             50    SINGLE  SINGLE    PASS
  Event             99    SINGLE  SINGLE    PASS
Validation: MIN_WORKLOAD enforcement: PASS

===== COLLISION MANAGER THRESHOLD LEARNING =====
  Threshold learned at frame 10: 2000 entities
  Final state:
    Learned threshold: 2000
    Threshold active: true

===== PARTICLE MANAGER THRESHOLD LEARNING =====
  Threshold learned at frame 70: 16750 particles
  Final state:
    Learned threshold: 16750
    Threshold active: true

===== HYSTERESIS BAND RE-LEARNING =====
  Learned threshold: 3000 (active)
  Hysteresis low boundary (95%): 2850
  After workload drop to 2840:
    Threshold: 0 (was 3000), Active: false, Decision: SINGLE
Validation: Re-learning triggered: PASS

===== BATCH MULTIPLIER TUNING =====
  System      Multiplier  InRange
  AI          1.000       PASS
  Collision   1.000       PASS
  Particle    1.000       PASS
  Event       1.000       PASS
Validation: All multipliers in range: PASS

===== WORKERBUDGET STATE SUMMARY (ALL SYSTEMS) =====
System       Threshold   Active    BatchMult   MultiTP
AI                   0    false         1.00         0
Collision            0    false         1.00      4294
Particle             0    false         1.00         0
Event                0    false         1.00         0
Pathfinding          0    false         1.00         0
```

**Key Metrics:**
- **MIN_WORKLOAD enforcement:** All 8 tests (4 systems × 2 workloads) must PASS
- **Threshold learning:** Collision typically learns at 2000 entities, Particle at ~16K-17K particles
- **AI threshold:** May not be learned if hardware is fast enough (0.9ms threshold not hit) — expected
- **Hysteresis:** Re-learning must trigger when workload drops below 95% band — PASS required
- **Batch multiplier:** All systems must be within [0.4, 2.0] range
- **Collision MultiTP:** Should be non-zero after test (typically 4000-5000 items/ms)

#### Projectile Scaling Metrics

```bash
# Extract entity scaling table
grep -A 10 "Projectile Entity Scaling" test_results/projectile_scaling_current.txt | \
  grep -E "^\s+[0-9]+|Projectiles"

# Extract scalability summary
grep -A 3 "SCALABILITY SUMMARY" test_results/projectile_scaling_current.txt

# Extract SIMD throughput table
grep -A 12 "SIMD 4-Wide Throughput" test_results/projectile_scaling_current.txt | \
  grep -E "^\s+[0-9]+|Projectiles"
```

**Example Output:**
```
--- Projectile Entity Scaling ---
 Projectiles   Time (ms)   Entities/ms   ns/entity   Threading   Status
         100       0.006         15903        62.9      single        OK
         500       0.029         17003        58.8      single        OK
        1000       0.058         17309        57.8      single        OK
        2000       0.115         17385        57.5      single        OK
        5000       0.286         17477        57.2      single        OK

SCALABILITY SUMMARY:
Best throughput: 17477 entities/ms (at 5000 projectiles)

--- SIMD 4-Wide Throughput ---
 Projectiles   Time (ms)   ns/entity  Throughput/ms
           4      0.0008       189.8            5269
          16      0.0019       118.5            8440
          64      0.0041        64.8           15435
         256      0.0153        59.7           16764
        1000      0.0581        58.1           17218
        4000      0.2288        57.2           17481
```

**Key Metrics:**
- **Peak throughput:** ~17K entities/ms (at 1000-5000 projectiles)
- **ns/entity:** Should converge to ~57-63ns at 1000+ projectiles (cache-warm regime)
- **SIMD efficiency:** Throughput should improve significantly from 4→64 projectiles (SIMD batching warmup), then plateau
- **Threading:** Typically all single-threaded (fast enough that threading overhead not beneficial)

### Step 4: Compare Against Baseline

**Comparison Algorithm:**

For each metric:
1. Read baseline value
2. Read current value
3. Calculate percentage change: `((current - baseline) / baseline) * 100`
4. Determine status:
   - **Regression:** Slower/worse performance
   - **Improvement:** Faster/better performance
   - **Stable:** Within noise threshold (±5%)

**Example Comparison:**

| System | Metric | Baseline | Current | Change | Status |
|--------|--------|----------|---------|--------|--------|
| AI | FPS | 62.3 | 56.8 | -8.8% | 🔴 Regression |
| AI | CPU% | 5.8% | 6.4% | +10.3% | 🔴 Regression |
| Collision | Checks/sec | 125000 | 134000 | +7.2% | 🟢 Improvement |
| Pathfinder | Calc Time | 8.5ms | 8.7ms | +2.4% | ⚪ Stable |

### Step 5: Flag Regressions

**Regression Severity Levels:**

#### 🔴 CRITICAL (Block Merge)
- AI System FPS drops below 60
- AI System CPU usage exceeds 8%
- Any performance metric degrades >15%
- Benchmark timeouts (infinite loops)

#### 🟠 WARNING (Review Required)
- Performance degradation 10-15%
- AI System FPS 60-65 (near threshold)
- Collision/Pathfinding >10% slower

#### 🟡 MINOR (Monitor)
- Performance degradation 5-10%
- Within acceptable variance but trending down

#### ⚪ STABLE (Acceptable)
- Performance change <5% (measurement noise)

#### 🟢 IMPROVEMENT
- Performance improvement >5%
- Successful optimization

**Regression Detection Logic:**

```python
def classify_change(metric_name, baseline, current, is_lower_better=False):
    change_pct = ((current - baseline) / baseline) * 100

    # Invert for metrics where lower is better (e.g., CPU%, time)
    if is_lower_better:
        change_pct = -change_pct

    # Critical thresholds for AI system (most important)
    if "AI" in metric_name or "FPS" in metric_name:
        if metric_name == "FPS" and current < 60:
            return "CRITICAL", "FPS below 60 threshold"
        if metric_name == "CPU" and current > 8:
            return "CRITICAL", "CPU exceeds 8% threshold"

    # General thresholds
    if change_pct < -15:
        return "CRITICAL", f"{abs(change_pct):.1f}% regression"
    elif change_pct < -10:
        return "WARNING", f"{abs(change_pct):.1f}% regression"
    elif change_pct < -5:
        return "MINOR", f"{abs(change_pct):.1f}% regression"
    elif change_pct > 5:
        return "IMPROVEMENT", f"{change_pct:.1f}% improvement"
    else:
        return "STABLE", f"{abs(change_pct):.1f}% variance (acceptable)"
```

**AI Benchmark Regression Detection Strategy:**

1. **Scaling Regression (Low Entity Counts):**
   - 100-500 entity performance regresses more than larger counts
   - **Root Cause:** Single-threaded path overhead, behavior initialization
   - **Action:** Profile single-threaded update path, check behavior creation costs
   - **Impact:** Small-world performance degraded

2. **Scaling Regression (High Entity Counts):**
   - 5000-10000 entity performance regresses
   - **Root Cause:** Threading overhead, batch processing, WorkerBudget tuning
   - **Action:** Profile ThreadSystem, check batch sizes, validate WorkerBudget
   - **Impact:** Large-world performance degraded

3. **Threading Speedup Degraded:**
   - Single-threaded time stable but multi-threaded time increases
   - **Root Cause:** Thread contention, lock overhead, false sharing
   - **Action:** Profile thread synchronization, check mutex usage
   - **Impact:** Threading efficiency degraded

4. **Uniform Regression:**
   - All entity counts regress proportionally
   - **Root Cause:** Core update loop changes, behavior execution overhead
   - **Action:** Profile behavior update(), check per-entity costs
   - **Impact:** System-wide performance degradation

**SIMD Benchmark Regression Detection:**

*Note: SIMD benchmarks should be run in Release mode for accurate results. Debug mode lacks
-O3 optimization, causing SIMD intrinsic overhead to exceed benefit (expected behavior).*

**Release Mode:**
- 🔴 CRITICAL: SIMD slower than scalar (speedup < 1.0x) for AI Distance or Particle Physics
- 🔴 CRITICAL: Platform shows "Scalar (no SIMD)" - SIMD not compiling correctly
- 🟠 WARNING: Speedup <2.0x for AI Distance (expect 3-4x)
- ⚪ STABLE: Speedup within ±20% of baseline

**Debug Mode:**
- ⚪ EXPECTED: SIMD may be slower than scalar (no optimization)
- 🔴 CRITICAL: Platform shows "Scalar (no SIMD)" - SIMD not compiling correctly

**Integrated System Regression Detection:**

*Note: Targets below are for Release builds. Debug builds have significant overhead;
expect ~5K max sustainable entities @ 60 FPS in Debug mode.*

**Release Mode:**
- 🔴 CRITICAL: Average frame time >16.67ms (below 60 FPS) at 10K entities
- 🔴 CRITICAL: Frame drop % >10% at standard load
- 🟠 WARNING: P95 >20ms or coordination overhead >2ms
- 🟠 WARNING: Max sustainable entities decreased >20%
- 🟡 MINOR: Sustained performance degradation >5% over 50s
- ⚪ STABLE: All metrics within targets

**Debug Mode:**
- ⚪ EXPECTED: ~5K max sustainable entities @ 60 FPS
- 🔴 CRITICAL: Coordination overhead >2ms (should still be low)
- 🟠 WARNING: Max sustainable entities <3K @ 60 FPS

**Background Simulation Regression Detection:**
- 🔴 CRITICAL: Update time regression >50% from baseline
- 🔴 CRITICAL: Adaptive tuning failing (batch sizing not converging)
- 🟠 WARNING: Throughput regression >25% from baseline
- 🟡 MINOR: Sub-linear scaling not maintained
- ⚪ STABLE: Performance within ±15% of baseline

**Note:** With EDM architecture, background simulation is extremely fast. Threading may not be
beneficial as single-threaded processing is often faster than threading overhead. WBM adapts correctly.

**Adaptive Threading Analysis Regression Detection:**

*Note: The benchmark now tests all 4 systems (AI, Collision, Particle, Event). AI threshold may not be
learned if single-threaded AI is fast enough that 0.9ms is never hit — this is expected behavior.*

- 🔴 CRITICAL: Any MIN_WORKLOAD test fails (expected: all 8/8 PASS for SINGLE below 100)
- 🔴 CRITICAL: Collision threshold not learned (expected: learned at frame ~10 for 2000 entities)
- 🔴 CRITICAL: Hysteresis re-learning not triggered (PASS expected when workload drops below 95% band)
- 🟠 WARNING: Batch multiplier outside valid range [0.4, 2.0]
- 🟠 WARNING: Particle threshold learned significantly later (>frame 200) suggesting performance regression
- ⚪ STABLE: All 8 MIN_WORKLOAD tests PASS, Collision/Particle thresholds learned, hysteresis working
- ⚪ EXPECTED: AI threshold not learned (hardware fast enough that 0.9ms not hit in benchmark)

**Projectile Scaling Regression Detection:**

- 🔴 CRITICAL: Peak throughput regression >15% from baseline (~17K entities/ms target)
- 🟠 WARNING: ns/entity at 1000+ projectiles regression >10% (expected ~57-63ns)
- 🟡 MINOR: SIMD warmup curve degraded (4→64 projectile improvement smaller than baseline)
- ⚪ STABLE: Throughput within ±10% of baseline, ns/entity consistent
- ⚪ EXPECTED: All single-threaded (threading overhead exceeds benefit at these counts)

### Step 6: Generate Report

**Report Structure:**

```markdown
# HammerEngine Performance Regression Report
**Date:** YYYY-MM-DD HH:MM:SS
**Branch:** <current-branch>
**Baseline:** <baseline-date or "New Baseline Created">
**Total Benchmark Time:** <duration>

---

## 🎯 Overall Status: <PASSED/FAILED/WARNING>

<summary-of-regressions>

---

## 📊 Performance Summary

### AI System - Entity Scaling (EDM Architecture)

**Purpose:** Tests AIManager performance with production behaviors

| Entities | Baseline | Current | Change | Threading | Status |
|----------|----------|---------|--------|-----------|--------|
| 100 | 5.0M/s | 4.8M/s | -4.0% | single | ⚪ Stable |
| 500 | 7.8M/s | 7.7M/s | -1.3% | single | ⚪ Stable |
| 1000 | 11.0M/s | 10.8M/s | -1.8% | single | ⚪ Stable |
| 2000 | 15.0M/s | 14.0M/s | -6.7% | single | 🟡 Minor |
| 5000 | 100M/s | 100M/s | +0.0% | multi | ⚪ Stable |
| 10000 | 200M/s | 207M/s | +3.5% | multi | 🟢 Improved |

**Status:** ⚪ **STABLE**
- All metrics within acceptable variance
- EDM architecture achieves ~200M+ updates/sec at 10K entities
- Threading beneficial above ~5K entities

**Threading Mode Comparison:**
| Entities | Single (ms) | Multi (ms) | Speedup |
|----------|-------------|------------|---------|
| 500 | 0.10 | 0.10 | 1.0x |
| 1000 | 0.18 | 0.17 | 1.1x |
| 2000 | 0.35 | 0.34 | 1.0x |
| 5000 | 0.86 | 0.30 | 2.9x |

**Note:** With EDM architecture, single-threaded processing is so fast that threading
overhead only becomes beneficial at higher entity counts (~5K+).

---

### Collision Scaling System

| Scenario | Baseline (ms) | Current (ms) | Change | Throughput | Status |
|----------|---------------|--------------|--------|------------|--------|
| MM 1000 movables | 0.25 | 0.22 | -12% | 4596/ms | 🟢 Improvement |
| MM 5000 movables | 1.20 | 1.11 | -8% | 4509/ms | 🟢 Improvement |
| MM 10000 movables | 2.50 | 2.26 | -10% | 4434/ms | 🟢 Improvement |
| MS 10K statics | 0.16 | 0.15 | -6% | FLAT | ⚪ Stable |
| MS 20K statics | 0.16 | 0.15 | -6% | FLAT | ⚪ Stable |
| Combined XL (6K) | 0.70 | 0.65 | -7% | N/A | 🟢 Improvement |
| Combined XXL (12K) | 1.40 | 1.31 | -6% | N/A | 🟢 Improvement |

**Status:** 🟢 **IMPROVEMENT**
- SAP (Sweep-and-Prune) for MM: O(n log n) scaling confirmed up to 10K movables
- Spatial Hash for MS: O(n) scaling confirmed - time stays FLAT from 100 to 20K statics
- Combined: Sub-quadratic scaling verified up to 12K entities

---

### Trigger Detection System (EventOnly Triggers)

**Purpose:** Tests detection of EventOnly triggers (water, area markers, etc.) by entities with NEEDS_TRIGGER_DETECTION flag.

| Detectors | Triggers | Baseline (ms) | Current (ms) | Change | Method | Status |
|-----------|----------|---------------|--------------|--------|--------|--------|
| 1 (Player) | 100 | 0.15 | 0.14 | -7% | spatial | ⚪ Stable |
| 1 (Player) | 400 | 0.15 | 0.14 | -7% | spatial | ⚪ Stable |
| 10 (NPCs) | 200 | 0.16 | 0.15 | -6% | spatial | ⚪ Stable |
| 25 (NPCs) | 200 | 0.16 | 0.15 | -6% | spatial | ⚪ Stable |
| 50 (threshold) | 200 | 0.25 | 0.23 | -8% | sweep | ⚪ Stable |
| 100 (NPCs) | 200 | 0.28 | 0.26 | -7% | sweep | ⚪ Stable |
| 200 (NPCs) | 400 | 0.45 | 0.43 | -4% | sweep | ⚪ Stable |

**Status:** ⚪ **STABLE**
- Adaptive strategy working correctly (spatial <50, sweep >=50)
- Performance within targets (<0.5ms for typical scenarios)
- Flag-based filtering eliminates unnecessary AABB tests

**Notes:**
- Only entities with NEEDS_TRIGGER_DETECTION flag are processed
- Player has flag by default; NPCs can opt-in via setTriggerDetection(true)
- Replaced O(movables × triggers) brute-force with adaptive O(N × k) or O((N+T) log (N+T))

---

### Pathfinding System **[ALWAYS INCLUDE - CRITICAL]**

**⚠️ IMPORTANT:** This section is MANDATORY in all regression reports. Pathfinding performance directly impacts integrated AI benchmarks.

| Distance (units) | Baseline Time | Current Time | Change | Path Nodes | Success Rate | Status |
|------------------|---------------|--------------|--------|------------|--------------|--------|
| 50 (Short) | 0.048 ms | 0.024 ms | -50.0% | 1 | 100% | 🟢 Major Improvement |
| 400 (Medium) | 0.259 ms | 0.049 ms | -81.1% | 3 | 100% | 🟢 Major Improvement |
| 2000 (Long) | 0.502 ms | 0.052 ms | -89.6% | 6 | 100% | 🟢 Major Improvement |
| 4000 (Very Long) | 0.756 ms | 0.128 ms | -83.1% | 10 | 100% | 🟢 Major Improvement |
| 8000 (Extreme) | N/A | 0.349 ms | N/A | 20 | 100% | 🟢 Excellent |

**Status:** [Determine based on actual results]
- Path calculation performance across all distance ranges
- Success rate (must be 100% - failures are critical regressions)
- Path quality (nodes explored should be reasonable)
- A* algorithm and cache effectiveness

**Template Notes:**
- Always show ALL distance ranges (50, 400, 2000, 4000, 8000 units)
- Include success rate for each distance (failures = critical regression)
- Note path quality (average nodes should be optimal)
- Highlight major improvements or regressions
- Cross-reference with integrated AI benchmark if pathfinding impacts it

---

### Event Manager

**Deferred Event Throughput (single enqueue + FIFO drain):**

| Config | Baseline (ev/s) | Current (ev/s) | Change | Status |
|--------|-----------------|-----------------|--------|--------|
| 10 events, 1 handler | 163K | 163K | 0% | ⚪ Stable |
| 50 events, 3 handlers | 145K | 145K | 0% | ⚪ Stable |
| 100 events, 4 handlers | 189K | 189K | 0% | ⚪ Stable |
| 200 events, 5 handlers | 270K | 270K | 0% | ⚪ Stable |

**Concurrency & Enqueue Methods:**

| Metric | Baseline | Current | Change | Status |
|--------|----------|---------|--------|--------|
| Concurrent (23 threads, 4K events) | 487K ev/s | 487K ev/s | 0% | ⚪ Stable |
| Batch enqueue (no alloc) | 7.9M ev/s | 7.9M ev/s | 0% | ⚪ Stable |
| Single enqueue (no alloc) | 1.8M ev/s | 1.8M ev/s | 0% | ⚪ Stable |
| Threading threshold | Single preferred | Single preferred | - | ⚪ Stable |

**Status:** ⚪ **STABLE**
- Batch enqueue 4-5x faster than single enqueue (lock reduction)
- WorkerBudget correctly prefers single-threaded at all tested counts

---

### Particle Manager

| Metric | Baseline | Current | Change | Status |
|--------|----------|---------|--------|--------|
| Particles/frame | 5000 | 5000 | 0.0% | ⚪ Stable |
| Render Time | 3.2ms | 3.1ms | -3.1% | ⚪ Stable |
| Batch Count | 12 | 11 | -8.3% | 🟢 Improvement |
| Culling Efficiency | 88% | 90% | +2.3% | 🟢 Improvement |

**Status:** 🟢 **IMPROVEMENT**
- Better batching efficiency
- Improved culling

---

### GPU Frame Timing System

**Purpose:** Tests GPU rendering pipeline performance (Vulkan/SPIR-V)

| Metric | Baseline | Current | Change | Status |
|--------|----------|---------|--------|--------|
| Avg Frame Time | 8.343ms | 8.343ms | 0.0% | ⚪ Stable |
| Avg Swapchain | 8.039ms | 8.039ms | 0.0% | ⚪ Stable |
| Avg GPU Upload | 0.001ms | 0.001ms | 0.0% | ⚪ Stable |
| Avg GPU Submit | 0.045ms | 0.045ms | 0.0% | ⚪ Stable |

**Status:** ⚪ **STABLE**
**Note:** Run from desktop session for meaningful VSync timings.

---

### SIMD Performance System

**Purpose:** Validates cross-platform SIMD optimizations deliver claimed speedups

| Operation | Platform | SIMD (ms) | Scalar (ms) | Speedup | Status |
|-----------|----------|-----------|-------------|---------|--------|
| AI Distance | NEON | 12.3 | 45.7 | 3.71x | 🟢 Excellent |
| Collision Bounds | NEON | 8.5 | 9.2 | 1.08x | ⚪ Stable |
| Layer Mask Filter | NEON | 5.1 | 4.8 | 0.94x | ⚪ Compiler auto-vec |
| Particle Physics | NEON | 10.2 | 38.4 | 3.76x | 🟢 Excellent |

**Status:** 🟢 **OPERATIONAL**
- SIMD detected and active (not scalar fallback)
- Key operations (AI Distance, Particle Physics) showing 3-4x speedups
- Auto-vectorization competitive for simple patterns (bounds, layer mask)

---

### Integrated System Performance

**Purpose:** Tests all managers under combined realistic load at 60 FPS target

| Scenario | Entities | Avg (ms) | P95 (ms) | Drops % | Status |
|----------|----------|----------|----------|---------|--------|
| Realistic (10K AI + 5K particles) | 15K | 10.34 | 14.56 | 3.8% | ⚪ Stable |
| Scaling 1K | 1K | 2.15 | 3.21 | 0.0% | ⚪ Stable |
| Scaling 5K | 5K | 5.82 | 8.45 | 1.2% | ⚪ Stable |
| Scaling 10K | 10K | 10.34 | 14.56 | 3.8% | ⚪ Stable |
| Scaling 15K | 15K | 16.23 | 22.34 | 8.5% | 🟡 MINOR |
| Scaling 20K | 20K | 24.56 | 35.67 | 18.2% | 🟠 WARNING |

**Max Sustainable @ 60 FPS:** 10,000 entities
**Coordination Overhead:** 1.2ms (< 2ms target) ✓
**Sustained Performance:** <5% degradation over 50s ✓

---

### Background Simulation System

**Purpose:** Tests background tier entity processing for tier-culled entities

| Entities | Baseline (ms) | Current (ms) | Change | Threaded | Batches | Status |
|----------|---------------|--------------|--------|----------|---------|--------|
| 100 | 0.003 | 0.003 | 0% | no | 1 | ⚪ Stable |
| 500 | 0.011 | 0.011 | 0% | no | 1 | ⚪ Stable |
| 1000 | 0.022 | 0.022 | 0% | no | 1 | ⚪ Stable |
| 5000 | 0.115 | 0.115 | 0% | no | 1 | ⚪ Stable |
| 10000 | 0.204 | 0.204 | 0% | no | 1 | ⚪ Stable |

**Status:** ⚪ **STABLE**
- Single-threaded throughput: ~50K items/ms (threading overhead not beneficial)
- Sub-linear scaling maintained
- WorkerBudget adaptive tuning: PASS

---

### Adaptive Threading Analysis (WorkerBudget Validation)

**Purpose:** Validates WorkerBudgetManager adaptive logic using Collision system

**Throughput Learning:**
| Metric | Baseline | Current | Change | Status |
|--------|----------|---------|--------|--------|
| Single TP (items/ms) | 588 | 617 | +5% | ⚪ Stable |
| Multi TP (items/ms) | 5018 | 4970 | -1% | ⚪ Stable |
| Batch Multiplier | 1.00 | 1.00 | 0% | ⚪ Stable |
| Multi Speedup | 8.5x | 8.0x | -6% | ⚪ Stable |

**Mode Switching Validation:**
| Test | Expected | Actual | Status |
|------|----------|--------|--------|
| 50 entities (below MIN_WORKLOAD=100) | SINGLE | SINGLE | ✓ PASS |
| 99 entities (below MIN_WORKLOAD=100) | SINGLE | SINGLE | ✓ PASS |
| 2000 entities (high count) | MULTI | MULTI | ✓ PASS |
| Bidirectional switching | Scale up→MULTI, down→SINGLE | Correct | ✓ PASS |

**Gradual Scale Down (Natural Crossover Detection):**
| Entities | Mode | Single TP | Multi TP | Ratio |
|----------|------|-----------|----------|-------|
| 1500 | MULTI | 2538 | 4541 | 1.79x |
| 500 | MULTI | 2278 | 4739 | 2.08x |
| 200 | MULTI | 2067 | 3384 | 1.64x |
| 125 | MULTI | 1883 | 2173 | 1.15x |
| 100 | SINGLE | 3591 | 1596 | 0.44x |

**Natural Crossover Point:** At MIN_WORKLOAD boundary (100 entities) - MULTI preferred above

**Status:** ⚪ **STABLE**
- Throughput learning: PASS (non-zero values after 1000 frames)
- Mode selection: PASS (correct mode based on throughput comparison)
- Batch multiplier: PASS (within [0.4, 2.0] range, stabilized)
- Bidirectional switching: PASS (SINGLE→MULTI on scale up, MULTI→SINGLE on scale down)

---

## 🚨 Critical Issues (BLOCKING)

1. **AI System FPS Below Threshold**
   - Current: 56.8 FPS (Target: 60+)
   - Regression: -8.8%
   - **Action Required:** Must fix before merge

---

## ⚠️ Warnings (Review Required)

1. **AI System CPU Usage Increase**
   - Current: 6.4% (Target: <6%)
   - Regression: +10.3%

2. **AI Update Time Increase**
   - Current: 13.9ms (Baseline: 12.4ms)
   - Regression: +12.1%

---

## 📈 Improvements

1. **Collision System Performance**
   - Query time improved 12.5%
   - Collision checks/sec improved 7.2%

2. **Particle Manager Batching**
   - Batch count reduced 8.3% (better efficiency)
   - Culling efficiency improved 2.3%

---

## 🔍 Detailed Analysis

### Performance Hotspots (if callgrind data available)

<parse callgrind reports from test_results/valgrind/callgrind/>

Top Functions by Time:
1. AIManager::updateBehaviors - 45% (up from 38% - REGRESSION)
2. CollisionManager::detectCollisions - 18% (down from 22% - IMPROVEMENT)
3. PathfinderManager::calculatePath - 12% (stable)

---

## 📋 Recommendations

### Immediate Actions (Critical)
1. Investigate AI System performance regression
2. Profile AIManager::updateBehaviors with valgrind/callgrind
3. Review commits since baseline for AI changes
4. Do not merge until FPS ≥60 restored

### Short-term Actions (Warnings)
1. Monitor Event Manager dispatch latency
2. Consider AI batch size optimization
3. Review recent AI behavior changes

### Long-term Actions (Optimization)
1. Apply collision system improvements to other managers
2. Document particle manager batching technique
3. Consider updating baseline after AI fixes validated

---

## 📁 Files

**Baseline:** `$PROJECT_ROOT/test_results/baseline/*.txt`
**Current Results:** `$PROJECT_ROOT/test_results/*/performance_metrics.txt`
**Callgrind Reports:** `$PROJECT_ROOT/test_results/valgrind/callgrind/` (if available)
**Full Report:** `$PROJECT_ROOT/test_results/regression_reports/regression_YYYY-MM-DD.md`

---

## ✅ Next Steps

- [ ] Fix AI System FPS regression (BLOCKING)
- [ ] Verify fixes with re-run: `claude run benchmark regression check`
- [ ] Update baseline after validation: `claude update performance baseline`
- [ ] Document optimization techniques from collision improvements

---

**Generated by:** hammer-benchmark-regression Skill
**Report saved to:** $PROJECT_ROOT/test_results/regression_reports/regression_YYYY-MM-DD.md
```

**Console Summary:**
```
=== Performance Regression Check ===

Status: 🔴 REGRESSION DETECTED (BLOCKING)

Critical Issues:
  🔴 AI System FPS: 56.8 (target: 60+) - 8.8% regression

Warnings:
  🟠 AI CPU Usage: 6.4% (target: <6%) - 10.3% increase
  🟡 Event Dispatch Latency: +8.3%

Improvements:
  🟢 Collision Query Time: -12.5%
  🟢 Particle Batching: -8.3%

Total Benchmark Time: 21m 15s

❌ DO NOT MERGE - Fix AI regression first

Full Report: $PROJECT_ROOT/test_results/regression_reports/regression_2025-01-15.md
```

## Storage & Baseline Management

### Baseline Update Command
```bash
# After validating improvements, update baseline (requires PROJECT_ROOT)
cp "$PROJECT_ROOT/test_results/"*/performance_metrics.txt "$PROJECT_ROOT/test_results/baseline/"
echo "Baseline updated: $(date)" > "$PROJECT_ROOT/test_results/baseline/baseline_date.txt"
```

### Baseline History
Keep historical baselines for long-term tracking:
```
$PROJECT_ROOT/test_results/baseline_history/
├── 2025-01-01_baseline/
├── 2025-01-15_baseline/
└── 2025-02-01_baseline/
```

## Integration with Development Workflow

**Use this Skill:**
- Before merging feature branches
- After performance optimizations (verify improvement)
- Weekly during active development
- Before releases (ensure no regressions)
- When modifying AI, collision, pathfinding, or particle systems

## Final Report Validation Checklist

**⚠️ MANDATORY: Verify BEFORE submitting report to user**

Before finalizing any regression report, confirm ALL of the following:

### Benchmark Execution
- [ ] All 11 benchmarks completed successfully (no timeouts/crashes)
- [ ] AI Scaling: Entity scaling results present with updates/sec metrics
- [ ] **Pathfinding: Async throughput metrics extracted** ← CRITICAL!
- [ ] Collision: SAP/Hash timing and trigger detection data extracted
- [ ] Event Manager: Throughput data extracted
- [ ] Particle Manager: Update timing data extracted
- [ ] GPU Frame Timing: Frame time, swapchain, upload, submit metrics extracted
- [ ] SIMD Performance: Platform detection and speedup data extracted
- [ ] Integrated System: Frame statistics, scaling, coordination overhead extracted
- [ ] Background Simulation: Scaling and adaptive tuning summary extracted
- [ ] Adaptive Threading: MIN_WORKLOAD enforcement (8/8 PASS), threshold learning, hysteresis extracted
- [ ] Projectile Scaling: Entity throughput (entities/ms) and SIMD 4-wide curve extracted

### Report Completeness
- [ ] **Pathfinding System section included in report** ← DO NOT SKIP!
- [ ] AI System: Entity scaling and threading comparison sections present
- [ ] Collision System: Performance table present
- [ ] Event Manager: Metrics table present
- [ ] Particle Manager: Performance data present
- [ ] GPU Frame Timing: Performance data present
- [ ] SIMD System: Platform and speedup table present
- [ ] Integrated System: Frame statistics, scaling summary, coordination overhead present
- [ ] Background Simulation: Scaling table and threading data present
- [ ] Adaptive Threading: MIN_WORKLOAD results, threshold learning per system, hysteresis present
- [ ] Projectile Scaling: Entity scaling table and SIMD 4-wide throughput table present
- [ ] Overall status determined (PASSED/WARNING/FAILED)
- [ ] Regression/improvement analysis complete

### Critical Pathfinding Verification
- [ ] Pathfinding metrics extracted from `test_results/pathfinder_benchmark_current.txt`
- [ ] All 5 distance ranges present (50, 400, 2000, 4000, 8000 units)
- [ ] Success rates reported (must be 100%)
- [ ] Performance comparison against baseline completed
- [ ] Pathfinding section visible in final report

**If ANY checklist item is unchecked, DO NOT submit the report. Extract missing data first.**

## Exit Codes

- **0:** All benchmarks passed, no regressions
- **1:** Critical regressions detected (BLOCKING)
- **2:** Warnings detected (review required)
- **3:** Benchmark failed to run (timeout/crash)
- **4:** Baseline creation mode (informational)

## Usage Examples

When the user says:
- "check for performance regressions"
- "run benchmarks"
- "test performance"
- "verify no performance degradation"
- "compare against baseline"

Activate this Skill automatically.

## Performance Expectations

- **Full benchmark suite:** ~27 minutes
  - AI Scaling: ~3 minutes
  - Collision: ~2 minutes
  - Pathfinder: ~5 minutes
  - Event Manager: ~2 minutes
  - Particle Manager: ~2 minutes
  - GPU Frame Timing: ~1 minute
  - SIMD Performance: ~1 minute
  - Integrated System: ~3 minutes
  - Background Simulation: ~2 minutes
  - Adaptive Threading: ~2 minutes
  - Projectile Scaling: ~1 minute
- **Report generation:** 2-3 minutes
- **Total:** ~29-30 minutes for complete analysis

## Troubleshooting

**Benchmark timeouts:**
- Possible infinite loop or catastrophic performance regression
- Run individual benchmark with debugging: `gdb ./bin/debug/ai_system_benchmark`

**Inconsistent results:**
- System load affecting benchmarks
- Re-run benchmarks in clean environment
- Close other applications
- Check for thermal throttling

**No baseline found:**
- Skill will create baseline from current run
- Subsequent runs will compare against this baseline
- Update baseline after validating improvements
