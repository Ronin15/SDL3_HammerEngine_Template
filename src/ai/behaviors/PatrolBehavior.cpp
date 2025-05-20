/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/PatrolBehavior.hpp"
#include "SDL3/SDL_surface.h"
#include "entities/Entity.hpp"
#include "entities/NPC.hpp"
#include <algorithm>

PatrolBehavior::PatrolBehavior(const std::vector<Vector2D>& waypoints, float moveSpeed, bool includeOffscreenPoints)
    : m_waypoints(waypoints), m_currentWaypoint(0), m_moveSpeed(moveSpeed), m_includeOffscreenPoints(includeOffscreenPoints) {
    // Ensure we have at least two waypoints
    if (m_waypoints.size() < 2) {
        // Add a fallback waypoint if the list is too small
        m_waypoints.push_back(Vector2D(100, 100));
        m_waypoints.push_back(Vector2D(200, 200));
    }
}

void PatrolBehavior::init(Entity* entity) {
    if (!entity) return;

    // Set initial target to the first waypoint
    m_currentWaypoint = 0;

    // If entity is already near the first waypoint, move to the next one
    if (isAtWaypoint(entity->getPosition(), m_waypoints[m_currentWaypoint])) {
        m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
    }
    
    // Disable bounds checking when off-screen movement is allowed
    // Check if entity is an NPC that supports bounds checking control
    NPC* npc = dynamic_cast<NPC*>(entity);
    if (npc) {
        npc->setBoundsCheckEnabled(!m_includeOffscreenPoints);
    }
}

void PatrolBehavior::update(Entity* entity) {
    if (!entity || !m_active || m_waypoints.empty()) {
        return;
    }

    // Safety check for valid waypoint index
    if (m_currentWaypoint >= m_waypoints.size()) {
        m_currentWaypoint = 0;
    }

    // Get current entity position
    Vector2D position = entity->getPosition();

    // Check if entity is well offscreen and needs to be reset
    if (m_needsReset && isWellOffscreen(position)) {
        resetEntityPosition(entity);
        m_needsReset = false;
        return;
    }

    // Get the target waypoint
    Vector2D targetWaypoint = m_waypoints[m_currentWaypoint];

    // Check if we've reached the current waypoint
    if (isAtWaypoint(position, targetWaypoint)) {
        // Move to the next waypoint
        m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
        targetWaypoint = m_waypoints[m_currentWaypoint];

        // If next waypoint is offscreen, mark for reset when entity goes offscreen
        if (m_includeOffscreenPoints && isOffscreen(targetWaypoint)) {
            m_needsReset = true;
        }
    }

    // Get the direction to the current waypoint
    Vector2D direction = targetWaypoint - position;

    // Normalize direction if not zero
    float length = direction.length();
    if (length > 0.1f) {
        direction = direction * (1.0f / length);
    } else {
        // We're very close to target, move to next waypoint
        m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
        targetWaypoint = m_waypoints[m_currentWaypoint];
        direction = targetWaypoint - position;
        direction = direction * (1.0f / direction.length());
    }

    // Set entity velocity based on direction and speed
    Vector2D newVelocity = direction * m_moveSpeed;
    entity->setVelocity(newVelocity);

    // Handle sprite flipping for visual direction
    if (direction.getX() < 0) {
        entity->setFlip(SDL_FLIP_NONE);
    } else if (direction.getX() > 0) {
        entity->setFlip(SDL_FLIP_HORIZONTAL);
    }
}

void PatrolBehavior::clean(Entity* entity) {
    if (!entity) return;

    // Stop the entity's movement when cleaning up
    entity->setVelocity(Vector2D(0, 0));
    
    // Re-enable bounds checking when behavior is cleaned up
    NPC* npc = dynamic_cast<NPC*>(entity);
    if (npc) {
        npc->setBoundsCheckEnabled(true);
    }
}

void PatrolBehavior::onMessage(Entity* entity, const std::string& message) {
    (void)entity; // Mark parameter as intentionally unused

    if (message == "pause") {
        setActive(false);
        if (entity) {
            entity->setVelocity(Vector2D(0, 0));
        }
    } else if (message == "resume") {
        setActive(true);
    } else if (message == "reverse") {
        reverseWaypoints();
    }
}

std::string PatrolBehavior::getName() const {
    return "Patrol";
}

void PatrolBehavior::addWaypoint(const Vector2D& waypoint) {
    m_waypoints.push_back(waypoint);
}

void PatrolBehavior::setWaypoints(const std::vector<Vector2D>& waypoints) {
    if (waypoints.size() >= 2) {
        m_waypoints = waypoints;
        m_currentWaypoint = 0;
    }
}

void PatrolBehavior::setIncludeOffscreenPoints(bool include) {
    m_includeOffscreenPoints = include;
}

void PatrolBehavior::setScreenDimensions(float width, float height) {
    m_screenWidth = width;
    m_screenHeight = height;
}

const std::vector<Vector2D>& PatrolBehavior::getWaypoints() const {
    return m_waypoints;
}

void PatrolBehavior::setMoveSpeed(float speed) {
    m_moveSpeed = speed;
}

bool PatrolBehavior::isAtWaypoint(const Vector2D& position, const Vector2D& waypoint) const {
    // Simple distance check
    Vector2D difference = position - waypoint;
    return difference.length() < m_waypointRadius;
}

bool PatrolBehavior::isOffscreen(const Vector2D& position) const {
    return position.getX() < 0 ||
           position.getX() > m_screenWidth ||
           position.getY() < 0 ||
           position.getY() > m_screenHeight;
}

bool PatrolBehavior::isWellOffscreen(const Vector2D& position) const {
    const float buffer = 100.0f; // Distance past the edge to consider "well offscreen"
    return position.getX() < -buffer ||
           position.getX() > m_screenWidth + buffer ||
           position.getY() < -buffer ||
           position.getY() > m_screenHeight + buffer;
}

void PatrolBehavior::resetEntityPosition(Entity* entity) {
    if (!entity) return;

    // Find an onscreen waypoint to teleport to
    for (size_t i = 0; i < m_waypoints.size(); i++) {
        size_t index = (m_currentWaypoint + i) % m_waypoints.size();
        if (!isOffscreen(m_waypoints[index])) {
            // Found an onscreen waypoint, teleport to it
            entity->setPosition(m_waypoints[index]);
            m_currentWaypoint = (index + 1) % m_waypoints.size(); // Move to next waypoint
            return;
        }
    }

    // If no onscreen waypoints found, reset to center of screen
    entity->setPosition(Vector2D(m_screenWidth / 2, m_screenHeight / 2));
}

void PatrolBehavior::reverseWaypoints() {
    if (m_waypoints.size() < 2) return;

    std::reverse(m_waypoints.begin(), m_waypoints.end());

    // Adjust current waypoint index for the new order
    if (m_currentWaypoint > 0) {
        m_currentWaypoint = m_waypoints.size() - m_currentWaypoint;
    }
}
