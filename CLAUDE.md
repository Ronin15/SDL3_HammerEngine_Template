# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Debug build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Release build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Run the engine
./bin/debug/SDL3_Template
./bin/release/SDL3_Template

# Reconfigure (required when switching sanitizers or build options)
rm build/CMakeCache.txt && cmake -B build/ ...
```

**Sanitizers** (mutually exclusive, require CMakeCache removal to switch):
```bash
# AddressSanitizer
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build

# ThreadSanitizer
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build
export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"
```

## Testing

Uses **Boost.Test** framework with 69 test executables. See `tests/TESTING.md` for comprehensive documentation.

```bash
# Direct test execution (PREFERRED for development - fast feedback)
./bin/debug/<test_executable>                        # Run all tests in executable
./bin/debug/<test_executable> --list_content         # List available tests
./bin/debug/<test_executable> --run_test="TestCase*" # Run specific test

# Examples
./bin/debug/entity_data_manager_tests
./bin/debug/ai_manager_edm_integration_tests
./bin/debug/behavior_functionality_tests --run_test="FleeFromAttacker*"

# Run all tests (slow, 7-20 min)
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
./tests/test_scripts/run_controller_tests.sh --verbose
```

Prefer direct test executables over wrapper scripts for speed.

**Boost.Test Notes**: Test names use the BOOST_AUTO_TEST_CASE name directly (e.g., `ThreadingModeComparison`, not `TestThreadingModeComparison`). Suite prefix is optional. Use `--list_content` to verify exact names.

## Architecture

**C++20 game engine** using SDL3, built with CMake/Ninja. Data-oriented design supporting 10K+ entities at 60+ FPS.

### Dependency Direction
```
Core → Managers → GameStates → Entities/Controllers
```

### Source Layout
- `src/` and `include/` mirror each other: `{core, managers, controllers, gameStates, entities, events, ai, collisions, utils, world, gpu}`
- `tests/` — Boost.Test suites, scripts in `tests/test_scripts/`
- `res/` — img, shaders, fonts, data, sprites
- `docs/` — comprehensive subsystem documentation

### Key Systems

**Core**: GameEngine (fixed timestep) | ThreadSystem (WorkerBudget) | Logger (thread-safe) | TimestepManager

**Managers**: EntityDataManager (central data store, SoA) | AIManager (10K+ entities, SIMD) | EventManager (16 event types) | CollisionManager (HierarchicalSpatialHash) | ParticleManager (SoA, pooled) | PathfinderManager | WorldManager (chunk-based procedural) | WorldResourceManager (spatial registry) | BackgroundSimulationManager (tiered) | UIManager (theming, DPI) | GameTimeManager | InputManager | TextureManager | FontManager | SoundManager

**Entities**: EntityKind (10 types) | SimulationTier (Active/Background/Hibernated) | EntityHandle (generation-safe)

**AI**: AIBehavior base → 9 behaviors (Idle, Wander, Patrol, Chase, Flee, Follow, Guard, Attack, Custom) | BehaviorContext (lock-free EDM access)

**Controllers**: State-scoped helpers via ControllerRegistry. Dir: `controllers/{combat,social,world,render}/`

**Utils**: Camera (world↔screen) | Vector2D | SIMDMath (SSE2/NEON) | JsonReader | BinarySerializer | UniqueID | FrameProfiler (F3 debug overlay)

**GPU Rendering**: GPUDevice (singleton) | GPURenderer (frame orchestration) | GPUShaderManager (SPIR-V/Metal) | SpriteBatch (50K sprites) | GPUVertexPool (triple-buffered). Shaders: `res/shaders/`

### Rendering
- Flow: scene pass → composite to swapchain → UI pass. Platform-native shaders (Metal/macOS, DXIL/Windows, SPIR-V/Linux).
- Game states implement `renderGPUScene()` and `renderGPUUI()`. The engine ends the frame outside the state.
- Scene texture = viewport dimensions (1x). Zoom handled in composite shader, not by scaling tile positions.
- Composite shader uses LINEAR sampler for sub-pixel scrolling: `uv = fragTexCoord / zoom + subPixelOffset`.

**Rule**: Exactly one present per frame. `GameEngine::render()` handles scene+UI, `GameEngine::present()` does the present. NEVER call SDL_RenderClear/Present in GameStates.

**DayNightController**: Requires `update(dt)` each frame for lighting interpolation (30s transitions). Updates automatically via `GPURenderer::setDayNightParams()`.

**Loading**: Use `LoadingState` with async ThreadSystem ops, not blocking manual rendering.

**Deferred transitions**: Set flag in `enter()`, transition in `update()` to avoid timing issues.

### AI and Controller Boundaries
- Decision logic belongs in `Behaviors::` namespace (`BehaviorExecutors.hpp/.cpp`), not in EDM.
- Controllers must never directly mutate AI behavior state in EDM.
  - Main thread: `Behaviors::queueBehaviorMessage(idx, BehaviorMessage::X)`
  - Worker threads: `Behaviors::deferBehaviorMessage()`
- Cross-frame state (paths, timers) belongs in EDM, not local variables.
- `switchBehavior()` calls `clearBehaviorData()` → `setBehaviorConfig()` → `init()`. State set before switch is wiped — always set state after.

### State Transitions
Cleanup order for AI-heavy states when all managers are initialized:
AIManager → BackgroundSimulationManager → WorldResourceManager → EventManager → CollisionManager → PathfinderManager → EntityDataManager → WorkerBudgetManager → ParticleManager

Call `prepareForStateTransition()` on managers before cleanup. Pauses work, waits for pending batches, drains message queues.

`ControllerRegistry::clear()` (not just `unsubscribeAll()`) must be called in `GamePlayState::exit()`.

## Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw threads) | STL algorithms > loops

**Naming**: UpperCamelCase (classes) | lowerCamelCase (functions/vars) | `m_`/`mp_` prefixes | ALL_CAPS (constants)

**Headers**: `.hpp` C++, `.h` C | Forward declarations | Non-trivial logic in .cpp

**Params**: `const T&` for read-only, `T&` for mutation, value only for primitives. `const std::string&` for map lookups (never string_view→string conversion).

**Logging**: Use `std::format()`, never `+` concatenation. Use `AI_INFO_IF(cond, msg)` macros when condition only gates logging.

**Copyright**: `/* Copyright (c) 2025 Hammer Forged Games ... MIT License */`

### Threading

Sequential execution with parallel batching. Main thread handles SDL (events, render). Worker threads process batches.

**ThreadSystem**: Pool of `hardware_concurrency - 1` workers. 5 priority levels (Critical→Idle). Use `enqueueTaskWithResult()` for futures, `batchEnqueueTasks()` for bulk submission.

**WorkerBudget**: Adaptive batch sizing with unified threshold learning. `shouldUseThreading()` returns threading decision. `getBatchStrategy()` calculates batch count/size. `reportExecution()` for unified throughput tracking.

**Manager Pattern** (AIManager, ParticleManager):
```cpp
auto decision = budgetMgr.shouldUseThreading(SystemType::AI, count);
if (decision.shouldThread) {
    for (size_t i = 0; i < decision.batchCount; ++i) {
        m_futures.push_back(threadSystem.enqueueTaskWithResult([...] { processBatch(...); }));
    }
    for (auto& f : m_futures) { f.get(); }  // Wait before frame ends
}
budgetMgr.reportExecution(SystemType::AI, count, decision.shouldThread, decision.batchCount, elapsedMs);
```

**Thread-Local**: Use for RNG (`thread_local std::mt19937`), reusable buffers, and spatial caches. Eliminates contention without locks. When collecting from thread_local vectors, use ref-based API with `clear()` to preserve capacity — never `swap()` or return-by-value which destroys capacity and causes per-frame heap allocations per worker thread.

**Synchronization**: `shared_mutex` for reader-writer (entities, behaviors) | `mutex` for exclusive | `atomic<bool>` for flags | `condition_variable` for worker wake.

**Rules**: NEVER static vars in threaded code | Main thread only for SDL | Futures must complete before dependent ops | Cache-line align hot atomics (`alignas(64)`)

## Memory

Avoid per-frame allocations. Reuse buffers:
```cpp
class Manager {
    std::vector<Data> m_buffer;  // Member, reused
    void update() { m_buffer.clear(); /* use */ }  // clear() keeps capacity
};
```
Always `reserve()` when size known.

## EDM (EntityDataManager) Patterns

**EDM is pure data storage** — it stores, retrieves, and aggregates entity state. AI decision logic (personality-scaled emotions, distance-based intensity, threat evaluation) belongs in the AI layer (`Behaviors::` namespace in `BehaviorExecutors.hpp/.cpp`).

Behaviors access EDM via `BehaviorContext`: `ctx.behaviorData` (state), `ctx.pathData` (navigation), `ctx.memoryData` (combat/emotions). All pre-fetched in `processBatch()`.

**Controller → AI boundary**: Controllers must NEVER directly mutate AI behavior state in EDM. Use `Behaviors::queueBehaviorMessage()` from main thread or `Behaviors::deferBehaviorMessage()` from worker threads.

**CRITICAL:** Data surviving between frames (paths, timers) MUST use EDM, never local variables — temporaries are destroyed each frame causing infinite recomputation.

## SIMD

Cross-platform: `include/utils/SIMDMath.hpp` (SSE2/NEON/AVX2). Process 4 elements/iteration + scalar tail. Always provide scalar fallback.

## UI Positioning

**Always** call `setComponentPositioning()` after creating components for resize/fullscreen support.

Helpers: `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, `createCenteredDialog()`

