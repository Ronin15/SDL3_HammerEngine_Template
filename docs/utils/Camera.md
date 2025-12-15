# Camera Documentation

## Overview

The Camera class is a non-singleton 2D camera utility for world navigation and rendering in the Hammer Engine. It provides smooth target following, world bounds clamping, camera shake effects, discrete zoom levels, and flexible camera modes. The camera supports event firing for reactive gameplay and integrates seamlessly with the rendering pipeline for world-to-screen coordinate transformation.

## Architecture

### Design Principles
- **Non-Singleton**: Multiple cameras supported for split-screen or different viewports
- **Mode-Based**: Free, Follow, and Fixed modes for different behaviors
- **Event-Driven**: Optional event firing for camera state changes
- **Smooth Interpolation**: Configurable smoothing for pleasant player following
- **Zoom System**: Discrete zoom levels with event integration

### Camera Modes
```cpp
enum class Mode {
    Free,       // Camera moves freely, not following anything
    Follow,     // Camera follows a target entity with smooth interpolation
    Fixed       // Camera is fixed at a specific position
};
```

## Configuration

### Camera::Config Structure
```cpp
struct Config {
    // Following behavior
    float followSpeed{5.0f};         // Speed of camera interpolation
    float deadZoneRadius{32.0f};     // Dead zone around target (no movement)
    float maxFollowDistance{200.0f}; // Maximum distance from target
    float smoothingFactor{0.85f};    // Smoothing factor (0-1)
    bool clampToWorldBounds{true};   // Whether to clamp to world bounds

    // Zoom configuration
    std::vector<float> zoomLevels{1.0f, 1.5f, 2.0f}; // Discrete zoom levels
    int defaultZoomLevel{0};                          // Starting zoom level index

    bool isValid() const; // Validates all parameters
};
```

#### Configuration Parameters

**followSpeed** (default: 5.0f)
- Controls how quickly camera catches up to target
- Higher values = faster, more responsive
- Lower values = slower, more cinematic
- Range: > 0.0

**deadZoneRadius** (default: 32.0f)
- Radius around target where camera doesn't move
- Prevents micro-oscillations
- Smaller = tighter following
- Larger = looser following
- Range: ≥ 0.0

**maxFollowDistance** (default: 200.0f)
- Maximum allowed distance between camera and target
- Camera will snap closer if exceeds this distance
- Range: > 0.0

**smoothingFactor** (default: 0.85f)
- Controls exponential smoothing interpolation
- 0.0 = instant snap (no smoothing)
- 1.0 = no movement (infinite smoothing)
- Recommended: 0.7-0.9 for smooth following
- Range: 0.0-1.0

**zoomLevels** (default: {1.0f, 1.5f, 2.0f})
- Array of discrete zoom levels
- 1.0 = native resolution
- >1.0 = zoomed in (objects appear larger)
- <1.0 = zoomed out (objects appear smaller)
- All values must be > 0.0

**defaultZoomLevel** (default: 0)
- Starting zoom level index
- Must be valid index into zoomLevels array

### Camera::Bounds Structure
```cpp
struct Bounds {
    float minX{0.0f};
    float minY{0.0f};
    float maxX{1000.0f};
    float maxY{1000.0f};

    bool isValid() const; // Validates maxX > minX && maxY > minY
};
```

### Camera::Viewport Structure
```cpp
struct Viewport {
    float width{1920.0f};
    float height{1080.0f};

    bool isValid() const; // Validates width > 0 && height > 0

    // Convenience methods
    float halfWidth() const { return width * 0.5f; }
    float halfHeight() const { return height * 0.5f; }
};
```

## API Reference

### Construction

#### `Camera()`
Default constructor with default configuration.
```cpp
Camera camera; // 1920x1080 viewport, free mode, zoom 1.0
```

#### `Camera(const Config& config)`
Constructor with custom configuration.
```cpp
Camera::Config config;
config.followSpeed = 3.0f;
config.zoomLevels = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f};
config.defaultZoomLevel = 1; // Start at 1.0x zoom

Camera camera(config);
```

#### `Camera(float x, float y, float viewportWidth, float viewportHeight)`
Constructor with initial position and viewport.
```cpp
Camera camera(960.0f, 540.0f, 1920.0f, 1080.0f);
```

### Update and Positioning

