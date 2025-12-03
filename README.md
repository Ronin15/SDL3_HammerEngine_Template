# SDL3 HammerEngine Template

A modern, production-ready C++20 SDL3 game engine template for 2D games. Built for rapid prototyping and scalable game development, it features robust multi-threading, high-performance AI, a professional UI system, and comprehensive resource and event management. Designed for cross-platform deployment (Windows, macOS, Linux) with a focus on performance, safety, and extensibility.

## Key Features

- **Modern C++20 & SDL3 Core**  
    
    Clean, modular codebase with strict style, memory, and type safety.

- **Adaptive Multi-Threading System**
   
   Hardware-adaptive thread pool with intelligent WorkerBudget allocation that scales from 1 to 16+ cores. Dynamic burst capacity (30% buffer reserve), queue pressure adaptation, and performance-based batch tuning that converges to optimal parallelism for your hardware. Priority-based scheduling (5 levels) with cache-line aligned atomics for minimal lock contention.

- **High-Performance AI System**  
    
    Cache-friendly, lock-free, and batch-processed AI manager. Supports 10K+ entities at 60+ FPS with only 4-6% CPU usage. Includes dynamic behaviors (Wander, Patrol, Guard, Flee, Attack, etc.), message system, and distance-based culling.

- **Robust Event & State Management**  
    
    Event-driven architecture with batch event processing, state machines for entities and game flow, and thread-safe manager updates.

- **Professional UI System**

    Content-aware auto-sizing, professional theming (light/dark/custom), and rich component library (buttons, labels, input fields, lists, modals, etc.). Responsive layouts with DPI-aware rendering and animation support. Centralized UI constants with resolution-aware scaling (1920×1080 baseline) and event-driven resize handling. Optimized for PC handheld devices (Steam Deck, ROG Ally, OneXPlayer) with automatic baseline resolution scaling down to 1280×720.

- **Automatic Resource Management**  
  
    JSON-based resource loading for items, materials, currency, and custom types. Handle-based runtime access for performance and extensibility.

- **Fast, Safe Serialization**
  
    Header-only binary serialization system with smart pointer memory management. Used by SaveGameManager for robust, versioned save/load across platforms.

- **Comprehensive Testing & Analysis**

    83+ test executables with Boost.Test framework covering unit, integration, and performance testing. Includes AI+Collision integration tests, SIMD correctness validation, and comprehensive thread safety verification with documented TSAN suppressions. Static analysis (cppcheck), AddressSanitizer (ASAN), ThreadSanitizer (TSAN), and Valgrind integration for production-ready quality assurance.

- **Cross-Platform Optimizations**
    
    Unified codebase with platform-specific enhancements: SIMD acceleration (x86-64: SSE2/AVX2, ARM64: NEON), macOS letterbox mode, Wayland detection, adaptive VSync, and DPI scaling.

- **Extensive Documentation**  
    
    Full guides, API references, best practices, and troubleshooting for all major systems.

### Why Choose HammerEngine Template?

- **Performance**: Engineered for cache efficiency, lock-free concurrency, and minimal CPU overhead—even with thousands of entities.
- **Safety**: Smart pointers, RAII, strong typing, and robust error handling throughout.
- **Extensibility**: Modular managers, clear APIs, and easy resource and UI customization.
- **Developer Experience**: Clean code, strict style, automated testing, and comprehensive docs.
- **Production-Ready Design**: Architecture and tooling designed for serious game development, with comprehensive testing infrastructure and performance validation.

---

**Get started building your next 2D game with a foundation that’s fast, safe, and ready for anything.**

---

## Quick Start

### Prerequisites

