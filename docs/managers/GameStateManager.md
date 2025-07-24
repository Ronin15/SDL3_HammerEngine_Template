# GameStateManager Documentation

**Where to find the code:**
- Implementation: `src/managers/GameStateManager.cpp`
- Header: `include/managers/GameStateManager.hpp`

## Overview

The `GameStateManager` is responsible for managing the collection of `GameState` objects that represent the different states/screens of your game (e.g., main menu, gameplay, pause, etc). It provides methods to add, switch, update, render, and remove game states, ensuring only one state is active at a time.

## Key Features
- Add, remove, and switch between named game states
- Delegates update, render, and input handling to the current state
- Smart pointer ownership for safe memory management
- Efficient state lookup and management

## API Reference

### Construction & Destruction
```cpp
GameStateManager();
```

### State Management
```cpp
void addState(std::unique_ptr<GameState> state);
    // Adds a new game state. Takes ownership.

void setState(const std::string& stateName);
    // Switches to the named state. Only one state is active at a time.

bool hasState(const std::string& stateName) const;
    // Returns true if a state with the given name exists.

std::shared_ptr<GameState> getState(const std::string& stateName) const;
    // Returns a shared pointer to the named state, or nullptr if not found.

void removeState(const std::string& stateName);
    // Removes the named state. If it was active, no state will be active after removal.

void clearAllStates();
    // Removes all states.
```

### State Update & Rendering
```cpp
void update(float deltaTime);
    // Calls update on the currently active state (if any).

void render();
    // Calls render on the currently active state (if any).

void handleInput();
    // Calls input handling on the currently active state (if any).
```

## Usage Example
```cpp
GameStateManager gsm;
gsm.addState(std::make_unique<MainMenuState>());
gsm.addState(std::make_unique<GamePlayState>());
gsm.setState("MainMenuState");

// In your main loop:
gsm.handleInput();
gsm.update(deltaTime);
gsm.render();

// Switch state on some event:
gsm.setState("GamePlayState");
```

## Best Practices
- Use unique, descriptive names for each state.
- Always check `hasState()` before switching or removing states.
- Remove unused states to free memory.
- Only one state should be active at a time.

## Thread Safety
This class is **not** thread-safe. All state changes and updates should occur on the main/game thread.

## See Also
- `GameState` (base class for game states)
- Example states: `MainMenuState`, `GamePlayState`, `PauseState`
