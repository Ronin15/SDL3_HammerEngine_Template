# TimestepManager Documentation

## Overview

The `TimestepManager` class provides consistent game timing with accumulator-based fixed timestep handling that separates update timing (fixed timestep for consistent physics/logic) from render timing (VSync-driven or software-limited). The main loop implementation is in `HammerMain.cpp`, with TimestepManager handling timing calculations.

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Timing Philosophy](#timing-philosophy)
- [Core Methods](#core-methods)
- [Performance Features](#performance-features)
- [Configuration](#configuration)
- [API Reference](#api-reference)
- [Best Practices](#best-practices)
- [Examples](#examples)

## Quick Start

### Basic Usage

```cpp
#include "core/TimestepManager.hpp"

// Create with 60 FPS target and 1/60s fixed timestep
TimestepManager timestep(60.0f, 1.0f/60.0f);

// Main loop
while (running) {
    timestep.startFrame();
    
    // Handle events
    processEvents();
    
    // Fixed timestep updates
    while (timestep.shouldUpdate()) {
        float deltaTime = timestep.getUpdateDeltaTime(); // Always 1/60s
        updateGame(deltaTime);
    }
    
    // Variable timestep rendering
    if (timestep.shouldRender()) {
        float interpolation = timestep.getRenderInterpolation(); // Always 0.0f
        renderGame(interpolation);
    }
    
    timestep.endFrame(); // Handles frame rate limiting
}
```

### Integration with GameEngine

```cpp
// TimestepManager is created and owned by GameEngine
GameEngine& engine = GameEngine::Instance();
engine.init("My Game", 1280, 720, false);

// Access for configuration
TimestepManager& ts = engine.getTimestepManager();

// Main loop pattern
while (engine.isRunning()) {
    ts.startFrame();
    engine.handleEvents();

    while (ts.shouldUpdate()) {
        engine.update(ts.getUpdateDeltaTime());
    }

    engine.render();
    ts.endFrame();
}
```

## Architecture

### Design Philosophy

The TimestepManager implements a **simplified timing model** that prioritizes:

1. **Consistency**: Fixed timestep ensures deterministic game logic
2. **Simplicity**: 1:1 frame-to-update mapping eliminates complex accumulator logic
3. **Performance**: Minimal overhead with SDL3's high-precision timing
4. **Predictability**: No timing drift or micro-stuttering

### Core Components

```cpp
class TimestepManager {
private:
    // Timing configuration
    float m_targetFPS;                    // Target frames per second for rendering
    float m_fixedTimestep;               // Fixed timestep for updates (seconds)
    float m_targetFrameTime;             // Target frame time (1/targetFPS)
    
    // Frame timing (SDL_GetTicks() returns Uint64 milliseconds)
    Uint64 m_frameStart;
    Uint64 m_lastFrameTime;
    
    // Simplified timing pattern
    double m_accumulator;                // Simple frame timing state
    
    // Frame statistics
    uint32_t m_lastFrameTimeMs;         // Last frame duration in milliseconds
    float m_currentFPS;                 // Current measured FPS
    uint32_t m_frameCount;              // Frame counter for FPS calculation
    Uint64 m_fpsLastUpdate;             // Last FPS update time
    
    // State flags
    bool m_shouldRender;                // True when render should happen this frame
    bool m_firstFrame;                  // True for the very first frame
};
```

## Timing Philosophy

### Simplified 1:1 Frame Mapping

Unlike traditional game loops with complex accumulator patterns, TimestepManager uses a simplified approach:

```cpp
void TimestepManager::startFrame() {
    // Simple timing - each frame gets one update and one render
    m_accumulator = m_fixedTimestep;
    m_shouldRender = true;
}

bool TimestepManager::shouldUpdate() {
    // Simple 1:1 frame to update mapping
    if (m_accumulator >= m_fixedTimestep) {
        m_accumulator = 0.0;
        return true;
    }
    return false;
}
```

**Benefits:**
- **No Timing Drift**: Each frame processes exactly one update
- **Predictable Behavior**: Consistent frame-to-update relationship
- **Reduced Complexity**: No complex catch-up logic or frame skipping
- **Better Performance**: Minimal computational overhead

### Frame Rate Limiting

```cpp
void TimestepManager::limitFrameRate() const {
    Uint64 currentTime = SDL_GetTicks();
    double frameTime = (currentTime - m_frameStart) / 1000.0;
    
    if (frameTime < m_targetFrameTime) {
        double sleepTime = m_targetFrameTime - frameTime;
        uint32_t sleepMs = static_cast<uint32_t>(sleepTime * 1000.0);
        
        if (sleepMs > 0) {
            SDL_Delay(sleepMs);
        }
    }
}
```

## Core Methods

### Frame Lifecycle

```cpp
// Start of each frame
void startFrame();

// Check if update should run (typically returns true once per frame)
bool shouldUpdate();

// Check if rendering should run (returns true once per frame)
bool shouldRender() const;

// End of frame - handles rate limiting
void endFrame();
```

### Timing Access

```cpp
// Get fixed delta time for updates (always consistent)
float getUpdateDeltaTime() const;

// Get interpolation factor (always 0.0f for simplified timing)
float getRenderInterpolation() const;

// Get current measured FPS
float getCurrentFPS() const;

// Get last frame time in milliseconds
uint32_t getFrameTimeMs() const;
```

### Configuration

```cpp
// Change target rendering FPS
void setTargetFPS(float fps);

// Change fixed timestep for updates
void setFixedTimestep(float timestep);

// Reset timing state (useful for pause/resume)
void reset();
```

## Performance Features

### FPS Monitoring

```cpp
void TimestepManager::updateFPS() {
    Uint64 currentTime = SDL_GetTicks();
    double timeSinceLastUpdate = (currentTime - m_fpsLastUpdate) / 1000.0;
    
    // Update FPS calculation every second
    if (timeSinceLastUpdate >= 1.0) {
        m_currentFPS = static_cast<float>(m_frameCount) / static_cast<float>(timeSinceLastUpdate);
        m_frameCount = 0;
        m_fpsLastUpdate = currentTime;
    }
}
```

### Performance Detection

```cpp
bool TimestepManager::isFrameTimeExcessive() const {
    // Consider frame time excessive if it's more than 2x target frame time
    return m_lastFrameTimeMs > static_cast<uint32_t>(m_targetFrameTime * 2000.0f);
}
```

### High-Precision Timing

- Uses SDL3's `SDL_GetTicks()` for millisecond precision
- Uint64 timestamps prevent overflow issues
- Efficient frame time calculations

## Configuration

### Constructor Parameters

```cpp
TimestepManager(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f);
```

**Parameters:**
- `targetFPS`: Target frames per second for rendering (affects frame rate limiting)
- `fixedTimestep`: Fixed timestep for updates in seconds (affects game logic consistency)

### Runtime Configuration

```cpp
// Change target FPS dynamically
timestep.setTargetFPS(144.0f);

// Change update frequency
timestep.setFixedTimestep(1.0f/120.0f); // 120 Hz updates

// Reset timing state (e.g., after pause)
timestep.reset();
```

### Common Configurations

```cpp
// Standard 60 FPS gaming
TimestepManager standard(60.0f, 1.0f/60.0f);

// High refresh rate gaming
TimestepManager highRefresh(144.0f, 1.0f/60.0f); // 144 FPS render, 60 FPS logic

// Competitive gaming
TimestepManager competitive(240.0f, 1.0f/120.0f); // 240 FPS render, 120 FPS logic

// Low-power/mobile
TimestepManager mobile(30.0f, 1.0f/30.0f);
```

## API Reference

### Constructor

```cpp
explicit TimestepManager(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f);
```

### Frame Control Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `startFrame()` | Call at the start of each frame | `void` |
| `shouldUpdate()` | Returns true if an update should be performed | `bool` |
| `shouldRender()` | Returns true if rendering should be performed | `bool` |
| `endFrame()` | Call at the end of each frame, handles rate limiting | `void` |

### Timing Access Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `getUpdateDeltaTime()` | Gets the fixed delta time for updates | `float` |
| `getRenderInterpolation()` | Gets interpolation factor (always 0.0f) | `float` |
| `getCurrentFPS()` | Gets current measured FPS | `float` |
| `getTargetFPS()` | Gets target FPS setting | `float` |
| `getFrameTimeMs()` | Gets last frame time in milliseconds | `uint32_t` |

### Configuration Methods

| Method | Description | Return Type |
|--------|-------------|-------------|
| `setTargetFPS(float fps)` | Set new target FPS | `void` |
| `setFixedTimestep(float timestep)` | Set new fixed timestep | `void` |
| `reset()` | Reset timing state | `void` |
| `isFrameTimeExcessive()` | Check if frame time is excessive | `bool` |

## Best Practices

### 1. Consistent Update Logic

```cpp
// Good: Use fixed timestep for game logic
while (timestep.shouldUpdate()) {
    float deltaTime = timestep.getUpdateDeltaTime(); // Always consistent
    
    // Physics simulation
    physics.update(deltaTime);
    
    // Game logic
    gameWorld.update(deltaTime);
    
    // AI processing
    aiManager.update(deltaTime);
}
```

### 2. Separate Rendering from Logic

```cpp
// Good: Render independently of update frequency
if (timestep.shouldRender()) {
    float interpolation = timestep.getRenderInterpolation();
    
    // Render game state (interpolation is 0.0f for simplified timing)
    renderer.render(gameWorld, interpolation);
}
```

### 3. Performance Monitoring

```cpp
// Monitor performance periodically
static int frameCounter = 0;
if (++frameCounter % 300 == 0) { // Every 5 seconds at 60 FPS
    float fps = timestep.getCurrentFPS();
    uint32_t frameTime = timestep.getFrameTimeMs();
    
    if (timestep.isFrameTimeExcessive()) {
        TIMESTEP_WARN("Frame time excessive: " + std::to_string(frameTime) + "ms");
    }
    
    TIMESTEP_INFO("FPS: " + std::to_string(fps) + ", Frame time: " + std::to_string(frameTime) + "ms");
}
```

### 4. Pause/Resume Handling

```cpp
void setPaused(bool paused) {
    if (m_paused && !paused) {
        // Resuming from pause - reset timing to avoid time jumps
        timestep.reset();
    }
    m_paused = paused;
}
```

### 5. Dynamic FPS Adjustment

```cpp
void adjustPerformance() {
    float currentFPS = timestep.getCurrentFPS();
    float targetFPS = timestep.getTargetFPS();
    
    // If performance is poor, reduce target FPS
    if (currentFPS < targetFPS * 0.8f) {
        float newTarget = std::max(30.0f, targetFPS * 0.9f);
        timestep.setTargetFPS(newTarget);
        TIMESTEP_INFO("Reduced target FPS to " + std::to_string(newTarget));
    }
}
```

## Examples

### Complete Game Loop Integration

```cpp
#include "core/TimestepManager.hpp"
#include "core/Logger.hpp"

class GameApplication {
private:
    TimestepManager m_timestep;
    bool m_running{true};
    
public:
    GameApplication() : m_timestep(60.0f, 1.0f/60.0f) {}
    
    void run() {
        TIMESTEP_INFO("Starting game loop with " + 
                     std::to_string(m_timestep.getTargetFPS()) + " FPS target");
        
        while (m_running) {
            m_timestep.startFrame();
            
            // Handle input and events
            processEvents();
            
            // Fixed timestep updates
            while (m_timestep.shouldUpdate()) {
                float deltaTime = m_timestep.getUpdateDeltaTime();
                updateGame(deltaTime);
            }
            
            // Variable timestep rendering
            if (m_timestep.shouldRender()) {
                float interpolation = m_timestep.getRenderInterpolation();
                renderGame(interpolation);
            }
            
            m_timestep.endFrame();
            
            // Monitor performance
            monitorPerformance();
        }
    }
    
private:
    void updateGame(float deltaTime) {
        // Game logic with consistent timing
        // deltaTime is always 1/60s = 0.0167s
        
        // Update physics
        physics.step(deltaTime);
        
        // Update AI
        aiManager.update(deltaTime);
        
        // Update game state
        gameWorld.update(deltaTime);
    }
    
    void renderGame(float interpolation) {
        // Rendering with interpolation (always 0.0f for simplified timing)
        renderer.clear();
        renderer.render(gameWorld);
        renderer.present();
    }
    
    void monitorPerformance() {
        static int frameCount = 0;
        if (++frameCount % 60 == 0) { // Every second
            float fps = m_timestep.getCurrentFPS();
            uint32_t frameTime = m_timestep.getFrameTimeMs();
            
            if (m_timestep.isFrameTimeExcessive()) {
                TIMESTEP_WARN("Performance issue - Frame time: " + 
                             std::to_string(frameTime) + "ms, FPS: " + 
                             std::to_string(fps));
            }
        }
    }
};
```

### High-Performance Configuration

```cpp
class HighPerformanceTimer {
private:
    TimestepManager m_timestep;
    
public:
    HighPerformanceTimer() : m_timestep(144.0f, 1.0f/60.0f) {
        // 144 FPS rendering with 60 FPS logic for optimal performance
        TIMESTEP_INFO("High-performance timing: 144 FPS render, 60 FPS logic");
    }
    
    void configureForCompetitive() {
        // Ultra-high refresh rate for competitive gaming
        m_timestep.setTargetFPS(240.0f);
        m_timestep.setFixedTimestep(1.0f/120.0f); // 120 Hz logic
        
        TIMESTEP_INFO("Configured for competitive gaming: 240 FPS render, 120 FPS logic");
    }
    
    void configureDynamically() {
        float currentFPS = m_timestep.getCurrentFPS();
        
        // Adjust based on current performance
        if (currentFPS > 100.0f) {
            m_timestep.setTargetFPS(144.0f);
        } else if (currentFPS > 55.0f) {
            m_timestep.setTargetFPS(60.0f);
        } else {
            m_timestep.setTargetFPS(30.0f);
        }
    }
};
```

### Mobile/Low-Power Configuration

```cpp
class MobileTimer {
private:
    TimestepManager m_timestep;
    
public:
    MobileTimer() : m_timestep(30.0f, 1.0f/30.0f) {
        // 30 FPS for battery conservation
        TIMESTEP_INFO("Mobile timing: 30 FPS for power efficiency");
    }
    
    void enablePowerSaving() {
        m_timestep.setTargetFPS(20.0f);
        m_timestep.setFixedTimestep(1.0f/20.0f);
        
        TIMESTEP_INFO("Power saving mode: 20 FPS");
    }
    
    void enablePerformanceMode() {
        m_timestep.setTargetFPS(60.0f);
        m_timestep.setFixedTimestep(1.0f/60.0f);
        
        TIMESTEP_INFO("Performance mode: 60 FPS");
    }
};
```

---

The TimestepManager provides a robust, simplified timing foundation that eliminates common timing issues while maintaining professional-grade performance and consistency. Its 1:1 frame-to-update mapping ensures predictable behavior across different hardware configurations and frame rates.