#### `void update(float deltaTime)`
Updates camera position based on mode and target.
- **Follow Mode**: Interpolates towards target with smoothing
- **Free/Fixed Mode**: No automatic movement
- **Shake**: Updates shake offset
- **Bounds Clamping**: Clamps to world bounds if enabled

```cpp
void GameState::update(float deltaTime) {
    m_camera.update(deltaTime);
}
```

#### `void setPosition(float x, float y)` / `setPosition(const Vector2D& position)`
Sets camera position directly (no interpolation).
```cpp
camera.setPosition(500.0f, 300.0f);
camera.setPosition(Vector2D(500.0f, 300.0f));
```

#### `const Vector2D& getPosition() const`
Gets current camera position (center of viewport).
```cpp
Vector2D pos = camera.getPosition();
float x = camera.getX(); // Convenience getter
float y = camera.getY(); // Convenience getter
```

### Viewport Management

#### `void setViewport(float width, float height)` / `setViewport(const Viewport& viewport)`
Sets viewport dimensions.
```cpp
camera.setViewport(1920.0f, 1080.0f);
camera.setViewport(Camera::Viewport{1920.0f, 1080.0f});
```

#### `const Viewport& getViewport() const`
Gets current viewport.
```cpp
const Camera::Viewport& viewport = camera.getViewport();
float width = viewport.width;
```

#### `void syncViewportWithEngine()`
Synchronizes viewport with GameEngine logical size (for window resize handling).
- **Thread Safety**: Safe to call every frame
- **Performance**: Only updates if dimensions changed
- **Use Case**: Call in game state `update()` to handle window resizes

```cpp
void GameState::update(float deltaTime) {
    m_camera.syncViewportWithEngine(); // Auto-sync with window size
    m_camera.update(deltaTime);
}
```

### World Bounds

#### `void setWorldBounds(float minX, float minY, float maxX, float maxY)` / `setWorldBounds(const Bounds& bounds)`
Sets world boundaries for camera clamping.
```cpp
camera.setWorldBounds(0.0f, 0.0f, 3200.0f, 3200.0f); // 200x200 tile world (16px tiles)
camera.setWorldBounds(Camera::Bounds{0.0f, 0.0f, 3200.0f, 3200.0f});
```

#### `const Bounds& getWorldBounds() const`
Gets current world bounds.

### Camera Modes

#### `void setMode(Mode mode)`
Sets camera mode.
```cpp
camera.setMode(Camera::Mode::Follow); // Follow target entity
camera.setMode(Camera::Mode::Free);   // Free movement
camera.setMode(Camera::Mode::Fixed);  // Fixed position
```

#### `Mode getMode() const`
Gets current camera mode.

### Target Following

#### `void setTarget(std::weak_ptr<Entity> target)`
Sets target entity for Follow mode.
```cpp
auto player = std::make_shared<Entity>();
camera.setTarget(player); // Weak pointer, safe if entity destroyed
camera.setMode(Camera::Mode::Follow);
```

#### `void setTargetPositionGetter(std::function<Vector2D()> positionGetter)`
Sets target using a position getter function (alternative to entity).
```cpp
camera.setTargetPositionGetter([]() {
    return Vector2D(player.x, player.y);
});
```

#### `void clearTarget()`
Clears current target.

#### `bool hasTarget() const`
Checks if camera has a valid target.

#### `void snapToTarget()`
Immediately snaps camera to target position (no interpolation).
```cpp
camera.setTarget(player);
camera.setMode(Camera::Mode::Follow);
camera.snapToTarget(); // Instant snap, no smooth transition
```

### Zoom System

#### `void zoomIn()`
Zooms in to the next zoom level (makes objects appear larger).
- Stops at maximum configured zoom level
- Triggers `CameraZoomChanged` event if enabled

```cpp
// User presses zoom in key
if (inputManager.wasKeyPressed(SDL_SCANCODE_EQUALS)) {
    camera.zoomIn(); // 1.0 → 1.5 → 2.0 (stops at max)
}
```

#### `void zoomOut()`
Zooms out to the previous zoom level (makes objects appear smaller).
- Stops at minimum configured zoom level
- Triggers `CameraZoomChanged` event if enabled

```cpp
// User presses zoom out key
if (inputManager.wasKeyPressed(SDL_SCANCODE_MINUS)) {
    camera.zoomOut(); // 2.0 → 1.5 → 1.0 (stops at min)
}
```

