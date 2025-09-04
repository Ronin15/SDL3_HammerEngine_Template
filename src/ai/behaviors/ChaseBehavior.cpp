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
void ChaseBehavior::executeLogic(EntityPtr entity) {
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
  Uint64 now = SDL_GetTicks();

  // State: TARGET_IN_RANGE - Check if target is within chase range
  if (distanceSquared <= maxRangeSquared) {
    // Update line of sight
    m_hasLineOfSight = checkLineOfSight(entity, target);
    m_lastKnownTargetPos = targetPos;

    // State: APPROACHING_TARGET - Move toward target if not too close
    if (distanceSquared > minRangeSquared) {
      // CACHE-AWARE CHASE: Smart pathfinding with target tracking
      bool needsNewPath = false;
      
      // Only request new path if:
      // 1. No current path exists, OR
      // 2. Target moved significantly (>100px), OR  
      // 3. Path is getting stale (>3 seconds old)
      if (m_navPath.empty() || m_navIndex >= m_navPath.size()) {
        needsNewPath = true;
      } else if (now - m_lastPathUpdate > 3000) { // Path older than 3 seconds
        needsNewPath = true;
      } else {
        // Check if target moved significantly from when path was computed
        Vector2D pathGoal = m_navPath.back();
        float targetMovement = (targetPos - pathGoal).length();
        needsNewPath = (targetMovement > 100.0f); // Target moved >100px
      }
      
      if (needsNewPath && m_cooldowns.canRequestPath(now)) {
        // GOAL VALIDATION: Don't pathfind if already very close to target
        float distanceToTarget = std::sqrt(distanceSquared);
        if (distanceToTarget < (m_minRange * 1.5f)) { // Already close enough
          return; // Skip pathfinding request
        }
        
        // SMART CHASE: Predict target movement for more accurate paths
        Vector2D goalPosition = targetPos;
        
        // Simple target prediction based on velocity (if available)
        if (auto targetEntity = target) {
          Vector2D targetVel = targetEntity->getVelocity();
          float predictionTime = 0.5f; // Predict 0.5 seconds ahead
          if (targetVel.length() > 10.0f) { // Only predict if target is moving
            goalPosition = targetPos + targetVel * predictionTime;
            
            // Validate predicted position is within reasonable bounds
            goalPosition = PathfinderManager::Instance().clampToWorldBounds(goalPosition, 100.0f);
          }
        }

        // ASYNC PATHFINDING: Use high-performance background processing for chasing
        auto& pathfinder = PathfinderManager::Instance();
        pathfinder.requestPathAsync(entity->getID(), entityPos, goalPosition, 
                                   AIInternal::PathPriority::High,
                                   8, // High AIManager priority for chase behavior
                                   [this](EntityID, const std::vector<Vector2D>& path) {
                                     // Update path when received (may be from background thread)
                                     m_navPath = path;
                                     m_navIndex = 0;
                                     m_lastPathUpdate = SDL_GetTicks();
                                   });
        m_cooldowns.applyPathCooldown(now, 1200); // Increased cooldown from 600ms to 1.2s
      }

      // State: PATH_FOLLOWING using optimized PathfinderManager method
      if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
        // Use standardized path following for consistency and performance
        bool following = PathfinderManager::Instance().followPathStep(
            entity, entityPos, m_navPath, m_navIndex, m_chaseSpeed, m_navRadius);
        
        if (following) {
          m_lastProgressTime = now;
          // Apply enhanced separation for combat scenarios with dynamic adjustment
          using namespace AIInternal::SeparationParams;
          float dynamicRadius = COMBAT_RADIUS;
          float dynamicStrength = COMBAT_STRENGTH;
          
          // Use queryArea to find nearby entities for crowd analysis
          AABB queryArea(entityPos.getX() - COMBAT_RADIUS * 2.0f, 
                        entityPos.getY() - COMBAT_RADIUS * 2.0f,
                        entityPos.getX() + COMBAT_RADIUS * 2.0f, 
                        entityPos.getY() + COMBAT_RADIUS * 2.0f);
          std::vector<EntityID> nearbyIDs;
          CollisionManager::Instance().queryArea(queryArea, nearbyIDs);
          
          int chaserCount = 0;
          auto& cm = CollisionManager::Instance();
          for (auto id : nearbyIDs) {
            if (id != entity->getID() && (cm.isDynamic(id) || cm.isKinematic(id)) && !cm.isTrigger(id)) {
              Vector2D nearbyCenter;
              if (cm.getBodyCenter(id, nearbyCenter)) {
                // Check if this entity is likely also chasing (moving toward target)
                Vector2D nearbyToTarget = targetPos - nearbyCenter;
                float distToTarget = nearbyToTarget.length();
                if (distToTarget < m_maxRange * 1.5f) { // Within reasonable chase range
                  chaserCount++;
                }
              }
            }
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
          if (now - m_lastSepTick >= 2) {
            m_lastSepVelocity = AIInternal::ApplySeparation(
                entity, entityPos, entity->getVelocity(), m_chaseSpeed,
                dynamicRadius, dynamicStrength,
                COMBAT_MAX_NEIGHBORS + chaserCount);
            m_lastSepTick = now;
          }
          entity->setVelocity(m_lastSepVelocity);
        } else {
          // Fallback to direct movement with crowd awareness
          Vector2D direction = (targetPos - entityPos);
          direction.normalize();
          // Apply decimated separation on direct movement too
          Vector2D intended = direction * m_chaseSpeed;
          if (now - m_lastSepTick >= 2) {
            m_lastSepVelocity = AIInternal::ApplySeparation(
                entity, entityPos, intended, m_chaseSpeed, 26.0f, 0.22f, 4);
            m_lastSepTick = now;
          }
          entity->setVelocity(m_lastSepVelocity);
          m_lastProgressTime = now;
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
        if (now - m_lastSepTick >= 2) {
          m_lastSepVelocity = AIInternal::ApplySeparation(
              entity, entityPos, intended, m_chaseSpeed, 26.0f, 0.22f, 4);
          m_lastSepTick = now;
        }
        entity->setVelocity(m_lastSepVelocity);
        m_lastProgressTime = now;
      }

      // Update chase state
      if (!m_isChasing) {
        m_isChasing = true;
      }

      // State: STALL_DETECTION - Handle movement blockages
      float currentSpeed = entity->getVelocity().length();
      const float stallThreshold = std::max(1.0f, m_chaseSpeed * 0.6f);
      const Uint64 stallTimeLimit = static_cast<Uint64>(800 + (m_chaseSpeed * 10));
      
      if (currentSpeed < stallThreshold) {
        if (m_stallStart == 0) {
          m_stallStart = now;
          m_lastStallPosition = entityPos;
          m_stallPositionVariance = 0.0f;
        } else {
          // Track position variance to detect true stalls vs normal slow movement
          float positionDiff = (entityPos - m_lastStallPosition).length();
          m_stallPositionVariance = std::max(m_stallPositionVariance, positionDiff);
          
          if (now - m_stallStart >= stallTimeLimit && m_stallPositionVariance < 8.0f) {
            // True stall detected - apply recovery
            if (now - m_lastUnstickTime > 4000) { // Emergency unstick
              AI_WARN("Chase entity " + std::to_string(entity->getID()) + " is stalled, applying emergency unstick");
              // Simple unstick: small random movement
              float randomAngle = ((float)rand() / RAND_MAX) * 2.0f * M_PI;
              Vector2D unstickDir(std::cos(randomAngle), std::sin(randomAngle));
              entity->setVelocity(unstickDir * m_chaseSpeed);
              m_lastUnstickTime = now;
              m_stallStart = 0;
              return;
            } else {
              // Regular stall recovery: clear path and apply jitter
              m_navPath.clear();
              m_navIndex = 0;
              m_lastPathUpdate = 0;
              m_cooldowns.applyStallCooldown(now, entity->getID());
              
              // Apply random jitter to break deadlock
              float jitter = ((float)rand() / RAND_MAX - 0.5f) * 0.4f;
              Vector2D direction = (targetPos - entityPos).normalized();
              float c = std::cos(jitter), s = std::sin(jitter);
              Vector2D rotated(direction.getX()*c - direction.getY()*s, 
                             direction.getX()*s + direction.getY()*c);
              entity->setVelocity(rotated * m_chaseSpeed);
              m_stallStart = 0;
            }
          }
        }
      } else {
        m_stallStart = 0; // Reset when moving normally
        m_stallPositionVariance = 0.0f;
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
      m_lastPathUpdate = 0;
      
      onTargetLost(entity);
    }
  }
}

void ChaseBehavior::clean(EntityPtr entity) {
  // Stop the entity's movement when cleaning up
  if (entity) {
    entity->setVelocity(Vector2D(0, 0));
    
    // Clear pathfinding state from PathfinderManager
    PathfinderManager::Instance().cancelEntityRequests(entity->getID());
    
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
