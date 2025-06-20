# GameEngine Documentation

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
    // Get singleton instance
    GameEngine& engine = GameEngine::Instance();
    
    // Initialize with window parameters
    if (!engine.init("My Game", 1280, 720, false)) {
        return -1;
    }
    
    // Create and set game loop
    auto gameLoop = std::make_shared<GameLoop>();
    engine.setGameLoop(gameLoop);
    
    // Start the main loop
    gameLoop->run();
    
    // Cleanup
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
3. **Renderer Setup**: Creates hardware-accelerated renderer with VSync
4. **DPI Calculation**: Calculates display-aware scaling factors
5. **Multi-threaded Manager Initialization**: Initializes managers across multiple threads
6. **Resource Loading**: Loads textures, sounds, fonts, and other resources
7. **State Setup**: Initializes game states and sets initial state
8. **Buffer Initialization**: Sets up double buffering system

### Platform-Specific Features

#### macOS Optimizations
```cpp
#ifdef __APPLE__
SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");
// Use borderless fullscreen desktop mode
SDL_SetWindowFullscreenMode(mp_window.get(), nullptr);
#endif
```

#### Multi-threaded Initialization
```cpp
// Example: Sound manager initialization in background thread
initTasks.push_back(
    Hammer::ThreadSystem::Instance().enqueueTaskWithResult([]() -> bool {
        SoundManager& soundMgr = SoundManager::Instance();
        return soundMgr.init();
    }));
```

### Error Handling During Initialization

