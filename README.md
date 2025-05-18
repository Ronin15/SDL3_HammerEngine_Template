# SDL3 2D Game Template with Multi-Threading
  -Work in progress.... Features do work and are implemented!

Based off of my SDL2 template, but updated for SDL3 and more complete. It has simplified Entity management and Entity state management systems. Also has a more robust game state management system and uses cmake and ninja instead of a custom build.sh. This is designed to be a jump off point for making a game with some of the low level and architecture stuff handled. Just add your content and start adding game states.

I use the Zed IDE with custom cmake and ninja task configurations to build/compile on all platforms. Zed has good documentation check it out at https://zed.dev/docs/

  - **Note**: Below in the Prerequisites I mentioned some ways that I used to get the project to compile on Windows. You may need some additional tweaks depending on your system and preferences. Via Cmake the compile_commands.json file is generated automatically and moved to the project root directory. This will allow Zed, when it automatically installs Clangd, to provide code completion and diagnostics.

## Features Overview

- SDL3 integration with SDL_image, SDL_ttf, and SDL_mixer
- imgui support for SDL3 renderer -> https://github.com/ocornut/imgui
- Boost Container lib for efficient memory management (keeping things on the stack) -> https://www.boost.org https://github.com/boostorg/boost
- Cross-platform support (Windows, macOS, Linux)
- Multi-threading support
- Automatic dependency management with FetchContent (for SDL3 libs, boost container lib, imgui)
- Debug and Release build configurations
- Custom window icon support on all platforms
- Game state management system (state machine)
- Entity state management system (state machine)
- Save game system (for saving and loading game state)
- Texture management (auto loads all from img dir)
- Sound & Music management (auto loads all from sound and music dir) stop, start, pause, halt, play sfx
- Font management (auto loads all from font dir)
- Test player with 2 frame animation (with Arnie the Armadillo) Arnie is copyrighted Hammer Forged Games (C) 2025
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

The project includes unit tests for the SaveGameManager component and the Threading System. To run the tests:

```bash
# Run the tests
./run_save_tests.sh
or
./run_thread_tests.sh

# Run with verbose output
./run_save_tests.sh --verbose
or
./run_thread_tests.sh --verbose

# Clean and rebuild tests
./run_save_tests.sh --clean
or
./run_thread_tests.sh --clean
```

See `docs/SaveManagerTesting.md` or `docs/ThreadSystem.md` for more details on the testing frameworks.

## Project Components

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

See `include/SaveGameManager.hpp` for the full API.

### ThreadSystem

The ThreadSystem provides a thread pool implementation for efficient multi-threaded task execution:

- Thread-safe task queue with pre-allocated memory
- Worker thread pool that automatically scales to hardware capabilities
- Support for both fire-and-forget tasks and tasks with future results
- Singleton pattern for easy access throughout the application
- Queue capacity management to avoid overhead from memory reallocations
- Graceful shutdown with proper cleanup of resources

See `docs/ThreadSystem_API.md` for the full API, and the other ThreadSystem docs for more information.

### TextureManager

The TextureManager handles all image loading and rendering operations:

- Automatic loading of textures from files or directories
- Support for drawing static images or sprite animations
- Parallax scrolling capabilities for background effects
- Memory-efficient texture management

See `include/TextureManager.hpp` for the full API.

### SoundManager

The SoundManager provides a comprehensive audio system:

- Loading of sound effects and music from files or directories
- Control for sound effect playback (volume, loops)
- Music playback control (play, pause, resume, stop)
- Volume management for different audio types

See `include/SoundManager.hpp` for the full API.

### FontManager

The FontManager handles text rendering throughout the application:

- Loading of TTF and OTF fonts in various sizes
- Text rendering to textures or directly to the screen
- Memory-efficient font management

See `include/FontManager.hpp` for the full API.

### InputHandler

The InputHandler manages all user input across different devices:

- Keyboard input detection
- Mouse position and button state tracking
- Xbox Series X and PS4 controller support
- Gamepad axis movement and button states
- Input state resets and cleanups

See `include/InputHandler.hpp` for the full API.

### GameStateManager

The GameStateManager controls the high-level game states:

- State transitions between gameplay, menu, pause, etc.
- State addition, removal, and clearance
- State lookup by name
- Update and render delegation to the current state

See `include/GameStateManager.hpp` for the full API.

### EntityStateManager

The EntityStateManager handles the state machine for individual entities:

- State transitions for entity behaviors (idle, running, etc.)
- State addition, removal, and lookup
- Current state tracking and updates
- Memory-efficient state storage using flat maps

See `include/EntityStateManager.hpp` for the full API.

## Window Icon
This project supports window icons across all platforms:

- **Windows**: Icon is set both via the resource file (executable icon) and programmatically
- **macOS**: Icon is set programmatically and bundled with the application
- **Linux**: Icon is set programmatically

The icon is automatically loaded from the `res/img` folder.

## General Notes

This is a template and the first player state running "PlayerRunningState.cpp" contains player move to mouse for point click hold movement, keyboard movement (up, down, left, right), and controller movement. Keep or delete any combination of controls you want. Controller keys are mapped out and detected properly in "InputHandler.cpp" - just need to be applied in code.

Also, this template can be used for 3D as well. Just focus on replacing SDL_renderer with SDL_GPU in TexureManager::Draw functions, and update the init process in GameEngine.cpp and you should be good to go.

## Documentation

Additional documentation can be found in the `docs/` directory:

- `SaveManagerTesting.md` - Details on the SaveGameManager testing framework
