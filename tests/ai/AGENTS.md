# AGENTS.md - AI Tests

These instructions apply to tests under `tests/ai/`. Follow the root
`AGENTS.md` first, then use this file for AI-specific test guidance.

## Test Focus

- Prefer tests that prove observable AI behavior and subsystem contracts:
  command-bus handoff, behavior transitions, state persistence, cache
  invalidation, and worker/main-thread boundaries.
- Keep tests flexible for future AI development. Avoid locking down incidental
  implementation details such as exact helper names, temporary buffer layouts,
  or behavior-internal branch structure.
- When a behavior bug is reported, trace the runtime path first: behavior
  executor, `AIManager` assignment/commit path, EDM state/config, and any
  authored data that affects the behavior.

## Test Execution

- Prefer direct Boost test executables over broad scripts.
- Use `--list_content` before adding or relying on a focused `--run_test`
  filter.
- Keep fixtures explicit about required manager initialization order,
  especially `ThreadSystem`, `PathfinderManager`, `CollisionManager`,
  `EntityDataManager`, and `AIManager`.

## Regression Coverage

- Cover both the queued/deferred command path and the committed runtime state
  when behavior execution crosses thread or entity boundaries.
- For cache and reusable-buffer behavior, assert externally visible results and
  reset/invalidation behavior rather than private storage details.
- Do not relax expectations to hide a production behavior issue. If a test
  setup is missing production wiring, fix the setup or state that limitation
  clearly.
