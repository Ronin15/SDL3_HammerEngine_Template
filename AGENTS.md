# AGENTS.md

Codex CLI instructions for `SDL3_HammerEngine_Template`.

## Mission

- Match existing patterns before changing code.
- Fix root causes in production code.
- Preserve architecture, performance, and rendering behavior.
- Prefer minimal, direct fixes over new abstractions.
- Keep production code and tests aligned in the same change.
- When the user names a specific file, work on exactly that file.

## Codex Workflow

Before editing:

- Read the relevant code path first.
- Search for existing patterns in the same subsystem.
- Prefer targeted edits over broad cleanup.

While editing:

- Use established systems and helpers. Do not add ad-hoc implementations if a pattern already exists.
- Do not add unnecessary abstractions, compatibility overloads, or safety machinery beyond the task.
- When standardizing drifting systems, unify data layout and draw semantics first.
- When tightening APIs, finish the migration in production code and tests in the same change.

Before finishing:

- Run the most targeted build or test that matches the change when feasible.
- Prefer direct test executables over slow wrapper scripts.
- State what was verified and what was not.

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

- Main source layout: `src/{core,managers,controllers,gameStates,entities,events,ai,collisions,utils,world,gpu}`
- Headers mirror source under `include/`
- Other important dirs: `tests/`, `res/`, `res/shaders/`
- Dependency direction: `Core -> Managers -> GameStates -> Entities/Controllers`

Key systems:

- Core: `GameEngine`, `ThreadSystem`, `Logger`, `TimestepManager`
- Managers: `EntityDataManager`, `AIManager`, `EventManager`, `CollisionManager`, `ParticleManager`, `PathfinderManager`, `WorldManager`, `WorldResourceManager`, `BackgroundSimulationManager`, `UIManager`, `GameTimeManager`, `InputManager`, `TextureManager`, `FontManager`, `SoundManager`
- AI: `AIBehavior` base with Idle, Wander, Patrol, Chase, Flee, Follow, Guard, Attack
- Controllers are state-scoped via `ControllerRegistry`
- GPU path: `GPUDevice`, `GPURenderer`, `GPUShaderManager`, `SpriteBatch`, `GPUVertexPool`, `GPUSceneRenderer`

## Coding Rules

- C++20, 4-space indent, Allman braces.
- Use RAII and smart pointers.
- Use `ThreadSystem`, not raw threads.
- Prefer STL algorithms where reasonable.
- Use `const T&` for read-only non-trivial inputs, `T&` for mutation, value for primitives.
- Use `const std::string&` for map lookups. Avoid `string_view -> string` churn.
- Prefer `std::span`, `std::string_view`, `std::optional`, and explicit read/mutate APIs.
- Avoid raw arrays, raw pointer escape paths, nullable pointer-return accessors, and new compatibility overloads for legacy raw-pointer access.
- Stored raw pointers are not acceptable for ownership or long-lived cached state. If a C API needs a raw pointer, materialize it only at the final submission boundary.
- Naming: UpperCamelCase for types, lowerCamelCase for functions and variables, `m_` and `mp_` members, `ALL_CAPS` constants.
- Use `.hpp` for C++ headers. Keep non-trivial logic in `.cpp`.
- Use `std::format()` for logs. Never concatenate log strings with `+`. Use `AI_INFO_IF(cond, msg)` when only logging is conditional.
- Copyright header:

```cpp
/* Copyright (c) 2025 Hammer Forged Games ... MIT License */
```

## Performance and Memory

- Avoid per-frame allocations.
- Reuse member buffers and preserve capacity with `clear()`.
- Always `reserve()` when size is known.
- Use thread-local storage for RNG, reusable buffers, and spatial caches.
- Prefer ref-based APIs for reusable buffers. Avoid return-by-value patterns that force repeated allocations.

## Threading and State Invariants

- Main thread owns SDL events and rendering.
- Worker threads process batches only.
- Avoid non-`thread_local` static state in threaded code.
- Futures must complete before dependent operations.
- Cache-line align hot atomics with `alignas(64)`.
- `ThreadSystem` uses `hardware_concurrency - 1` workers and priority levels from Critical to Idle.
- Use `enqueueTaskWithResult()` for future-based work and `batchEnqueueTasks()` for bulk submission.
- `WorkerBudget`: `shouldUseThreading()` decides, `getBatchStrategy()` sizes batches, `reportExecution()` feeds throughput tracking.

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

State transitions:

- Call `prepareForStateTransition()` on active managers before cleanup.
- Current AI-heavy cleanup order when initialized: `AIManager`, `BackgroundSimulationManager`, `WorldResourceManager`, `EventManager`, `CollisionManager`, `PathfinderManager`, `EntityDataManager`, `WorkerBudgetManager`, `ParticleManager`.
- Some demo states skip managers they do not initialize.

