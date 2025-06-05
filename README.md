# SDL3 2D Game Template with Multi-Threading
  -Work in progress.... Features do work and are implemented! Also, learning all the features of github will implement more as I figure it out. Please strongly consider donating if you use this or like it! Its most appreciated!

Based off of my SDL2 template, but updated for SDL3 and more complete. It has simplified Entity management and Entity state management systems. Also has a more robust game state management system and uses CMake and Ninja instead of a custom build.sh. This is designed to be a jump off point for making a game with some of the low level and architecture stuff handled. Just add your content and start modifing the managers and add states. Demo's included to show how the systems integrate.

I use the Zed IDE with custom cmake and ninja task configurations to build/compile on all platforms. Zed has good documentation check it out at https://zed.dev/docs/

  - **Note**: Below in the Prerequisites I mentioned some ways that I used to get the project to compile on Windows. You may need some additional tweaks depending on your system and preferences. Via Cmake the compile_commands.json file is generated automatically and moved to the project root directory. This will allow Zed, when it automatically installs Clangd, to provide code completion and diagnostics.


### Generative AI useage
 - This project has evolved into a large-scale simulation engine—not like Unity or Unreal, but rather a framework that powers the game you want to create. I’m using AI to accelerate development, but it’s not building the architecture or tying everything together for me. I’ve designed the core and the shell of the engine myself, and AI is helping me refine it, especially when it comes to memory safety. One of my key goals is safety, which means no raw pointers, and no manual memory allocation or deallocation (no new or delete). There are a couple of exceptions in some SDL subsystems that I couldn't convert—only about two. I also leverage AI to help me synchronize threading issues and resolve crashes. I see AI as a powerful tool—like a well-crafted sword—that slays big nasty memory bugs, and helps me bring games to life. I hope you find this project useful or cool!

## Features Overview

- SDL3 integration with SDL_image, SDL_ttf, and SDL_mixer
- Boost Container lib for efficient memory management -> https://www.boost.org https://github.com/boostorg/boost
- Cross-platform support (Windows, macOS, Linux)
- Multi-threading support with priority-based task scheduling
- Automatic dependency management with FetchContent
- Custom window icon support on all platforms
- Game state management system (state machine)
- Entity state management system (state machine)
- Event management system for game events (weather, scene transitions, NPC spawning, Quests)
- Save game system (for saving and loading game state)
- Texture management (auto loads all from img dir)
- Sound & Music management (auto loads all from sound and music dir) stop, start, pause, halt, play sfx
- Font management (auto loads all from font dir)
- UI management system with comprehensive component support and layout management
- UI stress testing framework for performance validation and optimization
- AI Manager framework for adding AI behaviors that uses a messaging system.(Manages 10K objects easily)
- Simple and Efficient Multi-Threading system with task priorities
- Test player and NPC sprites with 2 frame animations. They are all copyrighted - Hammer Forged Games (C) 2025
- Input handling:
  - Keyboard and mouse
  - Xbox series x controller support
  - PS4 Controller support
- Comprehensive testing framework with automated test runners:
  - Boost Unit testing for core components
  - UI stress testing and performance validation
  - AI scaling benchmarks and thread safety tests
  - Event system performance and functionality tests
  - Threading system validation and optimization tests
  - Continuous integration support with result logging

- Supports the following Image, Sound, and font formats:
  - Images: png
  - Sounds: wav, mp3, ogg
  - Fonts: ttf and otf

## Building the Project

### Prerequisites

- CMake 3.28 or higher
- Ninja build system (recommended)
- A C++ compiler with C++20 support. GCC and G++ 13.30 (recommended)
- Boost libraries (automatically downloaded via FetchContent)

### Windows
Need to install mysys2 for compiler and for SDL3 dependencies like harfbuzz, freetype etc.
scoop or chocolatey to install Ninja and zed.
Cmake can be installed from the official website.
Windows will need some env vars setup for path:
- C:\msys64\mingw64\lib
- C:\msys64\mingw64\include
- C:\msys64\mingw64\bin
- Windows build tools

