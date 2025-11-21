# LoadingState Documentation

## Overview

LoadingState is a non-blocking loading screen implementation for the Hammer Engine that handles asynchronous world generation and state transitions. It provides responsive UI feedback during long-running operations by executing world generation on ThreadSystem background threads while keeping the render loop active. This is the industry-standard pattern used by Unity, Unreal Engine, and Godot.

## Architecture

### Design Patterns
- **Async Operation Pattern**: Long-running tasks execute on background threads
- **Progress Reporting**: Real-time progress updates via thread-safe atomics
- **Auto-Transition**: Automatically transitions to target state when complete
- **Unified Rendering**: All rendering through `GameEngine::render()` pipeline
- **Stateless Configuration**: Can be reused multiple times with different configurations

### Core Components

#### Thread Safety Model
```cpp
// Atomic progress tracking (lock-free)
std::atomic<float> m_progress{0.0f};
std::atomic<bool> m_loadComplete{false};
std::atomic<bool> m_loadFailed{false};

// Mutex-protected string data
std::string m_statusText{"Initializing..."};
mutable std::mutex m_statusMutex;
```

#### State Lifecycle
1. **Configure**: Set target state and world configuration
2. **Enter**: Initialize UI, start async task
3. **Update**: Monitor completion, transition when ready
4. **Render**: Update progress bar and status text
5. **Exit**: Cleanup UI, wait for task completion

## Public API Reference

### Configuration

#### `void configure(const std::string& targetStateName, const WorldGenerationConfig& worldConfig)`
Configures the loading state before pushing it.
- **Parameters**:
  - `targetStateName`: State name to transition to after loading completes
  - `worldConfig`: World generation configuration (seed, size, params)
- **Side Effects**: Resets progress, status, and error state for reuse
- **Thread Safety**: Must be called before `enter()`, not thread-safe during loading

```cpp
// Get LoadingState from GameStateManager
auto* loadingState = dynamic_cast<LoadingState*>(
    gameStateManager->getState("LoadingState").get()
);

// Configure with target state and world params
HammerEngine::WorldGenerationConfig config;
config.seed = 12345;
config.width = 200;
config.height = 200;
config.generateStructures = true;

loadingState->configure("GamePlayState", config);

// Push loading state to start loading
gameStateManager->pushState("LoadingState");
```

### Error Handling

#### `bool hasError() const`
Checks if an error occurred during loading.
- **Returns**: `true` if loading failed with an error, `false` otherwise
- **Thread Safety**: Thread-safe read operation

#### `std::string getLastError() const`
Gets the last error message from failed loading.
- **Returns**: Error message string, empty if no error occurred
- **Thread Safety**: Thread-safe read operation

```cpp
// Check for errors after loading completes
if (loadingState->hasError()) {
    std::string error = loadingState->getLastError();
    GAMEENGINE_ERROR("Loading failed: " + error);
    // Handle error recovery
}
```

### GameState Interface Implementation

#### `bool enter()`
Called when LoadingState becomes active.
- **Validation**: Ensures `configure()` was called first
- **Initialization**: Creates loading UI, starts async world generation
- **Returns**: `false` if not configured, `true` otherwise

#### `void update(float deltaTime)`
Called every frame during loading.
- **Monitoring**: Checks atomic `m_loadComplete` flag
- **Transition**: Automatically calls `changeState()` when loading completes
- **Error Handling**: Transitions even on failure (matches current behavior)

#### `void render()`
Called every frame to render loading screen.
- **Progress Bar**: Updates with current percentage
- **Status Text**: Displays current operation status
- **UI Rendering**: Calls `UIManager::render()` for actual drawing
- **Note**: No manual `SDL_RenderClear()` or `SDL_RenderPresent()` calls

#### `void handleInput()`
Input handling during loading.
- **Behavior**: Empty implementation - loading must complete without interruption

#### `bool exit()`
Called when leaving LoadingState.
- **Cleanup**: Removes all UI components
- **Task Waiting**: Ensures async task completes before exit
- **Returns**: Always `true`

