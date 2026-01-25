# SDL3 HammerEngine Template

A modern, production-ready C++20 SDL3 game engine template for 2D games. Built for rapid prototyping and scalable game development, it features Data-Oriented Design with EntityDataManager as the central data authority, robust multi-threading, high-performance AI supporting 10K+ entities, a professional UI system, and comprehensive resource and event management. Designed for cross-platform deployment (Windows, macOS, Linux) with a focus on performance, safety, and extensibility.

## Key Features

- **Modern C++20 & SDL3 Core**

    Clean, modular codebase with strict style, memory, and type safety.

- **Rendering & Engine Core**

    Fixed timestep game loop with smooth interpolation at any display refresh rate. Adaptive VSync, sprite sheet animations, particle effects with camera-aware culling, and a smooth-following camera system with world bounds clamping.

- **Adaptive Multi-Threading System**

   Hardware-adaptive thread pool with intelligent WorkerBudget batch optimization. Automatically detects logical cores (including SMT/hyperthreading) and reserves one to reduce OS contention. Sequential manager execution gives each system ALL workers during its update window. Throughput-based hill-climbing converges to optimal batch sizes for your hardware. Priority-based scheduling (5 levels) with cache-line aligned atomics for minimal lock contention.

- **High-Performance AI System**

    Data-Oriented Design with EntityDataManager as single source of truth. Cache-friendly, lock-free, batch-processed AI using Structure-of-Arrays storage. Supports 10K+ entities at 60+ FPS with only 4-6% CPU usage. Includes dynamic behaviors (Wander, Patrol, Guard, Flee, Attack, etc.), simulation tiers (Active/Background/Hibernated), and distance-based culling.

- **Robust Event & State Management**  
    
    Event-driven architecture with batch event processing, state machines for entities and game flow, and thread-safe manager updates.

- **Flexible UI System**

    Content-aware auto-sizing, professional theming (light/dark/custom), and rich component library (buttons, labels, input fields, lists, modals, etc.). Responsive layouts with DPI-aware rendering and animation support. Centralized UI constants with resolution-aware scaling (1920×1080 baseline) and event-driven resize handling. Optimized for PC handheld devices (Steam Deck, ROG Ally, OneXPlayer) with automatic baseline resolution scaling down to 1280×720.

- **Automatic Resource Management**  
  
    JSON-based resource loading for items, materials, currency, and custom types. Handle-based runtime access for performance and extensibility.

- **Fast, Safe Serialization**
  
    Header-only binary serialization system with smart pointer memory management. Used by SaveGameManager for robust, versioned save/load across platforms.

- **Comprehensive Testing & Analysis**

    55+ test executables with Boost.Test framework covering unit, integration, and performance testing. Includes AI+Collision integration tests, SIMD correctness validation, and comprehensive thread safety verification with documented TSAN suppressions. Static analysis (cppcheck, clang-tidy), AddressSanitizer (ASAN), ThreadSanitizer (TSAN), and Valgrind integration for production-ready quality assurance.

- **Cross-Platform Optimizations**

    Unified codebase with platform-specific enhancements: SIMD acceleration (x86-64: SSE2/AVX2, ARM64: NEON), macOS Retina support with borderless fullscreen, Wayland detection, adaptive VSync, and native DPI scaling.

- **GameTime & World Simulation**

    Fantasy calendar system with day/night cycles, four seasons, dynamic weather, and temperature simulation. Event-driven controllers for time-based gameplay.

- **Chunk-Based World System**

    Efficient tile-based world rendering with chunk culling for off-screen optimization. Supports procedural generation, seamless streaming, and automatic seasonal tile switching.

- **Robust Combat System**

    Dedicated `CombatController` handles all combat logic, including hit detection, damage calculation, and status effects. Integrated with entity state machines and event system for dynamic combat scenarios.

- **Power Efficient (Race-to-Idle)**

    Optimized for battery-powered devices. Completes frame work quickly then sleeps until vsync, achieving 80%+ CPU idle residency during active gameplay. See [Power Efficiency](docs/performance/PowerEfficiency.md) for detailed benchmarks.

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

