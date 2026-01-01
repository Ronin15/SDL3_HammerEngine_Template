/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/ChaseBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "ai/internal/SpatialPriority.hpp"
#include "managers/CollisionManager.hpp"
#include "ai/internal/Crowd.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <random>

namespace {
// Thread-safe RNG for stall recovery jitter
std::mt19937& getThreadLocalRNG() {
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}
} // namespace

ChaseBehavior::ChaseBehavior(const HammerEngine::ChaseBehaviorConfig& config)
    : m_config(config), m_chaseSpeed(config.chaseSpeed), m_maxRange(1000.0f), m_minRange(50.0f),
      m_isChasing(false), m_hasLineOfSight(false), m_lastKnownTargetPos(0, 0),
      m_timeWithoutSight(0), m_maxTimeWithoutSight(60),
      m_currentDirection(0, 0) {}

ChaseBehavior::ChaseBehavior(float chaseSpeed, float maxRange, float minRange)
    : m_chaseSpeed(chaseSpeed), m_maxRange(maxRange), m_minRange(minRange),
      m_isChasing(false), m_hasLineOfSight(false), m_lastKnownTargetPos(0, 0),
      m_timeWithoutSight(0), m_maxTimeWithoutSight(60),
      m_currentDirection(0, 0) {
  // Update config to match legacy parameters
  m_config.chaseSpeed = chaseSpeed;
}

void ChaseBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  const auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  const auto& hotData = edm.getHotDataByIndex(idx);

  // Initialize the entity's state for chasing
  m_isChasing = false;
  m_hasLineOfSight = false;
  m_lastKnownTargetPos = hotData.transform.position;

  // Check if player is valid and in range
  const auto& aiMgr = AIManager::Instance();
  if (aiMgr.isPlayerValid()) {
    Vector2D entityPos = hotData.transform.position;
    Vector2D targetPos = aiMgr.getPlayerPosition();
    float const distance = (targetPos - entityPos).length();

    m_isChasing = (distance <= m_maxRange);
    m_hasLineOfSight = (distance <= m_maxRange); // Simplified line of sight check
  }
}

EntityHandle ChaseBehavior::getTargetHandle() const {
  return AIManager::Instance().getPlayerHandle();
}

bool ChaseBehavior::checkLineOfSight(EntityHandle handle, const Vector2D& targetPos) const {
  if (!handle.isValid()) return false;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return false;

  Vector2D entityPos = edm.getHotDataByIndex(idx).transform.position;
  float const distanceSquared = (targetPos - entityPos).lengthSquared();
  float const maxRangeSquared = m_maxRange * m_maxRange;

  return distanceSquared <= maxRangeSquared;
}

