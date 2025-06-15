/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "ai/behaviors/PatrolBehavior.hpp"
#include "entities/Entity.hpp"
#include "entities/NPC.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

PatrolBehavior::PatrolBehavior(const std::vector<Vector2D>& waypoints, float moveSpeed, bool includeOffscreenPoints)
    : m_waypoints(waypoints),
      m_currentWaypoint(0),
      m_moveSpeed(moveSpeed),
      m_waypointRadius(25.0f),
      m_includeOffscreenPoints(includeOffscreenPoints),
      m_needsReset(false),
      m_screenWidth(1280.0f),
      m_screenHeight(720.0f),
      m_rng(std::random_device{}()) {
    // Reserve capacity for typical patrol routes (performance optimization)
    m_waypoints.reserve(10);

    // Ensure we have at least two waypoints
    if (m_waypoints.size() < 2) {
        // Add a fallback waypoint if the list is too small
        m_waypoints.push_back(Vector2D(100, 100));
        m_waypoints.push_back(Vector2D(200, 200));
    }
}

PatrolBehavior::PatrolBehavior(PatrolMode mode, float moveSpeed, bool includeOffscreenPoints)
    : m_currentWaypoint(0),
      m_moveSpeed(moveSpeed),
      m_waypointRadius(25.0f),
      m_includeOffscreenPoints(includeOffscreenPoints),
      m_needsReset(false),
      m_screenWidth(1280.0f),
      m_screenHeight(720.0f),
      m_rng(std::random_device{}()) {
    // Set up the behavior based on the mode
    setupModeDefaults(mode, m_screenWidth, m_screenHeight);
}

void PatrolBehavior::init(EntityPtr entity) {
    if (!entity) return;

    // Set initial target to the first waypoint
    m_currentWaypoint = 0;

    // If entity is already near the first waypoint, move to the next one
    if (isAtWaypoint(entity->getPosition(), m_waypoints[m_currentWaypoint])) {
        m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
    }

    // No direction tracking needed

    // Disable bounds checking when off-screen movement is allowed
    // Check if entity is an NPC that supports bounds checking control
    NPC* npc = dynamic_cast<NPC*>(entity.get());
    if (npc) {
        npc->setBoundsCheckEnabled(!m_includeOffscreenPoints);
    }
}

void PatrolBehavior::executeLogic(EntityPtr entity) {
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

        // Check if we've completed a full cycle and need to regenerate waypoints
        if (m_currentWaypoint == 0 && m_autoRegenerate && m_patrolMode != PatrolMode::FIXED_WAYPOINTS) {
            regenerateRandomWaypoints();
        }

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

    // NPC class now handles sprite flipping based on velocity
}

void PatrolBehavior::clean(EntityPtr entity) {
    if (entity) {
        // Stop the entity's movement when cleaning up
        entity->setVelocity(Vector2D(0, 0));

        // Re-enable bounds checking when behavior is cleaned up
        NPC* npc = dynamic_cast<NPC*>(entity.get());
        if (npc) {
            npc->setBoundsCheckEnabled(true);
        }
    }

    // Reset internal state
    m_needsReset = false;
}

void PatrolBehavior::onMessage(EntityPtr entity, const std::string& message) {
    if (message == "pause") {
        setActive(false);
        if (entity) {
            entity->setVelocity(Vector2D(0, 0));
        }
    } else if (message == "resume") {
        setActive(true);
    } else if (message == "reverse") {
        reverseWaypoints();
    } else if (message == "release_entities") {
        // Stop the entity and clean up when asked to release entities
        if (entity) {
            entity->setVelocity(Vector2D(0, 0));

            // Re-enable bounds checking
            NPC* npc = dynamic_cast<NPC*>(entity.get());
            if (npc) {
                npc->setBoundsCheckEnabled(true);
            }
        }

        // Reset internal state
        m_needsReset = false;
    }
}

std::string PatrolBehavior::getName() const {
    return "Patrol";
}

