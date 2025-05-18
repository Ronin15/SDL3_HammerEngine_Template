/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PATROL_BEHAVIOR_HPP
#define PATROL_BEHAVIOR_HPP

#include "AIBehavior.hpp"
#include "Vector2D.hpp"
#include <vector>
#include <SDL3/SDL.h>

class PatrolBehavior : public AIBehavior {
public:
    PatrolBehavior(const std::vector<Vector2D>& waypoints, float moveSpeed = 2.0f)
        : m_waypoints(waypoints), m_currentWaypoint(0), m_moveSpeed(moveSpeed) {
        // Ensure we have at least two waypoints
        if (m_waypoints.size() < 2) {
            // Add a fallback waypoint if the list is too small
            m_waypoints.push_back(Vector2D(100, 100));
            m_waypoints.push_back(Vector2D(200, 200));
        }
    }

    void init(Entity* entity) override {
        if (!entity) return;

        // Set initial target to the first waypoint
        m_currentWaypoint = 0;

        // If entity is already near the first waypoint, move to the next one
        if (isAtWaypoint(entity->getPosition(), m_waypoints[m_currentWaypoint])) {
            m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
        }
    }

    void update(Entity* entity) override {
        if (!entity || !m_active || m_waypoints.empty()) return;

        // Safety check for valid waypoint index
        if (m_currentWaypoint >= m_waypoints.size()) {
            m_currentWaypoint = 0;
        }

        // Get current entity position
        Vector2D position = entity->getPosition();

        // Get the target waypoint
        Vector2D targetWaypoint = m_waypoints[m_currentWaypoint];

        // Check if we've reached the current waypoint
        if (isAtWaypoint(position, targetWaypoint)) {
            // Move to the next waypoint
            m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
            targetWaypoint = m_waypoints[m_currentWaypoint];
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
        entity->setVelocity(direction * m_moveSpeed);

        // Handle sprite flipping for visual direction
        if (direction.getX() < 0) {
            entity->setFlip(SDL_FLIP_HORIZONTAL);
        } else if (direction.getX() > 0) {
            entity->setFlip(SDL_FLIP_NONE);
        }
    }

    void clean(Entity* entity) override {
        if (!entity) return;

        // Stop the entity's movement when cleaning up
        entity->setVelocity(Vector2D(0, 0));
    }

    void onMessage(Entity* entity, const std::string& message) override {
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

    std::string getName() const override {
        return "Patrol";
    }

    // Add a new waypoint to the patrol route
    void addWaypoint(const Vector2D& waypoint) {
        m_waypoints.push_back(waypoint);
    }

    // Clear all waypoints and set new ones
    void setWaypoints(const std::vector<Vector2D>& waypoints) {
        if (waypoints.size() >= 2) {
            m_waypoints = waypoints;
            m_currentWaypoint = 0;
        }
    }

    // Get current waypoints
    const std::vector<Vector2D>& getWaypoints() const {
        return m_waypoints;
    }

    // Set movement speed
    void setMoveSpeed(float speed) {
        m_moveSpeed = speed;
    }

private:
    std::vector<Vector2D> m_waypoints;
    size_t m_currentWaypoint{0};
    float m_moveSpeed{2.0f};
    float m_waypointRadius{15.0f}; // How close entity needs to be to "reach" a waypoint

    // Check if entity has reached the current waypoint
    bool isAtWaypoint(const Vector2D& position, const Vector2D& waypoint) const {
        // Simple distance check
        Vector2D difference = position - waypoint;
        return difference.length() < m_waypointRadius;
    }

    // Reverse the order of waypoints
    void reverseWaypoints() {
        if (m_waypoints.size() < 2) return;

        std::reverse(m_waypoints.begin(), m_waypoints.end());

        // Adjust current waypoint index for the new order
        if (m_currentWaypoint > 0) {
            m_currentWaypoint = m_waypoints.size() - m_currentWaypoint;
        }
    }
};

#endif // PATROL_BEHAVIOR_HPP
