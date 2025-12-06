/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/Camera.hpp"
#include "entities/Entity.hpp"
#include "core/Logger.hpp"
#include "core/GameEngine.hpp"
#include "events/CameraEvent.hpp"
#include "managers/EventManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include "managers/WorldManager.hpp"

namespace HammerEngine {

Camera::Camera() {
    // Initialize zoom from default config
    if (m_config.isValid() && !m_config.zoomLevels.empty()) {
        m_currentZoomIndex = m_config.defaultZoomLevel;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];
    }
    CAMERA_INFO("Camera created with default configuration");
}

Camera::Camera(const Config& config) : m_config(config) {
    if (!m_config.isValid()) {
        CAMERA_WARN("Invalid camera configuration provided, using defaults");
        m_config = Config{};
    }

    // Initialize zoom from config
    if (m_config.isValid() && !m_config.zoomLevels.empty()) {
        m_currentZoomIndex = m_config.defaultZoomLevel;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];
    }
    CAMERA_INFO("Camera created with custom configuration");
}

Camera::Camera(float x, float y, float viewportWidth, float viewportHeight)
    : m_viewport{viewportWidth, viewportHeight} {
    m_position.setX(x);
    m_position.setY(y);
    m_targetPosition.setX(x);
    m_targetPosition.setY(y);

    if (!m_viewport.isValid()) {
        CAMERA_WARN("Invalid viewport dimensions provided, using defaults");
        m_viewport = Viewport{};
    }

    // Initialize zoom from default config
    if (m_config.isValid() && !m_config.zoomLevels.empty()) {
        m_currentZoomIndex = m_config.defaultZoomLevel;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];
    }
    CAMERA_INFO("Camera created at position (" + std::to_string(x) + ", " + std::to_string(y) + ")");
}

void Camera::update(float deltaTime) {
    // Store previous position BEFORE updating for render interpolation
    // This enables smooth camera at any refresh rate with fixed 60Hz updates
    m_previousPosition = m_position;

    // Update camera shake first (no-ops if inactive)
    if (m_shakeTimeRemaining > 0.0f) {
        m_shakeTimeRemaining -= deltaTime;
        if (m_shakeTimeRemaining <= 0.0f) {
            m_shakeTimeRemaining = 0.0f;
            m_shakeOffset = Vector2D{0.0f, 0.0f};
        } else {
            m_shakeOffset = generateShakeOffset();
        }
    }

    // Smooth follow mode - camera smoothly tracks target position
    if (m_mode == Mode::Follow && hasTarget()) {
        Vector2D targetPos = getTargetPosition();

        // Ideal camera position is the target's position (camera centers on target)
        Vector2D idealPosition = targetPos;

        // Clamp ideal position to world bounds, accounting for viewport size
        if (m_config.clampToWorldBounds) {
            // Use zoom-adjusted viewport - at higher zoom, you see less world space
            const float halfW = m_viewport.halfWidth() / m_zoom;
            const float halfH = m_viewport.halfHeight() / m_zoom;

            const float minX = m_worldBounds.minX + halfW;
            const float maxX = m_worldBounds.maxX - halfW;
            const float minY = m_worldBounds.minY + halfH;
            const float maxY = m_worldBounds.maxY - halfH;

            if (maxX > minX) {
                idealPosition.setX(std::clamp(idealPosition.getX(), minX, maxX));
            } else {
                idealPosition.setX((m_worldBounds.minX + m_worldBounds.maxX) * 0.5f);
            }

            if (maxY > minY) {
                idealPosition.setY(std::clamp(idealPosition.getY(), minY, maxY));
            } else {
                idealPosition.setY((m_worldBounds.minY + m_worldBounds.maxY) * 0.5f);
            }
        }

        // Critically-damped spring (SmoothDamp) - industry standard camera follow
        // Uses velocity tracking for consistent lag regardless of target movement
        const float smoothTime = std::max(0.0001f, m_config.smoothTime);
        const float maxSpeed = m_config.maxSpeed;

        // Calculate spring constants for critical damping (no oscillation)
        // omega = 2/smoothTime gives critical damping behavior
        const float omega = 2.0f / smoothTime;
        const float x = omega * deltaTime;
        const float exp_term = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

        // X axis
        float deltaX = m_position.getX() - idealPosition.getX();
        float tempX = (m_velocity.getX() + omega * deltaX) * deltaTime;
        m_velocity.setX((m_velocity.getX() - omega * tempX) * exp_term);
        float newX = idealPosition.getX() + (deltaX + tempX) * exp_term;

        // Y axis
        float deltaY = m_position.getY() - idealPosition.getY();
        float tempY = (m_velocity.getY() + omega * deltaY) * deltaTime;
        m_velocity.setY((m_velocity.getY() - omega * tempY) * exp_term);
        float newY = idealPosition.getY() + (deltaY + tempY) * exp_term;

        // Clamp velocity to maxSpeed to prevent overshooting on sudden target jumps
        float velMagnitude = std::sqrt(m_velocity.getX() * m_velocity.getX() +
                                        m_velocity.getY() * m_velocity.getY());
        if (velMagnitude > maxSpeed) {
            float scale = maxSpeed / velMagnitude;
            m_velocity.setX(m_velocity.getX() * scale);
            m_velocity.setY(m_velocity.getY() * scale);
        }

        m_position.setX(newX);
        m_position.setY(newY);
    }

    // Ensure final camera position respects world bounds across all modes
    if (m_config.clampToWorldBounds) {
        clampToWorldBounds();
    }
}