std::shared_ptr<AIBehavior> PatrolBehavior::clone() const {
    auto cloned = std::make_shared<PatrolBehavior>(m_waypoints, m_moveSpeed, m_includeOffscreenPoints);
    cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
    cloned->setActive(m_active);

    // Copy patrol mode and related settings
    cloned->m_patrolMode = m_patrolMode;
    cloned->m_areaTopLeft = m_areaTopLeft;
    cloned->m_areaBottomRight = m_areaBottomRight;
    cloned->m_areaCenter = m_areaCenter;
    cloned->m_areaRadius = m_areaRadius;
    cloned->m_useCircularArea = m_useCircularArea;
    cloned->m_waypointCount = m_waypointCount;
    cloned->m_autoRegenerate = m_autoRegenerate;
    cloned->m_minWaypointDistance = m_minWaypointDistance;
    cloned->m_eventTarget = m_eventTarget;
    cloned->m_eventTargetRadius = m_eventTargetRadius;

    return cloned;
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

void PatrolBehavior::resetEntityPosition(EntityPtr entity) {
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

// Random area patrol methods
void PatrolBehavior::setRandomPatrolArea(const Vector2D& topLeft, const Vector2D& bottomRight, int waypointCount) {
    m_patrolMode = PatrolMode::RANDOM_AREA;
    m_useCircularArea = false;
    m_areaTopLeft = topLeft;
    m_areaBottomRight = bottomRight;
    m_waypointCount = waypointCount;
    m_currentWaypoint = 0;

    generateRandomWaypointsInRectangle();
}

void PatrolBehavior::setRandomPatrolArea(const Vector2D& center, float radius, int waypointCount) {
    m_patrolMode = PatrolMode::RANDOM_AREA;
    m_useCircularArea = true;
    m_areaCenter = center;
    m_areaRadius = radius;
    m_waypointCount = waypointCount;
    m_currentWaypoint = 0;

    generateRandomWaypointsInCircle();
}

// Event target patrol methods
void PatrolBehavior::setEventTarget(const Vector2D& target, float radius, int waypointCount) {
    m_patrolMode = PatrolMode::EVENT_TARGET;
    m_eventTarget = target;
    m_eventTargetRadius = radius;
    m_waypointCount = waypointCount;
    m_currentWaypoint = 0;

    generateWaypointsAroundTarget();
}

void PatrolBehavior::updateEventTarget(const Vector2D& newTarget) {
    if (m_patrolMode == PatrolMode::EVENT_TARGET) {
        m_eventTarget = newTarget;
        generateWaypointsAroundTarget();
        m_currentWaypoint = 0; // Reset to first waypoint
    }
}

// Utility methods
void PatrolBehavior::regenerateRandomWaypoints() {
    if (m_patrolMode == PatrolMode::RANDOM_AREA) {
        if (m_useCircularArea) {
            generateRandomWaypointsInCircle();
        } else {
            generateRandomWaypointsInRectangle();
        }
    } else if (m_patrolMode == PatrolMode::EVENT_TARGET) {
        generateWaypointsAroundTarget();
    }
    m_currentWaypoint = 0;
}

PatrolBehavior::PatrolMode PatrolBehavior::getPatrolMode() const {
    return m_patrolMode;
}

void PatrolBehavior::setAutoRegenerate(bool autoRegen) {
    m_autoRegenerate = autoRegen;
}

void PatrolBehavior::setMinWaypointDistance(float distance) {
    m_minWaypointDistance = distance;
}

void PatrolBehavior::setRandomSeed(unsigned int seed) {
    m_rng.seed(seed);
    m_seedSet = true;
}

// Private helper methods
void PatrolBehavior::generateRandomWaypointsInRectangle() {
    ensureRandomSeed();
    m_waypoints.clear();

    // Generate waypoints with minimum distance constraints
    for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
        Vector2D newPoint;
        int attempts = 0;
        const int maxAttempts = 50;

        do {
            newPoint = generateRandomPointInRectangle();
            attempts++;
        } while (!isValidWaypointDistance(newPoint) && attempts < maxAttempts);

        m_waypoints.push_back(newPoint);
    }

    // Ensure we have at least 2 waypoints
    if (m_waypoints.size() < 2) {
        Vector2D center = (m_areaTopLeft + m_areaBottomRight) * 0.5f;
        Vector2D size = m_areaBottomRight - m_areaTopLeft;
        m_waypoints.clear();
        m_waypoints.push_back(center + Vector2D(-size.getX() * 0.25f, -size.getY() * 0.25f));
        m_waypoints.push_back(center + Vector2D(size.getX() * 0.25f, size.getY() * 0.25f));
    }
}

void PatrolBehavior::generateRandomWaypointsInCircle() {
    ensureRandomSeed();
    m_waypoints.clear();

    // Generate waypoints with minimum distance constraints
    for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
        Vector2D newPoint;
        int attempts = 0;
        const int maxAttempts = 50;

        do {
            newPoint = generateRandomPointInCircle();
            attempts++;
        } while (!isValidWaypointDistance(newPoint) && attempts < maxAttempts);

        m_waypoints.push_back(newPoint);
    }

    // Ensure we have at least 2 waypoints
    if (m_waypoints.size() < 2) {
        m_waypoints.clear();
        m_waypoints.push_back(m_areaCenter + Vector2D(-m_areaRadius * 0.5f, 0));
        m_waypoints.push_back(m_areaCenter + Vector2D(m_areaRadius * 0.5f, 0));
    }
}