#### `bool setZoomLevel(int levelIndex)`
Sets zoom to a specific level index.
- **Returns**: `true` if level was valid and set, `false` otherwise
- **Range**: 0 to `getNumZoomLevels() - 1`

```cpp
camera.setZoomLevel(0); // Reset to first zoom level (typically 1.0x)
camera.setZoomLevel(2); // Jump to third zoom level
```

#### `float getZoom() const`
Gets current zoom scale factor.
```cpp
float zoom = camera.getZoom(); // 1.0, 1.5, 2.0, etc.
```

#### `int getZoomLevel() const`
Gets current zoom level index.
```cpp
int level = camera.getZoomLevel(); // 0, 1, 2, etc.
```

#### `int getNumZoomLevels() const`
Gets number of configured zoom levels.
```cpp
int count = camera.getNumZoomLevels(); // 3 for {1.0, 1.5, 2.0}
```

### Coordinate Transformation

#### `void worldToScreen(float worldX, float worldY, float& screenX, float& screenY) const`
Transforms world coordinates to screen coordinates.
```cpp
float screenX, screenY;
camera.worldToScreen(entityX, entityY, screenX, screenY);
// Render entity at (screenX, screenY)
```

#### `Vector2D worldToScreen(const Vector2D& worldCoords) const`
Convenience overload returning `Vector2D`.
```cpp
Vector2D screenPos = camera.worldToScreen(entityPos);
```

#### `void screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const`
Transforms screen coordinates to world coordinates (mouse clicks).
```cpp
float worldX, worldY;
camera.screenToWorld(mouseX, mouseY, worldX, worldY);
// Place building at (worldX, worldY)
```

#### `Vector2D screenToWorld(const Vector2D& screenCoords) const`
Convenience overload returning `Vector2D`.
```cpp
Vector2D worldPos = camera.screenToWorld(mousePos);
```

### Visibility Culling

#### `Camera::ViewRect getViewRect() const`
Gets the visible world rectangle (accounting for zoom).
```cpp
struct ViewRect {
    float x, y, width, height; // Top-left corner + dimensions

    // Convenience methods
    float left() const { return x; }
    float right() const { return x + width; }
    float top() const { return y; }
    float bottom() const { return y + height; }
    float centerX() const { return x + width * 0.5f; }
    float centerY() const { return y + height * 0.5f; }
};

ViewRect view = camera.getViewRect();
// At 2x zoom: width/height are half the viewport size (you see less world)
```

#### `bool isPointVisible(float x, float y) const` / `isPointVisible(const Vector2D& point) const`
Checks if a point is visible in the camera view.
```cpp
if (camera.isPointVisible(entity.x, entity.y)) {
    renderEntity(entity);
}
```

#### `bool isRectVisible(float x, float y, float width, float height) const`
Checks if a rectangle intersects with the camera view (for AABB culling).
```cpp
if (camera.isRectVisible(entity.x, entity.y, 32.0f, 32.0f)) {
    renderEntity(entity); // Only render if visible
}
```

### Camera Shake

#### `void shake(float duration, float intensity)`
Shakes the camera for impact effects.
- **duration**: Shake duration in seconds
- **intensity**: Shake intensity in pixels

```cpp
// Explosion shake
camera.shake(0.5f, 10.0f); // 0.5s shake with 10px intensity

// Damage shake
camera.shake(0.2f, 5.0f); // Brief shake on player damage
```

#### `bool isShaking() const`
Checks if camera is currently shaking.

### Event System Integration

#### `void setEventFiringEnabled(bool enabled)`
Enables or disables event firing for camera state changes.
```cpp
camera.setEventFiringEnabled(true); // Enable events
```

#### `bool isEventFiringEnabled() const`
Checks if event firing is enabled.

#### Events Fired
When event firing is enabled, camera fires these events:
- `CameraPositionChanged` - Camera moved
- `CameraModeChanged` - Camera mode changed
- `CameraTargetChanged` - Camera target changed
- `CameraShakeStarted` - Camera shake started
- `CameraShakeEnded` - Camera shake ended
- `CameraZoomChanged` - Camera zoom level changed

## Practical Examples

