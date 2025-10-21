/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/ChaseBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"
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
void ChaseBehavior::executeLogic(EntityPtr entity, float deltaTime) {
  if (!entity || !m_active) {
    return;
  }

  // Get player target from optimized cache
  auto target = getCachedPlayerTarget();
  if (!target) {
    // No target available - clean exit from chase state
    if (m_isChasing) {
      entity->setVelocity(Vector2D(0, 0));
      m_isChasing = false;
      m_hasLineOfSight = false;
      onTargetLost(entity);
    }
    return;
  }

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  float distanceSquared = (targetPos - entityPos).lengthSquared();
  float maxRangeSquared = m_maxRange * m_maxRange;
  float minRangeSquared = m_minRange * m_minRange;

  // Update cooldown timers
  m_cooldowns.update(deltaTime);
  m_pathUpdateTimer += deltaTime;

  // State: TARGET_IN_RANGE - Check if target is within chase range
  if (distanceSquared <= maxRangeSquared) {
    // Update line of sight
    m_hasLineOfSight = checkLineOfSight(entity, target);
    m_lastKnownTargetPos = targetPos;

    // State: APPROACHING_TARGET - Move toward target if not too close
    if (distanceSquared > minRangeSquared) {
      // CACHE-AWARE CHASE: Smart pathfinding with target tracking
      bool needsNewPath = false;

      // OPTIMIZED: Further reduced pathfinding frequency for better performance
      // Only request new path if:
      // 1. No current path exists, OR
      // 2. Target moved very significantly (>300px), OR
      // 3. Path is getting quite stale (>8 seconds old)
      constexpr float PATH_INVALIDATION_DISTANCE = 300.0f; // Further increased threshold
      constexpr float PATH_REFRESH_INTERVAL = 8.0f; // 8 seconds

      if (m_navPath.empty() || m_navIndex >= m_navPath.size()) {
        needsNewPath = true;
      } else if (m_pathUpdateTimer > PATH_REFRESH_INTERVAL) {
        needsNewPath = true;
      } else {
        // Check if target moved significantly from when path was computed
        Vector2D pathGoal = m_navPath.back();
        float targetMovementSquared = (targetPos - pathGoal).lengthSquared();
        needsNewPath = (targetMovementSquared > PATH_INVALIDATION_DISTANCE * PATH_INVALIDATION_DISTANCE);
      }

      // OBSTACLE DETECTION: Force path refresh if stuck on obstacle
      bool stuckOnObstacle = (m_progressTimer > 3.0f); // Stuck if no progress for 3 seconds
      if (stuckOnObstacle) {
        m_navPath.clear(); // Clear path to force refresh
        m_navIndex = 0;
      }

      if ((needsNewPath || stuckOnObstacle) && m_cooldowns.canRequestPath()) {
        // PERFORMANCE: Use squared distance to avoid expensive sqrt
        float minRangeCheckSquared = (m_minRange * 1.5f) * (m_minRange * 1.5f);
        if (distanceSquared < minRangeCheckSquared) { // Already close enough
          return; // Skip pathfinding request
        }
        
        // SIMPLIFIED: Basic target prediction for better performance
        Vector2D goalPosition = targetPos;
        
        // Light prediction only for fast-moving targets
        Vector2D targetVel = target->getVelocity();
        if (targetVel.length() > 30.0f) { // Only predict for fast movement
          goalPosition = targetPos + targetVel * 0.3f; // Shorter prediction time
        }

        // ASYNC PATHFINDING: Use high-performance background processing for chasing
        auto& pf = this->pathfinder();
        pf.requestPath(entity->getID(), entityPos, goalPosition,
                              PathfinderManager::Priority::High,
                              [this](EntityID, const std::vector<Vector2D>& path) {
                                // Update path when received (may be from background thread)
                                m_navPath = path;
                                m_navIndex = 0;
                                m_pathUpdateTimer = 0.0f;
                              });
        m_cooldowns.applyPathCooldown(3.0f); // 3 second cooldown for better performance
      }

      // State: PATH_FOLLOWING using optimized PathfinderManager method
      if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
        // Use standardized path following for consistency and performance
        bool following = pathfinder().followPathStep(
            entity, entityPos, m_navPath, m_navIndex, m_chaseSpeed, m_navRadius);
        
        if (following) {
          m_progressTimer = 0.0f; // Reset progress timer
          // Apply enhanced separation for combat scenarios with dynamic adjustment
          using namespace AIInternal::SeparationParams;
          float dynamicRadius = COMBAT_RADIUS;
          float dynamicStrength = COMBAT_STRENGTH;

          // OPTIMIZED: Use AIManager's spatial partitioning instead of expensive collision queries
          int chaserCount = 0;
          constexpr float CROWD_CHECK_INTERVAL = 2.0f; // Check every 2 seconds

          m_crowdCheckTimer += deltaTime;
          if (m_crowdCheckTimer >= CROWD_CHECK_INTERVAL) {
            // Use AIManager's optimized spatial counting instead of collision queries
            chaserCount = AIInternal::CountNearbyEntities(entity, entityPos, COMBAT_RADIUS * 2.0f);
            m_crowdCheckTimer = 0.0f;
            m_cachedChaserCount = chaserCount;
          } else {
            // Use cached value between checks
            chaserCount = m_cachedChaserCount;
          }
          
          // Simple crowd management - maintain direct pursuit with minimal spreading
          if (chaserCount > 3) {
            // High density: slight lateral spread but still pursue directly
            dynamicRadius = COMBAT_RADIUS * 1.2f;
            dynamicStrength = COMBAT_STRENGTH * 1.3f;
            
            // Small lateral offset to reduce clumping
            Vector2D toTarget = (targetPos - entityPos).normalized();
            Vector2D lateral(-toTarget.getY(), toTarget.getX());
            float lateralBias = ((float)(entity->getID() % 3) - 1.0f) * 15.0f; // Small spread: -15, 0, or +15
            Vector2D adjustedTarget = targetPos + lateral * lateralBias;
            Vector2D newDir = (adjustedTarget - entityPos).normalized();
            entity->setVelocity(newDir * m_chaseSpeed);
          }
          
          // Separation decimation: compute at most every 2 ticks
          applyDecimatedSeparation(entity, entityPos, entity->getVelocity(),
                                   m_chaseSpeed, dynamicRadius, dynamicStrength,
                                   COMBAT_MAX_NEIGHBORS + chaserCount,
                                   m_separationTimer, m_lastSepVelocity, deltaTime);
        } else {
          // Fallback to direct movement with crowd awareness
          Vector2D direction = (targetPos - entityPos);
          direction.normalize();
          // Apply decimated separation on direct movement too
          Vector2D intended = direction * m_chaseSpeed;
          applyDecimatedSeparation(entity, entityPos, intended, m_chaseSpeed,
                                   26.0f, 0.22f, 4, m_separationTimer,
                                   m_lastSepVelocity, deltaTime);
          m_progressTimer = 0.0f;
        }
      } else {
        // Direct movement toward target with crowd awareness
        Vector2D direction = (targetPos - entityPos);
        direction.normalize();
        
        // Simple crowd awareness - minor lateral spread for direct pursuit
        int nearbyCount = AIInternal::CountNearbyEntities(entity, entityPos, 80.0f);
        
        if (nearbyCount > 1) {
          // Add minimal lateral offset to reduce perfect stacking while maintaining chase
          Vector2D lateral(-direction.getY(), direction.getX());
          float offset = ((float)(entity->getID() % 3) - 1.0f) * 20.0f; // Small spread: -20 to +20
          direction = direction + lateral * (offset / 400.0f); // Very small lateral bias
          direction.normalize();
        }
        
        // Decimated separation on direct pursuit
        Vector2D intended = direction * m_chaseSpeed;
        applyDecimatedSeparation(entity, entityPos, intended, m_chaseSpeed,
                                 26.0f, 0.22f, 4, m_separationTimer,
                                 m_lastSepVelocity, deltaTime);
        m_progressTimer = 0.0f;
      }

      // Update chase state
      m_isChasing = true;

      // SIMPLIFIED: Basic stall detection without complex variance tracking
      float currentSpeed = entity->getVelocity().length();
      const float stallThreshold = std::max(1.0f, m_chaseSpeed * 0.5f);
      const float stallTimeLimit = 2.0f; // Fixed 2-second timeout

      if (currentSpeed < stallThreshold) {
        m_stallTimer += deltaTime;
        if (m_stallTimer >= stallTimeLimit) {
          // Simple stall recovery: clear path and request new one
          m_navPath.clear();
          m_navIndex = 0;
          m_pathUpdateTimer = 0.0f;
          m_stallTimer = 0.0f;
          
          // Light jitter to avoid deadlock
          float jitter = ((float)rand() / RAND_MAX - 0.5f) * 0.2f;
          Vector2D direction = (targetPos - entityPos).normalized();
          float c = std::cos(jitter), s = std::sin(jitter);
          Vector2D rotated(direction.getX()*c - direction.getY()*s, 
                         direction.getX()*s + direction.getY()*c);
          entity->setVelocity(rotated * m_chaseSpeed);
        }
      } else {
        m_stallTimer = 0.0f; // Reset when moving normally
      }

    } else {
      // State: TARGET_REACHED - Too close to target, stop movement
      if (m_isChasing) {
        entity->setVelocity(Vector2D(0, 0));
        m_isChasing = false;
        onTargetReached(entity);
      }
    }
  } else {
    // State: TARGET_OUT_OF_RANGE - Target is beyond chase range
    if (m_isChasing) {
      m_isChasing = false;
      m_hasLineOfSight = false;
      entity->setVelocity(Vector2D(0, 0));
      
      // Clear pathfinding state
      m_navPath.clear();
      m_navIndex = 0;
      m_pathUpdateTimer = 0.0f;
      
      onTargetLost(entity);
    }
  }
}

