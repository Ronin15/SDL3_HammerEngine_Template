# GameLoop Documentation

## Overview

The `GameLoop` class manages the main game execution loop using industry-standard timing patterns. It provides a callback-based architecture with support for both single-threaded and multi-threaded execution modes, featuring fixed timestep updates and variable timestep rendering with interpolation.

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
  - [Basic Setup](#basic-setup)
  - [Multi-Threaded Setup](#multi-threaded-setup)
- [Architecture](#architecture)
- [Timing Systems](#timing-systems)
- [Threading Models](#threading-models)
- [Callback System](#callback-system)
- [Performance Features](#performance-features)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Setup

```cpp
#include "core/GameLoop.hpp"
#include "core/GameEngine.hpp"

int main() {
    // Initialize game engine
    GameEngine& engine = GameEngine::Instance();
    if (!engine.init("My Game", 1280, 720, false)) {
        return -1;
    }
    
    // Create game loop with 60 FPS, 1/60s fixed timestep, threaded mode
    auto gameLoop = std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true);
    
    // Set callbacks
    gameLoop->setEventHandler([&engine]() {
        engine.handleEvents();
    });
    
    gameLoop->setUpdateHandler([&engine](float deltaTime) {
        engine.update(deltaTime);
    });
    
    gameLoop->setRenderHandler([&engine](float interpolation) {
        engine.render(interpolation);
    });
    
    // Set engine's game loop reference
    engine.setGameLoop(gameLoop);
    
    // Run the game loop
    bool success = gameLoop->run();
    
    // Cleanup
    engine.clean();
    return success ? 0 : -1;
}
```

### Multi-Threaded Setup

The GameLoop automatically handles threading when constructed with `threaded = true`:

```cpp
// Multi-threaded: Updates run on separate thread, events/rendering on main thread
GameLoop gameLoop(60.0f, 1.0f/60.0f, true);

// Single-threaded: Everything runs on main thread
GameLoop gameLoop(60.0f, 1.0f/60.0f, false);
```

## Architecture

### Design Principles

- **Separation of Concerns**: Events, updates, and rendering are handled separately
- **Fixed Timestep Updates**: Ensures consistent game logic regardless of frame rate
- **Variable Timestep Rendering**: Provides smooth visuals with interpolation
- **Thread Safety**: Safe callback invocation and state management
- **Professional Patterns**: Based on industry-standard game loop implementations

### Core Components

```cpp
class GameLoop {
private:
    // Timing management
    std::unique_ptr<TimestepManager> m_timestepManager;

    // Callback handlers
    EventHandler m_eventHandler;
    UpdateHandler m_updateHandler;
    RenderHandler m_renderHandler;

    // Loop state
    std::atomic<bool> m_running;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_stopRequested;

    // Threading
    bool m_threaded;
    std::unique_ptr<std::thread> m_updateThread;
    std::mutex m_updateMutex;
    std::atomic<bool> m_updateThreadRunning;

    // Update synchronization
    std::atomic<bool> m_pendingUpdates;
    std::atomic<int> m_updateCount;
    std::mutex m_callbackMutex;
};
```

### Execution Flow

**Single-Threaded Mode:**
1. Process events (main thread)
2. Process updates with fixed timestep (main thread)
3. Process rendering with interpolation (main thread)
4. Frame rate limiting

**Multi-Threaded Mode:**
1. Main thread: Events → Rendering → Frame rate limiting
2. Update thread: Fixed timestep updates with smart sleep timing

## Timing Systems

### TimestepManager Integration

The GameLoop uses `TimestepManager` for precise timing control:

```cpp
// Constructor creates TimestepManager
GameLoop(float targetFPS, float fixedTimestep, bool threaded)
    : m_timestepManager(std::make_unique<TimestepManager>(targetFPS, fixedTimestep))
```

### Fixed Timestep Updates

Updates run at a consistent rate regardless of frame rate:

```cpp
void GameLoop::processUpdates() {
    // Process all pending fixed timestep updates
    while (m_timestepManager->shouldUpdate()) {
        float deltaTime = m_timestepManager->getUpdateDeltaTime();
        invokeUpdateHandler(deltaTime);
        m_updateCount.fetch_add(1);
    }
}
```

**Key Features:**
- **Consistent Physics**: Game logic runs at fixed intervals (e.g., 1/60s)
- **Catch-up Updates**: Multiple updates per frame if system is behind
- **Deterministic Behavior**: Same input produces same output regardless of FPS

### Variable Timestep Rendering

Rendering runs as fast as possible with interpolation:

```cpp
void GameLoop::processRender() {
    if (m_timestepManager->shouldRender()) {
        float interpolation = m_timestepManager->getRenderInterpolation();
        invokeRenderHandler(interpolation);
    }
}
```

**Benefits:**
- **Smooth Visuals**: Rendering not tied to update frequency
- **High FPS Support**: Can render at 120+ FPS while updates stay at 60 FPS
- **Interpolation**: Smooth motion between fixed update steps

## Threading Models

### Single-Threaded Mode

Everything runs on the main thread in sequence:

```cpp
void GameLoop::runMainThread() {
    while (m_running.load() && !m_stopRequested.load()) {
        m_timestepManager->startFrame();
        
        // Always process events on main thread (SDL requirement)
        processEvents();
        
        // Process updates (single-threaded mode only)
        if (!m_threaded && !m_paused.load()) {
            processUpdates();
        }
        
        // Always process rendering
        processRender();
        
        m_timestepManager->endFrame();
    }
}
```

### Multi-Threaded Mode

Updates run on a separate thread with smart timing:

```cpp
void GameLoop::runUpdateThread() {
    while (m_updateThreadRunning.load() && !m_stopRequested.load()) {
        if (!m_paused.load()) {
            processUpdates();
        }
        
        // Smart sleep time based on target FPS
        float targetFPS = m_timestepManager->getTargetFPS();
        float targetFrameTimeMs = 1000.0f / targetFPS;
        
        // Sleep for half target frame time (1-8ms range)
        uint32_t sleepTimeMs = static_cast<uint32_t>(
            std::max(1.0f, std::min(8.0f, targetFrameTimeMs * 0.5f))
        );
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
    }
}
```

### Thread Synchronization

```cpp
class GameLoop {
private:
    std::mutex m_callbackMutex;          // Protects callback function pointers
    std::atomic<bool> m_updateThreadRunning;  // Update thread state
    std::atomic<int> m_updateCount;      // Update counter for debugging
    std::atomic<bool> m_pendingUpdates;  // Update synchronization flag
};
```

## Callback System

### Callback Function Types

```cpp
// Event handling (main thread only - SDL requirement)
using EventHandler = std::function<void()>;

// Game logic updates (fixed timestep)
using UpdateHandler = std::function<void(float deltaTime)>;

// Rendering (variable timestep with interpolation)
using RenderHandler = std::function<void(float interpolation)>;
```

### Setting Up Callbacks

```cpp
// Thread-safe callback registration
void setEventHandler(EventHandler handler);
void setUpdateHandler(UpdateHandler handler);
void setRenderHandler(RenderHandler handler);
```

### Thread-Safe Callback Invocation

All callbacks are invoked with proper thread safety:

```cpp
void GameLoop::invokeEventHandler() {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_eventHandler) {
        try {
            m_eventHandler();
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in event handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeUpdateHandler(float deltaTime) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_updateHandler) {
        try {
            m_updateHandler(deltaTime);
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in update handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeRenderHandler(float interpolation) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_renderHandler) {
        try {
            m_renderHandler(interpolation);
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in render handler: " + std::string(e.what()));
        }
    }
}
```

## Performance Features

### Frame Time Monitoring

```cpp
// Get current performance metrics
float getCurrentFPS() const;           // Measured FPS
uint32_t getFrameTimeMs() const;      // Last frame time in milliseconds
float getTargetFPS() const;           // Target FPS setting

// Check for performance issues
bool isFrameTimeExcessive() const;    // From TimestepManager
```

### Dynamic Configuration

```cpp
// Runtime adjustments
void setTargetFPS(float fps);         // Change target frame rate
void setFixedTimestep(float timestep); // Change update frequency

// Advanced access
TimestepManager& getTimestepManager(); // Direct access for custom timing
```

### Pause/Resume System

```cpp
void setPaused(bool paused);          // Pause/resume game logic
bool isPaused() const;               // Check pause state

// Automatic timing reset when resuming
if (wasPaused && !paused) {
    m_timestepManager->reset();      // Prevents time jumps
}
```

## API Reference

### Constructor

```cpp
explicit GameLoop(float targetFPS = 60.0f, 
                 float fixedTimestep = 1.0f/60.0f, 
                 bool threaded = true);
```

### Core Methods

```cpp
bool run();                          // Start main loop (blocks until stopped)
void stop();                         // Stop the loop (thread-safe)
bool isRunning() const;             // Check if loop is active
```

### Callback Configuration

```cpp
void setEventHandler(EventHandler handler);
void setUpdateHandler(UpdateHandler handler);
void setRenderHandler(RenderHandler handler);
```

### State Management

```cpp
void setPaused(bool paused);
bool isPaused() const;
```

### Performance Monitoring

```cpp
float getCurrentFPS() const;
uint32_t getFrameTimeMs() const;
float getTargetFPS() const;
```

### Advanced Configuration

```cpp
void setTargetFPS(float fps);
void setFixedTimestep(float timestep);
TimestepManager& getTimestepManager();
```

## Best Practices

### 1. Proper Callback Design

Design callbacks to be fast and exception-safe:

```cpp
// Good: Fast, exception-safe event handling
gameLoop.setEventHandler([&engine]() {
    try {
        engine.handleEvents();
    } catch (const std::exception& e) {
        // Log error but don't crash the loop
        GAMELOOP_ERROR("Event handling failed: " + std::string(e.what()));
    }
});

// Good: Fixed timestep update logic
gameLoop.setUpdateHandler([&engine](float deltaTime) {
    // deltaTime is always consistent (e.g., 1/60s = 0.0167s)
    engine.update(deltaTime);
});

// Good: Interpolated rendering
gameLoop.setRenderHandler([&engine](float interpolation) {
    // interpolation helps smooth movement between updates
    engine.render(interpolation);
});
```

### 2. Thread-Safe Game State

When using multi-threaded mode, ensure proper synchronization:

```cpp
class ThreadSafeGameWorld {
private:
    mutable std::shared_mutex m_stateMutex;
    GameState m_gameState;
    
public:
    // Update (called from update thread)
    void update(float deltaTime) {
        std::unique_lock<std::shared_mutex> lock(m_stateMutex);
        m_gameState.update(deltaTime);
    }
    
    // Render (called from main thread)
    void render(float interpolation) const {
        std::shared_lock<std::shared_mutex> lock(m_stateMutex);
        m_gameState.render(interpolation);
    }
};
```

### 3. Graceful Shutdown

Implement proper shutdown handling:

```cpp
class GameApplication {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    std::atomic<bool> m_shutdownRequested{false};
    
public:
    void requestShutdown() {
        m_shutdownRequested.store(true);
        if (m_gameLoop) {
            m_gameLoop->stop();
        }
    }
    
    void setupCallbacks() {
        m_gameLoop->setEventHandler([this]() {
            // Check for quit events
            if (shouldQuit()) {
                requestShutdown();
            }
            // Handle other events...
        });
    }
};
```

### 4. Performance Monitoring

Monitor performance and adjust accordingly:

```cpp
class PerformanceAwareGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    
public:
    void monitorPerformance() {
        // Check if frame time is excessive
        if (m_gameLoop->getTimestepManager().isFrameTimeExcessive()) {
            GAMELOOP_WARN("Frame time excessive: " + 
                         std::to_string(m_gameLoop->getFrameTimeMs()) + "ms");
        }
        
        // Log FPS periodically
        static int frameCounter = 0;
        if (++frameCounter % 300 == 0) { // Every 5 seconds at 60 FPS
            GAMELOOP_INFO("Current FPS: " + 
                         std::to_string(m_gameLoop->getCurrentFPS()));
        }
    }
};
```

## Examples

### Complete Game Loop Setup

```cpp
#include "core/GameLoop.hpp"
#include "core/GameEngine.hpp"

class MyGameApplication {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    std::atomic<bool> m_running{true};
    
public:
    MyGameApplication() = default;
    
    bool initialize() {
        // Initialize engine
        GameEngine& engine = GameEngine::Instance();
        if (!engine.init("My Game", 1920, 1080, false)) {
            return false;
        }
        
        // Create game loop (60 FPS, 1/60s updates, multi-threaded)
        m_gameLoop = std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true);
        
        // Set up callbacks
        setupCallbacks();
        
        // Set engine's game loop reference
        engine.setGameLoop(m_gameLoop);
        
        return true;
    }
    
    bool run() {
        if (!m_gameLoop) {
            return false;
        }
        
        // Start the main loop (blocks until stopped)
        return m_gameLoop->run();
    }
    
    void shutdown() {
        if (m_gameLoop) {
            m_gameLoop->stop();
        }
        
        GameEngine& engine = GameEngine::Instance();
        engine.clean();
    }
    
private:
    void setupCallbacks() {
        GameEngine& engine = GameEngine::Instance();
        
        // Event handling (main thread only)
        m_gameLoop->setEventHandler([&engine, this]() {
            engine.handleEvents();
            
            // Check for quit condition
            if (!engine.getRunning()) {
                m_running.store(false);
                m_gameLoop->stop();
            }
        });
        
        // Game logic updates (fixed timestep)
        m_gameLoop->setUpdateHandler([&engine](float deltaTime) {
            engine.update(deltaTime);
        });
        
        // Rendering (variable timestep)
        m_gameLoop->setRenderHandler([&engine](float interpolation) {
            engine.render(interpolation);
        });
    }
};

int main() {
    MyGameApplication app;
    
    if (!app.initialize()) {
        return -1;
    }
    
    bool success = app.run();
    app.shutdown();
    
    return success ? 0 : -1;
}
```

### High-Performance Configuration

```cpp
class HighPerformanceGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    
public:
    HighPerformanceGameLoop() {
        // High refresh rate setup: 144 FPS rendering, 60 FPS logic
        m_gameLoop = std::make_shared<GameLoop>(144.0f, 1.0f/60.0f, true);
        configureTiming();
    }
    
private:
    void configureTiming() {
        // Access TimestepManager for advanced configuration
        TimestepManager& timestep = m_gameLoop->getTimestepManager();
        
        // Monitor performance
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            static int updateCount = 0;
            static auto lastTime = std::chrono::high_resolution_clock::now();
            
            updateHighFrequencyLogic(deltaTime);
            
            // Performance monitoring every 60 updates (1 second)
            if (++updateCount % 60 == 0) {
                auto currentTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastTime).count();
                
                float actualUpdateRate = 60000.0f / duration;
                GAMELOOP_INFO("Update rate: " + std::to_string(actualUpdateRate) + " Hz");
                lastTime = currentTime;
            }
        });
    }
    
    void updateHighFrequencyLogic(float deltaTime) {
        // High-precision game logic
        // deltaTime = 1/60s = 0.0167s consistently
    }
};
```

### Pause/Resume System

```cpp
class PausableGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    bool m_gameMenuOpen{false};
    
public:
    PausableGameLoop() {
        m_gameLoop = std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true);
        setupPauseHandling();
    }
    
private:
    void setupPauseHandling() {
        m_gameLoop->setEventHandler([this]() {
            // Handle pause input
            if (isEscapePressed()) {
                togglePause();
            }
            
            // Handle other events based on pause state
            if (m_gameLoop->isPaused()) {
                // Handle menu events
            } else {
                // Handle game events
            }
        });
        
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            if (m_gameLoop->isPaused()) {
                updatePauseMenu(deltaTime);
            } else {
                updateGameLogic(deltaTime);
            }
        });
        
        m_gameLoop->setRenderHandler([this](float interpolation) {
            renderGame(interpolation);
            
            if (m_gameLoop->isPaused()) {
                renderPauseOverlay(interpolation);
            }
        });
    }
    
    void togglePause() {
        bool currentlyPaused = m_gameLoop->isPaused();
        m_gameLoop->setPaused(!currentlyPaused);
        m_gameMenuOpen = !currentlyPaused;
        
        GAMELOOP_INFO(currentlyPaused ? "Game resumed" : "Game paused");
    }
    
    bool isEscapePressed() {
        // Implement escape key detection
        return false; // Placeholder
    }
    
    void updateGameLogic(float deltaTime) {
        // Normal game updates
    }
    
    void updatePauseMenu(float deltaTime) {
        // Pause menu logic
    }
    
    void renderGame(float interpolation) {
        // Game rendering
    }
    
    void renderPauseOverlay(float interpolation) {
        // Pause menu rendering
    }
};
```

---

The GameLoop provides a robust, professional-grade main loop implementation that handles the complexities of timing, threading, and callback management, allowing developers to focus on game logic rather than infrastructure concerns.