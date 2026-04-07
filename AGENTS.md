# AGENTS.md

Codex CLI instructions for `SDL3_VoidLight-Framework_Template`.

## Mission

- Match existing subsystem patterns before changing code.
- Fix root causes in production code.
- Preserve architecture, performance, threading, and rendering behavior.
- Prefer minimal direct fixes over new abstractions.
- Keep production code and tests aligned in the same change.
- When the user names a specific file, work on exactly that file unless they explicitly approve spillover.

## Working Order

Follow this priority when guidance conflicts:

1. Explicit user instructions
2. This `AGENTS.md`
3. Existing local subsystem patterns
4. General style preferences

Before editing:

- Read the exact code path first.
- Search for matching patterns in the same subsystem.
- Never assume an implied system, owner, or hot path. Trace the actual participating systems and verify with code, tests, or benchmarks before making architecture or performance recommendations.
- Prefer targeted edits over cleanup or opportunistic refactors.

While editing:

- Use established helpers and systems.
- Do not add ad-hoc implementations if the repo already has a pattern.
- Do not add compatibility overloads, extra safety layers, or new abstractions unless the task requires them.
- When standardizing drifting systems, unify data layout and draw semantics first.
- When tightening APIs, complete the production and test migration in the same change.

Before finishing:

- Run the most targeted build or test that matches the change when feasible.
- Prefer direct test executables over slow wrapper scripts.
- State exactly what you verified and what you did not.

## Fast Commands

Build:

```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_SDL3_GPU=ON && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3_GPU=ON && ninja -C build
```

Sanitizers:

```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build
export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"
```

Reconfigure:

```bash
rm build/CMakeCache.txt && cmake -B build/ ...
```

Run:

```bash
./bin/debug/SDL3_Template
```

Tests:

```bash
./bin/debug/<test_executable>
./bin/debug/<test_executable> --list_content
./bin/debug/<test_executable> --run_test="TestCase*"
./bin/debug/entity_data_manager_tests
./bin/debug/ai_manager_edm_integration_tests
```

Slow comprehensive scripts:

```bash
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
./tests/test_scripts/run_controller_tests.sh --verbose
```

Boost.Test notes:

- Test names use the `BOOST_AUTO_TEST_CASE` name directly.
- Example: `ThreadingModeComparison`, not `TestThreadingModeComparison`.
- Suite prefix is optional.
- Use `--list_content` to confirm exact names.

See `tests/TESTING.md` for deeper test docs.

## Repo Map

- Source layout: `src/{core,managers,controllers,gameStates,entities,events,ai,collisions,utils,world,gpu}`
- Headers mirror source under `include/`
- Other important dirs: `tests/`, `res/`, `res/shaders/`
- Dependency direction: `Core -> Managers -> GameStates -> Entities/Controllers`

Key systems:

- Core: `GameEngine`, `ThreadSystem`, `Logger`, `TimestepManager`
- Managers: `EntityDataManager`, `AIManager`, `EventManager`, `CollisionManager`, `ParticleManager`, `PathfinderManager`, `WorldManager`, `WorldResourceManager`, `BackgroundSimulationManager`, `UIManager`, `GameTimeManager`, `InputManager`, `TextureManager`, `FontManager`, `SoundManager`
- AI: `AIBehavior` base with Idle, Wander, Patrol, Chase, Flee, Follow, Guard, Attack
- Controllers are state-scoped via `ControllerRegistry`
- GPU path: `GPUDevice`, `GPURenderer`, `GPUShaderManager`, `SpriteBatch`, `GPUVertexPool`, `GPUSceneRecorder`

## Core Coding Rules

