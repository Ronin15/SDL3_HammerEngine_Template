/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "utils/Vector2D.hpp"
#include <algorithm>
#include <memory>
#include <functional>
#include <cstdint>
#include <random>
#include <atomic>

// Forward declarations
class Entity;
struct SDL_Renderer;

namespace HammerEngine {

/**
 * @brief Camera utility class for 2D world navigation and rendering
 * 
 * This camera follows industry best practices:
 * - Non-singleton design for flexibility
 * - Smooth interpolation for player following
 * - World bounds clamping
 * - Modular and testable architecture
 * - Support for different camera modes
 */
class Camera {
public:
    /**
     * @brief Camera modes for different behaviors
     */
    enum class Mode {
        Free,       // Camera moves freely, not following anything
        Follow,     // Camera follows a target entity with smooth interpolation
        Fixed       // Camera is fixed at a specific position
    };

    /**
     * @brief Camera configuration structure
     */
    struct Config {
        float smoothTime{0.15f};        // Time to reach target (seconds) - lower = snappier
        float deadZoneRadius{0.0f};     // Dead zone around target (no movement if target within this)
        float maxSpeed{1000.0f};        // Maximum camera speed (pixels/second)
        bool clampToWorldBounds{true};  // Whether to clamp camera to world bounds

        // Zoom configuration
        std::vector<float> zoomLevels{1.0f, 1.5f, 2.0f};  // Discrete zoom levels
        int defaultZoomLevel{0};                           // Starting zoom level index

        // Validation
        bool isValid() const {
            if (smoothTime <= 0.0f || deadZoneRadius < 0.0f || maxSpeed <= 0.0f) {
                return false;
            }
            if (zoomLevels.empty()) {
                return false;
            }
            if (!std::all_of(zoomLevels.begin(), zoomLevels.end(),
                             [](float zoom) { return zoom > 0.0f; })) {
                return false;
            }
            if (defaultZoomLevel < 0 || defaultZoomLevel >= static_cast<int>(zoomLevels.size())) {
                return false;
            }
            return true;
        }
    };

    /**
     * @brief Camera bounds structure for world clamping
     */
    struct Bounds {
        float minX{0.0f};
        float minY{0.0f}; 
        float maxX{1000.0f};
        float maxY{1000.0f};
        
        bool isValid() const {
            return maxX > minX && maxY > minY;
        }
    };

    /**
     * @brief Viewport structure for rendering calculations
     */
    struct Viewport {
        float width{1920.0f};
        float height{1080.0f};
        
        bool isValid() const {
            return width > 0.0f && height > 0.0f;
        }
        
        // Convenience methods
        float halfWidth() const { return width * 0.5f; }
        float halfHeight() const { return height * 0.5f; }
    };

public:
    /**
     * @brief Constructor with default configuration
     */
    Camera();
    
    /**
     * @brief Constructor with custom configuration
     * @param config Camera configuration
     */
    explicit Camera(const Config& config);
    
    /**
     * @brief Constructor with position and viewport
     * @param x Initial camera X position
     * @param y Initial camera Y position
     * @param viewportWidth Viewport width
     * @param viewportHeight Viewport height
     */
    Camera(float x, float y, float viewportWidth, float viewportHeight);
    
    /**
     * @brief Default destructor
     */
    ~Camera() = default;
    
    // Copy and move semantics
    Camera(const Camera&) = default;
    Camera& operator=(const Camera&) = default;
    Camera(Camera&&) = default;
    Camera& operator=(Camera&&) = default;

    /**
     * @brief Updates the camera position based on mode and target
     * @param deltaTime Time elapsed since last update in seconds
     */
    void update(float deltaTime);
    
    /**
     * @brief Sets the camera position directly
     * @param x X position
     * @param y Y position
     */
    void setPosition(float x, float y);
    
    /**
     * @brief Sets the camera position using Vector2D
     * @param position New position
     */
    void setPosition(const Vector2D& position);
    
    /**
     * @brief Gets the current camera position
     * @return Current position as Vector2D
     */
    const Vector2D& getPosition() const { return m_position; }
    
    /**
     * @brief Gets camera X position (float precision for smooth entity positioning)
     * @return X coordinate
     */
    float getX() const { return m_position.getX(); }

    /**
     * @brief Gets camera Y position (float precision for smooth entity positioning)
     * @return Y coordinate
     */
    float getY() const { return m_position.getY(); }

    /**
     * @brief Sets the viewport size
     * @param width Viewport width
     * @param height Viewport height
     */
    void setViewport(float width, float height);
    
    /**
     * @brief Sets the viewport using Viewport structure
     * @param viewport New viewport
     */
    void setViewport(const Viewport& viewport);
    
    /**
     * @brief Gets the current viewport
     * @return Current viewport
     */
    const Viewport& getViewport() const { return m_viewport; }
    
