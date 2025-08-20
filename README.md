# SDL3 HammerEngine Template

A modern, production-ready C++20 SDL3 game engine template for 2D games. Built for rapid prototyping and scalable game development, it features robust multi-threading, high-performance AI, a professional UI system, and comprehensive resource and event management. Designed for cross-platform deployment (Windows, macOS, Linux) with a focus on performance, safety, and extensibility.

## Key Features

- **Modern C++20 & SDL3 Core**  
  Clean, modular codebase with strict style, memory, and type safety.

- **Advanced Multi-Threading**  
  Production-grade thread pool with priority-based scheduling and WorkerBudget system for intelligent, hardware-adaptive resource allocation. Lock-free double buffering and batch task submission for optimal concurrency.

- **High-Performance AI System**  
  Cache-friendly, lock-free, and batch-processed AI manager. Supports 10K+ entities at 60+ FPS with only 4-6% CPU usage. Includes dynamic behaviors (Wander, Patrol, Guard, Flee, Attack, etc.), message system, and distance-based culling.

- **Robust Event & State Management**  
  Event-driven architecture with batch event processing, state machines for entities and game flow, and thread-safe manager updates.

- **Professional UI System**  
  Content-aware auto-sizing, professional theming (light/dark/custom), and a rich set of components (buttons, labels, input fields, lists, modals, etc.). Responsive, DPI-aware, and cross-platform with layout and animation support.

- **Automatic Resource Management**  
  JSON-based resource loading for items, materials, currency, and custom types. Handle-based runtime access for performance and extensibility.

- **Fast, Safe Serialization**  
  Header-only, cross-platform binary serialization system with smart pointer memory management. Used by SaveGameManager for robust, versioned save/load.

- **Comprehensive Testing & Analysis**  
  Extensive Boost.Test suite, static analysis (cppcheck), and Valgrind integration for performance, memory, and thread safety validation.

- **Cross-Platform Optimizations**  
  Unified codebase with platform-specific enhancements: macOS letterbox mode, Wayland detection, adaptive VSync, and DPI scaling.

- **Extensive Documentation**  
  Full guides, API references, best practices, and troubleshooting for all major systems.

### Why Choose HammerEngine Template?

- **Performance**: Engineered for cache efficiency, lock-free concurrency, and minimal CPU overhead—even with thousands of entities.
- **Safety**: Smart pointers, RAII, strong typing, and robust error handling throughout.
- **Extensibility**: Modular managers, clear APIs, and easy resource and UI customization.
- **Developer Experience**: Clean code, strict style, automated testing, and comprehensive docs.
- **Production-Ready**: Used for real-world projects, with proven reliability and scalability.

---

**Get started building your next 2D game with a foundation that’s fast, safe, and ready for anything.**

---

## Quick Start

### Prerequisites

- CMake 3.28+, Ninja, C++20 compiler (GCC/Clang/MSVC) - MSVC planned
- [SDL3 dependencies](https://wiki.libsdl.org/SDL3/README-linux) (image, ttf, mixer)
- Boost (for tests), cppcheck (for static analysis)

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
- See [tests/TESTING.md](tests/TESTING.md) for test details and options
- Static analysis: `./tests/test_scripts/run_cppcheck_focused.sh`  
  See [tests/cppcheck/README.md](tests/cppcheck/README.md) for more.

### Valgrind Analysis Suite
- Comprehensive memory, cache, and thread safety analysis with Valgrind
- Quick memory check: `./tests/valgrind/quick_memory_check.sh`
- Cache performance: `./tests/valgrind/cache_performance_analysis.sh`
- Function profiling: `./tests/valgrind/callgrind_profiling_analysis.sh`
- Thread safety: `./tests/valgrind/thread_safety_check.sh`
- Full suite: `./tests/valgrind/run_complete_valgrind_suite.sh`
- See [tests/valgrind/README.md](tests/valgrind/README.md) for details, usage, and performance metrics

---

## Documentation

**📚 [Documentation Hub](docs/README.md)** – Full guides, API references, and best practices.

- **AI System:** [Overview](docs/ai/AIManager.md), [Optimization](docs/ai/AIManager_Optimization_Summary.md), [Behaviors](docs/ai/BehaviorModes.md)
- **Event System:** [Overview](docs/events/EventManager.md), [Quick Reference](docs/events/EventManager_QuickReference.md), [Advanced](docs/events/EventManager_Advanced.md), [Examples](docs/events/EventManager_Examples.cpp), [EventFactory](docs/events/EventFactory.md)
- **Threading:** [ThreadSystem](docs/ThreadSystem.md)
- **Managers:** [ParticleManager](docs/managers/ParticleManager.md), [FontManager](docs/managers/FontManager.md), [TextureManager](docs/managers/TextureManager.md), [SoundManager](docs/managers/SoundManager.md), [ResourceTemplateManager](docs/managers/ResourceTemplateManager.md), [WorldResourceManager](docs/managers/WorldResourceManager.md)
- **UI:** [UIManager Guide](docs/ui/UIManager_Guide.md), [Auto-Sizing](docs/ui/Auto_Sizing_System.md), [DPI-Aware Fonts](docs/ui/DPI_Aware_Font_System.md)
- **Utilities:** [JsonReader](docs/utils/JsonReader.md), [JSON Resource Loading](docs/utils/JSON_Resource_Loading_Guide.md), [Serialization](docs/SERIALIZATION.md), [Performance Changelog](docs/PERFORMANCE_CHANGELOG.md)

---

## Core Design Principles

- **Memory Safety:** Smart pointers, RAII, no raw pointers
- **Performance:** Cache-friendly, batch processing, optimized threading
- **Type Safety:** Strong typing, compile-time and runtime validation
- **Cross-Platform:** Unified codebase, platform-specific optimizations

---

## Contributing

Contributions welcome!  
- See [AGENTS.md](AGENTS.md) for build, test, and style guidelines.
- Report issues via GitHub with environment details and steps to reproduce.
- Fork, branch, test, and submit PRs with clear descriptions.

---

## Notes

- Window icon support for all platforms (see `res/img/`)
- Player and NPC controls: mouse, keyboard, controller (see `InputManager`)
- Template can be adapted for 3D (see `GameEngine.cpp` and `TextureManager`)
- For advanced usage, see [docs/README.md](docs/README.md)

---

## License

[MIT License](LICENSE)