### Linux
Follow the instructions on the official SDL3 website to install SDL3 dependencies.
[https://wiki.libsdl.org/SDL3/README-linux](https://wiki.libsdl.org/SDL3/README-linux)

### macOS
Homebrew is recommended for SDL3 dependencies like harfbuzz, truetype, and freetype etc.
brew install sdl3 sdl3_image sdl3_ttf sdl3_mixer should get you everything you need for SDL3. However, via CMAKE the project will use the SDL3 libraries downloaded from official SDL github via fecthContent. It will not use the SDL3 libraries installed via Homebrew.
xcode command line tools is needed to compile.

### Build Steps

1. Clone the repository
2. Create a build directory: `mkdir build` in project root
3. Configure with CMake: `cmake -B build/ -G Ninja -DCMAKE_BUILD_TYPE=Debug` (change Debug to Release for release build)
4. Build the project: `ninja -C build && ./bin/debug/SDL3_Template` or `SDL_template.exe` on Windows
5. On Windows use replace the `&&` with a `|` if using powershell

## Running the Tests

The project includes comprehensive testing suites for all major components including unit tests, performance benchmarks, and stress testing frameworks:

### Core Component Tests
```bash
# Run all tests - executes all test scripts below
./run_all_tests.sh

# Core manager component tests
./run_save_tests.sh          # SaveGameManager functionality tests
./run_thread_tests.sh        # ThreadSystem unit tests and benchmarks
./run_event_tests.sh         # EventManager performance and functionality tests

# AI system comprehensive testing
./run_ai_optimization_tests.sh           # AI performance optimization tests
./run_thread_safe_ai_tests.sh           # Thread safety validation tests
./run_thread_safe_ai_integration_tests.sh # Integration testing with ThreadSystem
./run_ai_benchmark.sh                    # AI scaling and performance benchmarks
```

### UI System Testing
```bash
# UI stress testing and validation
./run_ui_stress_tests.sh     # Comprehensive UI performance testing

# UI test configurations
./run_ui_stress_tests.sh --level light --duration 20    # Light testing for 20 seconds
./run_ui_stress_tests.sh --level heavy --memory-stress  # Heavy testing with memory pressure
./run_ui_stress_tests.sh --benchmark                    # Full benchmark suite
./run_ui_stress_tests.sh --verbose --save-results       # Verbose output with result logging
```

### Test Script Options
```bash
# Common test script options (available for most test runners)
--verbose          # Enable detailed output and logging
--clean           # Clean and rebuild before testing
--release         # Run tests in release mode for performance testing
--save-results    # Save test results to log files
--duration N      # Set test duration in seconds
--level LEVEL     # Set stress level (light/medium/heavy/extreme)
--memory-stress   # Enable memory pressure simulation
--benchmark       # Run full benchmark suite
```

### Continuous Integration Support
All test scripts support CI/CD integration with exit codes and result logging:
```bash
# Example CI usage
./run_ui_stress_tests.sh --level light --duration 5 --save-results=ci_results.log
echo $? # Returns 0 for success, non-zero for failures
```

See `tests/TESTING.md` for comprehensive documentation on all testing frameworks, `tests/TROUBLESHOOTING.md` for common issues and solutions, `docs/ui/UI_Stress_Testing_Guide.md` for UI testing details, or component-specific documentation like `docs/ThreadSystem.md` and `docs/EventManager.md` for detailed information.

## Feature Component Details

flowchart TD
    Start([Start])
    Start --> Init[Initialize Core Systems]
    Init -->|ThreadSystem| Threads[Init Thread Pool]
    Init -->|Managers| Managers[Init Managers]
    Init -->|Input| Input[Init InputManager]
    Init -->|Audio| Audio[Init SoundManager]
    Init -->|Graphics| Graphics[Init TextureManager]
    Init -->|Fonts| Fonts[Init FontManager]
    Init --> GameLoop[Game Loop]
    GameLoop -->|Input| InputUpdate[Update Input State]
    GameLoop -->|Events| EventUpdate[Process Events]
    GameLoop -->|AI| AIUpdate[Update AI (AIManager, ThreadSystem)]
    GameLoop -->|Entities| EntityUpdate[Update Entities (EntityStateManager)]
    GameLoop -->|State| StateUpdate[Update Game State (GameStateManager)]
    GameLoop -->|Rendering| Render[Render Frame]
    GameLoop -->|Audio| AudioUpdate[Update Sound]
    GameLoop --> SaveCheck{Save/Load?}
    SaveCheck -- Yes --> SaveLoad[SaveGameManager: Save/Load Game]
    SaveCheck -- No --> GameLoop
    Render --> GameLoop
    SaveLoad --> GameLoop
    GameLoop --> Exit{Exit Condition?}
    Exit -- Yes --> Shutdown[Cleanup and Exit]
    Exit -- No --> GameLoop

### Game Engine

The core engine manages the game loop, rendering, and resource management. It's designed to be thread-safe and efficient.

### Game State Management

The game uses a state machine to manage different game states (logo, menu, gameplay, pause). States can be stacked, allowing for features like pause menus.

### Entity System

Player character has their own state machines to manage different behaviors (idle, running) other Non-Player characters have behviours controlled by the AI Manager.

### SaveGameManager

The SaveGameManager provides comprehensive save and load functionality with robust file management:

- **Binary Save Format**: Custom binary format with "FORGESAVE" signature and version control
- **Slot Management**: Save/load to numbered slots (1-N) with standardized naming conventions
- **File Operations**: Direct file saving/loading with custom filenames and full path control
- **Metadata Extraction**: Retrieve save information without loading full game state (timestamps, player position, level)
- **File Validation**: Verify save file integrity and format compatibility
- **Directory Management**: Automatic save directory creation with write permission validation
- **Boost Serialization**: Efficient Vector2D serialization using Boost binary archives
- **Error Handling**: Comprehensive error checking with detailed logging and exception safety
- **Memory Safety**: RAII principles with smart pointers and automatic resource cleanup
- **Cross-Platform**: Full Windows, macOS, and Linux compatibility with filesystem operations

Key features include automatic save directory setup (`res/game_saves/`), save file listing and enumeration, batch save information retrieval, and safe file deletion with validation.

See `include/managers/SaveGameManager.hpp` for the full API and implementation details.

### ThreadSystem

The ThreadSystem provides a thread pool implementation for efficient multi-threaded task execution:

- Thread-safe task queue with pre-allocated memory
- Worker thread pool that automatically scales to hardware capabilities
- Priority-based task scheduling (Critical, High, Normal, Low, Idle)
- Support for both fire-and-forget tasks and tasks with future results
- Queue capacity management to avoid overhead from memory reallocations
- Graceful shutdown with proper cleanup of resources
- Automatic capacity management for different workloads

See `docs/ThreadSystem.md` for the full API and detailed documentation.

### TextureManager

The TextureManager provides comprehensive texture loading and rendering with advanced features:

- **Directory Loading**: Automatic batch loading of all PNG files from specified directories
- **Individual File Loading**: Load specific texture files with custom IDs
- **Sprite Animation**: Frame-based animation support with row/column sprite sheets
- **Parallax Scrolling**: Seamless scrolling background effects with automatic wrapping
- **Advanced Rendering**: Support for rotation, scaling, and SDL flip modes
- **Memory Management**: Efficient texture storage using shared_ptr with automatic cleanup
- **Format Support**: PNG image format with SDL3_image integration
- **Error Handling**: Comprehensive error checking and logging for failed operations
- **Texture Retrieval**: Direct access to SDL_Texture objects for custom rendering
- **Cache Management**: Texture map management with existence checking and selective cleanup

Key rendering capabilities include static texture drawing, animated sprite frame rendering, and parallax background systems for creating dynamic visual effects.

See `include/managers/TextureManager.hpp` for the full API and `docs/TextureManager.md` for detailed documentation.

### SoundManager

The SoundManager provides a comprehensive audio system with full SDL3_mixer integration:

- **Directory Loading**: Automatic batch loading of audio files from directories (WAV, MP3, OGG)
- **Individual File Loading**: Load specific sound effects and music with custom IDs
- **Sound Effect Control**: Play, loop, and volume control for individual sound effects
- **Music Management**: Complete music playback control (play, pause, resume, stop, halt)
- **Volume Control**: Independent volume management for music and sound effects (0-128 range)
- **Format Support**: WAV, MP3, and OGG audio formats with SDL3_mixer codec support
- **Memory Management**: Automatic resource cleanup with proper Mix_Chunk and Mix_Music handling
- **Audio Device Management**: SDL3 audio device integration with proper initialization and cleanup
- **Playback State**: Music playing status detection and state management
- **Resource Validation**: Audio resource existence checking and selective cleanup operations

The system provides thread-safe audio operations with comprehensive error handling and supports concurrent sound effect playback alongside background music.

See `include/managers/SoundManager.hpp` for the full API and `docs/SoundManager.md` for detailed documentation.

### FontManager

The FontManager handles text rendering throughout the application:

- Loading of TTF and OTF fonts in various sizes
- Text rendering to textures or directly to the screen
- Text alignment control (center, left, right, top-left, top-center, top-right)
- Memory-efficient font management with automatic resource cleanup
- Directory loading support for batch font loading

See `include/managers/FontManager.hpp` for the full API and `docs/FontManager.md` for detailed documentation.

### InputManager

The InputManager manages all user input across different devices:

- Keyboard input detection with SDL_Scancode support
- Mouse position and button state tracking (left, middle, right)
- Xbox Series X and PS4 controller support with dead zone handling
- Gamepad axis movement and button states with proper mapping
- Input state resets and cleanups with proper resource management
- Automatic gamepad initialization and hot-plug support

See `include/managers/InputManager.hpp` for the full API.

### GameStateManager

The GameStateManager provides robust high-level game state management with enhanced safety features:

- **Safe State Transitions**: Exception-safe state switching with proper cleanup and initialization
- **Memory Management**: Smart pointer usage with weak pointer observers for current state tracking
- **State Lifecycle**: Complete enter/exit cycle management with error handling and validation
- **State Operations**: Addition, removal, lookup, and clearance with duplicate name protection
- **Render Delegation**: Automatic update and render delegation to active states
- **Error Recovery**: Graceful handling of state transition failures with automatic cleanup
- **Resource Safety**: RAII principles ensure proper resource management during state changes
- **State Validation**: Runtime state existence checking and name-based lookup with error reporting
- **Memory Efficiency**: Small vector optimization for typical game state counts with flat storage

The system supports complex state machines including overlay states (pause menus, inventories, dialogs) while maintaining thread safety and preventing memory leaks through careful resource management.

See `include/managers/GameStateManager.hpp` for the full API and implementation details.

### EntityStateManager

The EntityStateManager handles the state machine for the Player:

- State addition, removal, and lookup with exception safety
- Current state tracking and updates with weak pointer observers
- Memory-efficient state storage using flat maps
- Seamless transitions between Player states(running, jumping, shooting, idle, walking)
- Thread-safe state management with proper cleanup

See `include/managers/EntityStateManager.hpp` for the full API.

### UIManager

The UIManager provides a comprehensive UI system with full SDL3 logical presentation support:

- **Component System**: Buttons, labels, panels, progress bars, input fields, images, sliders, checkboxes, lists, and tooltips
- **Layout Management**: Absolute, flow, grid, stack, and anchor layout types with automatic positioning
- **Theme System**: Customizable themes with per-component styling and global theme management
- **Animation System**: Smooth component animations for position, size, and color transitions
- **Input Handling**: Full mouse and keyboard interaction with focus management
- **SDL3 Presentation Mode Support**: Universal compatibility with all SDL3 logical presentation modes:
  - **SDL_LOGICAL_PRESENTATION_LETTERBOX**: Maintains aspect ratio with black bars
  - **SDL_LOGICAL_PRESENTATION_STRETCH**: Stretches to fill window (may distort)
  - **SDL_LOGICAL_PRESENTATION_OVERSCAN**: Crops content to maintain aspect ratio
  - **SDL_LOGICAL_PRESENTATION_DISABLED**: Direct 1:1 pixel mapping
- **Event System**: Callback-based event handling for clicks, value changes, hover, and focus
- **Tooltips**: Context-sensitive tooltip system with configurable delays
- **Debug Features**: Visual debug bounds and component inspection tools

#### UI Stress Testing System

The template includes a comprehensive UI stress testing framework for performance validation:

- **Template Validation Tool**: Integrated system for evaluating UI performance characteristics
- **Real-World Testing**: Tests UI within actual game engine context including all managers and rendering pipeline
- **Multiple Test Scenarios**: Component scaling, animation stress, input flood testing, memory pressure simulation
- **Performance Benchmarking**: Frame rate monitoring, memory usage tracking, and CPU utilization measurement
- **SDL3 Presentation Mode Testing**: Validates UI behavior across all logical presentation modes
- **Automated Test Runners**: Command-line scripts for continuous integration and automated testing
- **Configurable Stress Levels**: Light, medium, heavy, and extreme testing configurations
- **Removable Design**: Clean removal process for production deployment

The stress testing system serves as both a validation tool for template users and a development aid for ongoing UI performance optimization.

See `include/managers/UIManager.hpp` for the full API, `docs/ui/SDL3_Logical_Presentation_Modes.md` for presentation mode details, and `docs/ui/UI_Stress_Testing_Guide.md` for comprehensive testing documentation.

### EventManager

The EventManager provides a high-performance, type-indexed event system optimized for speed:

- **Ultra-Fast Event Processing**: Type-indexed storage system eliminates string lookups for maximum performance
- **Data-Oriented Design**: Cache-friendly event data structures optimized for batch processing
- **Threading Integration**: Seamless ThreadSystem integration with worker budget allocation and queue pressure management
- **Event Type System**: Strongly-typed event categories (Weather, SceneChange, NPCSpawn, Custom) for fast dispatch
- **One-Line Convenience Methods**: Simplified event creation and registration with EventFactory integration
- **Handler Registration**: Type-safe event handler system with fast function call execution
- **Batch Processing**: AIManager-style batch updating with configurable threading thresholds
- **Memory Pools**: Event pooling system for memory-efficient event management and reuse
- **Performance Monitoring**: Real-time performance statistics with min/max/average timing data
- **Thread-Safe Operations**: Concurrent event processing with shared_mutex optimization
- **Event Lifecycle**: Complete event state management (active/inactive, priority-based processing)
- **Storage Compaction**: Dynamic memory optimization with automatic cleanup of removed events

#### EventDemoState
A comprehensive demonstration and testing framework for the event system:
- **Access**: From main menu, press 'E' to enter Event Demo
- **Visual SDL UI**: Real-time display with centered text layout showing phase, timer, FPS, weather, NPC count
- **Automatic Mode**: Cycles through all event types automatically (Weather → NPC Spawn → Scene Transition → Custom Events)
- **Manual Mode**: Use number keys 1-5 to trigger specific event types
- **Convenience Methods Demo**: Press 'C' to see one-line event creation in action
- **Event Log**: Visual display of last 6 triggered events with timestamps
- **Performance Monitoring**: Real-time FPS tracking and system status

The EventDemoState serves as both documentation and testing platform for developers to understand event system integration.
![Forge Engine](./md_imgs/Event_Demo.png)
![Forge Engine](./md_imgs/Event_Demo_resources.png)

See `docs/EventManager.md`, `docs/EventManager_QuickReference.md`, and `docs/EventManager_ThreadSystem.md` for detailed documentation and usage examples.

### AIManager

The AIManager provides a high-performance AI behavior management system optimized for large-scale entity management:

- **Cache-Friendly Architecture**: Type-indexed behavior storage with unified spatial system for optimal performance
- **Smart Threading Integration**: Worker budget allocation system with ThreadSystem integration and queue pressure management
- **Priority-Based Distance Optimization**: 10-level priority system (0-9) with dynamic update range multipliers
- **Batch Processing**: AIManager-style batch updating with configurable threading thresholds and cache-friendly data structures
- **Advanced Entity Management**: Unified registration system combining entity updates and behavior assignment
- **Message System**: Queue-based messaging with immediate and deferred delivery options
- **Performance Monitoring**: Real-time statistics tracking with behavior execution counters and timing data

**Behavior Types and AI Patterns**:
- **WanderBehavior**: Random movement within defined areas with direction change intervals
- **PatrolBehavior**: Waypoint-based navigation with sequence following and path reversal
- **ChaseBehavior**: Dynamic target pursuit with detection range and line-of-sight systems
- **GuardBehavior**: Position-based defensive AI with threat detection
- **AttackBehavior**: Combat-oriented behavior with engagement mechanics
- **FleeBehavior**: Escape and avoidance patterns with threat assessment

**High-Performance Features**:
- **Entity Scaling**: Efficiently manages 10,000+ entities with minimal performance impact
- **Distance Optimization**: Player-relative distance calculations with frame-based update frequencies
- **Memory Efficiency**: Smart pointer usage throughout with automatic cleanup and RAII principles
- **Thread Safety**: Shared mutex optimization for concurrent read operations
- **Spatial Indexing**: Optimized entity storage with fast lookup and batch processing capabilities
- **Behavior Cloning**: Template-based behavior instantiation for memory-efficient entity management

**Priority System Details**:
- **0-2 Priority**: Background entities (1.0x-1.2x update range)
- **3-5 Priority**: Standard entities (1.3x-1.5x update range) 
- **6-8 Priority**: Important entities (1.6x-1.8x update range)
- **9 Priority**: Critical entities (1.9x update range)

Higher priority entities receive more frequent updates and larger detection ranges, enabling responsive AI for important game elements while maintaining performance for background entities.

### AIDemoState

A full-featured demonstration and benchmarking framework for the AI system:

- Mass AI Entity Handling: Spawns and manages thousands of NPCs (default: 10,000), each with dynamic, hot-swappable AI behaviors.
- Live Behavior Switching: Instantly switch all NPCs between Wander, Patrol, and Chase behaviors using keys [1], [2], and [3], leveraging the AIManager’s registration and assignment system.
- Player Targeting: The Chase AI behavior dynamically targets the player entity for real-time pursuit demonstrations.
- Pause/Resume: Pause and resume all AI updates with [SPACE] via broadcast messaging.
- Performance Monitoring: Tracks and displays live FPS, average FPS, and entity counts for AI stress testing.
- Visual Info Panel: Renders on-screen instructions and real-time status, including current FPS and controls.
- Randomized NPC Placement: All NPCs are distributed randomly within the simulated world on startup.
- Robust Cleanup: Ensures safe cleanup of AI behaviors, player, and NPCs on exit to prevent memory/resource leaks.
- Extensible AI Behaviors: Easily add or extend behaviors (e.g., Wander, Patrol with offscreen waypoints, Chase) with AIManager’s plugin-like architecture.
- Lifecycle Management: Handles initialization (enter()), per-frame updates (update()), rendering (render()), and resource cleanup (exit()) cleanly as a GameState.
- Thread-Safe AI: Integrates with the ThreadSystem for scalable, multi-threaded AI updates.
- The AIDemoState serves as a reference and stress test for AI scalability, behavior switching, and real-time control, mirroring the structure and purpose of EventDemoState for the event system.

See docs/AIManager.md, include/gameStates/AIDemoState.hpp, and src/gameStates/AIDemoState.cpp for full API and code examples.

See `docs/AIManager.md` for detailed documentation with examples and best practices. Additional API details can be found in `include/managers/AIManager.hpp`, `include/ai/AIBehavior.hpp`, and the specific behavior implementations.

![Forge Engine](./md_imgs/Forge_Engine.png)
![Forge Engine](./md_imgs/AIDemo_resources.png)

## Template Architecture Overview

This SDL3 Game Template represents a complete, production-ready game engine framework designed for performance, safety, and scalability. The architecture emphasizes:

### Core Design Principles

- **Memory Safety**: No raw pointers, no manual memory management - everything uses smart pointers and RAII
- **Performance First**: Cache-friendly data structures, batch processing, and optimized algorithms throughout
- **Thread Safety**: Comprehensive multi-threading support with proper synchronization and worker budgets
- **Type Safety**: Strong typing systems with compile-time guarantees and runtime validation
- **Cross-Platform**: Unified codebase supporting Windows, macOS, and Linux with platform-specific optimizations

### System Integration

The template demonstrates advanced integration patterns:

- **Manager Ecosystem**: All managers work together seamlessly - UI coordinates with Input, AI uses Threading, Events integrate with all systems
- **Resource Management**: Automatic loading/cleanup with shared resource pools and efficient memory usage
- **State Machines**: Hierarchical state management at both game and entity levels for complex behavior modeling
- **Performance Monitoring**: Built-in profiling and benchmarking tools for continuous optimization
- **Testing Framework**: Comprehensive validation suites for all components with automated CI/CD support

### Scalability Features

Designed to handle production-scale requirements:

- **Entity Management**: Efficiently handles 10,000+ AI entities with minimal performance impact
- **UI Scalability**: Supports complex UI hierarchies with thousands of components
- **Event Processing**: High-throughput event system with type-indexed storage and batch processing
- **Threading Optimization**: Intelligent worker allocation with priority-based task scheduling
- **Memory Efficiency**: Optimized data structures using Boost containers and memory pools

### Development Workflow

The template provides a complete development environment:

- **Rapid Prototyping**: Pre-built systems allow immediate focus on game logic and content
- **Debugging Tools**: Visual debug modes, performance monitors, and comprehensive logging
- **Testing Integration**: Automated test suites validate system behavior and performance characteristics
- **Documentation**: Extensive documentation with examples, best practices, and troubleshooting guides
- **Modular Design**: Easy to extend, modify, or remove components based on project needs

This template serves as both a learning resource and a production foundation, demonstrating modern C++ game development practices while providing immediate usability for game projects.

## Window Icon
This project supports window icons across all platforms:

- **Windows**: Icon is set both via the resource file (executable icon) and programmatically
- **macOS**: Icon is set programmatically and bundled with the application
- **Linux**: Icon is set programmatically

The icon is automatically loaded from the `res/img` folder.

## General Notes

This is a template and the first player state running "PlayerRunningState.cpp" contains player move to mouse for point click hold movement, keyboard movement (up, down, left, right), and controller movement. Keep or delete any combination of controls you want. Controller keys are mapped out and detected properly in "InputManager.cpp" - just need to be applied in code.

Also, this template can be used for 3D as well. Just focus on replacing SDL_renderer creation in the GameEngine.cpp with SDL_GPU. Then change the draw functions in TextureManager and you should be good to go. There are only two Places that SDL_renderer is used/called. Frist, in the UI MAnager for draw colors and rects and then all the GameStates funnel their render calls back through GameEngines Render fucntion via the GameState Manager!

For more complex games, consider using the EventManager system for handling game events, scene transitions, and dynamic world interactions. The ThreadSystem priority-based task scheduling is particularly useful for managing complex AI behaviors in games with many entities.

## Documentation

**📚 [Complete Documentation Hub](docs/README.md)** - Comprehensive documentation overview with quick start guides, architecture details, and best practices.

Additional documentation can be found in the `docs/` directory:

### AI System Documentation
- **[AI System Overview](docs/ai/AIManager.md)** - Complete API reference and usage guide
- **[Behavior Modes](docs/ai/BehaviorModes.md)** - PatrolBehavior and WanderBehavior mode-based system
- **[Behavior Modes Quick Reference](docs/ai/BehaviorModes_QuickReference.md)** - Quick setup guide for behavior modes
- **[Batched Behavior Assignment](docs/ai/BATCHED_BEHAVIOR_ASSIGNMENT.md)** - Global batched assignment system
- **[Entity Update Management](docs/ai/EntityUpdateManagement.md)** - Entity update system details
- **[AI Developer Guide](docs/ai/DeveloperGuide.md)** - Advanced AI development patterns and techniques

### Event System Documentation
- **[Event Manager](docs/EventManager.md)** - Guide to the event management system with examples and integration details
- **[EventManager Quick Reference](docs/EventManager_QuickReference.md)** - Convenience methods guide and quick reference
- **[Event System Integration](docs/EventSystem_Integration.md)** - Quick reference guide for EventSystem integration and usage patterns
- **[Event Manager Threading](docs/EventManager_ThreadSystem.md)** - Details on EventManager and ThreadSystem integration
- **[Event Manager Performance](docs/EventManager_Performance_Improvements.md)** - Performance optimization techniques
- **[Event Manager Examples](docs/EventManagerExamples.cpp)** - Code examples for using the EventManager system

### Manager System Documentation
- **[FontManager](docs/FontManager.md)** - Font loading and text rendering system with TTF/OTF support
- **[TextureManager](docs/TextureManager.md)** - Texture loading and sprite rendering with animation support
- **[SoundManager](docs/SoundManager.md)** - Audio playback and sound management with volume control

### UI System Documentation
- **[SDL3 Logical Presentation Modes](docs/ui/SDL3_Logical_Presentation_Modes.md)** - Comprehensive guide to SDL3's logical presentation system and UIManager compatibility
- **[UI Stress Testing Guide](docs/ui/UI_Stress_Testing_Guide.md)** - Complete documentation for the integrated UI performance testing framework

### Threading System Documentation
- **[ThreadSystem Overview](docs/ThreadSystem.md)** - Core documentation for the ThreadSystem component with usage examples and best practices