```cpp
bool GameEngine::init(std::string_view title, int width, int height, bool fullscreen) {
    if (SDL_Init(SDL_INIT_VIDEO)) {
        // SDL initialization
    } else {
        GAMEENGINE_CRITICAL("SDL Video initialization failed! SDL error: " + std::string(SDL_GetError()));
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

### GameLoop Delegation

The GameEngine delegates main loop control to a GameLoop instance:

```cpp
class GameEngine {
private:
    std::weak_ptr<GameLoop> m_gameLoop;  // Non-owning reference
    
public:
    void setGameLoop(std::shared_ptr<GameLoop> gameLoop) { 
        m_gameLoop = gameLoop; 
    }
};
```

### Core Loop Methods

#### Event Handling
```cpp
void GameEngine::handleEvents() {
    // Delegates to InputManager for proper SDL event polling
    // InputManager is not cached to maintain SDL architecture
}
```

#### Game Logic Update
```cpp
void GameEngine::update(float deltaTime) {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    
    // Use WorkerBudget system for coordinated task submission
    if (Hammer::ThreadSystem::Exists()) {
        auto& threadSystem = Hammer::ThreadSystem::Instance();
        size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
        Hammer::WorkerBudget budget = Hammer::calculateWorkerBudget(availableWorkers);

        // Submit engine coordination tasks
        threadSystem.enqueueTask([this, deltaTime]() {
            processEngineCoordination(deltaTime);
        }, Hammer::TaskPriority::High);

        // Submit secondary tasks if multiple workers available
        if (budget.engineReserved > 1) {
            threadSystem.enqueueTask([this]() {
                processEngineSecondaryTasks();
            }, Hammer::TaskPriority::Normal);
        }
    }
    
    // Hybrid Manager Update Architecture:
    // Global systems updated by GameEngine, state-specific by individual states
    
    // Global Systems (cached references for performance):
    if (mp_aiManager) {
        mp_aiManager->update(deltaTime);
    }
    if (mp_eventManager) {
        mp_eventManager->update();
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
void GameEngine::render(float interpolation) {
    // Always on MAIN thread (SDL requirement)
    std::lock_guard<std::mutex> lock(m_renderMutex);

    SDL_SetRenderDrawColor(mp_renderer.get(), HAMMER_GRAY);
    SDL_RenderClear(mp_renderer.get());
    
    mp_gameStateManager->render();
    
    SDL_RenderPresent(mp_renderer.get());
    m_lastRenderedFrame.fetch_add(1, std::memory_order_relaxed);
}
```

## Threading and Synchronization

### WorkerBudget Integration & Multi-Threading Support

The GameEngine implements sophisticated threading with centralized resource management through the WorkerBudget system:

**Engine Resource Allocation:**
- Receives **2 workers** (Critical priority) from ThreadSystem's WorkerBudget system
- Uses `Hammer::calculateWorkerBudget()` for centralized resource allocation
- Coordinates with GameLoop to ensure optimal frame-rate performance
- Submits tasks with appropriate priorities to prevent system overload

**Threading Architecture:**
- **Primary Coordination**: Engine coordination tasks with High priority
- **Secondary Tasks**: Resource management and cleanup with Normal priority (only if 2+ workers allocated)
- **Manager Integration**: Coordinates AI (60%) and Event (30%) manager threading
- **Queue Pressure Management**: Monitors ThreadSystem load to prevent bottlenecks

#### Thread-Safe State Management
```cpp
class GameEngine {
private:
    // Threading synchronization
    std::mutex m_updateMutex;
    std::condition_variable m_updateCondition;
    std::atomic<bool> m_updateCompleted{false};
    std::atomic<bool> m_updateRunning{false};
    std::atomic<uint64_t> m_lastUpdateFrame{0};
    std::atomic<uint64_t> m_lastRenderedFrame{0};
    
    // Render synchronization
    std::mutex m_renderMutex;
};
```

#### Lock-Free Double Buffering System
```cpp
class GameEngine {
private:
    static constexpr size_t BUFFER_COUNT = 2;
    std::atomic<size_t> m_currentBufferIndex{0};
    std::atomic<size_t> m_renderBufferIndex{0};
    std::atomic<bool> m_bufferReady[BUFFER_COUNT]{false, false};
    std::condition_variable m_bufferCondition;
    
public:
    void swapBuffers() {
        size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
        size_t nextUpdateIndex = (currentIndex + 1) % BUFFER_COUNT;
        
        if (m_bufferReady[currentIndex].load(std::memory_order_acquire)) {
            m_renderBufferIndex.store(currentIndex, std::memory_order_release);
            m_currentBufferIndex.store(nextUpdateIndex, std::memory_order_release);
            m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);
        }
    }

    bool hasNewFrameToRender() const {
        uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
        uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);
        return lastUpdate > lastRendered || (lastUpdate == 1 && lastRendered == 0);
    }
};
```

#### Background Task Processing

```cpp
void GameEngine::processEngineCoordination(float deltaTime) {
    // Critical engine coordination tasks
    // Runs with high priority in WorkerBudget system
}

void GameEngine::processEngineSecondaryTasks() {
    // Secondary tasks that only run with multiple workers allocated
    // Examples: performance monitoring, resource cleanup
}
```

#### Threading Implementation Details

**WorkerBudget Task Submission:**
```cpp
void GameEngine::update(float deltaTime) {
    // Use WorkerBudget system for coordinated task submission
    if (Hammer::ThreadSystem::Exists()) {
        auto& threadSystem = Hammer::ThreadSystem::Instance();

        // Calculate worker budget for this frame
        size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
        Hammer::WorkerBudget budget = Hammer::calculateWorkerBudget(availableWorkers);

        // Submit engine coordination tasks with high priority
        threadSystem.enqueueTask([this, deltaTime]() {
            processEngineCoordination(deltaTime);
        }, Hammer::TaskPriority::High, "GameEngine_Coordination");

        // Only submit secondary tasks if multiple workers allocated
        if (budget.engineReserved > 1) {
            threadSystem.enqueueTask([this]() {
                processEngineSecondaryTasks();
            }, Hammer::TaskPriority::Normal, "GameEngine_Secondary");
        }
    }
}
```

**Manager Update Architecture:**
- **Global Systems**: AIManager and EventManager updated by GameEngine for consistency
- **State-Specific Systems**: UIManager and others updated by individual game states
- **Cached References**: Zero-overhead access to WorkerBudget-participating managers
- **Exception Handling**: Robust error handling prevents single manager failures from crashing the engine

**Resource Coordination:**
- Engine coordinates with GameLoop's Critical priority allocation
- Respects AI (60%) and Event (30%) manager worker budgets
- Utilizes buffer threads for secondary tasks when available
- Monitors queue pressure to prevent system overload

### Synchronization Methods

#### Update Thread Coordination
```cpp
void GameEngine::waitForUpdate() {
    std::unique_lock<std::mutex> lock(m_updateMutex);
    if (!m_updateCondition.wait_for(lock, std::chrono::milliseconds(100),
        [this] { return m_updateCompleted.load(std::memory_order_acquire); })) {
        // Timeout handling for high entity counts
        m_updateCompleted.store(true, std::memory_order_release);
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
    SDL_RendererLogicalPresentation m_logicalPresentationMode{SDL_LOGICAL_PRESENTATION_LETTERBOX};
    float m_dpiScale{1.0f};
};
```

### Cross-Platform Coordinate System

The engine automatically handles coordinate systems and rendering for optimal text quality across platforms:

#### Platform-Specific Rendering Strategies

**macOS (Letterbox Mode):**
```cpp
// On macOS, use aspect ratio-based logical resolution for compatibility
int actualWidth, actualHeight;
SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth, &actualHeight);

