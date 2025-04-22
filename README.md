# SDL3 Game Template with CMAKE and Ninja
Based off of my SDL2 template, but updated for SDL3 and more complete. It has simplified Entity management and Entity state management system. Also has a more robust Game state management system and uses cmake and ninja instead of custom build.sh.

I use the Zed IDE with custom cmake and ninja task configurations to build on all platforms.

**Note**: Below in the Prerequisites I mentioned some ways that I used to get the project to compile. You may need some additional tweaks depending on your system and preferences. I assume you are a programmer with some experience and can setup the environment. Questions are always welcome. :)

## Features

- SDL3 integration with SDL_image, SDL_ttf, and SDL_mixer
- Cross-platform support (Windows, macOS, Linux)
- Automatic dependency management with FetchContent (for SDL3 libs)
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
- Font management

## Building the Project

### Prerequisites

- CMake 3.28 or higher
- Ninja build system (recommended)
- A C++ compiler with C++23 support

**Windows**
need to install mysys2 for compiler and SDL3 dependencies like harfbuzz, truetype, freetype etc.
scoop or chocolatey to install Ninja and zed.
Cmake can be installed from the official website.

**Linux**
Follow the instructions on the official SDL3 website to install SDL3 dependencies.
https://wiki.libsdl.org/SDL3/README/linux

**macOS**
Homebrew is recommended for SDL3 dependencies like harfbuzz, truetype, and freetype etc.
brew install sdl3 sdl3_image sdl3_ttf sdl3_mixer should get you everything you need for SDL3. However, via CMAKE the project will use the SDL3 libraries downloaded from official SDL github via fecthContent.It will not use the SDL3 libraries installed via Homebrew.
xcode command line tools is needed to compile.

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
The icon is automatically loaded from the `res/img` folder.
