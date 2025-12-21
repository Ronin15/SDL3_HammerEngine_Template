# GEMINI.md - SDL3 HammerEngine Template

This document provides a comprehensive overview of the SDL3 HammerEngine Template project, intended as a guide for Gemini and other AI agents.

## Project Overview

The SDL3 HammerEngine Template is a modern, production-ready C++20 game engine template for 2D games. It is built for rapid prototyping and scalable game development, with a strong focus on performance, safety, and extensibility. The engine is designed for cross-platform deployment on Windows, macOS, and Linux.

### Key Features

*   **Modern C++20 & SDL3 Core:** A clean, modular codebase with a focus on memory and type safety.
*   **Adaptive Multi-Threading System:** A hardware-adaptive thread pool that scales from 1 to 16+ cores, with priority-based scheduling and cache-line aligned atomics for minimal lock contention.
*   **High-Performance AI System:** A cache-friendly, lock-free, and batch-processed AI manager that supports over 10,000 entities at 60+ FPS.
*   **Robust Event & State Management:** An event-driven architecture with batch event processing and state machines for entities and game flow.
*   **Professional UI System:** A content-aware, themeable UI system with a rich component library and responsive layouts.
*   **Automatic Resource Management:** JSON-based resource loading and handle-based runtime access for performance and extensibility.
*   **Comprehensive Testing & Analysis:** Over 83 test executables using the Boost.Test framework, with support for static analysis, address sanitization, and thread sanitization.

### Architecture

The engine is built around a set of singleton managers that handle different aspects of the game:

*   **`GameEngine`:** The core of the engine, responsible for initializing the engine, running the game loop, and managing the other managers.
*   **`ThreadSystem`:** A sophisticated, header-only thread management system that provides a thread pool, prioritized task queue, and batch enqueueing for performance.
*   **`AIManager`:** A high-performance, data-oriented AI manager that uses a structure of arrays (SoA) for entity data, batch processing, and SIMD optimizations, supporting various behaviors like Attack, Flee, Follow, Guard, Chase, Idle, and Patrol.
*   **`EventManager`:** A robust and feature-rich event management system that uses type-indexed storage, event handlers, and a `WorkerBudget` system for multi-threaded event processing.
*   **Controllers:** State-scoped helpers that control specific system behaviors. Unlike Managers (global lifecycle, own data), Controllers subscribe per-GameState and contain game logic without owning data, e.g., CombatController for handling in-game combat interactions.
*   **Other Managers:** The engine also includes managers for input, textures, sounds, fonts, game states, UI, and a comprehensive `GameTimeManager` that works with `DayNightController` and `WeatherController` to simulate dynamic world conditions.

## Building and Running

### Prerequisites

*   CMake 3.28+
*   Ninja
*   A C++20 compliant compiler (GCC, Clang, or MSVC)
*   SDL3 dependencies (image, ttf, mixer)
*   Boost (for tests)
*   cppcheck (for static analysis)
*   Valgrind (optional, for profiling)

### Build Instructions

*   **Debug Build:**
    ```bash
    cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C build
    ```
*   **Release Build:**
    ```bash
    cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build
    ```
*   **Debug with AddressSanitizer (ASAN):**
    ```bash
    cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" -DUSE_MOLD_LINKER=OFF && ninja -C build
    ```
*   **Debug with ThreadSanitizer (TSAN):**
    ```bash
    cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=thread -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" -DUSE_MOLD_LINKER=OFF && ninja -C build
    ```

### Running the Application

```bash
./bin/debug/SDL3_Template
```

## Testing and Static Analysis

*   **Run all tests:**
    ```bash
    ./run_all_tests.sh --core-only --errors-only
    ```
*   **Run targeted tests:**
    ```bash
    ./tests/test_scripts/run_save_tests.sh --verbose
    ./tests/test_scripts/run_ai_optimization_tests.sh
    ./tests/test_scripts/run_thread_tests.sh
    ./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
    ```
*   **Run static analysis:**
    ```bash
    ./tests/test_scripts/run_cppcheck_focused.sh
    ```
*   **Run tests with TSAN suppressions:**
    ```bash
    export TSAN_OPTIONS="suppressions=$(pwd)/tsan_suppressions.txt"
    ./bin/debug/thread_safe_ai_manager_tests
    ```

## Development Workflow

**ALWAYS Check Established Patterns First:** Before implementing new features, search the codebase for existing patterns. Don't reinvent solutions that already exist.

**Pattern Discovery**:
```bash
# UI positioning/components
grep -rn "createEventLog\|BOTTOM_\|createOverlay" src/gameStates/
grep -n "enum class UIPositionMode" include/managers/UIManager.hpp

# Reference states: EventDemoState (event log, bottom-left), UIDemoState (full UI showcase),
# SettingsMenuState (centered buttons, tabs), MainMenuState (simple menus)

# Manager patterns
grep -rn "class.*Manager" include/managers/
```

## UI Positioning

**CRITICAL:** Always call `setComponentPositioning()` after creating UI components to ensure proper fullscreen/resize behavior.

**Helper methods** (auto-position):
- `createTitleAtTop()` - Page titles
- `createButtonAtBottom()` - Back buttons
- `createCenteredButton()` - Menu buttons
- `createCenteredDialog()` - Modal dialogs

