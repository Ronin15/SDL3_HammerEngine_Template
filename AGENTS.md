# AGENTS.md — VoidLight-Framework

Codex instructions for this repository. Match existing subsystem patterns, fix root causes, and keep changes aligned with production architecture.

## Instruction Order

Apply guidance in this order:

1. Explicit user instructions
2. The nearest `AGENTS.md` or `AGENTS.override.md`
3. Existing local subsystem patterns
4. General style preferences

- This is repo-level guidance. Narrower nested `AGENTS.md` files override it for their subtree.
- Keep this file durable, concise, and repo-specific. Move subsystem-only detail into nested agent files when needed.
- If the user names a specific file, work in that file only unless they approve spillover.

## Project Stance

- Performance-oriented, data-oriented, and minimal abstraction overhead.
- Prioritize memory safety, cache efficiency, low latency, minimal allocations, and deterministic behavior.

## Workflow

- Read the exact code path before editing.
- Search the same subsystem for the established pattern before inventing one.
- Do not assume system ownership, hot paths, or participating managers. Trace them in code first.
- Prefer targeted fixes over cleanup or opportunistic refactors.
- Do not add compatibility overloads, ad-hoc safety layers, or new abstractions unless the task requires them.
- Keep production and test updates in the same change when behavior changes.
- Before finishing, run the most targeted build or test feasible and state exactly what you verified.

## Fast Commands

Build:

```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_SDL3_GPU=ON && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3_GPU=ON && ninja -C build
```

Sanitizers:

- ASan and TSan are mutually exclusive.
- Remove `build/CMakeCache.txt` when switching sanitizers or major build options.

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
./bin/debug/VoidLight_Template
./bin/release/VoidLight_Template
```

Tests:

```bash
./bin/debug/<test_executable>
./bin/debug/<test_executable> --list_content
./bin/debug/<test_executable> --run_test="TestCase*"
./bin/debug/entity_data_manager_tests
./bin/debug/ai_manager_edm_integration_tests
./bin/debug/behavior_functionality_tests --run_test="FleeFromAttacker*"
```

Slow scripts:

```bash
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
./tests/test_scripts/run_controller_tests.sh --verbose
```

Boost.Test notes:

- Test names use the `BOOST_AUTO_TEST_CASE` name directly.
- Suite prefixes are optional.
- Use `--list_content` to confirm the exact test name before filtering.

See `tests/TESTING.md` for broader test documentation.

## Repo Map

- Source: `src/{core,managers,controllers,gameStates,entities,events,ai,collisions,utils,world,gpu}`
- Headers mirror source under `include/`
- Other important dirs: `tests/`, `res/`, `res/shaders/`
- Dependency direction: `Core -> Managers -> GameStates -> Entities/Controllers`
- Common architectural anchors: `GameEngine`, `ThreadSystem`, `EntityDataManager`, `AIManager`, `EventManager`, `ControllerRegistry`, `WorldRenderPipeline`, `GPURenderer`

## Core Rules

### C++ and APIs

- C++20, 4-space indent, Allman braces.
- Prefer RAII, smart pointers, forward declarations, and non-trivial logic in `.cpp`.
- Use `const T&` for read-only non-trivial inputs, `T&` for mutation, and value for primitives.
- Prefer `std::span`, `std::string_view`, `std::optional`, and explicit read/mutate APIs.
- Use `const std::string&` for map lookups. Avoid `string_view -> string` churn.
- Avoid raw arrays, nullable pointer-return accessors, long-lived raw pointers, and new legacy compatibility overloads. Materialize raw pointers only at the final C API boundary.
- Use `std::format()` for logs. Never concatenate log strings with `+`. Use `AI_INFO_IF(cond, msg)` when only logging is conditional.
- Use `VOIDLIGHT_DEBUG_ONLY(...)` for debug-only code. Do not use raw `#ifdef DEBUG`.
- Remove unused parameter names entirely, for example `void foo(float)`. Do not use `(void)param`, commented names, or `[[maybe_unused]]` except on empty virtual base defaults.
- Check important `[[nodiscard]]` bool returns such as `init()`, `load()`, and `create()`. Use `BOOST_REQUIRE()` in tests.
- Preserve the project copyright header:

```cpp
/* Copyright (c) 2025 Hammer Forged Games ... MIT License */
```

### Performance and Threading

- Avoid per-frame allocations. Reuse member buffers, call `reserve()` when size is known, preserve capacity with `clear()`, and prefer reusable ref-based buffer APIs over return-by-value patterns.
- Keep reusable scratch state thread-local when used from worker code.
- Main thread owns SDL events and rendering. Worker threads process batches only.
- Use `ThreadSystem`, not raw threads.
- Use `WorkerBudget` to decide threading and batch sizing, and report execution after work completes.
- Futures must complete before dependent operations.
- Avoid non-`thread_local` static state in threaded code.
- Align hot atomics with `alignas(64)` when contention matters.
- Use `include/utils/SIMDMath.hpp` for SIMD work. Process 4 elements per iteration plus a scalar tail.

