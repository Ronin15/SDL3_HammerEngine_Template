# Interpolation System Documentation

**Where to find the code:**
- Entity: `include/entities/Entity.hpp` (lines 123-219)
- Camera: `include/utils/Camera.hpp` (lines 460-469)
- GameLoop: `src/core/GameLoop.cpp`

## Overview

The Interpolation System provides smooth rendering for entities and camera at any display refresh rate, regardless of the fixed timestep update rate. It uses 16-byte aligned atomic structs for lock-free, thread-safe access between the update and render threads.

## The Problem

Without interpolation, entities appear to "stutter" or "jitter" because:

1. **Fixed Timestep Updates:** Game logic updates at a fixed rate (e.g., 60 Hz)
2. **Variable Render Rate:** Rendering may occur at different rates (e.g., 144 Hz display)
3. **Position Snapping:** Entities jump to discrete positions on each update
4. **Competing Reads:** Multiple atomic reads in render path cause inconsistent values

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

## The Solution: Lock-Free Atomic Interpolation

### 16-Byte Aligned Atomic Struct

Both Entity and Camera use a 16-byte aligned struct for atomic operations:

```cpp
// Entity.hpp
struct alignas(16) EntityInterpState {
    float posX{0.0f}, posY{0.0f};
    float prevPosX{0.0f}, prevPosY{0.0f};
};
std::atomic<EntityInterpState> m_interpState{};

// Camera.hpp
struct alignas(16) InterpolationState {
    float posX{0.0f}, posY{0.0f};
    float prevPosX{0.0f}, prevPosY{0.0f};
};
std::atomic<InterpolationState> m_interpState{};
```

### Why 16 Bytes?

- **Lock-Free Guarantee:** 16-byte atomics are lock-free on modern hardware
  - x86-64: Uses `CMPXCHG16B` instruction
  - ARM64: Uses `LDXP/STXP` instruction pair
- **Single Operation:** All four floats are read/written atomically as one unit
- **No Tearing:** Prevents reading partial updates

### Why Alignment?

```cpp
struct alignas(16) EntityInterpState { ... };
```

- **Performance:** Aligned access is faster on most architectures
- **Correctness:** Some architectures require alignment for atomic operations
- **Cache Efficiency:** Aligned data fits cleanly in cache lines

## Entity Interpolation Pattern

### Update Thread (Fixed Timestep)

```cpp
void Player::update(float deltaTime) {
    // Step 1: Store current position as previous (at START of update)
    storePositionForInterpolation();

    // Step 2: Update position normally
    m_velocity += m_acceleration * deltaTime;
    updatePositionFromMovement(m_position + m_velocity * deltaTime);

    // Step 3: Publish state for render thread (at END of update)
    publishInterpolationState();
}
```

### Render Thread (Variable Rate)

```cpp
void Player::render(SDL_Renderer* renderer, float cameraX, float cameraY,
                    float interpolationAlpha) {
    // Get smooth interpolated position (single atomic read)
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

// Publish to atomic state (call at END of update)
void publishInterpolationState() {
    m_interpState.store({
        m_position.getX(), m_position.getY(),
        m_previousPosition.getX(), m_previousPosition.getY()
    }, std::memory_order_release);
}

// Thread-safe interpolated position read
Vector2D getInterpolatedPosition(float alpha) const {
    auto state = m_interpState.load(std::memory_order_acquire);
    return Vector2D(
        state.prevPosX + (state.posX - state.prevPosX) * alpha,
        state.prevPosY + (state.posY - state.prevPosY) * alpha
    );
}
```

## Camera Interpolation Pattern

Camera uses the same pattern but exposes it through `getRenderOffset()`:

