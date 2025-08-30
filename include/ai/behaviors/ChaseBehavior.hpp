/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CHASE_BEHAVIOR_HPP
#define CHASE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <vector>

class ChaseBehavior : public AIBehavior {
public:
  explicit ChaseBehavior(float chaseSpeed = 3.0f, float maxRange = 500.0f,
                         float minRange = 50.0f);

  void init(EntityPtr entity) override;

  void executeLogic(EntityPtr entity) override;

  void clean(EntityPtr entity) override;

  void onMessage(EntityPtr entity, const std::string &message) override;

  std::string getName() const override;

  // Get current target (returns AIManager::getPlayerReference())
  EntityPtr getTarget() const;

  // Set chase parameters
  void setChaseSpeed(float speed);
  void setMaxRange(float range);
  void setMinRange(float range);
  void setUpdateFrequency(uint32_t frequency); // Configure staggering frequency

  // Get state information
  bool isChasing() const;
  bool hasLineOfSight() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;

  

protected:
  // Called when target is reached (within minimum range)
  virtual void onTargetReached(EntityPtr entity);

  // Called when target is lost (out of max range)
  virtual void onTargetLost(EntityPtr entity);

private:
  // Note: Target is now obtained via AIManager::getPlayerReference()
  float m_chaseSpeed{10.0f}; // Increased to 10.0 for very visible movement
  float m_maxRange{
      1000.0f}; // Maximum distance to chase target - increased to 1000
  float m_minRange{50.0f}; // Minimum distance to maintain from target

  bool m_isChasing{false};
  bool m_hasLineOfSight{false};
  Vector2D m_lastKnownTargetPos{0, 0};
  int m_timeWithoutSight{0};
  const int m_maxTimeWithoutSight{60}; // Frames to chase last known position
  Vector2D m_currentDirection{0, 0};

  // Performance optimization: cache player reference to avoid repeated
  // AIManager lookups
  mutable EntityPtr m_cachedPlayerTarget{nullptr};
  mutable bool m_playerCacheValid{false};

  

  // Get cached player reference - optimized for 60fps calls
  EntityPtr getCachedPlayerTarget() const;

  // Invalidate player cache (called on behavior switch or player changes)
  void invalidatePlayerCache() const;

  // Check if entity has line of sight to target (simplified)
  bool checkLineOfSight(EntityPtr entity, EntityPtr target) const;

  // Handle behavior when line of sight is lost
  void handleNoLineOfSight(EntityPtr entity);

  // Path-following state for chasing around obstacles
  std::vector<Vector2D> m_navPath;
  size_t m_navIndex{0};
  float m_navRadius{18.0f};
  int m_recalcCounter{0};
  int m_recalcInterval{15}; // frames between path recalcs
  // Deviation detection
  float m_lastNodeDistance{std::numeric_limits<float>::infinity()};
  Uint64 m_lastProgressTime{0};
  Uint64 m_lastPathUpdate{0};

  
};

#endif // CHASE_BEHAVIOR_HPP
