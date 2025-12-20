# Interpolation System Documentation

**Where to find the code:**
- Entity: `include/entities/Entity.hpp`
- Camera: `include/utils/Camera.hpp` and `src/utils/Camera.cpp`
- Main Loop: `src/core/HammerMain.cpp`

## Overview

The Interpolation System provides smooth rendering for entities and camera at any display refresh rate, regardless of the fixed timestep update rate.

**Architecture Note:** With the single-threaded main loop (update completes before render), atomics are no longer needed. The interpolation now uses simple member variables (`m_position` and `m_previousPosition`).

## The Problem

Without interpolation, entities appear to "stutter" or "jitter" because:

1. **Fixed Timestep Updates:** Game logic updates at a fixed rate (e.g., 60 Hz)
2. **Variable Render Rate:** Rendering may occur at different rates (e.g., 144 Hz display)
3. **Position Snapping:** Entities jump to discrete positions on each update

### Before Interpolation

```
Update 1: Position = (100, 100)
Update 2: Position = (110, 100)

Render frames between updates show entity "snapping" between positions
```

### With Interpolation

```
Update 1: Position = (100, 100), Previous = (90, 100)
Update 2: Position = (110, 100), Previous = (100, 100)

Render with alpha=0.5: Display position = (105, 100)  // Smooth!
```

## The Solution: Simple Linear Interpolation

### Entity Interpolation

Entity uses `m_position` and `m_previousPosition` for interpolation:

```cpp
// Entity.hpp
Vector2D m_position{0, 0};
Vector2D m_previousPosition{0, 0};  // For render interpolation

Vector2D getInterpolatedPosition(float alpha) const {
    return Vector2D(
        m_previousPosition.getX() + (m_position.getX() - m_previousPosition.getX()) * alpha,
        m_previousPosition.getY() + (m_position.getY() - m_previousPosition.getY()) * alpha);
}
```

### Camera Interpolation

Camera uses the same pattern:

```cpp
// Camera.hpp
Vector2D m_position;
Vector2D m_previousPosition;

// Camera.cpp - getRenderOffset()
Vector2D center(
    m_previousPosition.getX() + (m_position.getX() - m_previousPosition.getX()) * interpolationAlpha,
    m_previousPosition.getY() + (m_position.getY() - m_previousPosition.getY()) * interpolationAlpha);
```

## Entity Interpolation Pattern

### Update (Fixed Timestep)

```cpp
void Player::update(float deltaTime) {
    // Step 1: Store current position as previous (at START of update)
    storePositionForInterpolation();

    // Step 2: Update position normally
    m_velocity += m_acceleration * deltaTime;
    updatePositionFromMovement(m_position + m_velocity * deltaTime);
}
```

### Render (After Update Completes)

```cpp
void Player::render(SDL_Renderer* renderer, float cameraX, float cameraY,
                    float interpolationAlpha) {
    // Get smooth interpolated position
    Vector2D renderPos = getInterpolatedPosition(interpolationAlpha);

    // Use interpolated position for rendering
    float screenX = renderPos.getX() - cameraX;
    float screenY = renderPos.getY() - cameraY;

    // Draw at smooth position
    SDL_FRect destRect = {screenX, screenY, m_width, m_height};
    SDL_RenderTexture(renderer, m_texture, nullptr, &destRect);
}
```

### Implementation Details

```cpp
// Store previous position (call at START of update)
void storePositionForInterpolation() {
    m_previousPosition = m_position;
}

// Update position without resetting previous (for smooth movement)
void updatePositionFromMovement(const Vector2D& position) {
    m_position = position;
}

// Get interpolated position for rendering
Vector2D getInterpolatedPosition(float alpha) const {
    return Vector2D(
        m_previousPosition.getX() + (m_position.getX() - m_previousPosition.getX()) * alpha,
        m_previousPosition.getY() + (m_position.getY() - m_previousPosition.getY()) * alpha);
}
```

## Camera Interpolation Pattern

Camera uses the same pattern but exposes it through `getRenderOffset()`:

