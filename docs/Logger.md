# Logger System Documentation

## Overview

The Forge Game Engine logging system provides efficient, configurable logging with zero overhead in release builds. The system is designed for high-performance game development with separate debug and release configurations.

## Table of Contents

- [Quick Start](#quick-start)
- [System Architecture](#system-architecture)
- [Log Levels](#log-levels)
- [Debug vs Release Builds](#debug-vs-release-builds)
- [Available Macros](#available-macros)
- [System-Specific Logging](#system-specific-logging)
- [Best Practices](#best-practices)
- [Performance Considerations](#performance-considerations)
- [Examples](#examples)

## Quick Start

### Basic Usage

```cpp
#include "utils/Logger.hpp"

// Basic logging with system identification
FORGE_INFO("MySystem", "Application started successfully");
FORGE_ERROR("Network", "Failed to connect to server");
FORGE_CRITICAL("Memory", "Out of memory condition detected");
```

### System-Specific Macros

```cpp
// Use convenient system-specific macros
GAMEENGINE_INFO("Engine initialized successfully");
TEXTURE_ERROR("Failed to load texture: player.png");
SOUND_WARN("Audio device not found, using software mixing");
```

## System Architecture

### Design Principles

1. **Zero Overhead in Release**: No logging overhead in release builds except CRITICAL messages
2. **Fast printf-based Output**: Uses printf for maximum performance in debug builds
3. **Immediate Flushing**: Real-time output for debugging with fflush()
4. **Type Safety**: String conversion handled automatically in macros
5. **System Identification**: Clear identification of which system generated each log

### Class Structure

```cpp
namespace Forge {
    enum class LogLevel : uint8_t {
        CRITICAL = 0,  // Always logged (even in release)
        ERROR = 1,     // Debug only
        WARNING = 2,   // Debug only
        INFO = 3,      // Debug only
        DEBUG = 4      // Debug only
    };
    
    class Logger {
        static void Log(LogLevel level, const char* system, const std::string& message);
        static void Log(LogLevel level, const char* system, const char* message);
    };
}
```

## Log Levels

### CRITICAL (Level 0)
- **Always logged** in both debug and release builds
- Reserved for fatal errors and crash conditions
- Automatically flushed to ensure output before potential crash

### ERROR (Level 1)
- **Debug builds only**
- Used for error conditions that don't crash the application
- Non-recoverable errors that affect functionality

### WARNING (Level 2)
- **Debug builds only**
- Potential issues that don't prevent operation
- Deprecated function usage, fallback behavior

### INFO (Level 3)
- **Debug builds only**
- General information about system state
- Initialization messages, status updates

### DEBUG (Level 4)
- **Debug builds only**
- Detailed debugging information
- Performance metrics, detailed state information

## Debug vs Release Builds

### Debug Build Behavior
```cpp
#ifdef DEBUG
    // Full Logger class with printf-based output
    // All log levels functional
    // Immediate flushing for real-time feedback
    #define FORGE_INFO(system, msg) Forge::Logger::Log(Forge::LogLevel::INFO, system, std::string(msg))
#endif
```

### Release Build Behavior
```cpp
#else
    // Ultra-minimal overhead
    #define FORGE_CRITICAL(system, msg) do { \
        printf("Forge Game Engine - [%s] CRITICAL: %s\n", system, std::string(msg).c_str()); \
    } while(0)
    
    // All other levels become no-ops
    #define FORGE_ERROR(system, msg) ((void)0)
    #define FORGE_WARN(system, msg) ((void)0)
    // ... etc
#endif
```

## Available Macros

### Core Logging Macros

| Macro | Parameters | Description |
|-------|------------|-------------|
| `FORGE_CRITICAL(system, msg)` | system: const char*, msg: any type | Critical error logging |
| `FORGE_ERROR(system, msg)` | system: const char*, msg: any type | Error logging |
| `FORGE_WARN(system, msg)` | system: const char*, msg: any type | Warning logging |
| `FORGE_INFO(system, msg)` | system: const char*, msg: any type | Information logging |
| `FORGE_DEBUG(system, msg)` | system: const char*, msg: any type | Debug logging |

### Message Parameter Types
The `msg` parameter accepts:
- `const char*` strings
- `std::string` objects
- Any type convertible to string via `std::string(msg)`

## System-Specific Logging

### Core Systems

#### GameLoop System
```cpp
GAMELOOP_CRITICAL(msg)    // GameLoop critical errors
GAMELOOP_ERROR(msg)       // GameLoop errors
GAMELOOP_WARN(msg)        // GameLoop warnings
GAMELOOP_INFO(msg)        // GameLoop information
GAMELOOP_DEBUG(msg)       // GameLoop debug info
```

#### GameEngine System
```cpp
GAMEENGINE_CRITICAL(msg)  // Engine critical errors
GAMEENGINE_ERROR(msg)     // Engine errors
GAMEENGINE_WARN(msg)      // Engine warnings
GAMEENGINE_INFO(msg)      // Engine information
GAMEENGINE_DEBUG(msg)     // Engine debug info
```

#### ThreadSystem
```cpp
THREADSYSTEM_CRITICAL(msg) // Threading critical errors
THREADSYSTEM_ERROR(msg)    // Threading errors
THREADSYSTEM_WARN(msg)     // Threading warnings
THREADSYSTEM_INFO(msg)     // Threading information
THREADSYSTEM_DEBUG(msg)    // Threading debug info
```

### Manager Systems

#### Resource Managers
```cpp
// Texture Management
TEXTURE_CRITICAL(msg)     // Texture loading failures
TEXTURE_ERROR(msg)        // Texture operation errors
TEXTURE_WARN(msg)         // Texture warnings
TEXTURE_INFO(msg)         // Texture information
TEXTURE_DEBUG(msg)        // Texture debug info

// Sound Management
SOUND_CRITICAL(msg)       // Audio system failures
SOUND_ERROR(msg)          // Sound operation errors
SOUND_WARN(msg)           // Audio warnings
SOUND_INFO(msg)           // Sound information
SOUND_DEBUG(msg)          // Sound debug info

// Font Management
FONT_CRITICAL(msg)        // Font loading failures
FONT_ERROR(msg)           // Font operation errors
FONT_WARN(msg)            // Font warnings
FONT_INFO(msg)            // Font information
FONT_DEBUG(msg)           // Font debug info
```

#### Game Systems
```cpp
// AI Management
AI_CRITICAL(msg)          // AI system failures
AI_ERROR(msg)             // AI operation errors
AI_WARN(msg)              // AI warnings
AI_INFO(msg)              // AI information
AI_DEBUG(msg)             // AI debug info

// Event Management
EVENT_CRITICAL(msg)       // Event system failures
EVENT_ERROR(msg)          // Event operation errors
EVENT_WARN(msg)           // Event warnings
EVENT_INFO(msg)           // Event information
EVENT_DEBUG(msg)          // Event debug info

// Input Management
INPUT_CRITICAL(msg)       // Input system failures
INPUT_ERROR(msg)          // Input operation errors
INPUT_WARN(msg)           // Input warnings
INPUT_INFO(msg)           // Input information
INPUT_DEBUG(msg)          // Input debug info
```

#### State Management
```cpp
// Game State Management
GAMESTATE_CRITICAL(msg)   // Game state failures
GAMESTATE_ERROR(msg)      // State operation errors
GAMESTATE_WARN(msg)       // State warnings
GAMESTATE_INFO(msg)       // State information
GAMESTATE_DEBUG(msg)      // State debug info

// Entity State Management
ENTITYSTATE_CRITICAL(msg) // Entity state failures
ENTITYSTATE_ERROR(msg)    // Entity operation errors
ENTITYSTATE_WARN(msg)     // Entity warnings
ENTITYSTATE_INFO(msg)     // Entity information
ENTITYSTATE_DEBUG(msg)    // Entity debug info
```

#### UI and Save Systems
```cpp
// UI Management
UI_CRITICAL(msg)          // UI system failures
UI_ERROR(msg)             // UI operation errors
UI_WARN(msg)              // UI warnings
UI_INFO(msg)              // UI information
UI_DEBUG(msg)             // UI debug info

// Save Game Management
SAVEGAME_CRITICAL(msg)    // Save system failures
SAVEGAME_ERROR(msg)       // Save operation errors
SAVEGAME_WARN(msg)        // Save warnings
SAVEGAME_INFO(msg)        // Save information
SAVEGAME_DEBUG(msg)       // Save debug info
```

## Best Practices

### 1. Use Appropriate Log Levels
```cpp
// Good: Use CRITICAL for actual critical errors
GAMEENGINE_CRITICAL("Failed to initialize SDL - application cannot continue");

// Good: Use ERROR for recoverable errors
TEXTURE_ERROR("Failed to load optional texture: background.png");

// Good: Use WARN for potential issues
SOUND_WARN("Audio device busy, retrying in 100ms");

// Good: Use INFO for status updates
GAMELOOP_INFO("Game loop started at 60 FPS");

// Good: Use DEBUG for detailed information
AI_DEBUG("Pathfinding calculated route with 15 waypoints");
```

### 2. Provide Context in Messages
```cpp
// Good: Specific, actionable information
TEXTURE_ERROR("Failed to load texture 'player_sprite.png': File not found");

// Bad: Vague, unhelpful message
TEXTURE_ERROR("Load failed");
```

### 3. Use System-Specific Macros
```cpp
// Good: Use system-specific macro
SOUND_INFO("Audio subsystem initialized with OpenAL");

// Less ideal: Generic macro requires system parameter
FORGE_INFO("SoundManager", "Audio subsystem initialized with OpenAL");
```

### 4. Format Complex Data
```cpp
// Good: Format complex data clearly
GAMELOOP_DEBUG("Frame timing - Delta: " + std::to_string(deltaTime) + 
               "s, FPS: " + std::to_string(currentFPS));

// Good: Use string concatenation for readable output
AI_INFO("Entity " + std::to_string(entityId) + " switched to patrol mode");
```

## Performance Considerations

### Debug Build Performance
- Uses fast printf-based output
- Immediate flushing ensures real-time feedback
- String conversion handled automatically
- Minimal overhead for typical game logging

### Release Build Performance
- **Zero overhead** for ERROR, WARNING, INFO, DEBUG levels
- CRITICAL messages have minimal overhead (single printf call)
- No function calls or string processing for disabled levels
- Compiler optimizes away disabled macros completely

### Memory Usage
- No dynamic memory allocation
- No log buffering or storage
- Immediate output to stdout
- No memory leaks possible

## Examples

### Basic System Initialization
```cpp
bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
    GAMEENGINE_INFO("Initializing Forge Game Engine");
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        GAMEENGINE_CRITICAL("Failed to initialize SDL: " + std::string(SDL_GetError()));
        return false;
    }
    
    GAMEENGINE_INFO("SDL initialized successfully");
    
    // Create window
    mp_window.reset(SDL_CreateWindow(title, width, height, 
                    fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    
    if (!mp_window) {
        GAMEENGINE_ERROR("Failed to create window: " + std::string(SDL_GetError()));
        return false;
    }
    
    GAMEENGINE_DEBUG("Window created: " + std::to_string(width) + "x" + 
                     std::to_string(height) + (fullscreen ? " (fullscreen)" : " (windowed)"));
    
    return true;
}
```

### Resource Loading with Error Handling
```cpp
bool TextureManager::load(const std::string& fileName, const std::string& textureID, 
                         SDL_Renderer* renderer) {
    TEXTURE_INFO("Loading texture: " + fileName + " as ID: " + textureID);
    
    SDL_Surface* surface = IMG_Load(fileName.c_str());
    if (!surface) {
        TEXTURE_ERROR("Failed to load image '" + fileName + "': " + std::string(IMG_GetError()));
        return false;
    }
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    if (!texture) {
        TEXTURE_ERROR("Failed to create texture from '" + fileName + "': " + std::string(SDL_GetError()));
        return false;
    }
    
    m_textureMap[textureID] = std::shared_ptr<SDL_Texture>(texture, SDL_DestroyTexture);
    TEXTURE_DEBUG("Texture loaded successfully - ID: " + textureID + ", File: " + fileName);
    
    return true;
}
```

### Game Loop with Performance Monitoring
```cpp
void GameLoop::runMainThread() {
    GAMELOOP_INFO("Starting main game loop thread");
    
    while (m_running.load()) {
        auto frameStart = std::chrono::high_resolution_clock::now();
        
        processEvents();
        
        if (!m_paused.load()) {
            processUpdates();
        }
        
        processRender();
        
        auto frameEnd = std::chrono::high_resolution_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
        
        if (frameDuration.count() > 20000) { // > 20ms frame time
            GAMELOOP_WARN("Long frame detected: " + std::to_string(frameDuration.count()) + " microseconds");
        }
        
        GAMELOOP_DEBUG("Frame completed in " + std::to_string(frameDuration.count()) + " microseconds");
    }
    
    GAMELOOP_INFO("Game loop thread terminated");
}
```

### Error Recovery with Logging
```cpp
bool SoundManager::playSound(const std::string& soundID) {
    auto it = m_soundMap.find(soundID);
    if (it == m_soundMap.end()) {
        SOUND_ERROR("Attempted to play unknown sound: " + soundID);
        return false;
    }
    
    if (Mix_PlayChannel(-1, it->second.get(), 0) == -1) {
        SOUND_WARN("Failed to play sound '" + soundID + "': " + std::string(Mix_GetError()) + 
                   " - attempting retry");
        
        // Retry once
        if (Mix_PlayChannel(-1, it->second.get(), 0) == -1) {
            SOUND_ERROR("Sound playback failed after retry: " + soundID);
            return false;
        }
    }
    
    SOUND_DEBUG("Sound played successfully: " + soundID);
    return true;
}
```

## Integration with Other Systems

### Thread Safety
The logging system is thread-safe through the use of printf, which is atomic for single calls. For multi-threaded applications:

```cpp
// Safe: Single printf call per log message
THREADSYSTEM_INFO("Worker thread " + std::to_string(threadId) + " started");

// Safe: System-specific macros are thread-safe
AI_DEBUG("Processing AI update for entity " + std::to_string(entityId));
```

### Performance Monitoring Integration
```cpp
// Use with performance monitoring systems
auto start = std::chrono::high_resolution_clock::now();
processAIUpdates();
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

AI_DEBUG("AI update completed in " + std::to_string(duration.count()) + " microseconds");
```

---

The Logger system provides the foundation for debugging and monitoring the Forge Game Engine. Use it liberally in debug builds for comprehensive insight into system behavior, while maintaining zero overhead in release builds for optimal performance.