# Repository Guidelines

SDL3 HammerEngine development guide for AI agents.

## Project Structure

```
src/{core, managers, controllers, gameStates, entities, events, ai, collisions, utils, world}
include/       # Headers mirror src/
tests/         # Boost.Test (58 executables)
bin/debug/     # Debug builds & test binaries
bin/release/   # Release builds
build/         # CMake/Ninja artifacts (reconfigure, don't delete)
res/           # Assets (fonts, images, audio, data)
docs/          # Developer documentation
```

## Build Commands

```bash
# Debug/Release
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# ASAN/TSAN (require -DUSE_MOLD_LINKER=OFF, mutually exclusive)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build

# TSAN suppressions: export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"

# Filter warnings/errors
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)" | head -n 100
```

**Outputs**: `bin/debug/` or `bin/release/`  
**Run**: `./bin/debug/SDL3_Template` | `timeout 60s ./bin/debug/SDL3_Template`

## Testing

**Framework**: Boost.Test (58 executables). **Prefer direct test execution** - much faster than test scripts.

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

# Static analysis
./tests/test_scripts/run_cppcheck_focused.sh

# Performance analysis
./tests/valgrind/quick_memory_check.sh
./tests/valgrind/cache_performance_analysis.sh
```

See `tests/TESTING.md` for comprehensive documentation.

**Boost.Test Notes**: Test names use the BOOST_AUTO_TEST_CASE name directly (e.g., `ThreadingModeComparison`, not `TestThreadingModeComparison`). Suite prefix is optional. Use `--list_content` to verify exact names.

## Architecture

**Core**: GameEngine (fixed timestep) | ThreadSystem (WorkerBudget) | Logger (thread-safe)

**Managers**: AIManager (10K+ entities, SIMD batch) | EventManager (thread-safe batch) | CollisionManager (spatial hash) | ParticleManager (camera-aware) | WorldManager (tile-based procedural) | UIManager (theming, DPI) | GameTimeManager + WeatherController/DayNightController

**Controllers**: State-scoped helpers (no data ownership). Dir: `controllers/{world,combat}/`

**Utils**: Camera (world↔screen) | Vector2D | JsonReader | BinarySerializer

## Coding Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw std::thread) | Logger macros | Cross-platform guards | STL algorithms > manual loops

**Naming**: UpperCamelCase (classes/enums) | lowerCamelCase (functions/vars) | `m_` prefix (members), `mp_` (pointers) | ALL_CAPS (constants)

**Params**: `const T&` for read-only, `T&` for mutation, value only for primitives. `const std::string&` for map lookups (never string_view→string conversion).

**Headers**: `.hpp` (C++), `.h` (C) | Minimal interface, forward declarations | Non-trivial logic in .cpp | Inline only for trivial 1-2 line accessors | Templates/constexpr/trivial members in headers | All logic, constructors, singletons in .cpp

**Threading**: Sequential execution with parallel batching. Main thread handles SDL (events, render). Worker threads process batches.

**ThreadSystem**: Pool of `hardware_concurrency - 1` workers. 5 priority levels (Critical→Idle). Use `enqueueTaskWithResult()` for futures, `batchEnqueueTasks()` for bulk submission.

**WorkerBudget**: Adaptive batch sizing. `getOptimalWorkers()` returns all workers (sequential model). `getBatchStrategy()` calculates batch count/size. `reportBatchCompletion()` for throughput tuning.

**Manager Pattern** (AIManager, ParticleManager):
```cpp
auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(SystemType::AI, count, workers);
for (size_t i = 0; i < batchCount; ++i) {
    m_futures.push_back(threadSystem.enqueueTaskWithResult([...] { processBatch(...); }));
}
for (auto& f : m_futures) { f.get(); }  // Wait before frame ends
```

**State Transitions**: Call `prepareForStateTransition()` on managers before cleanup. Pauses work, waits for pending batches, drains message queues.

**Thread-Local**: Use for RNG (`thread_local std::mt19937`), reusable buffers, and spatial caches. Eliminates contention without locks.

**Synchronization**: `shared_mutex` for reader-writer (entities, behaviors) | `mutex` for exclusive | `atomic<bool>` for flags | `condition_variable` for worker wake.

**Rules**: NEVER static vars in threaded code | Main thread only for SDL | Futures must complete before dependent ops | Cache-line align hot atomics (`alignas(64)`)

**Strings**: `std::string_view` for input-only params | `std::string` for ownership | C strings only at C/SDL/OS boundaries | Never persist `string_view` beyond source lifetime

**Logging**: Use `std::format()`, never `+` concatenation. Use `AI_INFO_IF(cond, msg)` macros when condition only gates logging.

**Copyright** (all files):
```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/
```

### Manager Singleton Pattern
```cpp
class ExampleManager {
private:
    std::atomic<bool> m_isShutdown{false};
public:
    static ExampleManager& Instance() {
        static ExampleManager instance;
        return instance;
    }
    void shutdown();
};
```

## Memory Management

**Avoid Per-Frame Allocations**: Heap allocations cause frame dips due to allocator overhead. Reuse buffers.

```cpp
// BAD: Allocates/deallocates every frame
void update() {
    std::vector<Data> buffer;
    buffer.reserve(entityCount);
    // ... use buffer
}  // Deallocation

// GOOD: Reuses capacity across frames
class Manager {
    std::vector<Data> m_reusableBuffer;  // Member variable
    
