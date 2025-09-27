# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Debug Build
```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
```

### Debug Build with Warning/Error Filtering
```bash
ninja -C build -v 2>&1 | grep -E "(warning|unused|error)" | head -n 100
```

### Release Build
```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Release  
ninja -C build
```

### Debug with AddressSanitizer
```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-D_GLIBCXX_DEBUG -fsanitize=address" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
ninja -C build
```

### Build System Notes
- **Output Directories**: Debug builds output to `bin/debug/`, Release builds to `bin/release/`

### Running the Application
- Debug: `./bin/debug/SDL3_Template`
- Release: `./bin/release/SDL3_Template`
- Testing: `timeout 60s ./bin/debug/SDL3_Template` (25 second timeout for behavior testing)

## Testing

**Framework**: Uses Boost.Test framework. Test binaries are placed in `bin/debug/` and write focused, thorough tests covering both success and error paths. Clean up test artifacts after execution.

**Extensive Test Suite**: The project includes 68+ specialized test executables covering all systems (AI, collision, pathfinding, threading, events, particles, resources, etc.). When working on a specific system, use targeted tests rather than the full suite for faster iteration.

### Run All Tests
```bash
./run_all_tests.sh --core-only --errors-only
```

**Note**: Use targeted tests when developing specific systems to avoid running the entire suite unnecessarily. Only run the full test suite when needed for comprehensive validation.

### Run Specific Tests
```bash
# Save/Load tests
./tests/test_scripts/run_save_tests.sh --verbose

# AI system tests
./tests/test_scripts/run_ai_optimization_tests.sh

# Thread safety tests
./tests/test_scripts/run_thread_tests.sh

# Individual test binaries
./bin/debug/SaveManagerTests --run_test="TestSaveAndLoad*"
```

### Static Analysis
```bash
./tests/test_scripts/run_cppcheck_focused.sh
```

### Performance Analysis (Valgrind)
```bash
# Quick memory check
./tests/valgrind/quick_memory_check.sh

# Cache performance analysis  
./tests/valgrind/cache_performance_analysis.sh

# Complete Valgrind suite
./tests/valgrind/run_complete_valgrind_suite.sh
```

## Code Architecture

### Core Components 
- **GameEngine**: Central coordinator managing all subsystems with double-buffered rendering
- **GameLoop**: Fixed-timestep game loop with separate update/render threads
- **Managers**: Singleton pattern with `m_isShutdown` guard for clean shutdown
- **ThreadSystem**: Production-grade thread pool with WorkerBudget priority system
- **Logger**: Thread-safe logging with macros for consistent usage

### Key Systems
- **AI Manager**: High-performance batch-processed AI supporting 10K+ entities at 60+ FPS
- **Event Manager**: Thread-safe event-driven architecture with batch processing
- **Resource Management**: JSON-based loading with handle-based runtime access
- **UI Manager**: Professional theming with auto-sizing and DPI awareness
- **Collision Manager**: Spatial hash-based collision detection with pathfinding integration
- **Particle Manager**: Camera-aware, batched particle rendering with lifetime Management
- **World Manager**: Tile-based world with procedural generation and efficient Rendering
- **Input Manager**: Comprehensive input handling for keyboard, mouse, and gamepads

### Utilities
- **Camera**: World-to-screen coordinate conversion with zoom and pattern
- **Vector2D**: 2D vector math with common operations
- **JSON Parsing**: JsonReader, Lightweight JSON handling for configuration and resources
- **BinarySerializer**: Cross-platform file save/reading/writing with error handling

### Module Organization
```
src/
├── core/           # GameEngine, GameLoop, ThreadSystem, Logger
├── managers/       # All manager classes (AI, Event, Collision, etc.)
├── gameStates/     # Game state implementations
├── entities/       # Player, NPC, Entity base classes
├── events/         # Event system and event types
├── ai/             # AI behaviors and pathfinding
├── collisions/     # Collision detection and spatial partitioning
├── utils/          # Utilities, Camera, Vector2D
└── world/          # World generation and management

include/            # Public headers mirroring src/ structure
tests/              # Boost.Test framework with test scripts
res/                # Game assets (fonts, images, audio, data files)
```

## Coding Standards

