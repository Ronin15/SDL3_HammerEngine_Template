# UIManager Architecture Documentation

## Overview

The UIManager follows a **state-managed architecture** where individual game states are responsible for managing their own UI lifecycle. This design ensures optimal performance, clear separation of concerns, and maximum flexibility for state-specific UI behavior.

## Architecture Principles

### 1. State-Managed UI Updates

**✅ Correct Pattern:**
```cpp
void UIExampleState::update(float deltaTime) {
    // Each state that uses UI is responsible for updating it
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    // Continue with state-specific updates...
}
```

**❌ Incorrect Pattern:**
```cpp
void GameEngine::update(float deltaTime) {
    // DON'T DO THIS - Global UI updates in GameEngine
    UIManager::Instance().update(deltaTime);
    mp_gameStateManager->update(deltaTime);
}
```

### 2. State-Managed UI Rendering

**✅ Correct Pattern:**
```cpp
void UIExampleState::render() {
    // Each state renders its own UI components
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
}
```

## Benefits of This Architecture

### Performance Optimization
- **Conditional Updates**: UI is only updated when states actually use UI components
- **Selective Rendering**: Only active UI states perform expensive UI operations
- **Memory Efficiency**: No unnecessary UI processing in non-UI game states

### Clean Separation of Concerns
- **Engine Responsibility**: GameEngine focuses on core game loop, threading, and resource management
- **State Responsibility**: Game states handle their specific UI needs and lifecycle
- **Manager Responsibility**: UIManager provides the UI framework without dictating usage patterns

### Maximum Flexibility
- **State-Specific Behavior**: Each state can customize UI update frequency, rendering order, and component management
- **Conditional UI**: States can easily enable/disable UI based on game conditions
- **Independent Lifecycle**: UI components lifecycle is tied to state lifecycle, preventing resource leaks

## Implementation Guidelines

### For New Game States Using UI

1. **Include UIManager Update in State Update:**
```cpp
void YourGameState::update(float deltaTime) {
    // Always update UIManager first if using UI
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    // Your state-specific updates...
}
```

2. **Include UIManager Rendering in State Render:**
```cpp
void YourGameState::render() {
    // Render your UI components
    auto& gameEngine = GameEngine::Instance();
    auto& ui = UIManager::Instance();
    ui.render(gameEngine.getRenderer());
    
    // Additional state-specific rendering...
}
```

### For Game States Not Using UI

Simply omit the UIManager calls - the pattern is self-selecting:

```cpp
void NonUIGameState::update(float deltaTime) {
    // No UIManager updates needed
    // State-specific updates only...
}

void NonUIGameState::render() {
    // No UIManager rendering needed
    // State-specific rendering only...
}
```

## Example State Integration

See `UIExampleState` for a complete example of proper UIManager integration:

```cpp
class UIExampleState : public GameState {
public:
    void update(float deltaTime) override {
        // Update UI Manager - architectural pattern
        auto& uiManager = UIManager::Instance();
        if (!uiManager.isShutdown()) {
            uiManager.update(deltaTime);
        }
        
        // UI screen updates
        if (m_uiScreen) {
            m_uiScreen->update(deltaTime);
        }
        
        // State-specific logic...
    }
    
    void render() override {
        // Render UI components
        auto& gameEngine = GameEngine::Instance();
        auto& ui = UIManager::Instance();
        ui.render(gameEngine.getRenderer());
    }
    
private:
    std::unique_ptr<UIScreen> m_uiScreen;
};
```

## Threading Considerations

### Thread Safety
- UIManager is designed to be called from the main thread only (OpenGL/SDL rendering thread)
- State updates and renders occur on the main thread, ensuring thread safety
- No additional synchronization needed for UIManager calls

### Performance with Threading
- UI updates don't block background thread operations
- Game logic threading remains independent of UI operations
- ThreadSystem can continue processing while UI renders

## Migration Notes

### From Global UI Updates

If you have existing code with global UIManager updates in GameEngine:

1. **Remove from GameEngine:**
```cpp
// Remove this from GameEngine::update()
UIManager& uiMgr = UIManager::Instance();
if (!uiMgr.isShutdown()) {
    uiMgr.update(deltaTime);
}
```

2. **Add to Each UI State:**
```cpp
// Add this to each state that uses UI
void YourUIState::update(float deltaTime) {
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    // ... rest of state update
}
```

## Best Practices

### UI State Design
- Keep UI component creation in state `enter()` method
- Handle UI cleanup in state `exit()` method
- Use UIScreen classes for complex UI hierarchies
- Implement proper callback handling for UI interactions

### Error Handling
```cpp
void YourState::update(float deltaTime) {
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        try {
            uiManager.update(deltaTime);
        } catch (const std::exception& e) {
            std::cerr << "UI update error: " << e.what() << std::endl;
            // Handle gracefully
        }
    }
}
```

### Component Management
```cpp
class YourUIState : public GameState {
public:
    bool enter() override {
        // Create UI components when entering state
        auto& ui = UIManager::Instance();
        ui.createButton("my_button", {100, 100, 200, 50}, "Click Me");
        ui.setOnClick("my_button", [this]() { handleButtonClick(); });
        return true;
    }
    
    bool exit() override {
        // Clean up UI components when exiting state
        auto& ui = UIManager::Instance();
        ui.removeComponent("my_button");
        return true;
    }
};
```

## Related Documentation

- [UIManager API Reference](../managers/UIManager.hpp)
- [SDL3 Logical Presentation Modes](SDL3_Logical_Presentation_Modes.md)
- [UI Stress Testing Guide](UI_Stress_Testing_Guide.md)
- [Game State Management](../GameStateManager.md)

## Troubleshooting

### Common Issues

**Issue**: UI not updating or responding
**Solution**: Ensure UIManager::update() is called in your state's update method

**Issue**: UI not rendering
**Solution**: Ensure UIManager::render() is called in your state's render method

**Issue**: UI components persist between states
**Solution**: Properly clean up UI components in state's exit() method

**Issue**: Performance degradation
**Solution**: Verify only UI-using states are calling UIManager updates