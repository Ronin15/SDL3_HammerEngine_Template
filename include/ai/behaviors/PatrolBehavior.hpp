/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef PATROL_BEHAVIOR_HPP
#define PATROL_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <boost/container/small_vector.hpp>
#include <SDL3/SDL.h>

class PatrolBehavior : public AIBehavior {
public:
    PatrolBehavior(const boost::container::small_vector<Vector2D, 10>& waypoints, float moveSpeed = 2.0f, bool includeOffscreenPoints = false);

    void init(EntityPtr entity) override;
    void update(EntityPtr entity) override;
    void clean(EntityPtr entity) override;
    void onMessage(EntityPtr entity, const std::string& message) override;
    std::string getName() const override;

    // Add a new waypoint to the patrol route
    void addWaypoint(const Vector2D& waypoint);

    // Clear all waypoints and set new ones
    void setWaypoints(const boost::container::small_vector<Vector2D, 10>& waypoints);

    // Enable or disable offscreen waypoints
    void setIncludeOffscreenPoints(bool include);

    // Set screen dimensions for offscreen detection
    void setScreenDimensions(float width, float height);

    // Get current waypoints
    const boost::container::small_vector<Vector2D, 10>& getWaypoints() const;

    // Set movement speed
    void setMoveSpeed(float speed);

private:
    boost::container::small_vector<Vector2D, 10> m_waypoints;
    size_t m_currentWaypoint{0};
    float m_moveSpeed{2.0f};
    float m_waypointRadius{25.0f}; // How close entity needs to be to "reach" a waypoint - increased from 15 to 25
    bool m_includeOffscreenPoints{false}; // Whether patrol route can include offscreen points
    bool m_needsReset{false}; // Flag to track if entity needs to be reset

    // Screen dimensions - defaults that will be updated by setScreenDimensions
    float m_screenWidth{1280.0f};
    float m_screenHeight{720.0f};

    // Check if entity has reached the current waypoint
    bool isAtWaypoint(const Vector2D& position, const Vector2D& waypoint) const;

    // Check if a position is offscreen
    bool isOffscreen(const Vector2D& position) const;

    // Check if entity is well off screen (completely out of view)
    bool isWellOffscreen(const Vector2D& position) const;

    // Reset entity to a new position on screen edge
    void resetEntityPosition(EntityPtr entity);

    // Reverse the order of waypoints
    void reverseWaypoints();
};

#endif // PATROL_BEHAVIOR_HPP
