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

-   **Build (Debug):**
    ```bash
    ninja -C build -v 2>&1 | grep -E "(warning|unused|error)"
    ```
-   **Build (Release):**
    ```bash
    cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja -C build
    ```
-   **Output Directories:**
    -   Debug builds: `bin/debug/`
    -   Release builds: `bin/release/`
-   **Compile Commands:** `compile_commands.json` is automatically generated and copied to the project root for tooling support (e.g., `clangd`).

## Testing

-   **Run All Tests:**
    ```bash
    ./run_all_tests.sh --core-only
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
    -   **Event System:** A decoupled `EventManager` handles game-wide events.
    -   **Multithreading:** A `ThreadSystem` is used to manage background tasks for initialization and async operations.
    -   **Resource Management:** `TextureManager`, `FontManager`, `SoundManager`, and `ResourceTemplateManager` handle loading and managing game assets.

## Important Files

-   `CMakeLists.txt`: The root CMake file defining the project, dependencies, and build settings.
-   `include/core/GameEngine.hpp`: The main engine class, acting as the central hub.
-   `run_all_tests.sh`: The script for executing the test suite.
-   `docs/`: Contains extensive documentation on various engine systems.
-   `res/`: Contains all game assets (images, fonts, sounds, data).
