/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef AI_BEHAVIOR_HPP
#define AI_BEHAVIOR_HPP

#include "entities/Entity.hpp"
#include <string>

class AIBehavior {
public:
  virtual ~AIBehavior() = default;

  // Core behavior methods - pure logic only
  virtual void executeLogic(EntityPtr entity) = 0;
  virtual void init(EntityPtr entity) = 0;
  virtual void clean(EntityPtr entity) = 0;

  // Behavior identification
  virtual std::string getName() const = 0;

  // Optional message handling for behavior communication
  virtual void onMessage([[maybe_unused]] EntityPtr entity,
                         [[maybe_unused]] const std::string &message) {}

  // Behavior state access
  virtual bool isActive() const { return m_active; }
  virtual void setActive(bool active) { m_active = active; }

  // Frame-based update staggering system
  virtual void executeLogicWithStaggering(EntityPtr entity,
                                          uint64_t globalFrame);
  virtual uint32_t getUpdateFrequency() const {
    return 1;
  } // Update every frame by default
  virtual bool useStaggering() const {
    return false;
  } // Override in behaviors that want staggering

  // Entity range checks (behavior-specific logic)
  virtual bool isEntityInRange([[maybe_unused]] EntityPtr entity) const {
    return true;
  }

  // Entity cleanup
  virtual void cleanupEntity(EntityPtr entity);

  // Clone method for creating unique behavior instances
  virtual std::shared_ptr<AIBehavior> clone() const = 0;

  // Expose to AIManager for behavior management
  friend class AIManager;

protected:
  bool m_active{true};

  // Staggering system state
  mutable uint64_t m_lastUpdateFrame{0};
  mutable uint32_t m_entityStaggerOffset{0};
  mutable bool m_staggerOffsetInitialized{false};

  // Helper to calculate if this entity should update on this frame
  bool shouldUpdateThisFrame(EntityPtr entity, uint64_t globalFrame) const;
};

#endif // AI_BEHAVIOR_HPP