# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Debug Build
```bash
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
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

### Running the Application
- Debug: `./bin/debug/SDL3_Template`
- Release: `./bin/release/SDL3_Template`
- Testing: `timeout 25s ./bin/debug/SDL3_Template` (25 second timeout for behavior testing)

## Testing

### Run All Tests
```bash
./run_all_tests.sh --core-only --errors-only
```

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

### Key Systems
- **AI System**: High-performance batch-processed AI supporting 10K+ entities at 60+ FPS
- **Event System**: Thread-safe event-driven architecture with batch processing
- **Resource Management**: JSON-based loading with handle-based runtime access
- **UI System**: Professional theming with auto-sizing and DPI awareness
- **Collision System**: Spatial hash-based collision detection with pathfinding integration

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
```

## Coding Standards

### C++ Standards
- **Language**: C++20
- **Style**: 4-space indentation, Allman-style braces
- **Memory**: RAII with smart pointers, no raw new/delete
- **Threading**: Use ThreadSystem, avoid raw std::thread

### Naming Conventions
- **Classes/Enums**: UpperCamelCase (`GameEngine`, `EventType`)
- **Functions/Variables**: lowerCamelCase (`updateGame`, `playerHealth`) 
- **Member Variables**: `m_` prefix (`m_isRunning`, `m_playerPosition`)
- **Constants**: ALL_CAPS (`MAX_PLAYERS`, `DEFAULT_SPEED`)

### Header/Implementation Guidelines
- **Headers**: Minimal, stable interface with forward declarations
- **Implementation**: All non-trivial logic goes in .cpp files
- **Inline**: Only trivial 1-2 line accessors and getters
- **Dependencies**: Avoid leaking implementation details through headers

### Manager Pattern
All managers follow this singleton pattern:
```cpp
class ExampleManager {
private:
    static std::unique_ptr<ExampleManager> m_instance;
    std::atomic<bool> m_isShutdown{false};
    
public:
    static ExampleManager& getInstance();
    static void shutdown();
};
```

### Threading Architecture
- **Update Loop**: Thread-safe via mutex, fixed timestep
- **Render Loop**: Main thread only, double-buffered
- **Background Work**: Use ThreadSystem with WorkerBudget priorities
- **No Cross-Thread Rendering**: All drawing happens on main thread

### Logging
Use provided macros for consistent logging:
- `GAMEENGINE_ERROR(message)`
- `GAMEENGINE_WARN(message)`  
- `GAMEENGINE_INFO(message)`

## Dependencies

### Required
- CMake 3.28+, Ninja, C++20 compiler
- SDL3 (fetched automatically via FetchContent)
- SDL3_image, SDL3_ttf, SDL3_mixer (fetched automatically)

### Optional
- Boost (for testing framework)
- cppcheck (static analysis)
- Valgrind (performance/memory analysis)

## Platform Support

### Cross-Platform Features
- **macOS**: Optimized build flags, dSYM generation, letterbox mode
- **Linux**: Wayland detection, adaptive VSync
- **Windows**: Console output control, DLL management (planned)

### Debug vs Release
- **Debug**: Console output enabled, full debug symbols, no optimization
- **Release**: Optimized builds, LTO enabled, platform-specific flags

## Key Performance Notes

### AI System
- Designed for 10K+ entities at 60+ FPS with 4-6% CPU usage
- Uses cache-friendly batch processing and distance-based culling
- Lock-free design with spatial partitioning

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
1. **Update**: Mutex-protected, updates all managers and current game state
2. **Buffer Swap**: `hasNewFrameToRender()` and `swapBuffers()` coordinate double buffering  
3. **Render**: Main thread renders world, entities, particles, UI using stable buffer

### Entity Rendering
- Use `Entity::render(const Camera*)` for world-to-screen conversion
- Do not compute per-entity camera offsets outside this pattern
- Camera view is computed once per render pass and reused

### Resource Loading
- JSON-based configuration for items, materials, currency
- ResourceTemplateManager handles template loading and instantiation
- Handle-based access pattern for performance and safety

This architecture supports rapid prototyping while maintaining production-ready performance and code quality.