### EDM, AI, and Controllers

- `EntityDataManager` is storage only. AI decision logic belongs in `Behaviors::` and `BehaviorExecutors`.
- `EDM::recordCombatEvent()` records stats and memory only; emotion math belongs in behavior helpers.
- `Behaviors::processWitnessedCombat()` owns witnessed-combat emotion and memory handling.
- Emotional contagion runs in the main-thread pre-pass in `AIManager::update()`.
- Cross-frame state such as paths and timers belongs in EDM, not local temporaries.
- Controllers must never mutate AI behavior state directly in EDM.
- Use `Behaviors::queueBehaviorMessage()` from the main thread and `Behaviors::deferBehaviorMessage()` from worker threads.
- `switchBehavior()` clears behavior data before `init()`. Set new behavior state after the switch, not before.
- EDM render data stores manager-owned texture handles as `std::shared_ptr<SDL_Texture>`. Call `.get()` only at the final draw site and avoid copying `shared_ptr` in visible-entity loops.

### State Transitions and Events

- Call `prepareForStateTransition()` on active managers before cleanup.
- In AI-heavy states, clean up in this order when initialized: `AIManager`, `ProjectileManager`, `BackgroundSimulationManager`, `WorldResourceManager`, `EventManager`, `CollisionManager`, `PathfinderManager`, `EntityDataManager`, `WorkerBudgetManager`, `ParticleManager`.
- Demo states may skip managers they never initialized.
- `ControllerRegistry::clear()` must be called in `GamePlayState::exit()`, not just `unsubscribeAll()`.
- `EventManager` supports persistent and transient handlers. Persistent manager-level handlers register in `init()` with `registerPersistentHandler[WithToken]()` and survive transitions. State-level handlers register in `enter()` with `registerHandler[WithToken]()` and are cleared by `clearTransientHandlers()`. `clearAllHandlers()` is for shutdown only.
- Collision-to-`EventManager` bridge callbacks stay persistent across transitions.
- No game state should register collision callbacks directly.
- Use deferred transitions: set intent in `enter()`, then transition in `update()`.

### Rendering, UI, and GameState

- Exactly one present per frame. `GameEngine::render()` performs scene and UI rendering; `GameEngine::present()` performs the actual present. Never call `SDL_RenderClear` or `SDL_RenderPresent` inside a game state.
- SDL renderer path uses `WorldRenderPipeline` for `prepareChunks`, `beginScene`, `renderWorld`, and `endScene`.
- GPU flow is scene pass, composite to swapchain, then UI pass. States implement `renderGPUScene()` and `renderGPUUI()`.
- When both SDL and GPU consume atlas-backed EDM render data, GPU atlas interpretation is authoritative.
- For SDL3 GPU UI text, use `TTF_GetGPUTextDrawData()` only. Do not add UV flips, half-texel offsets, or shader hacks. Snap integer UI text placement to whole pixels before emitting vertices.
- Trace camera updates, interpolation, rounding, sub-pixel offsets, and draw submission before proposing jitter or flicker fixes. Do not apply speculative fixes for jitter, shimmer, or flicker.
- `DayNightController` requires `update(dt)` every frame. The GPU path already feeds it through `GPURenderer::setDayNightParams()`.
- Use `LoadingState` plus async `ThreadSystem` work for loading instead of blocking manual rendering.
- Call `setComponentPositioning()` after creating UI components. Prefer existing UI helpers such as `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, and `createCenteredDialog()`.
- Use `mp_stateManager->changeState()` for transitions. `GameEngine::Instance()` remains valid for non-transition engine access.
- Prefer local references over cached manager or controller members. Add controllers with `m_controllers.add<T>()` in `enter()` and do not keep cached `mp_*Ctrl` members.

### Tests, Debugging, and Traps

- Prefer direct test executables over slow wrapper scripts.
- Never relax test expectations to hide a production bug unless the user explicitly asks.
- For `EventManager` regressions, first distinguish missing state-owned handler wiring in tests from a production defect.
- Delete dead code and unused parameters. Do not comment them out.
- `FrameProfiler` uses `F3`. Prefer RAII timers: `ScopedPhaseTimer`, `ScopedManagerTimer`, `ScopedRenderTimer`. Profiling is a no-op in release builds; hitch detection starts above 20 ms.
- Demo states are test and showcase code. `GamePlayState` stays production-clean.
- Use `SettingsMenuState` and `MainMenuState` as menu references.
- `WorldResourceManager` is a spatial index over EDM, not a quantity store.
- Do not wire `subscribeWorldEvents()` in `init()`. Deferred `WorldManager` events can fire after a new world is populated.