void ChaseBehavior::executeLogic(BehaviorContext& ctx) {
  if (!m_active) {
    return;
  }

  // PERFORMANCE OPTIMIZATION: Cache crowd analysis results every 3-5 seconds
  // Reduces CollisionManager queries significantly for chasing entities
  m_lastCrowdAnalysis += ctx.deltaTime;
  float crowdCacheInterval = 3.0f + (static_cast<float>(ctx.entityId % 200) * 0.01f); // 3-5 seconds
  if (m_lastCrowdAnalysis >= crowdCacheInterval) {
    // Query nearby entities for separation steering
    constexpr float kCrowdQueryRadius = 80.0f;
    m_cachedNearbyCount = AIInternal::GetNearbyEntitiesWithPositions(
        ctx.entityId, ctx.transform.position, kCrowdQueryRadius, m_cachedNearbyPositions);
    m_lastCrowdAnalysis = 0.0f;
  }

  // Get player position from AIManager
  auto& aiMgr = AIManager::Instance();
  if (!aiMgr.isPlayerValid()) {
    // No target available - clean exit from chase state
    if (m_isChasing) {
      ctx.transform.velocity = Vector2D(0, 0);
      m_isChasing = false;
      m_hasLineOfSight = false;
    }
    return;
  }

  Vector2D entityPos = ctx.transform.position;
  Vector2D targetPos = aiMgr.getPlayerPosition();
  float const distanceSquared = (targetPos - entityPos).lengthSquared();

  float const maxRangeSquared = m_maxRange * m_maxRange;
  float const minRangeSquared = m_minRange * m_minRange;

  // Update cooldown timers
  m_cooldowns.update(ctx.deltaTime);
  m_pathUpdateTimer += ctx.deltaTime;

  // State: TARGET_IN_RANGE - Check if target is within chase range
  if (distanceSquared <= maxRangeSquared) {
    // Update line of sight (simplified - just distance check in BehaviorContext path)
    m_hasLineOfSight = (distanceSquared <= maxRangeSquared);
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
      const float PATH_INVALIDATION_DISTANCE = m_config.pathInvalidationDistance; // Distance target must move to invalidate path
      const float PATH_REFRESH_INTERVAL = m_config.pathRefreshInterval; // Seconds between path recalculations

      if (m_navPath.empty() || m_navIndex >= m_navPath.size()) {
        needsNewPath = true;
      } else if (m_pathUpdateTimer > PATH_REFRESH_INTERVAL) {
        needsNewPath = true;
      } else {
        // Check if target moved significantly from when path was computed
        Vector2D pathGoal = m_navPath.back();
        float const targetMovementSquared = (targetPos - pathGoal).lengthSquared();
        needsNewPath = (targetMovementSquared > PATH_INVALIDATION_DISTANCE * PATH_INVALIDATION_DISTANCE);
      }

      // OBSTACLE DETECTION: Force path refresh if stuck on obstacle
      bool const stuckOnObstacle = (m_progressTimer > 3.0f); // Stuck if no progress for 3 seconds
      if (stuckOnObstacle) {
        m_navPath.clear(); // Clear path to force refresh
        m_navIndex = 0;
      }

      if ((needsNewPath || stuckOnObstacle) && m_cooldowns.canRequestPath()) {
        // PERFORMANCE: Use squared distance to avoid expensive sqrt
        float const minRangeCheckSquared = (m_minRange * 1.5f) * (m_minRange * 1.5f);
        if (distanceSquared < minRangeCheckSquared) { // Already close enough
          return; // Skip pathfinding request
        }

        // SIMPLIFIED: Basic target prediction for better performance
        Vector2D goalPosition = targetPos;

        // Note: Target velocity prediction removed - would need to be stored in AIManager if needed

        // ASYNC PATHFINDING: Use high-performance background processing for chasing
        auto& pf = this->pathfinder();
        auto self = std::static_pointer_cast<ChaseBehavior>(shared_from_this());
        pf.requestPath(ctx.entityId, entityPos, goalPosition,
                              PathfinderManager::Priority::High,
                              [self](EntityID, const std::vector<Vector2D>& path) {
                                // Update path when received (may be from background thread)
                                self->m_navPath = path;
                                self->m_navIndex = 0;
                                self->m_pathUpdateTimer = 0.0f;
                              });
        m_cooldowns.applyPathCooldown(m_config.pathRequestCooldown); // Cooldown for better performance
      }

      // State: PATH_FOLLOWING - inline path following logic
      if (!m_navPath.empty() && m_navIndex < m_navPath.size()) {
        // Inline path following to avoid EntityPtr dependency
        Vector2D waypoint = m_navPath[m_navIndex];
        Vector2D toWaypoint = waypoint - entityPos;
        float dist = toWaypoint.length();

        // Check if reached current waypoint
        if (dist < m_navRadius) {
          m_navIndex++;
          if (m_navIndex < m_navPath.size()) {
            waypoint = m_navPath[m_navIndex];
            toWaypoint = waypoint - entityPos;
            dist = toWaypoint.length();
          }
        }

        bool following = (m_navIndex < m_navPath.size() && dist > 0.001f);

        if (following) {
          // Move towards waypoint
          Vector2D direction = toWaypoint / dist;
          ctx.transform.velocity = direction * m_chaseSpeed;
          m_progressTimer = 0.0f; // Reset progress timer

          // Simple crowd management - use cached chaser count for lateral spreading
          m_crowdCheckTimer += ctx.deltaTime;
          if (m_crowdCheckTimer >= m_config.crowdCheckInterval) {
            m_crowdCheckTimer = 0.0f;
          }

          // High density: apply lateral spread to reduce clumping
          if (m_cachedChaserCount > 3) {
            Vector2D const toTarget = (targetPos - entityPos).normalized();
            Vector2D const lateral(-toTarget.getY(), toTarget.getX());
            float lateralBias = ((float)(ctx.entityId % 3) - 1.0f) * 15.0f; // -15, 0, or +15
            Vector2D const adjustedTarget = targetPos + lateral * lateralBias;
            Vector2D const newDir = (adjustedTarget - entityPos).normalized();
            ctx.transform.velocity = newDir * m_chaseSpeed;
          }

          // Velocity set - CollisionManager handles overlap resolution
        } else {
          // Fallback to direct movement
          Vector2D direction = (targetPos - entityPos);
          direction.normalize();
          ctx.transform.velocity = direction * m_chaseSpeed;
          m_progressTimer = 0.0f;
        }
      } else {
        // Direct movement toward target with crowd awareness
        Vector2D direction = (targetPos - entityPos);
        direction.normalize();

        // OPTIMIZATION: Use cached crowd count (updated every 3-5 seconds) instead of
        // expensive per-frame CollisionManager query. Reduces ~5% frame time for chase entities.
        int const nearbyCount = m_cachedNearbyCount;

        if (nearbyCount > 1) {
          // Add minimal lateral offset to reduce perfect stacking while maintaining chase
          Vector2D const lateral(-direction.getY(), direction.getX());
          float offset = ((float)(ctx.entityId % 3) - 1.0f) * 20.0f; // Small spread: -20 to +20
          direction = direction + lateral * (offset / 400.0f); // Very small lateral bias
          direction.normalize();
        }

        // Set velocity directly - CollisionManager handles overlap resolution
        ctx.transform.velocity = direction * m_chaseSpeed;
        m_progressTimer = 0.0f;
      }

      // Update chase state
      m_isChasing = true;

      // SIMPLIFIED: Basic stall detection without complex variance tracking
      float currentSpeed = ctx.transform.velocity.length();
      const float stallThreshold = std::max(1.0f, m_chaseSpeed * m_config.stallSpeedMultiplier);
      const float stallTimeLimit = m_config.stallTimeout; // Timeout before triggering recovery

      if (currentSpeed < stallThreshold) {
        m_stallTimer += ctx.deltaTime;
        if (m_stallTimer >= stallTimeLimit) {
          // Simple stall recovery: clear path and request new one
          m_navPath.clear();
          m_navIndex = 0;
          m_pathUpdateTimer = 0.0f;
          m_stallTimer = 0.0f;

          // Light jitter to avoid deadlock (thread-safe)
          std::uniform_real_distribution<float> jitterDist(-0.1f, 0.1f);
          float jitter = jitterDist(getThreadLocalRNG());
          Vector2D const direction = (targetPos - entityPos).normalized();
          float c = std::cos(jitter), s = std::sin(jitter);
          Vector2D const rotated(direction.getX()*c - direction.getY()*s,
                         direction.getX()*s + direction.getY()*c);
          ctx.transform.velocity = rotated * m_chaseSpeed;
        }
      } else {
        m_stallTimer = 0.0f; // Reset when moving normally
      }

    } else {
      // State: TARGET_REACHED - Too close to target, stop movement
      if (m_isChasing) {
        ctx.transform.velocity = Vector2D(0, 0);
        m_isChasing = false;
        // onTargetReached needs EntityPtr - skip for now in hot path
      }
    }
  } else {
    // State: TARGET_OUT_OF_RANGE - Target is beyond chase range
    if (m_isChasing) {
      m_isChasing = false;
      m_hasLineOfSight = false;
      ctx.transform.velocity = Vector2D(0, 0);

      // Clear pathfinding state
      m_navPath.clear();
      m_navIndex = 0;
      m_pathUpdateTimer = 0.0f;

      // onTargetLost needs EntityPtr - skip for now in hot path
    }
  }
}