### C++ Standards
- **Language**: C++20
- **Style**: 4-space indentation, Allman-style braces
- **Memory**: RAII with smart pointers, no raw new/delete
- **Threading**: Use ThreadSystem, avoid raw std::thread
- **Error Handling**: Prefer exceptions for critical errors, return error codes for expected failures
- **Comments**: Doxygen-style for public APIs, inline comments for complex logic
- **logging**: Use provided logger.hpp macros for consistent logging
- **Cross-Platform**: Use platform guards for OS-specific code, avoid hardcoding paths, and make sure code is portable.
- **Copyright**: All files must include the standard copyright header:
  ```cpp
  /* Copyright (c) 2025 Hammer Forged Games
   * All rights reserved.
   * Licensed under the MIT License - see LICENSE file for details
  */
  ```

### Naming Conventions
- **Classes/Enums**: UpperCamelCase (`GameEngine`, `EventType`)
- **Functions/Variables**: lowerCamelCase (`updateGame`, `playerHealth`) 
- **Member Variables**: `m_` prefix (`m_isRunning`, `m_playerPosition`), `mp_` prefix for pointers (`mp_window`, `mp_renderer`)
- **Constants**: ALL_CAPS (`MAX_PLAYERS`, `DEFAULT_SPEED`)

### Header/Implementation Guidelines
- **Headers**: Minimal, stable interface with forward declarations
- **Implementation**: All non-trivial logic goes in .cpp files
- **Inline**: Only trivial 1-2 line accessors and getters
- **Dependencies**: Avoid leaking implementation details through headers

### Threading Architecture
- **Update Loop**: Thread-safe via mutex, fixed timestep
- **Render Loop**: Main thread only, double-buffered
- **Background Work**: Use ThreadSystem with WorkerBudget priorities
- **No Cross-Thread Rendering**: All drawing happens on main thread

### Performance Guidelines
- **STL Algorithms**: Prefer STL algorithms (`std::sort`, `std::find_if`, `std::transform`) over manual loops for better optimization
- **Platform Guards**: Use platform-specific logic guards (`#ifdef __APPLE__`, `#ifdef WIN32`) for OS-specific code
- **Avoid Premature Optimization**: Focus on clean, maintainable code first; optimize hotspots based on profiling data

### Optional
- Boost (for testing framework)
- cppcheck (static analysis)
- Valgrind (performance/memory analysis)

## Key Performance Notes

### Memory Management  
- All dynamic allocation uses smart pointers
- RAII patterns throughout for resource management
- Binary serialization system for save/load operations

### Rendering Pipeline
- Double-buffered rendering with buffer swapping
- Camera-aware rendering with consistent world-to-screen conversion
- Batched particle and UI rendering

## Important Implementation Details

### GameEngine Update/Render Flow

**GameLoop Architecture**: Drives three callbacks — events (main thread), fixed-timestep updates, and rendering. Target FPS and fixed timestep are configured in `HammerMain.cpp` via `GameLoop`.

**Update Phase (thread-safe)**: `GameEngine::update(deltaTime)` runs under a mutex to guarantee completion before any render. It updates global systems (AIManager, EventManager, ParticleManager), then delegates to the current `GameStateManager::update`.

**Double Buffering**: `GameEngine` maintains `m_currentBufferIndex` (update) and `m_renderBufferIndex` (render) with `m_bufferReady[]`. The main loop calls `hasNewFrameToRender()` and `swapBuffers()` before each update, allowing render to consume a stable buffer from the previous tick.

**Render Phase (main thread)**: `GameEngine::render()` clears the renderer and calls `GameStateManager::render()`. States render world, entities, particles, and UI in a deterministic order using the current camera view.

**Threading Guidelines**:
- No rendering from background threads. AI/particles may schedule work but all drawing occurs during `GameEngine::render()` on the main thread.
- Do not introduce additional synchronization between managers for rendering; rely on `GameEngine`'s mutexed update and double-buffer swap.
- When adding a new state, snapshot camera/view once per render pass and reuse it for all world-space systems.
- **NEVER use static variables in threaded code** - they create race conditions and data corruption. Use instance variables, thread_local storage, or atomic operations instead.

This architecture supports rapid prototyping while maintaining production-ready performance and code quality.
