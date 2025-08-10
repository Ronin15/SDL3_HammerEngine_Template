# Gemini Project Helper: SDL3_HammerEngine_Template

This document provides a summary of the SDL3_HammerEngine_Template project to help the Gemini assistant understand its structure and conventions.

## Project Overview

This is a C++ game engine template named "Hammer Engine". It uses SDL3 for cross-platform windowing, rendering, and input. The project is structured with a clear separation between header (`include/`) and source (`src/`) files, organized by engine features (e.g., `core`, `managers`, `entities`, `ai`).

## Core Technologies

-   **Language:** C++20
-   **Build System:** CMake
-   **Core Library:** SDL3 (including SDL3_image, SDL3_ttf, SDL3_mixer)
-   **Dependencies:**
    -   Dependencies are managed via CMake's `FetchContent`.
    -   Boost (Unit Test Framework) is used for testing.
-   **JSON Parsing:** A custom `JsonReader` utility is used for data files.

## Build System (CMake)

The project uses CMake for building. The main executable is `SDL3_Template`.

-   **Configure (Debug):**
    ```bash
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
    ```
-   **Configure (Release):**
    ```bash
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    ```
-   **Build:**
    After configuring, build the project with:
    ```bash
    ninja -C build
    ```
-   **Build and Check for Issues (Debug):**
    A useful command for development to build and filter for warnings/errors:
    ```bash
    ninja -C build -v 2>&1 | grep -E "(warning|unused|error)"
    ```
-   **Output Directories:**
    -   Debug builds: `bin/debug/`
    -   Release builds: `bin/release/`
-   **Compile Commands:** `compile_commands.json` is automatically generated and copied to the project root for tooling support (e.g., `clangd`).

## Testing

-   **Run All Tests:**
    ```bash
     timeout 95s ./run_all_tests.sh --core-only --errors-only
    ```
-   **Run a Single Test:**
    There are two ways to run a single test:
    1.  Using the provided test scripts:
        ```bash
        ./tests/test_scripts/run_save_tests.sh --verbose
        ./tests/test_scripts/run_json_reader_tests.sh --verbose
        ```
    2.  Running the test executable directly with a filter:
        ```bash
        ./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
        ./bin/debug/json_reader_tests --run_test="TestBasicParsing"
        ```

## Code Style and Conventions

-   **File Structure:**
    -   Headers: `include/<feature>/*.hpp`
    -   Sources: `src/<feature>/*.cpp`
-   **Naming Conventions:**
    -   Classes: `PascalCase` (e.g., `GameEngine`, `GameStateManager`).
    -   Methods: `camelCase` (e.g., `handleEvents`, `setRunning`).
    -   Member Variables: `mp_` prefix for pointers (e.g., `mp_window`), `m_` for others (e.g., `m_windowWidth`).
-   **Key Architectural Concepts:**
    -   **Singleton Managers:** Many core systems are implemented as singletons (e.g., `GameEngine::Instance()`, `TextureManager::Instance()`).
    -   **Game State Machine:** A `GameStateManager` manages different game states (e.g., `MainMenuState`, `GamePlayState`).
    - **Event System:** A decoupled `EventManager` handles game-wide events.
    -   **Multithreading:** A `ThreadSystem` is used to manage background tasks for initialization and async operations.
    -   **Update/Render Synchronization:** The `GameEngine` orchestrates a two-thread (update/render) model. It uses a condition variable and atomic flags to create a synchronization point, ensuring all update logic (including manager-level tasks from the `ThreadSystem`) is complete before the render phase begins. This architecture inherently prevents data races between updates and rendering. Do not add redundant, manual synchronization calls within individual managers.
    -   **Resource Management:** `TextureManager`, `FontManager`, `SoundManager`, and `ResourceTemplateManager` handle loading and managing game assets.

## Important Files

-   `CMakeLists.txt`: The root CMake file defining the project, dependencies, and build settings.
-   `include/core/GameEngine.hpp`: The main engine class, acting as the central hub.
-   `run_all_tests.sh`: The script for executing the test suite.
-   `docs/`: Contains extensive documentation on various engine systems.
-   `res/`: Contains all game assets (images, fonts, sounds, data).

## Critical Workflow Reminder

**ALWAYS COMPILE BEFORE TESTING.**

After making any code changes, you must compile the project to ensure tests run against the latest version.

Use the following command to compile:
```bash
ninja -C build
```
For a more thorough check for warnings and errors during compilation, use:
```bash
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)"
```

## Critical InputManager SDL Cleanup Issue

**CRITICAL:** The InputManager has a very specific SDL gamepad subsystem cleanup pattern that must be maintained exactly as implemented. Do NOT modify this pattern without extreme caution.

**The Issue:** When no gamepads are detected during initialization, the SDL gamepad subsystem is still initialized via `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` but if not properly cleaned up, it causes a "trace trap" crash during `SDL_Quit()` on macOS.

**The Correct Pattern:**
1. In `initializeGamePad()`: Use `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` to initialize the subsystem
2. If no gamepads are found: Immediately call `SDL_QuitSubSystem(SDL_INIT_GAMEPAD)` before returning
3. If gamepads are found: Set `m_gamePadInitialized = true` and let normal cleanup handle it
4. In `clean()`: Only call `SDL_QuitSubSystem(SDL_INIT_GAMEPAD)` if `m_gamePadInitialized` is true

**What NOT to do:**
- Do NOT call `SDL_QuitSubSystem()` in both initialization and cleanup paths
- Do NOT use platform-specific `#ifdef` blocks to skip SDL cleanup
- Do NOT rely solely on `SDL_Quit()` to clean up subsystems if they were individually initialized
- Do NOT remove or modify the `m_gamePadInitialized` flag logic

This pattern has been broken multiple times by well-meaning "fixes" that cause crashes. The current implementation is correct and tested.
