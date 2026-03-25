# Repository Guidelines

SDL3 HammerEngine development guidance for AI agents.

## Build

```bash
# Debug/Release (SDL_Renderer path)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Debug/Release with SDL3 GPU rendering (compiles SPIR-V/Metal shaders)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_SDL3_GPU=ON && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3_GPU=ON && ninja -C build

# ASAN/TSAN (require -DUSE_MOLD_LINKER=OFF, mutually exclusive)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build

# TSAN suppressions: export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"
# CMake reconfigure (without full rebuild): rm build/CMakeCache.txt && cmake -B build/ ...
```

**Output**: `bin/debug/` or `bin/release/` | **Run**: `./bin/debug/SDL3_Template`

## Testing

Boost.Test (70 executables in the default non-GPU build; additional GPU test executables/benchmarks when `USE_SDL3_GPU=ON`). **Prefer direct test execution** - much faster than test scripts.

```bash
# Direct test execution (PREFERRED for development - fast feedback)
./bin/debug/<test_executable>                        # Run all tests in executable
./bin/debug/<test_executable> --list_content         # List available tests
./bin/debug/<test_executable> --run_test="TestCase*" # Run specific test
./bin/debug/entity_data_manager_tests                # Run EDM tests directly
./bin/debug/ai_manager_edm_integration_tests         # Run AI-EDM integration

# Test scripts (use for comprehensive validation only - slow)
./tests/test_scripts/run_all_tests.sh --core-only --errors-only
./tests/test_scripts/run_controller_tests.sh --verbose
```

See `tests/TESTING.md` for comprehensive documentation.

**Boost.Test Notes**: Test names use the BOOST_AUTO_TEST_CASE name directly (e.g., `ThreadingModeComparison`, not `TestThreadingModeComparison`). Suite prefix is optional. Use `--list_content` to verify exact names.

## Architecture

**Core**: GameEngine (fixed timestep) | ThreadSystem (WorkerBudget) | Logger (thread-safe) | TimestepManager

**Managers**: EntityDataManager (central data store, SoA) | AIManager (10K+ entities, SIMD) | EventManager (16 event types) | CollisionManager (HierarchicalSpatialHash) | ParticleManager (SoA, pooled) | PathfinderManager | WorldManager (chunk-based procedural) | WorldResourceManager (spatial registry) | BackgroundSimulationManager (tiered) | UIManager (theming, DPI) | GameTimeManager | InputManager | TextureManager | FontManager | SoundManager

**Entities**: EntityKind (8 types) | SimulationTier (Active/Background/Hibernated) | EntityHandle (generation-safe)

**AI**: AIBehavior base → 8 behaviors (Idle, Wander, Patrol, Chase, Flee, Follow, Guard, Attack) | BehaviorContext (lock-free EDM access)

**Controllers**: State-scoped helpers via ControllerRegistry. Dir: `controllers/{combat,social,world,render}/`

**Utils**: Camera (world↔screen) | Vector2D | SIMDMath (SSE2/NEON) | JsonReader | BinarySerializer | UniqueID | WorldRenderPipeline (SDL_Renderer facade) | FrameProfiler (F3 debug overlay)

**GPU Rendering** (USE_SDL3_GPU): GPUDevice (singleton) | GPURenderer (frame orchestration) | GPUShaderManager (SPIR-V/Metal) | SpriteBatch (25K sprites) | GPUVertexPool (triple-buffered) | GPUSceneRenderer (scene facade). Shaders: `res/shaders/`

**Structure**: `src/{core,managers,controllers,gameStates,entities,events,ai,collisions,utils,world,gpu}` | `include/` mirrors src | `tests/` | `res/`

**Layer Dependencies**: Core → Managers → GameStates → Entities/Controllers

## Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw threads) | STL algorithms > loops

**Params**: `const T&` for read-only, `T&` for mutation, value only for primitives. `const std::string&` for map lookups (never string_view→string conversion).

**Access Patterns**: Prefer `std::span`, `std::string_view`, `std::optional`, and explicit read/mutate APIs over raw arrays, raw pointer escape paths, and nullable pointer-return accessors. Do not add new compatibility overloads that preserve legacy raw-pointer access during cleanups unless explicitly required.

**Naming**: UpperCamelCase (classes) | lowerCamelCase (functions/vars) | `m_`/`mp_` prefixes | ALL_CAPS (constants)

**Headers**: `.hpp` C++, `.h` C | Forward declarations | Non-trivial logic in .cpp

**Threading**: Sequential execution with parallel batching. Main thread handles SDL (events, render). Worker threads process batches.

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

**State Transitions**: Call `prepareForStateTransition()` on active managers before cleanup. Current AI-heavy states typically transition in this order when those systems are initialized: AIManager, BackgroundSimulationManager, WorldResourceManager, EventManager, CollisionManager, PathfinderManager, EntityDataManager, WorkerBudgetManager, ParticleManager. Some demo states skip managers they do not initialize.

**Thread-Local**: Use for RNG (`thread_local std::mt19937`), reusable buffers, and spatial caches. Eliminates contention without locks. Prefer ref-based APIs and `clear()` when reusing thread-local vectors to preserve capacity. Avoid return-by-value patterns that force repeated allocations; use `swap()` only when it is intentionally preserving reusable storage across buffers.

**Synchronization**: `shared_mutex` for reader-writer (entities, behaviors) | `mutex` for exclusive | `atomic<bool>` for flags | `condition_variable` for worker wake.

