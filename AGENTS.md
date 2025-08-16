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
- Platform guards for OS-specific logic (`#ifdef __APPLE__`, `#ifdef WIN32`).

## Testing Guidelines
- Framework: Boost.Test. Place tests under `tests/`; binaries output to `bin/debug/`.
- Write focused/thorough tests (success and error paths). Clean up test artifacts.

## Architecture & Safety Notes
- Update/Render: GameEngine coordinates update/render; don’t add extra sync in managers.
- Managers: Follow Singleton shutdown pattern (`m_isShutdown` guard).
- InputManager: Preserve SDL gamepad init/quit pattern to avoid macOS crashes.