    /**
     * @brief Sets the world bounds for camera clamping
     * @param minX Minimum X coordinate
     * @param minY Minimum Y coordinate
     * @param maxX Maximum X coordinate
     * @param maxY Maximum Y coordinate
     */
    void setWorldBounds(float minX, float minY, float maxX, float maxY);
    
    /**
     * @brief Sets the world bounds using Bounds structure
     * @param bounds New world bounds
     */
    void setWorldBounds(const Bounds& bounds);
    
    /**
     * @brief Gets the current world bounds
     * @return Current world bounds
     */
    const Bounds& getWorldBounds() const { return m_worldBounds; }
    
    /**
     * @brief Sets the camera mode
     * @param mode New camera mode
     */
    void setMode(Mode mode);
    
    /**
     * @brief Gets the current camera mode
     * @return Current mode
     */
    Mode getMode() const { return m_mode; }
    
    /**
     * @brief Sets the target entity for following mode
     * @param target Weak pointer to target entity
     */
    void setTarget(std::weak_ptr<Entity> target);
    
    /**
     * @brief Sets target using a function that returns position
     * @param positionGetter Function that returns target position
     */
    void setTargetPositionGetter(std::function<Vector2D()> positionGetter);
    
    /**
     * @brief Clears the current target
     */
    void clearTarget();
    
    /**
     * @brief Gets whether camera has a valid target
     * @return True if target is valid, false otherwise
     */
    bool hasTarget() const;
    
    /**
     * @brief Updates camera configuration
     * @param config New configuration
     * @return True if configuration is valid and applied
     */
    bool setConfig(const Config& config);
    
    /**
     * @brief Gets current camera configuration
     * @return Current configuration
     */
    const Config& getConfig() const { return m_config; }
    
    /**
     * @brief Gets the view rectangle for rendering calculations
     * @return View rectangle with top-left corner and dimensions
     */
    struct ViewRect {
        float x, y, width, height;

        // Convenience methods
        float left() const { return x; }
        float right() const { return x + width; }
        float top() const { return y; }
        float bottom() const { return y + height; }
        float centerX() const { return x + width * 0.5f; }
        float centerY() const { return y + height * 0.5f; }
    };

    /**
     * @brief Gets the current view rectangle
     * @return View rectangle for culling and rendering
     */
    ViewRect getViewRect() const;

    /**
     * @brief Gets the pixel-snapped render offset for this frame
     *
     * Returns the authoritative camera offset that ALL rendering operations
     * should use (tiles, entities, particles). Using this single cached value
     * prevents 1-pixel drift between different rendered elements.
     *
     * The offset is computed once per frame (on first call) and cached.
     *
     * @param offsetX Output: pixel-snapped camera X offset (top-left)
     * @param offsetY Output: pixel-snapped camera Y offset (top-left)
     */
    void getRenderOffset(float& offsetX, float& offsetY, float interpolationAlpha = 1.0f) const;


    /**
     * @brief Checks if a point is visible in the camera view
     * @param x Point X coordinate
     * @param y Point Y coordinate
     * @return True if point is visible
     */
    bool isPointVisible(float x, float y) const;
    
    /**
     * @brief Checks if a point is visible in the camera view
     * @param point Point to check
     * @return True if point is visible
     */
    bool isPointVisible(const Vector2D& point) const;
    
    /**
     * @brief Checks if a rectangle intersects with the camera view
     * @param x Rectangle X coordinate
     * @param y Rectangle Y coordinate  
     * @param width Rectangle width
     * @param height Rectangle height
     * @return True if rectangle is visible
     */
    bool isRectVisible(float x, float y, float width, float height) const;
    
    /**
     * @brief Transforms world coordinates to screen coordinates
     * @param worldX World X coordinate
     * @param worldY World Y coordinate
     * @param screenX Output screen X coordinate
     * @param screenY Output screen Y coordinate
     */
    void worldToScreen(float worldX, float worldY, float& screenX, float& screenY) const;
    
    /**
     * @brief Transforms screen coordinates to world coordinates
     * @param screenX Screen X coordinate
     * @param screenY Screen Y coordinate
     * @param worldX Output world X coordinate
     * @param worldY Output world Y coordinate
     */
     void screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const;
     Vector2D screenToWorld(const Vector2D& screenCoords) const;
     Vector2D worldToScreen(const Vector2D& worldCoords) const;
    
    /**
     * @brief Immediately snaps camera to target position (no interpolation)
     */
    void snapToTarget();
    
    /**
     * @brief Shakes the camera for a given duration and intensity
     * @param duration Duration of shake in seconds
     * @param intensity Shake intensity (pixels)
     */
    void shake(float duration, float intensity);
    
    /**
     * @brief Gets whether camera is currently shaking
     * @return True if shaking
     */
    bool isShaking() const { return m_shakeTimeRemaining > 0.0f; }
    
