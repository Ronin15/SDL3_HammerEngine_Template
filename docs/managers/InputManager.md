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
- **Thread-Safe Operations**: Safe to query from multiple threads
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
Vector2D mousePos = input.getMousePosition();
bool leftClick = input.getMouseButtonState(InputManager::LEFT);
```

## Coordinate System Integration

### Automatic Coordinate Conversion

The InputManager automatically converts mouse coordinates to match the engine's coordinate system:

```cpp
// In InputManager::update() - the main event loop
SDL_Event event;
while (SDL_PollEvent(&event)) {
    // Convert window coordinates to logical coordinates for all mouse events
    SDL_ConvertEventToRenderCoordinates(gameEngine.getRenderer(), &event);
    
    // Now all mouse events use logical coordinates automatically
    switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
            onMouseMove(event);     // Uses converted coordinates
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            onMouseButtonDown(event); // Uses converted coordinates  
            break;
    }
}
```

**Coordinate Conversion Benefits:**
- **Cross-Platform Accuracy**: Works correctly on macOS letterbox and Windows/Linux native resolution
- **UI System Compatibility**: Mouse coordinates match UI component positioning
- **Single Conversion Point**: One conversion in the event loop handles all mouse interactions
- **SDL3 Recommended Approach**: Uses the official SDL3 coordinate conversion function

### Platform-Specific Behavior

**macOS (Letterbox Mode):**
- Raw mouse coordinates: Native screen resolution (e.g., 2560x1440)
- Converted coordinates: Aspect ratio-based logical resolution (e.g., 2304x1080)
- UI components positioned using logical coordinates match mouse input

**Windows/Linux (Native Resolution):**
- Raw mouse coordinates: Native screen resolution (e.g., 1920x1080)
- Converted coordinates: Same as raw (no conversion needed)
- Direct 1:1 mapping between mouse input and UI positioning

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

if (input.wasKeyReleased(SDL_SCANCODE_F)) {
    stopCastingSpell();
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
Vector2D mousePos = input.getMousePosition();
int mouseX = static_cast<int>(mousePos.getX());
int mouseY = static_cast<int>(mousePos.getY());

// Mouse button states
bool leftPressed = input.getMouseButtonState(InputManager::LEFT);
bool rightPressed = input.getMouseButtonState(InputManager::RIGHT);
bool middlePressed = input.getMouseButtonState(InputManager::MIDDLE);

// Mouse buttons integrate perfectly with UI system
// Coordinates automatically match UI component bounds
```

### Gamepad Input

```cpp
// Check for connected gamepads
if (input.getConnectedGamepadCount() > 0) {
    // Get gamepad input (player 1)
    Vector2D leftStick = input.getGamepadAxis(0, InputManager::LEFT_STICK);
    Vector2D rightStick = input.getGamepadAxis(0, InputManager::RIGHT_STICK);
    
    // Button states
    bool aButton = input.getGamepadButtonState(0, InputManager::BUTTON_A);
    bool bButton = input.getGamepadButtonState(0, InputManager::BUTTON_B);
    
    // Trigger values (0.0 to 1.0)
    float leftTrigger = input.getGamepadTrigger(0, InputManager::LEFT_TRIGGER);
    float rightTrigger = input.getGamepadTrigger(0, InputManager::RIGHT_TRIGGER);
}
```

## Window Event Handling

### Automatic Window Resize Handling

The InputManager automatically handles window resize events and coordinates system updates:

```cpp
void InputManager::onWindowResize(const SDL_Event& event) {
    // Get new window dimensions
    int newWidth = event.window.data1;
    int newHeight = event.window.data2;
    
    // Update GameEngine with new window dimensions
    gameEngine.setWindowSize(newWidth, newHeight);
    
    // Recalculate coordinate system based on platform
    #ifdef __APPLE__
    // On macOS, recalculate aspect ratio-based logical resolution
    float aspectRatio = static_cast<float>(actualWidth) / static_cast<float>(actualHeight);
    int targetLogicalWidth = static_cast<int>(std::round(1080 * aspectRatio));
    SDL_SetRenderLogicalPresentation(gameEngine.getRenderer(), targetLogicalWidth, 1080, 
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
    #else
    // On non-Apple platforms, use actual screen resolution
    SDL_SetRenderLogicalPresentation(gameEngine.getRenderer(), actualWidth, actualHeight, 
                                   SDL_LOGICAL_PRESENTATION_DISABLED);
    #endif
    
    // Update UI systems
    UIManager& uiManager = UIManager::Instance();
    uiManager.setGlobalScale(1.0f);  // Consistent scaling
    uiManager.cleanupForStateTransition();  // Force UI layout recalculation
    
    // Update FontManager with new logical dimensions
    FontManager& fontManager = FontManager::Instance();
    fontManager.refreshFontsForDisplay("res/fonts", gameEngine.getLogicalWidth(), 
                                     gameEngine.getLogicalHeight());
}
```

