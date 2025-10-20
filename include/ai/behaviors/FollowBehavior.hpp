/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef FOLLOW_BEHAVIOR_HPP
#define FOLLOW_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <unordered_map>
#include <vector>

class FollowBehavior : public AIBehavior {
public:
  enum class FollowMode {
    CLOSE_FOLLOW,    // Stay very close to target
    LOOSE_FOLLOW,    // Maintain some distance
    FLANKING_FOLLOW, // Follow from the sides
    REAR_GUARD,      // Follow from behind
    ESCORT_FORMATION // Maintain formation around target
  };

  explicit FollowBehavior(float followSpeed = 2.5f,
                          float followDistance = 100.0f,
                          float maxDistance = 300.0f);

  // Constructor with mode
  explicit FollowBehavior(FollowMode mode, float followSpeed = 2.5f);

  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity, float deltaTime) override;
  void clean(EntityPtr entity) override;
  void onMessage(EntityPtr entity, const std::string &message) override;
  std::string getName() const override;

  // Configuration methods
  void setFollowSpeed(float speed);
  void setFollowDistance(float distance);
  void setMaxDistance(float maxDistance);
  void setFollowMode(FollowMode mode);
  void setCatchUpSpeed(float speedMultiplier); // Speed boost when far behind
  void setFormationOffset(const Vector2D &offset); // For formation following

  // Pathfinding and obstacle avoidance
  void setAvoidanceRadius(float radius);
  void setPathSmoothing(bool enabled);
  void setMaxTurnRate(float degreesPerSecond);

  // Behavior control
  void setStopWhenTargetStops(bool stopWhenTargetStops);
  void setMinimumMovementThreshold(float threshold);
  void setPredictiveFollowing(bool enabled, float predictionTime = 0.5f);

  // State queries
  bool isFollowing() const;
  bool isInFormation() const;
  float getDistanceToTarget() const;
  FollowMode getFollowMode() const;
  Vector2D getTargetPosition() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;


private:
  
  struct EntityState {
    Vector2D lastTargetPosition{0, 0};
    Vector2D currentVelocity{0, 0};
    Vector2D desiredPosition{0, 0};
    Vector2D formationOffset{0, 0};
    float currentSpeed{0.0f};
    float currentHeading{0.0f}; // In radians
    bool isFollowing{false};
    bool targetMoving{false};
    bool inFormation{true};
    bool isStopped{false}; // Track if stopped at personal space boundary
    int formationSlot{0}; // For escort formation

    // Pathfinding state (using deltaTime accumulators instead of SDL_GetTicks)
    std::vector<Vector2D> pathPoints;
    size_t currentPathIndex{0};
    float pathUpdateTimer{0.0f};     // Replaces lastPathUpdate
    float lastNodeDistance{std::numeric_limits<float>::infinity()};
    float progressTimer{0.0f};       // Replaces lastProgressTime
    float backoffTimer{0.0f};        // Replaces backoffUntil (counts down)
    // Separation decimation (stores separation FORCE, not velocity)
    float separationTimer{0.0f};     // Replaces lastSepTick
    Vector2D lastSepForce{0, 0};

    EntityState()
        : lastTargetPosition(0, 0), currentVelocity(0, 0),
          desiredPosition(0, 0), formationOffset(0, 0), currentSpeed(0.0f), currentHeading(0.0f),
          isFollowing(false), targetMoving(false), inFormation(true), isStopped(false),
          formationSlot(0), currentPathIndex(0), pathUpdateTimer(0.0f),
          lastNodeDistance(std::numeric_limits<float>::infinity()), progressTimer(0.0f), backoffTimer(0.0f),
          separationTimer(0.0f) {}
  };

  // Map to store per-entity state
  std::unordered_map<EntityPtr, EntityState> m_entityStates;

  // Behavior parameters
  FollowMode m_followMode{FollowMode::LOOSE_FOLLOW};
  float m_followSpeed{2.5f};
  float m_stopDistance{40.0f};          // Minimum distance - stop moving when this close
  float m_resumeDistance{55.0f};        // Distance before resuming movement (prevents jitter)
  float m_followDistance{100.0f};       // Preferred distance from target
  float m_maxDistance{300.0f};          // Maximum distance before catch-up
  float m_catchUpSpeedMultiplier{1.5f}; // Speed boost when catching up

  // Formation and positioning
  Vector2D m_formationOffset{0, 0}; // Custom formation offset
  float m_formationRadius{80.0f};   // Radius for escort formation

  // Movement parameters
  float m_avoidanceRadius{30.0f};         // Radius for obstacle avoidance
  float m_maxTurnRate{180.0f};            // Degrees per second
  float m_minimumMovementThreshold{5.0f}; // Minimum target movement to follow
  bool m_pathSmoothing{true};
  bool m_stopWhenTargetStops{true};

  // Predictive following
  bool m_predictiveFollowing{false};
  float m_predictionTime{0.5f}; // Seconds to predict ahead

  // Timing parameters
  Uint64 m_stationaryThreshold{
      1000}; // Milliseconds before considering target stationary

  // Formation management
  static int s_nextFormationSlot;
  static std::vector<Vector2D> s_escortFormationOffsets;

  // Random number generation for formation variation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_offsetVariation{-10.0f,
                                                                  10.0f};

  // PATHFINDING CONSOLIDATION: Removed - all pathfinding now uses PathfindingScheduler
  // bool m_useAsyncPathfinding removed

  // Helper methods
  EntityPtr getTarget() const; // Gets player reference from AIManager
  Vector2D calculateDesiredPosition(EntityPtr entity, EntityPtr target,
                                    const EntityState &state) const;
  Vector2D calculateFormationOffset(const EntityState &state) const;
  Vector2D predictTargetPosition(EntityPtr target,
                                 const EntityState &state) const;

  bool isTargetMoving(EntityPtr target) const;
  bool shouldCatchUp(float distanceToTarget) const;
  float calculateFollowSpeed(EntityPtr entity, const EntityState &state,
                             float distanceToTarget) const;

  Vector2D avoidObstacles(EntityPtr entity,
                          const Vector2D &desiredVelocity) const;
  Vector2D smoothPath(const Vector2D &currentPos, const Vector2D &targetPos,
                      const EntityState &state) const;

  // Mode-specific updates
  void updateCloseFollow(EntityPtr entity, EntityState &state);
  void updateLooseFollow(EntityPtr entity, EntityState &state);
  void updateFlankingFollow(EntityPtr entity, EntityState &state);
  void updateRearGuard(EntityPtr entity, EntityState &state);
  void updateEscortFormation(EntityPtr entity, EntityState &state);

  // Utility methods
  Vector2D normalizeVector(const Vector2D &vector) const;

  // Formation setup
  void initializeFormationOffsets();
  int assignFormationSlot();
  void releaseFormationSlot(int slot);
};

#endif // FOLLOW_BEHAVIOR_HPP