#### `void onWindowResize(int newLogicalWidth, int newLogicalHeight)`
Handles window resize events.
- **Behavior**: Recreates UI components with new dimensions

#### `std::string getName() const`
Gets state name.
- **Returns**: `"LoadingState"`

## Integration Examples

### Basic World Loading
```cpp
// In MainMenuState - user selects "New Game"
void MainMenuState::onNewGameSelected() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();

    // Get LoadingState
    auto* loadingState = dynamic_cast<LoadingState*>(
        gameStateManager->getState("LoadingState").get()
    );

    // Configure world generation
    HammerEngine::WorldGenerationConfig config;
    config.seed = static_cast<int>(time(nullptr)); // Random seed
    config.width = 200;
    config.height = 200;
    config.biomeScale = 0.02f;
    config.generateStructures = true;

    // Configure loading state to transition to GamePlayState
    loadingState->configure("GamePlayState", config);

    // Push loading state (starts loading immediately)
    gameStateManager->pushState("LoadingState");
}
```

### Loading with Specific Seed
```cpp
// Load world with specific seed (e.g., from save game)
void loadSavedWorld(int seed) {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();
    auto* loadingState = dynamic_cast<LoadingState*>(
        gameStateManager->getState("LoadingState").get()
    );

    // Configure with saved seed
    HammerEngine::WorldGenerationConfig config;
    config.seed = seed;
    config.width = 200;
    config.height = 200;
    config.biomeScale = 0.02f;

    loadingState->configure("GamePlayState", config);
    gameStateManager->pushState("LoadingState");
}
```

### Loading from Multiple States
```cpp
// LoadingState can transition to different states based on context

// From MainMenuState -> GamePlayState
loadingState->configure("GamePlayState", config);
gameStateManager->pushState("LoadingState");

// From AIDemoState -> AIDemoState (reload world)
loadingState->configure("AIDemoState", config);
gameStateManager->pushState("LoadingState");

// From Settings -> CustomState
loadingState->configure("CustomState", config);
gameStateManager->pushState("LoadingState");
```

### Error Handling Pattern
```cpp
// In target state's enter() - check if loading succeeded
bool GamePlayState::enter() {
    auto& gameEngine = GameEngine::Instance();
    auto* gameStateManager = gameEngine.getGameStateManager();

    // Get LoadingState to check for errors
    auto* loadingState = dynamic_cast<LoadingState*>(
        gameStateManager->getState("LoadingState").get()
    );

    if (loadingState && loadingState->hasError()) {
        std::string error = loadingState->getLastError();
        GAMESTATE_ERROR("World loading failed: " + error);

        // Show error dialog
        showErrorDialog("Failed to load world", error);

        // Return to main menu
        gameStateManager->changeState("MainMenuState");
        return false;
    }

    // Loading succeeded - initialize gameplay state
    GAMESTATE_INFO("Entering GamePlayState with loaded world");
    return initializeGameplay();
}
```

## Async Loading Implementation

### Background Thread Execution
```cpp
// LoadingState uses ThreadSystem for async execution
void LoadingState::startAsyncWorldLoad() {
    auto& threadSystem = HammerEngine::ThreadSystem::Instance();
    auto& worldManager = WorldManager::Instance();

    // Create lambda that runs on background thread
    auto loadTask = [this, worldConfig, &worldManager]() -> bool {
        // Progress callback updates atomic progress + status text
        auto progressCallback = [this](float percent, const std::string& status) {
            m_progress.store(percent, std::memory_order_release);
            setStatusText(status);
        };

        // Load world with progress callback
        bool success = worldManager.loadNewWorld(worldConfig, progressCallback);

        // Mark loading as complete
        m_loadFailed.store(!success, std::memory_order_release);
        m_loadComplete.store(true, std::memory_order_release);

        return success;
    };

    // Enqueue task with high priority
    m_loadTask = threadSystem.enqueueTaskWithResult(
        loadTask,
        HammerEngine::TaskPriority::High,
        "LoadingState_WorldGeneration"
    );
}
```

