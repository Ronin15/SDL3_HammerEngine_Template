# UIManager Architecture Documentation

## Overview

The UIManager follows a **hybrid architecture** with **centralized theme management** where core world systems are managed globally by GameEngine for optimal performance, while UI systems are managed by individual game states for flexibility. The centralized theme system provides professional, consistent styling across all UI while maintaining state-specific control. This design ensures optimal performance, clear separation of concerns, and maximum flexibility for both world simulation and state-specific UI behavior.

## Hybrid Architecture Principles

### 1. Global vs State-Managed Systems

**Global Systems (Updated by GameEngine):**
- **InputManager**: Fundamental to all states, minimal overhead, always needed
- **AIManager**: World simulation with 10K+ entities, benefits from consistent global updates and threading
- **EventManager**: Global game events (weather, scene changes), batch processing optimization

**State-Managed Systems (Updated by individual states):**
- **UIManager**: Optional, state-specific, only updated when UI is actually used
- **Audio/Visual effects**: State-specific requirements and lifecycle

**Centralized Theme Management:**
- **Automatic styling**: Components use professional themes without manual styling
- **Theme switching**: Easy light/dark mode switching across entire application
- **Consistent appearance**: No style conflicts between states
- **Enhanced UX**: Improved contrast and mouse accuracy built-in

### 2. State-Managed UI Updates

**✅ Correct Pattern with Centralized Themes:**
```cpp
void UIExampleState::update(float deltaTime) {
    // Each state that uses UI is responsible for updating it
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    // Continue with state-specific updates...
}

void UIExampleState::enter() {
    auto& ui = UIManager::Instance();
    
    // Create theme background for full-screen UI
    ui.createThemeBackground(windowWidth, windowHeight);
    
    // Components automatically use professional theme styling
    ui.createButton("play_btn", {x, y, w, h}, "Play Game");
    ui.createList("options_list", {x, y, w, h});
    
    // Optional: Switch themes easily
    // ui.setThemeMode("dark");
}

void UIExampleState::exit() {
    auto& ui = UIManager::Instance();
    
    // Clean up using centralized methods
    ui.removeComponentsWithPrefix("mystate_");
    ui.removeThemeBackground();
    ui.resetToDefaultTheme();
}
```

**❌ Incorrect Pattern:**
```cpp
void GameEngine::update(float deltaTime) {
    // DON'T DO THIS - Global UI updates in GameEngine
    UIManager::Instance().update(deltaTime);
    mp_gameStateManager->update(deltaTime);
}

void SomeGameState::update(float deltaTime) {
    // DON'T DO THIS - Redundant manager updates in states
    AIManager::Instance().update(deltaTime);
    EventManager::Instance().update();
    // These are now handled globally by GameEngine
}

void BadUIState::create() {
    // DON'T DO THIS - Manual styling fights centralized themes
    UIStyle buttonStyle;
    buttonStyle.backgroundColor = {70, 130, 180, 255};
    buttonStyle.hoverColor = {100, 149, 237, 255};
    // ... lots of manual styling code
    ui.setStyle("my_button", buttonStyle);
    
    // DON'T DO THIS - Manual component cleanup
    ui.removeComponent("comp1");
    ui.removeComponent("comp2");
    ui.removeComponent("comp3");
    // ... dozens of manual removals
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

## Benefits of This Hybrid Architecture

### Performance Optimization
- **Global Systems**: World simulation systems (AI, Events) benefit from consistent updates and threading optimization
- **Conditional Updates**: UI is only updated when states actually use UI components
- **Manager Caching**: GameEngine caches manager references for optimal performance
- **Memory Efficiency**: No unnecessary UI processing in non-UI game states

### Clean Separation of Concerns
- **Engine Responsibility**: GameEngine handles world simulation, core systems, threading, and resource management
- **State Responsibility**: Game states handle their specific UI needs and state-specific systems
- **Manager Responsibility**: Each manager provides its framework without dictating usage patterns

### Maximum Flexibility
- **World Consistency**: Global systems ensure consistent world state across all game states
- **State-Specific Behavior**: Each state can customize UI and other optional systems
- **Conditional Systems**: States can opt-in to systems like UI without affecting global performance
- **Independent Lifecycle**: Optional system lifecycle is tied to state lifecycle, preventing resource leaks

## Implementation Guidelines

### For New Game States Using UI

1. **Include UIManager Update in State Update:**
```cpp
void YourGameState::update(float deltaTime) {
    // Update UIManager for states that use UI components
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    
    // Your state-specific updates...
    // DO NOT update global systems (AI, Events, Input) - they're handled by GameEngine
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
    // Global systems (AI, Events, Input) are handled by GameEngine
}

void NonUIGameState::render() {
    // No UIManager rendering needed
    // State-specific rendering only...
}
```

### GameEngine Implementation (Reference)

```cpp
void GameEngine::update(float deltaTime) {
    // Global systems updated by GameEngine for optimal performance
    mp_inputManager->update();                    // Always needed
    mp_aiManager->update(deltaTime);             // World simulation
    mp_eventManager->update();                   // Global events
    
    // State-specific systems handled by states
    mp_gameStateManager->update(deltaTime);     // Delegates to current state
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

If you have existing code with inappropriate manager updates:

1. **Remove Redundant Updates from States:**
```cpp
// Remove these from individual game states (now handled globally)
void AIDemoState::update(float deltaTime) {
    // DON'T DO THIS - AIManager is updated globally
    // AIManager::Instance().update(deltaTime);
    
    // State-specific logic only
}
```

2. **Add UI Updates to UI States Only:**
```cpp
// Add this only to states that use UI components
void YourUIState::update(float deltaTime) {
    auto& uiManager = UIManager::Instance();
    if (!uiManager.isShutdown()) {
        uiManager.update(deltaTime);
    }
    // ... rest of state update
}
```

3. **GameEngine Handles Global Systems:**
```cpp
// This is now handled automatically by GameEngine
void GameEngine::update(float deltaTime) {
    mp_inputManager->update();        // Cached reference
    mp_aiManager->update(deltaTime);  // Cached reference  
    mp_eventManager->update();        // Cached reference
    mp_gameStateManager->update(deltaTime);
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
- [AIManager Documentation](../ai/AIManager.md)
- [EventManager Documentation](../EventManager.md)

## Troubleshooting

### Common Issues

**Issue**: UI not updating or responding
**Solution**: Ensure UIManager::update() is called in your UI state's update method

**Issue**: UI not rendering
**Solution**: Ensure UIManager::render() is called in your UI state's render method

**Issue**: UI components persist between states
**Solution**: Properly clean up UI components in state's exit() method

**Issue**: Redundant manager updates causing performance issues
**Solution**: Remove AI/Event/Input manager updates from states - they're handled globally

**Issue**: Global systems not working in states
**Solution**: Verify GameEngine is properly updating global systems before state updates

**Issue**: Manager caching errors
**Solution**: Ensure GameEngine::init() completes successfully before using cached references