### Example 1: Player Following Camera
```cpp
class GamePlayState : public GameState {
private:
    Camera m_camera;
    std::shared_ptr<Entity> m_player;

public:
    bool enter() override {
        // Create player
        m_player = std::make_shared<Entity>();
        m_player->setPosition(1600.0f, 1600.0f); // Center of 200x200 tile world

        // Configure camera for smooth following
        Camera::Config config;
        config.followSpeed = 4.0f;
        config.smoothingFactor = 0.85f;
        config.deadZoneRadius = 16.0f;
        config.zoomLevels = {1.0f, 1.5f, 2.0f, 2.5f};
        config.defaultZoomLevel = 0; // Start at 1.0x
        m_camera.setConfig(config);

        // Set world bounds (200x200 tiles * 16px = 3200x3200)
        m_camera.setWorldBounds(0.0f, 0.0f, 3200.0f, 3200.0f);

        // Set viewport to match window
        m_camera.setViewport(1920.0f, 1080.0f);

        // Follow player
        m_camera.setTarget(m_player);
        m_camera.setMode(Camera::Mode::Follow);
        m_camera.snapToTarget(); // Start centered on player

        return true;
    }

    void update(float deltaTime) override {
        // Sync viewport with window (handles resize)
        m_camera.syncViewportWithEngine();

        // Update camera (smooth follow)
        m_camera.update(deltaTime);

        // Handle zoom input
        const auto& input = InputManager::Instance();
        if (input.wasKeyPressed(SDL_SCANCODE_EQUALS)) {
            m_camera.zoomIn();
        }
        if (input.wasKeyPressed(SDL_SCANCODE_MINUS)) {
            m_camera.zoomOut();
        }
    }

    void render() override {
        // Get visible world area for culling
        Camera::ViewRect view = m_camera.getViewRect();

        // Render only visible entities
        for (const auto& entity : m_entities) {
            if (m_camera.isPointVisible(entity->getPosition())) {
                Vector2D screenPos = m_camera.worldToScreen(entity->getPosition());
                entity->render(screenPos.x, screenPos.y);
            }
        }
    }
};
```

### Example 2: Strategy Game Camera (Free Movement)
```cpp
class StrategyState : public GameState {
private:
    Camera m_camera;
    float m_cameraMoveSpeed = 500.0f;

public:
    bool enter() override {
        m_camera.setMode(Camera::Mode::Free);
        m_camera.setWorldBounds(0.0f, 0.0f, 6400.0f, 6400.0f); // Large map
        m_camera.setPosition(3200.0f, 3200.0f); // Start at center
        return true;
    }

    void update(float deltaTime) override {
        m_camera.syncViewportWithEngine();

        // WASD camera movement
        const auto& input = InputManager::Instance();
        Vector2D movement(0.0f, 0.0f);

        if (input.isKeyDown(SDL_SCANCODE_W)) movement.y -= 1.0f;
        if (input.isKeyDown(SDL_SCANCODE_S)) movement.y += 1.0f;
        if (input.isKeyDown(SDL_SCANCODE_A)) movement.x -= 1.0f;
        if (input.isKeyDown(SDL_SCANCODE_D)) movement.x += 1.0f;

        if (movement.lengthSquared() > 0.0f) {
            movement.normalize();
            Vector2D newPos = m_camera.getPosition() + movement * m_cameraMoveSpeed * deltaTime;
            m_camera.setPosition(newPos);
        }

        m_camera.update(deltaTime);
    }

    void handleMouseClick(float screenX, float screenY) {
        // Convert mouse click to world position
        Vector2D worldPos = m_camera.screenToWorld(Vector2D(screenX, screenY));

        // Place building at world position
        placeBuildingAt(worldPos.x, worldPos.y);
    }
};
```

### Example 3: Zoom-Responsive UI
```cpp
class GameState {
private:
    Camera m_camera;
    bool m_showZoomIndicator = false;
    float m_zoomIndicatorTimer = 0.0f;

public:
    void update(float deltaTime) override {
        const auto& input = InputManager::Instance();

        // Zoom controls with UI feedback
        if (input.wasKeyPressed(SDL_SCANCODE_EQUALS)) {
            m_camera.zoomIn();
            m_showZoomIndicator = true;
            m_zoomIndicatorTimer = 2.0f; // Show for 2 seconds
        }
        if (input.wasKeyPressed(SDL_SCANCODE_MINUS)) {
            m_camera.zoomOut();
            m_showZoomIndicator = true;
            m_zoomIndicatorTimer = 2.0f;
        }

        // Update zoom indicator timer
        if (m_zoomIndicatorTimer > 0.0f) {
            m_zoomIndicatorTimer -= deltaTime;
            if (m_zoomIndicatorTimer <= 0.0f) {
                m_showZoomIndicator = false;
            }
        }

        m_camera.update(deltaTime);
    }

    void render() override {
        // Render world
        renderWorld();

        // Render zoom indicator UI
        if (m_showZoomIndicator) {
            float zoom = m_camera.getZoom();
            int level = m_camera.getZoomLevel();
            int maxLevel = m_camera.getNumZoomLevels() - 1;

            // Render zoom UI (e.g., "Zoom: 1.5x (2/3)")
            renderZoomUI(zoom, level, maxLevel);
        }
    }
};
```