// Calculate logical resolution based on actual screen aspect ratio
float aspectRatio = static_cast<float>(actualWidth) / static_cast<float>(actualHeight);
int targetLogicalHeight = 1080;  // Keep consistent height
int targetLogicalWidth = static_cast<int>(std::round(targetLogicalHeight * aspectRatio));

// Use letterbox mode to maintain aspect ratio and avoid black bars
SDL_SetRenderLogicalPresentation(mp_renderer.get(), targetLogicalWidth, targetLogicalHeight, 
                                SDL_LOGICAL_PRESENTATION_LETTERBOX);
```

**Windows/Linux (Native Resolution):**
```cpp
// On non-Apple platforms, use actual screen resolution to eliminate scaling blur
int actualWidth, actualHeight;
SDL_GetWindowSizeInPixels(mp_window.get(), &actualWidth, &actualHeight);

// Store actual dimensions for UI positioning (no scaling needed)
m_logicalWidth = actualWidth;
m_logicalHeight = actualHeight;

// Disable logical presentation to render at native resolution
SDL_SetRenderLogicalPresentation(mp_renderer.get(), actualWidth, actualHeight, 
                                SDL_LOGICAL_PRESENTATION_DISABLED);
```

#### Coordinate System Benefits

- **Eliminates Text Blurriness**: Native resolution rendering on Windows/Linux prevents scaling artifacts
- **Cross-Platform Consistency**: Each platform uses its optimal rendering approach
- **Automatic Adaptation**: Systems automatically adapt to the engine's coordinate choice
- **Zero Coupling**: UI and Font systems automatically query engine for logical dimensions

### Font System Integration

The engine coordinates with FontManager for platform-optimized font sizing:

```cpp
// Platform-specific font size calculation
#ifdef __APPLE__
// On macOS, use fixed 18px base with logical presentation scaling
float baseSizeFloat = 18.0f;
#else
// On non-Apple platforms, calculate based on actual screen resolution
// Formula: height / 90 with 18px minimum for readability
int clampedHeight = std::clamp(windowHeight, 480, 8640);
float baseSizeFloat = std::max(static_cast<float>(clampedHeight) / 90.0f, 18.0f);
#endif
```

**Font Sizing Results:**
- **macOS (any resolution)**: 18px base font with logical scaling
- **Windows/Linux 1080p**: 18px base font (minimum enforced) 
- **Windows/Linux 4K**: 24px base font (dynamically calculated)
- **All platforms**: Consistent readability and crisp rendering

### VSync Management

The GameEngine implements intelligent VSync management with platform-specific optimizations to handle compatibility issues across different display servers and graphics drivers.

#### Platform Compatibility
**Platform Compatibility and Fixed Timestep Solution**

**VSync Timing Issues on Wayland**

Wayland display server can experience timing issues when VSync is enabled, leading to:
- Micro-stuttering and pixel-level "ticking" in movement
- Inconsistent deltaTime values causing jerky animations
- Poor visual smoothness despite stable frame rates

**Automatic Platform Detection and Fixed Timestep**

The GameEngine automatically detects Wayland and configures a fixed timestep system for smooth movement:

```cpp
// Platform detection in GameEngine::init() - Modern C++17
const std::string videoDriverRaw = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
std::string_view videoDriver = videoDriverRaw;
bool isWayland = (videoDriver == "wayland");