void PatrolBehavior::generateWaypointsAroundTarget() {
    ensureRandomSeed();
    m_waypoints.clear();

    // Generate waypoints in a circle around the target
    float angleStep = 2.0f * M_PI / m_waypointCount;

    for (int i = 0; i < m_waypointCount && m_waypoints.size() < 10; ++i) {
        float angle = i * angleStep;

        // Add some randomness to the radius (between 0.7 and 1.0 of target radius)
        std::uniform_real_distribution<float> radiusDist(0.7f, 1.0f);
        float randomRadius = m_eventTargetRadius * radiusDist(m_rng);

        Vector2D waypoint = m_eventTarget + Vector2D(
            std::cos(angle) * randomRadius,
            std::sin(angle) * randomRadius
        );

        m_waypoints.push_back(waypoint);
    }

    // Ensure we have at least 2 waypoints
    if (m_waypoints.size() < 2) {
        m_waypoints.clear();
        m_waypoints.push_back(m_eventTarget + Vector2D(-m_eventTargetRadius, 0));
        m_waypoints.push_back(m_eventTarget + Vector2D(m_eventTargetRadius, 0));
    }
}

Vector2D PatrolBehavior::generateRandomPointInRectangle() const {
    std::uniform_real_distribution<float> xDist(m_areaTopLeft.getX(), m_areaBottomRight.getX());
    std::uniform_real_distribution<float> yDist(m_areaTopLeft.getY(), m_areaBottomRight.getY());

    return Vector2D(xDist(m_rng), yDist(m_rng));
}

Vector2D PatrolBehavior::generateRandomPointInCircle() const {
    // Generate random point in circle using polar coordinates
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
    std::uniform_real_distribution<float> radiusDist(0.0f, 1.0f);

    float angle = angleDist(m_rng);
    float radius = std::sqrt(radiusDist(m_rng)) * m_areaRadius; // sqrt for uniform distribution

    return m_areaCenter + Vector2D(
        std::cos(angle) * radius,
        std::sin(angle) * radius
    );
}

bool PatrolBehavior::isValidWaypointDistance(const Vector2D& newPoint) const {
    return std::all_of(m_waypoints.begin(), m_waypoints.end(),
        [this, &newPoint](const Vector2D& existingPoint) {
            Vector2D diff = newPoint - existingPoint;
            return diff.length() >= m_minWaypointDistance;
        });
}

void PatrolBehavior::ensureRandomSeed() const {
    if (!m_seedSet) {
        // Use a more deterministic seed for testing, but still random
        m_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    }
}

void PatrolBehavior::setupModeDefaults(PatrolMode mode, float screenWidth, float screenHeight) {
    m_patrolMode = mode;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    switch (mode) {
        case PatrolMode::FIXED_WAYPOINTS:
            // Create default fixed waypoints if none exist
            if (m_waypoints.empty()) {
                float margin = 100.0f;
                m_waypoints.push_back(Vector2D(margin, margin));
                m_waypoints.push_back(Vector2D(screenWidth - margin, margin));
                m_waypoints.push_back(Vector2D(screenWidth - margin, screenHeight - margin));
                m_waypoints.push_back(Vector2D(margin, screenHeight - margin));
            }
            break;

        case PatrolMode::RANDOM_AREA:
            // Set up random rectangular area in left half of screen
            m_useCircularArea = false;
            m_areaTopLeft = Vector2D(50, 50);
            m_areaBottomRight = Vector2D(screenWidth * 0.4f, screenHeight - 50);
            m_waypointCount = 6;
            m_autoRegenerate = true;
            m_minWaypointDistance = 80.0f;
            generateRandomWaypointsInRectangle();
            break;

        case PatrolMode::EVENT_TARGET:
            // Set up event target at screen center
            m_eventTarget = Vector2D(screenWidth * 0.5f, screenHeight * 0.5f);
            m_eventTargetRadius = 150.0f;
            m_waypointCount = 8;
            generateWaypointsAroundTarget();
            break;

        case PatrolMode::CIRCULAR_AREA:
            // Set up circular area in right half of screen
            m_useCircularArea = true;
            m_areaCenter = Vector2D(screenWidth * 0.75f, screenHeight * 0.5f);
            m_areaRadius = 120.0f;
            m_waypointCount = 5;
            m_autoRegenerate = true;
            m_minWaypointDistance = 60.0f;
            generateRandomWaypointsInCircle();
            break;
    }
}