void ChaseBehavior::clean(EntityHandle handle) {
  // Stop the entity's movement when cleaning up
  if (handle.isValid()) {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }

    // Clear pathfinding state from PathfinderManager
    // Note: Request cancellation is handled automatically by the new pathfinding architecture
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

  // Clear cached crowd data
  m_cachedNearbyPositions.clear();
  m_cachedNearbyCount = 0;
  m_lastCrowdAnalysis = 0.0f;
  m_cachedChaserCount = 0;
  m_crowdCheckTimer = 0.0f;

  // Reset cooldowns
  m_cooldowns.pathRequestCooldown = 0.0f;
  m_cooldowns.stallRecoveryCooldown = 0.0f;
  m_cooldowns.behaviorChangeCooldown = 0.0f;
}

void ChaseBehavior::onMessage(EntityHandle handle, const std::string &message) {
  auto& edm = EntityDataManager::Instance();
  size_t idx = handle.isValid() ? edm.getIndex(handle) : SIZE_MAX;

  if (message == "pause") {
    setActive(false);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }
  } else if (message == "resume") {
    setActive(true);

    // Reinitialize chase state when resuming
    if (handle.isValid()) {
      const auto& aiMgr = AIManager::Instance();
      if (aiMgr.isPlayerValid()) {
        init(handle);
      }
    }
  } else if (message == "lose_target") {
    m_isChasing = false;
    m_hasLineOfSight = false;
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
    }
  } else if (message == "release_entities") {
    // Reset state when asked to release entities
    m_isChasing = false;
    m_hasLineOfSight = false;
    m_lastKnownTargetPos = Vector2D(0, 0);
    m_timeWithoutSight = 0;
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
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

void ChaseBehavior::setChaseSpeed(float speed) { m_chaseSpeed = speed; }

void ChaseBehavior::setMaxRange(float range) { m_maxRange = range; }

void ChaseBehavior::setMinRange(float range) { m_minRange = range; }

void ChaseBehavior::setUpdateFrequency(uint32_t) {}

bool ChaseBehavior::isChasing() const { return m_isChasing; }

bool ChaseBehavior::hasLineOfSight() const { return m_hasLineOfSight; }

void ChaseBehavior::onTargetReached(EntityHandle handle) {
  // Base implementation does nothing
  // Override in derived behaviors for specific actions
  (void)handle; // Mark parameter as intentionally unused
}

void ChaseBehavior::onTargetLost(EntityHandle handle) {
  // Base implementation does nothing
  // Override in derived behaviors for specific actions
  (void)handle; // Mark parameter as intentionally unused
}


void ChaseBehavior::handleNoLineOfSight(EntityHandle handle) {
  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  auto& hotData = edm.getHotDataByIndex(idx);

  if (m_timeWithoutSight < m_maxTimeWithoutSight) {
    Vector2D entityPos = hotData.transform.position;
    Vector2D const toLastKnown = m_lastKnownTargetPos - entityPos;
    float const distanceSquared = toLastKnown.lengthSquared();

    // Use squared distance threshold (10.0f squared = 100.0f)
    if (distanceSquared > 100.0f) {
      // Optimized normalization with inverse sqrt
      float invDistance = 1.0f / std::sqrt(distanceSquared);
      Vector2D const direction = toLastKnown * invDistance;
      hotData.transform.velocity = direction * (m_chaseSpeed * 0.8f);
    } else {
      hotData.transform.velocity = Vector2D(0, 0);
    }

    m_timeWithoutSight++;
  } else {
    // Timeout - stop chasing efficiently
    if (m_isChasing) {
      m_isChasing = false;
      hotData.transform.velocity = Vector2D(0, 0);
      onTargetLost(handle);
    }
  }
}

