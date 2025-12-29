/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "entities/Entity.hpp"
#include "managers/EntityDataManager.hpp"

// ============================================================================
// TRANSFORM ACCESSORS - Redirect to EntityDataManager (Phase 4)
// ============================================================================

Vector2D Entity::getPosition() const {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            return edm.getTransform(m_handle).position;
        }
    }
    return m_position;  // Fallback to legacy storage
}

Vector2D Entity::getPreviousPosition() const {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            return edm.getTransform(m_handle).previousPosition;
        }
    }
    return m_previousPosition;  // Fallback to legacy storage
}

Vector2D Entity::getVelocity() const {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            return edm.getTransform(m_handle).velocity;
        }
    }
    return m_velocity;  // Fallback to legacy storage
}

Vector2D Entity::getAcceleration() const {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            return edm.getTransform(m_handle).acceleration;
        }
    }
    return m_acceleration;  // Fallback to legacy storage
}

Vector2D Entity::getInterpolatedPosition(float alpha) const {
    Vector2D pos = getPosition();
    Vector2D prevPos = getPreviousPosition();
    return Vector2D(
        prevPos.getX() + (pos.getX() - prevPos.getX()) * alpha,
        prevPos.getY() + (pos.getY() - prevPos.getY()) * alpha);
}

void Entity::storePositionForInterpolation() {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            auto& transform = edm.getTransform(m_handle);
            transform.previousPosition = transform.position;
            return;
        }
    }
    m_previousPosition = m_position;  // Fallback to legacy storage
}

void Entity::updatePositionFromMovement(const Vector2D& position) {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            edm.getTransform(m_handle).position = position;
            return;
        }
    }
    m_position = position;  // Fallback to legacy storage
}

void Entity::setPosition(const Vector2D& position) {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            auto& transform = edm.getTransform(m_handle);
            transform.position = position;
            transform.previousPosition = position;  // Prevents interpolation sliding
            return;
        }
    }
    // Fallback to legacy storage
    m_position = position;
    m_previousPosition = position;
}

void Entity::setVelocity(const Vector2D& velocity) {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            edm.getTransform(m_handle).velocity = velocity;
            return;
        }
    }
    m_velocity = velocity;  // Fallback to legacy storage
}

void Entity::setAcceleration(const Vector2D& acceleration) {
    if (m_handle.isValid()) {
        auto& edm = EntityDataManager::Instance();
        if (edm.isInitialized()) {
            edm.getTransform(m_handle).acceleration = acceleration;
            return;
        }
    }
    m_acceleration = acceleration;  // Fallback to legacy storage
}

// ============================================================================
// ANIMATION
// ============================================================================

void Entity::playAnimation(const std::string& animName) {
    auto it = m_animationMap.find(animName);
    if (it != m_animationMap.end()) {
        const auto& config = it->second;
        m_currentRow = config.row + 1;  // TextureManager uses 1-based rows
        m_numFrames = config.frameCount;
        m_animSpeed = config.speed;
        m_animationLoops = config.loop;
        m_currentFrame = 0;
    }
}