### Progress Reporting
```cpp
// WorldManager calls progress callback during generation
worldManager.loadNewWorld(config, [this](float percent, const std::string& status) {
    // Examples of status messages during loading:
    // "Generating terrain... (0%)"
    // "Creating biomes... (25%)"
    // "Placing structures... (50%)"
    // "Populating entities... (75%)"
    // "Finalizing world... (100%)"

    // LoadingState automatically updates UI with this info
});
```

## UI Components

### Loading Screen Layout
```
┌─────────────────────────────────────────────────────┐
│                                                     │
│                                                     │
│                  Loading World...                   │  ← Title
│                                                     │
│         ██████████████░░░░░░░░░░░░░░░░░            │  ← Progress Bar
│                                                     │
│              Generating terrain... (45%)            │  ← Status Text
│                                                     │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### UI Component Creation
```cpp
void LoadingState::initializeUI() {
    auto& ui = UIManager::Instance();
    auto& gameEngine = GameEngine::Instance();
    int windowWidth = gameEngine.getLogicalWidth();
    int windowHeight = gameEngine.getLogicalHeight();

    // Create loading overlay (semi-transparent background)
    ui.createOverlay();

    // Create title (centered, above progress bar)
    ui.createTitle("loading_title",
                   {0, windowHeight / 2 - 80, windowWidth, 40},
                   "Loading World...");
    ui.setTitleAlignment("loading_title", UIAlignment::CENTER_CENTER);

    // Create progress bar (centered horizontally and vertically)
    int progressBarWidth = 400;
    int progressBarHeight = 30;
    int progressBarX = (windowWidth - progressBarWidth) / 2;
    int progressBarY = windowHeight / 2;
    ui.createProgressBar("loading_progress",
                        {progressBarX, progressBarY, progressBarWidth, progressBarHeight},
                        0.0f, 100.0f);

    // Create status text (centered, below progress bar)
    ui.createTitle("loading_status",
                   {0, progressBarY + 50, windowWidth, 30},
                   "Initializing...");
    ui.setTitleAlignment("loading_status", UIAlignment::CENTER_CENTER);
}
```

## Performance Considerations

### Thread Safety
- **Lock-Free Progress**: Atomic operations for progress updates (no mutex contention)
- **Mutex for Strings**: Status and error text use mutexes (strings aren't trivially copyable)
- **Render Thread Safety**: All UI updates happen on main thread in `render()`

### Memory Efficiency
- **No Blocking**: Render loop continues during loading (no frame drops)
- **Single Task**: One async task per loading operation
- **Automatic Cleanup**: UI components removed in `exit()`, no memory leaks

### Best Practices
1. **Always Configure**: Call `configure()` before pushing state
2. **Single Responsibility**: LoadingState only handles loading, not gameplay
3. **Error Checking**: Check `hasError()` in target state's `enter()`
4. **Reusable**: Same LoadingState instance can load multiple times
5. **High Priority**: World generation runs at high priority for fast loading

```cpp
// GOOD: Configure before pushing
loadingState->configure("GamePlayState", config);
gameStateManager->pushState("LoadingState");

// BAD: Pushing without configuration
gameStateManager->pushState("LoadingState"); // Will fail in enter()!
```

## Deferred State Transition Pattern

### Problem: State Transitions in enter()
GameStates that need to load worlds cannot transition to LoadingState directly in `enter()` because state transitions during `enter()` cause timing issues.

### Solution: Deferred Transition
```cpp
class GamePlayState : public GameState {
private:
    bool m_worldLoaded = false;
    bool m_needsLoading = false;

public:
    bool enter() override {
        if (!m_worldLoaded) {
            // Set flag for deferred loading
            m_needsLoading = true;
            m_worldLoaded = true; // Prevent loop
            return true; // Exit early
        }

        // Normal initialization when world is already loaded
        GAMESTATE_INFO("Entering GamePlayState with loaded world");
        return initializeGameplay();
    }

