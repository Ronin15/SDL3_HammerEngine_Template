/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef BEHAVIOR_CONFIG_HPP
#define BEHAVIOR_CONFIG_HPP

namespace HammerEngine
{

/**
 * Configuration for WanderBehavior
 *
 * Controls how entities wander around the world with boundary avoidance
 * and crowd awareness.
 */
struct WanderBehaviorConfig
{
    // Movement parameters
    float speed = 35.0f;                          // Base wandering speed in px/s

    // Direction change parameters
    float changeDirectionIntervalMin = 3000.0f;   // Minimum ms between direction changes
    float changeDirectionIntervalMax = 8000.0f;   // Maximum ms between direction changes

    // Edge avoidance
    float edgeThreshold = 50.0f;                  // Distance from world edge (px) to start avoiding
    float worldPaddingMargin = 256.0f;            // Safety margin when selecting new goals near edges

    // Crowd escape
    int crowdEscapeThreshold = 8;                 // Number of nearby entities to trigger escape
    float crowdEscapeDistanceMultiplier = 3.0f;   // Multiply wander distance by this when escaping crowds

    // Pathfinding parameters
    float pathRequestCooldown = 30.0f;            // Seconds between pathfinding requests (reduces load)
    float minGoalChangeDistance = 200.0f;         // Minimum distance change to justify new path request

    // Stuck detection
    float stallSpeed = 0.5f;                      // Speed threshold (px/s) to consider entity stalled
    float stallTimeout = 0.6f;                    // Seconds without progress before triggering unstuck
};

/**
 * Configuration for ChaseBehavior
 *
 * Controls how entities pursue and catch targets with pathfinding and
 * line-of-sight tracking.
 */
struct ChaseBehaviorConfig
{
    // Movement parameters
    float chaseSpeed = 60.0f;                     // Speed when actively chasing target

    // Pathfinding parameters
    float pathInvalidationDistance = 300.0f;      // Distance target must move to invalidate path
    float pathRefreshInterval = 12.0f;            // Seconds between path recalculations
    float pathRequestCooldown = 5.0f;             // Minimum seconds between path requests

    // Crowd awareness
    float crowdCheckInterval = 2.0f;              // Seconds between crowd density checks
    float lateralOffsetDistance = 48.0f;          // Perpendicular offset to reduce clumping

    // Stall recovery
    float stallSpeedMultiplier = 0.5f;            // Fraction of chase speed to trigger stall detection
    float stallTimeout = 2.0f;                    // Seconds stalled before recovery attempt
    float jitterAmount = 12.0f;                   // Random position offset when stuck (px)

    // Line-of-sight
    float losCheckInterval = 0.5f;                // Seconds between LOS checks
    float catchRadius = 20.0f;                    // Distance to consider target "caught"
};

/**
 * Configuration for PatrolBehavior
 *
 * Controls how entities patrol between waypoints with obstacle avoidance
 * and stuck recovery.
 */
struct PatrolBehaviorConfig
{
    // Movement parameters
    float moveSpeed = 40.0f;                      // Speed when patrolling between waypoints

    // Waypoint parameters
    float waypointReachedRadius = 32.0f;          // Distance to consider waypoint reached
    float waypointCooldown = 0.75f;               // Seconds to wait at waypoint before advancing
    int randomWaypointGenerationAttempts = 50;    // Max attempts to find valid random waypoint

    // Pathfinding parameters
    float pathRequestCooldown = 15.0f;            // Minimum seconds between path requests
    float pathRequestCooldownVariation = 3.0f;    // Random variation added to cooldown (0-3s)

    // Stall recovery
    float stallSpeedMultiplier = 0.3f;            // Fraction of move speed to trigger stall detection
    float sidestepDistance = 64.0f;               // Distance to sidestep when stalled
    float advanceWaypointDelay = 1.5f;            // Seconds stalled before skipping to next waypoint

    // Boundary padding
    float boundaryPadding = 80.0f;                // Keep patrol paths this far from world edges
};

/**
 * Configuration for FleeBehavior
 *
 * Controls how entities flee from threats using pathfinding to escape.
 */
struct FleeBehaviorConfig
{
    // Movement parameters
    float fleeSpeed = 70.0f;                      // Speed when fleeing from threat

    // Flee distance parameters
    float safeDistance = 400.0f;                  // Distance to flee to be considered "safe"
    float worldPadding = 80.0f;                   // Keep flee goals this far from world edges

    // Pathfinding parameters
    float pathTTL = 2.5f;                         // Path time-to-live in seconds
    float noProgressWindow = 0.4f;                // Seconds without progress before repath
    float goalChangeThreshold = 180.0f;           // Distance threat must move to trigger repath
};

/**
 * Configuration for FollowBehavior
 *
 * Controls how entities follow a leader with distance management.
 */
struct FollowBehaviorConfig
{
    // Movement parameters
    float followSpeed = 50.0f;                    // Speed when following leader
    float followDistance = 100.0f;                // Desired distance to maintain from leader

    // Catchup parameters
    float catchupRange = 200.0f;                  // Distance at which to slow down for stragglers

    // Pathfinding parameters
    float nodeRadius = 20.0f;                     // Waypoint reached radius
    float pathTTL = 10.0f;                        // Path validity duration
    float goalChangeThreshold = 200.0f;           // Distance leader must move to trigger repath

    // Stall recovery
    float stallSpeedMultiplier = 0.5f;            // Fraction of follow speed to trigger stall
    float stallTimeout = 0.6f;                    // Seconds stalled before recovery

    // Arrival
    float arrivalRadius = 25.0f;                  // Distance to consider arrived at leader
    float velocityThreshold = 10.0f;              // Speed threshold for arrival detection
};

/**
 * Configuration for GuardBehavior
 *
 * Controls how entities guard a position and return to it after threats.
 */
struct GuardBehaviorConfig
{
    // Movement parameters
    float guardSpeed = 45.0f;                     // Speed when returning to guard position

    // Guard parameters
    float guardRadius = 50.0f;                    // Radius around guard position to patrol
    float returnToGuardDelay = 3.0f;              // Seconds after threat before returning

    // Pathfinding parameters
    float pathTTL = 1.8f;                         // Path validity duration in seconds
    float goalChangeThreshold = 64.0f;            // Distance to trigger path recalculation

    // Stall recovery
    float stallSpeedMultiplier = 0.5f;            // Fraction of guard speed to trigger stall
};

// Note: AttackBehaviorConfig has been moved to include/ai/behaviors/AttackBehaviorConfig.hpp
// for more comprehensive configuration options

} // namespace HammerEngine

#endif // BEHAVIOR_CONFIG_HPP
