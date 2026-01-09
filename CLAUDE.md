# CLAUDE.md

SDL3 HammerEngine development guidance.

## Build

```bash
# Debug/Release
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# ASAN/TSAN (require -DUSE_MOLD_LINKER=OFF, mutually exclusive)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build

# TSAN suppressions: export TSAN_OPTIONS="suppressions=$(pwd)/tests/tsan_suppressions.txt"
```

**Output**: `bin/debug/` or `bin/release/` | **Run**: `./bin/debug/SDL3_Template`

## Testing

Boost.Test (58 executables). **Prefer direct test execution** - much faster than test scripts.

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

**Core**: GameEngine (fixed timestep) | ThreadSystem (WorkerBudget) | Logger (thread-safe)

**Managers**: AIManager (10K+ entities, SIMD batch) | EventManager (thread-safe batch) | CollisionManager (spatial hash) | ParticleManager (camera-aware) | WorldManager (tile-based procedural) | UIManager (theming, DPI) | GameTimeManager + WeatherController/DayNightController

**Utils**: Camera (world↔screen) | Vector2D | JsonReader | BinarySerializer

**Controllers**: State-scoped helpers (no data ownership). Dir: `controllers/{world,combat}/`

**Structure**: `src/{core,managers,controllers,gameStates,entities,events,ai,collisions,utils,world}` | `include/` mirrors src | `tests/` | `res/`

## Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw threads) | STL algorithms > loops

**Params**: `const T&` for read-only, `T&` for mutation, value only for primitives. `const std::string&` for map lookups (never string_view→string conversion).

**Naming**: UpperCamelCase (classes) | lowerCamelCase (functions/vars) | `m_`/`mp_` prefixes | ALL_CAPS (constants)

**Headers**: `.hpp` C++, `.h` C | Forward declarations | Non-trivial logic in .cpp

**Threading**: Main thread: Update→Render sequential | Background: ThreadSystem for AI/Particle/Pathfinding | **No static vars in threaded code**

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

## Workflow

Search existing patterns before implementing. Reference states: EventDemoState, UIDemoState, SettingsMenuState, MainMenuState.