### Example 4: Event-Driven Camera (Listen to Zoom Changes)
```cpp
class GameState {
private:
    Camera m_camera;
    size_t m_zoomEventHandlerId;

public:
    bool enter() override {
        // Enable camera events
        m_camera.setEventFiringEnabled(true);

        // Subscribe to zoom changed events
        m_zoomEventHandlerId = EventManager::Instance().subscribe<CameraZoomChanged>(
            [this](const CameraZoomChanged& event) {
                float oldZoom = event.oldZoom;
                float newZoom = event.newZoom;

                LOGGER_INFO("Zoom changed: " + std::to_string(oldZoom) + " → " +
                           std::to_string(newZoom));

                // Update UI scale
                updateUIScale(newZoom);

                // Adjust minimap zoom
                updateMinimapZoom(newZoom);
            }
        );

        return true;
    }

    bool exit() override {
        // Unsubscribe from events
        EventManager::Instance().unsubscribe<CameraZoomChanged>(m_zoomEventHandlerId);
        return true;
    }

    void updateUIScale(float zoom) {
        // Scale UI elements inversely to keep them constant screen size
        float uiScale = 1.0f / zoom;
        // Apply to UI elements...
    }
};
```

## Performance Considerations

### Zoom and Viewport Calculations
- **Zoom affects view rectangle**: Higher zoom = smaller visible world area
- **Formula**: `worldViewWidth = viewportWidth / zoom`
- **At 2x zoom**: You see half as much world (400x300 instead of 800x600)

### Visibility Culling
```cpp
// Efficient culling pattern
Camera::ViewRect view = camera.getViewRect();

for (const auto& entity : m_entities) {
    // Early-exit: Check bounding box first
    if (!camera.isRectVisible(entity.x, entity.y, entity.width, entity.height)) {
        continue; // Skip rendering
    }

    // Transform to screen space and render
    Vector2D screenPos = camera.worldToScreen(entity.getPosition());
    entity.render(screenPos);
}
```

### Best Practices
1. **Call syncViewportWithEngine() once per frame** - Handles window resize automatically
2. **Use discrete zoom levels** - Easier for players to understand than continuous zoom
3. **Cache ViewRect** - If querying multiple times per frame
4. **Cull before transforming** - Check visibility before worldToScreen conversion
5. **Smooth following** - Use 0.7-0.9 smoothingFactor for pleasant following

### Common Pitfalls
```cpp
// BAD: Computing view rect every entity
for (const auto& entity : m_entities) {
    Camera::ViewRect view = camera.getViewRect(); // Recomputing!
    if (entity.x >= view.left() && entity.x <= view.right()) {
        render(entity);
    }
}

// GOOD: Cache view rect
Camera::ViewRect view = camera.getViewRect();
for (const auto& entity : m_entities) {
    if (entity.x >= view.left() && entity.x <= view.right()) {
        render(entity);
    }
}
```

## Thread-Safe Interpolation

The Camera implements lock-free atomic interpolation for smooth rendering at any display refresh rate, regardless of the fixed timestep update rate.

### Why Interpolation?

Without interpolation, the camera "stutters" because:
- Game logic updates at fixed rate (e.g., 60 Hz)
- Display may render at different rate (e.g., 144 Hz)
- Camera jumps to discrete positions each update

### Atomic Interpolation State

The camera uses a 16-byte aligned atomic struct for thread-safe position sharing:

```cpp
struct alignas(16) InterpolationState {
    float posX{0.0f}, posY{0.0f};
    float prevPosX{0.0f}, prevPosY{0.0f};
};
std::atomic<InterpolationState> m_interpState{};
```

**Why 16 bytes?**
- Lock-free on x86-64 (CMPXCHG16B instruction)
- Lock-free on ARM64 (LDXP/STXP instruction pair)
- All four floats read/written atomically as one unit

### Using getRenderOffset()

