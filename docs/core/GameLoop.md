# GameLoop Documentation

## Overview

The GameLoop class implements industry-standard game timing patterns with callback-based architecture for clean separation of concerns. It provides fixed timestep updates for consistent game logic, variable timestep rendering with interpolation, and optional multi-threading support.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Timing Systems](#timing-systems)
- [Threading Models](#threading-models)
- [Callback System](#callback-system)
- [Performance Optimization](#performance-optimization)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Single-Threaded Setup

```cpp
#include "core/GameLoop.hpp"
#include "core/GameEngine.hpp"

int main() {
    GameEngine& engine = GameEngine::Instance();
    
    // Initialize engine
    if (!engine.init("My Game", 1280, 720, false)) {
        return -1;
    }
    
    // Create game loop (60 FPS, single-threaded)
    GameLoop gameLoop(60.0f, 1.0f/60.0f, false);
    
    // Set up callbacks
    gameLoop.setEventHandler([&engine]() {
        engine.handleEvents();
    });
    
    gameLoop.setUpdateHandler([&engine](float deltaTime) {
        engine.update(deltaTime);
    });
    
    gameLoop.setRenderHandler([&engine](float interpolation) {
        engine.render(interpolation);
    });
    
    // Run the game
    bool success = gameLoop.run();
    
    engine.clean();
    return success ? 0 : -1;
}
```

### Multi-Threaded Setup

```cpp
// Create threaded game loop (60 FPS target, 60 Hz updates, threaded)
GameLoop gameLoop(60.0f, 1.0f/60.0f, true);

// Same callback setup as above
// Updates will run on separate thread automatically
bool success = gameLoop.run();
```

## Architecture

### Design Principles

1. **Separation of Concerns**: Events, updates, and rendering are separate phases
2. **Fixed Timestep Updates**: Consistent game logic regardless of frame rate
3. **Variable Timestep Rendering**: Smooth rendering with interpolation
4. **Thread Safety**: Optional multi-threading with proper synchronization
5. **Callback Architecture**: Clean integration with game engine systems

### Core Components

```cpp
class GameLoop {
    // Timing management
    std::unique_ptr<TimestepManager> m_timestepManager;
    
    // Callback handlers
    EventHandler m_eventHandler;
    UpdateHandler m_updateHandler;
    RenderHandler m_renderHandler;
    
    // Loop state
    std::atomic<bool> m_running;
    std::atomic<bool> m_paused;
    
    // Threading support
    bool m_threaded;
    std::unique_ptr<std::thread> m_updateThread;
};
```

### Execution Flow

```
Single-Threaded:           Multi-Threaded:
┌─────────────┐           ┌─────────────┐  ┌─────────────┐
│ Main Thread │           │ Main Thread │  │Update Thread│
└─────────────┘           └─────────────┘  └─────────────┘
       │                         │                │
   ┌───▼───┐                 ┌───▼───┐           │
   │Events │                 │Events │           │
   └───┬───┘                 └───┬───┘           │
   ┌───▼───┐                     │           ┌───▼───┐
   │Updates│                     │           │Updates│
   └───┬───┘                     │           └───┬───┘
   ┌───▼───┐                 ┌───▼───┐           │
   │Render │                 │Render │◄──────────┘
   └───────┘                 └───────┘     (synchronized)
```

## Timing Systems

### Fixed Timestep Updates

Updates run at a fixed frequency for deterministic game logic:

```cpp
// Configure fixed timestep (60 Hz updates)
GameLoop gameLoop(60.0f, 1.0f/60.0f, false);

// Update callback receives consistent deltaTime
gameLoop.setUpdateHandler([](float deltaTime) {
    // deltaTime will always be 1.0f/60.0f (0.016667 seconds)
    updateGameLogic(deltaTime);
});
```

### Variable Timestep Rendering

Rendering runs as fast as possible with interpolation:

```cpp
gameLoop.setRenderHandler([](float interpolation) {
    // interpolation ranges from 0.0 to 1.0
    // Represents how far between update frames we are
    renderWithInterpolation(interpolation);
});
```

### TimestepManager Integration

The GameLoop uses TimestepManager for consistent timing control:

```cpp
// Access timestep manager for advanced configuration
TimestepManager& timestepMgr = gameLoop.getTimestepManager();

// Configure timing parameters
timestepMgr.setTargetFPS(60.0f);
timestepMgr.setFixedTimestep(1.0f/60.0f);

// Note: Uses simplified 1:1 frame mapping to eliminate timing drift
```

## Threading Models

### Single-Threaded Mode

All operations run on the main thread in sequence:

```cpp
GameLoop gameLoop(60.0f, 1.0f/60.0f, false); // threaded = false

// Execution order: Events → Updates → Render → Repeat
```

**Advantages:**
- Simple debugging
- No threading overhead
- Predictable execution order
- No synchronization issues

**Disadvantages:**
- Update processing can cause frame drops
- Limited CPU utilization

### Multi-Threaded Mode

Updates run on a separate thread from rendering:

```cpp
GameLoop gameLoop(60.0f, 1.0f/60.0f, true); // threaded = true

// Main thread: Events → Render
// Update thread: Updates (synchronized)
```

**Advantages:**
- Better CPU utilization
- Updates don't block rendering
- Smoother frame rates
- Scalable performance

**Disadvantages:**
- More complex debugging
- Requires thread-safe game logic
- Synchronization overhead

### Thread Synchronization

```cpp
class GameLoop {
private:
    // Update thread synchronization
    std::mutex m_updateMutex;
    std::atomic<bool> m_updateThreadRunning;
    std::atomic<bool> m_pendingUpdates;
    std::atomic<int> m_updateCount;
    
    void runUpdateThread() {
        while (m_updateThreadRunning.load()) {
            // Wait for update signal
            std::unique_lock<std::mutex> lock(m_updateMutex);
            
            // Process fixed timestep updates
            if (m_pendingUpdates.load()) {
                invokeUpdateHandler(getFixedTimestep());
                m_pendingUpdates.store(false);
                m_updateCount.fetch_add(1);
            }
        }
    }
};
```

## Callback System

### Callback Function Types

```cpp
// Event handling callback (main thread only)
using EventHandler = std::function<void()>;

// Update callback (fixed timestep)
using UpdateHandler = std::function<void(float deltaTime)>;

// Render callback (variable timestep with interpolation)
using RenderHandler = std::function<void(float interpolation)>;
```

### Setting Up Callbacks

```cpp
GameLoop gameLoop;

// Event handling (SDL events, input processing)
gameLoop.setEventHandler([]() {
    // Process SDL events
    // Handle input
    // Update input state
});

// Game logic updates (physics, AI, game state)
gameLoop.setUpdateHandler([](float deltaTime) {
    // Update physics with fixed timestep
    // Process AI behavior
    // Update game state
    // Handle collisions
});

// Rendering (draw calls, present)
gameLoop.setRenderHandler([](float interpolation) {
    // Clear screen
    // Render game objects with interpolation
    // Present frame
});
```

### Thread-Safe Callback Invocation

```cpp
class GameLoop {
private:
    std::mutex m_callbackMutex;
    
    void invokeEventHandler() {
        if (m_eventHandler) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_eventHandler();
        }
    }
    
    void invokeUpdateHandler(float deltaTime) {
        if (m_updateHandler) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_updateHandler(deltaTime);
        }
    }
    
    void invokeRenderHandler(float interpolation) {
        if (m_renderHandler) {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_renderHandler(interpolation);
        }
    }
};
```

## Performance Optimization

### Frame Time Monitoring

```cpp
GameLoop gameLoop;

// Monitor performance
void checkPerformance() {
    float currentFPS = gameLoop.getCurrentFPS();
    uint32_t frameTimeMs = gameLoop.getFrameTimeMs();
    
    if (currentFPS < 55.0f) {
        GAMELOOP_WARN("Low FPS detected: " + std::to_string(currentFPS));
    }
    
    if (frameTimeMs > 20) { // > 20ms frame time
        GAMELOOP_WARN("Long frame time: " + std::to_string(frameTimeMs) + "ms");
    }
}
```

### Dynamic FPS Adjustment

```cpp
class AdaptiveGameLoop {
private:
    GameLoop& m_gameLoop;
    float m_targetFPS;
    float m_minFPS;
    
public:
    AdaptiveGameLoop(GameLoop& gameLoop) 
        : m_gameLoop(gameLoop), m_targetFPS(60.0f), m_minFPS(30.0f) {}
    
    void updateFPSTarget() {
        float currentFPS = m_gameLoop.getCurrentFPS();
        
        // Reduce target FPS if performance is poor
        if (currentFPS < m_minFPS) {
            float newTarget = std::max(30.0f, m_targetFPS * 0.9f);
            m_gameLoop.setTargetFPS(newTarget);
            m_targetFPS = newTarget;
            
            GAMELOOP_INFO("Reduced target FPS to " + std::to_string(newTarget));
        }
        // Increase target FPS if performance allows
        else if (currentFPS > m_targetFPS * 1.1f && m_targetFPS < 60.0f) {
            float newTarget = std::min(60.0f, m_targetFPS * 1.1f);
            m_gameLoop.setTargetFPS(newTarget);
            m_targetFPS = newTarget;
            
            GAMELOOP_INFO("Increased target FPS to " + std::to_string(newTarget));
        }
    }
};
```

### Memory Optimization

```cpp
// Minimize allocations in callbacks
gameLoop.setUpdateHandler([](float deltaTime) {
    // Avoid memory allocations in update loop
    // Use object pools for temporary objects
    // Reuse containers instead of creating new ones
    
    // Good: Pre-allocated containers
    static std::vector<Entity*> tempEntities;
    tempEntities.clear(); // Reuse capacity
    
    // Bad: New allocation every frame
    // std::vector<Entity*> tempEntities; // Don't do this
});
```

## API Reference

### Constructor

```cpp
GameLoop(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f, bool threaded = true);
```

**Parameters:**
- `targetFPS`: Target frames per second for rendering
- `fixedTimestep`: Fixed timestep for updates in seconds
- `threaded`: Whether to run updates on separate thread

### Core Methods

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `run()` | Start the main game loop (blocking) | Main thread only |
| `stop()` | Stop the game loop | Thread-safe |
| `isRunning()` | Check if loop is running | Thread-safe |
| `setPaused(bool)` | Pause/resume updates | Thread-safe |
| `isPaused()` | Check if loop is paused | Thread-safe |

### Callback Configuration

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `setEventHandler(EventHandler)` | Set event processing callback | Main thread only |
| `setUpdateHandler(UpdateHandler)` | Set update logic callback | Main thread only |
| `setRenderHandler(RenderHandler)` | Set rendering callback | Main thread only |

### Performance Monitoring

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `getCurrentFPS()` | Get current frames per second | Thread-safe |
| `getFrameTimeMs()` | Get frame time in milliseconds | Thread-safe |
| `setTargetFPS(float)` | Set new target FPS | Thread-safe |
| `setFixedTimestep(float)` | Set new fixed timestep | Thread-safe |

### Advanced Configuration

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `getTimestepManager()` | Access timestep manager | Thread-safe |

## Best Practices

### 1. Proper Callback Design

```cpp
// Good: Keep callbacks focused and efficient
gameLoop.setUpdateHandler([&gameWorld](float deltaTime) {
    gameWorld.updatePhysics(deltaTime);
    gameWorld.updateAI(deltaTime);
    gameWorld.updateGameLogic(deltaTime);
});

// Bad: Complex logic in callback
gameLoop.setUpdateHandler([&](float deltaTime) {
    // Hundreds of lines of game logic here - don't do this
    // Move complex logic to dedicated classes/functions
});
```

### 2. Thread-Safe Game State

```cpp
class ThreadSafeGameWorld {
private:
    mutable std::shared_mutex m_stateMutex;
    GameState m_gameState;
    
public:
    void update(float deltaTime) {
        std::unique_lock<std::shared_mutex> lock(m_stateMutex);
        // Update game state safely
        m_gameState.update(deltaTime);
    }
    
    void render(float interpolation) const {
        std::shared_lock<std::shared_mutex> lock(m_stateMutex);
        // Read game state safely for rendering
        m_gameState.render(interpolation);
    }
};
```

### 3. Graceful Shutdown

```cpp
class GameApplication {
private:
    GameLoop m_gameLoop;
    std::atomic<bool> m_shutdownRequested{false};
    
public:
    void requestShutdown() {
        m_shutdownRequested.store(true);
        m_gameLoop.stop();
    }
    
    void setupCallbacks() {
        m_gameLoop.setEventHandler([this]() {
            handleEvents();
            
            // Check for shutdown conditions
            if (m_shutdownRequested.load()) {
                m_gameLoop.stop();
            }
        });
    }
};
```

### 4. Performance Monitoring Integration

```cpp
class PerformanceAwareGameLoop {
private:
    GameLoop m_gameLoop;
    PerformanceMonitor m_monitor;
    
public:
    void setupCallbacks() {
        m_gameLoop.setUpdateHandler([this](float deltaTime) {
            auto start = std::chrono::high_resolution_clock::now();
            
            // Perform updates
            updateGame(deltaTime);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            m_monitor.recordUpdateTime(duration.count());
            
            // Log performance issues
            if (duration.count() > 16000) { // > 16ms
                GAMELOOP_WARN("Slow update: " + std::to_string(duration.count()) + " microseconds");
            }
        });
    }
};
```

## Examples

### Complete Game Loop Setup

```cpp
#include "core/GameLoop.hpp"
#include "core/GameEngine.hpp"
#include "managers/GameStateManager.hpp"

class MyGameApplication {
private:
    GameEngine& m_engine;
    GameLoop m_gameLoop;
    std::atomic<bool> m_running{true};
    
public:
    MyGameApplication() 
        : m_engine(GameEngine::Instance())
        , m_gameLoop(60.0f, 1.0f/60.0f, true) // 60 FPS, threaded
    {
        setupCallbacks();
    }
    
    bool initialize() {
        if (!m_engine.init("My Game", 1920, 1080, false)) {
            GAMELOOP_ERROR("Failed to initialize game engine");
            return false;
        }
        
        // Connect game loop to engine
        m_engine.setGameLoop(std::shared_ptr<GameLoop>(&m_gameLoop, [](GameLoop*){}));
        
        return true;
    }
    
    bool run() {
        GAMELOOP_INFO("Starting main game loop");
        
        bool success = m_gameLoop.run();
        
        GAMELOOP_INFO("Game loop finished");
        return success;
    }
    
    void shutdown() {
        GAMELOOP_INFO("Shutting down application");
        
        m_running.store(false);
        m_gameLoop.stop();
        m_engine.clean();
    }
    
private:
    void setupCallbacks() {
        // Event handling
        m_gameLoop.setEventHandler([this]() {
            m_engine.handleEvents();
            
            // Check for application exit
            if (!m_engine.getRunning()) {
                m_running.store(false);
                m_gameLoop.stop();
            }
        });
        
        // Game logic updates
        m_gameLoop.setUpdateHandler([this](float deltaTime) {
            if (m_running.load()) {
                m_engine.update(deltaTime);
            }
        });
        
        // Rendering
        m_gameLoop.setRenderHandler([this](float interpolation) {
            if (m_running.load()) {
                m_engine.render(interpolation);
            }
        });
    }
};

// Main function
int main() {
    MyGameApplication app;
    
    if (!app.initialize()) {
        return -1;
    }
    
    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, [](int) {
        // Handle Ctrl+C gracefully
        static MyGameApplication* appPtr = &app;
        appPtr->shutdown();
    });
    
    bool success = app.run();
    
    app.shutdown();
    
    return success ? 0 : -1;
}
```

### Custom Timing Configuration

```cpp
class HighPerformanceGameLoop {
private:
    GameLoop m_gameLoop;
    
public:
    HighPerformanceGameLoop() : m_gameLoop(144.0f, 1.0f/120.0f, true) {
        // 144 FPS target, 120 Hz updates, threaded
        configureTiming();
    }
    
private:
    void configureTiming() {
        TimestepManager& timestepMgr = m_gameLoop.getTimestepManager();
        
        // Configure for high-performance gaming
        timestepMgr.setTargetFPS(144.0f);
        timestepMgr.setFixedTimestep(1.0f/144.0f); // Match update rate
        
        // Monitor performance
        m_gameLoop.setUpdateHandler([this](float deltaTime) {
            updateHighFrequencyLogic(deltaTime);
            
            // Monitor update frequency
            static int updateCount = 0;
            static auto lastTime = std::chrono::steady_clock::now();
            
            updateCount++;
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastTime);
            
            if (elapsed.count() >= 1) {
                float actualUpdateRate = updateCount / elapsed.count();
                GAMELOOP_DEBUG("Update rate: " + std::to_string(actualUpdateRate) + " Hz");
                
                updateCount = 0;
                lastTime = currentTime;
            }
        });
    }
    
    void updateHighFrequencyLogic(float deltaTime) {
        // High-frequency game logic for competitive gaming
        updatePrecisePhysics(deltaTime);
        updateInputWithLowLatency(deltaTime);
        updateNetworkState(deltaTime);
    }
};
```

### Pause/Resume System

```cpp
class PausableGameLoop {
private:
    GameLoop m_gameLoop;
    bool m_gameMenuOpen{false};
    
public:
    PausableGameLoop() : m_gameLoop(60.0f, 1.0f/60.0f, true) {
        setupPauseHandling();
    }
    
private:
    void setupPauseHandling() {
        m_gameLoop.setEventHandler([this]() {
            handleEvents();
            
            // Check for pause toggle
            if (isEscapePressed()) {
                togglePause();
            }
        });
        
        m_gameLoop.setUpdateHandler([this](float deltaTime) {
            if (!m_gameLoop.isPaused()) {
                updateGameLogic(deltaTime);
            } else {
                updatePauseMenu(deltaTime);
            }
        });
        
        m_gameLoop.setRenderHandler([this](float interpolation) {
            renderGame(interpolation);
            
            if (m_gameLoop.isPaused()) {
                renderPauseOverlay(interpolation);
            }
        });
    }
    
    void togglePause() {
        bool currentlyPaused = m_gameLoop.isPaused();
        m_gameLoop.setPaused(!currentlyPaused);
        m_gameMenuOpen = !currentlyPaused;
        
        if (m_gameMenuOpen) {
            GAMELOOP_INFO("Game paused");
        } else {
            GAMELOOP_INFO("Game resumed");
        }
    }
    
    bool isEscapePressed() {
        // Check for escape key press
        return false; // Implement input checking
    }
    
    void updateGameLogic(float deltaTime) {
        // Normal game updates
    }
    
    void updatePauseMenu(float deltaTime) {
        // Update pause menu UI
    }
    
    void renderGame(float interpolation) {
        // Render game world
    }
    
    void renderPauseOverlay(float interpolation) {
        // Render pause menu overlay
    }
};
```

---

The GameLoop provides the timing foundation for the Forge Game Engine, ensuring consistent game logic through fixed timestep updates while maintaining smooth rendering through variable timestep interpolation. Its flexible callback system and optional multi-threading support make it suitable for both simple games and complex, high-performance applications.