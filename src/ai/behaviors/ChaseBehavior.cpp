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
    : m_config(config), m_chaseSpeed(config.chaseSpeed), m_maxRange(1000.0f), m_minRange(50.0f) {}

ChaseBehavior::ChaseBehavior(float chaseSpeed, float maxRange, float minRange)
    : m_chaseSpeed(chaseSpeed), m_maxRange(maxRange), m_minRange(minRange) {
  // Update config to match legacy parameters
  m_config.chaseSpeed = chaseSpeed;
}

void ChaseBehavior::init(EntityHandle handle) {
  if (!handle.isValid())
    return;

  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  // Initialize behavior data in EDM (required for pathData access in executeLogic)
  edm.initBehaviorData(idx, BehaviorType::Chase);
  auto& data = edm.getBehaviorData(idx);
  data.setInitialized(true);

  // Initialize chase-specific state in EDM
  auto& chase = data.state.chase;
  chase.isChasing = false;
  chase.hasLineOfSight = false;
  chase.timeWithoutSight = 0.0f;
  chase.pathRequestCooldown = 0.0f;
  chase.stallRecoveryCooldown = 0.0f;
  chase.behaviorChangeCooldown = 0.0f;
  chase.crowdCheckTimer = 0.0f;
  chase.cachedChaserCount = 0;
  chase.recalcCounter = 0;
  chase.stallPositionVariance = 0.0f;
  chase.unstickTimer = 0.0f;

  const auto& hotData = edm.getHotDataByIndex(idx);
  chase.lastKnownTargetPos = hotData.transform.position;
  chase.currentDirection = Vector2D{0, 0};
  chase.lastStallPosition = Vector2D{0, 0};

  // Check if player is valid and in range
  const auto& aiMgr = AIManager::Instance();
  if (aiMgr.isPlayerValid()) {
    Vector2D entityPos = hotData.transform.position;
    Vector2D targetPos = aiMgr.getPlayerPosition();
    float const distance = (targetPos - entityPos).length();

    chase.isChasing = (distance <= m_maxRange);
    chase.hasLineOfSight = (distance <= m_maxRange); // Simplified line of sight check
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

// Cooldown helpers using EDM chase state
bool ChaseBehavior::canRequestPath(const BehaviorData& data) const {
  const auto& chase = data.state.chase;
  return chase.pathRequestCooldown <= 0.0f && chase.stallRecoveryCooldown <= 0.0f;
}

void ChaseBehavior::applyPathCooldown(BehaviorData& data, float cooldownSeconds) {
  data.state.chase.pathRequestCooldown = cooldownSeconds;
}

void ChaseBehavior::updateCooldowns(BehaviorData& data, float deltaTime) {
  auto& chase = data.state.chase;
  if (chase.pathRequestCooldown > 0.0f) chase.pathRequestCooldown -= deltaTime;
  if (chase.stallRecoveryCooldown > 0.0f) chase.stallRecoveryCooldown -= deltaTime;
  if (chase.behaviorChangeCooldown > 0.0f) chase.behaviorChangeCooldown -= deltaTime;
}

void ChaseBehavior::executeLogic(BehaviorContext& ctx) {
  if (!m_active) {
    return;
  }

  // Require EDM state - behavior data must be initialized
  if (!ctx.behaviorData || !ctx.behaviorData->isValid()) {
    return;
  }

  auto& data = *ctx.behaviorData;
  auto& chase = data.state.chase;

  // PERFORMANCE OPTIMIZATION: Cache crowd analysis results every 3-5 seconds
  // Uses BehaviorData common fields (lastCrowdAnalysis, cachedNearbyCount)
  data.lastCrowdAnalysis += ctx.deltaTime;
  float crowdCacheInterval = 3.0f + (static_cast<float>(ctx.entityId % 200) * 0.01f); // 3-5 seconds
  if (data.lastCrowdAnalysis >= crowdCacheInterval) {
    // Query nearby entities for separation steering - result stored in BehaviorData common
    constexpr float kCrowdQueryRadius = 80.0f;
    std::vector<Vector2D> nearbyPositions;
    data.cachedNearbyCount = AIInternal::GetNearbyEntitiesWithPositions(
        ctx.entityId, ctx.transform.position, kCrowdQueryRadius, nearbyPositions);

    // Store cluster center (can't store full vector in union)
    if (!nearbyPositions.empty()) {
      Vector2D sum{0, 0};
      for (const auto& pos : nearbyPositions) {
        sum = sum + pos;
      }
      data.cachedClusterCenter = sum * (1.0f / static_cast<float>(nearbyPositions.size()));
    }
    data.lastCrowdAnalysis = 0.0f;
  }

  // Use cached player info from context (lock-free, cached once per frame)
  if (!ctx.playerValid) {
    // No target available - clean exit from chase state
    if (chase.isChasing) {
      ctx.transform.velocity = Vector2D(0, 0);
      chase.isChasing = false;
      chase.hasLineOfSight = false;
    }
    return;
  }

  Vector2D entityPos = ctx.transform.position;
  Vector2D targetPos = ctx.playerPosition;
  float const distanceSquared = (targetPos - entityPos).lengthSquared();

  float const maxRangeSquared = m_maxRange * m_maxRange;
  float const minRangeSquared = m_minRange * m_minRange;

  // Update cooldown timers using EDM state
  updateCooldowns(data, ctx.deltaTime);

  // State: TARGET_IN_RANGE - Check if target is within chase range
  if (distanceSquared <= maxRangeSquared) {
    // Update line of sight (simplified - just distance check in BehaviorContext path)
    chase.hasLineOfSight = (distanceSquared <= maxRangeSquared);
    chase.lastKnownTargetPos = targetPos;

    // State: APPROACHING_TARGET - Move toward target if not too close
    if (distanceSquared > minRangeSquared && ctx.pathData) {
      // CACHE-AWARE CHASE: Smart pathfinding with target tracking
      // Use pre-fetched path data from context (no Instance() call needed)
      auto& pathData = *ctx.pathData;

      // Update path timer using PathData
      pathData.pathUpdateTimer += ctx.deltaTime;

      bool needsNewPath = false;

      // OPTIMIZED: Further reduced pathfinding frequency for better performance
      // Only request new path if:
      // 1. No current path exists, OR
      // 2. Target moved very significantly (>300px), OR
      // 3. Path is getting quite stale (>8 seconds old)
      const float PATH_INVALIDATION_DISTANCE = m_config.pathInvalidationDistance;
      const float PATH_REFRESH_INTERVAL = m_config.pathRefreshInterval;

      if (!pathData.hasPath || pathData.navIndex >= pathData.navPath.size()) {
        needsNewPath = true;
      } else if (pathData.pathUpdateTimer > PATH_REFRESH_INTERVAL) {
        needsNewPath = true;
      } else {
        // Check if target moved significantly from when path was computed
        Vector2D pathGoal = pathData.navPath.back();
        float const targetMovementSquared = (targetPos - pathGoal).lengthSquared();
        needsNewPath = (targetMovementSquared > PATH_INVALIDATION_DISTANCE * PATH_INVALIDATION_DISTANCE);
      }

      // OBSTACLE DETECTION: Force path refresh if stuck on obstacle
      bool const stuckOnObstacle = (pathData.progressTimer > 3.0f);
      if (stuckOnObstacle) {
        pathData.clear();
      }

      if ((needsNewPath || stuckOnObstacle) && canRequestPath(data)) {
        // PERFORMANCE: Use squared distance to avoid expensive sqrt
        float const minRangeCheckSquared = (m_minRange * 1.5f) * (m_minRange * 1.5f);
        if (distanceSquared < minRangeCheckSquared) {
          ctx.transform.velocity = Vector2D(0, 0);  // Stop movement when close enough
          return;
        }

        Vector2D goalPosition = targetPos;

        // EDM-integrated async pathfinding - result written directly to EDM
        pathfinder().requestPathToEDM(ctx.edmIndex, entityPos, goalPosition,
                                      PathfinderManager::Priority::High);
        applyPathCooldown(data, m_config.pathRequestCooldown);
      }

      // State: PATH_FOLLOWING - inline path following logic using EDM
      if (pathData.isFollowingPath()) {
        Vector2D waypoint = pathData.getCurrentWaypoint();
        Vector2D toWaypoint = waypoint - entityPos;
        float dist = toWaypoint.length();

        if (dist < m_navRadius) {
          pathData.advanceWaypoint();
          if (pathData.isFollowingPath()) {
            waypoint = pathData.getCurrentWaypoint();
            toWaypoint = waypoint - entityPos;
            dist = toWaypoint.length();
          }
        }

        bool following = (pathData.isFollowingPath() && dist > 0.001f);

        if (following) {
          Vector2D direction = toWaypoint / dist;
          ctx.transform.velocity = direction * m_chaseSpeed;
          pathData.progressTimer = 0.0f;

          // Simple crowd management - use cached chaser count for lateral spreading
          chase.crowdCheckTimer += ctx.deltaTime;
          if (chase.crowdCheckTimer >= m_config.crowdCheckInterval) {
            chase.crowdCheckTimer = 0.0f;
          }

          // High density: apply lateral spread to reduce clumping
          if (chase.cachedChaserCount > 3) {
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
          pathData.progressTimer = 0.0f;  // Reset EDM timer for stall detection
        }
      } else {
        // Direct movement toward target with crowd awareness
        Vector2D direction = (targetPos - entityPos);
        direction.normalize();

        // OPTIMIZATION: Use cached crowd count from BehaviorData common
        int const nearbyCount = data.cachedNearbyCount;

        if (nearbyCount > 1) {
          // Add minimal lateral offset to reduce perfect stacking while maintaining chase
          Vector2D const lateral(-direction.getY(), direction.getX());
          float offset = ((float)(ctx.entityId % 3) - 1.0f) * 20.0f; // Small spread: -20 to +20
          direction = direction + lateral * (offset / 400.0f); // Very small lateral bias
          direction.normalize();
        }

        // Set velocity directly - CollisionManager handles overlap resolution
        ctx.transform.velocity = direction * m_chaseSpeed;
        pathData.progressTimer = 0.0f;  // Reset EDM timer for stall detection
      }

      // Update chase state in EDM
      chase.isChasing = true;

      // SIMPLIFIED: Basic stall detection using PathData.stallTimer
      float currentSpeed = ctx.transform.velocity.length();
      const float stallThreshold = std::max(1.0f, m_chaseSpeed * m_config.stallSpeedMultiplier);
      const float stallTimeLimit = m_config.stallTimeout;

      if (currentSpeed < stallThreshold) {
        pathData.stallTimer += ctx.deltaTime;
        if (pathData.stallTimer >= stallTimeLimit) {
          // Simple stall recovery: clear path in EDM and request new one
          pathData.clear();
          pathData.stallTimer = 0.0f;

          // Light jitter to avoid deadlock (thread-safe)
          std::uniform_real_distribution<float> jitterDist(-0.1f, 0.1f);
          float jitter = jitterDist(getThreadLocalRNG());
          Vector2D const dir = (targetPos - entityPos).normalized();
          float c = std::cos(jitter), s = std::sin(jitter);
          Vector2D const rotated(dir.getX()*c - dir.getY()*s,
                         dir.getX()*s + dir.getY()*c);
          ctx.transform.velocity = rotated * m_chaseSpeed;
        }
      } else {
        pathData.stallTimer = 0.0f; // Reset when moving normally
      }

    } else {
      // State: TARGET_REACHED - Too close to target, stop movement
      if (chase.isChasing) {
        ctx.transform.velocity = Vector2D(0, 0);
        chase.isChasing = false;
      }
    }
  } else {
    // State: TARGET_OUT_OF_RANGE - Target is beyond chase range
    if (chase.isChasing) {
      chase.isChasing = false;
      chase.hasLineOfSight = false;
      ctx.transform.velocity = Vector2D(0, 0);

      // Clear pathfinding state from context (no Instance() call needed)
      if (ctx.pathData) {
        ctx.pathData->clear();
      }
    }
  }
}

void ChaseBehavior::clean(EntityHandle handle) {
  // Stop the entity's movement and clear EDM path data when cleaning up
  if (handle.isValid()) {
    auto& edm = EntityDataManager::Instance();
    size_t idx = edm.getIndex(handle);
    if (idx != SIZE_MAX) {
      edm.getHotDataByIndex(idx).transform.velocity = Vector2D(0, 0);
      edm.clearPathData(idx);  // Clear path state in EDM
      edm.clearBehaviorData(idx);  // Clear behavior state in EDM
    }
  }
}

void ChaseBehavior::onMessage(EntityHandle handle, const std::string &message) {
  auto& edm = EntityDataManager::Instance();
  size_t idx = handle.isValid() ? edm.getIndex(handle) : SIZE_MAX;
  if (idx == SIZE_MAX) return;

  auto& hotData = edm.getHotDataByIndex(idx);

  // Get behavior data for state updates
  BehaviorData* behaviorData = nullptr;
  if (edm.hasBehaviorData(idx)) {
    behaviorData = &edm.getBehaviorData(idx);
  }

  if (message == "pause") {
    setActive(false);
    hotData.transform.velocity = Vector2D(0, 0);
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
    if (behaviorData) {
      behaviorData->state.chase.isChasing = false;
      behaviorData->state.chase.hasLineOfSight = false;
    }
    hotData.transform.velocity = Vector2D(0, 0);
  } else if (message == "release_entities") {
    // Reset state when asked to release entities
    if (behaviorData) {
      behaviorData->state.chase.isChasing = false;
      behaviorData->state.chase.hasLineOfSight = false;
      behaviorData->state.chase.lastKnownTargetPos = Vector2D(0, 0);
      behaviorData->state.chase.timeWithoutSight = 0.0f;
    }
    hotData.transform.velocity = Vector2D(0, 0);
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

bool ChaseBehavior::isChasing() const {
  // Note: This returns a default value since state is now per-entity in EDM
  // Callers should query EDM directly for specific entity state
  return false;
}

bool ChaseBehavior::hasLineOfSight() const {
  // Note: This returns a default value since state is now per-entity in EDM
  // Callers should query EDM directly for specific entity state
  return false;
}

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


void ChaseBehavior::handleNoLineOfSight(EntityHandle handle, BehaviorData& data) {
  auto& edm = EntityDataManager::Instance();
  size_t idx = edm.getIndex(handle);
  if (idx == SIZE_MAX) return;

  auto& hotData = edm.getHotDataByIndex(idx);
  auto& chase = data.state.chase;

  if (chase.timeWithoutSight < static_cast<float>(m_maxTimeWithoutSight)) {
    Vector2D entityPos = hotData.transform.position;
    Vector2D const toLastKnown = chase.lastKnownTargetPos - entityPos;
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

    chase.timeWithoutSight += 1.0f;  // Increment as float
  } else {
    // Timeout - stop chasing efficiently
    if (chase.isChasing) {
      chase.isChasing = false;
      hotData.transform.velocity = Vector2D(0, 0);
      onTargetLost(handle);
    }
  }
}
