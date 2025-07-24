/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef CHASE_BEHAVIOR_HPP
#define CHASE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"

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

  // Staggering system overrides
  bool useStaggering() const override { return true; }
  uint32_t getUpdateFrequency() const override { return m_updateFrequency; }

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

  // Staggering configuration
  uint32_t m_updateFrequency{3}; // Update expensive calculations every 3 frames

  // Cached state for staggered updates
  mutable Vector2D m_cachedTargetPosition{0, 0};
  mutable float m_cachedDistanceSquared{0.0f};
  mutable bool m_cachedHasLineOfSight{false};
  mutable bool m_cachedStateValid{false};

  // Get cached player reference - optimized for 60fps calls
  EntityPtr getCachedPlayerTarget() const;

  // Invalidate player cache (called on behavior switch or player changes)
  void invalidatePlayerCache() const;

  // Check if entity has line of sight to target (simplified)
  bool checkLineOfSight(EntityPtr entity, EntityPtr target) const;

  // Handle behavior when line of sight is lost
  void handleNoLineOfSight(EntityPtr entity);

  // Staggered update methods
  void updateCachedState(EntityPtr entity) const;
  void executeLightweightLogic(EntityPtr entity) const;
};

#endif // CHASE_BEHAVIOR_HPP