void Camera::setPosition(float x, float y) {
    Vector2D oldPosition = m_position;
    m_position.setX(x);
    m_position.setY(y);
    m_targetPosition = m_position; // Update target position to avoid interpolation
    
    // Fire position changed event if enabled
    if (m_eventFiringEnabled) {
        firePositionChangedEvent(oldPosition, m_position);
    }
}

void Camera::setPosition(const Vector2D& position) {
    setPosition(position.getX(), position.getY());
}

void Camera::setViewport(float width, float height) {
    if (width > 0.0f && height > 0.0f) {
        m_viewport.width = width;
        m_viewport.height = height;
        CAMERA_DEBUG("Viewport updated to: " + std::to_string(static_cast<int>(width)) + "x" +
                        std::to_string(static_cast<int>(height)));
    } else {
        CAMERA_WARN("Invalid viewport dimensions: " + std::to_string(width) + "x" + std::to_string(height));
    }
}

void Camera::setViewport(const Viewport& viewport) {
    if (viewport.isValid()) {
        m_viewport = viewport;
        CAMERA_DEBUG("Viewport updated to: " + std::to_string(static_cast<int>(viewport.width)) + "x" +
                        std::to_string(static_cast<int>(viewport.height)));
    } else {
        CAMERA_WARN("Invalid viewport provided");
    }
}

void Camera::setWorldBounds(float minX, float minY, float maxX, float maxY) {
    if (maxX > minX && maxY > minY) {
        m_worldBounds.minX = minX;
        m_worldBounds.minY = minY;
        m_worldBounds.maxX = maxX;
        m_worldBounds.maxY = maxY;
    } else {
        CAMERA_WARN("Invalid world bounds: (" + std::to_string(minX) + ", " + std::to_string(minY) +
                        ") to (" + std::to_string(maxX) + ", " + std::to_string(maxY) + ")");
    }
}

void Camera::setWorldBounds(const Bounds& bounds) {
    if (bounds.isValid()) {
        m_worldBounds = bounds;
    } else {
        CAMERA_WARN("Invalid world bounds provided");
    }
}

void Camera::setMode(Mode mode) {
    if (m_mode != mode) {
        Mode oldMode = m_mode;
        m_mode = mode;
        
        // When switching to follow mode, snap to target if available
        if (mode == Mode::Follow && hasTarget()) {
            Vector2D targetPos = getTargetPosition();
            m_targetPosition = targetPos;
        }
        
        // Fire mode changed event if enabled
        if (m_eventFiringEnabled) {
            fireModeChangedEvent(oldMode, mode);
        }
        
        CAMERA_INFO("Camera mode changed from " + std::to_string(static_cast<int>(oldMode)) +
                       " to " + std::to_string(static_cast<int>(mode)));
    }
}

void Camera::setTarget(std::weak_ptr<Entity> target) {
    std::weak_ptr<Entity> oldTarget = m_target;
    m_target = target;
    m_positionGetter = nullptr; // Clear function-based target
    
    // Fire target changed event if enabled
    if (m_eventFiringEnabled) {
        fireTargetChangedEvent(oldTarget, target);
    }
    
    if (auto targetPtr = target.lock()) {
        CAMERA_INFO("Camera target set to entity");
        // If in follow mode, update target position immediately
        if (m_mode == Mode::Follow) {
            Vector2D targetPos = getTargetPosition();
            m_targetPosition = targetPos;
        }
    } else {
        CAMERA_WARN("Attempted to set invalid target entity");
    }
}