```cpp
// In update() - publish interpolation state
void Camera::update(float deltaTime) {
    m_previousPosition = m_position;

    // ... camera movement logic ...

    // Publish for render thread
    m_interpState.store({
        m_position.getX(), m_position.getY(),
        m_previousPosition.getX(), m_previousPosition.getY()
    }, std::memory_order_release);
}

// In render path - get interpolated offset
Vector2D Camera::getRenderOffset(float& offsetX, float& offsetY,
                                  float interpolationAlpha) const {
    // Single atomic read
    auto state = m_interpState.load(std::memory_order_acquire);

    // Interpolate center position
    float centerX = state.prevPosX + (state.posX - state.prevPosX) * interpolationAlpha;
    float centerY = state.prevPosY + (state.posY - state.prevPosY) * interpolationAlpha;

    // Calculate offset from interpolated center
    computeOffsetFromCenter(centerX, centerY, offsetX, offsetY);
    return Vector2D(centerX, centerY);
}
```

## Teleportation Handling

When teleporting an entity, you must reset both positions to prevent "sliding":

```cpp
void Entity::setPosition(const Vector2D& position) {
    m_position = position;
    m_previousPosition = position;  // Prevents interpolation sliding

    // Publish immediately
    m_interpState.store({
        position.getX(), position.getY(),
        position.getX(), position.getY()  // Same values = no interpolation
    }, std::memory_order_release);
}
```

## Integration with GameLoop

The interpolation alpha is calculated by GameLoop and passed through the render chain:

```cpp
// GameLoop::run()
float accumulator = 0.0f;
const float fixedTimestep = 1.0f / 60.0f;  // 60 Hz updates

while (running) {
    float deltaTime = getFrameTime();
    accumulator += deltaTime;

    // Fixed timestep updates
    while (accumulator >= fixedTimestep) {
        gameEngine.update(fixedTimestep);
        accumulator -= fixedTimestep;
    }

    // Calculate interpolation alpha (0.0 to 1.0)
    float alpha = accumulator / fixedTimestep;

    // Pass alpha to render
    gameEngine.render(alpha);
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

## Memory Ordering

The system uses specific memory orderings for correctness:

```cpp
// Writer (update thread) - release semantics
m_interpState.store(newState, std::memory_order_release);

// Reader (render thread) - acquire semantics
auto state = m_interpState.load(std::memory_order_acquire);
```

This ensures:
1. All writes before the store are visible to readers
2. Readers see a consistent snapshot of all related data

## Thread Safety Guarantees

| Operation | Thread Safety | Guarantee |
|-----------|---------------|-----------|
| `publishInterpolationState()` | Update thread only | Single writer |
| `getInterpolatedPosition()` | Any thread | Lock-free read |
| `setPosition()` | Update thread only | Atomic update |
| `storePositionForInterpolation()` | Update thread only | Non-atomic (internal) |

## Performance Characteristics

- **Lock-Free:** No mutex contention between update and render threads
- **Single Atomic Op:** One load in render path (not multiple competing reads)
- **Cache Friendly:** 16-byte aligned for optimal cache line usage
- **Zero Allocations:** No heap allocations in hot path

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
    publishInterpolationState();  // Previous position is stale!
}

// CORRECT: Store at start of update
void Entity::update(float deltaTime) {
    storePositionForInterpolation();  // Store current as previous
    m_position += m_velocity * deltaTime;
    publishInterpolationState();
}
```

### 2. Multiple Atomic Reads in Render

```cpp
// WRONG: Multiple atomic reads cause inconsistency
void render(float alpha) {
    float x = m_interpState.load().posX;      // Read 1
    float y = m_interpState.load().posY;      // Read 2 - may be different update!
}

// CORRECT: Single atomic read
void render(float alpha) {
    auto state = m_interpState.load();  // One read
    float x = state.posX;
    float y = state.posY;
}
```

### 3. Not Resetting on Teleport

```cpp
// WRONG: Causes sliding effect
void teleport(const Vector2D& pos) {
    m_position = pos;
    // m_previousPosition is still old location!
}

// CORRECT: Reset both
void teleport(const Vector2D& pos) {
    setPosition(pos);  // Handles both + atomic publish
}
```

## Related Documentation

- **Entity:** `include/entities/Entity.hpp`
- **Camera:** `docs/utils/Camera.md`
- **GameLoop:** `docs/core/GameLoop.md`
- **ThreadSystem:** `docs/core/ThreadSystem.md`

---

*This documentation reflects the lock-free interpolation system introduced in the world_time branch for smooth rendering at any refresh rate.*