Manual: `ui.createButton("id", rect, "text"); ui.setComponentPositioning("id", {UIPositionMode::TOP_ALIGNED, ...});`

Modes: ABSOLUTE, TOP_ALIGNED, BOTTOM_ALIGNED, LEFT/RIGHT_ALIGNED, TOP_RIGHT, BOTTOM_RIGHT, BOTTOM_CENTERED, CENTERED_H, CENTERED_V, CENTERED_BOTH

## GameState Architecture

**State Transitions**: Use `mp_stateManager->changeState()`, never `GameEngine::Instance()`. Base class provides `mp_stateManager`.

**Manager/Controller Access**: Local references, not cached member pointers. Cache at function top when used **multiple times**, otherwise call directly.
```cpp
void SomeState::update(float dt) {
    const auto& inputMgr = InputManager::Instance();  // Manager (singleton)
    auto& combatCtrl = *m_controllers.get<CombatController>();  // Controller (registry)
    // Use with dot notation throughout function
}
// Single use - no caching needed
m_controllers.get<WeatherController>()->getCurrentWeather();
```
`const auto&` for read-only, `auto&` for mutable. Controllers: `m_controllers.add<T>()` in enter(), no cached `mp_*Ctrl` pointers.

**Lazy String Caching**: Cache enum→string conversions, recompute only on change: `if (m_phase != m_lastPhase) { m_str = getPhaseString(); m_lastPhase = m_phase; }`

