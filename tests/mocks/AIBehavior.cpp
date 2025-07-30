/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/AIBehavior.hpp"
#include <functional>

void AIBehavior::cleanupEntity(EntityPtr entity) { (void)entity; }

void AIBehavior::executeLogicWithStaggering(EntityPtr entity,
                                            uint64_t globalFrame) {
  if (!entity || !m_active) {
    return;
  }

  if (useStaggering() && shouldUpdateThisFrame(entity, globalFrame)) {
    m_lastUpdateFrame = globalFrame;
    executeLogic(entity);
  } else if (!useStaggering()) {
    executeLogic(entity);
  }
}

bool AIBehavior::shouldUpdateThisFrame(EntityPtr entity,
                                       uint64_t globalFrame) const {
  if (!m_staggerOffsetInitialized) {
    // PRODUCTION OPTIMIZATION: Cache the stagger offset calculation
    // Uses fast pointer-based hash instead of std::hash for better performance
    // This optimization benefits both test and production code
    const auto entityAddress = reinterpret_cast<uintptr_t>(entity.get());
    m_entityStaggerOffset =
        static_cast<uint32_t>(entityAddress % getUpdateFrequency());
    m_staggerOffsetInitialized = true;
  }

  return (globalFrame + m_entityStaggerOffset) % getUpdateFrequency() == 0;
}