**Rules**: Avoid non-`thread_local` static state in threaded code | Main thread only for SDL | Futures must complete before dependent ops | Cache-line align hot atomics (`alignas(64)`)

**Logging**: Use `std::format()`, never `+` concatenation. Use `AI_INFO_IF(cond, msg)` macros when condition only gates logging.

**Copyright**: `/* Copyright (c) 2025 Hammer Forged Games ... MIT License */`

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

- `EDM::recordCombatEvent()` — records stats (lastAttacker, damage totals, flags, memory entry). No emotion math.
- `Behaviors::processCombatEvent()` — wraps EDM data call + applies personality-scaled emotion changes.
- `Behaviors::processWitnessedCombat()` — distance falloff + composure-modulated emotions + memory entry via `EDM::addMemory()`.
- Emotional contagion runs as a main-thread pre-pass in `AIManager::update()`, not in EDM.

Behaviors access EDM via context: `ctx.behaviorData` (state), `ctx.pathData` (navigation). Both pre-fetched in `processBatch()`.

**Controller → AI boundary**: Controllers must NEVER directly mutate AI behavior state in EDM (guard alertLevel, behavior flags, etc.). Use `Behaviors::queueBehaviorMessage(idx, BehaviorMessage::X)` from main thread or `Behaviors::deferBehaviorMessage()` from worker threads. The behavior's message handler applies the state change during its next update.

**CRITICAL:** Data surviving between frames (paths, timers) MUST use EDM, never local variables:
```cpp
// BAD - temp destroyed each frame = infinite path recomputation
AIBehaviorState temp; temp.pathPoints = compute(); // LOST!

// GOOD - use EDM directly
PathData& pd = *ctx.pathData; pathfinder().requestPathToEDM(ctx.edmIndex, ...);
```

## SIMD

Cross-platform: `include/utils/SIMDMath.hpp` (SSE2/NEON). Process 4 elements/iteration + scalar tail. Always provide scalar fallback. Reference: `AIManager::calculateDistancesSIMD()`.

## UI Positioning

**Always** call `setComponentPositioning()` after creating components for resize/fullscreen support.

Helpers: `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, `createCenteredDialog()`

Manual: `ui.createButton("id", rect, "text"); ui.setComponentPositioning("id", {UIPositionMode::TOP_ALIGNED, ...});`

Modes: ABSOLUTE, CENTERED_H, CENTERED_V, CENTERED_BOTH, TOP_ALIGNED, TOP_RIGHT, BOTTOM_ALIGNED, BOTTOM_CENTERED, BOTTOM_RIGHT, LEFT_ALIGNED, RIGHT_ALIGNED

## Rendering

**One Present() per frame**: `GameEngine::render()` performs scene/UI rendering, and `GameEngine::present()` performs the actual present/end-frame step. NEVER call SDL_RenderClear/Present in GameStates.

**SDL_Renderer Path**: WorldRenderPipeline (4-phase: prepareChunks→beginScene→renderWorld→endScene) wraps SceneRenderer for pixel-perfect zoom and sub-pixel scrolling.

**GPU Path**: Scene pass → composite to swapchain → UI pass. GameStates implement `renderGPUScene()` and `renderGPUUI()`, while the engine ends the frame outside the GameState.

**SDL3_GPU UI Text**: Use `TTF_GetGPUTextDrawData()` only. No UV flips, half-texel offsets, or shader hacks. For raster UI/menu text in integer UI layouts, snap final text placement to whole pixels before emitting GPU vertices to avoid bottom-edge shaving with linear filtering.

**DayNightController**: Requires `update(dt)` each frame for lighting interpolation (30s transitions). GPU path updates automatically via `GPURenderer::setDayNightParams()`.

**Loading**: Use `LoadingState` with async ThreadSystem ops, not blocking manual rendering.

**Deferred transitions**: Set flag in `enter()`, transition in `update()` to avoid timing issues.

## GameState Architecture

**State Transitions**: Use `mp_stateManager->changeState()` for state changes. Base class provides `mp_stateManager`. `GameEngine::Instance()` is still used in states for non-transition engine access such as pause, window sizing, or shutdown.

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

## Workflow

Always use established systems and patterns (UIManager helpers, state architecture, existing constants). NEVER create ad-hoc or one-off implementations when a pattern already exists — read the existing code first.

Prefer minimal, architecturally performant, and efficient solutions. Do not add unnecessary abstractions, statistical analysis, or safety checks beyond what was asked.

When tightening APIs for safety, finish the migration in production code and tests in the same change. Do not leave legacy call paths behind if the new API is intended to replace them.

When the user names a specific file (e.g., "AIDemoState"), work on exactly that file. Do not substitute similar-sounding files.

Search existing patterns before implementing.

**Demo States**: Demo-oriented states are for testing/showcasing features. File/class names do not always match the runtime state name exactly: for example, `EventDemoState` registers as `EventDemo`, and `UIDemoState.hpp` defines `UIExampleState`.

**GamePlayState**: The pristine official gameplay state. Keep clean and production-ready.

**Reference States**: SettingsMenuState, MainMenuState for menu patterns.

## Bug Fixing

Fix root causes in production code. NEVER bypass failing tests by modifying test expectations unless explicitly told to.
For EventManager regressions, first distinguish missing state-owned handler wiring in tests from actual production manager bugs.

When debugging rendering issues (jitter, shimmer, flickering), trace the full render pipeline before proposing any fix: camera update → interpolation → floor/round operations → sub-pixel offset → draw. No speculative fixes.

When removing dead code or unused parameters, delete them entirely. Do not comment them out.