// Fallback to environment detection with null safety
if (!isWayland) {
    const std::string sessionTypeRaw = std::getenv("XDG_SESSION_TYPE") ? std::getenv("XDG_SESSION_TYPE") : "";
    const std::string waylandDisplayRaw = std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "";
    
    std::string_view sessionType = sessionTypeRaw;
    bool hasWaylandDisplay = !waylandDisplayRaw.empty();
    
    isWayland = (sessionType == "wayland") || hasWaylandDisplay;
}

if (isWayland) {
    // Disable VSync on Wayland to prevent timing issues
    SDL_SetRenderVSync(mp_renderer.get(), 0);
    GAMEENGINE_WARN("Detected Wayland session - using software frame limiting");
} else {
    // Enable VSync on stable platforms (X11, macOS, Windows)
    if (SDL_SetRenderVSync(mp_renderer.get(), 1)) {
        GAMEENGINE_INFO("VSync enabled - hardware-synchronized presentation");
    } else {
        GAMEENGINE_WARN("VSync failed - falling back to software limiting");
    }
}
```

**Fixed Timestep Configuration:**

The TimestepManager is automatically configured in HammerMain after GameLoop initialization using modern C++17:

```cpp
// Configure TimestepManager for platform-specific frame limiting using modern C++17
// This must happen after GameLoop is set but before the game starts running
const std::string sessionTypeRaw = std::getenv("XDG_SESSION_TYPE") ? std::getenv("XDG_SESSION_TYPE") : "";
const std::string waylandDisplayRaw = std::getenv("WAYLAND_DISPLAY") ? std::getenv("WAYLAND_DISPLAY") : "";

std::string_view sessionType = sessionTypeRaw;
bool hasWaylandDisplay = !waylandDisplayRaw.empty();
bool isWayland = (sessionType == "wayland") || hasWaylandDisplay;

if (isWayland) {
    gameLoop->getTimestepManager().setSoftwareFrameLimiting(true);
    GAMELOOP_INFO("Configured TimestepManager for Wayland software frame limiting");
} else {
    gameLoop->getTimestepManager().setSoftwareFrameLimiting(false);
    GAMELOOP_INFO("Configured TimestepManager for hardware VSync");
}
```

**Modern C++ Implementation Notes:**
- Uses `std::string_view` for efficient, zero-copy string operations (C++17)
- Employs type-safe string comparisons instead of C-style `strcmp()`
- Implements explicit null-safety checks to prevent undefined behavior
- Leverages RAII smart pointers with `.get()` for SDL API interoperability
- Avoids raw pointer ownership while safely interfacing with C APIs
- **HammerMain Configuration**: Modern C++17 patterns for environment variable handling
- **Null-Safe Environment Access**: Immediate wrapping of raw pointers in `std::string_view`

**Platform-Specific Behavior:**
- **Linux Wayland**: VSync disabled, fixed timestep (16.667ms) for smooth movement
- **Linux X11**: VSync enabled, variable timestep based on actual frame time
- **macOS**: VSync enabled, variable timestep (excellent VSync support)
- **Windows**: VSync enabled, variable timestep (reliable VSync implementation)

#### API Methods

```cpp
bool GameEngine::isVSyncEnabled() const {
    if (!mp_renderer) return false;

    int vsync = 0;
    if (SDL_GetRenderVSync(mp_renderer.get(), &vsync)) {
        return (vsync > 0);
    }
    return false;
}