**Window Resize Features:**
- **Automatic Coordinate Recalculation**: Updates logical presentation for new window size
- **System Coordination**: Updates GameEngine, UIManager, and FontManager automatically
- **Cross-Platform Adaptation**: Handles macOS and Windows/Linux differently as appropriate
- **UI Layout Refresh**: Forces UI components to recalculate based on new dimensions

## Integration with Game Systems

### UI System Integration

```cpp
// InputManager provides coordinates that match UI component positioning
Vector2D mousePos = input.getMousePosition();

// These coordinates work directly with UI bounds checking
if (buttonBounds.contains(mousePos.getX(), mousePos.getY())) {
    // Mouse is over the button - coordinates guaranteed to match
    button.onHover();
}

// UI system automatically uses InputManager for event handling
// No manual coordinate conversion needed in game code
```

### Game State Integration

```cpp
class GamePlayState : public GameState {
public:
    void update(float deltaTime) override {
        InputManager& input = InputManager::Instance();
        
        // Player movement
        Vector2D moveDirection(0, 0);
        if (input.isKeyDown(SDL_SCANCODE_W)) moveDirection.setY(-1);
        if (input.isKeyDown(SDL_SCANCODE_S)) moveDirection.setY(1);
        if (input.isKeyDown(SDL_SCANCODE_A)) moveDirection.setX(-1);
        if (input.isKeyDown(SDL_SCANCODE_D)) moveDirection.setX(1);
        
        player.move(moveDirection * deltaTime);
        
        // Discrete actions
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            player.attack();
        }
        
        if (input.wasKeyPressed(SDL_SCANCODE_E)) {
            player.interact();
        }
    }
};
```

## Performance Optimization

### Efficient Input Polling

```cpp
// ✅ GOOD: Check input state once per frame
void GameState::update(float deltaTime) {
    InputManager& input = InputManager::Instance();
    
    // Cache frequently used input states
    bool isMoving = input.isKeyDown(SDL_SCANCODE_W) || 
                   input.isKeyDown(SDL_SCANCODE_S) ||
                   input.isKeyDown(SDL_SCANCODE_A) || 
                   input.isKeyDown(SDL_SCANCODE_D);
    
    if (isMoving) {
        updatePlayerMovement();
    }
}

// ❌ BAD: Multiple queries for the same key
void GameState::update(float deltaTime) {
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_W)) { /* ... */ }
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_W)) { /* ... */ }  // Redundant
}
```

### Memory Efficiency

The InputManager uses optimized data structures:

```cpp
// Pre-allocated containers for performance
m_pressedThisFrame.reserve(16);  // Typical max keys pressed per frame
m_joystickValues.reserve(4);     // Max 4 gamepads typically
m_mouseButtonStates.reserve(3);  // 3 mouse buttons

// Frame-based cleanup prevents memory growth
void InputManager::update() {
    m_pressedThisFrame.clear();  // Clear pressed keys each frame
    // Process events...
}
```

## Thread Safety

### Safe Cross-Thread Access

```cpp
// InputManager methods are safe to call from multiple threads
class GameThread {
public:
    void updateAI() {
        // Safe to query input from AI thread
        InputManager& input = InputManager::Instance();
        if (input.isKeyDown(SDL_SCANCODE_P)) {
            pauseAI();
        }
    }
};

// Main game loop
void GameLoop::update() {
    // InputManager update() must be called from main thread
    InputManager::Instance().update();  // Processes SDL events
}
```

**Thread Safety Guidelines:**
- **Main Thread Only**: `update()` method must be called from main thread (processes SDL events)
- **Query Anywhere**: Input state queries (`isKeyDown()`, `getMousePosition()`, etc.) are thread-safe
- **Event Processing**: SDL event processing is single-threaded by design

## Error Handling

### Gamepad Connection Handling

```cpp
// Check gamepad availability before use
if (input.getConnectedGamepadCount() > playerIndex) {
    Vector2D movement = input.getGamepadAxis(playerIndex, InputManager::LEFT_STICK);
    player.move(movement);
} else {
    // Fall back to keyboard input
    Vector2D movement = getKeyboardMovement();
    player.move(movement);
}

// Handle gamepad disconnection gracefully
void GameState::handleGamepadDisconnected(int gamepadIndex) {
    if (gamepadIndex == currentPlayerGamepad) {
        // Switch to keyboard input
        switchToKeyboardInput();
        showGamepadDisconnectedMessage();
    }
}
```

### Input Validation