void ChaseBehavior::clean(EntityPtr entity) {
  // Stop the entity's movement when cleaning up
  if (entity) {
    entity->setVelocity(Vector2D(0, 0));
    
    // Clear pathfinding state from PathfinderManager
    // Note: Request cancellation is handled automatically by the new pathfinding architecture
    
    // ChaseBehavior cleanup completed
  }

  // Reset all state
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_lastKnownTargetPos = Vector2D(0, 0);
  m_timeWithoutSight = 0;
  
  // Clear path-following state
  m_navPath.clear();
  m_navIndex = 0;
  m_pathUpdateTimer = 0.0f;
  m_progressTimer = 0.0f;
  m_stallTimer = 0.0f;
  m_stallPositionVariance = 0.0f;
  m_unstickTimer = 0.0f;

  // Reset cooldowns
  m_cooldowns.pathRequestCooldown = 0.0f;
  m_cooldowns.stallRecoveryCooldown = 0.0f;
  m_cooldowns.behaviorChangeCooldown = 0.0f;
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
  // PERFORMANCE: Use squared distance to avoid sqrt
  if (!entity || !target)
    return false;

  Vector2D entityPos = entity->getPosition();
  Vector2D targetPos = target->getPosition();
  float distanceSquared = (targetPos - entityPos).lengthSquared();
  float maxRangeSquared = m_maxRange * m_maxRange;

  // Simple line of sight check - would be replaced with raycasting in a real game
  return distanceSquared <= maxRangeSquared;
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