## EDM and AI Rules

- `EntityDataManager` is pure data storage. AI decision logic belongs in `Behaviors::` in `BehaviorExecutors.hpp/.cpp`.
- `EDM::recordCombatEvent()` records stats and memory only, not emotion math.
- `Behaviors::processCombatEvent()` applies personality-scaled emotion changes around the EDM call.
- `Behaviors::processWitnessedCombat()` handles distance falloff, composure-modulated emotions, and memory via `EDM::addMemory()`.
- Emotional contagion runs in a main-thread pre-pass in `AIManager::update()`.
- Behaviors use `ctx.behaviorData` and `ctx.pathData`, both pre-fetched in `processBatch()`.

Controller boundary:

- Controllers must never directly mutate AI behavior state in EDM.
- Use `Behaviors::queueBehaviorMessage(idx, BehaviorMessage::X)` from the main thread.
- Use `Behaviors::deferBehaviorMessage()` from worker threads.

Cross-frame data:

- Data that must survive between frames, including paths and timers, must live in EDM, not local variables.

Render ownership:

- EDM render data stores manager-owned texture handles, not raw `SDL_Texture*`.
- `TextureManager` stays the owner/cache.
- EDM retains `std::shared_ptr<SDL_Texture>` handles.
- Call `.get()` only at the final SDL draw site.
- In visible-entity loops, do not copy `shared_ptr`.

## Rendering Rules

- Exactly one present per frame.
- `GameEngine::render()` handles scene and UI rendering.
- `GameEngine::present()` performs the actual present/end-frame step.
- Never call `SDL_RenderClear` or `SDL_RenderPresent` inside game states.

SDL renderer path:

- `WorldRenderPipeline` owns the 4-phase flow: `prepareChunks`, `beginScene`, `renderWorld`, `endScene`.
- It wraps `SceneRenderer` for pixel-perfect zoom and sub-pixel scrolling.

GPU path:

- Flow is scene pass, composite to swapchain, UI pass.
- Game states implement `renderGPUScene()` and `renderGPUUI()`.
- The engine ends the frame outside the state.
- When SDL and GPU paths both consume atlas-based EDM render data, GPU atlas interpretation is the standard. SDL source rects must match GPU atlas offsets and frame stepping.

SDL3 GPU UI text:

- Use `TTF_GetGPUTextDrawData()` only.
- Do not add UV flips, half-texel offsets, or shader hacks.
- For integer UI layouts, snap final text placement to whole pixels before emitting GPU vertices.

Related systems:

- `DayNightController` requires `update(dt)` every frame for 30-second lighting interpolation transitions.
- GPU path updates this automatically via `GPURenderer::setDayNightParams()`.
- Use `LoadingState` with async `ThreadSystem` work, not blocking manual rendering.
- Use deferred transitions: set a flag in `enter()`, transition in `update()`.

## UI and GameState Rules

- Always call `setComponentPositioning()` after creating UI components.
- Common helpers: `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, `createCenteredDialog()`.
- Supported positioning modes: `ABSOLUTE`, `CENTERED_H`, `CENTERED_V`, `CENTERED_BOTH`, `TOP_ALIGNED`, `TOP_RIGHT`, `BOTTOM_ALIGNED`, `BOTTOM_CENTERED`, `BOTTOM_RIGHT`, `LEFT_ALIGNED`, `RIGHT_ALIGNED`.

GameState architecture:

- Use `mp_stateManager->changeState()` for transitions.
- `GameEngine::Instance()` is still valid for non-transition engine access such as pause, window sizing, or shutdown.
- Use local references, not cached member pointers, for managers and controllers.
- Cache at function top only if used multiple times.
- Add controllers with `m_controllers.add<T>()` in `enter()`.
- Do not keep cached `mp_*Ctrl` controller members.
- Use lazy caching for enum-to-string conversion and compute static layout positions in `enter()` when possible.

## Repo-Specific Traps

- Demo states are for testing/showcasing features.
- File/class names do not always match runtime state names.
- Example: `EventDemoState` registers as `EventDemo`.
- Example: `UIDemoState.hpp` defines `UIExampleState`.
- `GamePlayState` is the pristine official gameplay state. Keep it clean and production-ready.
- Use `SettingsMenuState` and `MainMenuState` as menu references.

## Bug Fixing

- Fix root causes in production code.
- Never bypass failing tests by changing test expectations unless explicitly asked.
- For `EventManager` regressions, first distinguish missing state-owned handler wiring in tests from an actual production bug.
- Delete dead code and unused parameters entirely. Do not comment them out.

Rendering bug workflow:

- Trace camera update, interpolation, floor/round behavior, sub-pixel offset, and draw submission before proposing a fix.
- Do not apply speculative fixes for jitter, shimmer, or flickering.

## References

- Deeper test docs: `tests/TESTING.md`
