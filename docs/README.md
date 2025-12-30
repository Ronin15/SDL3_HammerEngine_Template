# Hammer Game Engine Documentation

## Table of Contents

- [Core Systems](#core-systems)
  - [GameEngine & Core Systems](#gameengine--core-systems)
  - [AI System](#ai-system)
  - [Collision System](#collision-system)
  - [Combat System](#combat-system)
  - [Event System](#event-system)
  - [Controller System](#controller-system)
  - [UI System](#ui-system)
  - [Threading System](#threading-system)
  - [Manager Systems](#manager-systems)
  - [GameState System](#gamestate-system)
  - [Utility Systems](#utility-systems)
- [Getting Started](#getting-started)
- [Key Features](#key-features)
- [Development Workflow](#development-workflow)
- [Roadmaps & Known Issues](#roadmaps--known-issues)
- [Support and Troubleshooting](#support-and-troubleshooting)

## Overview

The Hammer Game Engine is a high-performance game development framework built on SDL3, featuring advanced AI systems, event management, threading capabilities, and comprehensive UI support with auto-sizing and DPI awareness.

## Core Systems

### GameEngine & Core Systems
Foundation systems that power the game engine architecture and timing.

- **[GameEngine](core/GameEngine.md)** - Central engine singleton managing all systems and coordination
- **[GameTime](core/GameTime.md)** - Fantasy calendar, day/night cycles, seasons, weather, and time events
- **[TimestepManager](managers/TimestepManager.md)** - Fixed timestep timing with accumulator-based updates

### AI System
The AI system provides flexible, thread-safe behavior management for game entities with individual behavior instances and mode-based configuration.

- **[AI System Overview](ai/AIManager.md)** - Complete AI system documentation with unified architecture, performance optimization, and threading support
- **[Behavior Modes](ai/BehaviorModes.md)** - Comprehensive documentation for all 8 AI behaviors with 32 total modes, configuration examples, and best practices
- **[Behavior Quick Reference](ai/BehaviorQuickReference.md)** - Streamlined quick lookup for all behaviors, modes, and setup patterns
- **[Pathfinding System](ai/PathfindingSystem.md)** - Deep dive into grid navigation, heuristics, and multi-threaded job scheduling for large path queries

### Collision System
High-performance spatial hashing, broadphase queries, and pathfinding integration for world interactions.

- **[CollisionManager](managers/CollisionManager.md)** - Complete collision system documentation including spatial hash architecture, dual-path threading for broadphase and narrowphase, SIMD processing, SOA storage, and WorkerBudget integration

### Combat System
Dedicated system for managing in-game combat logic, interactions, and events.

- **[CombatController](controllers/CombatController.md)** - Manages hit detection, damage calculation, status effects, and integrates with entity state machines and the event system.

### Event System
Comprehensive event management system with EventManager as the single source of truth for weather events, NPC spawning, scene transitions, and custom events.

- **[EventManager Overview](events/EventManager.md)** - Complete unified documentation for EventManager as the single source of truth
- **[EventManager Quick Reference](events/EventManager_QuickReference.md)** - Fast API lookup for all event functionality through EventManager
- **[EventManager Advanced](events/EventManager_Advanced.md)** - Advanced topics like threading, performance optimization, and complex event patterns
- **[EventManager Examples](events/EventManager_Examples.cpp)** - Comprehensive C++ code examples and best practices (code file)
- **[EventFactory](events/EventFactory.md)** - Definition-driven creation, custom creators, and event sequences
- **[TimeEvents](events/TimeEvents.md)** - Time-related event types (HourChanged, SeasonChanged, TimePeriodChanged, etc.)

### Controller System
State-scoped event handlers that control specific behaviors without owning data. Subscribe/unsubscribe lifecycle per GameState.

- **[Controllers Overview](controllers/README.md)** - Controller pattern, lifecycle, vs. Managers comparison
- **[WeatherController](controllers/WeatherController.md)** - Weather event coordination
- **[DayNightController](controllers/DayNightController.md)** - Time period tracking and visual effects
- **[CombatController](controllers/CombatController.md)** - Handles combat logic, including hit detection, damage, and status effects.

### UI System
Comprehensive UI system with professional theming, animations, layouts, content-aware auto-sizing, and event handling for creating polished game interfaces.

- **[UIManager Guide](ui/UIManager_Guide.md)** - Complete UI system guide with auto-sizing, theming, and SDL3 integration
- **[Auto-Sizing System](ui/Auto_Sizing_System.md)** - Content-aware component sizing with multi-line text support and font-based measurements
- **[DPI-Aware Font System](ui/DPI_Aware_Font_System.md)** - Automatic display detection and font scaling for crisp text on all display types
- **[SDL3 Logical Presentation](ui/SDL3_Logical_Presentation_Modes.md)** - SDL3 presentation system integration and coordinate handling
- **[Minimap Implementation](ui/Minimap_Implementation.md)** - Design blueprint for the minimap renderer, layer compositing, and interaction hooks

### Threading System
High-performance multithreading framework with priority-based task scheduling and adaptive batch optimization.

- **[ThreadSystem](core/ThreadSystem.md)** - Thread pool with priority-based task queue, worker thread management, and task submission API
- **[WorkerBudget](core/WorkerBudget.md)** - Adaptive batch optimization using throughput-based hill-climbing for each manager system
- **Priority-Based Scheduling** - Five-level priority system (Critical, High, Normal, Low, Idle) for optimal task ordering
- **Sequential Manager Execution** - Each manager gets ALL workers during its update window (no concurrent manager execution)
- **Hardware Adaptive** - Detects logical cores (including SMT/hyperthreading), reserves one for main thread, scales workers accordingly

### Manager Systems
Resource management systems for fonts, textures, audio, particles, game data, entity states, settings, and world resources.

See the [Manager Documentation Index](managers/README.md) for a complete, alphabetized list of all manager docs.

- **[EntityStateManager](managers/EntityStateManager.md)** – Manages named state machines for entities (player, NPCs), supporting safe transitions and update delegation.
- **[FontManager](managers/FontManager.md)** – Centralized font loading, management, and text rendering with DPI-aware scaling and UI integration.
- **[GameStateManager](managers/GameStateManager.md)** – Handles the collection and switching of game states/screens, ensuring only one is active at a time.
- **[InputManager](managers/InputManager.md)** – Centralized input handling for keyboard, mouse, and gamepad, with event-driven detection and coordinate conversion.
- **[CollisionManager](managers/CollisionManager.md)** – Spatial hash registration, overlap queries, and collision channel filtering for entities and world tiles.
- **[ParticleManager](managers/ParticleManager.md)** – High-performance particle system for real-time visual effects, weather, and custom effects.
- **[PathfinderManager](managers/PathfinderManager.md)** – Centralized A* job submission, multi-thread scheduling, and result caching for path queries.
- **[ResourceFactory](managers/ResourceFactory.md)** – Blueprint-driven instantiation pipeline for complex game objects with dependency resolution.
- **[ResourceTemplateManager](managers/ResourceTemplateManager.md)** – Registers, indexes, and instantiates resource templates (items, blueprints) with thread safety and statistics.
- **[SaveGameManager](managers/SaveGameManager.md)** – Comprehensive save/load system with binary format, slot management, and robust error handling.
- **[SettingsManager](managers/SettingsManager.md)** – Thread-safe settings management with JSON persistence, category organization, and change listener callbacks.
- **[SoundManager](managers/SoundManager.md)** – Centralized audio system for sound effects and music playback, supporting multiple formats and volume control.
- **[TextureManager](managers/TextureManager.md)** – Handles loading, management, and rendering of textures (PNG), with batch loading and animation support.
- **[WorldManager](managers/WorldManager.md)** – Oversees world generation, streaming, and region management with tight integration to collision and AI systems.
- **[WorldResourceManager](managers/WorldResourceManager.md)** – Tracks and manipulates resource quantities across multiple worlds, supporting thread-safe operations and statistics.

**Note:** Some managers have dedicated documentation in specialized sections due to their extensive sub-systems: **AIManager** (see [AI System](#ai-system) section), **EventManager** (see [Event System](#event-system) section), and **UIManager** (see [UI System](#ui-system) section). See the [Manager Documentation Index](managers/README.md) for the complete alphabetized list of all managers.

**Resource System**: ResourceTemplateManager and WorldResourceManager work together to provide a complete resource management solution. See the [Resource System Integration](#resource-system-integration) section below for details on how these systems integrate with events, entities, and JSON loading.

### GameState System
GameState management for different gameplay modes and screens with async loading support.

See the [GameState Documentation Index](gameStates/README.md) for complete documentation on the GameState system.

- **[LoadingState](gameStates/LoadingState.md)** – Non-blocking loading screen with async world generation on ThreadSystem and responsive progress UI.
- **[SettingsMenuState](gameStates/SettingsMenuState.md)** – Tab-based settings UI with Apply/Cancel functionality and SettingsManager integration.

### Utility Systems
Core utility classes and helper systems used throughout the engine.

See the [Utility Documentation Index](utils/README.md) for additional utility documentation and organization.

- **[Logger System](utils/Logger.md)** - Comprehensive logging system with debug/release optimization and system-specific macros
- **[JsonReader](utils/JsonReader.md)** - RFC 8259 compliant JSON parser with type-safe accessors and robust error handling
- **[JSON Resource Loading](utils/JSON_Resource_Loading_Guide.md)** - Complete guide to loading items, materials, currency, and game resources from JSON files with ResourceTemplateManager integration
- **[Binary Serialization](utils/SERIALIZATION.md)** - Fast, header-only serialization system for game data
- **[ResourceHandle System](utils/ResourceHandle_System.md)** - Lightweight, type-safe handle indirection for resource lookups across modules
- **[SIMDMath](utils/SIMDMath.md)** - Cross-platform SIMD abstraction layer for x86-64 (SSE2/AVX2) and ARM64 (NEON) with 2-4x performance improvements
- **[Camera](utils/Camera.md)** - 2D camera utility with smooth target following, discrete zoom levels, world bounds clamping, and coordinate transformation
- **[Interpolation System](architecture/InterpolationSystem.md)** - Lock-free atomic interpolation for smooth rendering across threads
- **[Power Efficiency](performance/PowerEfficiency.md)** - Race-to-idle strategy achieving 80%+ idle residency, 2-3W average during gameplay, detailed benchmarks and optimization tips

### Performance Reports
Detailed performance analysis and benchmarking reports.

- **[EntityDataManager Power Analysis (2025-12-30)](performance_reports/power_profile_edm_comparison_2025-12-30.md)** - Comprehensive power profile comparison showing EDM architecture benefits: 55% P-core reduction, 52% peak power reduction, 21% lower CPU frequency
- **[Collision Benchmark Report (2025-12-25)](performance_reports/collision_benchmark_report_2025-12-25.md)** - Collision system performance benchmarks

## Resource System Integration

The HammerEngine features a comprehensive resource management system that integrates with multiple engine subsystems:

### Core Components
- **[ResourceTemplateManager](managers/ResourceTemplateManager.md)** - Central registry for all resource templates with fast lookup and handle management
- **[WorldResourceManager](managers/WorldResourceManager.md)** - Runtime resource quantity tracking across multiple worlds with thread-safe operations
- **[ResourceHandle System](utils/ResourceHandle_System.md)** - High-performance resource identification and validation
- **[InventoryComponent](../../include/entities/resources/InventoryComponent.hpp)** - Entity-based inventory management with resource stacking and validation

### Integration Points
- **JSON Loading**: Resources are loaded from `res/data/items.json` and `res/data/materials_and_currency.json` using the [JsonReader](utils/JsonReader.md) system
- **Event System**: Resource changes trigger [ResourceChangeEvent](../../include/events/ResourceChangeEvent.hpp) through the EventManager for real-time updates
- **Entity System**: NPCs and Players can own and manipulate resources through the InventoryComponent
- **Game States**: Resource interactions are demonstrated in EventDemoState and GamePlayState

### Quick Start
```cpp
// Load resource templates
ResourceTemplateManager::Instance().init();

// Create world resource manager
WorldResourceManager::Instance().init();

// Get a resource handle by name
auto handle = ResourceTemplateManager::Instance().getHandleByName("Iron Sword");

// Add resources to world
WorldResourceManager::Instance().addResource(worldId, handle, 5);
```

For complete integration examples, see the [JSON Resource Loading Guide](utils/JSON_Resource_Loading_Guide.md).

## Platform Notes

### Windows
- Install MSYS2 for compiler and SDL3 dependencies (harfbuzz, freetype, etc.).
- Use scoop or chocolatey to install Ninja and cppcheck.
- CMake can be installed from the official website.
- Ensure cppcheck is in your PATH.
- Set environment variables for MSYS2:
  - `C:\msys64\mingw64\lib`
  - `C:\msys64\mingw64\include`
  - `C:\msys64\mingw64\bin`
- Required packages:
  - `mingw-w64-x86_64-boost` (for testing)
  - `mingw-w64-x86_64-harfbuzz` (SDL3 req)
  - `mingw-w64-x86_64-freetype` (SDL3 req)
- Install with:
  ```
  pacman -S mingw-w64-x86_64-boost mingw-w64-x86_64-harfbuzz mingw-w64-x86_64-freetype
  ```

### Linux
- Follow the [official SDL3 Linux instructions](https://wiki.libsdl.org/SDL3/README-linux) for dependencies.
- Install Boost for tests, Valgrind for memory/thread testing, and cppcheck for static analysis:
  ```
  sudo apt-get install libboost-all-dev valgrind cppcheck
  ```
- Example tested environment:
  - Ubuntu 24.04.2 LTS
  - Kernel: Linux 6.11.0-26-generic

### macOS
- Use Homebrew for SDL3 dependencies:
  ```
  brew install freetype harfbuzz boost cppcheck
  ```
- Note: CMake will use SDL3 libraries downloaded via FetchContent, not Homebrew, for the build. Homebrew SDL3 libs can conflict.
- Xcode command line tools are required to compile.

## Getting Started

### System Overview
The Hammer Game Engine provides several core systems that work together:
- **Core Engine**: GameEngine singleton with fixed timestep timing via TimestepManager
- **AI System**: Behavior management for NPCs with threading support and distance optimization
- **Event System**: Global event handling for weather, spawning, and custom events
- **UI System**: Professional interface components with theming, animations, and auto-sizing
- **Threading System**: Multi-threaded task processing with priority scheduling and worker budgets
- **Manager Systems**: Resource management for fonts, textures, audio, and more

### Quick Links
- **[GameEngine Setup](core/GameEngine.md#quick-start)** - Initialize the engine
- **[TimestepManager Setup](managers/TimestepManager.md#quick-start)** - Timing system configuration

- **[AI Quick Start](ai/BehaviorQuickReference.md)** - Set up AI behaviors in minutes
- **[Event Quick Start](events/EventManager_QuickReference.md)** - Event system essentials with EventManager
- **[UI Quick Start](ui/UIManager_Guide.md#quick-start)** - Create UI components with auto-sizing
- **[Threading Setup](core/ThreadSystem.md#quick-start)** - Initialize multi-threading

## Key Features

### Modern Architecture
- **Singleton Engine Management**: Centralized system coordination through GameEngine
- **Fixed Timestep Architecture**: Deterministic updates with accumulator-based timing via TimestepManager
- **Single-Threaded Main Loop**: Sequential update/render on main thread with background worker threads
- **Zero-Overhead Utilities**: Debug logging and memory management without release impact
- **Individual Behavior Instances**: Each NPC gets isolated behavior state via clone()
- **Mode-Based Configuration**: Automatic setup for common AI and UI patterns
- **Professional UI Theming**: Consistent appearance without manual styling
- **Content-Aware Auto-Sizing**: Components automatically size to fit content
- **DPI-Aware Font System**: Crisp text rendering on all display types
- **SDL3 Coordinate Integration**: Accurate mouse input with logical presentation
- **Thread-Safe Operations**: Concurrent access without race conditions
- **Resource Management**: Automatic cleanup and efficient memory usage

### Performance Optimized
- **Scales to 10,000+ NPCs**: Linear performance scaling with distance optimization and adaptive batch sizing
- **Priority-Based Threading**: Critical tasks processed first with optimal task ordering
- **Sequential Manager Execution**: Each manager gets ALL workers during its update window with adaptive batch optimization via throughput-based hill-climbing
- **Efficient UI Rendering**: Only processes visible components with auto-sizing
- **Memory Optimizations**: Smart pointers and cache-friendly data structures
- **Batched Operations**: Bulk processing for better performance across all systems

### Developer Friendly
- **Comprehensive Documentation**: Detailed guides with examples and quick references
- **Quick Reference Guides**: Fast API lookup for all major systems
- **Debug Tools**: Built-in diagnostics and performance monitoring
- **Best Practice Guides**: Proven patterns and techniques
- **Migration Support**: Guides for updating existing code

## Development Workflow

1. **Read the appropriate system guide** for detailed information
2. **Check quick reference** for API lookups
3. **Review examples** in the documentation
4. **Use debug tools** to monitor performance
5. **Follow best practices** for optimal results

## Roadmaps & Known Issues

- **Camera Pipeline:** [Camera Refactor Plan](Camera_Refactor_Plan.md) – Upcoming renderer/camera modernization roadmap aligned with SDL3 logical presentation updates.
- **macOS SDL Cleanup:** [SDL3 macOS Cleanup Issue](issues/SDL3_MACOS_CLEANUP_ISSUE.md) – Platform-specific guidance for maintaining stable shutdown paths.

## Support and Troubleshooting

For issues with specific systems, see the troubleshooting sections in each system's documentation:
- Core engine issues: See [GameEngine Documentation](core/GameEngine.md)
- Timing issues: See [TimestepManager Best Practices](managers/TimestepManager.md#best-practices)
- Logger issues: See [Logger Best Practices](utils/Logger.md#best-practices)
- AI issues: See [AI System Overview](ai/AIManager.md) and [Behavior Modes](ai/BehaviorModes.md)
- Event issues: See [EventManager Overview](events/EventManager.md) and [EventManager Advanced](events/EventManager_Advanced.md)
- UI issues: See [UIManager Guide](ui/UIManager_Guide.md), [Auto-Sizing System](ui/Auto_Sizing_System.md), and [DPI-Aware Font System](ui/DPI_Aware_Font_System.md)
- Threading issues: See [ThreadSystem](core/ThreadSystem.md)
- SDL shutdown crashes (macOS): See [SDL3 macOS Cleanup Issue](issues/SDL3_MACOS_CLEANUP_ISSUE.md)

---

For specific system details, see the individual documentation files linked above.