void Camera::setTargetPositionGetter(std::function<Vector2D()> positionGetter) {
    m_positionGetter = positionGetter;
    m_target.reset(); // Clear entity-based target
    
    if (positionGetter) {
        CAMERA_INFO("Camera target set to position getter function");
        // If in follow mode, update target position immediately
        if (m_mode == Mode::Follow) {
            Vector2D targetPos = getTargetPosition();
            m_targetPosition = targetPos;
        }
    }
}

void Camera::clearTarget() {
    m_target.reset();
    m_positionGetter = nullptr;
    CAMERA_INFO("Camera target cleared");
}

bool Camera::hasTarget() const {
    return !m_target.expired() || (m_positionGetter != nullptr);
}

bool Camera::setConfig(const Config& config) {
    if (config.isValid()) {
        m_config = config;
        CAMERA_INFO("Camera configuration updated (smoothTime=" +
                    std::to_string(m_config.smoothTime) + "s)");
        return true;
    } else {
        CAMERA_WARN("Invalid camera configuration provided");
        return false;
    }
}

Camera::ViewRect Camera::getViewRect() const {
    // With SDL_SetRenderScale, the visible world area shrinks as zoom increases
    // At 2x zoom, you see half as much world space (400x300 instead of 800x600)
    float worldViewWidth = m_viewport.width / m_zoom;
    float worldViewHeight = m_viewport.height / m_zoom;

    // Use full floating-point precision for smooth sub-pixel camera movement
    // Tile pixel-snapping is handled in TextureManager::drawTileF() to prevent shimmer
    return ViewRect{
        m_position.getX() - (worldViewWidth * 0.5f),
        m_position.getY() - (worldViewHeight * 0.5f),
        worldViewWidth,
        worldViewHeight
    };
}

void Camera::getRenderOffset(float& offsetX, float& offsetY, float interpolationAlpha) const {
    // Interpolate between previous and current position for smooth rendering
    // at any refresh rate with fixed 60Hz game updates.
    // interpolationAlpha: 0.0 = at previous position, 1.0 = at current position
    float interpX = m_previousPosition.getX() +
                    (m_position.getX() - m_previousPosition.getX()) * interpolationAlpha;
    float interpY = m_previousPosition.getY() +
                    (m_position.getY() - m_previousPosition.getY()) * interpolationAlpha;

    float worldViewWidth = m_viewport.width / m_zoom;
    float worldViewHeight = m_viewport.height / m_zoom;

    offsetX = interpX - (worldViewWidth * 0.5f);
    offsetY = interpY - (worldViewHeight * 0.5f);
}

bool Camera::isPointVisible(float x, float y) const {
    ViewRect view = getViewRect();
    return x >= view.left() && x <= view.right() && 
           y >= view.top() && y <= view.bottom();
}

bool Camera::isPointVisible(const Vector2D& point) const {
    return isPointVisible(point.getX(), point.getY());
}

bool Camera::isRectVisible(float x, float y, float width, float height) const {
    ViewRect view = getViewRect();
    
    // Check if rectangles intersect
    return !(x + width < view.left() || x > view.right() || 
             y + height < view.top() || y > view.bottom());
}

void Camera::worldToScreen(float worldX, float worldY, float& screenX, float& screenY) const {
    // Use cached render offset - ensures entities use SAME offset as tiles
    float offsetX, offsetY;
    getRenderOffset(offsetX, offsetY);

    // Transform world position to screen position
    screenX = worldX - offsetX;
    screenY = worldY - offsetY;
}

void Camera::screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const {
    // Mouse coordinates are in PHYSICAL window space (NOT scaled by SDL_SetRenderScale)
    // Convert physical mouse coords to logical coords first
    float logicalX = screenX / m_zoom;
    float logicalY = screenY / m_zoom;

    // Use cached render offset - consistent with worldToScreen
    float offsetX, offsetY;
    getRenderOffset(offsetX, offsetY);

    // Inverse of worldToScreen: worldPos = screenPos + cameraOffset
    worldX = logicalX + offsetX;
    worldY = logicalY + offsetY;
}

Vector2D Camera::screenToWorld(const Vector2D& screenCoords) const {
    float worldX, worldY;
    screenToWorld(screenCoords.getX(), screenCoords.getY(), worldX, worldY);
    return Vector2D(worldX, worldY);
}

Vector2D Camera::worldToScreen(const Vector2D& worldCoords) const {
    float screenX, screenY;
    worldToScreen(worldCoords.getX(), worldCoords.getY(), screenX, screenY);
    return Vector2D(screenX, screenY);
}

void Camera::snapToTarget() {
    if (hasTarget()) {
        Vector2D targetPos = getTargetPosition();
        setPosition(targetPos);
        CAMERA_INFO("Camera snapped to target position");
    } else {
        CAMERA_WARN("Cannot snap to target: no target set");
    }
}

