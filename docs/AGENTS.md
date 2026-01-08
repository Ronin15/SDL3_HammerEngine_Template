# Repository Guidelines

SDL3 HammerEngine development guide for AI agents.

## Project Structure

```
src/{core, managers, controllers, gameStates, entities, events, ai, collisions, utils, world}
include/       # Headers mirror src/
tests/         # Boost.Test (68+ executables)
bin/debug/     # Debug builds & test binaries
bin/release/   # Release builds
build/         # CMake/Ninja artifacts (reconfigure, don't delete)
res/           # Assets (fonts, images, audio, data)
docs/          # Developer documentation
```

## Build Commands

```bash
# Debug
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build

# Release
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build

# Debug with AddressSanitizer (requires -DUSE_MOLD_LINKER=OFF)
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build

# Filter warnings/errors
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)" | head -n 100
```

**Outputs**: `bin/debug/` or `bin/release/`  
**Run**: `./bin/debug/SDL3_Template` | `timeout 60s ./bin/debug/SDL3_Template`

## Testing

**Framework**: Boost.Test (68+ test executables). Use targeted tests for specific systems during development.

```bash
# All tests
./run_all_tests.sh --core-only --errors-only

# Targeted tests
./tests/test_scripts/run_save_tests.sh --verbose
./tests/test_scripts/run_ai_optimization_tests.sh
./tests/test_scripts/run_thread_tests.sh
./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"

# Static analysis
./tests/test_scripts/run_cppcheck_focused.sh

# Performance analysis
./tests/valgrind/quick_memory_check.sh
./tests/valgrind/cache_performance_analysis.sh
```

## Architecture

**Core**: GameEngine (fixed timestep, single-threaded main loop) | TimestepManager (accumulator-based timing) | ThreadSystem (WorkerBudget priorities) | Logger (thread-safe)

**Systems**: AIManager (10K+ entities @ 60+ FPS, batch-processed, with dynamic behaviors like Attack, Flee, Follow, Guard, Chase, Idle, Patrol) | GameTimeManager (manages game time, day/night cycles, and weather in coordination with controllers) | EventManager (thread-safe, batch) | CombatController (handles combat logic and interactions) | CollisionManager (spatial hash, pathfinding) | ParticleManager (camera-aware, batched) | WorldManager (tile-based, procedural) | UIManager (theming, DPI-aware) | ResourceManager (JSON + handles) | InputManager (keyboard/mouse/gamepad)

**Utils**: Camera (world↔screen, zoom) | Vector2D (2D math) | JsonReader | BinarySerializer (cross-platform save/load)

## Coding Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw std::thread) | Logger macros | Cross-platform guards | STL algorithms > manual loops

**Naming**: UpperCamelCase (classes/enums) | lowerCamelCase (functions/vars) | `m_` prefix (members), `mp_` (pointers) | ALL_CAPS (constants)

**Headers**: `.hpp` (C++), `.h` (C) | Minimal interface, forward declarations | Non-trivial logic in .cpp | Inline only for trivial 1-2 line accessors | Templates/constexpr/trivial members in headers | All logic, constructors, singletons in .cpp

**Threading**: Update (mutex-locked, fixed timestep) | Render (main thread only, double-buffered) | Background (ThreadSystem + WorkerBudget) | **NEVER static vars in threaded code** (use instance vars, thread_local, or atomics)

**Strings**: `std::string_view` for input-only params | `std::string` for ownership | C strings only at C/SDL/OS boundaries | Never persist `string_view` beyond source lifetime

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

## Engine Loop & Render Flow

**Main Loop** (in `HammerMain.cpp`): Single-threaded fixed timestep pattern. Events → Update(s) → Render, all sequential on main thread.

**Update** (main thread): `GameEngine::update(deltaTime)` updates global systems (AIManager, EventManager, ParticleManager) → delegates to `GameStateManager::update`. Fixed timestep via accumulator drains multiple updates per frame if needed.

**Double Buffer**: `m_currentBufferIndex` (update) + `m_renderBufferIndex` (render) + `m_bufferReady[]`. Main loop calls `hasNewFrameToRender()` + `swapBuffers()` before update; render consumes stable previous buffer.

**Render** (main thread): `GameEngine::render()` clears renderer → `GameStateManager::render()` → world/entities/particles/UI (deterministic order, current camera). World tiles use `WorldManager::render(renderer, cameraX, cameraY, viewportW, viewportH)`. Entities use `Entity::render(const Camera*)` for world→screen conversion.

**Rules**: No background thread rendering | Sequential update/render (no mutex needed) | Snapshot camera once per render | NEVER static vars in threaded code | Background work via ThreadSystem workers

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
