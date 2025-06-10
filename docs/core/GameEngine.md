# GameEngine Documentation

## Overview

The GameEngine class is the central singleton that manages all core systems, SDL initialization, window management, and coordinates the main game loop. It provides a unified interface for initializing, updating, and rendering the game while managing threading and resource synchronization.

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
- [Examples](#examples)

## Quick Start

### Basic Engine Setup

```cpp
#include "core/GameEngine.hpp"
#include "core/GameLoop.hpp"

int main() {
    // Get singleton instance
    GameEngine& engine = GameEngine::Instance();
    
    // Initialize engine
    if (!engine.init("My Game", 1280, 720, false)) {
        return -1;
    }
    
    // Create and configure game loop
    auto gameLoop = std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true);
    engine.setGameLoop(gameLoop);
    
    // Set up callbacks
    gameLoop->setEventHandler([&engine]() { engine.handleEvents(); });
    gameLoop->setUpdateHandler([&engine](float dt) { engine.update(dt); });
    gameLoop->setRenderHandler([&engine](float interp) { engine.render(interp); });
    
    // Run the game
    bool success = gameLoop->run();
    
    // Cleanup
    engine.clean();
    return success ? 0 : -1;
}
```

## Architecture

### Singleton Pattern
The GameEngine uses the singleton pattern to ensure single instance access throughout the application:

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

1. **SDL Management**: Window, renderer, and SDL subsystem initialization
2. **Manager Coordination**: Centralized access to all manager systems
3. **Threading Support**: Multi-threaded update and render coordination
4. **Double Buffering**: Ping-pong buffer system for smooth rendering
5. **State Management**: Game state transitions and lifecycle management

### System Dependencies

```
GameEngine
├── SDL3 (Window/Renderer)
├── GameStateManager (State transitions)
├── GameLoop (Main loop coordination)
├── AIManager (AI processing)
├── EventManager (Event handling)
└── Various Resource Managers
```

## Initialization

### Basic Initialization

```cpp
bool GameEngine::init(const char* title, int width, int height, bool fullscreen);
```

**Parameters:**
- `title`: Window title for the game
- `width`: Initial window width (0 for auto-sizing)
- `height`: Initial window height (0 for auto-sizing) 
- `fullscreen`: Whether to start in fullscreen mode

**Returns:** `true` if initialization successful, `false` otherwise

### Initialization Process

1. **SDL Subsystem Initialization**
   - Video subsystem for rendering
   - Audio subsystem for sound
   - Input subsystem for controllers

2. **Window Creation**
   - Creates SDL window with specified parameters
   - Configures window properties and flags

3. **Renderer Setup**
   - Creates hardware-accelerated renderer
   - Sets up logical presentation mode
   - Configures render targets

4. **Manager Initialization**
   - Initializes GameStateManager
   - Caches manager references for performance
   - Sets up inter-manager communication

### Error Handling During Initialization

```cpp
bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
    GAMEENGINE_INFO("Initializing Forge Game Engine");
    
    // SDL initialization
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD) < 0) {
        GAMEENGINE_CRITICAL("Failed to initialize SDL: " + std::string(SDL_GetError()));
        return false;
    }
    
    // Window creation
    Uint32 windowFlags = fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_RESIZABLE;
    mp_window.reset(SDL_CreateWindow(title, width, height, windowFlags));
    
    if (!mp_window) {
        GAMEENGINE_ERROR("Failed to create window: " + std::string(SDL_GetError()));
        SDL_Quit();
        return false;
    }
    
    // Renderer creation
    mp_renderer.reset(SDL_CreateRenderer(mp_window.get(), nullptr));
    if (!mp_renderer) {
        GAMEENGINE_ERROR("Failed to create renderer: " + std::string(SDL_GetError()));
        return false;
    }
    
    // Manager initialization
    mp_gameStateManager = std::make_unique<GameStateManager>();
    if (!mp_gameStateManager->init()) {
        GAMEENGINE_ERROR("Failed to initialize GameStateManager");
        return false;
    }
    
    // Cache manager references
    mp_aiManager = &AIManager::Instance();
    mp_eventManager = &EventManager::Instance();
    
    GAMEENGINE_INFO("Game engine initialized successfully");
    return true;
}
```

## Main Loop Integration

### GameLoop Delegation
The GameEngine works with GameLoop through a delegation pattern:

```cpp
class GameEngine {
    std::weak_ptr<GameLoop> m_gameLoop; // Non-owning reference
    
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
    // SDL event polling (main thread only)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                setRunning(false);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                setWindowSize(event.window.data1, event.window.data2);
                break;
            // Handle other events...
        }
    }
    
    // Delegate to InputManager for input processing
    // InputManager::Instance().handleEvents(); // Not cached for SDL architecture
}
```

#### Game Logic Update
```cpp
void GameEngine::update(float deltaTime) {
    // Update game state manager
    if (mp_gameStateManager) {
        mp_gameStateManager->update(deltaTime);
    }
    
    // Update AI systems
    if (mp_aiManager) {
        mp_aiManager->update();
    }
    
    // Update event systems
    if (mp_eventManager) {
        mp_eventManager->update();
    }
    
    // Process background tasks
    processBackgroundTasks();
}
```

#### Rendering
```cpp
void GameEngine::render(float interpolation) {
    SDL_SetRenderDrawColor(mp_renderer.get(), 0, 0, 0, 255);
    SDL_RenderClear(mp_renderer.get());
    
    // Render current game state
    if (mp_gameStateManager) {
        mp_gameStateManager->render(interpolation);
    }
    
    SDL_RenderPresent(mp_renderer.get());
}
```

## Threading and Synchronization

### Multi-Threading Support
The GameEngine supports multi-threaded operation with proper synchronization:

#### Thread-Safe State Management
```cpp
class GameEngine {
private:
    // Atomic flags for thread safety
    std::atomic<bool> m_updateCompleted{false};
    std::atomic<bool> m_updateRunning{false};
    std::atomic<uint64_t> m_lastUpdateFrame{0};
    std::atomic<uint64_t> m_lastRenderedFrame{0};
    
    // Synchronization primitives
    std::mutex m_updateMutex;
    std::condition_variable m_updateCondition;

};
```

#### Lock-Free Double Buffering System (v4.2+)
```cpp
class GameEngine {
private:
    static constexpr size_t BUFFER_COUNT = 2;
    std::atomic<size_t> m_currentBufferIndex{0};
    std::atomic<size_t> m_renderBufferIndex{0};
    std::atomic<bool> m_bufferReady[BUFFER_COUNT]{false, false};
    
    // Lock-free buffer synchronization (lock-free atomic operations)
    std::condition_variable m_bufferCondition;
    
public:
    void swapBuffers() {
        // Ultra-fast lock-free buffer swap - optimized for render thread performance
        size_t currentIndex = m_currentBufferIndex.load(std::memory_order_acquire);
        size_t nextUpdateIndex = (currentIndex + 1) % BUFFER_COUNT;

        // Only swap if current buffer is ready
        if (m_bufferReady[currentIndex].load(std::memory_order_acquire)) {
            // Make current buffer available for rendering immediately
            m_renderBufferIndex.store(currentIndex, std::memory_order_release);
            
            // Switch to next buffer for updates
            m_currentBufferIndex.store(nextUpdateIndex, std::memory_order_release);
            
            // Clear the next buffer's ready state for the next update cycle
            m_bufferReady[nextUpdateIndex].store(false, std::memory_order_release);
        }
    }
    
    bool hasNewFrameToRender() const {
        // Ultra-fast render check - single atomic read with relaxed ordering for speed
        uint64_t lastUpdate = m_lastUpdateFrame.load(std::memory_order_relaxed);
        uint64_t lastRendered = m_lastRenderedFrame.load(std::memory_order_relaxed);
        // Always true for first frame, then compare frame counters
        return lastUpdate > lastRendered || (lastUpdate == 1 && lastRendered == 0);
    }
};
```

**Key Optimizations:**
- **Lock-Free Buffer Swapping**: Eliminated mutex-based buffer swapping that was blocking render thread
- **Zero Contention**: No blocking between update and render threads
- **Atomic Frame Counters**: Thread-safe frame synchronization using `fetch_add(1, memory_order_relaxed)`
- **Performance**: Buffer operations now take ~100-500 nanoseconds vs 1-10μs with mutex

#### Background Task Processing
```cpp
void GameEngine::processBackgroundTasks() {
    // Delegate to ThreadSystem for background processing
    auto& threadSystem = ThreadSystem::Instance();
    
    // Process pending resource loads
    threadSystem.processBackgroundTasks();
    
    // Update entity processing count for thread coordination
    m_entityProcessingCount.store(mp_aiManager ? mp_aiManager->getEntityCount() : 0);
}
```

### Synchronization Methods

#### Update Thread Coordination
```cpp
void GameEngine::waitForUpdate() {
    std::unique_lock<std::mutex> lock(m_updateMutex);
    m_updateCondition.wait(lock, [this] { 
        return m_updateCompleted.load() || !m_updateRunning.load(); 
    });
}

void GameEngine::signalUpdateComplete() {
    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_updateCompleted.store(true);
        m_lastUpdateFrame.fetch_add(1);
    }
    m_updateCondition.notify_all();
}

bool GameEngine::hasNewFrameToRender() const {
    return m_lastUpdateFrame.load() > m_lastRenderedFrame.load();
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
    float m_dpiScale{1.0f};  // DPI scale factor for high-DPI displays
};
```

### DPI Management

The GameEngine automatically detects and manages display pixel density for crisp text rendering:

```cpp
// Automatic DPI detection during initialization
float GameEngine::calculateDPIScale() {
    int pixelWidth, pixelHeight;
    int logicalWidth, logicalHeight;
    SDL_GetWindowSizeInPixels(mp_window.get(), &pixelWidth, &pixelHeight);
    SDL_GetWindowSize(mp_window.get(), &logicalWidth, &logicalHeight);
    
    if (logicalWidth > 0 && logicalHeight > 0) {
        float scaleX = static_cast<float>(pixelWidth) / static_cast<float>(logicalWidth);
        float scaleY = static_cast<float>(pixelHeight) / static_cast<float>(logicalHeight);
        return std::max(scaleX, scaleY);
    }
    return 1.0f;
}

// Access DPI scale for other managers
float getDPIScale() const { return m_dpiScale; }
```

**DPI System Features:**
- **Automatic Detection**: Calculates pixel density during window creation
- **Font Scaling**: Provides scale factor for DPI-aware font loading
- **Quality Optimization**: Ensures crisp text on all display types
- **Manager Integration**: Shared scale factor across FontManager and UIManager
- **Cross-Platform**: Works on standard, high-DPI, and 4K/Retina displays

### Window Management Methods

#### Size Management
```cpp
int getWindowWidth() const { return m_windowWidth; }
int getWindowHeight() const { return m_windowHeight; }

void setWindowSize(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;
    
    // Update renderer logical size if needed
    SDL_SetRenderLogicalPresentation(mp_renderer.get(), width, height, 
                                    m_logicalPresentationMode);
}
```

#### Logical Presentation
```cpp
void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode) {
    m_logicalPresentationMode = mode;
    SDL_SetRenderLogicalPresentation(mp_renderer.get(), m_windowWidth, m_windowHeight, mode);
}

SDL_RendererLogicalPresentation getLogicalPresentationMode() const {
    return m_logicalPresentationMode;
}
```

## Manager Integration

### Cached Manager References
For performance, frequently accessed managers are cached:

```cpp
class GameEngine {
private:
    // Cached for zero-overhead performance
    AIManager* mp_aiManager{nullptr};
    EventManager* mp_eventManager{nullptr};
    
    // Note: InputManager not cached - handled in handleEvents() 
    // for proper SDL event polling architecture
};
```

### Manager Access
```cpp
// Direct access to game state manager
GameStateManager* getGameStateManager() const { 
    return mp_gameStateManager.get(); 
}

// Access to SDL renderer for managers
SDL_Renderer* getRenderer() const { 
    return mp_renderer.get(); 
}
```

### Font System Integration

The GameEngine integrates with FontManager to provide display-aware font loading:

```cpp
// Automatic font system initialization during GameEngine::init()
bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
    // ... SDL, window, and renderer initialization ...
    
    // Calculate DPI scale for font sizing
    float dpiScale = calculateDPIScale();
    m_dpiScale = dpiScale;
    
    // Initialize managers with threading
    std::vector<std::future<bool>> initTasks;
    
    // FontManager initialization in background thread
    initTasks.push_back(
        Forge::ThreadSystem::Instance().enqueueTaskWithResult([this]() -> bool {
            FontManager& fontMgr = FontManager::Instance();
            if (!fontMgr.init()) {
                GAMEENGINE_CRITICAL("Failed to initialize Font Manager");
                return false;
            }
            
            // Load fonts with display-aware sizing
            if (!fontMgr.loadFontsForDisplay("res/fonts", m_windowWidth, m_windowHeight)) {
                GAMEENGINE_CRITICAL("Failed to load fonts for display");
                return false;
            }
            return true;
        })
    );
    
    // Wait for all initialization tasks to complete
    for (auto& task : initTasks) {
        if (!task.get()) {
            return false;
        }
    }
    
    return true;
}
```

**Font System Features:**
- **Display-Aware Loading**: Automatically calculates font sizes based on screen resolution
- **DPI Integration**: Uses GameEngine's DPI scale for optimal text rendering
- **Quality Fonts**: Loads multiple font categories (base, UI, title, tooltip) with appropriate sizes
- **SDL3 Compatibility**: Leverages SDL3's logical presentation system for automatic scaling
- **Threading Support**: Font loading occurs in background threads for better performance

### Initialization Order
Critical for proper system startup:

```cpp
bool GameEngine::init(const char* title, int width, int height, bool fullscreen) {
    // 1. SDL subsystems first
    if (!initializeSDL()) return false;
    
    // 2. Window and renderer
    if (!createWindow(title, width, height, fullscreen)) return false;
    if (!createRenderer()) return false;
    
    // 3. DPI calculation
    m_dpiScale = calculateDPIScale();
    
    // 4. Core managers (with threading)
    if (!initializeGameStateManager()) return false;
    if (!initializeFontManager()) return false;
    
    // 5. Cache manager references (after singleton initialization)
    cacheManagerReferences();
    
    return true;
}
```

## API Reference

### Core Lifecycle Methods

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `init(title, width, height, fullscreen)` | Initialize engine and SDL | Main thread only |
| `handleEvents()` | Process SDL events | Main thread only |
| `update(deltaTime)` | Update game logic | Thread-safe |
| `render(interpolation)` | Render frame | Main thread only |
| `clean()` | Cleanup resources | Main thread only |

### Threading Methods

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `waitForUpdate()` | Wait for update completion | Thread-safe |
| `signalUpdateComplete()` | Signal update finished | Thread-safe |
| `hasNewFrameToRender()` | Check for new frame | Thread-safe |
| `swapBuffers()` | Swap double buffers | Thread-safe |
| `processBackgroundTasks()` | Process async tasks | Thread-safe |

### State Management

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `setRunning(bool)` | Set engine running state | Thread-safe |
| `getRunning()` | Get engine running state | Thread-safe |
| `isUpdateRunning()` | Check if update active | Thread-safe |
| `getCurrentFPS()` | Get current FPS | Thread-safe |

### Resource Management

| Method | Description | Thread Safety |
|--------|-------------|---------------|
| `loadResourcesAsync(path)` | Load resources asynchronously | Thread-safe |
| `getRenderer()` | Get SDL renderer | Thread-safe |
| `getGameStateManager()` | Get state manager | Thread-safe |

## Best Practices

### 1. Proper Initialization Order
```cpp
// Correct initialization sequence
bool initializeGame() {
    GameEngine& engine = GameEngine::Instance();
    
    // 1. Initialize engine first
    if (!engine.init("Game Title", 1280, 720, false)) {
        return false;
    }
    
    // 2. Create game loop
    auto gameLoop = std::make_shared<GameLoop>();
    engine.setGameLoop(gameLoop);
    
    // 3. Set up callbacks
    setupGameLoopCallbacks(gameLoop, engine);
    
    // 4. Initialize game-specific systems
    initializeGameSystems();
    
    return true;
}
```

### 2. Thread-Safe Resource Access
```cpp
// Safe pattern for accessing renderer from threads
void renderFromThread() {
    GameEngine& engine = GameEngine::Instance();
    
    // Check if engine is running before access
    if (!engine.getRunning()) {
        return;
    }
    
    SDL_Renderer* renderer = engine.getRenderer();
    if (renderer) {
        // Use renderer safely
        // Note: Actual rendering should still be main thread only
    }
}
```

### 3. Proper Cleanup
```cpp
void shutdownGame() {
    GameEngine& engine = GameEngine::Instance();
    
    // 1. Stop the game loop
    engine.setRunning(false);
    
    // 2. Wait for threads to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 3. Clean up engine
    engine.clean();
}
```

### 4. Error Handling
```cpp
bool safeEngineOperation() {
    GameEngine& engine = GameEngine::Instance();
    
    try {
        // Perform operation
        return engine.loadResourcesAsync("assets/");
    } catch (const std::exception& e) {
        GAMEENGINE_ERROR("Operation failed: " + std::string(e.what()));
        return false;
    }
}
```

## Examples

### Complete Game Setup
```cpp
#include "core/GameEngine.hpp"
#include "core/GameLoop.hpp"
#include "managers/GameStateManager.hpp"

class MyGame {
private:
    GameEngine& m_engine;
    std::shared_ptr<GameLoop> m_gameLoop;
    
public:
    MyGame() : m_engine(GameEngine::Instance()) {}
    
    bool initialize() {
        // Initialize engine
        if (!m_engine.init("My Game", 1920, 1080, false)) {
            GAMEENGINE_ERROR("Failed to initialize game engine");
            return false;
        }
        
        // Create game loop with 60 FPS target
        m_gameLoop = std::make_shared<GameLoop>(60.0f, 1.0f/60.0f, true);
        m_engine.setGameLoop(m_gameLoop);
        
        // Set up game loop callbacks
        m_gameLoop->setEventHandler([this]() { handleEvents(); });
        m_gameLoop->setUpdateHandler([this](float dt) { update(dt); });
        m_gameLoop->setRenderHandler([this](float interp) { render(interp); });
        
        // Load initial resources
        if (!loadGameResources()) {
            GAMEENGINE_ERROR("Failed to load game resources");
            return false;
        }
        
        // Set up initial game state
        auto* stateManager = m_engine.getGameStateManager();
        if (!stateManager->pushState("MainMenu")) {
            GAMEENGINE_ERROR("Failed to set initial game state");
            return false;
        }
        
        return true;
    }
    
    bool run() {
        if (!m_gameLoop) {
            GAMEENGINE_ERROR("Game loop not initialized");
            return false;
        }
        
        GAMEENGINE_INFO("Starting game");
        bool success = m_gameLoop->run();
        
        GAMEENGINE_INFO("Game loop ended");
        return success;
    }
    
    void shutdown() {
        GAMEENGINE_INFO("Shutting down game");
        
        // Stop the game loop
        if (m_gameLoop) {
            m_gameLoop->stop();
        }
        
        // Clean up engine
        m_engine.clean();
    }
    
private:
    void handleEvents() {
        m_engine.handleEvents();
    }
    
    void update(float deltaTime) {
        m_engine.update(deltaTime);
        
        // Add game-specific updates here
        updateGameLogic(deltaTime);
    }
    
    void render(float interpolation) {
        m_engine.render(interpolation);
        
        // Add game-specific rendering here
        renderGameElements(interpolation);
    }
    
    bool loadGameResources() {
        // Load textures, sounds, etc.
        return m_engine.loadResourcesAsync("assets/");
    }
    
    void updateGameLogic(float deltaTime) {
        // Game-specific logic
    }
    
    void renderGameElements(float interpolation) {
        // Game-specific rendering
    }
};

// Main function
int main() {
    MyGame game;
    
    if (!game.initialize()) {
        return -1;
    }
    
    bool success = game.run();
    
    game.shutdown();
    
    return success ? 0 : -1;
}
```

### Multi-threaded Resource Loading
```cpp
class AsyncResourceLoader {
private:
    GameEngine& m_engine;
    std::atomic<bool> m_loadingComplete{false};
    std::thread m_loadingThread;
    
public:
    AsyncResourceLoader() : m_engine(GameEngine::Instance()) {}
    
    void startLoading() {
        m_loadingThread = std::thread([this]() {
            loadResourcesInBackground();
        });
    }
    
    bool isComplete() const {
        return m_loadingComplete.load();
    }
    
    void waitForCompletion() {
        if (m_loadingThread.joinable()) {
            m_loadingThread.join();
        }
    }
    
private:
    void loadResourcesInBackground() {
        GAMEENGINE_INFO("Starting background resource loading");
        
        // Load resources using engine's async methods
        bool success = m_engine.loadResourcesAsync("assets/textures/");
        success &= m_engine.loadResourcesAsync("assets/sounds/");
        success &= m_engine.loadResourcesAsync("assets/fonts/");
        
        if (success) {
            GAMEENGINE_INFO("Background resource loading completed successfully");
        } else {
            GAMEENGINE_ERROR("Background resource loading failed");
        }
        
        m_loadingComplete.store(true);
    }
};
```

### Performance Monitoring Integration
```cpp
class PerformanceMonitor {
private:
    GameEngine& m_engine;
    std::chrono::high_resolution_clock::time_point m_lastFrame;
    float m_frameTimeAccumulator{0.0f};
    int m_frameCount{0};
    
public:
    PerformanceMonitor() : m_engine(GameEngine::Instance()) {
        m_lastFrame = std::chrono::high_resolution_clock::now();
    }
    
    void updatePerformanceMetrics() {
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto deltaTime = std::chrono::duration<float>(currentTime - m_lastFrame).count();
        m_lastFrame = currentTime;
        
        m_frameTimeAccumulator += deltaTime;
        m_frameCount++;
        
        // Report every second
        if (m_frameTimeAccumulator >= 1.0f) {
            float avgFPS = m_frameCount / m_frameTimeAccumulator;
            float engineFPS = m_engine.getCurrentFPS();
            
            GAMEENGINE_DEBUG("Performance - Calculated FPS: " + std::to_string(avgFPS) + 
                           ", Engine FPS: " + std::to_string(engineFPS));
            
            // Reset accumulators
            m_frameTimeAccumulator = 0.0f;
            m_frameCount = 0;
        }
        
        // Check for frame drops
        if (deltaTime > 1.0f/30.0f) { // More than 33ms (below 30 FPS)
            GAMEENGINE_WARN("Frame drop detected: " + std::to_string(deltaTime * 1000.0f) + "ms");
        }
    }
};
```

---

The GameEngine serves as the foundation of the Forge Game Engine, providing centralized management of all core systems while supporting both single-threaded and multi-threaded operation modes. Its design emphasizes performance, thread safety, and clean integration with the GameLoop and manager systems.