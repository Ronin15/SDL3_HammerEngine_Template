# SDL3 2D Game Template with Multi-Threading

A modern, production-ready C++20 SDL3 game engine template. Features robust entity, state, event, AI, UI, and resource management, with multi-threading, cross-platform support, and comprehensive testing. Designed for rapid prototyping and scalable game development.

---

## Features

- SDL3 integration (image, ttf, mixer)
- Multi-threaded task scheduling (WorkerBudget)
- Entity, game, and event state machines
- High-performance AI and particle systems
- Automatic resource, texture, sound, and font management
- UI system with auto-sizing, themes, and stress testing
- Save/load system with fast binary serialization
- RFC 8259 compliant JSON parser for configuration and data
- Cross-platform: Windows, macOS, Linux
- Comprehensive test suite and static analysis

---

## Quick Start

### Prerequisites

- CMake 3.28+, Ninja, C++20 compiler (GCC/Clang/MSVC)
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
- Static analysis: `./tests/cppcheck/cppcheck_focused.sh`  
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
- **Event System:** [Overview](docs/events/EventManager.md), [Quick Reference](docs/events/EventManager_QuickReference.md), [Examples](docs/events/EventManager_Examples.cpp)
- **Threading:** [ThreadSystem](docs/ThreadSystem.md)
- **Managers:** [ParticleManager](docs/managers/ParticleManager.md), [FontManager](docs/managers/FontManager.md), [TextureManager](docs/managers/TextureManager.md), [SoundManager](docs/managers/SoundManager.md)
- **UI:** [UIManager Guide](docs/ui/UIManager_Guide.md), [Auto-Sizing](docs/ui/Auto_Sizing_System.md), [DPI-Aware Fonts](docs/ui/DPI_Aware_Font_System.md)
- **Utilities:** [JsonReader](docs/utils/JsonReader.md), [Serialization](docs/SERIALIZATION.md), [Performance Changelog](docs/PERFORMANCE_CHANGELOG.md)

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
