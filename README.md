# SDL3 2D Game Template with Multi-Threading
  -Work in progress.... Features do work and are implemented! Also, learning all the features of github will implement more as I figure it out.

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
- Automatic dependency management with FetchContent (for SDL3 libs, boost container lib)
- Debug and Release build configurations
- Custom window icon support on all platforms
- Game state management system (state machine)
- Entity state management system (state machine)
- Event management system for game events (weather, scene transitions, NPC spawning)
- Save game system (for saving and loading game state)
- Texture management (auto loads all from img dir)
- Sound & Music management (auto loads all from sound and music dir) stop, start, pause, halt, play sfx
- Font management (auto loads all from font dir)
- AI Manager framework for adding AI behaviors that uses a messaging system.
- Simple and Efficient Multi-Threading system with task priorities
- Test player and NPC with 2 frame animations. They both are copyrighted - Hammer Forged Games (C) 2025
- Input handling:
  - Keyboard and mouse
  - Xbox series x controller support
  - PS4 Controller support
- Unit testing framework for core components

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
[https://wiki.libsdl.org/SDL3/README/linux](https://wiki.libsdl.org/SDL3/README-linux)

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

The project includes unit tests for the SaveGameManager, Threading System, and AI components. To run the tests:

```bash
#Run all tests - run of the scripts below.
./run_all_tests.sh

# Run the Save Manager tests
./run_save_tests.sh

# Run the Thread System tests
./run_thread_tests.sh

# Run the Event Manager tests
./run_event_tests.sh

# Run the AI Optimization tests
./run_ai_optimization_tests.sh

# Run the Thread-Safe AI tests
./run_thread_safe_ai_tests.sh

# Run the Thread-Safe AI Integration tests
./run_thread_safe_ai_integration_tests.sh

# Run the AI Scaling Benchmark
./run_ai_benchmark.sh

# Run with verbose output (example)
./run_ai_optimization_tests.sh --verbose

# Clean and rebuild tests (example)
./run_thread_safe_ai_tests.sh --clean

# Run in release mode (example)
./run_ai_benchmark.sh --release
```

See `tests/TESTING.md` for comprehensive documentation on all testing frameworks, `tests/TROUBLESHOOTING.md` for common issues and solutions, or component-specific documentation like `docs/ThreadSystem.md` and `docs/EventManager.md` for detailed information.

## Feature Component Details

### Game Engine

The core engine manages the game loop, rendering, and resource management. It's designed to be thread-safe and efficient.

### Game State Management

The game uses a state machine to manage different game states (logo, menu, gameplay, pause). States can be stacked, allowing for features like pause menus.

### Entity System

Entities like the player character have their own state machines to manage different behaviors (idle, running).

### SaveGameManager

The SaveGameManager provides functionality to save and load game state. Features include:

- Save/load to specific files or numbered slots
- Binary format save files with headers
- Metadata extraction from save files
- File management (listing, deletion, validation)

See `include/managers/SaveGameManager.hpp` for the full API.

### ThreadSystem

The ThreadSystem provides a thread pool implementation for efficient multi-threaded task execution:

- Thread-safe task queue with pre-allocated memory
- Worker thread pool that automatically scales to hardware capabilities
- Priority-based task scheduling (Critical, High, Normal, Low, Idle)
- Support for both fire-and-forget tasks and tasks with future results
- Queue capacity management to avoid overhead from memory reallocations
- Graceful shutdown with proper cleanup of resources
- Automatic capacity management for different workloads

See `docs/ThreadSystem_API.md` for the full API, and the other ThreadSystem docs for more information.

### TextureManager

The TextureManager handles all image loading and rendering operations:

- Automatic loading of textures from files or directories
- Support for drawing static images or sprite animations
- Parallax scrolling capabilities for background effects
- Memory-efficient texture management

See `include/managers/TextureManager.hpp` for the full API.

### SoundManager

The SoundManager provides a comprehensive audio system:

- Loading of sound effects and music from files or directories
- Control for sound effect playback (volume, loops)
- Music playback control (play, pause, resume, stop)
- Volume management for different audio types

See `include/managers/SoundManager.hpp` for the full API.

### FontManager

The FontManager handles text rendering throughout the application:

- Loading of TTF and OTF fonts in various sizes
- Text rendering to textures or directly to the screen
- Memory-efficient font management

See `include/managers/FontManager.hpp` for the full API.

### InputHandler

The InputHandler manages all user input across different devices:

- Keyboard input detection
- Mouse position and button state tracking
- Xbox Series X and PS4 controller support
- Gamepad axis movement and button states
- Input state resets and cleanups

See `include/managers/InputManager.hpp` for the full API.

### GameStateManager

The GameStateManager controls the high-level game states:

- State transitions between gameplay, menu, pause, etc.
- State addition, removal, and clearance
- State lookup by name
- Update and render delegation to the current state
- State stacking for overlay states (like pause menus)

See `include/managers/GameStateManager.hpp` for the full API.

### EntityStateManager

The EntityStateManager handles the state machine for individual entities:

- State transitions for entity behaviors (idle, running, etc.)
- State addition, removal, and lookup
- Current state tracking and updates
- Memory-efficient state storage using flat maps
- Seamless transitions between entity states

See `include/managers/EntityStateManager.hpp` for the full API.

### EventManager

The EventManager provides a condition-based event system for game events:

- Registration and management of different event types (weather, scene transitions, NPC spawning)
- Condition-based event triggering for dynamic game worlds
- Sequence-based events for creating complex scenarios
- Thread-safe event processing with priority-based scheduling
- Event messaging system for communication between events
- Integration with the ThreadSystem for parallel event processing
- Full GameEngine lifecycle integration (automatic initialization, update, and cleanup)

#### EventDemoState
A comprehensive demonstration and testing framework for the event system:
- **Access**: From main menu, press 'E' to enter Event Demo
- **Visual SDL UI**: Real-time display with centered text layout showing phase, timer, FPS, weather, NPC count
- **Automatic Mode**: Cycles through all event types automatically (Weather → NPC Spawn → Scene Transition → Custom Events)
- **Manual Mode**: Use number keys 1-5 to trigger specific event types
- **Event Log**: Visual display of last 6 triggered events with timestamps
- **Performance Monitoring**: Real-time FPS tracking and system status

The EventDemoState serves as both documentation and testing platform for developers to understand event system integration.

See `docs/EventManager.md`, `docs/EventDemo.md`, and `docs/EventManager_ThreadSystem.md` for detailed documentation and usage examples.

### AIManager

The AIManager provides a comprehensive AI behavior management system:

- Dynamic behavior assignment to game entities
- Thread-safe behavior updates through the game's thread system
- Priority-based task scheduling for critical AI behaviors
- Multiple behavior types for different AI patterns:
  - **WanderBehavior**: Entities move randomly within a defined area, changing direction periodically
  - **PatrolBehavior**: Entities follow predefined waypoints in sequence, moving along patrol routes
  - **ChaseBehavior**: Entities pursue a target (like the player) when within detection range
- Behavior messaging system for pausing, resuming, and controlling AI states
- Memory-efficient behavior storage with automatic cleanup
- Distance-based update frequency optimization

Key features include:

- **Centralized AI Management**: Register behaviors once and reuse across many entities
- **Behavior Switching**: Easily change entity behaviors at runtime
- **Multi-threaded Updates**: AI processing distributes across available CPU cores
- **Priority-Based Processing**: Critical AI behaviors receive processing time before less important ones
- **Waypoint System**: Create complex patrol paths with multiple points
- **Target Tracking**: Chase behavior maintains pursuit even when line of sight is lost
- **Messaging API**: Control behaviors with messages like "pause", "resume", or "reverse"
- **Extensible Design**: Create custom behaviors by implementing the AIBehavior interface

### AIDemoState

A full-featured demonstration and benchmarking framework for the AI system:

- Mass AI Entity Handling: Spawns and manages thousands of NPCs (default: 5000), each with dynamic, hot-swappable AI behaviors.
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

## Window Icon
This project supports window icons across all platforms:

- **Windows**: Icon is set both via the resource file (executable icon) and programmatically
- **macOS**: Icon is set programmatically and bundled with the application
- **Linux**: Icon is set programmatically

The icon is automatically loaded from the `res/img` folder.

## General Notes

This is a template and the first player state running "PlayerRunningState.cpp" contains player move to mouse for point click hold movement, keyboard movement (up, down, left, right), and controller movement. Keep or delete any combination of controls you want. Controller keys are mapped out and detected properly in "InputHandler.cpp" - just need to be applied in code.

Also, this template can be used for 3D as well. Just focus on replacing SDL_renderer with SDL_GPU in TexureManager::Draw functions, and update the init process in GameEngine.cpp and you should be good to go.

For more complex games, consider using the EventManager system for handling game events, scene transitions, and dynamic world interactions. The ThreadSystem priority-based task scheduling is particularly useful for managing complex AI behaviors in games with many entities.

## Documentation

Additional documentation can be found in the `docs/` directory:

- `AIManager.md` - Comprehensive guide to the AI system with examples and custom behavior creation
- `OPTIMIZATIONS.md` - Technical details on AI system performance optimizations
- `EventManager.md` - Guide to the event management system with examples and integration details
- `EventDemo.md` - Complete documentation for the EventDemoState testing framework
- `EventSystem_Integration.md` - Quick reference guide for EventSystem integration and usage patterns
- `EventManager_ThreadSystem.md` - Details on EventManager and ThreadSystem integration
- `EventManagerExamples.cpp` - Code examples for using the EventManager system
- `SaveManagerTesting.md` - Details on the SaveGameManager testing framework and how to run the tests
- `ThreadSystem.md` - Core documentation for the ThreadSystem component with usage examples and best practices
- `ThreadSystem_API.md` - Complete API reference for the ThreadSystem with method signatures and parameters
- `ThreadSystem_Optimization.md` - Explanation of what "500 tasks" means in practical game development scenarios
- `QueueCapacity_Optimization.md` - Technical details on memory optimization in the ThreadSystem task queue
