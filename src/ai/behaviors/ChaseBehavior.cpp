/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/ChaseBehavior.hpp"
#include "managers/AIManager.hpp"
#include "ai/internal/PathFollow.hpp"
#include "managers/CollisionManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "core/Logger.hpp"

ChaseBehavior::ChaseBehavior(float chaseSpeed, float maxRange, float minRange)
    : m_chaseSpeed(chaseSpeed), m_maxRange(maxRange), m_minRange(minRange),
      m_isChasing(false), m_hasLineOfSight(false), m_lastKnownTargetPos(0, 0),
      m_timeWithoutSight(0), m_maxTimeWithoutSight(60),
      m_currentDirection(0, 0), m_cachedPlayerTarget(nullptr),
      m_playerCacheValid(false) {}

void ChaseBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  // Initialize the entity's state for chasing
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_lastKnownTargetPos = entity->getPosition();

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

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  float distanceSquared = (targetPos - entityPos).lengthSquared();

  if (distanceSquared <= m_maxRange * m_maxRange) {
    bool los = checkLineOfSight(entity, target);
    m_hasLineOfSight = los;
    Uint64 now = SDL_GetTicks();
    // Refresh path with a chase-specific policy (direct, minimal detours)
    {
      using namespace AIInternal;
      PathPolicy policy;
      policy.pathTTL = 2500;          // Less frequent refresh for chase
      policy.noProgressWindow = 600;  // More patience when blocked
      policy.nodeRadius = m_navRadius;
      policy.allowDetours = true;     // allow small detours to slip around blocks
      // Tighter detour ring for assertive chase
      policy.detourRadii[0] = 48.0f;
      policy.detourRadii[1] = 96.0f;
      policy.lateralBias = 0.0f;
      if (m_cooldowns.canRequestPath(now)) {
        RefreshPathWithPolicy(entity, entityPos, targetPos,
                              m_navPath, m_navIndex, m_lastPathUpdate,
                              m_lastProgressTime, m_lastNodeDistance,
                              policy);
        m_cooldowns.applyPathCooldown(now, 600); // Apply cooldown after path request
      }
    }
    if (distanceSquared > m_minRange * m_minRange) {
        // Follow path if available; else direct steer toward target
        if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
            using namespace AIInternal;
            bool following = FollowPathStepWithPolicy(entity, entityPos,
                                 m_navPath, m_navIndex,
                                 m_chaseSpeed, m_navRadius, 0.0f);
            if (!following) {
                Vector2D direction = (targetPos - entityPos);
                direction.normalize();
                entity->setVelocity(direction * m_chaseSpeed);
            } else {
                m_lastProgressTime = now;
                // Apply enhanced separation when following to reduce stacking (unified helper)
                using namespace AIInternal::SeparationParams;
                Vector2D adjusted = AIInternal::ApplySeparation(entity, entityPos,
                                     entity->getVelocity(), m_chaseSpeed,
                                     COMBAT_RADIUS, COMBAT_STRENGTH, COMBAT_MAX_NEIGHBORS);
                entity->setVelocity(adjusted);
            }
        } else {
            Vector2D direction = (targetPos - entityPos);
            direction.normalize();
            entity->setVelocity(direction * m_chaseSpeed);
            m_lastProgressTime = now;
        }
        m_isChasing = true;
        // Improved stall detection for chase behavior
        float spd = entity->getVelocity().length();
        const float stallSpeed = std::max(1.0f, m_chaseSpeed * 0.6f); // Improved threshold
        const Uint64 stallMillis = static_cast<Uint64>(800 + (m_chaseSpeed * 100)); // Adaptive timing
        
        if (spd < stallSpeed) {
            if (m_stallStart == 0) {
                m_stallStart = now;
                m_lastStallPosition = entityPos;
                m_stallPositionVariance = 0.0f;
            } else {
                // Calculate position variance to detect true stalls
                float positionDiff = (entityPos - m_lastStallPosition).length();
                m_stallPositionVariance = std::max(m_stallPositionVariance, positionDiff);
                
                if (now - m_stallStart >= stallMillis && m_stallPositionVariance < 10.0f) {
                    // True stall detected - check if we need emergency unstick
                    if (now - m_lastUnstickTime > 5000) { // Don't unstick too frequently
                        AI_WARN("Chase entity " + std::to_string(entity->getID()) + " is truly stalled, applying emergency unstick");
                        AIManager::Instance().forceUnstickEntity(entity);
                        m_lastUnstickTime = now;
                        return;
                    } else {
                        // Regular stall recovery
                        m_navPath.clear();
                        m_navIndex = 0;
                        m_lastPathUpdate = 0;
                        m_cooldowns.applyStallCooldown(now, entity->getID());
                        
                        // Apply jitter to break out of local minima
                        float jitter = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
                        Vector2D v = entity->getVelocity(); 
                        if (v.length() < 0.01f) v = Vector2D(1, 0);
                        float c = std::cos(jitter), s = std::sin(jitter);
                        Vector2D rotated(v.getX()*c - v.getY()*s, v.getX()*s + v.getY()*c);
                        rotated.normalize(); 
                        entity->setVelocity(rotated * m_chaseSpeed);
                    }
                    m_stallStart = 0;
                    m_stallPositionVariance = 0.0f;
                }
            }
        } else {
            m_stallStart = 0;
            m_stallPositionVariance = 0.0f;
        }
    } else {
        if (m_isChasing) {
            entity->setVelocity(Vector2D(0, 0));
            m_isChasing = false;
            onTargetReached(entity);
        }
    }
  } else {
    if (m_isChasing) {
        m_isChasing = false;
        entity->setVelocity(Vector2D(0, 0));
        onTargetLost(entity);
    }
  }
}