bool GameEngine::setVSyncEnabled(bool enable) {
    if (!mp_renderer) return false;
    return SDL_SetRenderVSync(mp_renderer.get(), enable ? 1 : 0) == 0;
}
```

#### Troubleshooting VSync Issues
**Fixed Timestep Implementation:**

The TimestepManager automatically provides consistent deltaTime based on platform:

```cpp
float TimestepManager::getUpdateDeltaTime() const {
    // For VSync platforms, use actual frame time (already smooth)
    if (!m_usingSoftwareFrameLimiting) {
        return static_cast<float>(m_lastFrameTimeMs) / 1000.0f;
    }
    
    // For software frame limiting, use fixed timestep for perfect consistency
    return m_fixedTimestep; // Always 16.667ms at 60 FPS
}
```

**Movement Consistency Benefits:**
- **Wayland**: `position += velocity * 16.667ms` (perfect consistency)
- **VSync Platforms**: `position += velocity * actualFrameTime` (hardware smooth)
- **Eliminates**: Micro-stuttering, pixel-level ticking, jerky animations

**Performance Monitoring:**
The GameLoop provides performance metrics to identify timing issues:
```
[GameLoop] DEBUG: Update performance: 2.5ms avg (15% frame budget)
```
Normal frame budget should be under 50% at target FPS.

### Window Management Methods

#### Size Management
```cpp
int getWindowWidth() const { return m_windowWidth; }
int getWindowHeight() const { return m_windowHeight; }
void setWindowSize(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;
}
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

The GameEngine uses a hybrid approach for manager updates:

**Global Systems** (Updated by GameEngine):
- AIManager: World simulation with 10K+ entities
- EventManager: Global game events, batch processing

**State-Managed Systems** (Updated by individual states):
- UIManager: Optional, state-specific updates
- Other state-specific managers

### Cached Manager References with WorkerBudget Integration

For optimal performance, the GameEngine caches references to WorkerBudget-participating managers:

```cpp
class GameEngine {
private:
    // Cached manager references for zero-overhead performance
    // These managers integrate with WorkerBudget system for threading
    AIManager* mp_aiManager{nullptr};        // 60% worker allocation
    EventManager* mp_eventManager{nullptr};  // 30% worker allocation

    // InputManager not cached - handled in handleEvents() for proper SDL event polling
    // (SDL requires main thread event processing)
};
```

**WorkerBudget Integration Advantages:**
- **Direct Access**: Zero-overhead cached references for frequent WorkerBudget coordination
- **Thread Coordination**: Managers coordinate through centralized WorkerBudget calculation
- **Performance Monitoring**: Direct access enables real-time performance tracking
**Resource Management**: Cached references allow efficient worker allocation monitoring

### WorkerBudget Coordination Architecture

The GameEngine implements sophisticated coordination with the WorkerBudget system:

**Centralized Resource Management:**
```cpp
void GameEngine::update(float deltaTime) {
    // Calculate worker budget for coordinated resource allocation
    size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
    Hammer::WorkerBudget budget = Hammer::calculateWorkerBudget(availableWorkers);

    // Resource allocation coordination:
    // - GameLoop: 2 workers (Critical priority)
    // - GameEngine: 2 workers (engine tasks)
    // - AIManager: 60% of remaining workers
    // - EventManager: 30% of remaining workers
    // - Buffer: Remaining workers for burst capacity
}
```

**Task Priority Coordination:**
- **Critical Priority**: GameLoop tasks (frame-rate consistency)
- **High Priority**: Engine coordination tasks
- **Normal Priority**: Secondary engine tasks (when 2+ workers available)
- **AI/Event Tasks**: Managed by respective managers with their allocated budgets

**Performance Scaling Examples:**
- **4-core/8-thread system (7 workers)**: Engine gets 2 workers for coordination tasks
- **8-core/16-thread system (15 workers)**: Engine gets 2 workers + enhanced secondary task processing
- **32-thread system (31 workers)**: Engine gets 2 workers + full secondary task utilization - AMD 7950X3D (16c/32t), Intel 13900K/14900K (24c/32t)

