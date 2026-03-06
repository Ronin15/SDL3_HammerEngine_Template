---
name: performance-regression-check
description: Detect, reproduce, and validate runtime performance regressions in this SDL3 HammerEngine C++ repository. Use when asked to compare branch performance, investigate FPS/frame-time slowdowns, identify regressions after gameplay/AI/rendering/threading changes, or provide evidence-backed performance review before merge.
---

# Performance Regression Check

Run a consistent, reproducible regression workflow. Prioritize branch-vs-baseline comparisons under identical build and runtime conditions, then report measured deltas with enough detail that another engineer can reproduce the result.

## Workflow

1. Read `AGENTS.md` first and follow repository constraints:
- Prefer direct test executable runs from `bin/debug/`.
- Prefer focused checks before broad suite runs.
- Keep changes minimal and avoid ad-hoc tooling when existing scripts already exist.
2. Determine comparison targets:
- `current`: working tree/branch under review.
- `baseline`: usually `main` or the commit immediately before the suspect change.
- Record both commit SHAs before measuring.
3. Normalize environment for both targets:
- Use the same build type and renderer path (`USE_SDL3_GPU` on/off).
- Avoid background workloads and keep process launch conditions consistent.
- Use multiple runs (minimum 5) and keep run count identical for baseline/current.
4. Build each target with identical flags:
```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
```
Do not switch to Release for benchmark comparison in this repository. Baselines are debug-path and comparisons must stay debug-path.
5. Run benchmarks in **sequential full-suite mode by default**:
- Execute all 10 baseline-mapped benchmark suites, one at a time (never in parallel).
- Do not start the next suite until the prior suite has fully finished.
- Do not run release benchmarks unless explicitly requested.
- If user asks for a targeted check, you may run a subset, but clearly label it as partial coverage.
6. Collect evidence per run:
- Command executed.
- Wall-clock timing (`/usr/bin/time -v` when useful).
- In-engine profiling evidence when available (FrameProfiler/F3 for frame and manager phases).
- Repeat count and aggregate summary using arithmetic mean (required), plus standard deviation.
7. Aggregate and compare:
- Use one warm-up run not included in statistics.
- Compute mean and standard deviation for baseline and current.
- Compute percent delta from means: `(current_mean - baseline_mean) / baseline_mean * 100`.
- Treat one-off spikes as noise unless reflected in the mean across repeated runs.
8. Analyze and classify:
- `regression`: mean slowdown >= 3% and reproducible across repeated runs.
- `no regression`: mean delta within +/-3% or change not reproducible.
- `inconclusive`: variance too high or workload mismatch.
9. If regression is confirmed, localize quickly:
- Compare suspect files and hot paths (AI batching, render phases, allocation churn, synchronization points).
- Propose or implement minimal fix, then rerun the same measurement protocol.

## Command Patterns

List candidate tests:
```bash
./bin/debug/<test_executable> --list_content
```

Run targeted tests:
```bash
./bin/debug/<test_executable>
./bin/debug/<test_executable> --run_test="CaseName*"
```

Repeat run pattern (one warm-up + 5 measured runs):
```bash
./bin/debug/<test_executable> >/tmp/<name>_warmup.log 2>&1
for i in {1..5}; do /usr/bin/time -f "%e" ./bin/debug/<test_executable> >/tmp/<name>_$i.log 2>/tmp/<name>_$i.time; done
```

## Baseline Parsing (This Repo)

Use the baseline bundle at `test_results/baseline/` as the source of truth for established benchmark snapshots.

Current baseline file-to-script mapping:
- `adaptive_threading_analysis_current.txt` -> `tests/test_scripts/run_adaptive_threading_analysis.sh`
- `ai_scaling_current.txt` -> `tests/test_scripts/run_ai_benchmark.sh`
- `background_simulation_benchmark_current.txt` -> `tests/test_scripts/run_background_simulation_manager_benchmark.sh`
- `collision_scaling_current.txt` -> `tests/test_scripts/run_collision_scaling_benchmark.sh`
- `event_scaling_benchmark_output.txt` -> `tests/test_scripts/run_event_scaling_benchmark.sh`
- `integrated_benchmark_baseline.txt` -> `tests/test_scripts/run_integrated_benchmark.sh`
- `particle_benchmark_performance_metrics.txt` -> `tests/test_scripts/run_particle_manager_benchmark.sh`
- `pathfinder_benchmark_results.txt` -> `tests/test_scripts/run_pathfinder_benchmark.sh`
- `simd_benchmark_baseline.txt` -> `tests/test_scripts/run_simd_benchmark.sh`
- `ui_stress_baseline.log` -> `tests/test_scripts/run_ui_stress_tests.sh`

