# GameLoop Documentation

## Overview

The GameLoop class provides a robust, high-performance game loop implementation with support for both single-threaded and multi-threaded execution modes. It integrates seamlessly with the Hammer Engine's ThreadSystem and WorkerBudget allocation system to provide optimal performance scaling across different hardware configurations.

**Key Features:**
- Fixed timestep updates with variable timestep rendering
- WorkerBudget-aware thread allocation and resource management
- Adaptive performance scaling based on system capabilities
- Thread-safe callback system with exception handling
- Comprehensive performance monitoring and logging
- Graceful pause/resume functionality

## Table of Contents

1. [Quick Start](#quick-start)
2. [Architecture](#architecture)
3. [Timing Systems](#timing-systems)
4. [Threading Models & WorkerBudget Integration](#threading-models--workerbudget-integration)
5. [Callback System](#callback-system)
6. [Performance Features](#performance-features)
7. [API Reference](#api-reference)
8. [Best Practices](#best-practices)
9. [Examples](#examples)

## Quick Start

### Basic Setup

```cpp
#include "core/GameLoop.hpp"

int main() {
    // Create single-threaded game loop
    GameLoop gameLoop(60.0f, 1.0f/60.0f, false);
    
    // Set up callbacks
    gameLoop.setEventHandler([]() {
        // Handle SDL events here
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                // Handle quit event
            }
        }
    });
    
    gameLoop.setUpdateHandler([](float deltaTime) {
        // Update game logic at fixed timestep
        updateGameWorld(deltaTime);
    });
    
    gameLoop.setRenderHandler([]() {
        // Render at variable framerate
        renderGame();
    });
    
    // Start the game loop
    if (!gameLoop.run()) {
        return -1;
    }
    
    return 0;
}
```

### Multi-Threaded Setup with WorkerBudget

```cpp
int main() {
    // Initialize ThreadSystem first
    Hammer::ThreadSystem::Initialize(8); // 8 worker threads
    
    // Create multi-threaded game loop
    GameLoop gameLoop(60.0f, 1.0f/60.0f, true);
    
    // Setup callbacks (same as above)
    setupCallbacks(gameLoop);
    
    // Run - automatically allocates workers from WorkerBudget
    gameLoop.run();
    
    // Cleanup
    Hammer::ThreadSystem::Shutdown();
    return 0;
}
```

## Architecture

### Design Principles

- **Separation of Concerns**: Events, updates, and rendering are handled independently
- **Resource-Aware Scaling**: Uses WorkerBudget to optimally allocate thread resources
- **Performance Adaptive**: Automatically adjusts behavior based on system capabilities
- **Exception Safe**: Comprehensive exception handling prevents crashes
- **Thread Safe**: All operations are thread-safe with proper synchronization

### Core Components

```cpp
class GameLoop {
private:
    // Timing Management
    std::unique_ptr<TimestepManager> m_timestepManager;
    
    // Callback Handlers
    EventHandler m_eventHandler;
    UpdateHandler m_updateHandler;
    RenderHandler m_renderHandler;
    
    // State Management
    std::atomic<bool> m_running;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_stopRequested;
    
    // Threading System
    bool m_threaded;
    std::future<void> m_updateTaskFuture;
    std::atomic<bool> m_updateTaskRunning;
    
    // Performance Tracking
    std::atomic<int> m_updateCount;
    
    // Thread Safety
    std::mutex m_callbackMutex;
};
```

### Execution Flow

1. **Initialization**: Configure timing, threading mode, and callbacks
2. **Main Thread**: Always handles events and rendering (SDL requirement)
3. **Update Worker**: Processes game logic updates (multi-threaded mode only)
4. **WorkerBudget Allocation**: Automatically allocates optimal thread resources
5. **Adaptive Performance**: Adjusts timing and resource usage based on system performance
6. **Graceful Shutdown**: Clean termination with proper resource cleanup

## Timing Systems

### TimestepManager Integration

The GameLoop uses TimestepManager for precise timing control:

- **Fixed Timestep Updates**: Game logic runs at consistent intervals
- **Variable Timestep Rendering**: Renders as fast as possible while respecting target FPS
- **Frame Time Monitoring**: Tracks performance metrics
- **Dynamic Configuration**: Runtime adjustment of timing parameters

### Fixed Timestep Updates

```cpp
void GameLoop::processUpdates() {
    // Process all pending fixed timestep updates
    while (m_timestepManager->shouldUpdate()) {
        float deltaTime = m_timestepManager->getUpdateDeltaTime();
        invokeUpdateHandler(deltaTime);
        m_updateCount.fetch_add(1, std::memory_order_relaxed);
    }
}
```

**Benefits:**
- Deterministic game logic
- Network synchronization friendly
- Physics stability
- Consistent gameplay across different hardware

### Variable Timestep Rendering

```cpp
void GameLoop::processRender() {
    if (m_timestepManager->shouldRender()) {
        invokeRenderHandler();
    }
}
```

**Benefits:**
- Smooth visual experience
- Optimal GPU utilization
- Adaptive to display refresh rates
- Battery efficiency on mobile platforms

## Threading Models & WorkerBudget Integration

### Single-Threaded Mode

```cpp
void GameLoop::runMainThread() {
    while (m_running.load() && !m_stopRequested.load()) {
        m_timestepManager->startFrame();
        
        try {
            processEvents();    // SDL events (main thread required)
            
            if (!m_threaded && !m_paused.load()) {
                processUpdates();  // Game logic updates
            }
            
            processRender();    // Rendering
            
            m_timestepManager->endFrame();
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in main thread: " + std::string(e.what()));
        }
    }
}
```

**Use Cases:**
- Simple games with light processing requirements
- Platforms with limited threading support
- Debugging and development
- Fallback mode when ThreadSystem is unavailable

### WorkerBudget-Aware Multi-Threaded Mode

```cpp
void GameLoop::runUpdateWorker(const Hammer::WorkerBudget& budget) {
    GAMELOOP_INFO("Update worker started with " + std::to_string(budget.engineReserved) + " allocated workers");
    
    // Adaptive timing system
    float targetFPS = m_timestepManager->getTargetFPS();
    const auto targetFrameTime = std::chrono::microseconds(static_cast<long>(1000000.0f / targetFPS));
    
    // Performance tracking for adaptive sleep
    auto avgUpdateTime = std::chrono::microseconds(0);
    
    // System capability detection
    bool canUseParallelUpdates = (budget.engineReserved >= 2);
    bool isHighEndSystem = (budget.totalWorkers > 4);
    
    while (m_updateTaskRunning.load() && !m_stopRequested.load()) {
        // Adaptive update processing based on available workers
        if (canUseParallelUpdates && !m_paused.load()) {
            processUpdatesParallel();
        } else if (!m_paused.load()) {
            processUpdates();
        }
        
        // Adaptive sleep timing based on system capabilities
        // ... (sophisticated timing logic)
    }
}
```

### WorkerBudget Resource Scaling

The GameLoop automatically scales its resource usage based on the WorkerBudget allocation:

- **Low-End Systems** (`budget.engineReserved < 2`): Standard sequential processing
- **High-End Systems** (`budget.engineReserved >= 2`): Enhanced parallel processing
- **Resource Monitoring**: Continuous performance tracking and adaptation
- **Dynamic Allocation**: Respects system-wide thread budget constraints

**Example Allocation:**
- 4-core system: 1-2 workers allocated to GameLoop
- 8-core system: 2-4 workers allocated to GameLoop  
- 16-core system: 4-8 workers allocated to GameLoop

### Thread Synchronization & Critical Priority

```cpp
class GameLoop {
private:
    // Future-based task management instead of raw threads
    std::future<void> m_updateTaskFuture;
    std::atomic<bool> m_updateTaskRunning;
    
    // Thread-safe callback protection
    std::mutex m_callbackMutex;
    
    // Performance tracking
    std::atomic<int> m_updateCount;
    
    // Enhanced processing methods
    void runUpdateWorker(const Hammer::WorkerBudget& budget);
    void processUpdatesParallel();
};
```

**Key Features:**
- **Critical Priority**: Update worker gets high priority in ThreadSystem
- **Future-Based**: Uses `std::future` for better task management than raw threads
- **Atomic Operations**: Lock-free state management where possible
- **Exception Safety**: Exceptions don't terminate the update worker

## Callback System

### Callback Function Types

```cpp
using EventHandler = std::function<void()>;
using UpdateHandler = std::function<void(float deltaTime)>;
using RenderHandler = std::function<void()>;
```

**EventHandler**: Called once per frame on main thread
- Process SDL events
- Handle input
- Update UI state

**UpdateHandler**: Called at fixed timestep intervals
- Update game logic
- Physics simulation
- AI processing

**RenderHandler**: Called when rendering should occur
- Draw game objects
- Update display
- Present frame

### Setting Up Callbacks

```cpp
void setEventHandler(EventHandler handler);
void setUpdateHandler(UpdateHandler handler);
void setRenderHandler(RenderHandler handler);
```

All callback setters are thread-safe and can be called while the game loop is running.

### Thread-Safe Callback Invocation

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
```

**Safety Features:**
- **Mutex Protection**: Callbacks protected from concurrent modification
- **Exception Handling**: Exceptions logged but don't crash the loop
- **Null Checking**: Safe to call even if callbacks aren't set
- **RAII Locking**: Automatic lock management with `lock_guard`

## Performance Features

### Frame Time Monitoring

```cpp
float getCurrentFPS();           // Current measured FPS
uint32_t getFrameTimeMs();       // Last frame time in milliseconds
float getTargetFPS();            // Target FPS setting
```

**Advanced Metrics:**
- Rolling average FPS calculation
- Frame time variance tracking
- Performance budget analysis
- System capability detection

### Dynamic Configuration

```cpp
void setTargetFPS(float fps);                    // Change target FPS at runtime
void setFixedTimestep(float timestep);           // Adjust update frequency
TimestepManager& getTimestepManager();           // Direct access to timing system
```

**Runtime Adaptation:**
- Automatic FPS adjustment based on performance
- Dynamic timestep scaling for heavy processing
- Performance-based quality adjustment
- Battery life optimization

### Pause/Resume System

```cpp
void setPaused(bool paused);     // Pause/resume game logic
bool isPaused();                 // Check pause state
```

**Features:**
- **Timing Reset**: Prevents time jumps when resuming
- **Thread-Safe**: Safe to call from any thread
- **Update Blocking**: Pauses only updates, not events or rendering
- **Menu Integration**: Perfect for pause menus and loading screens

## API Reference

### Constructor

```cpp
GameLoop(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f, bool threaded = true);
```

**Parameters:**
- `targetFPS`: Target frames per second for rendering
- `fixedTimestep`: Fixed time interval for updates (in seconds)
- `threaded`: Enable multi-threaded mode with WorkerBudget integration

### Core Methods

```cpp
bool run();              // Start the game loop (blocking call)
void stop();             // Request loop termination
bool isRunning();        // Check if loop is currently running
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
bool isPaused();
```

### Performance Monitoring

```cpp
float getCurrentFPS();
uint32_t getFrameTimeMs();
float getTargetFPS();
```

### Advanced Configuration

```cpp
void setTargetFPS(float fps);
void setFixedTimestep(float timestep);
TimestepManager& getTimestepManager();
```

## Best Practices

### 1. Proper Callback Design

**DO:**
```cpp
// Keep callbacks focused and efficient
gameLoop.setUpdateHandler([&gameWorld](float deltaTime) {
    gameWorld.update(deltaTime);
    physics.step(deltaTime);
});

// Use RAII for resource management
gameLoop.setRenderHandler([&renderer]() {
    renderer.beginFrame();
    renderer.renderScene();
    renderer.endFrame();
});
```

**DON'T:**
```cpp
// Avoid heavy operations in event handler
gameLoop.setEventHandler([]() {
    processComplexAI();      // This blocks SDL event processing!
    loadLargeAssets();       // This causes frame drops!
});

// Don't ignore exceptions
gameLoop.setUpdateHandler([](float deltaTime) {
    riskyOperation();        // Unhandled exceptions crash the update worker!
});
```

### 2. Thread-Safe Game State

```cpp
class ThreadSafeGameWorld {
private:
    mutable std::shared_mutex m_stateMutex;
    GameState m_gameState;
    
public:
    // Write operations (updates) use exclusive lock
    void update(float deltaTime) {
        std::unique_lock<std::shared_mutex> lock(m_stateMutex);
        m_gameState.update(deltaTime);
    }
    
    // Read operations (rendering) use shared lock
    void render() const {
        std::shared_lock<std::shared_mutex> lock(m_stateMutex);
        m_gameState.render();
    }
};
```

### 3. Graceful Shutdown

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
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || m_shutdownRequested.load()) {
                    requestShutdown();
                    return;
                }
            }
        });
    }
};
```

### 4. Performance Monitoring

```cpp
class PerformanceAwareGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    
public:
    void monitorPerformance() {
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            static int frameCounter = 0;
            static auto lastTime = std::chrono::high_resolution_clock::now();
            
            updateGameLogic(deltaTime);
            
            if (++frameCounter % 300 == 0) { // Every 5 seconds at 60 FPS
                float fps = m_gameLoop->getCurrentFPS();
                uint32_t frameTime = m_gameLoop->getFrameTimeMs();
                
                if (fps < 45.0f) {
                    GAMELOOP_WARN("Low FPS detected: " + std::to_string(fps));
                    adjustQualitySettings();
                }
            }
        });
    }
};
```

## Examples

### Complete Game Loop Setup

```cpp
#include "core/GameLoop.hpp"
#include "core/ThreadSystem.hpp"

class MyGameApplication {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    std::atomic<bool> m_running{true};
    
public:
    MyGameApplication() : m_gameLoop(std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true)) {}
    
    bool initialize() {
        // Initialize ThreadSystem for multi-threading
        if (!Hammer::ThreadSystem::Initialize()) {
            GAMELOOP_ERROR("Failed to initialize ThreadSystem");
            return false;
        }
        
        // Setup callbacks
        setupCallbacks();
        
        GAMELOOP_INFO("Game application initialized successfully");
        return true;
    }
    
    bool run() {
        if (!m_gameLoop->run()) {
            GAMELOOP_ERROR("Game loop failed to start");
            return false;
        }
        return true;
    }
    
    void shutdown() {
        m_gameLoop->stop();
        
        // Cleanup ThreadSystem
        Hammer::ThreadSystem::Shutdown();
        
        GAMELOOP_INFO("Game application shut down successfully");
    }
    
private:
    void setupCallbacks() {
        // Event handling (main thread)
        m_gameLoop->setEventHandler([this]() {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    m_running.store(false);
                    m_gameLoop->stop();
                }
                // Handle other events...
            }
        });
        
        // Game logic updates (update worker thread)
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            if (m_running.load()) {
                updateGameWorld(deltaTime);
                updatePhysics(deltaTime);
                updateAI(deltaTime);
            }
        });
        
        // Rendering (main thread)
        m_gameLoop->setRenderHandler([this]() {
            if (m_running.load()) {
                renderGame();
            }
        });
    }
};

int main() {
    MyGameApplication app;
    
    if (!app.initialize()) {
        return -1;
    }
    
    app.run();
    app.shutdown();
    
    return 0;
}
```

### High-Performance Configuration

```cpp
class HighPerformanceGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    
public:
    HighPerformanceGameLoop() : 
        m_gameLoop(std::make_shared<GameLoop>(120.0f, 1.0f/120.0f, true)) {
        configureTiming();
    }
    
private:
    void configureTiming() {
        // Enable high-frequency updates for competitive gaming
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            // Track update frequency
            static int updateCount = 0;
            static auto lastTime = std::chrono::high_resolution_clock::now();
            
            updateHighFrequencyLogic(deltaTime);
            
            if (++updateCount % 1200 == 0) { // Every 10 seconds at 120 FPS
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime);
                
                float actualUpdateRate = 1200.0f / (elapsed.count() / 1000.0f);
                GAMELOOP_INFO("High-frequency update rate: " + std::to_string(actualUpdateRate) + " Hz");
                
                lastTime = now;
            }
        });
    }
    
    void updateHighFrequencyLogic(float deltaTime) {
        // Critical game logic that benefits from high update rates
        updatePlayerInput(deltaTime);
        updateNetworking(deltaTime);
        updatePrecisionPhysics(deltaTime);
    }
};
```

### Pause/Resume System

```cpp
class PausableGameLoop {
private:
    std::shared_ptr<GameLoop> m_gameLoop;
    bool m_gameMenuOpen = false;
    
public:
    PausableGameLoop() : m_gameLoop(std::make_shared<GameLoop>()) {
        setupPauseHandling();
    }
    
private:
    void setupPauseHandling() {
        m_gameLoop->setEventHandler([this]() {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    m_gameLoop->stop();
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        togglePause();
                    }
                }
            }
        });
        
        m_gameLoop->setUpdateHandler([this](float deltaTime) {
            if (m_gameLoop->isPaused()) {
                updatePauseMenu(deltaTime);
            } else {
                updateGameLogic(deltaTime);
            }
        });
        
        m_gameLoop->setRenderHandler([this]() {
            renderGame();
            if (m_gameLoop->isPaused()) {
                renderPauseOverlay();
            }
        });
    }
    
    void togglePause() {
        bool wasPaused = m_gameLoop->isPaused();
        m_gameLoop->setPaused(!wasPaused);
        m_gameMenuOpen = !wasPaused;
        
        GAMELOOP_INFO(m_gameMenuOpen ? "Game paused" : "Game resumed");
    }
    
    void updateGameLogic(float deltaTime) {
        // Normal game updates
    }
    
    void updatePauseMenu(float deltaTime) {
        // Update pause menu UI
    }
    
    void renderGame() {
        // Render game world
    }
    
    void renderPauseOverlay() {
        // Render pause menu overlay
    }
};
```

---

*This documentation covers the complete GameLoop system with WorkerBudget integration. For additional details on specific components like TimestepManager or ThreadSystem, refer to their respective documentation files.*