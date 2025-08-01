/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/ChaseBehavior.hpp"
#include "core/Logger.hpp"
#include "managers/AIManager.hpp"

ChaseBehavior::ChaseBehavior(float chaseSpeed, float maxRange, float minRange)
    : m_chaseSpeed(chaseSpeed), m_maxRange(maxRange), m_minRange(minRange),
      m_isChasing(false), m_hasLineOfSight(false), m_lastKnownTargetPos(0, 0),
      m_timeWithoutSight(0), m_maxTimeWithoutSight(60),
      m_currentDirection(0, 0), m_cachedPlayerTarget(nullptr),
      m_playerCacheValid(false), m_updateFrequency(3),
      m_cachedTargetPosition(0, 0), m_cachedDistanceSquared(0.0f),
      m_cachedHasLineOfSight(false), m_cachedStateValid(false) {}

void ChaseBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  // Initialize the entity's state for chasing
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_cachedStateValid = false;

  // Invalidate cache on initialization
  invalidatePlayerCache();

  // Get player target from cache and check if it's in range
  auto target = getCachedPlayerTarget();
  if (target) {
    Vector2D entityPos = entity->getPosition();
    Vector2D targetPos = target->getPosition();
    float distance = (targetPos - entityPos).length();

    m_isChasing = (distance <= m_maxRange);
    m_hasLineOfSight = checkLineOfSight(entity, target);
  }
  entity->setAcceleration(Vector2D(0,0));
}

EntityPtr ChaseBehavior::getCachedPlayerTarget() const {
  if (!m_playerCacheValid || !m_cachedPlayerTarget) {
    const AIManager &aiMgr = AIManager::Instance();
    m_cachedPlayerTarget = aiMgr.getPlayerReference();
    m_playerCacheValid = true;
  }
  return m_cachedPlayerTarget;
}

void ChaseBehavior::invalidatePlayerCache() const {
  m_playerCacheValid = false;
  m_cachedPlayerTarget = nullptr;
}
void ChaseBehavior::executeLogic(EntityPtr entity) {
  if (!entity || !m_active) {
    return;
  }

  // Get player target from optimized cache
  auto target = getCachedPlayerTarget();
  if (!target) {
    // No target, stop chasing efficiently
    if (m_isChasing) {
      entity->setVelocity(Vector2D(0, 0));
      m_isChasing = false;
    }
    return;
  }

  // This method is called on staggered frames - update expensive calculations
  updateCachedState(entity);

  // Execute movement logic based on cached state
  executeLightweightLogic(entity);
}

void ChaseBehavior::clean(EntityPtr entity) {
  // Stop the entity's movement when cleaning up
  if (entity) {
    entity->setAcceleration(Vector2D(0, 0));
  }

  // Reset all state
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_lastKnownTargetPos = Vector2D(0, 0);
  m_timeWithoutSight = 0;
  m_cachedStateValid = false;
}

void ChaseBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (message == "pause") {
    setActive(false);
    if (entity) {
      entity->setAcceleration(Vector2D(0, 0));
    }
  } else if (message == "resume") {
    setActive(true);
    // Invalidate cache on resume to refresh player reference
    invalidatePlayerCache();
    m_cachedStateValid = false;

    // Reinitialize chase state when resuming
    if (entity) {
      auto target = getCachedPlayerTarget();
      if (target) {
        init(entity);
      }
    }
  } else if (message == "lose_target") {
    m_isChasing = false;
    m_hasLineOfSight = false;
    invalidatePlayerCache();
    m_cachedStateValid = false;
    if (entity) {
      entity->setAcceleration(Vector2D(0, 0));
    }
  } else if (message == "release_entities") {
    // Reset state when asked to release entities
    m_isChasing = false;
    m_hasLineOfSight = false;
    m_lastKnownTargetPos = Vector2D(0, 0);
    m_timeWithoutSight = 0;
    invalidatePlayerCache();
    m_cachedStateValid = false;
    if (entity) {
      entity->setAcceleration(Vector2D(0, 0));
    }
  }
}

std::string ChaseBehavior::getName() const { return "Chase"; }

std::shared_ptr<AIBehavior> ChaseBehavior::clone() const {
  // Clone with same parameters - will use AIManager::getPlayerReference()
  auto cloned =
      std::make_shared<ChaseBehavior>(m_chaseSpeed, m_maxRange, m_minRange);
  cloned->setActive(m_active);
  return cloned;
}

EntityPtr ChaseBehavior::getTarget() const { return getCachedPlayerTarget(); }

void ChaseBehavior::setChaseSpeed(float speed) { m_chaseSpeed = speed; }

void ChaseBehavior::setMaxRange(float range) { m_maxRange = range; }

void ChaseBehavior::setMinRange(float range) { m_minRange = range; }

void ChaseBehavior::setUpdateFrequency(uint32_t frequency) {
  m_updateFrequency = std::max(1u, frequency); // Ensure minimum frequency of 1
  m_cachedStateValid = false; // Invalidate cache when frequency changes
}

