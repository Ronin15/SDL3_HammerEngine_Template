---
name: quality-check
description: Check C++ code quality in this repository using project standards and static analysis workflows. Use when asked to review code quality, run static analysis, or validate changes with cppcheck/clang-tidy, especially for current branch changes.
---

# Quality Check

## Use This Workflow

1. Read repository standards in `AGENTS.md` before running checks or judging findings.
2. Read static-analysis instructions in:
- `tests/cppcheck/README.md`
- `tests/clang-tidy/README.md`
3. Determine scope from git state:
- Prefer changed files for quick feedback.
- Expand to full analysis when requested or when focused runs are inconclusive.
4. Run an explicit AGENTS standards pass using the checklist in this skill.
5. Run static analysis from the documented scripts:
- `tests/cppcheck/cppcheck_focused.sh` for fast local checks.
- `tests/cppcheck/run_cppcheck.sh` for full cppcheck pass.
- `tests/clang-tidy/clang_tidy_focused.sh` for changed-file clang-tidy.
- `tests/clang-tidy/run_clang_tidy.sh` for full clang-tidy.
6. If static-analysis output indicates likely runtime behavior risks, run the most relevant direct test executables from `bin/debug/` (prefer direct execution over wrapper scripts).
7. Report results in severity order with file and line references, then list commands run and any skipped checks.

## AGENTS Standards Checks

Check code against `AGENTS.md` sections: `Standards`, `Memory`, `EDM (EntityDataManager) Patterns`, `Rendering`, `GameState Architecture`, `Workflow`, and `Bug Fixing`.

Flag violations for:
- C++ style and API usage: C++20, Allman braces, parameter passing rules, naming, header/source split, `std::format()` for logging, STL algorithms preference where appropriate.
- Threading model: main-thread-only SDL usage, ThreadSystem usage over raw threads, futures completed before dependent operations, no unsafe static usage in threaded code.
- Memory/performance: avoid per-frame allocations, preserve vector capacity with `clear()`, `reserve()` when sizes are known.
- EDM boundaries: keep EDM as data storage; behavior logic in AI layer; no controller direct mutation of AI behavior state in EDM.
- Rendering pipeline: single `Present()` per frame, no ad-hoc clear/present in GameStates, follow renderer/GPU pipeline contracts.
- GameState patterns: use `mp_stateManager->changeState()`, local manager/controller references, avoid cached controller member pointers.
- Bug-fix discipline: fix root causes in production code, do not mask failures by changing test expectations, remove dead code instead of commenting out.

## Reporting Format

1. Findings first, sorted by severity (`high`, `medium`, `low`).
2. For each finding, include:
- Rule/check name (if available)
- File path and line
- Why it matters (bug/race/perf/maintainability)
- Minimal fix direction
3. If no findings are present, explicitly state that and include residual risk:
- tools not run
- tool/environment limitations
- analysis scope limits (focused vs full)
4. Include an `AGENTS Standards Coverage` section listing:
- Which checklist areas were reviewed
- Any violations found (or `none`)
- Any area not reviewed and why

## Constraints

- Follow `AGENTS.md` architecture and coding standards when proposing fixes.
- Do not downgrade or ignore warnings without reason.
- Do not change tests to hide production issues.
- Keep recommendations concrete and directly actionable.

## Trigger Examples

- "Check the code quality of the current code."
- "Run cppcheck and clang-tidy on my branch and summarize issues."
- "Do a static-analysis review using this repo's standards."
- "Quality-check recent C++ changes and call out risky patterns."