- CMake 3.28+, Ninja, C++20 compiler (GCC/Clang/MSVC) - MSVC planned
- Platforms: Windows, macOS (Apple Silicon only), Linux
- [SDL3 dependencies](https://wiki.libsdl.org/SDL3/README-linux) (image, ttf, mixer)
- Boost (for tests), cppcheck (static analysis), Valgrind (optional, for profiling and validation)

**Platform notes:**  
See [Platform Notes](docs/README.md#platform-notes) for detailed Windows, Linux, and macOS setup instructions.

### Build

```bash
git clone https://github.com/yourname/SDL3_HammerEngine_Template.git
cd SDL3_HammerEngine_Template
cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C build
./bin/debug/SDL3_Template
```

---

## Testing & Static Analysis

- Run all tests: `./run_all_tests.sh`
- See [tests/TESTING.md](tests/TESTING.md) for comprehensive test documentation and options
- Static analysis: `./tests/test_scripts/run_cppcheck_focused.sh`
  See [tests/cppcheck/README.md](tests/cppcheck/README.md) for more.
- Memory & thread safety validation: AddressSanitizer (ASAN) and ThreadSanitizer (TSAN) support
  See [docs/core/ThreadSystem.md#threadsanitizer-tsan-support](docs/core/ThreadSystem.md#threadsanitizer-tsan-support) for details

### Valgrind Analysis Suite
- Comprehensive memory, cache, and thread analysis with Valgrind
- Quick memory check: `./tests/valgrind/quick_memory_check.sh`
- Cache performance: `./tests/valgrind/cache_performance_analysis.sh`
- Function profiling: `./tests/valgrind/callgrind_profiling_analysis.sh`
- Thread analysis: `./tests/valgrind/thread_safety_check.sh`
- Full suite: `./tests/valgrind/run_complete_valgrind_suite.sh`
- See [tests/valgrind/README.md](tests/valgrind/README.md) for details, usage, and performance metrics

---

## Documentation

**📚 [Documentation Hub](docs/README.md)** – Full guides, API references, and best practices.

- **AI System:** [Overview](docs/ai/AIManager.md), [Optimization](docs/ai/AIManager_Optimization_Summary.md), [Behaviors](docs/ai/BehaviorModes.md), [Quick Reference](docs/ai/BehaviorQuickReference.md), [Pathfinding System](docs/ai/PathfindingSystem.md)
- **Collision & Physics:** [Collision System](docs/collisions/CollisionSystem.md)
- **Event System:** [Overview](docs/events/EventManager.md), [Quick Reference](docs/events/EventManager_QuickReference.md), [Advanced](docs/events/EventManager_Advanced.md), [Examples](docs/events/EventManager_Examples.cpp), [EventFactory](docs/events/EventFactory.md)
- **Threading:** [ThreadSystem](docs/core/ThreadSystem.md)
- **Managers:** [ParticleManager](docs/managers/ParticleManager.md), [FontManager](docs/managers/FontManager.md), [TextureManager](docs/managers/TextureManager.md), [SoundManager](docs/managers/SoundManager.md), [CollisionManager](docs/managers/CollisionManager.md), [PathfinderManager](docs/managers/PathfinderManager.md), [ResourceFactory](docs/managers/ResourceFactory.md), [ResourceTemplateManager](docs/managers/ResourceTemplateManager.md), [WorldManager](docs/managers/WorldManager.md), [WorldResourceManager](docs/managers/WorldResourceManager.md)
- **UI:** [UIManager Guide](docs/ui/UIManager_Guide.md), [UIConstants Reference](docs/ui/UIConstants.md), [Auto-Sizing](docs/ui/Auto_Sizing_System.md), [DPI-Aware Fonts](docs/ui/DPI_Aware_Font_System.md), [Minimap Implementation](docs/ui/Minimap_Implementation.md)
- **Utilities:** [JsonReader](docs/utils/JsonReader.md), [JSON Resource Loading](docs/utils/JSON_Resource_Loading_Guide.md), [Serialization](docs/utils/SERIALIZATION.md), [ResourceHandle System](docs/utils/ResourceHandle_System.md), [Performance Notes](hammer_engine_performance.md)
- **Development:** [Claude Code Skills](docs/development/ClaudeSkills.md)
- **Engine Plans & Issues:** [Camera Refactor Plan](docs/Camera_Refactor_Plan.md), [SDL3 macOS Cleanup Issue](docs/issues/SDL3_MACOS_CLEANUP_ISSUE.md)

---

## Core Design Principles

- **Memory Safety:** Smart pointers, RAII, no raw pointers
- **Performance:** Cache-friendly, batch processing, optimized threading
- **Type Safety:** Strong typing, compile-time and runtime validation
- **Cross-Platform:** Unified codebase, platform-specific optimizations

---

## Contributing

Contributions welcome!
- Report issues via GitHub with environment details and steps to reproduce.
- Fork, branch, test, and submit PRs with clear descriptions.

---

## Notes

- Window icon support for all platforms (see `res/img/`)
- Player and NPC controls: mouse, keyboard, controller (see `InputManager`)
- Template can be adapted for 3D (see `GameEngine.cpp` and `TextureManager`)
- For advanced usage, see [docs/README.md](docs/README.md)
- SDL3 is working great, but there are some issues with SDL3 Mixer. Mixer is requiring SDL3 main branch so issues fluctuate a bit. When SDL3 mixer gets a stable release, then things will calm down.
- This is a work in progress and Art is just a place holder for now. All Art is credited to its authors listed below in the Art section!

---

## Art

- World Tiles : [Pipoya](https://pipoya.itch.io/pipoya-rpg-tileset-32x32)
- Slimes [patvanmackelberg](https://opengameart.org/users/patvanmackelberg)
- Player Abigail [adythewolf](https://opengameart.org/users/adythewolf)

## License

[MIT License](LICENSE)
