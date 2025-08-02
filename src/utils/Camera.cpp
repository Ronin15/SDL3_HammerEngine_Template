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
    // Store old position for event firing
    Vector2D oldPosition = m_position;
    
    // Update camera shake
    updateCameraShake(deltaTime);
    
    // Update based on mode
    switch (m_mode) {
        case Mode::Follow:
            updateFollowMode(deltaTime);
            break;
        case Mode::Free:
            // Free mode - no automatic updates
            break;
        case Mode::Fixed:
            // Fixed mode - camera doesn't move
            break;
    }
    
    // Apply world bounds clamping if enabled
    if (m_config.clampToWorldBounds) {
        clampToWorldBounds();
    }
    
    // Fire position changed event if position changed significantly
    if (m_eventFiringEnabled) {
        float deltaX = std::abs(m_position.getX() - oldPosition.getX());
        float deltaY = std::abs(m_position.getY() - oldPosition.getY());
        if (deltaX > 1.0f || deltaY > 1.0f) { // Only fire for significant changes
            firePositionChangedEvent(oldPosition, m_position);
        }
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
        float oldWidth = m_viewport.width;
        float oldHeight = m_viewport.height;
        
        m_viewport.width = width;
        m_viewport.height = height;
        
        // Fire viewport changed event if enabled
        if (m_eventFiringEnabled) {
            fireViewportChangedEvent(oldWidth, oldHeight, width, height);
        }
    } else {
        GAMEENGINE_WARN("Invalid viewport dimensions provided: " + 
                       std::to_string(width) + "x" + std::to_string(height));
    }
}

void Camera::setViewport(const Viewport& viewport) {
    if (viewport.isValid()) {
        m_viewport = viewport;
    } else {
        GAMEENGINE_WARN("Invalid viewport provided");
    }
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
    // Apply shake offset to the position
    Vector2D finalPos = m_position + m_shakeOffset;
    
    return ViewRect{
        finalPos.getX() - m_viewport.halfWidth(),
        finalPos.getY() - m_viewport.halfHeight(),
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

void Camera::updateFollowMode(float deltaTime) {
    if (!hasTarget()) {
        return;
    }
    
    Vector2D targetPos = getTargetPosition();
    Vector2D currentPos = m_position;
    
    // Calculate distance to target
    float distance = calculateDistance(currentPos, targetPos);
    
    // Check if target is outside dead zone
    if (distance > m_config.deadZoneRadius) {
        // Check if target is too far away
        if (distance > m_config.maxFollowDistance) {
            // Snap to within max follow distance
            Vector2D direction = targetPos - currentPos;
            direction = direction.normalized();
            m_targetPosition = targetPos - direction * (m_config.maxFollowDistance * 0.8f);
        } else {
            m_targetPosition = targetPos;
        }
        
        // Smooth interpolation to target position
        float lerpFactor = 1.0f - std::pow(1.0f - m_config.smoothingFactor, deltaTime * m_config.followSpeed);
        m_position = lerp(currentPos, m_targetPosition, lerpFactor);
    }
}

void Camera::updateCameraShake(float deltaTime) {
    if (m_shakeTimeRemaining > 0.0f) {
        bool wasShaking = true;
        m_shakeTimeRemaining -= deltaTime;
        
        if (m_shakeTimeRemaining <= 0.0f) {
            m_shakeTimeRemaining = 0.0f;
            m_shakeOffset = Vector2D{0.0f, 0.0f};
            
            // Fire shake ended event if enabled
            if (m_eventFiringEnabled && wasShaking) {
                fireShakeEndedEvent();
            }
        } else {
            // Generate random shake offset
            m_shakeOffset = generateShakeOffset();
        }
    }
}

void Camera::clampToWorldBounds() {
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
        auto event = std::make_shared<CameraMovedEvent>(newPosition, oldPosition);
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_moved_" + std::to_string(static_cast<int>(newPosition.getX())) + 
                               "_" + std::to_string(static_cast<int>(newPosition.getY()));
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraMovedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireModeChangedEvent(Mode oldMode, Mode newMode) {
    try {
        // Convert Camera::Mode to CameraModeChangedEvent::Mode
        auto convertMode = [](Mode mode) -> CameraModeChangedEvent::Mode {
            switch (mode) {
                case Mode::Free: return CameraModeChangedEvent::Mode::Free;
                case Mode::Follow: return CameraModeChangedEvent::Mode::Follow;
                case Mode::Fixed: return CameraModeChangedEvent::Mode::Fixed;
                default: return CameraModeChangedEvent::Mode::Free;
            }
        };
        
        auto event = std::make_shared<CameraModeChangedEvent>(convertMode(newMode), convertMode(oldMode));
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_mode_changed_" + std::to_string(static_cast<int>(newMode));
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraModeChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireTargetChangedEvent(std::weak_ptr<Entity> oldTarget, std::weak_ptr<Entity> newTarget) {
    try {
        auto event = std::make_shared<CameraTargetChangedEvent>(newTarget, oldTarget);
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_target_changed";
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraTargetChangedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeStartedEvent(float duration, float intensity) {
    try {
        auto event = std::make_shared<CameraShakeStartedEvent>(duration, intensity);
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_shake_started";
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraShakeStartedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireShakeEndedEvent() {
    try {
        auto event = std::make_shared<CameraShakeEndedEvent>();
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_shake_ended";
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire CameraShakeEndedEvent: " + std::string(ex.what()));
    }
}

void Camera::fireViewportChangedEvent(float oldWidth, float oldHeight, float newWidth, float newHeight) {
    try {
        auto event = std::make_shared<ViewportChangedEvent>(newWidth, newHeight, oldWidth, oldHeight);
        EventManager& eventMgr = EventManager::Instance();
        
        std::string eventName = "camera_viewport_changed";
        
        eventMgr.registerEvent(eventName, event);
        eventMgr.executeEvent(eventName);
    } catch (const std::exception& ex) {
        GAMEENGINE_ERROR("Failed to fire ViewportChangedEvent: " + std::string(ex.what()));
    }
}

} // namespace HammerEngine