    void update() {
        m_reusableBuffer.clear();  // Keeps capacity
        // ... use m_reusableBuffer
    }
};
```

**Rules**: Member vars for hot-path buffers | `clear()` over reconstruction | `reserve()` before loops | Avoid `push_back()` without reserve | Profile with -fsanitize=address

## EDM (EntityDataManager) Patterns

Behaviors access EDM via context: `ctx.behaviorData` (state), `ctx.pathData` (navigation). Both pre-fetched in `processBatch()`.

**CRITICAL:** Data surviving between frames (paths, timers) MUST use EDM, never local variables:
```cpp
// BAD - temp destroyed each frame = infinite path recomputation
AIBehaviorState temp; temp.pathPoints = compute(); // LOST!

// GOOD - use EDM directly
PathData& pd = *ctx.pathData; pathfinder().requestPathToEDM(ctx.edmIndex, ...);
```

## SIMD

Cross-platform: `include/utils/SIMDMath.hpp` (SSE2/NEON). Process 4 elements/iteration + scalar tail. Always provide scalar fallback. Reference: `AIManager::calculateDistancesSIMD()`.

## Engine Loop & Render Flow

**Main Loop** (in `HammerMain.cpp`): Single-threaded fixed timestep pattern. Events → Update(s) → Render, all sequential on main thread.

**Update** (main thread): `GameEngine::update(deltaTime)` updates global systems (AIManager, EventManager, ParticleManager) → delegates to `GameStateManager::update`. Fixed timestep via accumulator drains multiple updates per frame if needed.

**Double Buffer**: `m_currentBufferIndex` (update) + `m_renderBufferIndex` (render) + `m_bufferReady[]`. Main loop calls `hasNewFrameToRender()` + `swapBuffers()` before update; render consumes stable previous buffer.

**Render** (main thread): `GameEngine::render()` clears renderer → `GameStateManager::render()` → world/entities/particles/UI (deterministic order, current camera). World tiles use `WorldManager::render(renderer, cameraX, cameraY, viewportW, viewportH)`. Entities use `Entity::render(const Camera*)` for world→screen conversion.

**Rules**: No background thread rendering | Sequential update/render (no mutex needed) | Snapshot camera once per render | NEVER static vars in threaded code | Background work via ThreadSystem workers

## Threading

See **Threading** section under Coding Standards for ThreadSystem, WorkerBudget, and synchronization details.

## UI Positioning

**Always** call `setComponentPositioning()` after creating components for resize/fullscreen support.

Helpers: `createTitleAtTop()`, `createButtonAtBottom()`, `createCenteredButton()`, `createCenteredDialog()`

Manual: `ui.createButton("id", rect, "text"); ui.setComponentPositioning("id", {UIPositionMode::TOP_ALIGNED, ...});`

Modes: TOP_ALIGNED, BOTTOM_ALIGNED, LEFT/RIGHT_ALIGNED, BOTTOM_RIGHT, CENTERED_H, CENTERED_BOTH

## Rendering

**One Present() per frame** via `GameEngine::render()` → `GameStateManager::render()` → `GameState::render()`. NEVER call SDL_RenderClear/Present in GameStates.

**Loading**: Use `LoadingState` with async ThreadSystem ops, not blocking manual rendering.

**Deferred transitions**: Set flag in `enter()`, transition in `update()` to avoid timing issues.

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

## Critical System Patterns

### InputManager SDL Cleanup Pattern

**CRITICAL**: The InputManager has a specific SDL gamepad cleanup pattern that MUST be maintained exactly. Do NOT modify without extreme caution.

**Issue**: When no gamepads are detected, `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` still initializes the subsystem. Improper cleanup causes "trace trap" crash during `SDL_Quit()` on macOS.

**Correct Pattern**:
1. `initializeGamePad()`: Call `SDL_InitSubSystem(SDL_INIT_GAMEPAD)`
2. If no gamepads found: Immediately call `SDL_QuitSubSystem(SDL_INIT_GAMEPAD)` before returning
3. If gamepads found: Set `m_gamePadInitialized = true` and let normal cleanup handle it
4. `clean()`: Only call `SDL_QuitSubSystem(SDL_INIT_GAMEPAD)` if `m_gamePadInitialized` is true

**Do NOT**:
- Call `SDL_QuitSubSystem()` in both init and cleanup paths
- Use platform `#ifdef` blocks to skip SDL cleanup
- Rely solely on `SDL_Quit()` to clean up individually initialized subsystems
- Remove/modify `m_gamePadInitialized` flag logic

This pattern has been broken multiple times. Current implementation is correct and tested.

## Dependencies

**Required**: CMake 3.28+, Ninja, C++20 compiler | SDL3, SDL3_image, SDL3_ttf, SDL3_mixer (auto-fetched via FetchContent)

**Optional**: Boost (testing) | cppcheck (static analysis) | Valgrind (performance/memory)

## Platform Support

**Cross-Platform**: macOS (optimized flags, dSYM, Retina) | Linux (Wayland, adaptive VSync) | Windows (console control, DLL planned)

**Build Types**: Debug (console, full symbols, no opt) | Release (optimized, LTO, platform flags)

## Workflow

Search existing patterns before implementing. Reference states: EventDemoState, UIDemoState, SettingsMenuState, MainMenuState.
