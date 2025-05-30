/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/WanderBehavior.hpp"

#include <algorithm>
#include <cmath>

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval, float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval), m_areaRadius(areaRadius), 
      m_minimumFlipInterval(400) {
    // Random number generators are already initialized in class definition
}

WanderBehavior::WanderBehavior(WanderMode mode, float speed)
    : m_speed(speed), m_minimumFlipInterval(400) {
    // Set up the behavior based on the mode
    setupModeDefaults(mode, m_screenWidth, m_screenHeight);
}

void WanderBehavior::init(EntityPtr entity) {
    if (!entity) return;

    // Store initial position as center point
    m_centerPoint = entity->getPosition();

    // Create entity state if it doesn't exist
    EntityWeakPtr entityWeak = entity;
    if (m_entityStates.find(entityWeak) == m_entityStates.end()) {
        m_entityStates[entityWeak] = EntityState{};
        
        // Generate a random start delay between 0 and 5000 milliseconds
        std::uniform_int_distribution<Uint64> delayDist(0, 5000);
        m_entityStates[entityWeak].startDelay = delayDist(m_rng);
        m_entityStates[entityWeak].movementStarted = false;
    }

    // Record start time for direction changes
    m_entityStates[entityWeak].lastDirectionChangeTime = SDL_GetTicks();

    // Set initial random direction but with zero velocity until delay expires
    chooseNewDirection(entity);
    if (m_entityStates[entityWeak].startDelay > 0) {
        // Set zero velocity until delay expires
        entity->setVelocity(Vector2D(0, 0));
    }
}

void WanderBehavior::executeLogic(EntityPtr entity) {
    if (!entity || !m_active) return;

    // Create entity state if it doesn't exist
    EntityWeakPtr entityWeak = entity;
    if (m_entityStates.find(entityWeak) == m_entityStates.end()) {
        m_entityStates[entityWeak] = EntityState{};
        m_entityStates[entityWeak].lastDirectionChangeTime = SDL_GetTicks();
        
        // Generate a random start delay between 0 and 5000 milliseconds
        std::uniform_int_distribution<Uint64> delayDist(0, 5000);
        m_entityStates[entityWeak].startDelay = delayDist(m_rng);
        m_entityStates[entityWeak].movementStarted = false;
        
        chooseNewDirection(entity);
        // Set zero velocity until delay expires
        entity->setVelocity(Vector2D(0, 0));
    }
    
    // Get entity-specific state
    EntityState& state = m_entityStates[entityWeak];

    // Get current time
    Uint64 currentTime = SDL_GetTicks();
    
    // Check if we need to wait for the start delay
    if (!state.movementStarted) {
        if (currentTime >= state.lastDirectionChangeTime + state.startDelay) {
            // Delay expired, start moving
            state.movementStarted = true;
            // Apply the initial direction with proper velocity
            entity->setVelocity(state.currentDirection * m_speed);
        } else {
            // Still waiting for delay to expire
            return;
        }
    }

    // Store the previous velocity for flip stability
    Vector2D previousVelocity = entity->getVelocity();

    // Check if it's time to change direction
    if (currentTime - state.lastDirectionChangeTime > m_changeDirectionInterval) {
        // Decide whether to wander offscreen or stay within bounds
        bool wanderOffscreen = !state.resetScheduled && m_wanderOffscreenChance(m_rng) < m_offscreenProbability;
        chooseNewDirection(entity, wanderOffscreen);
        state.lastDirectionChangeTime = currentTime;
    }

    // Check if entity is outside screen bounds
    Vector2D position = entity->getPosition();

    // If we've scheduled a reset and the entity is sufficiently offscreen
    if (state.resetScheduled) {
        // Check if entity is far enough off screen to reset
        if (isWellOffscreen(position)) {
            // Reset to a random position near the opposite side of the screen
            resetEntityPosition(entity);
            state.resetScheduled = false;
        }
    }
    else if (!state.currentlyWanderingOffscreen) {
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

            state.currentDirection = (state.currentDirection * (1.0f - blendFactor)) +
                                (returnDirection * blendFactor);
            state.currentDirection.normalize();

            // Apply the new direction
            entity->setVelocity(state.currentDirection * m_speed);
        }
    }

    // Control sprite flipping to avoid rapid flips
    // Only allow direction flips if enough time has passed since the last flip
    Vector2D currentVelocity = entity->getVelocity();
    
    // Check if there was a direction change that would cause a flip
    bool wouldFlip = (previousVelocity.getX() > 0.5f && currentVelocity.getX() < -0.5f) || 
                     (previousVelocity.getX() < -0.5f && currentVelocity.getX() > 0.5f);
    
    if (wouldFlip && (currentTime - state.lastDirectionFlip < m_minimumFlipInterval)) {
        // Prevent the flip by maintaining previous direction's sign but with new magnitude
        float magnitude = currentVelocity.length();
        float xDir = (previousVelocity.getX() < 0) ? -1.0f : 1.0f;
        float yVal = currentVelocity.getY();
        
        // Create a new direction that doesn't cause a flip
        Vector2D stableVelocity(xDir * magnitude * 0.8f, yVal);
        stableVelocity.normalize();
        stableVelocity = stableVelocity * m_speed;
        
        // Apply the stable velocity
        entity->setVelocity(stableVelocity);
    } else if (wouldFlip) {
        // Record the time of this flip
        state.lastDirectionFlip = currentTime;
    }
}

