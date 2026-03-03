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
5. Run the narrowest performance-relevant workload:
- For subsystem changes, run the most relevant direct test executable(s).
- For gameplay/render regressions, run the main binary and gather frame metrics.
- For threading regressions, compare sequential vs threaded behavior where applicable.
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

## Reporting Format

Report findings in this order:

1. Verdict: `regression`, `no regression`, or `inconclusive`.
2. Scope: workload, binary/test executable, renderer path, build type.
3. Comparison: baseline SHA vs current SHA.
4. Evidence: run counts, per-run metrics, mean, standard deviation, and percent delta.
5. Suspected cause: file/function level hypothesis, marked as `confirmed` or `inferred`.
6. Next action: exact fix or additional measurement needed.

If unable to run a required command, state what failed, why, and the minimal missing prerequisite.
