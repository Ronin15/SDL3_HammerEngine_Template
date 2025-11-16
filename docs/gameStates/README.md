# GameStates Documentation

## Overview

GameStates in the Hammer Engine provide the organizational structure for different gameplay modes and screens. Each state represents a distinct mode of the game (main menu, gameplay, settings, loading, etc.) and manages its own update, rendering, input handling, and lifecycle.

## Available GameState Documentation

### [LoadingState](LoadingState.md)
Non-blocking loading screen implementation with asynchronous world generation.

**Features:**
- Async world generation on ThreadSystem
- Real-time progress updates
- Responsive UI during loading
- Auto-transition to target state
- Error handling and recovery

**Use Cases:**
- World generation and loading
- Level transitions
- Asset loading screens
- Save game loading

### [SettingsMenuState](SettingsMenuState.md)
Tab-based settings UI with Apply/Cancel functionality.

**Features:**
- Organized tabs (Graphics, Audio, Gameplay)
- Temporary state pattern (preview without commitment)
- Integration with SettingsManager
- Settings persistence to disk
- Keyboard shortcuts

**Use Cases:**
- Game settings configuration
- Graphics/audio/gameplay options
- User preferences management

## GameState Base Class

All game states inherit from the `GameState` base class and implement the following interface:

### Core Methods
- `bool enter()` - Called when state becomes active
- `void update(float deltaTime)` - Called every frame during update
- `void render()` - Called every frame during rendering
- `void handleInput()` - Called for input processing
- `bool exit()` - Called when leaving the state
- `std::string getName() const` - Returns state name

### Lifecycle

```
Register State → Push/Change State → enter() → Active State → exit() → Next State
                                          ↓
                              update() + render() + handleInput()
                                    (every frame)
```

## Common GameState Patterns

### Pattern 1: Simple Menu State
```cpp
class MainMenuState : public GameState {
public:
    bool enter() override {
        // Initialize UI
        createMenuButtons();
        return true;
    }

    void update(float deltaTime) override {
        // UI updates handled in render
    }

    void render() override {
        auto& ui = UIManager::Instance();
        ui.update(0.0);
        ui.render();
    }

    void handleInput() override {
        // Handle keyboard shortcuts
        if (inputManager.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
            exitGame();
        }
    }

    bool exit() override {
        auto& ui = UIManager::Instance();
        ui.prepareForStateTransition();
        return true;
    }
};
```

### Pattern 2: Gameplay State with World
```cpp
class GamePlayState : public GameState {
private:
    bool m_worldLoaded = false;
    bool m_needsLoading = false;

public:
    bool enter() override {
        if (!m_worldLoaded) {
            // Defer world loading to update()
            m_needsLoading = true;
            m_worldLoaded = true;
            return true;
        }

        // Normal initialization when world loaded
        initializeGameplay();
        return true;
    }

    void update(float deltaTime) override {
        if (m_needsLoading) {
            m_needsLoading = false;

            // Configure and transition to LoadingState
            auto* loadingState = getLoadingState();
            loadingState->configure("GamePlayState", worldConfig);
            gameStateManager->pushState("LoadingState");
            return;
        }

        // Normal gameplay update
        updateGameplay(deltaTime);
    }
};
```

### Pattern 3: State Transition Flow
```cpp
// From Main Menu → Game Play (with loading)
MainMenuState
  → User clicks "New Game"
  → Configure LoadingState with target "GamePlayState"
  → Push LoadingState
    → LoadingState: Async world generation
    → LoadingState: Progress updates
    → LoadingState: Auto-transition when complete
  → GamePlayState enter() with loaded world
```

## State Management

### Registering States
States must be registered with `GameStateManager` before use:

```cpp
auto* gameStateManager = gameEngine.getGameStateManager();

// Register all states
gameStateManager->registerState("MainMenuState", std::make_shared<MainMenuState>());
gameStateManager->registerState("GamePlayState", std::make_shared<GamePlayState>());
gameStateManager->registerState("LoadingState", std::make_shared<LoadingState>());
gameStateManager->registerState("SettingsMenuState", std::make_shared<SettingsMenuState>());
```

### State Transitions
```cpp
// Change to different state (exits current, enters new)
gameStateManager->changeState("GamePlayState");

// Push state on stack (current state paused, new state active)
gameStateManager->pushState("SettingsMenuState");

// Pop state from stack (returns to previous state)
gameStateManager->popState();
```

## Best Practices

### 1. UI Cleanup
Always clean up UI components in `exit()`:
```cpp
bool MyState::exit() override {
    auto& ui = UIManager::Instance();
    ui.prepareForStateTransition(); // Removes all components
    return true;
}
```

### 2. Deferred State Transitions
Never transition to another state in `enter()` - use deferred pattern:
```cpp
bool MyState::enter() override {
    if (needsInitialization) {
        m_needsSetup = true; // Set flag
        return true;          // Exit early
    }
    // Normal initialization
}

void MyState::update(float deltaTime) override {
    if (m_needsSetup) {
        m_needsSetup = false;
        // Do state transition here
        gameStateManager->changeState("OtherState");
        return;
    }
    // Normal update
}
```

### 3. Unified Rendering
Never call `SDL_RenderClear()` or `SDL_RenderPresent()` in state `render()`:
```cpp
// BAD: Manual SDL rendering
void MyState::render() override {
    SDL_RenderClear(renderer);  // NO!
    // ... render stuff
    SDL_RenderPresent(renderer); // NO!
}

// GOOD: Through manager render methods
void MyState::render() override {
    ui.render();        // ✓ Correct
    particles.render(); // ✓ Correct
    world.render();     // ✓ Correct
}
```

### 4. State Reusability
Configure states before transitions for reusability:
```cpp
// LoadingState can be reused for different targets
auto* loadingState = getLoadingState();

// Load into GamePlayState
loadingState->configure("GamePlayState", gameWorldConfig);
gameStateManager->pushState("LoadingState");

// Later: Load into DemoState
loadingState->configure("AIDemoState", demoWorldConfig);
gameStateManager->pushState("LoadingState");
```

## Testing Game States

### Unit Testing Pattern
```cpp
#define BOOST_TEST_MODULE GameStateTests
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(TestStateTransition) {
    auto gameStateManager = std::make_shared<GameStateManager>();
    auto state = std::make_shared<MyGameState>();

    gameStateManager->registerState("MyState", state);
    gameStateManager->changeState("MyState");

    BOOST_TEST(state->enter() == true);
    // ... more tests
}
```

### Integration Testing
```bash
# Run full state transition flow
./bin/debug/SDL3_Template

# Test scenarios:
# 1. State transitions (menu → game → menu)
# 2. State stack (push/pop settings)
# 3. Loading state with world generation
# 4. Error handling (invalid states)
```

## See Also

- [GameStateManager Documentation](../managers/GameStateManager.md) - State management system
- [GameEngine Documentation](../core/GameEngine.md) - Engine lifecycle and rendering
- [UIManager Guide](../ui/UIManager_Guide.md) - UI component creation
- [ThreadSystem Documentation](../core/ThreadSystem.md) - Async operations for loading
