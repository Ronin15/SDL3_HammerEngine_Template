# SDL3 Game Template with CMAKE and Ninja

## Features

- SDL3 integration with SDL_image, SDL_ttf, and SDL_mixer
- Cross-platform support (Windows, macOS, Linux)
- Automatic dependency management with FetchContent
- Debug and Release build configurations
- Custom window icon support on all platforms
- Game state management system
- Entity state management system
- Input handling :
  - Keyboard and mouse
  - Xbox series x controller support
  - PS4 Controller support
- Texture management
- Sound management

## Building the Project

### Prerequisites

- CMake 3.28 or higher
- Ninja build system (recommended)
- A C++ compiler with C++23 support

### Build Steps

1. Clone the repository
2. Create a build directory: `mkdir build && cd build`
3. Configure with CMake: `cmake -G Ninja ..`
4. Build the project: `ninja`

## Window Icon

This project supports window icons across all platforms:

- **Windows**: Icon is set both via the resource file (executable icon) and programmatically
- **macOS**: Icon is set programmatically and bundled with the application
- **Linux**: Icon is set programmatically

The icon is automatically loaded from the `res/img/ForgeEngine.png` file.