- C++20, 4-space indent, Allman braces.
- Use RAII and smart pointers.
- Use `ThreadSystem`, not raw threads.
- Prefer STL algorithms where reasonable.
- Use `const T&` for read-only non-trivial inputs, `T&` for mutation, and value for primitives.
- Use `const std::string&` for map lookups. Avoid `string_view -> string` churn.
- Prefer `std::span`, `std::string_view`, `std::optional`, and explicit read/mutate APIs.
- Avoid raw arrays, raw pointer escape paths, nullable pointer-return accessors, and new legacy compatibility overloads.
- Stored raw pointers are not acceptable for ownership or long-lived cached state. Materialize raw pointers only at the final C API submission boundary.
- Naming: UpperCamelCase for types, lowerCamelCase for functions and variables, `m_` and `mp_` members, `ALL_CAPS` constants.
- Use `.hpp` for headers. Keep non-trivial logic in `.cpp`.
- Use `std::format()` for logs. Never concatenate log strings with `+`. Use `AI_INFO_IF(cond, msg)` when only logging is conditional.
- Copyright header:

```cpp
/* Copyright (c) 2025 Hammer Forged Games ... MIT License */
```

## Performance Rules

- Avoid per-frame allocations.
- Reuse member buffers and preserve capacity with `clear()`.
- Always `reserve()` when size is known.
- Use thread-local storage for RNG, reusable buffers, and spatial caches.
- Prefer ref-based APIs for reusable buffers over return-by-value patterns that force allocations.

## Threading and State Invariants

- Main thread owns SDL events and rendering.
- Worker threads process batches only.
- Avoid non-`thread_local` static state in threaded code.
- Futures must complete before dependent operations.
- Cache-line align hot atomics with `alignas(64)`.
- `ThreadSystem` uses `hardware_concurrency - 1` workers and priority levels from Critical to Idle.
- Use `enqueueTaskWithResult()` for future-based work and `batchEnqueueTasks()` for bulk submission.
- `WorkerBudget` rules:
  - `shouldUseThreading()` decides whether to thread.
  - `getBatchStrategy()` sizes batches.
  - `reportExecution()` feeds throughput tracking.

Canonical manager threading pattern:

```cpp
auto decision = budgetMgr.shouldUseThreading(SystemType::AI, count);
if (decision.shouldThread) {
    for (size_t i = 0; i < decision.batchCount; ++i) {
        m_futures.push_back(threadSystem.enqueueTaskWithResult([...] { processBatch(...); }));
    }
    for (auto& f : m_futures) { f.get(); }
}
budgetMgr.reportExecution(SystemType::AI, count, decision.shouldThread, decision.batchCount, elapsedMs);
```

State transition rules:

- Call `prepareForStateTransition()` on active managers before cleanup.
- Current AI-heavy cleanup order when initialized:
  - `AIManager`
  - `BackgroundSimulationManager`
  - `WorldResourceManager`
  - `EventManager`
  - `CollisionManager`
  - `PathfinderManager`
  - `EntityDataManager`
  - `WorkerBudgetManager`
  - `ParticleManager`
- Demo states may skip managers they do not initialize.

## EDM, AI, and Controller Boundaries

- `EntityDataManager` is pure data storage.
- AI decision logic belongs in `Behaviors::` in `BehaviorExecutors.hpp/.cpp`.
- `EDM::recordCombatEvent()` records stats and memory only, not emotion math.
- `Behaviors::processCombatEvent()` applies personality-scaled emotion changes around the EDM call.
- `Behaviors::processWitnessedCombat()` handles distance falloff, composure-scaled emotion changes, and memory via `EDM::addMemory()`.
- Emotional contagion runs in a main-thread pre-pass in `AIManager::update()`.
- Behaviors use pre-fetched `ctx.behaviorData` and `ctx.pathData` from `processBatch()`.

Controller boundary:

- Controllers must never directly mutate AI behavior state in EDM.
- Use `Behaviors::queueBehaviorMessage(idx, BehaviorMessage::X)` from the main thread.
- Use `Behaviors::deferBehaviorMessage()` from worker threads.

Cross-frame data:

- Data that must survive between frames, including paths and timers, belongs in EDM, not local variables.

Render ownership:

- EDM render data stores manager-owned texture handles, not raw `SDL_Texture*`.
- `TextureManager` remains the owner/cache.
- EDM retains `std::shared_ptr<SDL_Texture>` handles.
- Call `.get()` only at the final SDL draw site.
- Do not copy `shared_ptr` in visible-entity loops.

## Rendering Rules

