/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef PATROL_BEHAVIOR_HPP
#define PATROL_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <vector>

class PatrolBehavior : public AIBehavior {
public:
  enum class PatrolMode {
    FIXED_WAYPOINTS, // Use predefined waypoints (default behavior)
    RANDOM_AREA,     // Generate random waypoints within a rectangular area
    CIRCULAR_AREA,   // Generate random waypoints within a circular area
    EVENT_TARGET     // Generate waypoints around an event target
  };

  explicit PatrolBehavior(const std::vector<Vector2D> &waypoints,
                          float moveSpeed = 2.0f,
                          bool includeOffscreenPoints = false);

  // Constructor with mode - automatically configures behavior based on mode
  explicit PatrolBehavior(PatrolMode mode, float moveSpeed = 2.0f,
                          bool includeOffscreenPoints = false);

  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity, float deltaTime) override;
  void clean(EntityPtr entity) override;
  void onMessage(EntityPtr entity, const std::string &message) override;
  std::string getName() const override;

  // Add a new waypoint to the patrol route
  void addWaypoint(const Vector2D &waypoint);

  // Clear all waypoints and set new ones
  void setWaypoints(const std::vector<Vector2D> &waypoints);

  // Enable or disable offscreen waypoints
  void setIncludeOffscreenPoints(bool include);

  // Get current waypoints
  const std::vector<Vector2D> &getWaypoints() const;

  // Set movement speed
  void setMoveSpeed(float speed);

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;

  // Random area patrol methods
  void setRandomPatrolArea(const Vector2D &topLeft, const Vector2D &bottomRight,
                           int waypointCount = 5);
  void setRandomPatrolArea(const Vector2D &center, float radius,
                           int waypointCount = 5);

  // Event target patrol methods
  void setEventTarget(const Vector2D &target, float radius = 100.0f,
                      int waypointCount = 6);
  void updateEventTarget(const Vector2D &newTarget);

  // Utility methods
  void regenerateRandomWaypoints();
  PatrolMode getPatrolMode() const;
  void setAutoRegenerate(bool autoRegen);
  void setMinWaypointDistance(float distance);
  void setRandomSeed(unsigned int seed);



private:
  std::vector<Vector2D> m_waypoints;
  
  size_t m_currentWaypoint{0};
  float m_moveSpeed{2.0f};
  float m_waypointRadius{25.0f}; // How close entity needs to be to "reach" a
                                 // waypoint - increased from 15 to 25
  bool m_includeOffscreenPoints{
      false};               // Whether patrol route can include offscreen points
  bool m_needsReset{false}; // Flag to track if entity needs to be reset

  // Random patrol and event target system
  PatrolMode m_patrolMode{PatrolMode::FIXED_WAYPOINTS};

  // Random area patrol variables
  Vector2D m_areaTopLeft{0, 0};
  Vector2D m_areaBottomRight{0, 0};
  Vector2D m_areaCenter{0, 0};
  float m_areaRadius{0.0f};
  bool m_useCircularArea{false};
  int m_waypointCount{5};
  bool m_autoRegenerate{false};
  float m_minWaypointDistance{50.0f};

  // Event target patrol variables
  Vector2D m_eventTarget{0, 0};
  float m_eventTargetRadius{100.0f};

  // Random number generation
  mutable std::mt19937 m_rng;
  bool m_seedSet{false};

  // Check if entity has reached the current waypoint
  bool isAtWaypoint(const Vector2D &position, const Vector2D &waypoint) const;

  // Reset entity to a new position on screen edge
  void resetEntityPosition(EntityPtr entity);

  // Reverse the order of waypoints
  void reverseWaypoints();

  // Random waypoint generation helpers
  void generateRandomWaypointsInRectangle();
  void generateRandomWaypointsInCircle();
  void generateWaypointsAroundTarget();
  Vector2D generateRandomPointInRectangle() const;
  Vector2D generateRandomPointInCircle() const;
  bool isValidWaypointDistance(const Vector2D &newPoint) const;
  void ensureRandomSeed() const;

  // Adaptive area management for crowd control
  void expandPatrolAreaIfCrowded(const Vector2D& entityPos);
  bool isAreaOvercrowded(const Vector2D& center, float radius) const;
  float calculateOptimalAreaSize(const Vector2D& center) const;

  // Mode setup helper
  void setupModeDefaults(PatrolMode mode);

  // State validation and debugging
  bool validateState(EntityPtr entity) const;
  void resetState(EntityPtr entity);

  // Path-following settings
  void setPathFollowRadius(float r) { m_navRadius = r; }

  // Path-following state (uses AIManager's grid)
  std::vector<Vector2D> m_navPath;
  size_t m_navIndex{0};
  float m_navRadius{18.0f};

  // Per-instance progress and refresh tracking (fixes thread-local cross-entity interference)
  Uint64 m_lastPathUpdate{0};
  Uint64 m_lastProgressTime{0};
  float m_lastNodeDistance{std::numeric_limits<float>::infinity()};
  Uint64 m_stallStart{0};
  Uint64 m_backoffUntil{0};
  Uint64 m_lastWaypointTime{0}; // Prevent rapid waypoint switching
  Uint64 m_lastCrowdCheck{0}; // Timer for crowd density checks
  // Separation decimation
  Uint64 m_lastSepTick{0};
  Vector2D m_lastSepVelocity{0, 0};
  
  // Async pathfinding control
  // PATHFINDING CONSOLIDATION: Removed - all pathfinding now uses PathfindingScheduler
  // bool m_useAsyncPathfinding removed
};

#endif // PATROL_BEHAVIOR_HPP