void WanderBehavior::clean(EntityPtr entity) {
    if (entity) {
        // Stop the entity when behavior is cleaned up
        entity->setVelocity(Vector2D(0, 0));
        
        // Remove entity state
        EntityWeakPtr entityWeak = entity;
        m_entityStates.erase(entityWeak);
    } else {
        // If entity is null, clean up all entity states
        m_entityStates.clear();
    }
}

void WanderBehavior::onMessage(EntityPtr entity, const std::string& message) {
    if (!entity) return;

    EntityWeakPtr entityWeak = entity;
    
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
        if (m_active && m_entityStates.find(entityWeak) != m_entityStates.end()) {
            entity->setVelocity(m_entityStates[entityWeak].currentDirection * m_speed);
        }
    } else if (message == "decrease_speed") {
        m_speed *= 0.75f;
        if (m_active && m_entityStates.find(entityWeak) != m_entityStates.end()) {
            entity->setVelocity(m_entityStates[entityWeak].currentDirection * m_speed);
        }
    } else if (message == "release_entities") {
        // Clear all entity state when asked to release entities
        entity->setVelocity(Vector2D(0, 0));
        // Clean up entity state for this specific entity
        m_entityStates.erase(entityWeak);
    }
}

std::string WanderBehavior::getName() const {
    return "Wander";
}

std::shared_ptr<AIBehavior> WanderBehavior::clone() const {
    auto cloned = std::make_shared<WanderBehavior>(m_speed, m_changeDirectionInterval, m_areaRadius);
    cloned->setCenterPoint(m_centerPoint);
    cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
    cloned->setOffscreenProbability(m_offscreenProbability);
    cloned->setActive(m_active);
    return cloned;
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

void WanderBehavior::resetEntityPosition(EntityPtr entity) {
    if (!entity) return;

    // Ensure entity state exists
    EntityWeakPtr entityWeak = entity;
    if (m_entityStates.find(entityWeak) == m_entityStates.end()) {
        m_entityStates[entityWeak] = EntityState{};
    }
    
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

void WanderBehavior::chooseNewDirection(EntityPtr entity, bool wanderOffscreen) {
    if (!entity) return;
    
    // Get entity-specific state
    EntityWeakPtr entityWeak = entity;
    EntityState& state = m_entityStates[entityWeak];
    
    // Track if we're currently wandering offscreen
    state.currentlyWanderingOffscreen = wanderOffscreen;

    // If movement hasn't started yet, just set the direction but don't apply velocity
    bool applyVelocity = state.movementStarted;

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
            state.currentDirection = Vector2D(-1.0f, 0.0f);
        } else if (minDist == distToRight) {
            state.currentDirection = Vector2D(1.0f, 0.0f);
        } else if (minDist == distToTop) {
            state.currentDirection = Vector2D(0.0f, -1.0f);
        } else {
            state.currentDirection = Vector2D(0.0f, 1.0f);
        }

        // Add some randomness to the direction
        float randomAngle = (m_angleDistribution(m_rng) - M_PI) * 0.2f; // Small angle variation
        float x = state.currentDirection.getX() * std::cos(randomAngle) - state.currentDirection.getY() * std::sin(randomAngle);
        float y = state.currentDirection.getX() * std::sin(randomAngle) + state.currentDirection.getY() * std::cos(randomAngle);
        state.currentDirection = Vector2D(x, y);
        state.currentDirection.normalize();

        // Schedule a reset once we go offscreen
        state.resetScheduled = true;
    } else {
        // Generate a random angle
        float angle = m_angleDistribution(m_rng);

        // Convert angle to direction vector
        float x = std::cos(angle);
        float y = std::sin(angle);

        // Set the new direction
        state.currentDirection = Vector2D(x, y);
    }

    // Apply the new direction to the entity only if movement has started
    if (applyVelocity) {
        entity->setVelocity(state.currentDirection * m_speed);
    }

    // NPC class now handles sprite flipping based on velocity
}

void WanderBehavior::setupModeDefaults(WanderMode mode, float screenWidth, float screenHeight) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    
    // Set center point to screen center by default
    m_centerPoint = Vector2D(screenWidth * 0.5f, screenHeight * 0.5f);
    
    switch (mode) {
        case WanderMode::SMALL_AREA:
            // Small wander area - personal space around position
            m_areaRadius = 75.0f;
            m_changeDirectionInterval = 1500.0f; // Change direction more frequently
            m_offscreenProbability = 0.05f; // Very low chance to go offscreen
            break;
            
        case WanderMode::MEDIUM_AREA:
            // Medium wander area - room/building sized
            m_areaRadius = 200.0f;
            m_changeDirectionInterval = 2500.0f; // Moderate direction changes
            m_offscreenProbability = 0.10f; // Low chance to go offscreen
            break;
            
        case WanderMode::LARGE_AREA:
            // Large wander area - village/district sized
            m_areaRadius = 450.0f;
            m_changeDirectionInterval = 3500.0f; // Less frequent direction changes
            m_offscreenProbability = 0.20f; // Higher chance to explore offscreen
            break;
            
        case WanderMode::EVENT_TARGET:
            // Wander around a specific target location (will be set later)
            m_areaRadius = 150.0f;
            m_changeDirectionInterval = 2000.0f; // Standard direction changes
            m_offscreenProbability = 0.05f; // Stay near the target
            break;
    }
}