- CMake 3.28+, Ninja, C++20 compiler (GCC/Clang) - MSVC support planned
- Platforms: Linux, macOS (Apple Silicon optimized, Intel supported), Windows (MinGW)
- [SDL3 dependencies](https://wiki.libsdl.org/SDL3/README-linux) (ttf, mixer)
- Boost (for tests), cppcheck & clang-tidy (static analysis), Valgrind (optional, Linux only)

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
- **Runtime analysis with Profile build** (Valgrind-compatible optimized):
  ```bash
  cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Profile && ninja -C build
  ./tests/valgrind/runtime_cache_analysis.sh --profile 300   # MPKI analysis
  ./tests/valgrind/runtime_memory_analysis.sh --profile 300  # Memory analysis
  ```
- See [tests/valgrind/README.md](tests/valgrind/README.md) for details, usage, and performance metrics

---

## Documentation

**📚 [Documentation Hub](docs/README.md)** – Full guides, API references, and best practices.

- **Core:** [GameEngine](docs/core/GameEngine.md), [GameTimeManager](docs/managers/GameTimeManager.md), [ThreadSystem](docs/core/ThreadSystem.md), [TimestepManager](docs/managers/TimestepManager.md)
- **AI System:** [Overview](docs/ai/AIManager.md), [Optimization](docs/ai/AIManager_Optimization_Summary.md), [Behaviors](docs/ai/BehaviorModes.md), [Quick Reference](docs/ai/BehaviorQuickReference.md), [Pathfinding System](docs/ai/PathfindingSystem.md)
- **Collision & Physics:** [CollisionManager](docs/managers/CollisionManager.md)
- **Entity System:** [Overview](docs/entities/README.md), [EntityHandle](docs/entities/EntityHandle.md), [EntityDataManager](docs/managers/EntityDataManager.md), [BackgroundSimulationManager](docs/managers/BackgroundSimulationManager.md)
- **Event System:** [Overview](docs/events/EventManager.md), [Quick Reference](docs/events/EventManager_QuickReference.md), [Advanced](docs/events/EventManager_Advanced.md), [TimeEvents](docs/events/TimeEvents.md), [EventFactory](docs/events/EventFactory.md)
- **Controllers:** [Overview](docs/controllers/README.md), [ControllerRegistry](docs/controllers/ControllerRegistry.md), [WeatherController](docs/controllers/WeatherController.md), [DayNightController](docs/controllers/DayNightController.md), [CombatController](docs/controllers/CombatController.md)
- **Managers:** [BackgroundSimulationManager](docs/managers/BackgroundSimulationManager.md), [CollisionManager](docs/managers/CollisionManager.md), [EntityDataManager](docs/managers/EntityDataManager.md), [FontManager](docs/managers/FontManager.md), [ParticleManager](docs/managers/ParticleManager.md), [PathfinderManager](docs/managers/PathfinderManager.md), [ResourceFactory](docs/managers/ResourceFactory.md), [ResourceTemplateManager](docs/managers/ResourceTemplateManager.md), [SoundManager](docs/managers/SoundManager.md), [TextureManager](docs/managers/TextureManager.md), [WorldManager](docs/managers/WorldManager.md), [WorldResourceManager](docs/managers/WorldResourceManager.md)
- **UI:** [UIManager Guide](docs/ui/UIManager_Guide.md), [UIConstants Reference](docs/ui/UIConstants.md), [Auto-Sizing](docs/ui/Auto_Sizing_System.md), [DPI-Aware Fonts](docs/ui/DPI_Aware_Font_System.md), [Minimap Implementation](docs/ui/Minimap_Implementation.md)
- **Utilities:** [SceneRenderer](docs/utils/SceneRenderer.md), [Camera](docs/utils/Camera.md), [JsonReader](docs/utils/JsonReader.md), [JSON Resource Loading](docs/utils/JSON_Resource_Loading_Guide.md), [Serialization](docs/utils/SERIALIZATION.md), [ResourceHandle System](docs/utils/ResourceHandle_System.md)
- **Architecture:** [Interpolation System](docs/architecture/InterpolationSystem.md)
- **Performance:** [Power Efficiency](docs/performance/PowerEfficiency.md), [EntityDataManager Power Analysis](docs/performance_reports/power_profile_edm_comparison_2026-01-24.md)
- **Development:** [Claude Code Skills](docs/development/ClaudeSkills.md)
- **Engine Plans & Issues:** [Camera Refactor Plan](docs/Camera_Refactor_Plan.md), [SDL3 macOS Cleanup Issue](docs/issues/SDL3_MACOS_CLEANUP_ISSUE.md)

---

## Core Design Principles

- **Data-Oriented Design:** EntityDataManager as single source of truth, Structure-of-Arrays (SOA) storage.
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
- This is a work in progress and Art is just a place holder for now. All Art is credited to its authors listed below in the Art section!

---

## Art
All art license follows artists licensing. See thier page below for more details!
- World Tiles/Assets : [Pipoya](https://pipoya.itch.io/pipoya-rpg-tileset-32x32)
- Slimes [patvanmackelberg](https://opengameart.org/users/patvanmackelberg)
- Player Abigail [adythewolf](https://opengameart.org/users/adythewolf)
- World/Various Ore deposits/ore/bars/gems [Senmou](https://opengameart.org/users/senmou)

## License

[MIT License](LICENSE)