**Integration Benefits:**
- **Centralized Coordination**: Single WorkerBudget calculation shared across all systems
- **Resource Fairness**: Prevents any single system from monopolizing worker threads
- **Adaptive Scaling**: Automatically adjusts based on available hardware
- **Queue Pressure Management**: Coordinated task submission prevents system overload

### Manager Access Methods

```cpp
GameStateManager* getGameStateManager() const { return mp_gameStateManager.get(); }
SDL_Renderer* getRenderer() const { return mp_renderer.get(); }
SDL_Window* getWindow() const { return mp_window.get(); }
float getDPIScale() const { return m_dpiScale; }
```

### Performance Monitoring Integration

```cpp
float GameEngine::getCurrentFPS() const {
    // Gets FPS from GameLoop's TimestepManager
    if (auto gameLoop = m_gameLoop.lock()) {
        return gameLoop->getCurrentFPS();
    }
    return 0.0f;
}
```

## API Reference

### Core Lifecycle Methods

```cpp
static GameEngine& Instance();
bool init(std::string_view title, int width, int height, bool fullscreen);
void handleEvents();
void update(float deltaTime);
void render(float interpolation);
void clean();
```

### Threading Methods

```cpp
void processBackgroundTasks();
void processEngineCoordination(float deltaTime);
void processEngineSecondaryTasks();
bool loadResourcesAsync(const std::string& path);
void waitForUpdate();
void signalUpdateComplete();
bool hasNewFrameToRender() const;
bool isUpdateRunning() const;
void swapBuffers();
size_t getCurrentBufferIndex() const;
size_t getRenderBufferIndex() const;
```

### State Management

```cpp
void setGameLoop(std::shared_ptr<GameLoop> gameLoop);
void setRunning(bool running);
bool getRunning() const;
float getCurrentFPS() const;
```

### Window and Rendering

```cpp
int getWindowWidth() const;
int getWindowHeight() const;
void setWindowSize(int width, int height);
void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode);
SDL_RendererLogicalPresentation getLogicalPresentationMode() const;
float getDPIScale() const;
int getOptimalDisplayIndex() const;
bool isVSyncEnabled() const;
bool setVSyncEnabled(bool enable);
```

### Resource Management

```cpp
GameStateManager* getGameStateManager() const;
SDL_Renderer* getRenderer() const;
SDL_Window* getWindow() const;
```

## Best Practices

### 1. Proper Initialization Order

Always initialize in the correct sequence:

```cpp
bool initializeGame() {
    GameEngine& engine = GameEngine::Instance();

    // 1. Initialize engine first
    if (!engine.init("Game Title", 1280, 720, false)) {
        return false;
    }

    // 2. Create and set game loop
    auto gameLoop = std::make_shared<GameLoop>();
    engine.setGameLoop(gameLoop);

    // 3. Start the loop
    gameLoop->run();

    // 4. Cleanup
    engine.clean();
    return true;
}
```

### 2. Thread-Safe Resource Access

When accessing resources from worker threads:

```cpp
void workerThreadFunction() {
    GameEngine& engine = GameEngine::Instance();

    // Safe: These methods are thread-safe
    if (engine.hasNewFrameToRender()) {
        // Process frame data
    }

    // Safe: Atomic operations
    bool isRunning = engine.isUpdateRunning();
}
```

### 3. Proper VSync Handling

The GameEngine automatically handles VSync based on platform compatibility, but you can still check VSync status for optimization purposes:

```cpp
void setupRenderer() {
    GameEngine& engine = GameEngine::Instance();

    // GameEngine automatically configures VSync based on platform
    // Check the result for optimization decisions
    if (engine.isVSyncEnabled()) {
        // VSync is active - hardware frame synchronization
        // Can rely on VSync for smooth animation timing
    } else {
        // Software frame limiting is active
        // May need more careful timing for smooth animations
        // This is normal on Wayland or when VSync fails
    }
}
```

