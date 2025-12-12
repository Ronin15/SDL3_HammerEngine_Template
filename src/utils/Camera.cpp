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
#include <format>
#include "managers/WorldManager.hpp"

namespace HammerEngine {

Camera::Camera() {
    // Initialize zoom from default config
    if (m_config.isValid() && !m_config.zoomLevels.empty()) {
        m_currentZoomIndex = m_config.defaultZoomLevel;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];
    }
    // Initialize atomic interpolation state with default position
    float x = m_position.getX();
    float y = m_position.getY();
    m_interpState.store({x, y, x, y}, std::memory_order_relaxed);
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
    // Initialize atomic interpolation state with default position
    float x = m_position.getX();
    float y = m_position.getY();
    m_interpState.store({x, y, x, y}, std::memory_order_relaxed);
    CAMERA_INFO("Camera created with custom configuration");
}

Camera::Camera(float x, float y, float viewportWidth, float viewportHeight)
    : m_viewport{viewportWidth, viewportHeight} {
    m_position.setX(x);
    m_position.setY(y);
    m_targetPosition.setX(x);
    m_targetPosition.setY(y);
    m_previousPosition.setX(x);
    m_previousPosition.setY(y);

    // Initialize atomic interpolation state with starting position
    m_interpState.store({x, y, x, y}, std::memory_order_relaxed);

    if (!m_viewport.isValid()) {
        CAMERA_WARN("Invalid viewport dimensions provided, using defaults");
        m_viewport = Viewport{};
    }

    // Initialize zoom from default config
    if (m_config.isValid() && !m_config.zoomLevels.empty()) {
        m_currentZoomIndex = m_config.defaultZoomLevel;
        m_zoom = m_config.zoomLevels[m_currentZoomIndex];
    }
    CAMERA_INFO(std::format("Camera created at position ({}, {})", x, y));
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

    // Follow mode - lock camera to target position (no lag = no jitter)
    if (m_mode == Mode::Follow && hasTarget()) {
        Vector2D targetPos = getTargetPosition();

        // Clamp target to world bounds, accounting for viewport size
        if (m_config.clampToWorldBounds) {
            const float halfW = m_viewport.halfWidth() / m_zoom;
            const float halfH = m_viewport.halfHeight() / m_zoom;

            const float minX = m_worldBounds.minX + halfW;
            const float maxX = m_worldBounds.maxX - halfW;
            const float minY = m_worldBounds.minY + halfH;
            const float maxY = m_worldBounds.maxY - halfH;

            if (maxX > minX) {
                targetPos.setX(std::clamp(targetPos.getX(), minX, maxX));
            } else {
                targetPos.setX((m_worldBounds.minX + m_worldBounds.maxX) * 0.5f);
            }

            if (maxY > minY) {
                targetPos.setY(std::clamp(targetPos.getY(), minY, maxY));
            } else {
                targetPos.setY((m_worldBounds.minY + m_worldBounds.maxY) * 0.5f);
            }
        }

        // Lock to target - camera position equals player position
        m_position = targetPos;
    }

    // Ensure final camera position respects world bounds across all modes
    if (m_config.clampToWorldBounds) {
        clampToWorldBounds();
    }

    // Atomically publish interpolation state for render thread (industry-standard pattern)
    // Render thread reads this via getRenderOffset() for smooth camera interpolation
    m_interpState.store({m_position.getX(), m_position.getY(),
                         m_previousPosition.getX(), m_previousPosition.getY()},
                        std::memory_order_release);
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
        CAMERA_DEBUG(std::format("Viewport updated to: {}x{}",
                                 static_cast<int>(width), static_cast<int>(height)));
    } else {
        CAMERA_WARN(std::format("Invalid viewport dimensions: {}x{}", width, height));
    }
}