void ChaseBehavior::clean(EntityPtr entity) {
  // Stop the entity's movement when cleaning up
  if (entity) {
    entity->setVelocity(Vector2D(0, 0));
    
    // Clear pathfinding state from AIManager
    AIManager::Instance().clearPath(entity);
    
    AI_DEBUG("Cleaned ChaseBehavior for entity " + std::to_string(entity->getID()));
  }

  // Reset all state
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_lastKnownTargetPos = Vector2D(0, 0);
  m_timeWithoutSight = 0;
  
  // Clear path-following state
  m_navPath.clear();
  m_navIndex = 0;
  m_lastPathUpdate = 0;
  m_lastProgressTime = 0;
  m_stallStart = 0;
  m_stallPositionVariance = 0.0f;
  m_lastUnstickTime = 0;
  
  // Reset cooldowns
  m_cooldowns.nextPathRequest = 0;
  m_cooldowns.stallRecoveryUntil = 0;
  m_cooldowns.behaviorChangeUntil = 0;
}

void ChaseBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (message == "pause") {
    setActive(false);
    if (entity) {
      entity->setVelocity(Vector2D(0, 0));
    }
  } else if (message == "resume") {
    setActive(true);
    // Invalidate cache on resume to refresh player reference
    invalidatePlayerCache();

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
    if (entity) {
      entity->setVelocity(Vector2D(0, 0));
    }
  } else if (message == "release_entities") {
    // Reset state when asked to release entities
    m_isChasing = false;
    m_hasLineOfSight = false;
    m_lastKnownTargetPos = Vector2D(0, 0);
    m_timeWithoutSight = 0;
    invalidatePlayerCache();
    if (entity) {
      entity->setVelocity(Vector2D(0, 0));
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

void ChaseBehavior::setUpdateFrequency(uint32_t) {}

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
      entity->setVelocity(direction * (m_chaseSpeed * 0.8f));
    } else {
      entity->setVelocity(Vector2D(0, 0));
    }

    m_timeWithoutSight++;
  } else {
    // Timeout - stop chasing efficiently
    if (m_isChasing) {
      m_isChasing = false;
      entity->setVelocity(Vector2D(0, 0));
      onTargetLost(entity);
    }
  }
}