- Exactly one present per frame.
- `GameEngine::render()` handles scene and UI rendering.
- `GameEngine::present()` performs the actual present/end-frame step.
- Never call `SDL_RenderClear` or `SDL_RenderPresent` inside game states.

SDL renderer path:

- `WorldRenderPipeline` owns the 4-phase flow: `prepareChunks`, `beginScene`, `renderWorld`, `endScene`.
- It wraps `SceneRenderer` for pixel-perfect zoom and sub-pixel scrolling.

GPU path:

- Flow is scene pass, composite to swapchain, then UI pass.
- Game states implement `renderGPUScene()` and `renderGPUUI()`.
- The engine ends the frame outside the state.
- When SDL and GPU paths both consume atlas-based EDM render data, GPU atlas interpretation is the standard. SDL source rects must match GPU atlas offsets and frame stepping.

SDL3 GPU UI text:

- Use `TTF_GetGPUTextDrawData()` only.
- Do not add UV flips, half-texel offsets, or shader hacks.
- For integer UI layouts, snap final text placement to whole pixels before emitting GPU vertices.

Related systems:

- `DayNightController` requires `update(dt)` every frame for 30-second lighting interpolation transitions.
- GPU path already updates this through `GPURenderer::setDayNightParams()`.
- Use `LoadingState` with async `ThreadSystem` work, not blocking manual rendering.
- Use deferred transitions: set a flag in `enter()`, then transition in `update()`.

Rendering bug workflow:

- Trace camera update, interpolation, floor/round behavior, sub-pixel offset, and draw submission before proposing a fix.
- Do not apply speculative fixes for jitter, shimmer, or flickering.

## UI and GameState Rules

- Always call `setComponentPositioning()` after creating UI components.
- Common helpers: `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, `createCenteredDialog()`.
- Supported positioning modes: `ABSOLUTE`, `CENTERED_H`, `CENTERED_V`, `CENTERED_BOTH`, `TOP_ALIGNED`, `TOP_RIGHT`, `BOTTOM_ALIGNED`, `BOTTOM_CENTERED`, `BOTTOM_RIGHT`, `LEFT_ALIGNED`, `RIGHT_ALIGNED`.

GameState architecture:

- Use `mp_stateManager->changeState()` for transitions.
- `GameEngine::Instance()` remains valid for non-transition engine access such as pause, window sizing, or shutdown.
- Use local references, not cached member pointers, for managers and controllers.
- Cache at function top only when reused multiple times.
- Add controllers with `m_controllers.add<T>()` in `enter()`.
- Do not keep cached `mp_*Ctrl` controller members.
- Use lazy caching for enum-to-string conversion and compute static layout positions in `enter()` when possible.

## Bug-Fix Rules

- Fix root causes in production code.
- Never bypass failing tests by changing expectations unless explicitly asked.
- For `EventManager` regressions, first distinguish missing state-owned handler wiring in tests from an actual production bug.
- Delete dead code and unused parameters entirely. Do not comment them out.

## Repo-Specific Traps

- Demo states are for testing and showcasing features.
- File and class names do not always match runtime state names.
- `EventDemoState` registers as `EventDemo`.
- `UIDemoState.hpp` defines `UIExampleState`.
- `GamePlayState` is the pristine official gameplay state. Keep it clean and production-ready.
- Use `SettingsMenuState` and `MainMenuState` as menu references.

## Task Checklists

Rendering changes:

- Trace the full render path before editing.
- Preserve one-present-per-frame behavior.
- Do not move clear/present work into game states.

AI or EDM changes:

- Keep EDM as storage only.
- Keep decision logic in `Behaviors::`.
- Store cross-frame state in EDM.

Threading changes:

- Use `ThreadSystem` and `WorkerBudget`.
- Ensure futures complete before dependent work.
- Avoid shared non-thread-local static state.

UI or GameState changes:

- Use existing UI helpers and positioning rules.
- Add controllers in `enter()`.
- Use deferred state transitions when needed.

Test updates:

- Prefer direct test executables.
- Confirm exact Boost.Test names with `--list_content` when needed.
- Keep test updates aligned with the production change.