**Manual pattern** (everything else):
```cpp
ui.createButton("my_btn", {100, 200, 120, 40}, "Click");
ui.setComponentPositioning("my_btn", {UIPositionMode::TOP_ALIGNED, 100, 200, 120, 40});
```

**Common modes**: TOP_ALIGNED, BOTTOM_ALIGNED, LEFT_ALIGNED, RIGHT_ALIGNED, BOTTOM_RIGHT, CENTERED_H, CENTERED_BOTH. See `include/managers/UIManager.hpp` for a full list.


## Development Conventions

*   **C++20:** The project uses modern C++20 features and best practices.
*   **Data-Oriented Design:** The engine favors data-oriented design patterns, such as the structure of arrays (SoA) used in the `AIManager`, to improve cache performance.
*   **Performance:** Performance is a key focus of the engine. This is evident in the use of multi-threading, batch processing, SIMD optimizations, and other performance-oriented features.
*   **Singleton Managers:** The engine is built around a set of singleton managers. This provides a single point of access to each manager and ensures that there is only one instance of each manager.
*   **Event-Driven Architecture:** The engine is built on an event-driven architecture, with the `EventManager` as the central hub.
*   **Header-Only Libraries:** Some parts of the engine, like the `ThreadSystem`, are implemented as header-only libraries for ease of use and to reduce compilation times.
*   **Parameter Passing:** **ALWAYS prefer references over copies.** Use `const T&` for read-only access to non-trivial objects. Use `T&` for mutation. Pass by value only for primitives or intentional ownership transfer.
*   **String Parameters**: Use `const std::string&` for map lookups/storage (zero-copy). Use `string_view` only for return types of literals or `constexpr` constants. **NEVER** convert `string_view` → `std::string` for lookups.
*   **Naming Conventions:** `UpperCamelCase` for classes/enums, `lowerCamelCase` for functions/vars, `m_` prefix for members, `mp_` for pointers, and `ALL_CAPS` for constants.
*   **Headers**: `.hpp` for C++, `.h` for C | Minimal interface, forward declarations | Non-trivial logic in .cpp | Inline only for trivial 1-2 line accessors
*   **Threading**: Update/Render (sequential on main thread, fixed timestep) | Background (ThreadSystem + WorkerBudget for AI, Particle, Event, Pathfinding) | **NEVER static vars in threaded code** (use instance vars, thread_local, or atomics)
*   **Logging**: Always use `std::format()` for logging with dynamic values. Never use string concatenation (`+` operator) with `std::to_string()` - it creates multiple heap allocations per log call.
*   **Extensive Documentation:** The project has extensive documentation in the `docs` directory, which should be consulted for more detailed information about the engine's systems.

## Memory Management

**Avoid Per-Frame Allocations:** Reuse buffers across frames to avoid heap allocations, which can cause frame dips. Use member variables for hot-path buffers, `clear()` instead of reconstruction, and `reserve()` before loops.

**Buffer Reuse Pattern**:
```cpp
// BAD: Allocates/deallocates 128KB every frame
void update() {
    std::vector<Data> buffer;  // Fresh allocation
    buffer.reserve(entityCount);
    // ... use buffer
}  // Deallocation

// GOOD: Reuses capacity across frames
class Manager {
    std::vector<Data> m_reusableBuffer;  // Member variable

    void update() {
        m_reusableBuffer.clear();  // Keeps capacity, no dealloc
        // ... use m_reusableBuffer
    }
};
```

**Pre-allocation**: Always `reserve()` when size is known to avoid incremental reallocations:
```cpp
std::vector<Entity> entities;
entities.reserve(expectedCount);  // Single allocation
for (size_t i = 0; i < expectedCount; ++i) {
    entities.push_back(data[i]);  // No reallocs
}
```

## Cross-Platform SIMD Optimizations

The engine uses a cross-platform SIMD abstraction layer for x86-64 (SSE2/AVX2) and ARM64 (NEON). Always provide a scalar fallback and handle non-multiple-of-4 with a tail loop. See `AIManager::calculateDistancesSIMD()` for reference implementation.


## Rendering Rules

**Critical for SDL3_Renderer:** Requires **exactly one `Present()` per frame** through a unified render path.

*   **NEVER call `SDL_RenderClear()` or `SDL_RenderPresent()` directly in `GameState` classes.** All rendering must go through the `GameEngine::render()` -> `GameStateManager::render()` -> `GameState::render()` flow.
*   Use the `LoadingState` for async operations, never blocking with manual rendering.
*   Use a deferred pattern for state transitions from `enter()` to avoid timing issues.
```cpp
// Configure LoadingState before transition
auto* loadingState = dynamic_cast<LoadingState*>(gameStateManager->getState("LoadingState").get());
loadingState->configure("TargetStateName", worldConfig);
gameStateManager->changeState("LoadingState");

// LoadingState handles async world generation on ThreadSystem
// Progress bar renders through normal GameEngine::render() flow
// No manual SDL_RenderClear/Present calls needed
```

**Deferred State Transitions**: State changes from `enter()` cause timing issues. Use deferred pattern:
```cpp
bool GameState::enter() {
    if (!m_worldLoaded) {
        m_needsLoading = true;    // Set flag
        m_worldLoaded = true;      // Prevent loop
        return true;               // Exit early
    }
    // Normal initialization when world loaded
}

void GameState::update(float deltaTime) {
    if (m_needsLoading) {
        m_needsLoading = false;
        // Configure and transition to LoadingState here
        return;
    }
    // Normal update
}
```