/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/WanderBehavior.hpp"
#include "SDL3/SDL_surface.h"
#include <algorithm>
#include <cmath>

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval, float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval), m_areaRadius(areaRadius) {
    // Initialize random number generator
    std::random_device rd;
    m_rng = std::mt19937(rd());
    m_angleDistribution = std::uniform_real_distribution<float>(0.0f, 2.0f * M_PI);
    m_wanderOffscreenChance = std::uniform_real_distribution<float>(0.0f, 1.0f);
}

void WanderBehavior::init(Entity* entity) {
    if (!entity) return;

    // Store initial position as center point
    m_centerPoint = entity->getPosition();

    // Record start time for direction changes
    m_lastDirectionChangeTime = SDL_GetTicks();

    // Set initial random direction
    chooseNewDirection(entity);
}

void WanderBehavior::update(Entity* entity) {
    if (!entity || !m_active) return;

    // Get current time
    Uint64 currentTime = SDL_GetTicks();

    // Check if it's time to change direction
    if (currentTime - m_lastDirectionChangeTime > m_changeDirectionInterval) {
        // Decide whether to wander offscreen or stay within bounds
        bool wanderOffscreen = !m_resetScheduled && m_wanderOffscreenChance(m_rng) < m_offscreenProbability;
        chooseNewDirection(entity, wanderOffscreen);
        m_lastDirectionChangeTime = currentTime;
    }

    // Check if entity is outside screen bounds
    Vector2D position = entity->getPosition();

    // If we've scheduled a reset and the entity is sufficiently offscreen
    if (m_resetScheduled) {
        // Check if entity is far enough off screen to reset
        if (isWellOffscreen(position)) {
            // Reset to a random position near the opposite side of the screen
            resetEntityPosition(entity);
            m_resetScheduled = false;
        }
    }
    else if (!m_currentlyWanderingOffscreen) {
        // Normal bounded wandering behavior
        Vector2D toCenter = m_centerPoint - position;
        float distanceFromCenter = toCenter.length();

        // If too far from center, adjust direction to return (unless we're wandering offscreen)
        if (distanceFromCenter > m_areaRadius) {
            // Calculate direction back to center
            Vector2D returnDirection = toCenter;
            returnDirection.normalize();

            // Blend current direction with return direction
            float blendFactor = (distanceFromCenter - m_areaRadius) / 50.0f;
            blendFactor = std::min(blendFactor, 1.0f);

            m_currentDirection = (m_currentDirection * (1.0f - blendFactor)) +
                                (returnDirection * blendFactor);
            m_currentDirection.normalize();

            // Apply the new direction
            entity->setVelocity(m_currentDirection * m_speed);
        }
    }

    // Apply current direction and handle sprite flipping
    if (m_currentDirection.getX() < 0) {
        entity->setFlip(SDL_FLIP_NONE);
    } else if (m_currentDirection.getX() > 0) {
        entity->setFlip(SDL_FLIP_HORIZONTAL);
    }
}

void WanderBehavior::clean(Entity* entity) {
    if (!entity) return;

    // Stop the entity when behavior is cleaned up
    entity->setVelocity(Vector2D(0, 0));
}

void WanderBehavior::onMessage(Entity* entity, const std::string& message) {
    if (!entity) return;

    if (message == "pause"){
        setActive(false);
        entity->setVelocity(Vector2D(0, 0));
    } else if (message == "resume") {
        setActive(true);
        chooseNewDirection(entity);
    } else if (message == "new_direction") {
        chooseNewDirection(entity);
    } else if (message == "increase_speed") {
        m_speed *= 1.5f;
        if (entity && m_active) {
            entity->setVelocity(m_currentDirection * m_speed);
        }
    } else if (message == "decrease_speed") {
        m_speed *= 0.75f;
        if (entity && m_active) {
            entity->setVelocity(m_currentDirection * m_speed);
        }
    }
}

std::string WanderBehavior::getName() const {
    return "Wander";
}

void WanderBehavior::setCenterPoint(const Vector2D& centerPoint) {
    m_centerPoint = centerPoint;

    // Estimate screen dimensions based on center point
    // (We'll assume the center is roughly in the middle of the screen)
    m_screenWidth = m_centerPoint.getX() * 2.0f;
    m_screenHeight = m_centerPoint.getY() * 2.0f;
}

void WanderBehavior::setAreaRadius(float radius) {
    m_areaRadius = radius;
}

