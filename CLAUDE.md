# CLAUDE.md

Claude Code guidance for SDL3 HammerEngine development.

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

# Clean: ninja -C build clean | Full rebuild: rm -rf build/
# Reconfigure: rm build/CMakeCache.txt (optional, re-running cmake updates settings)
```

**Outputs**: `bin/debug/` or `bin/release/`
**Run**: `./bin/debug/SDL3_Template` | `timeout 60s ./bin/debug/SDL3_Template > /tmp/app_log.txt 2>&1`

## Testing

**Framework**: Boost.Test (68+ test executables in `bin/debug/`). Use targeted tests for specific systems; avoid full suite during development.

```bash
# All tests
./run_all_tests.sh --core-only --errors-only

# Targeted tests
./tests/test_scripts/run_save_tests.sh --verbose
./tests/test_scripts/run_ai_optimization_tests.sh
./tests/test_scripts/run_thread_tests.sh
./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
```

## Architecture

**Core**: GameEngine (double-buffered, central coordinator) | GameLoop (fixed timestep, separate update/render threads) | ThreadSystem (WorkerBudget priorities) | Logger (thread-safe)

**Systems**: AIManager (10K+ entities @ 60+ FPS, batch-processed) | EventManager (thread-safe, batch processing) | CollisionManager (spatial hash, pathfinding integration) | ParticleManager (camera-aware, batched) | WorldManager (tile-based, procedural) | UIManager (theming, DPI-aware) | ResourceManager (JSON + handles) | InputManager (keyboard/mouse/gamepad)

**Utils**: Camera (world↔screen, zoom) | Vector2D (2D math) | JsonReader | BinarySerializer (cross-platform save/load)

```
src/{core, managers, gameStates, entities, events, ai, collisions, utils, world}
include/  # Headers mirror src/
tests/    # Boost.Test scripts
res/      # Assets
```

## Standards

**C++20** | 4-space indent, Allman braces | RAII + smart pointers | ThreadSystem (not raw std::thread) | Exceptions for critical errors, codes for expected failures | Logger macros | Cross-platform guards | STL algorithms > manual loops

**Naming**: UpperCamelCase (classes/enums) | lowerCamelCase (functions/vars) | `m_` prefix (members), `mp_` (pointers) | ALL_CAPS (constants)

**Headers**: `.hpp` for C++, `.h` for C | Minimal interface, forward declarations | Non-trivial logic in .cpp | Inline only for trivial 1-2 line accessors

**Threading**: Update (mutex-locked, fixed timestep) | Render (main thread only, double-buffered) | Background (ThreadSystem + WorkerBudget) | **NEVER static vars in threaded code** (use instance vars, thread_local, or atomics)

**Copyright** (all files):
```cpp
/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/
```

## GameEngine Update/Render Flow

**GameLoop** (configured in `HammerMain.cpp`): Drives events (main thread) → fixed-timestep update → render callbacks.

**Update** (thread-safe, mutex-locked): `GameEngine::update(deltaTime)` updates global systems (AIManager, EventManager, ParticleManager) → delegates to `GameStateManager::update`. Completes before render starts.

**Double Buffer**: `m_currentBufferIndex` (update) + `m_renderBufferIndex` (render) + `m_bufferReady[]`. Main loop calls `hasNewFrameToRender()` + `swapBuffers()` before update; render consumes stable previous buffer.

**Render** (main thread only): `GameEngine::render()` clears renderer → `GameStateManager::render()` → world/entities/particles/UI (deterministic order, current camera).

**Rules**: No background thread rendering (all drawing in `GameEngine::render()`) | No extra manager sync (rely on mutexed update + buffer swap) | Snapshot camera once per render | NEVER static vars in threaded code