void Camera::shake(float duration, float intensity) {
    if (duration > 0.0f && intensity > 0.0f) {
        bool wasShaking = m_shakeTimeRemaining > 0.0f;
        m_shakeTimeRemaining = duration;
        m_shakeIntensity = intensity;

        // Fire shake started event if enabled and wasn't already shaking
        if (m_eventFiringEnabled && !wasShaking) {
            fireShakeStartedEvent(duration, intensity);
        }

        CAMERA_INFO("Camera shake started: duration=" + std::to_string(duration) +
                       "s, intensity=" + std::to_string(intensity));
    }
}

void Camera::updateCameraShake(float deltaTime) {
    if (m_shakeTimeRemaining > 0.0f) {
        m_shakeTimeRemaining -= deltaTime;
        
        if (m_shakeTimeRemaining <= 0.0f) {
            m_shakeTimeRemaining = 0.0f;
            m_shakeOffset = Vector2D{0.0f, 0.0f};
            
            // Fire shake ended event if enabled (we were definitely shaking if we're here)
            if (m_eventFiringEnabled) {
                fireShakeEndedEvent();
            }
        } else {
            // Generate random shake offset
            m_shakeOffset = generateShakeOffset();
        }
    }
}

void Camera::updateFollowMode(float deltaTime) {
    (void)deltaTime; // Suppress unused parameter warning
    
    if (!hasTarget()) {
        return;
    }
    
    Vector2D targetPos = getTargetPosition();
    
    // Direct follow - no smoothing lag for tight, responsive camera control
    m_position = targetPos;
}

void Camera::clampToWorldBounds() {
    // Auto-sync world bounds with WorldManager if enabled
    if (m_autoSyncWorldBounds) {
        try {
            const auto &wm = WorldManager::Instance();
            if (wm.hasActiveWorld()) {
                uint64_t currentVersion = wm.getWorldVersion();
                if (currentVersion != m_lastWorldVersion) {
                    float minX = 0.0f, minY = 0.0f, maxX = 0.0f, maxY = 0.0f;
                    if (wm.getWorldBounds(minX, minY, maxX, maxY)) {
                        m_worldBounds.minX = minX;
                        m_worldBounds.minY = minY;
                        m_worldBounds.maxX = maxX;
                        m_worldBounds.maxY = maxY;
                        m_lastWorldVersion = currentVersion;
                    }
                }
            }
        } catch (...) {
            // If WorldManager is unavailable, keep current bounds
        }
    }

    // Calculate effective bounds using zoom-adjusted viewport dimensions
    // At higher zoom, you see less world space, so bounds are tighter
    float halfViewWidth = m_viewport.halfWidth() / m_zoom;
    float halfViewHeight = m_viewport.halfHeight() / m_zoom;

    // Calculate effective bounds (world bounds minus half viewport to keep camera centered)
    float minX = m_worldBounds.minX + halfViewWidth;
    float maxX = m_worldBounds.maxX - halfViewWidth;
    float minY = m_worldBounds.minY + halfViewHeight;
    float maxY = m_worldBounds.maxY - halfViewHeight;

    // Only clamp if the world is larger than the viewport
    if (maxX > minX) {
        m_position.setX(std::clamp(m_position.getX(), minX, maxX));
    } else {
        // World is smaller than viewport, center camera
        m_position.setX((m_worldBounds.minX + m_worldBounds.maxX) * 0.5f);
    }

    if (maxY > minY) {
        m_position.setY(std::clamp(m_position.getY(), minY, maxY));
    } else {
        // World is smaller than viewport, center camera
        m_position.setY((m_worldBounds.minY + m_worldBounds.maxY) * 0.5f);
    }
}

Vector2D Camera::getTargetPosition() const {
    if (auto targetPtr = m_target.lock()) {
        return targetPtr->getPosition();
    } else if (m_positionGetter) {
        return m_positionGetter();
    }
    return m_position; // Fallback to current position
}

float Camera::calculateDistance(const Vector2D& a, const Vector2D& b) const {
    float dx = b.getX() - a.getX();
    float dy = b.getY() - a.getY();
    return std::sqrt(dx * dx + dy * dy);
}

Vector2D Camera::lerp(const Vector2D& a, const Vector2D& b, float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    return Vector2D{
        a.getX() + (b.getX() - a.getX()) * t,
        a.getY() + (b.getY() - a.getY()) * t
    };
}