void WanderBehavior::setSpeed(float speed) {
    m_speed = speed;
}

void WanderBehavior::setChangeDirectionInterval(float interval) {
    m_changeDirectionInterval = interval;
}

void WanderBehavior::setScreenDimensions(float width, float height) {
    m_screenWidth = width;
    m_screenHeight = height;
}

void WanderBehavior::setOffscreenProbability(float probability) {
    m_offscreenProbability = std::max(0.0f, std::min(1.0f, probability));
}

bool WanderBehavior::isWellOffscreen(const Vector2D& position) const {
    const float buffer = 100.0f; // Distance past the edge to consider "well offscreen"
    return position.getX() < -buffer ||
           position.getX() > m_screenWidth + buffer ||
           position.getY() < -buffer ||
           position.getY() > m_screenHeight + buffer;
}

void WanderBehavior::resetEntityPosition(Entity* entity) {
    if (!entity) return;

    // Calculate entry point on the opposite side of the screen
    Vector2D position = entity->getPosition();
    Vector2D newPosition(0.0f, 0.0f);

    // Determine which side to come in from (opposite of where the entity exited)
    if (position.getX() < 0) {
        // Went off left side, come in from right
        newPosition.setX(m_screenWidth - 50.0f);
        newPosition.setY(m_wanderOffscreenChance(m_rng) * m_screenHeight);
    } else if (position.getX() > m_screenWidth) {
        // Went off right side, come in from left
        newPosition.setX(50.0f);
        newPosition.setY(m_wanderOffscreenChance(m_rng) * m_screenHeight);
    } else if (position.getY() < 0) {
        // Went off top, come in from bottom
        newPosition.setX(m_wanderOffscreenChance(m_rng) * m_screenWidth);
        newPosition.setY(m_screenHeight - 50.0f);
    } else {
        // Went off bottom, come in from top
        newPosition.setX(m_wanderOffscreenChance(m_rng) * m_screenWidth);
        newPosition.setY(50.0f);
    }

    // Set new position and choose a new direction
    entity->setPosition(newPosition);
    chooseNewDirection(entity, false);
}

void WanderBehavior::chooseNewDirection(Entity* entity, bool wanderOffscreen) {
    if (!entity) return;

    // Track if we're currently wandering offscreen
    m_currentlyWanderingOffscreen = wanderOffscreen;

    if (wanderOffscreen) {
        // Start wandering toward edge of screen by picking a direction toward nearest edge
        Vector2D position = entity->getPosition();

        // Find closest edge and set direction toward it
        float distToLeft = position.getX();
        float distToRight = m_screenWidth - position.getX();
        float distToTop = position.getY();
        float distToBottom = m_screenHeight - position.getY();

        // Find minimum distance to edge
        float minDist = std::min({distToLeft, distToRight, distToTop, distToBottom});

        // Set direction toward closest edge
        if (minDist == distToLeft) {
            m_currentDirection = Vector2D(-1.0f, 0.0f);
        } else if (minDist == distToRight) {
            m_currentDirection = Vector2D(1.0f, 0.0f);
        } else if (minDist == distToTop) {
            m_currentDirection = Vector2D(0.0f, -1.0f);
        } else {
            m_currentDirection = Vector2D(0.0f, 1.0f);
        }

        // Add some randomness to the direction
        float randomAngle = (m_angleDistribution(m_rng) - M_PI) * 0.2f; // Small angle variation
        float x = m_currentDirection.getX() * std::cos(randomAngle) - m_currentDirection.getY() * std::sin(randomAngle);
        float y = m_currentDirection.getX() * std::sin(randomAngle) + m_currentDirection.getY() * std::cos(randomAngle);
        m_currentDirection = Vector2D(x, y);
        m_currentDirection.normalize();

        // Schedule a reset once we go offscreen
        m_resetScheduled = true;
    } else {
        // Generate a random angle
        float angle = m_angleDistribution(m_rng);

        // Convert angle to direction vector
        float x = std::cos(angle);
        float y = std::sin(angle);

        // Set the new direction
        m_currentDirection = Vector2D(x, y);
    }

    // Apply the new direction to the entity
    entity->setVelocity(m_currentDirection * m_speed);

    // Update flip direction based on movement
    if (m_currentDirection.getX() < 0) {
        entity->setFlip(SDL_FLIP_HORIZONTAL);
    } else {
        entity->setFlip(SDL_FLIP_NONE);
    }
}
