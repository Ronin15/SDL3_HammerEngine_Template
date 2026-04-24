---
name: voidlight-architecture-guard
description: Review or fix VoidLight-Framework changes for architectural coherence, with emphasis on EDM ownership, manager vs controller boundaries, state-transition cleanup, event contracts, and world-lifecycle correctness. Use when the user wants an architecture check, wants to prevent subsystem drift, or wants help enforcing where new gameplay features should live.
---

# VoidLight Architecture Guard

## Use This Skill For

- Architecture checks before or after gameplay and engine changes.
- Enforcing EDM, manager, controller, render, and GameState ownership rules.
- Catching subsystem drift from incomplete feature cycles.
- Narrow fixes where code is locally correct but violates project boundaries.

## Workflow

1. Read repo guidance first.
   - Start with `AGENTS.md`.
   - Treat EDM, AI, controller, render, threading, and transition rules as binding.

2. Trace the real runtime path.
   - Identify the participating systems at runtime, not just the touched file.
   - Follow init, enter, update, render, event dispatch, transition, cleanup, and shutdown when relevant.

3. Classify each touched concern by owner.
   - Persistent per-entity cross-frame state belongs in EDM.
   - Behavior, scheduling, caches, registries, and orchestration belong in managers.
   - State-scoped feature flow belongs in controllers.
   - Render controllers read canonical state and must not become lifecycle owners.
   - Game states coordinate manager/controller setup and teardown order.

4. Check for common drift.
   - Controller directly mutates AI behavior or memory state in EDM.
   - Manager-local cache or spatial index outlives the world or state it describes.
   - Cross-frame gameplay state exists only in manager-local scratch.
   - Cleanup is split across the wrong owner or only runs on one transition path.
   - Event-driven UI or gameplay flow mutates state without emitting the contract other systems rely on.
   - Fix introduces a second source of truth instead of using the canonical owner.

5. Prefer the narrowest owner-correct fix.
   - Keep the feature in the current subsystem if its role is correct.
   - Move only the ownership-violating mutation, cache, or cleanup step.
   - Reuse existing owner paths such as behavior helpers, manager cleanup APIs, and existing event contracts.

6. Verify with targeted tests.
   - Add or update the narrowest tests that prove the ownership contract and runtime behavior.
   - Prefer direct test executables over broad scripts.

## Enforcement Rules

- Do not move feature orchestration out of a state-scoped controller just because it touches multiple systems.
- Do move direct state mutation if it crosses an established owner boundary.
- Do not broaden EDM into a policy layer.
- Do not leave world or state teardown implicit when a manager owns caches or registries.
- Do not accept fixes that bypass required events, transition ordering, or cleanup hooks.

## Findings / Fix Output

- State whether the code is conforming, an acceptable exception, or real drift.
- For drift, name the correct owner and the smallest safe repair.
- Cite concrete file paths and lines when possible.
- End with what you verified and any remaining coverage gap.

## Reference

- For the recurring checks and repo-specific drift patterns, read [references/checklist.md](references/checklist.md).