void Camera::setViewport(const Viewport& viewport) {
    if (viewport.isValid()) {
        m_viewport = viewport;
        CAMERA_DEBUG(std::format("Viewport updated to: {}x{}",
                                 static_cast<int>(viewport.width), static_cast<int>(viewport.height)));
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
        CAMERA_WARN(std::format("Invalid world bounds: ({}, {}) to ({}, {})",
                                minX, minY, maxX, maxY));
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

        CAMERA_INFO(std::format("Camera mode changed from {} to {}",
                                static_cast<int>(oldMode), static_cast<int>(mode)));
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
        CAMERA_INFO(std::format("Camera configuration updated (followSpeed={})", m_config.followSpeed));
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
    // Callers snap to pixels at render time if tile-aligned rendering is needed
    return ViewRect{
        m_position.getX() - (worldViewWidth * 0.5f),
        m_position.getY() - (worldViewHeight * 0.5f),
        worldViewWidth,
        worldViewHeight
    };
}

void Camera::getRenderOffset(float entityInterpX, float entityInterpY,
                             float& offsetX, float& offsetY) const {
    // UNIFIED INTERPOLATION: Compute camera offset from pre-computed entity position
    // This ensures camera and entity use the EXACT same position, eliminating wobble
    float worldViewWidth = m_viewport.width / m_zoom;
    float worldViewHeight = m_viewport.height / m_zoom;

    // Center camera on entity's interpolated position
    // Sub-pixel camera position - unified chunk rendering (biomes + obstacles together) eliminates wobble
    offsetX = entityInterpX - (worldViewWidth * 0.5f);
    offsetY = entityInterpY - (worldViewHeight * 0.5f);

    // Apply world bounds clamping if enabled
    if (m_config.clampToWorldBounds) {
        float maxOffsetX = m_worldBounds.maxX - worldViewWidth;
        float maxOffsetY = m_worldBounds.maxY - worldViewHeight;

        if (maxOffsetX > m_worldBounds.minX) {
            offsetX = std::clamp(offsetX, m_worldBounds.minX, maxOffsetX);
        } else {
            // World smaller than viewport - center it
            offsetX = (m_worldBounds.minX + m_worldBounds.maxX - worldViewWidth) * 0.5f;
        }

        if (maxOffsetY > m_worldBounds.minY) {
            offsetY = std::clamp(offsetY, m_worldBounds.minY, maxOffsetY);
        } else {
            // World smaller than viewport - center it
            offsetY = (m_worldBounds.minY + m_worldBounds.maxY - worldViewHeight) * 0.5f;
        }
    }
}

Vector2D Camera::getRenderOffset(float& offsetX, float& offsetY, float interpolationAlpha) const {
    Vector2D center;

    // Follow mode: derive offset from TARGET's interpolated position (single atomic read)
    // This ensures camera and followed entity use the EXACT same position - no jitter
    if (m_mode == Mode::Follow) {
        if (auto targetPtr = m_target.lock()) {
            // ONE atomic read - this is the source of truth for this frame
            center = targetPtr->getInterpolatedPosition(interpolationAlpha);
        } else {
            // No valid target, fall back to camera's own interpolation
            auto camState = m_interpState.load(std::memory_order_acquire);
            center.setX(camState.prevPosX + (camState.posX - camState.prevPosX) * interpolationAlpha);
            center.setY(camState.prevPosY + (camState.posY - camState.prevPosY) * interpolationAlpha);
        }
    } else {
        // Free/Fixed mode: use camera's own interpolation
        auto camState = m_interpState.load(std::memory_order_acquire);
        center.setX(camState.prevPosX + (camState.posX - camState.prevPosX) * interpolationAlpha);
        center.setY(camState.prevPosY + (camState.posY - camState.prevPosY) * interpolationAlpha);
    }

    // Compute screen offset from the center position we determined
    getRenderOffset(center.getX(), center.getY(), offsetX, offsetY);

    // Return the center position we used - caller should render followed entity here
    return center;
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
    // For coordinate transforms, use raw camera position without world bounds clamping
    // World bounds clamping is for rendering, not for abstract coordinate math
    float worldViewWidth = m_viewport.width / m_zoom;
    float worldViewHeight = m_viewport.height / m_zoom;

    // Get camera's interpolated position
    auto camState = m_interpState.load(std::memory_order_acquire);
    float camX = camState.posX;
    float camY = camState.posY;

    // Camera offset: centers camera on its position
    float offsetX = std::floor(camX - (worldViewWidth * 0.5f));
    float offsetY = std::floor(camY - (worldViewHeight * 0.5f));

    // Transform world position to screen position
    screenX = worldX - offsetX;
    screenY = worldY - offsetY;
}

void Camera::screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const {
    // Mouse coordinates are in PHYSICAL window space (NOT scaled by SDL_SetRenderScale)
    // Convert physical mouse coords to logical coords first
    float logicalX = screenX / m_zoom;
    float logicalY = screenY / m_zoom;

    // For coordinate transforms, use raw camera position without world bounds clamping
    float worldViewWidth = m_viewport.width / m_zoom;
    float worldViewHeight = m_viewport.height / m_zoom;

    // Get camera's interpolated position
    auto camState = m_interpState.load(std::memory_order_acquire);
    float camX = camState.posX;
    float camY = camState.posY;

    // Camera offset: centers camera on its position
    float offsetX = std::floor(camX - (worldViewWidth * 0.5f));
    float offsetY = std::floor(camY - (worldViewHeight * 0.5f));

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

        CAMERA_INFO(std::format("Camera shake started: duration={}s, intensity={}", duration, intensity));
    }
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

        CAMERA_INFO(std::format("Camera zoomed in to level {} (zoom: {}x)", m_currentZoomIndex, m_zoom));
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

        CAMERA_INFO(std::format("Camera zoomed out to level {} (zoom: {}x)", m_currentZoomIndex, m_zoom));
    } else {
        CAMERA_DEBUG("Camera already at minimum zoom level");
    }
}

bool Camera::setZoomLevel(int levelIndex) {
    const int maxZoomIndex = static_cast<int>(m_config.zoomLevels.size()) - 1;
    if (levelIndex < 0 || levelIndex > maxZoomIndex) {
        CAMERA_WARN(std::format("Invalid zoom level index: {} (valid range: 0-{})",
                                levelIndex, maxZoomIndex));
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

        CAMERA_INFO(std::format("Camera zoom set to level {} (zoom: {}x)", m_currentZoomIndex, m_zoom));
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
        CAMERA_INFO(std::format("Syncing camera viewport: {}x{} -> {}x{}",
                                static_cast<int>(m_viewport.width), static_cast<int>(m_viewport.height),
                                static_cast<int>(logicalWidth), static_cast<int>(logicalHeight)));

        setViewport(logicalWidth, logicalHeight);
    }
}

} // namespace HammerEngine