```cpp
// In update() - store previous position
void Camera::update(float deltaTime) {
    m_previousPosition = m_position;

    // ... camera movement logic ...

    m_position = targetPos;  // Update position
}

// In render path - get interpolated offset
Vector2D Camera::getRenderOffset(float& offsetX, float& offsetY,
                                  float interpolationAlpha) const {
    // Interpolate center position
    Vector2D center(
        m_previousPosition.getX() + (m_position.getX() - m_previousPosition.getX()) * interpolationAlpha,
        m_previousPosition.getY() + (m_position.getY() - m_previousPosition.getY()) * interpolationAlpha);

    // Calculate offset from interpolated center
    computeOffsetFromCenter(center.getX(), center.getY(), offsetX, offsetY);
    return center;
}
```

## Teleportation Handling

When teleporting an entity, you must reset both positions to prevent "sliding":

```cpp
void Entity::setPosition(const Vector2D& position) {
    m_position = position;
    m_previousPosition = position;  // Prevents interpolation sliding
}
```

## Integration with Main Loop

The interpolation alpha is calculated by TimestepManager and passed through the render chain:

```cpp
// HammerMain.cpp - main loop
TimestepManager& ts = engine.getTimestepManager();

while (engine.isRunning()) {
    ts.startFrame();  // Adds delta to internal accumulator
    engine.handleEvents();

    // Fixed timestep updates - drain accumulator
    while (ts.shouldUpdate()) {
        if (engine.hasNewFrameToRender()) {
            engine.swapBuffers();
        }
        engine.update(ts.getUpdateDeltaTime());
    }

    // Render uses alpha from remaining accumulator
    engine.render();  // TimestepManager provides interpolation alpha
    ts.endFrame();
}

// GameState render uses alpha
void GamePlayState::render(float interpolationAlpha) {
    // Get interpolated camera offset
    float cameraX, cameraY;
    m_camera->getRenderOffset(cameraX, cameraY, interpolationAlpha);

    // Render entities with interpolation
    for (auto& entity : m_entities) {
        entity->render(renderer, cameraX, cameraY, interpolationAlpha);
    }
}
```

## Single-Threaded Architecture

With the single-threaded main loop, update always completes before render:

```cpp
// HammerMain.cpp - sequential execution
while (engine.isRunning()) {
    while (ts.shouldUpdate()) {
        engine.update(ts.getUpdateDeltaTime());  // All updates complete here
    }
    engine.render();  // Render runs after all updates
}
```

This eliminates the need for atomics since there's no concurrent access.

## Performance Characteristics

- **No Synchronization Overhead:** Direct member access, no atomics
- **Cache Friendly:** Simple struct layout
- **Zero Allocations:** No heap allocations in hot path
- **Minimal Computation:** Simple linear interpolation

## When to Use This Pattern

### Use For:
- Player position
- NPC positions
- Camera position
- Any position that affects rendering

### Don't Use For:
- Static objects (no movement)
- UI elements (not affected by camera)
- Objects that teleport every frame

## Common Mistakes

### 1. Forgetting to Store Previous Position

```cpp
// WRONG: Missing storePositionForInterpolation()
void Entity::update(float deltaTime) {
    m_position += m_velocity * deltaTime;
    // Previous position is stale - interpolation will be wrong!
}

// CORRECT: Store at start of update
void Entity::update(float deltaTime) {
    storePositionForInterpolation();  // Store current as previous
    m_position += m_velocity * deltaTime;
}
```

### 2. Not Resetting on Teleport

```cpp
// WRONG: Causes sliding effect
void teleport(const Vector2D& pos) {
    m_position = pos;
    // m_previousPosition is still old location!
}

// CORRECT: Reset both
void teleport(const Vector2D& pos) {
    setPosition(pos);  // Resets both current and previous
}
```

## Related Documentation

- **Entity:** `include/entities/Entity.hpp`
- **Camera:** `docs/utils/Camera.md`
- **TimestepManager:** `docs/managers/TimestepManager.md`
- **GameEngine:** `docs/core/GameEngine.md`

---

*This documentation reflects the simplified interpolation system using direct member access (no atomics) with the single-threaded main loop architecture.*