    /**
     * @brief Enables or disables event firing for camera state changes
     * @param enabled Whether to fire events
     */
    void setEventFiringEnabled(bool enabled) { m_eventFiringEnabled = enabled; }
    
    /**
     * @brief Gets whether event firing is enabled
     * @return True if events are fired on state changes
     */
    bool isEventFiringEnabled() const { return m_eventFiringEnabled; }

    /**
     * @brief Zoom in to the next zoom level (make objects larger)
     * Cycles through configured zoom levels (stops at max)
     */
    void zoomIn();

    /**
     * @brief Zoom out to the previous zoom level (make objects smaller)
     * Cycles through configured zoom levels (stops at min)
     */
    void zoomOut();

    /**
     * @brief Set zoom to a specific level index
     * @param levelIndex Index into configured zoomLevels array
     * @return True if level was valid and set
     */
    bool setZoomLevel(int levelIndex);

    /**
     * @brief Get current zoom scale factor
     * @return Current zoom level (from configured zoomLevels)
     */
    float getZoom() const { return m_zoom; }

    /**
     * @brief Get current zoom level index
     * @return Index into configured zoomLevels array
     */
    int getZoomLevel() const { return m_currentZoomIndex; }

    /**
     * @brief Get number of configured zoom levels
     * @return Number of zoom levels
     */
    int getNumZoomLevels() const { return static_cast<int>(m_config.zoomLevels.size()); }

    /**
     * @brief Synchronize viewport dimensions with GameEngine logical size
     *
     * Automatically updates the camera viewport to match the current logical
     * resolution from GameEngine. Call this in game state update() methods to
     * keep the camera viewport in sync with window resize events.
     *
     * This method is safe to call every frame as it only updates if dimensions changed.
     */
    void syncViewportWithEngine();

private:
    // Core camera state
    Vector2D m_position{960.0f, 540.0f};    // Current camera position (center of 1920x1080)
    Vector2D m_targetPosition{960.0f, 540.0f}; // Target position for interpolation
    Viewport m_viewport{1920.0f, 1080.0f};    // Camera viewport size
    Bounds m_worldBounds{0.0f, 0.0f, 1920.0f, 1080.0f}; // World boundaries
    Config m_config{};                       // Camera configuration
    Mode m_mode{Mode::Free};                // Current camera mode
    
    // Target tracking
    std::weak_ptr<Entity> m_target;         // Target entity to follow
    std::function<Vector2D()> m_positionGetter; // Alternative position getter
    
    // Camera shake
    float m_shakeTimeRemaining{0.0f};       // Remaining shake time
    float m_shakeIntensity{0.0f};           // Current shake intensity
    Vector2D m_shakeOffset{0.0f, 0.0f};     // Current shake offset
    
    // Event firing
    bool m_eventFiringEnabled{false};      // Whether to fire events on state changes
    
    // World sync (auto-correct camera bounds when world changes)
    bool m_autoSyncWorldBounds{true};
    uint64_t m_lastWorldVersion{0};

    // Zoom state
    float m_zoom{1.0f};              // Current zoom level (1.0 = native)
    int m_currentZoomIndex{0};       // Index into ZOOM_LEVELS array

    // Smooth follow velocity (for critically damped spring algorithm)
    Vector2D m_velocity{0.0f, 0.0f}; // Current camera velocity for smooth damping

    // Previous position for render interpolation (smooth camera at any refresh rate)
    Vector2D m_previousPosition{960.0f, 540.0f};

    // Thread-safe interpolation state for render thread access
    // 16-byte atomic is lock-free on x86-64 (CMPXCHG16B) and ARM64 (LDXP/STXP)
    struct alignas(16) InterpolationState {
        float posX{0.0f}, posY{0.0f};
        float prevPosX{0.0f}, prevPosY{0.0f};
    };
    std::atomic<InterpolationState> m_interpState{};

    // Shake random number generation (mutable for const generateShakeOffset)
    // Per CLAUDE.md: NEVER use static vars in threaded code - use member vars instead
    mutable std::mt19937 m_shakeRng{std::random_device{}()};
    mutable std::uniform_real_distribution<float> m_shakeDist{-1.0f, 1.0f};

    // Internal helper methods
    void clampToWorldBounds();
    Vector2D getTargetPosition() const;
    Vector2D generateShakeOffset() const;
    
    // Event firing helpers
    void firePositionChangedEvent(const Vector2D& oldPosition, const Vector2D& newPosition);
    void fireModeChangedEvent(Mode oldMode, Mode newMode);
    void fireTargetChangedEvent(std::weak_ptr<Entity> oldTarget, std::weak_ptr<Entity> newTarget);
    void fireShakeStartedEvent(float duration, float intensity);
    void fireShakeEndedEvent();
    void fireZoomChangedEvent(float oldZoom, float newZoom);
};

} // namespace HammerEngine

#endif // CAMERA_HPP