## Unified Full-Suite Execution (Default)

Run all suites in this exact order to minimize resource contention:

1. `tests/test_scripts/run_adaptive_threading_analysis.sh`
2. `tests/test_scripts/run_ai_benchmark.sh --debug`
3. `tests/test_scripts/run_background_simulation_manager_benchmark.sh`
4. `tests/test_scripts/run_collision_scaling_benchmark.sh`
5. `tests/test_scripts/run_event_scaling_benchmark.sh`
6. `tests/test_scripts/run_integrated_benchmark.sh`
7. `tests/test_scripts/run_particle_manager_benchmark.sh`
8. `tests/test_scripts/run_pathfinder_benchmark.sh`
9. `tests/test_scripts/run_simd_benchmark.sh`
10. `tests/test_scripts/run_ui_stress_tests.sh`

Per suite protocol (required):
- 1 warm-up run (not counted)
- 5 measured runs
- `/usr/bin/time -f "%e"` capture per measured run wall-clock
- Sequential execution only (no parallelization)

Canonical suite metric for unified report:
- `adaptive`: Particle learned threshold
- `ai`: Entity updates/sec @ 10k
- `background`: 5000-entity average update time (ms)
- `collision`: MM scaling time at 5000 movables (ms)
- `event`: maximum `Total time` in run output (ms)
- `integrated`: scaling average frame time at 5000 entities (ms)
- `particle`: `HighCountBench target=50000 update_avg_ms`
- `pathfinder`: first `Pathfinder Summary - RPS`
- `simd`: AI distance calculation speedup factor
- `ui`: average iteration time (ms)

## Reporting Format

Report findings in this order:

1. Verdict: `regression`, `no regression`, or `inconclusive`.
2. Scope: workload, binary/test executable, renderer path, build type.
3. Comparison: baseline SHA vs current SHA.
4. Evidence: run counts, per-run metrics, mean, standard deviation, and percent delta.
5. Suspected cause: file/function level hypothesis, marked as `confirmed` or `inferred`.
6. Next action: exact fix or additional measurement needed.

### Formatting Requirements (Mandatory)

- Prefer fixed-width plain text tables (inside code fences) for terminal compatibility.
- If markdown tables are used, also include the fixed-width plain text fallback.
- Include units in metric labels: `(ms)`, `(%)`, `(s)`, etc.
- Keep precision consistent:
- `mean`, `stddev`, and raw run times: 3 decimals for seconds, 4 decimals for milliseconds.
- `delta`: 2 decimals with explicit sign (`+` or `-`), e.g. `+3.70%`.
- Include the exact run protocol in one line: `warm-up + N measured runs`.
- Include per-run values in a dedicated table row (comma-separated) so reproducibility is visible.
- Add a `status` column for each metric:
- `regressed` when slowdown >= 3% and reproducible.
- `within_noise` when delta is between `-3%` and `+3%`.
- `improved` when speedup <= -3%.
- `high_variance` when stddev is large enough to undermine confidence.

### Output Template (Use Exactly)

1. Verdict: `regression|no regression|inconclusive`
2. Scope block (Debug, sequential full-suite, warm-up + 5)
3. Comparison block (baseline SHA, current SHA)
4. Unified evidence table:

```text
suite       metric                               baseline   runs                  mean      stddev    delta      status
----------  -----------------------------------  ---------  --------------------  --------  --------  ---------  ------------
adaptive    ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
ai          ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
background  ...                                  ...        r1,r2,r3,r4,r5        ...       ...       -...%      ...
collision   ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
event       ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
integrated  ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
particle    ...                                  ...        r1,r2,r3,r4,r5        ...       ...       -...%      ...
pathfinder  ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
simd        ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
ui          ...                                  ...        r1,r2,r3,r4,r5        ...       ...       +...%      ...
```

5. Suite runtime table:

```text
suite       runs (s)                 mean_s    stddev_s
----------  -----------------------  --------  --------
adaptive    r1,r2,r3,r4,r5           ...       ...
...
ui          r1,r2,r3,r4,r5           ...       ...
```

6. Suspected cause block (`confirmed|inferred`)
7. Next action block

If unable to run a required command, state what failed, why, and the minimal missing prerequisite.