**Platform-Aware Development:**
```cpp
void optimizeForPlatform() {
    GameEngine& engine = GameEngine::Instance();
    
    // Movement and animation code works automatically across all platforms
    // The TimestepManager provides appropriate deltaTime for each platform
    
    // Adapt rendering strategy based on VSync availability
    if (engine.isVSyncEnabled()) {
        // Hardware VSync available - can use aggressive rendering optimizations
        enableHighQualityEffects();
    } else {
        // Software limiting with fixed timestep - still smooth but different timing
        optimizeForSoftwareFrameLimiting();
    }
}
```

**Fixed Timestep Benefits:**
- **Consistent Physics**: Same deltaTime every frame on software limiting platforms
- **Smooth Movement**: Eliminates micro-stuttering and pixel-level ticking
- **Cross-Platform**: Automatic adaptation without code changes
- **Predictable**: Deterministic movement behavior for debugging and testing

**Never manually override the automatic platform detection** unless you have specific requirements. The GameEngine's automatic detection provides optimal timing for each platform.

### 4. Error Handling

Always check return values and handle exceptions:

```cpp
bool safeEngineOperation() {
    try {
        GameEngine& engine = GameEngine::Instance();

        if (!engine.init("My Game", 1280, 720, false)) {
            return false;
        }

        // Additional operations...
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Engine error: " << e.what() << std::endl;
        return false;
    }
}
```

## Troubleshooting

### VSync and Timing Issues

**Problem: Movement appears jerky or "ticks" pixel by pixel**

This is caused by inconsistent deltaTime values when using software frame limiting instead of hardware VSync.

**Symptoms:**
- Player or entity movement appears to stutter or tick forward
- Smooth movement works on some platforms but not others
- Animations appear jerky despite stable frame rates
- Movement inconsistency on Wayland-based systems

**Automatic Solution:**
The GameEngine automatically detects and handles this issue by:
- Detecting Wayland display server during initialization
- Disabling VSync and configuring fixed timestep mode
- Providing perfectly consistent 16.667ms deltaTime every frame
- Preserving smooth VSync timing on compatible platforms
- Logging the detection and configuration strategy

**Manual Diagnosis:**
```bash
# Check if you're running on Wayland
echo $XDG_SESSION_TYPE
echo $WAYLAND_DISPLAY

# Test with forced X11 (if available)
SDL_VIDEODRIVER=x11 ./your_game

# Check game logs for VSync detection messages
grep -i "wayland\|vsync" game_output.log
```

**Expected Log Output:**
```
[GameEngine] WARNING: Detected Wayland session - VSync may cause timing issues, using software limiting
[GameEngine] INFO: Using software frame rate limiting for consistent timing on Wayland
[GameLoop] INFO: Configured TimestepManager for Wayland software frame limiting
```

**Platform-Specific Notes:**
- **Linux X11**: VSync enabled, variable timestep based on hardware timing
- **Linux Wayland**: VSync disabled, fixed timestep (16.667ms) for smooth movement
- **macOS**: VSync enabled, variable timestep (excellent VSync support)
- **Windows**: VSync enabled, variable timestep (reliable VSync implementation)

**Solution Verification:**
The fixed timestep solution provides:
- **Consistent deltaTime**: Always 16.667ms on Wayland (60 FPS)
- **Smooth Movement**: `position += velocity * deltaTime` with perfect consistency
- **Automatic Detection**: No manual configuration required
- **Cross-Platform**: Works identically on all systems

**If Movement Still Appears Jerky:**
1. Verify the TimestepManager configuration message appears in logs
2. Check that movement code uses deltaTime properly: `position += velocity * deltaTime`
3. Ensure frame rate is stable (low CPU/GPU usage)
4. Test on X11 to confirm it's platform-specific: `SDL_VIDEODRIVER=x11 ./your_game`

### Performance Issues

**Problem: Low FPS or stuttering despite VSync being handled correctly**

