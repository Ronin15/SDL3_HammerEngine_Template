/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "utils/Camera.hpp"
#include "entities/Entity.hpp"
#include "core/Logger.hpp"
#include "events/CameraEvent.hpp"
#include "managers/EventManager.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include "managers/WorldManager.hpp"

namespace HammerEngine {

Camera::Camera() {
    GAMEENGINE_INFO("Camera created with default configuration");
}

Camera::Camera(const Config& config) : m_config(config) {
    if (!m_config.isValid()) {
        GAMEENGINE_WARN("Invalid camera configuration provided, using defaults");
        m_config = Config{};
    }
    GAMEENGINE_INFO("Camera created with custom configuration");
}

Camera::Camera(float x, float y, float viewportWidth, float viewportHeight) 
    : m_viewport{viewportWidth, viewportHeight} {
    m_position.setX(x);
    m_position.setY(y);
    m_targetPosition.setX(x);
    m_targetPosition.setY(y);
    
    if (!m_viewport.isValid()) {
        GAMEENGINE_WARN("Invalid viewport dimensions provided, using defaults");
        m_viewport = Viewport{};
    }
    GAMEENGINE_INFO("Camera created at position (" + std::to_string(x) + ", " + std::to_string(y) + ")");
}

void Camera::update(float deltaTime) {
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

    // Smooth follow mode using configured parameters
    if (m_mode == Mode::Follow && hasTarget()) {
        Vector2D targetPos = getTargetPosition();

        // Ideal camera position is the target's position (camera centers on target)
        Vector2D idealPosition = targetPos;

        // Clamp ideal position to world bounds, accounting for viewport size
        if (m_config.clampToWorldBounds) {
            const float halfW = m_viewport.halfWidth();
            const float halfH = m_viewport.halfHeight();

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

        // Offset from current to ideal
        const float dx = idealPosition.getX() - m_position.getX();
        const float dy = idealPosition.getY() - m_position.getY();
        const float distance = std::sqrt(dx * dx + dy * dy);

        // Dead zone from config to avoid micro-oscillations
        const float deadZone = std::max(0.0f, m_config.deadZoneRadius);
        if (distance > deadZone) {
            // Exponential smoothing with configurable responsiveness
            // alpha = 1 - smoothingFactor^(dt * followSpeed)
            float alpha = 1.0f - std::pow(std::clamp(m_config.smoothingFactor, 0.0f, 1.0f),
                                          std::max(0.0f, deltaTime * std::max(0.0f, m_config.followSpeed)));
            alpha = std::clamp(alpha, 0.0f, 1.0f);

            float newX = m_position.getX() + dx * alpha;
            float newY = m_position.getY() + dy * alpha;

            // If we would end up inside the dead zone, stop at its edge
            const float ndx = idealPosition.getX() - newX;
            const float ndy = idealPosition.getY() - newY;
            const float newDistance = std::sqrt(ndx * ndx + ndy * ndy);
            if (newDistance < deadZone && distance > 0.0f) {
                const float ratio = (distance - deadZone) / distance;
                newX = m_position.getX() + dx * ratio;
                newY = m_position.getY() + dy * ratio;
            }

            m_position.setX(newX);
            m_position.setY(newY);
    }
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

void Camera::setWorldBounds(float minX, float minY, float maxX, float maxY) {
    if (maxX > minX && maxY > minY) {
        m_worldBounds.minX = minX;
        m_worldBounds.minY = minY;
        m_worldBounds.maxX = maxX;
        m_worldBounds.maxY = maxY;
    } else {
        GAMEENGINE_WARN("Invalid world bounds: (" + std::to_string(minX) + ", " + std::to_string(minY) + 
                        ") to (" + std::to_string(maxX) + ", " + std::to_string(maxY) + ")");
    }
}

void Camera::setWorldBounds(const Bounds& bounds) {
    if (bounds.isValid()) {
        m_worldBounds = bounds;
    } else {
        GAMEENGINE_WARN("Invalid world bounds provided");
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
        
        GAMEENGINE_INFO("Camera mode changed from " + std::to_string(static_cast<int>(oldMode)) + 
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
        GAMEENGINE_INFO("Camera target set to entity");
        // If in follow mode, update target position immediately
        if (m_mode == Mode::Follow) {
            Vector2D targetPos = getTargetPosition();
            m_targetPosition = targetPos;
        }
    } else {
        GAMEENGINE_WARN("Attempted to set invalid target entity");
    }
}

void Camera::setTargetPositionGetter(std::function<Vector2D()> positionGetter) {
    m_positionGetter = positionGetter;
    m_target.reset(); // Clear entity-based target
    
    if (positionGetter) {
        GAMEENGINE_INFO("Camera target set to position getter function");
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
    GAMEENGINE_INFO("Camera target cleared");
}

bool Camera::hasTarget() const {
    return !m_target.expired() || (m_positionGetter != nullptr);
}

bool Camera::setConfig(const Config& config) {
    if (config.isValid()) {
        m_config = config;
        GAMEENGINE_INFO("Camera configuration updated");
        return true;
    } else {
        GAMEENGINE_WARN("Invalid camera configuration provided");
        return false;
    }
}

Camera::ViewRect Camera::getViewRect() const {
    // Return the camera's world position directly - SDL viewport will handle transformation
    return ViewRect{
        m_position.getX() - m_viewport.halfWidth(),
        m_position.getY() - m_viewport.halfHeight(),
        m_viewport.width,
        m_viewport.height
    };
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
    ViewRect view = getViewRect();
    screenX = worldX - view.x;
    screenY = worldY - view.y;
}

void Camera::screenToWorld(float screenX, float screenY, float& worldX, float& worldY) const {
    ViewRect view = getViewRect();
    worldX = screenX + view.x;
    worldY = screenY + view.y;
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
        GAMEENGINE_INFO("Camera snapped to target position");
    } else {
        GAMEENGINE_WARN("Cannot snap to target: no target set");
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
        
        GAMEENGINE_INFO("Camera shake started: duration=" + std::to_string(duration) + 
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
            auto &wm = WorldManager::Instance();
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

    float halfViewWidth = m_viewport.halfWidth();
    float halfViewHeight = m_viewport.halfHeight();
    
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
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(-1.0f, 1.0f);
    
    float normalizedTime = 1.0f - (m_shakeTimeRemaining / std::max(m_shakeTimeRemaining + 0.1f, 0.1f));
    float currentIntensity = m_shakeIntensity * (1.0f - normalizedTime); // Fade out over time
    
    return Vector2D{
        dis(gen) * currentIntensity,
        dis(gen) * currentIntensity
    };
}

// Event firing helper methods
void Camera::firePositionChangedEvent(const Vector2D& oldPosition, const Vector2D& newPosition) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraMoved(newPosition, oldPosition,
                                          EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraMovedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireModeChangedEvent(Mode oldMode, Mode newMode) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraModeChanged(static_cast<int>(newMode), static_cast<int>(oldMode),
                                                EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraModeChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireTargetChangedEvent(std::weak_ptr<Entity> oldTarget, std::weak_ptr<Entity> newTarget) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraTargetChanged(newTarget, oldTarget,
                                                  EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraTargetChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeStartedEvent(float duration, float intensity) {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraShakeStarted(duration, intensity,
                                                 EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraShakeStartedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeEndedEvent() {
    try {
        const EventManager& eventMgr = EventManager::Instance();
        (void)eventMgr.triggerCameraShakeEnded(EventManager::DispatchMode::Deferred);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraShakeEndedEvent: " + std::string(ex.what()));
    }
}

} // namespace HammerEngine