The `getRenderOffset()` method provides interpolated camera position for rendering:

```cpp
Vector2D getRenderOffset(float& offsetX, float& offsetY, float interpolationAlpha) const;
```

**Parameters:**
- `offsetX`, `offsetY`: Output camera offset for rendering (top-left of view)
- `interpolationAlpha`: Blend factor from GameLoop (0.0 = previous, 1.0 = current)

**Returns:** Interpolated center position

### Integration Pattern

```cpp
// In GameState::render()
void GamePlayState::render(float interpolationAlpha) {
    // Get interpolated camera offset (single atomic read internally)
    float cameraX, cameraY;
    m_camera->getRenderOffset(cameraX, cameraY, interpolationAlpha);

    // Use for entity rendering
    for (auto& entity : m_entities) {
        entity->render(renderer, cameraX, cameraY, interpolationAlpha);
    }

    // Use for world rendering
    WorldManager::Instance().render(renderer, cameraX, cameraY, viewW, viewH);
}
```

### Thread Safety Guarantees

| Operation | Thread | Guarantee |
|-----------|--------|-----------|
| `update()` | Update thread | Publishes new state atomically |
| `getRenderOffset()` | Render thread | Lock-free atomic read |
| `setPosition()` | Update thread | Resets interpolation state |

### Avoiding Jitter

**Problem:** Multiple atomic reads in render path cause inconsistent values:

```cpp
// WRONG: Two atomic reads may see different updates
float x = camera.getX();  // Read 1
float y = camera.getY();  // Read 2 - may be from different update!
```

**Solution:** Single atomic read through `getRenderOffset()`:

```cpp
// CORRECT: One atomic read, consistent values
float cameraX, cameraY;
camera.getRenderOffset(cameraX, cameraY, alpha);
```

### Performance

- **Lock-free**: No mutex contention between threads
- **Single atomic operation**: One load per frame in render path
- **Cache friendly**: 16-byte aligned for optimal access

For more details on the interpolation system, see `docs/architecture/InterpolationSystem.md`.

## Coordinate System

### World Space
- **Origin**: Top-left of world (0, 0)
- **Units**: Pixels
- **Range**: 0 to worldBounds.max

### Screen Space
- **Origin**: Top-left of window (0, 0)
- **Units**: Pixels
- **Range**: 0 to viewport.width/height

### Camera Position
- **Meaning**: Center of the viewport in world space
- **Example**: Camera at (1000, 500) with 800x600 viewport sees world rect (600, 200, 800, 600)

## Integration with Event System

### CameraZoomChanged Event
Defined in `events/CameraEvent.hpp`:
```cpp
struct CameraZoomChanged {
    float oldZoom;
    float newZoom;
};
```

**When Fired**:
- `zoomIn()` called and zoom level increased
- `zoomOut()` called and zoom level decreased
- `setZoomLevel()` called with different level

**Use Cases**:
- Update UI scale inversely to keep constant screen size
- Adjust minimap zoom
- Trigger zoom-dependent visual effects
- Update HUD elements

## Migration Notes

### From Previous Version (Without Zoom)
```cpp
// Old code (no zoom)
Camera camera;
Camera::ViewRect view = camera.getViewRect();
// View rect is always viewport size

// New code (with zoom)
Camera camera;
camera.zoomIn(); // Zoom to 1.5x
Camera::ViewRect view = camera.getViewRect();
// View rect is now viewport size / zoom (smaller visible area)
```

### Adding Zoom to Existing Game States
```cpp
// 1. Enable zoom input handling
if (inputManager.wasKeyPressed(SDL_SCANCODE_EQUALS)) {
    camera.zoomIn();
}
if (inputManager.wasKeyPressed(SDL_SCANCODE_MINUS)) {
    camera.zoomOut();
}

// 2. Configure zoom levels in Config
Camera::Config config;
config.zoomLevels = {0.75f, 1.0f, 1.5f, 2.0f}; // 4 levels
config.defaultZoomLevel = 1; // Start at 1.0x
camera.setConfig(config);

// 3. No other changes needed - culling and transforms already zoom-aware
```

## See Also

- [EventManager Documentation](../events/EventManager.md) - Camera event integration
- [GameEngine Documentation](../core/GameEngine.md) - Viewport synchronization
- [InputManager Documentation](../managers/InputManager.md) - Mouse coordinate transformation
- [WorldManager Documentation](../managers/WorldManager.md) - World bounds integration