**Diagnosis:**
Check GameLoop performance metrics in console output:
```
[GameLoop] DEBUG: Update performance: X.XXXms avg (Y.Y% frame budget)
```

**Normal Values:**
- At 60 FPS: ~16.67ms frame budget (100%)
- Update performance should be <50% frame budget
- Render operations should complete within frame time

**Common Causes:**
- Too many entities being processed simultaneously
- Inefficient rendering operations
- Background thread contention
- Resource loading blocking main thread

### Movement and Animation Issues

**Problem: Smooth movement works on some platforms but not others**

This is resolved by the automatic fixed timestep system for software frame limiting platforms.

**Technical Implementation:**
```cpp
// Movement code works identically across all platforms
void Entity::update(float deltaTime) {
    // deltaTime is automatically:
    // - Variable (hardware VSync) on X11/macOS/Windows
    // - Fixed 16.667ms on Wayland for perfect consistency
    m_position += m_velocity * deltaTime;
}
```

**Fixed Timestep Benefits:**
1. **Eliminates Micro-Stuttering**: Perfectly consistent deltaTime every frame
2. **Cross-Platform Consistency**: Movement behavior identical across systems
3. **Predictable Physics**: Deterministic movement for debugging and testing
4. **No Code Changes**: Existing movement code works automatically

**Verification Steps:**
1. Check logs for TimestepManager configuration messages
2. Verify smooth movement in GamePlayState (arrow keys to move player)
3. Test state transitions work correctly (LogoState → MainMenuState → GamePlayState)
4. Compare movement smoothness between Wayland and X11 if available

## Examples

### Complete Game Setup

```cpp
class MyGame {
private:
    std::shared_ptr<GameLoop> m_gameLoop;

public:
    MyGame() = default;

    bool initialize() {
        GameEngine& engine = GameEngine::Instance();

        // Initialize engine with specific parameters
        if (!engine.init("My Awesome Game", 1920, 1080, false)) {
            return false;
        }

        // Create game loop
        m_gameLoop = std::make_shared<GameLoop>();
        engine.setGameLoop(m_gameLoop);

        // Load game-specific resources
        if (!loadGameResources()) {
            return false;
        }

        return true;
    }

    bool run() {
        if (!m_gameLoop) {
            return false;
        }

        // Start the main loop
        m_gameLoop->run();
        return true;
    }

    void shutdown() {
        GameEngine& engine = GameEngine::Instance();
        engine.clean();
        m_gameLoop.reset();
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
        // Use ThreadSystem for coordinated loading
        auto& threadSystem = Hammer::ThreadSystem::Instance();

        // Load different resource types in parallel
        m_loadingTasks.push_back(
            threadSystem.enqueueTaskWithResult([]() -> bool {
                // Load textures
                TextureManager& texMgr = TextureManager::Instance();
                texMgr.load("res/textures", "", GameEngine::Instance().getRenderer());
                return true;
            })
        );

        m_loadingTasks.push_back(
            threadSystem.enqueueTaskWithResult([]() -> bool {
                // Load sounds
                SoundManager& soundMgr = SoundManager::Instance();
                soundMgr.loadSFX("res/audio", "sfx");
                return true;
            })
        );

        // Start background completion check
        threadSystem.enqueueTask([this]() {
            waitForCompletion();
            m_loadingComplete.store(true);
        });
    }

    bool isComplete() const {
        return m_loadingComplete.load();
    }

private:
    void waitForCompletion() {
        for (auto& task : m_loadingTasks) {
            try {
                task.get();
            } catch (const std::exception& e) {
                std::cerr << "Resource loading failed: " << e.what() << std::endl;
            }
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

            std::cout << "Engine FPS: " << engineFPS
                      << ", Calculated FPS: " << calculatedFPS
                      << ", Update Running: " << engine.isUpdateRunning()
                      << ", New Frame Available: " << engine.hasNewFrameToRender()
                      << std::endl;

            // Reset counters
            m_frameTimeAccumulator = 0.0f;
            m_frameCount = 0;
        }
    }
};
```