```cpp
// Validate mouse coordinates are within bounds
Vector2D mousePos = input.getMousePosition();
int screenWidth = GameEngine::Instance().getLogicalWidth();
int screenHeight = GameEngine::Instance().getLogicalHeight();

if (mousePos.getX() >= 0 && mousePos.getX() < screenWidth &&
    mousePos.getY() >= 0 && mousePos.getY() < screenHeight) {
    // Mouse position is valid
    handleMouseInput(mousePos);
}
```

## API Reference

### Core Methods

```cpp
// Initialization and cleanup
bool init();  // Called automatically by GameEngine
void clean(); // Called automatically by GameEngine

// Input polling (call once per frame)
void update();

// Keyboard input
bool isKeyDown(SDL_Scancode key) const;
bool wasKeyPressed(SDL_Scancode key) const;
bool wasKeyReleased(SDL_Scancode key) const;

// Mouse input
const Vector2D& getMousePosition() const;
bool getMouseButtonState(int buttonNumber) const;

// Gamepad input
int getConnectedGamepadCount() const;
Vector2D getGamepadAxis(int gamepadIndex, int axis) const;
bool getGamepadButtonState(int gamepadIndex, int button) const;
float getGamepadTrigger(int gamepadIndex, int trigger) const;

// Window events (handled internally)
void onWindowResize(const SDL_Event& event);
```

### Constants

```cpp
// Mouse button constants
static constexpr int LEFT = 0;
static constexpr int MIDDLE = 1;
static constexpr int RIGHT = 2;

// Gamepad axis constants
static constexpr int LEFT_STICK = 0;
static constexpr int RIGHT_STICK = 1;

// Gamepad button constants
static constexpr int BUTTON_A = 0;
static constexpr int BUTTON_B = 1;
static constexpr int BUTTON_X = 2;
static constexpr int BUTTON_Y = 3;

// Gamepad trigger constants
static constexpr int LEFT_TRIGGER = 0;
static constexpr int RIGHT_TRIGGER = 1;
```

## Best Practices

### Input Handling Patterns

```cpp
// ✅ GOOD: Use appropriate input method for action type
class Player {
public:
    void update(float deltaTime) {
        InputManager& input = InputManager::Instance();
        
        // Continuous actions: use isKeyDown()
        if (input.isKeyDown(SDL_SCANCODE_W)) {
            moveForward(deltaTime);
        }
        
        // Discrete actions: use wasKeyPressed()
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE)) {
            jump();  // Only jump once per key press
        }
        
        // State changes: use wasKeyReleased()
        if (input.wasKeyReleased(SDL_SCANCODE_LSHIFT)) {
            stopRunning();
        }
    }
};

// ✅ GOOD: Cache input manager reference
void GameState::update(float deltaTime) {
    InputManager& input = InputManager::Instance();  // Cache reference
    
    // Use cached reference multiple times
    handleMovement(input);
    handleActions(input);
    handleUI(input);
}

// ❌ BAD: Multiple singleton accesses
void GameState::update(float deltaTime) {
    if (InputManager::Instance().isKeyDown(SDL_SCANCODE_W)) { /* ... */ }
    if (InputManager::Instance().wasKeyPressed(SDL_SCANCODE_SPACE)) { /* ... */ }
    // Repeated singleton access is inefficient
}
```

### Cross-Platform Input

```cpp
// ✅ GOOD: Support both keyboard and gamepad
Vector2D getMovementInput() {
    InputManager& input = InputManager::Instance();
    Vector2D movement(0, 0);
    
    // Keyboard input
    if (input.isKeyDown(SDL_SCANCODE_W)) movement.setY(-1);
    if (input.isKeyDown(SDL_SCANCODE_S)) movement.setY(1);
    if (input.isKeyDown(SDL_SCANCODE_A)) movement.setX(-1);
    if (input.isKeyDown(SDL_SCANCODE_D)) movement.setX(1);
    
    // Gamepad input (if connected)
    if (input.getConnectedGamepadCount() > 0) {
        Vector2D gamepadMovement = input.getGamepadAxis(0, InputManager::LEFT_STICK);
        if (gamepadMovement.magnitude() > 0.1f) {  // Deadzone
            movement = gamepadMovement;
        }
    }
    
    return movement;
}
```

## Integration with Other Systems

The InputManager works seamlessly with:
- **[GameEngine](../GameEngine.md)**: Automatic initialization and coordinate system integration
- **[UIManager](../ui/UIManager_Guide.md)**: Coordinate conversion ensures perfect mouse accuracy
- **[FontManager](FontManager.md)**: Window resize events trigger automatic font refresh
- **Game States**: Direct integration for player input and UI interaction

The InputManager serves as the bridge between raw SDL input events and the game's coordinate system, ensuring that all input handling works correctly across platforms and display configurations.