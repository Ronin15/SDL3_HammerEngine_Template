# GameEngine Documentation

**Where to find the code:**
- Implementation: `src/core/GameEngine.cpp`
- Header: `include/core/GameEngine.hpp`

**Singleton Access:** Use `GameEngine::Instance()` to access the engine.

## Overview

The `GameEngine` class is the core singleton that manages all game systems, SDL subsystems, and coordinates the main game loop. It serves as the central hub for initialization, resource management, threading coordination, and system integration in the Hammer Game Engine.

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Initialization](#initialization)
- [Main Loop Integration](#main-loop-integration)
- [Threading and Synchronization](#threading-and-synchronization)
- [Window Management](#window-management)
- [Manager Integration](#manager-integration)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Troubleshooting](#troubleshooting)
- [Examples](#examples)

## Quick Start

### Basic Engine Setup

```cpp
int main() {
    // Initialize thread system first
    HammerEngine::ThreadSystem& threadSystem = HammerEngine::ThreadSystem::Instance();
    if (!threadSystem.init()) {
        return -1;
    }

    // Get GameEngine singleton
    GameEngine& engine = GameEngine::Instance();

    // Initialize with window parameters
    if (!engine.init("My Game", 1280, 720, false)) {
        engine.clean();
        return -1;
    }

    // Push initial state
    engine.getGameStateManager()->pushState("MainMenuState");

    // Get TimestepManager for main loop
    TimestepManager& ts = engine.getTimestepManager();

    // Main game loop - fixed timestep pattern
    while (engine.isRunning()) {
        ts.startFrame();
        engine.handleEvents();

        while (ts.shouldUpdate()) {
            if (engine.hasNewFrameToRender()) {
                engine.swapBuffers();
            }
            engine.update(ts.getUpdateDeltaTime());
        }

        engine.render();
        ts.endFrame();
    }

    engine.clean();
    return 0;
}
```

## Architecture

### Singleton Pattern

The GameEngine uses the Meyer's singleton pattern for thread-safe initialization:

```cpp
class GameEngine {
public:
    static GameEngine& Instance() {
        static GameEngine instance;
        return instance;
    }

private:
    GameEngine() = default;
    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;
};
```

### Core Components

- **SDL Integration**: Window, renderer, and input system management
- **Manager Coordination**: Centralized access to all game managers
- **Threading System**: Multi-threaded task processing with WorkerBudget system
- **Resource Loading**: Asynchronous resource initialization
- **State Management**: Integration with GameStateManager
- **Configurable Buffering**: Thread-safe rendering with lock-free double or triple buffering (configurable via `setBufferCount()`, toggle with F3 in debug mode)

### System Dependencies

The GameEngine initializes and manages:
- SDL3 Video subsystem
- TextureManager (main thread)
- SoundManager (background thread)
- FontManager (background thread)
- InputManager (background thread)
- AIManager (background thread)
- EventManager (background thread)
- SaveGameManager (background thread)
- UIManager (main thread)
- GameStateManager (main thread)

## Initialization

### Initialization Method

```cpp
bool GameEngine::init(std::string_view title, int width, int height, bool fullscreen)
```

**Parameters:**
- `title`: Window title for the game
- `width`: Initial window width (0 for auto-sizing to 1280)
- `height`: Initial window height (0 for auto-sizing to 720)
- `fullscreen`: Whether to start in fullscreen mode

### Initialization Process

1. **SDL Initialization**: Initializes SDL3 video subsystem with quality hints
2. **Window Creation**: Creates window with platform-specific optimizations
3. **Renderer Setup**: Creates hardware-accelerated renderer with adaptive VSync
4. **DPI Calculation**: Calculates display-aware scaling factors
5. **Multi-threaded Manager Initialization**: Initializes managers across 6 background threads
6. **Resource Loading**: Loads textures, sounds, fonts, and other resources
7. **State Setup**: Initializes game states and sets initial LogoState
8. **Buffer Initialization**: Sets up double buffering system
9. **Manager Caching**: Caches frequently accessed manager references for performance

### Platform-Specific Features

#### macOS Optimizations
- Borderless fullscreen desktop mode for compatibility
- Display content scale detection for proper DPI handling
- Logical presentation with letterbox mode (1920x1080 target resolution)
- Spaces integration for fullscreen mode

#### Wayland/Linux Support
- Automatic Wayland detection with VSync fallback
- Software frame rate limiting for timing consistency
- Native resolution rendering to eliminate scaling blur

#### Multi-threaded Initialization
The engine uses 6 background threads for parallel initialization:

```cpp
// Thread #1: Input Manager
initTasks.push_back(threadSystem.enqueueTaskWithResult([]() -> bool {
    InputManager& inputMgr = InputManager::Instance();
    inputMgr.initializeGamePad();
    return true;
}));

// Thread #2: Sound Manager
initTasks.push_back(threadSystem.enqueueTaskWithResult([]() -> bool {
    SoundManager& soundMgr = SoundManager::Instance();
    return soundMgr.init() && soundMgr.loadResources();
}));
```

### Error Handling During Initialization

```cpp
bool GameEngine::init(std::string_view title, int width, int height, bool fullscreen) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        GAMEENGINE_CRITICAL("SDL Video initialization failed: " + std::string(SDL_GetError()));
        return false;
    }

    // Wait for all background initialization tasks
    bool allTasksSucceeded = true;
    for (auto& task : initTasks) {
        try {
            allTasksSucceeded &= task.get();
        } catch (const std::exception& e) {
            GAMEENGINE_ERROR("Initialization task failed: " + std::string(e.what()));
            allTasksSucceeded = false;
        }
    }

    return allTasksSucceeded;
}
```

## Main Loop Integration

### Single-Threaded Main Loop

The main loop runs entirely on the main thread with a classic fixed timestep pattern. The loop is implemented in `HammerMain.cpp`:

```cpp
// Main game loop - fixed timestep pattern
while (engine.isRunning()) {
    ts.startFrame();           // Start frame timing (adds delta to accumulator)
    engine.handleEvents();      // Process SDL events (main thread only)

    while (ts.shouldUpdate()) { // Fixed timestep updates - drain accumulator
        if (engine.hasNewFrameToRender()) {
            engine.swapBuffers();
        }
        engine.update(ts.getUpdateDeltaTime());
    }

    engine.render();           // Render with interpolation alpha
    ts.endFrame();             // VSync or software frame limiting
}
```

### Core Loop Methods

#### Event Handling
```cpp
void GameEngine::handleEvents() {
    // Handle input events - InputManager for SDL event polling architecture
    InputManager& inputMgr = InputManager::Instance();
    inputMgr.update();

    // Handle game state input on main thread (SDL3 requirement)
    mp_gameStateManager->handleInput();
}
```

#### Game Logic Update
```cpp
void GameEngine::update(float deltaTime) {
    // Single-threaded main loop - no mutex needed

    // Hybrid Manager Update Architecture:
    // Global systems updated by GameEngine (cached references for performance)
    if (mp_aiManager) {
        mp_aiManager->update(deltaTime);  // Dispatches work to ThreadSystem
    }
    if (mp_eventManager) {
        mp_eventManager->update(deltaTime);
    }

    // State-managed systems updated by individual game states
    mp_gameStateManager->update(deltaTime);

    // Update frame counters and buffer management
    m_lastUpdateFrame.fetch_add(1, std::memory_order_relaxed);
    m_bufferReady[updateBufferIndex].store(true, std::memory_order_release);
}
```

#### Rendering
```cpp
void GameEngine::render() {
    // Always on MAIN thread (SDL requirement)
    // Single-threaded main loop - no mutex needed

    SDL_SetRenderDrawColor(mp_renderer.get(), HAMMER_GRAY);
    SDL_RenderClear(mp_renderer.get());

    mp_gameStateManager->render(dt);

    SDL_RenderPresent(mp_renderer.get());
    m_lastRenderedFrame.fetch_add(1, std::memory_order_relaxed);
}
```

## Threading and Synchronization

### WorkerBudget Integration & Multi-Threading Support

The GameEngine implements sophisticated threading with centralized resource management through the WorkerBudget system:

**Engine Resource Allocation:**
- Uses `HammerEngine::calculateWorkerBudget()` for centralized resource allocation
- All workers allocated to managers (AI, Particles, Events, Pathfinding)
- Main loop runs single-threaded on main thread
- Submits background tasks with appropriate priorities to prevent system overload

**Threading Architecture:**
- **Single-Threaded Main Loop**: Update and render run sequentially on main thread
- **Manager Integration**: Managers dispatch background work to ThreadSystem workers
- **Queue Pressure Management**: Monitors ThreadSystem load to prevent bottlenecks
- **Configurable Double/Triple Buffering**: Lock-free buffer coordination for render state. Toggle with F3 in debug mode

#### Thread-Safe State Management
```cpp
class GameEngine {
private:
    // Threading synchronization
    std::mutex m_updateMutex{};
    std::condition_variable m_updateCondition{};
    std::atomic<bool> m_updateCompleted{false};
    std::atomic<bool> m_updateRunning{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<uint64_t> m_lastUpdateFrame{0};
    std::atomic<uint64_t> m_lastRenderedFrame{0};

    // Render synchronization
    std::mutex m_renderMutex{};

    // Protection for high entity counts
    std::atomic<size_t> m_entityProcessingCount{0};
};
```

#### Lock-Free Configurable Buffering System (Double/Triple)

The GameEngine supports both **double buffering** (2 buffers) and **triple buffering** (3 buffers) for flexible performance tuning:

```cpp
class GameEngine {
private:
    // Configurable buffer count (default: 2 for double buffering)
    size_t m_bufferCount{2};                    // Set to 3 for triple buffering
    std::atomic<size_t> m_currentBufferIndex{0};
    std::atomic<size_t> m_renderBufferIndex{0};
    std::atomic<bool> m_bufferReady[3]{false, false, false};  // Max 3 buffers

    // Frame tracking for synchronization
    std::atomic<uint64_t> m_lastUpdateFrame{0};
    std::atomic<uint64_t> m_lastRenderedFrame{0};

    // DEBUG MODE ONLY: Buffer telemetry statistics (lock-free counters only)
    struct BufferTelemetryStats {
        std::atomic<uint64_t> swapAttempts{0};   // Total swap attempts
        std::atomic<uint64_t> swapSuccesses{0};  // Successful swaps
        std::atomic<uint64_t> renderStalls{0};   // Times renderer stalled
        std::atomic<uint64_t> framesSkipped{0};  // Skipped frames
    };
    #ifdef DEBUG
    BufferTelemetryStats m_bufferStats{};
    #endif

public:
    // Configure double (2 buffers) or triple (3 buffers) buffering
    void setBufferCount(size_t count) {
        if (count == 2 || count == 3) {
            m_bufferCount = count;
        }
    }

    void swapBuffers() {
        // Thread-safe buffer swap with proper synchronization
        size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
        size_t nextUpdateIndex = (currentIndex + 1) % m_bufferCount;

        // Check if we have a valid render buffer before attempting swap
        size_t currentRenderIndex = m_renderBufferIndex.load(std::memory_order_acquire);

        // Only swap if current buffer is ready AND next buffer isn't being rendered
        if (m_bufferReady[currentIndex].load(std::memory_order_acquire) &&
            nextUpdateIndex != currentRenderIndex) {

            // Atomic compare-exchange to ensure no race condition
            size_t expected = currentIndex;
            if (m_currentBufferIndex.compare_exchange_strong(expected, nextUpdateIndex,
                                                             std::memory_order_acq_rel)) {
                // Successfully swapped update buffer
                // Make previous update buffer available for rendering
                m_renderBufferIndex.store(currentIndex, std::memory_order_release);

                // Clear the next buffer's ready state for the next update cycle
                m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);

                // Signal buffer swap completion
                m_bufferCondition.notify_one();
            }
        }
    }

    bool hasNewFrameToRender() const noexcept {
        // Optimized render check with minimal atomic operations
        size_t renderIndex = m_renderBufferIndex.load(std::memory_order_acquire);

        // Single check for buffer readiness
        if (!m_bufferReady[renderIndex].load(std::memory_order_acquire)) {
            return false;
        }

        // Compare frame counters only if buffer is ready - use relaxed ordering for counters
        uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
        uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);

        return lastUpdate > lastRendered;
    }

    // DEBUG MODE: Get buffer telemetry statistics
    #ifdef DEBUG
    const BufferTelemetryStats& getBufferTelemetry() const {
        return m_bufferStats;
    }
    #endif
};
```

**Double vs Triple Buffering:**

| Feature | Double Buffering (2) | Triple Buffering (3) |
|---------|---------------------|----------------------|
| Memory Usage | Lower (2 buffers) | Higher (3 buffers) |
| Latency | Lower (simpler logic) | Slightly higher |
| Smoothness | Good (no wait) | Excellent (guaranteed) |
| Render Stalls | Possible if update slow | Never (always has buffer) |
| Typical Use | Most games | High frame rate games |

**Usage:**

```cpp
auto& engine = GameEngine::Instance();

// Switch to triple buffering (default is double)
engine.setBufferCount(3);

// In debug mode, check buffer statistics
#ifdef DEBUG
const auto& stats = engine.getBufferTelemetry();
std::cout << "Swap attempts: " << stats.swapAttempts << "\n";
std::cout << "Render stalls: " << stats.renderStalls << "\n";
#endif
```

**Debug Toggle:**

Press **F3** in debug mode to toggle between double and triple buffering at runtime.


#### Background Task Processing

```cpp
void GameEngine::processEngineCoordination(float deltaTime) {
    // Critical engine coordination tasks
    // Runs with high priority in WorkerBudget system
}
```

### Synchronization Methods

#### Update Thread Coordination
```cpp
void GameEngine::waitForUpdate() {
    std::unique_lock<std::mutex> lock(m_updateMutex);
    auto timeout = std::chrono::milliseconds(100);

    bool completed = m_updateCondition.wait_for(lock, timeout,
        [this] { return m_updateCompleted.load(std::memory_order_acquire) ||
                        m_stopRequested.load(std::memory_order_acquire); });

    if (!completed && !m_stopRequested.load(std::memory_order_acquire)) {
        if (m_updateRunning.load(std::memory_order_acquire)) {
            // Give more time for running updates
            m_updateCondition.wait_for(lock, std::chrono::milliseconds(50));
        }
    }
}

void GameEngine::signalUpdateComplete() {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    m_updateCompleted.store(false, std::memory_order_release);
}
```

## Window Management

### Window Properties
```cpp
class GameEngine {
private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{nullptr, SDL_DestroyRenderer};
    int m_windowWidth{1280};
    int m_windowHeight{720};
    int m_logicalWidth{1920};   // Logical rendering width for UI positioning
    int m_logicalHeight{1080};  // Logical rendering height for UI positioning
    SDL_RendererLogicalPresentation m_logicalPresentationMode{SDL_LOGICAL_PRESENTATION_LETTERBOX};
    float m_dpiScale{1.0f};
};
```

### Cross-Platform Coordinate System

#### Platform-Specific Rendering Strategies

**macOS Approach:**
- Uses logical presentation with letterbox mode
- Target resolution: 1920x1080 for consistent UI layout
- Display content scale for proper DPI handling
- Borderless fullscreen desktop mode

**Linux/Windows Approach:**
- Native resolution rendering to eliminate scaling blur
- Disabled logical presentation for pixel-perfect rendering
- Direct pixel coordinate system

#### Coordinate System Benefits

- **Consistent UI Layout**: UI elements position correctly across platforms
- **DPI Awareness**: Automatic scaling for high-DPI displays
- **Performance**: Native resolution on supported platforms eliminates scaling overhead
- **Compatibility**: Fallback modes ensure operation on all systems

### VSync Management

#### Platform Compatibility

**Adaptive VSync Handling:**
- **Wayland Detection**: Automatic detection with fallback to software limiting
- **X11/Windows**: Hardware VSync preferred for smooth presentation
- **macOS**: Native VSync support with display sync

**Detection Logic:**
```cpp
const std::string videoDriver = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
bool isWayland = (videoDriver == "wayland");

// Fallback environment detection
if (!isWayland) {
    const std::string sessionType = std::getenv("XDG_SESSION_TYPE") ? std::getenv("XDG_SESSION_TYPE") : "";
    const std::string waylandDisplay = std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "";
    isWayland = (sessionType == "wayland") || !waylandDisplay.empty();
}

if (isWayland) {
    SDL_SetRenderVSync(mp_renderer.get(), 0);  // Disable for Wayland
} else {
    SDL_SetRenderVSync(mp_renderer.get(), 1);  // Enable for other platforms
}
```

#### API Methods

**VSync Control:**
```cpp
bool GameEngine::isVSyncEnabled() const noexcept {
    if (!mp_renderer) return false;

    int vsync = 0;
    if (SDL_GetRenderVSync(mp_renderer.get(), &vsync)) {
        return (vsync > 0);
    }
    return false;
}

bool GameEngine::setVSyncEnabled(bool enable) {
    if (!mp_renderer) return false;

    return SDL_SetRenderVSync(mp_renderer.get(), enable ? 1 : 0);
}
```

#### Troubleshooting VSync Issues

**Common Issues:**
- **Wayland Timing Problems**: Automatic software fallback prevents frame time inconsistencies
- **Driver Compatibility**: Graceful degradation to software limiting
- **Performance Impact**: Monitor frame time consistency with TimestepManager

**Debugging VSync:**
```cpp
// TimestepManager provides frame timing information
TimestepManager& ts = engine.getTimestepManager();
float targetFPS = 1.0f / ts.getUpdateDeltaTime();  // Target FPS from fixed timestep
```

### Window Management Methods

#### Size Management
```cpp
int getWindowWidth() const noexcept { return m_windowWidth; }
int getWindowHeight() const noexcept { return m_windowHeight; }
int getLogicalWidth() const noexcept { return m_logicalWidth; }
int getLogicalHeight() const noexcept { return m_logicalHeight; }
void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }
```

#### Logical Presentation
```cpp
void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode) {
    m_logicalPresentationMode = mode;
    if (mp_renderer) {
        int width, height;
        SDL_GetWindowSize(mp_window.get(), &width, &height);
        SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height, mode);
    }
}
```

## Manager Integration

### Hybrid Manager Architecture

The GameEngine implements a hybrid approach to manager updates:

**Global Systems (Updated by GameEngine):**
- **AIManager**: World simulation with 10K+ entities, cached reference for performance
- **EventManager**: Global game events with batch processing, cached reference

**State-Managed Systems (Updated by individual states):**
- **UIManager**: Optional, state-specific, only updated when UI is actually used
- **InputManager**: Handled in handleEvents() for proper SDL event polling architecture

### Cached Manager References with WorkerBudget Integration

```cpp
class GameEngine {
private:
    // Cached manager references for zero-overhead performance
    AIManager* mp_aiManager{nullptr};
    EventManager* mp_eventManager{nullptr};
    // InputManager not cached - handled in handleEvents() for proper SDL architecture

public:
    // Manager caching happens after background initialization completes
    // Validation ensures managers are properly initialized before caching
};
```

**Manager Validation During Caching:**
```cpp
// Validate AI Manager before caching
AIManager& aiMgrTest = AIManager::Instance();
if (!aiMgrTest.isInitialized()) {
    GAMEENGINE_CRITICAL("AIManager not properly initialized before caching!");
    return false;
}
mp_aiManager = &aiMgrTest;

// Validate Event Manager before caching
EventManager& eventMgrTest = EventManager::Instance();
if (!eventMgrTest.isInitialized()) {
    GAMEENGINE_CRITICAL("EventManager not properly initialized before caching!");
    return false;
}
mp_eventManager = &eventMgrTest;
```

### WorkerBudget Coordination Architecture

```cpp
void GameEngine::update(float deltaTime) {
    // Use WorkerBudget system for coordinated task submission
    if (HammerEngine::ThreadSystem::Exists()) {
        auto& threadSystem = HammerEngine::ThreadSystem::Instance();
        size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
        HammerEngine::WorkerBudget budget = HammerEngine::calculateWorkerBudget(availableWorkers);

        // Engine receives 1 worker from WorkerBudget allocation
        threadSystem.enqueueTask([this, deltaTime]() {
            processEngineCoordination(deltaTime);
        }, HammerEngine::TaskPriority::High, "GameEngine_Coordination");
    }
}
```

### Manager Access Methods
```cpp
GameStateManager* getGameStateManager() const { return mp_gameStateManager.get(); }
SDL_Renderer* getRenderer() const noexcept { return mp_renderer.get(); }
SDL_Window* getWindow() const noexcept { return mp_window.get(); }
float getDPIScale() const { return m_dpiScale; }
```

### Performance Monitoring Integration

```cpp
// Performance monitoring in HammerMain.cpp (DEBUG builds)
// Rolling average of update times logged every 1800 frames
double avgMs = calculateRollingAverage(updateSamples);
double frameBudgetMs = 1000.0 / 60.0;  // 16.67ms
double utilizationPercent = (avgMs / frameBudgetMs) * 100.0;
GAMEENGINE_DEBUG(std::format("Update performance: {:.2f}ms avg ({:.1f}% frame budget)", avgMs, utilizationPercent));
```

## API Reference

### Core Lifecycle Methods

```cpp
static GameEngine& Instance();                                    // Get singleton instance
bool init(std::string_view title, int width, int height, bool fullscreen); // Initialize engine
void handleEvents();                                             // Handle SDL events and input
void update(float dt);                                    // Update game logic (thread-safe)
void render(float dt);                                                   // Render frame (main thread only)
void clean();                                                    // Cleanup all resources
```

### Threading Methods

```cpp
void processBackgroundTasks();                                   // Process background tasks
void processEngineCoordination(float dt);                        // Engine coordination (high priority)
void waitForUpdate();                                            // Wait for update completion
void signalUpdateComplete();                                     // Signal update complete
bool hasNewFrameToRender() const noexcept;                     // Check if new frame ready
bool isUpdateRunning() const noexcept;                         // Check if update in progress
void swapBuffers();                                             // Swap double buffers
size_t getCurrentBufferIndex() const noexcept;                 // Get current update buffer
size_t getRenderBufferIndex() const noexcept;                  // Get render buffer index
```

### State Management

```cpp
void setRunning(bool running);                                  // Set engine running state
bool getRunning() const;                                        // Get engine running state
bool isRunning() const;                                         // Check if engine is running
TimestepManager& getTimestepManager();                          // Get TimestepManager for main loop
```

### Window and Rendering

```cpp
int getWindowWidth() const noexcept;                           // Get window width
int getWindowHeight() const noexcept;                          // Get window height
int getLogicalWidth() const noexcept;                          // Get logical width
int getLogicalHeight() const noexcept;                         // Get logical height
void setWindowSize(int width, int height);                     // Set window size
void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode); // Set presentation mode
SDL_RendererLogicalPresentation getLogicalPresentationMode() const noexcept; // Get presentation mode
float getDPIScale() const;                                      // Get DPI scale factor
void setDPIScale(float newScale);                              // Set DPI scale factor
int getOptimalDisplayIndex() const;                            // Get optimal display index
bool isVSyncEnabled() const noexcept;                          // Check VSync status
bool setVSyncEnabled(bool enable);                             // Set VSync on/off
```

### Resource Management

```cpp
GameStateManager* getGameStateManager() const;                 // Get GameStateManager pointer
SDL_Renderer* getRenderer() const noexcept;                   // Get SDL renderer
SDL_Window* getWindow() const noexcept;                       // Get SDL window
```

## Best Practices

### 1. Proper Initialization Order

```cpp
bool initializeGame() {
    // Initialize ThreadSystem first
    HammerEngine::ThreadSystem& threadSystem = HammerEngine::ThreadSystem::Instance();
    if (!threadSystem.init()) {
        return false;
    }

    GameEngine& engine = GameEngine::Instance();

    // Initialize engine
    if (!engine.init("My Game", 1280, 720, false)) {
        GAMEENGINE_ERROR("Failed to initialize GameEngine");
        return false;
    }

    // Push initial state
    engine.getGameStateManager()->pushState("MainMenuState");

    return true;
}
```

### 2. Thread-Safe Resource Access

```cpp
void workerThreadFunction() {
    // Safe to call from worker threads
    GameEngine& engine = GameEngine::Instance();

    // These methods are thread-safe
    bool hasFrame = engine.hasNewFrameToRender();
    size_t bufferIndex = engine.getCurrentBufferIndex();

    // Access managers through engine (cached references)
    if (auto* gameStateManager = engine.getGameStateManager()) {
        // Safe access to game state
    }
}
```

### 3. Proper VSync Handling

```cpp
void setupRenderer() {
    GameEngine& engine = GameEngine::Instance();

    // Check VSync support
    if (engine.isVSyncEnabled()) {
        GAMEENGINE_INFO("VSync enabled - using hardware timing");
    } else {
        GAMEENGINE_INFO("VSync disabled - using software timing");
        // TimestepManager handles software frame limiting automatically
    }
}

void optimizeForPlatform() {
    GameEngine& engine = GameEngine::Instance();

    #ifdef __APPLE__
    // macOS uses letterbox mode for consistent UI
    engine.setLogicalPresentationMode(SDL_LOGICAL_PRESENTATION_LETTERBOX);
    #else
    // Other platforms use native resolution
    engine.setLogicalPresentationMode(SDL_LOGICAL_PRESENTATION_DISABLED);
    #endif
}
```

### 4. Error Handling

```cpp
bool safeEngineOperation() {
    try {
        GameEngine& engine = GameEngine::Instance();

        // Always check if engine is properly initialized
        if (!engine.getRenderer()) {
            GAMEENGINE_ERROR("Engine not properly initialized");
            return false;
        }

        // Perform operations...
        return true;
    } catch (const std::exception& e) {
        GAMEENGINE_ERROR("Engine operation failed: " + std::string(e.what()));
        return false;
    }
}
```

## Troubleshooting

### VSync and Timing Issues

**Problem**: Stuttering or inconsistent frame times
**Solution**:
- Check if VSync is properly enabled: `engine.isVSyncEnabled()`
- On Wayland, the engine automatically falls back to software timing
- Monitor FPS with `engine.getCurrentFPS()`

**Problem**: VSync not working on Linux
**Solution**:
- The engine automatically detects Wayland and disables VSync
- Use environment variables for manual override if needed
- Check SDL video driver: `SDL_GetCurrentVideoDriver()`

### Performance Issues

**Symptoms**: Low FPS, high CPU usage, frame drops
**Debugging Steps**:

1. **Check Thread System Load**:
   ```cpp
   if (HammerEngine::ThreadSystem::Instance().isBusy()) {
       GAMEENGINE_WARN("Thread system overloaded");
   }
   ```

2. **Monitor Manager Performance**:
   - AI Manager entity count: Check if entity processing is too high
   - Event Manager: Check for event processing bottlenecks
   - UI Manager: Only update when UI is active

3. **Buffer System Health**:
   ```cpp
   bool hasFrame = engine.hasNewFrameToRender();
   bool updateRunning = engine.isUpdateRunning();
   ```

### Movement and Animation Issues

**Problem**: Jerky movement or animation
**Cause**: Usually related to timing or buffer synchronization
**Solution**:
```cpp
void Entity::update(float dt) {
    // Use provided dt for consistent movement
    position += velocity * dt;

    // Don't create your own timing system
}
```

## Examples

### Complete Game Setup

```cpp
class MyGame {
public:
    MyGame() = default;

    bool initialize() {
        // Initialize ThreadSystem first
        HammerEngine::ThreadSystem& threadSystem = HammerEngine::ThreadSystem::Instance();
        if (!threadSystem.init()) {
            return false;
        }

        // Initialize engine
        GameEngine& engine = GameEngine::Instance();
        if (!engine.init("My Game", 1920, 1080, false)) {
            GAMEENGINE_ERROR("Failed to initialize GameEngine");
            return false;
        }

        // Load game-specific resources
        if (!loadGameResources()) {
            GAMEENGINE_ERROR("Failed to load game resources");
            return false;
        }

        // Push initial state
        engine.getGameStateManager()->pushState("MainMenuState");

        GAMEENGINE_INFO("Game initialized successfully");
        return true;
    }

    void run() {
        GameEngine& engine = GameEngine::Instance();
        TimestepManager& ts = engine.getTimestepManager();

        // Main game loop - fixed timestep pattern
        while (engine.isRunning()) {
            ts.startFrame();
            engine.handleEvents();

            while (ts.shouldUpdate()) {
                if (engine.hasNewFrameToRender()) {
                    engine.swapBuffers();
                }
                engine.update(ts.getUpdateDeltaTime());
            }

            engine.render();
            ts.endFrame();
        }
    }

    void shutdown() {
        GameEngine& engine = GameEngine::Instance();
        engine.clean();

        GAMEENGINE_INFO("Game shutdown complete");
    }

private:
    bool loadGameResources() {
        // Load game-specific resources here
        return true;
    }
};

int main() {
    MyGame game;

    if (!game.initialize()) {
        return -1;
    }

    game.run();
    game.shutdown();

    return 0;
}
```

### Multi-threaded Resource Loading

```cpp
class AsyncResourceLoader {
private:
    std::atomic<bool> m_loadingComplete{false};
    std::vector<std::future<bool>> m_loadingTasks;

public:
    AsyncResourceLoader() = default;

    void startLoading() {
        GameEngine& engine = GameEngine::Instance();
        auto& threadSystem = HammerEngine::ThreadSystem::Instance();

        // Load textures in background
        m_loadingTasks.push_back(
            threadSystem.enqueueTaskWithResult([&engine]() -> bool {
                TextureManager& texMgr = TextureManager::Instance();
                return texMgr.loadFromDirectory("res/textures", engine.getRenderer());
            })
        );

        // Load sounds in background
        m_loadingTasks.push_back(
            threadSystem.enqueueTaskWithResult([]() -> bool {
                SoundManager& soundMgr = SoundManager::Instance();
                return soundMgr.loadFromDirectory("res/sounds");
            })
        );

        // Start completion check
        threadSystem.enqueueTask([this]() {
            waitForCompletion();
        });
    }

    bool isComplete() const {
        return m_loadingComplete.load();
    }

private:
    void waitForCompletion() {
        bool allSucceeded = true;
        for (auto& task : m_loadingTasks) {
            try {
                allSucceeded &= task.get();
            } catch (const std::exception& e) {
                GAMEENGINE_ERROR("Resource loading failed: " + std::string(e.what()));
                allSucceeded = false;
            }
        }

        m_loadingComplete.store(allSucceeded);

        if (allSucceeded) {
            GAMEENGINE_INFO("All resources loaded successfully");
        } else {
            GAMEENGINE_ERROR("Some resources failed to load");
        }
    }
};
```

### Performance Monitoring Integration

```cpp
class PerformanceMonitor {
private:
    std::chrono::high_resolution_clock::time_point m_lastFrame;
    float m_frameTimeAccumulator{0.0f};
    int m_frameCount{0};

public:
    PerformanceMonitor() : m_lastFrame(std::chrono::high_resolution_clock::now()) {}

    void updatePerformanceMetrics() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto frameTime = std::chrono::duration<float>(currentTime - m_lastFrame).count();
        m_lastFrame = currentTime;

        m_frameTimeAccumulator += frameTime;
        m_frameCount++;

        // Report every second
        if (m_frameTimeAccumulator >= 1.0f) {
            GameEngine& engine = GameEngine::Instance();
            float engineFPS = engine.getCurrentFPS();
            float calculatedFPS = static_cast<float>(m_frameCount) / m_frameTimeAccumulator;

            GAMEENGINE_INFO("Engine FPS: " + std::to_string(engineFPS) +
                          ", Calculated FPS: " + std::to_string(calculatedFPS) +
                          ", Update Running: " + std::to_string(engine.isUpdateRunning()) +
                          ", New Frame Available: " + std::to_string(engine.hasNewFrameToRender()));

            // Reset counters
            m_frameTimeAccumulator = 0.0f;
            m_frameCount = 0;
        }
    }
};
```

---

*This documentation reflects the current GameEngine implementation with SDL3, multi-threaded initialization, WorkerBudget integration, and cross-platform compatibility. The engine provides a robust foundation for game development with automatic platform optimization and thread-safe operations.*
