/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CHASE_BEHAVIOR_HPP
#define CHASE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/Vector2D.hpp"

class ChaseBehavior : public AIBehavior {
public:
  explicit ChaseBehavior(const HammerEngine::ChaseBehaviorConfig& config = HammerEngine::ChaseBehaviorConfig{});

  // Legacy constructor for backward compatibility
  explicit ChaseBehavior(float chaseSpeed, float maxRange, float minRange);

  void init(EntityHandle handle) override;

  void executeLogic(BehaviorContext& ctx) override;

  void clean(EntityHandle handle) override;

  void onMessage(EntityHandle handle, const std::string &message) override;

  std::string getName() const override;

  // Get current target handle (player handle from AIManager)
  EntityHandle getTargetHandle() const;

  // Set chase parameters
  void setChaseSpeed(float speed);
  void setMaxRange(float range);
  void setMinRange(float range);
  void setUpdateFrequency(uint32_t frequency);

  // Get state information
  bool isChasing() const;
  bool hasLineOfSight() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;



protected:
  // Called when target is reached (within minimum range)
  virtual void onTargetReached(EntityHandle handle);

  // Called when target is lost (out of max range)
  virtual void onTargetLost(EntityHandle handle);

private:
  // Configuration (per-template, not per-entity)
  HammerEngine::ChaseBehaviorConfig m_config;

  // Note: Target is now obtained via AIManager::getPlayerReference()
  float m_chaseSpeed{10.0f}; // Increased to 10.0 for very visible movement
  float m_maxRange{1000.0f}; // Maximum distance to chase target
  float m_minRange{50.0f};   // Minimum distance to maintain from target
  float m_navRadius{18.0f};  // Waypoint arrival threshold
  int m_maxTimeWithoutSight{60}; // Frames to chase last known position

  // All per-entity runtime state is now stored in EDM:
  // - BehaviorData.state.chase: isChasing, hasLineOfSight, lastKnownTargetPos, timeWithoutSight,
  //                             currentDirection, cooldowns, etc.
  // - PathData: navPath, navIndex, stallTimer, progressTimer, pathUpdateTimer
  // - BehaviorData common: lastCrowdAnalysis, cachedNearbyCount, cachedClusterCenter

  // Check if entity has line of sight to target position
  bool checkLineOfSight(EntityHandle handle, const Vector2D& targetPos) const;

  // Handle behavior when line of sight is lost
  void handleNoLineOfSight(EntityHandle handle, BehaviorData& data);

  // Cooldown helper - uses EDM chase state
  bool canRequestPath(const BehaviorData& data) const;
  void applyPathCooldown(BehaviorData& data, float cooldownSeconds = 0.6f);
  void updateCooldowns(BehaviorData& data, float deltaTime);

public:

};

#endif // CHASE_BEHAVIOR_HPP
