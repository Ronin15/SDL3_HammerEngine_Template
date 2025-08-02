/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CAMERA_HPP
#define CAMERA_HPP

#include "utils/Vector2D.hpp"
#include <memory>
#include <functional>

// Forward declarations
class Entity;

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
        float followSpeed{5.0f};        // Speed of camera interpolation when following
        float deadZoneRadius{32.0f};    // Dead zone around target (no movement if target within this)
        float maxFollowDistance{200.0f}; // Maximum distance camera can be from target
        float smoothingFactor{0.85f};   // Smoothing factor for interpolation (0-1)
        bool clampToWorldBounds{true};  // Whether to clamp camera to world bounds
        
        // Validation
        bool isValid() const {
            return followSpeed > 0.0f && 
                   deadZoneRadius >= 0.0f && 
                   maxFollowDistance > 0.0f &&
                   smoothingFactor >= 0.0f && smoothingFactor <= 1.0f;
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
        float width{800.0f};
        float height{600.0f};
        
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
     * @brief Gets camera X position
     * @return X coordinate
     */
    float getX() const { return m_position.getX(); }
    
    /**
     * @brief Gets camera Y position  
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

private:
    // Core camera state
    Vector2D m_position{400.0f, 300.0f};    // Current camera position
    Vector2D m_targetPosition{400.0f, 300.0f}; // Target position for interpolation
    Viewport m_viewport{800.0f, 600.0f};    // Camera viewport size
    Bounds m_worldBounds{0.0f, 0.0f, 1000.0f, 1000.0f}; // World boundaries
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
    Vector2D m_lastPosition{400.0f, 300.0f}; // Last position for movement events
    
    // Internal helper methods
    void updateFollowMode(float deltaTime);
    void updateCameraShake(float deltaTime);
    void clampToWorldBounds();
    Vector2D getTargetPosition() const;
    float calculateDistance(const Vector2D& a, const Vector2D& b) const;
    Vector2D lerp(const Vector2D& a, const Vector2D& b, float t) const;
    Vector2D generateShakeOffset() const;
    
    // Event firing helpers
    void firePositionChangedEvent(const Vector2D& oldPosition, const Vector2D& newPosition);
    void fireModeChangedEvent(Mode oldMode, Mode newMode);
    void fireTargetChangedEvent(std::weak_ptr<Entity> oldTarget, std::weak_ptr<Entity> newTarget);
    void fireShakeStartedEvent(float duration, float intensity);
    void fireShakeEndedEvent();
    void fireViewportChangedEvent(float oldWidth, float oldHeight, float newWidth, float newHeight);
};

} // namespace HammerEngine

#endif // CAMERA_HPP