bool ChaseBehavior::isChasing() const { return m_isChasing; }

bool ChaseBehavior::hasLineOfSight() const { return m_hasLineOfSight; }

void ChaseBehavior::onTargetReached(EntityPtr entity) {
  // Base implementation does nothing
  // Override in derived behaviors for specific actions
  (void)entity; // Mark parameter as intentionally unused
}

void ChaseBehavior::onTargetLost(EntityPtr entity) {
  // Base implementation does nothing
  // Override in derived behaviors for specific actions
  (void)entity; // Mark parameter as intentionally unused
}

bool ChaseBehavior::checkLineOfSight(EntityPtr entity, EntityPtr target) const {
  // For a more complex implementation, you would do raycasting here
  // This simplified version just checks distance
  if (!entity || !target)
    return false;

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  float distance = (targetPos - entityPos).length();

  // Always report in range for testing purposes
  // Simple line of sight check - would be replaced with raycasting in a real
  // game
  return distance <= m_maxRange;
}

void ChaseBehavior::handleNoLineOfSight(EntityPtr entity) {
  if (m_timeWithoutSight < m_maxTimeWithoutSight) {
    Vector2D entityPos = entity->getPosition();
    Vector2D toLastKnown = m_lastKnownTargetPos - entityPos;
    float distanceSquared = toLastKnown.lengthSquared();

    // Use squared distance threshold (10.0f squared = 100.0f)
    if (distanceSquared > 100.0f) {
      // Optimized normalization with inverse sqrt
      float invDistance = 1.0f / std::sqrt(distanceSquared);
      Vector2D direction = toLastKnown * invDistance;
      entity->setAcceleration(direction * (m_chaseSpeed * 0.8f));
    } else {
      entity->setAcceleration(Vector2D(0, 0));
    }

    m_timeWithoutSight++;
  } else {
    // Timeout - stop chasing efficiently
    if (m_isChasing) {
      m_isChasing = false;
      entity->setAcceleration(Vector2D(0, 0));
      onTargetLost(entity);
    }
  }
}

void ChaseBehavior::updateCachedState(EntityPtr entity) const {
  auto target = getCachedPlayerTarget();
  if (!target) {
    m_cachedStateValid = false;
    return;
  }

  // Get positions
  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();

  // Pre-calculate squared distances for efficiency
  Vector2D toTarget = targetPos - entityPos;
  float distanceSquared = toTarget.lengthSquared();

  // Cache expensive calculations
  m_cachedTargetPosition = targetPos;
  m_cachedDistanceSquared = distanceSquared;
  m_cachedHasLineOfSight = checkLineOfSight(entity, target);
  m_cachedStateValid = true;
}

void ChaseBehavior::executeLightweightLogic(EntityPtr entity) const {
  if (!m_cachedStateValid) {
    // Fall back to immediate calculation if cache is invalid
    updateCachedState(entity);
  }

  Vector2D entityPos = entity->getPosition();
  float maxRangeSquared = m_maxRange * m_maxRange;
  float minRangeSquared = m_minRange * m_minRange;

  // Store target position (use cached value)
  const_cast<ChaseBehavior *>(this)->m_lastKnownTargetPos =
      m_cachedTargetPosition;

  // Fast range check using cached squared distance
  if (m_cachedDistanceSquared <= maxRangeSquared) {
    if (m_cachedHasLineOfSight) {
      const_cast<ChaseBehavior *>(this)->m_hasLineOfSight = true;

      if (m_cachedDistanceSquared > minRangeSquared) {
        // Calculate direction from current position to cached target
        Vector2D toTarget = m_cachedTargetPosition - entityPos;
        float currentDistanceSquared = toTarget.lengthSquared();

        // Only calculate sqrt and normalize when actually moving
        float invDistance = 1.0f / std::sqrt(currentDistanceSquared);
        Vector2D direction = toTarget * invDistance;

        const_cast<ChaseBehavior *>(this)->m_isChasing = true;
        entity->setAcceleration(direction * m_chaseSpeed);
      } else {
        // Within minimum range - stop efficiently
        if (m_isChasing) {
          entity->setAcceleration(Vector2D(0, 0));
          const_cast<ChaseBehavior *>(this)->m_isChasing = false;
          const_cast<ChaseBehavior *>(this)->onTargetReached(entity);
        }
      }
    } else {
      // Lost line of sight
      const_cast<ChaseBehavior *>(this)->m_hasLineOfSight = false;
      if (m_isChasing) {
        const_cast<ChaseBehavior *>(this)->m_timeWithoutSight = 0;
      }
      const_cast<ChaseBehavior *>(this)->handleNoLineOfSight(entity);
    }
  } else {
    // Out of range - only update state if needed
    if (m_isChasing) {
      const_cast<ChaseBehavior *>(this)->m_isChasing = false;
      entity->setAcceleration(Vector2D(0, 0));
      const_cast<ChaseBehavior *>(this)->onTargetLost(entity);
    }
  }
}