Vector2D Camera::generateShakeOffset() const {
    // Per CLAUDE.md: Use member vars instead of static for thread safety
    const float normalizedTime = 1.0f - (m_shakeTimeRemaining / std::max(m_shakeTimeRemaining + 0.1f, 0.1f));
    const float currentIntensity = m_shakeIntensity * (1.0f - normalizedTime); // Fade out over time

    // Use member RNG and distribution for thread safety and performance
    return Vector2D{
        m_shakeDist(m_shakeRng) * currentIntensity,
        m_shakeDist(m_shakeRng) * currentIntensity
    };
}

// Event firing helper methods
void Camera::firePositionChangedEvent(const Vector2D& oldPosition, const Vector2D& newPosition) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraMoved(newPosition, oldPosition,
                                          EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraMovedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireModeChangedEvent(Mode oldMode, Mode newMode) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraModeChanged(static_cast<int>(newMode), static_cast<int>(oldMode),
                                                EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraModeChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireTargetChangedEvent(std::weak_ptr<Entity> oldTarget, std::weak_ptr<Entity> newTarget) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraTargetChanged(newTarget, oldTarget,
                                                  EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraTargetChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeStartedEvent(float duration, float intensity) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraShakeStarted(duration, intensity,
                                                 EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraShakeStartedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeEndedEvent() {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraShakeEnded(EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraShakeEndedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireZoomChangedEvent(float oldZoom, float newZoom) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraZoomChanged(newZoom, oldZoom, EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        CAMERA_ERROR("Failed to fire CameraZoomChangedEvent: " + std::string(ex.what()));
    }
}

// Zoom control methods
void Camera::zoomIn() {
    const int maxZoomIndex = static_cast<int>(m_config.zoomLevels.size()) - 1;
    if (m_currentZoomIndex < maxZoomIndex) {
        float oldZoom = m_zoom;
        m_currentZoomIndex++;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];

        // Fire zoom changed event if enabled
        if (m_eventFiringEnabled) {
            fireZoomChangedEvent(oldZoom, m_zoom);
        }

        CAMERA_INFO("Camera zoomed in to level " + std::to_string(m_currentZoomIndex) +
                       " (zoom: " + std::to_string(m_zoom) + "x)");
    } else {
        CAMERA_DEBUG("Camera already at maximum zoom level");
    }
}

void Camera::zoomOut() {
    if (m_currentZoomIndex > 0) {
        float oldZoom = m_zoom;
        m_currentZoomIndex--;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];

        // Fire zoom changed event if enabled
        if (m_eventFiringEnabled) {
            fireZoomChangedEvent(oldZoom, m_zoom);
        }

        CAMERA_INFO("Camera zoomed out to level " + std::to_string(m_currentZoomIndex) +
                       " (zoom: " + std::to_string(m_zoom) + "x)");
    } else {
        CAMERA_DEBUG("Camera already at minimum zoom level");
    }
}

bool Camera::setZoomLevel(int levelIndex) {
    const int maxZoomIndex = static_cast<int>(m_config.zoomLevels.size()) - 1;
    if (levelIndex < 0 || levelIndex > maxZoomIndex) {
        CAMERA_WARN("Invalid zoom level index: " + std::to_string(levelIndex) +
                       " (valid range: 0-" + std::to_string(maxZoomIndex) + ")");
        return false;
    }

    if (m_currentZoomIndex != levelIndex) {
        float oldZoom = m_zoom;
        m_currentZoomIndex = levelIndex;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];

        // Fire zoom changed event if enabled
        if (m_eventFiringEnabled) {
            fireZoomChangedEvent(oldZoom, m_zoom);
        }

        CAMERA_INFO("Camera zoom set to level " + std::to_string(m_currentZoomIndex) +
                       " (zoom: " + std::to_string(m_zoom) + "x)");
    }

    return true;
}

void Camera::syncViewportWithEngine() {
    // Get current logical dimensions from GameEngine (authoritative source)
    const GameEngine& gameEngine = GameEngine::Instance();
    float logicalWidth = static_cast<float>(gameEngine.getLogicalWidth());
    float logicalHeight = static_cast<float>(gameEngine.getLogicalHeight());

    // Only update if dimensions actually changed (avoid unnecessary updates)
    if (m_viewport.width != logicalWidth || m_viewport.height != logicalHeight) {
        CAMERA_INFO("Syncing camera viewport: " +
                       std::to_string(static_cast<int>(m_viewport.width)) + "x" +
                       std::to_string(static_cast<int>(m_viewport.height)) + " -> " +
                       std::to_string(static_cast<int>(logicalWidth)) + "x" +
                       std::to_string(static_cast<int>(logicalHeight)));

        setViewport(logicalWidth, logicalHeight);
    }
}

} // namespace HammerEngine
