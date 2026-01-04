/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef FOLLOW_BEHAVIOR_HPP
#define FOLLOW_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "entities/Entity.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <mutex>
#include <random>
#include <vector>

class FollowBehavior : public AIBehavior {
public:
  enum class FollowMode : uint8_t {
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

  // Constructor with config
  explicit FollowBehavior(const HammerEngine::FollowBehaviorConfig& config,
                          FollowMode mode = FollowMode::LOOSE_FOLLOW);

  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
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
  // Entity state now stored in EDM BehaviorData (indexed by edmIndex)
  // No local m_entityStatesByIndex needed - eliminates sparse array memory waste

  // Configuration
  HammerEngine::FollowBehaviorConfig m_config;

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

  // Formation management (atomic for thread safety)
  static std::atomic<int> s_nextFormationSlot;
  static std::vector<Vector2D> s_escortFormationOffsets;
  static std::once_flag s_formationInitFlag;

  // Random number generation for formation variation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_offsetVariation{-10.0f,
                                                                  10.0f};

  // PATHFINDING CONSOLIDATION: Removed - all pathfinding now uses PathfindingScheduler
  // bool m_useAsyncPathfinding removed

  // Helper methods (all entity state stored in EDM BehaviorData)
  EntityHandle getTargetHandle() const; // Gets player handle from AIManager
  Vector2D calculateFormationOffset(const BehaviorData& data) const;

  bool shouldCatchUp(float distanceToTarget) const;
  float calculateFollowSpeed(float distanceToTarget) const;

  Vector2D smoothPath(const Vector2D &currentPos, const Vector2D &targetPos,
                      const BehaviorData& data) const;

  // Utility methods
  Vector2D normalizeVector(const Vector2D &vector) const;

  // OPTIMIZATION: Extracted lambda for better compiler optimization
  bool tryFollowPathToGoal(BehaviorContext& ctx, BehaviorData& data, const Vector2D& desiredPos, float speed);

  // Formation setup
  void initializeFormationOffsets();
  int assignFormationSlot();
  void releaseFormationSlot(int slot);
};

#endif // FOLLOW_BEHAVIOR_HPP