**Layout Caching**: Compute static positions (LogoState) in `enter()`, use cached values in `render()`.

## Debug Tools

**FrameProfiler** (F3): Three-tier timing (Frame→Manager→Render phases). RAII timers: `ScopedPhaseTimer`, `ScopedManagerTimer`, `ScopedRenderTimer`. Hitch detection (>20ms). No-op in Release builds.

## Repo-Specific Traps

- Demo states are for testing, not production. `GamePlayState` is the pristine official gameplay state.
- `WorldResourceManager` is a spatial index over EDM, not a quantity store
- Do not wire `subscribeWorldEvents()` in init() — `WorldManager` fires deferred events that arrive after new world is populated

## Workflow

Always use established systems and patterns (UIManager helpers, state architecture, existing constants). NEVER create ad-hoc or one-off implementations when a pattern already exists — read the existing code first.

Prefer minimal, architecturally performant, and efficient solutions. Do not add unnecessary abstractions, statistical analysis, or safety checks beyond what was asked.

When the user names a specific file (e.g., "AIDemoState"), work on exactly that file. Do not substitute similar-sounding files.

Search existing patterns before implementing.

**Demo States**: States with "Demo" suffix (EventDemoState, UIDemoState, AIDemoState, AdvancedAIDemoState, OverlayDemoState) are for testing/showcasing features.

**GamePlayState**: The pristine official gameplay state. Keep clean and production-ready.

**Reference States**: SettingsMenuState, MainMenuState for menu patterns.

## Bug Fixing

Fix root causes in production code. NEVER bypass failing tests by modifying test expectations unless explicitly told to.

When debugging rendering issues (jitter, shimmer, flickering), trace the full render pipeline before proposing any fix: camera update → interpolation → floor/round operations → sub-pixel offset → draw. No speculative fixes.

When removing dead code or unused parameters, delete them entirely. Do not comment them out.

## Working Principles

See `AGENTS.md` for detailed task checklists and the full priority order. Key points:
- Read the exact code path before editing. Search for matching patterns in the same subsystem.
- Fix root causes in production code; never bypass failing tests by changing expectations.
- Keep production code and tests aligned in the same change.
- Prefer minimal direct fixes over new abstractions.
- Run the most targeted test executable after changes when feasible.
