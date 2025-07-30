# InputManager Documentation

**Where to find the code:**
- Implementation: `src/managers/InputManager.cpp`
- Header: `include/managers/InputManager.hpp`

**Singleton Access:** Use `InputManager::Instance()` to access the manager.

## Overview

The InputManager provides centralized input handling for the Hammer Game Engine, including keyboard, mouse, and gamepad input. It features automatic coordinate conversion for cross-platform compatibility, event-driven input detection, and seamless integration with the UI system.

## Key Features

- **Cross-Platform Coordinate Conversion**: Automatic mouse coordinate transformation for UI accuracy
- **Event-Driven Input**: Efficient key press/release detection with frame-based tracking
- **Gamepad Support**: Multi-controller support with SDL3 gamepad API
- **Window Event Handling**: Automatic window resize detection and system coordination
- **Memory Efficient**: Optimized data structures with reserved capacity

## Quick Start

### Basic Input Handling

```cpp
// InputManager is automatically initialized by GameEngine
// Access the singleton instance
InputManager& input = InputManager::Instance();

// Check current key states
if (input.isKeyDown(SDL_SCANCODE_SPACE)) {
    // Space key is currently held down
    player.jump();
}

// Check for key press events (once per press)
if (input.wasKeyPressed(SDL_SCANCODE_RETURN)) {
    // Enter key was just pressed this frame
    confirmAction();
}

// Mouse input
const Vector2D& mousePos = input.getMousePosition();
bool leftClick = input.getMouseButtonState(LEFT);
```

## Coordinate System Integration

The InputManager automatically converts mouse coordinates to match the engine's coordinate system. All mouse events use logical coordinates after conversion, ensuring UI and gameplay accuracy across platforms.

## Input Detection Methods

### Keyboard Input

```cpp
// Current state checking (for continuous actions)
bool isWalking = input.isKeyDown(SDL_SCANCODE_W);
bool isRunning = input.isKeyDown(SDL_SCANCODE_LSHIFT);

// Event-based detection (for discrete actions)
if (input.wasKeyPressed(SDL_SCANCODE_TAB)) {
    toggleInventory();
}

// Multiple key combinations
if (input.isKeyDown(SDL_SCANCODE_LCTRL) && input.wasKeyPressed(SDL_SCANCODE_S)) {
    saveGame();
}
```

**Key Detection Features:**
- **Frame-Based Tracking**: `wasKeyPressed()` returns true only once per press
- **State Tracking**: `isKeyDown()` returns true while key is held
- **Automatic Cleanup**: Pressed keys list cleared each frame

### Mouse Input

```cpp
// Mouse position (automatically converted coordinates)
const Vector2D& mousePos = input.getMousePosition();
int mouseX = static_cast<int>(mousePos.getX());
int mouseY = static_cast<int>(mousePos.getY());

// Mouse button states
bool leftPressed = input.getMouseButtonState(LEFT);
bool rightPressed = input.getMouseButtonState(RIGHT);
bool middlePressed = input.getMouseButtonState(MIDDLE);
```

### Gamepad Input

```cpp
// Initialize gamepad support (optional, usually handled automatically)
input.initializeGamePad();

// Get joystick axis values
int leftStickX = input.getAxisX(0, 1); // 0 = first gamepad, 1 = left stick
int leftStickY = input.getAxisY(0, 1);

// Get button state
bool aButton = input.getButtonState(0, 0); // 0 = first gamepad, 0 = A button
```

## Window Event Handling

The InputManager automatically handles window resize events and coordinates system updates, updating GameEngine, UIManager, and FontManager as needed.

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
void initializeGamePad();
void clean();
bool isShutdown() const;

// Input polling (call once per frame)
void update();

// Keyboard input
bool isKeyDown(SDL_Scancode key) const;
bool wasKeyPressed(SDL_Scancode key) const;
void clearFrameInput();

// Joystick input
int getAxisX(int joy, int stick) const;
int getAxisY(int joy, int stick) const;
bool getButtonState(int joy, int buttonNumber) const;

// Mouse input
bool getMouseButtonState(int buttonNumber) const;
const Vector2D& getMousePosition() const;

// Reset mouse button states
void reset();
```

### Mouse Button Constants

```cpp
enum mouse_buttons { LEFT = 0, MIDDLE = 1, RIGHT = 2 };
```

## Best Practices

- Call `InputManager::Instance().update()` once per frame in the main loop.
- Use `isKeyDown()` for continuous actions, `wasKeyPressed()` for discrete actions.
- Use `getMousePosition()` and `getMouseButtonState()` for UI and gameplay input.
- Use `initializeGamePad()` if you need to ensure gamepad support is ready.
- Always check for valid indices when querying gamepad state.
- Call `clean()` on shutdown to free resources.

## Integration with Other Systems

The InputManager works seamlessly with:
- **[GameEngine](../GameEngine.md)**: Automatic initialization and coordinate system integration
- **[UIManager](../ui/UIManager_Guide.md)**: Coordinate conversion ensures perfect mouse accuracy
- **[FontManager](FontManager.md)**: Window resize events trigger automatic font refresh
- **Game States**: Direct integration for player input and UI interaction

The InputManager serves as the bridge between raw SDL input events and the game's coordinate system, ensuring that all input handling works correctly across platforms and display configurations.
