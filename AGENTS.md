# Repository Guidelines

## Project Structure & Module Organization
- `src/`: Engine/game implementation (managers, gameStates, utils).
- `include/`: Public headers aligned with `src/` modules.
- `tests/`: Boost.Test suites and scripts under `tests/test_scripts/`.
- `bin/debug`, `bin/release`: Final executables. Tests run from `bin/debug/`.
- `build/`: CMake/Ninja artifacts (don’t remove; re-configure first).
- `res/`: Assets (fonts, images, audio).  `docs/`: Developer docs (e.g., `docs/Logger.md`).

## Build, Test, and Development Commands
- Debug build: 
  ```
  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
  ninja -C build -v 2>&1 | grep -E "(warning|unused|error) " | head -n 100
  ```
- Debug + AddressSanitizer:
  ```
  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" && ninja -C build
  ```
- Run all tests:
  ```
  timeout 95s ./run_all_tests.sh --core-only --errors-only
  ```
- Run a single test (examples):
  ```
  ./tests/test_scripts/run_save_tests.sh --verbose
  ./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
  ```
- Main executable: `./bin/debug/SDL3_Template` (Release: `./bin/release/SDL3_Template`).

## Coding Style & Naming Conventions
- Standard: C++20.  Indentation: 4 spaces; braces on new lines.
- Naming: UpperCamelCase (classes/enums/namespaces), lowerCamelCase (func/vars), `m_` prefix for members.
- Memory: RAII with `std::unique_ptr`/`std::shared_ptr`; no raw `new/delete`.
- Threading: Use `ThreadSystem` + WorkerBudget; avoid raw `std::thread`.
- Logging: Use provided macros (`GAMEENGINE_ERROR`, etc.).
- Performance: Prefer STL algorithms (e.g., `std::sort`, `std::find_if`, `std::transform`) over manual loops for better optimization.
- Platform guards for OS-specific logic (`#ifdef __APPLE__`, `#ifdef WIN32`).

## Testing Guidelines
- Framework: Boost.Test. Place tests under `tests/`; binaries output to `bin/debug/`.
- Write focused/thorough tests (success and error paths). Clean up test artifacts.

## Architecture & Safety Notes
- Update/Render: GameEngine coordinates update/render; don’t add extra sync in managers.
- Managers: Follow Singleton shutdown pattern (`m_isShutdown` guard).
- InputManager: Preserve SDL gamepad init/quit pattern to avoid macOS crashes.

## Engine Loop & Render Flow
- GameLoop: Drives three callbacks — events (main thread), fixed-timestep updates, and rendering. Target FPS and fixed timestep are configured in `HammerMain.cpp` via `GameLoop`.
- Update (thread-safe): `GameEngine::update(deltaTime)` runs under a mutex to guarantee completion before any render. It updates global systems (AIManager, EventManager, ParticleManager), then delegates to the current `GameStateManager::update`.
- Double buffering: `GameEngine` maintains `m_currentBufferIndex` (update) and `m_renderBufferIndex` (render) with `m_bufferReady[]`. The main loop calls `hasNewFrameToRender()` and `swapBuffers()` before each update, allowing render to consume a stable buffer from the previous tick.
- Render (main thread): `GameEngine::render()` clears the renderer and calls `GameStateManager::render()`. States render world, entities, particles, and UI in a deterministic order using the current camera view.
- World tiles: `WorldManager::render(renderer, cameraX, cameraY, viewportW, viewportH)` renders visible tiles using the same camera view for consistent alignment with entities.
- Entities: `Entity::render(const Camera*)` converts world → screen using the provided camera; do not compute per-entity camera offsets outside this path.
- Threading: No rendering from background threads. AI/particles may schedule work but all drawing occurs during `GameEngine::render()` on the main thread.

Guidelines
- Do not introduce additional synchronization between managers for rendering; rely on `GameEngine`’s mutexed update and double-buffer swap.
- When adding a new state, snapshot camera/view once per render pass and reuse it for all world-space systems.
- Keep camera-aware rendering centralized; avoid ad-hoc camera math inside managers that don’t own presentation.