    void update(float deltaTime) override {
        if (m_needsLoading) {
            m_needsLoading = false;

            // Configure and transition to LoadingState
            auto& gameEngine = GameEngine::Instance();
            auto* gameStateManager = gameEngine.getGameStateManager();
            auto* loadingState = dynamic_cast<LoadingState*>(
                gameStateManager->getState("LoadingState").get()
            );

            HammerEngine::WorldGenerationConfig config;
            config.seed = 12345;
            config.width = 200;
            config.height = 200;

            loadingState->configure("GamePlayState", config);
            gameStateManager->pushState("LoadingState");
            return;
        }

        // Normal update logic
        updateGameplay(deltaTime);
    }
};
```

## Common Patterns

### Pattern 1: New Game
```cpp
// User selects "New Game" from main menu
MainMenuState → configure(GamePlayState) → LoadingState → GamePlayState
```

### Pattern 2: World Regeneration
```cpp
// User clicks "Regenerate World" in gameplay
GamePlayState → configure(GamePlayState) → LoadingState → GamePlayState
```

### Pattern 3: Level Selection
```cpp
// User selects different level
LevelSelectState → configure(LevelState, levelConfig) → LoadingState → LevelState
```

### Pattern 4: Save Game Loading
```cpp
// User loads saved game
MainMenuState → configure(GamePlayState, savedSeed) → LoadingState → GamePlayState
```

## Error Conditions

### Missing Configuration
```cpp
// Entering without configure() fails
gameStateManager->pushState("LoadingState");
// Logs: "LoadingState not configured - call configure() before pushing state"
// Returns false from enter()
```

### Missing Target State
```cpp
// Target state doesn't exist
loadingState->configure("NonExistentState", config);
gameStateManager->pushState("LoadingState");
// Logs: "Target state not found: NonExistentState"
// Error stored in m_lastError
```

### World Generation Failure
```cpp
// WorldManager fails to generate world
// LoadingState transitions to target state anyway (current behavior)
// Target state should check hasError() in enter()
```

## Testing

### Manual Testing
```bash
# Run application and test loading flow
./bin/debug/SDL3_Template

# Test scenarios:
# 1. New game from main menu
# 2. World regeneration from gameplay
# 3. Multiple consecutive loads
# 4. Window resize during loading
```

### Integration Testing
```cpp
// Test LoadingState configuration
void testLoadingStateConfiguration() {
    auto* loadingState = new LoadingState();

    HammerEngine::WorldGenerationConfig config;
    config.seed = 12345;

    // Test configuration
    loadingState->configure("TestState", config);
    assert(!loadingState->hasError());

    // Test enter without config
    auto* emptyState = new LoadingState();
    assert(!emptyState->enter()); // Should fail
}
```

## Rendering Architecture

### Why No Manual SDL Calls?
LoadingState follows the unified rendering pipeline:
```
GameLoop → GameEngine::render() → GameStateManager::render() → LoadingState::render()
```

**Key Points:**
- GameEngine manages `SDL_RenderClear()` and `SDL_RenderPresent()`
- Each state only calls manager `render()` methods (UI, particles, etc.)
- This pattern is compatible with future SDL3_GPU → SDL3_Renderer rendering system improvements
- Multiple `Present()` calls per frame break command buffer architecture

### Correct Pattern
```cpp
void LoadingState::render() {
    // ✓ Update UI components
    ui.updateProgressBar("loading_progress", m_progress);
    ui.setText("loading_status", getStatusText());

    // ✓ Render through manager
    ui.render();

    // ✗ NEVER do manual rendering
    // SDL_RenderClear(renderer);  // NO!
    // SDL_RenderPresent(renderer); // NO!
}
```

## See Also

- [GameStateManager Documentation](../managers/GameStateManager.md) - State management system
- [ThreadSystem Documentation](../core/ThreadSystem.md) - Async task execution
- [WorldManager Documentation](../managers/WorldManager.md) - World generation details
- [UIManager Guide](../ui/UIManager_Guide.md) - UI component creation
- [GameEngine Documentation](../core/GameEngine.md) - Rendering pipeline
