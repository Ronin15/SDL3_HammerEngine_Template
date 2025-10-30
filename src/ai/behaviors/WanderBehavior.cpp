/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/internal/Crowd.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/internal/SpatialPriority.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

// Static thread-local RNG pool for memory optimization
thread_local std::uniform_real_distribution<float>
    WanderBehavior::s_angleDistribution{0.0f, 2.0f * M_PI};
thread_local std::uniform_int_distribution<Uint64>
    WanderBehavior::s_delayDistribution{0, 5000};

std::mt19937 &WanderBehavior::getSharedRNG() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return rng;
}

WanderBehavior::WanderBehavior(const HammerEngine::WanderBehaviorConfig& config)
    : m_config(config), m_speed(config.speed),
      m_changeDirectionInterval(config.changeDirectionIntervalMin),
      m_areaRadius(300.0f) {
}

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval,
                               float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval),
      m_areaRadius(areaRadius) {
  // Update config to match legacy parameters
  m_config.speed = speed;
  m_config.changeDirectionIntervalMin = changeDirectionInterval;
  m_config.changeDirectionIntervalMax = changeDirectionInterval + 5000.0f;
}

WanderBehavior::WanderMode
getDefaultModeForFrequency(WanderBehavior::WanderMode mode) {
  return mode;
}

WanderBehavior::WanderBehavior(WanderMode mode, float speed) : m_speed(speed) {
  setupModeDefaults(mode);
}

void WanderBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  // Initialize entity state
  EntityState &state = m_entityStates[entity];
  state.directionChangeTimer = 0.0f;
  state.startDelay = s_delayDistribution(getSharedRNG()) / 1000.0f; // Convert ms to seconds
  state.movementStarted = false;

  // Pre-allocate vector capacity to avoid incremental reallocations (~0.2ms savings)
  state.baseState.pathPoints.reserve(20);
  state.baseState.cachedNearbyPositions.reserve(50);

  // Choose initial direction (pass 0.0f since this is initialization)
  chooseNewDirection(entity, 0.0f);
}

void WanderBehavior::executeLogic(EntityPtr entity, float deltaTime) {
  if (!entity || !m_active)
    return;

  // Get entity state
  EntityState &state = m_entityStates[entity];

  // Update all timers
  state.directionChangeTimer += deltaTime;
  state.lastDirectionFlip += deltaTime;
  state.baseState.pathUpdateTimer += deltaTime;
  state.baseState.progressTimer += deltaTime;
  state.stallTimer += deltaTime;
  state.unstickTimer += deltaTime;
  state.baseState.lastCrowdAnalysis += deltaTime;
  if (state.baseState.pathRequestCooldown > 0.0f) state.baseState.pathRequestCooldown -= deltaTime;

  // Check if we need to start movement after delay
  if (!state.movementStarted) {
    if (state.directionChangeTimer >= state.startDelay) {
      state.movementStarted = true;
      Vector2D intended = state.currentDirection * m_speed;

      // PERFORMANCE OPTIMIZATION: Use cached collision data if available
      if (!state.baseState.cachedNearbyPositions.empty()) {
        applySeparationWithCache(entity, entity->getPosition(), intended,
                                 m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                                 state.baseState.lastSepVelocity, deltaTime, state.baseState.cachedNearbyPositions);
      } else {
        // Fallback to direct calculation on first frame
        applyDecimatedSeparation(entity, entity->getPosition(), intended,
                                 m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                                 state.baseState.lastSepVelocity, deltaTime);
      }
    }
    return;
  }

  updateWanderState(entity, deltaTime);

  // Try to follow a short path towards the current direction destination
  if (state.movementStarted) {
    // Dynamic movement distance based on local density and world scale
    float baseDistance = std::min(600.0f, m_areaRadius * 1.5f); // Increased base distance

    // PERFORMANCE FIX: Check local entity density less frequently to avoid expensive CollisionManager calls
    Vector2D position = entity->getPosition();

    // Cache crowd analysis results to avoid expensive collision queries every frame
    int nearbyCount = state.baseState.cachedNearbyCount;
    const std::vector<Vector2D>& nearbyPositions = state.baseState.cachedNearbyPositions;  // Use reference to avoid copy (~0.8ms savings)

    // PERFORMANCE FIX: Update crowd analysis every 3-5 seconds (was 333-500ms)
    // At 2000 entities: 5000 queries/sec → 500 queries/sec (90% reduction!)
    float crowdInterval = 3.0f + (entity->getID() % 200) * 0.01f; // 3-5 seconds range
    if (state.baseState.lastCrowdAnalysis >= crowdInterval) {
      float queryRadius = 120.0f;
      // Update cache directly (can't use const reference here)
      nearbyCount = AIInternal::GetNearbyEntitiesWithPositions(entity, position, queryRadius, state.baseState.cachedNearbyPositions);
      state.baseState.cachedNearbyCount = nearbyCount;
      state.baseState.lastCrowdAnalysis = 0.0f; // Reset timer
    }

     // Dynamic distance adjustment based on crowding
     // PERFORMANCE FIX: Simplified cluster calculations with higher thresholds
     float moveDistance = baseDistance;

     if (nearbyCount > m_config.crowdEscapeThreshold) {
       // Very high density: pick completely different target away from cluster
       moveDistance = baseDistance * m_config.crowdEscapeDistanceMultiplier; // Long distance to escape cluster

       // SIMPLIFIED: Direct escape direction without complex rotation
       if (!nearbyPositions.empty()) {
         Vector2D crowdCenter = std::accumulate(nearbyPositions.begin(), nearbyPositions.end(), Vector2D(0, 0));
         crowdCenter = crowdCenter / static_cast<float>(nearbyPositions.size());
         Vector2D escapeDirection = (position - crowdCenter).normalized();

         // Simple randomization using entity ID
         float randomOffset = (entity->getID() % 60 - 30) * 0.01f; // ±0.3 variation
         escapeDirection.setX(escapeDirection.getX() + randomOffset);
         escapeDirection.setY(escapeDirection.getY() + randomOffset);
         escapeDirection.normalize();

         // Override current direction with escape direction
         state.currentDirection = escapeDirection;
       }
     } else if (nearbyCount > 5) {
       // High density: encourage longer wandering to spread out
       moveDistance = baseDistance * 2.0f; // Up to 1200px movement for spreading

       // SIMPLIFIED: Lighter blending, no cluster center calculation
       state.currentDirection = state.currentDirection.normalized();
     } else if (nearbyCount > 2) {
       // Medium density: moderate expansion
       moveDistance = baseDistance * 1.3f;
     }

    // PERFORMANCE FIX: Use cached world bounds instead of expensive WorldManager call
    // Cache world bounds in entity state to avoid repeated WorldManager calls
    if (state.cachedBounds.maxX == 0.0f) { // Initialize cached bounds once
      float minX, minY, maxX, maxY;
      if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        state.cachedBounds.minX = minX;
        state.cachedBounds.minY = minY;
        state.cachedBounds.maxX = maxX;
        state.cachedBounds.maxY = maxY;
      } else {
        // No world loaded - skip boundary avoidance
        return;
      }
    }

    // BOUNDARY AVOIDANCE: Bias direction away from world edges to prevent stuck NPCs
    const float EDGE_THRESHOLD = m_config.edgeThreshold; // Start avoiding when close to edge
    Vector2D boundaryForce(0, 0);

    if (position.getX() < state.cachedBounds.minX + EDGE_THRESHOLD) {
      // Near left edge - push right
      float strength = 1.0f - ((position.getX() - state.cachedBounds.minX) / EDGE_THRESHOLD);
      boundaryForce = boundaryForce + Vector2D(strength, 0);
    } else if (position.getX() > state.cachedBounds.maxX - EDGE_THRESHOLD) {
      // Near right edge - push left
      float strength = 1.0f - ((state.cachedBounds.maxX - position.getX()) / EDGE_THRESHOLD);
      boundaryForce = boundaryForce + Vector2D(-strength, 0);
    }

    if (position.getY() < state.cachedBounds.minY + EDGE_THRESHOLD) {
      // Near top edge - push down
      float strength = 1.0f - ((position.getY() - state.cachedBounds.minY) / EDGE_THRESHOLD);
      boundaryForce = boundaryForce + Vector2D(0, strength);
    } else if (position.getY() > state.cachedBounds.maxY - EDGE_THRESHOLD) {
      // Near bottom edge - push up
      float strength = 1.0f - ((state.cachedBounds.maxY - position.getY()) / EDGE_THRESHOLD);
      boundaryForce = boundaryForce + Vector2D(0, -strength);
    }

    // Apply boundary avoidance to direction (blend with current direction)
    if (boundaryForce.lengthSquared() > 0.01f) {
      state.currentDirection = (state.currentDirection * 0.4f + boundaryForce.normalized() * 0.6f).normalized();
    }

    Vector2D dest = position + state.currentDirection * moveDistance;

    // Clamp destination as final safety net
    const float MARGIN = m_config.worldPaddingMargin;
    dest.setX(std::clamp(dest.getX(), state.cachedBounds.minX + MARGIN, state.cachedBounds.maxX - MARGIN));
    dest.setY(std::clamp(dest.getY(), state.cachedBounds.minY + MARGIN, state.cachedBounds.maxY - MARGIN));
    
    // Additional validation: don't pathfind to current position
    float distanceToGoal = (dest - position).length();
    if (distanceToGoal < 64.0f) { // Too close to current position
      return; // Skip pathfinding request entirely
    }

    // CACHE-AWARE PATHFINDING: Check for existing path first
    bool needsNewPath = state.baseState.pathPoints.empty() ||
                       state.baseState.currentPathIndex >= state.baseState.pathPoints.size() ||
                       state.baseState.pathUpdateTimer > 15.0f; // Only refresh after 15 seconds

    // OBSTACLE DETECTION: Force path refresh if stuck on obstacle (800ms without progress)
    bool stuckOnObstacle = state.baseState.progressTimer > 0.8f;
    if (stuckOnObstacle) {
      state.baseState.pathPoints.clear(); // Clear path to force refresh
      state.baseState.currentPathIndex = 0;
    }

    if ((needsNewPath || stuckOnObstacle) && state.baseState.pathRequestCooldown <= 0.0f) {
      // SMART REQUEST: Only request if goal significantly different from last request
      const float MIN_GOAL_CHANGE = m_config.minGoalChangeDistance; // Minimum distance change to justify new request
      bool goalChanged = true;
      if (!state.baseState.pathPoints.empty()) {
        Vector2D lastGoal = state.baseState.pathPoints.back();
        float goalDistance = (dest - lastGoal).length();
        goalChanged = (goalDistance >= MIN_GOAL_CHANGE);
      }

      if (goalChanged) {
        // ASYNC PATHFINDING: Use background processing for wandering behavior
        pathfinder().requestPath(
            entity->getID(), entity->getPosition(), dest,
            PathfinderManager::Priority::Normal,
            [this, entity](EntityID, const std::vector<Vector2D>& path) {
              auto stateIt = m_entityStates.find(entity);
              if (stateIt != m_entityStates.end() && !path.empty()) {
                stateIt->second.baseState.pathPoints = path;
                stateIt->second.baseState.currentPathIndex = 0;
                stateIt->second.baseState.pathUpdateTimer = 0.0f;
              }
            });
        // PERFORMANCE FIX: 30 second cooldown (was 5s)
        // At 2000 entities: 400 requests/sec → 67 requests/sec (83% reduction!)
        state.baseState.pathRequestCooldown = m_config.pathRequestCooldown;
      }
    }
    if (!state.baseState.pathPoints.empty() && state.baseState.currentPathIndex < state.baseState.pathPoints.size()) {
      // Follow current path; velocity will be updated inside followPathStep
      bool following = pathfinder().followPathStep(
          entity, entity->getPosition(), state.baseState.pathPoints, state.baseState.currentPathIndex,
          m_speed, state.baseState.navRadius);
      if (following) {
        // PERFORMANCE OPTIMIZATION: Use cached collision data from crowd analysis
        // This eliminates redundant collision queries (was querying every 2 seconds)
        applySeparationWithCache(entity, entity->getPosition(),
                                 entity->getVelocity(), m_speed, 28.0f, 0.30f,
                                 6, state.baseState.separationTimer, state.baseState.lastSepVelocity, deltaTime,
                                 state.baseState.cachedNearbyPositions);
      }
    } else {
      // Always apply base velocity (in case something external changed it)
      Vector2D intended = state.currentDirection * m_speed;
      // PERFORMANCE OPTIMIZATION: Use cached collision data
      applySeparationWithCache(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                               state.baseState.lastSepVelocity, deltaTime, state.baseState.cachedNearbyPositions);
    }
  }
}

void WanderBehavior::updateWanderState(EntityPtr entity, float deltaTime) {
  if (!entity)
    return;

  // Check if entity state exists before getting reference - prevents heap-use-after-free
  auto stateIt = m_entityStates.find(entity);
  if (stateIt == m_entityStates.end()) {
    return; // Entity state doesn't exist, nothing to update
  }
  
  EntityState &state = stateIt->second;

  // Get current velocity and compare with stored previous velocity
  Vector2D currentVelocity = entity->getVelocity();
  Vector2D previousVelocity = state.previousVelocity;

  // Check if there was a direction change that would cause a flip
  bool wouldFlip =
      (previousVelocity.getX() > 0.5f && currentVelocity.getX() < -0.5f) ||
      (previousVelocity.getX() < -0.5f && currentVelocity.getX() > 0.5f);

  float minimumFlipIntervalSeconds = m_minimumFlipInterval / 1000.0f; // Convert ms to seconds
  if (wouldFlip && state.lastDirectionFlip < minimumFlipIntervalSeconds) {
    // Prevent the flip by maintaining previous direction's sign but with new
    // magnitude
    float magnitude = currentVelocity.length();
    float xDir = (previousVelocity.getX() < 0) ? -1.0f : 1.0f;
    float yVal = currentVelocity.getY();

    // Create a new direction that doesn't cause a flip
    Vector2D stableVelocity(xDir * magnitude * 0.8f, yVal);
    stableVelocity.normalize();
    stableVelocity = stableVelocity * m_speed;

    // PERFORMANCE OPTIMIZATION: Use cached collision data
    applySeparationWithCache(entity, entity->getPosition(), stableVelocity,
                             m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                             state.baseState.lastSepVelocity, deltaTime, state.baseState.cachedNearbyPositions);
  } else if (wouldFlip) {
    // Record the flip - reset timer
    state.lastDirectionFlip = 0.0f;
  }

  // Stall detection: scale with configured wander speed to prevent constant false stalls
  float speed = entity->getVelocity().length();
  const float stallSpeed = std::max(m_config.stallSpeed, m_speed * 0.5f); // px/s
  const float stallSeconds = m_config.stallTimeout; // Seconds without progress before triggering unstuck
  if (speed < stallSpeed) {
    if (state.stallTimer >= stallSeconds) {
      // Clear path and pick a fresh direction to break clumps
      state.baseState.pathPoints.clear();
      state.baseState.currentPathIndex = 0;
      state.baseState.pathUpdateTimer = 0.0f;
      chooseNewDirection(entity, deltaTime);
      state.baseState.pathRequestCooldown = 0.6f; // prevent immediate re-request
      state.stallTimer = 0.0f;
      return;
    }
  } else {
    state.stallTimer = 0.0f;
  }

  // Check if it's time to change direction (convert interval from ms to seconds)
  float changeIntervalSeconds = m_changeDirectionInterval / 1000.0f;
  if (state.directionChangeTimer >= changeIntervalSeconds) {
    chooseNewDirection(entity, deltaTime);
    state.directionChangeTimer = 0.0f;
  }

  // Micro-jitter to break small jams (when moving slower than expected but not stalled)
  if (speed < (m_speed * 1.5f) && speed >= stallSpeed) {
    // Rotate current direction slightly
    float jitter = (s_angleDistribution(getSharedRNG()) - static_cast<float>(M_PI)) * 0.1f; // ~±18deg
    Vector2D dir = state.currentDirection;
    float c = std::cos(jitter), s = std::sin(jitter);
    Vector2D rotated(dir.getX() * c - dir.getY() * s, dir.getX() * s + dir.getY() * c);
    if (rotated.length() > 0.001f) {
      rotated.normalize();
      state.currentDirection = rotated;
      Vector2D intended = state.currentDirection * m_speed;
      // PERFORMANCE OPTIMIZATION: Use cached collision data
      applySeparationWithCache(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                               state.baseState.lastSepVelocity, deltaTime, state.baseState.cachedNearbyPositions);
    }
  }

  // No edge avoidance - let entities wander naturally like PatrolBehavior
  // The pathfinding system and world collision will handle actual boundaries
  
  // Update previous velocity for next frame's flip detection
  state.previousVelocity = currentVelocity;
}

void WanderBehavior::clean(EntityPtr entity) {
  if (entity) {
    // Stop the entity when behavior is cleaned up
    entity->setVelocity(Vector2D(0, 0));

    // Remove entity state
    m_entityStates.erase(entity);
  } else {
    // If entity is null, clean up all entity states
    m_entityStates.clear();
  }
}

void WanderBehavior::onMessage(EntityPtr entity, const std::string &message) {
  if (!entity)
    return;

  if (message == "pause") {
    setActive(false);
    entity->setVelocity(Vector2D(0, 0));
  } else if (message == "resume") {
    setActive(true);
    chooseNewDirection(entity, 0.0f); // Pass 0.0f since this is a message handler
  } else if (message == "new_direction") {
    chooseNewDirection(entity, 0.0f); // Pass 0.0f since this is a message handler
  } else if (message == "increase_speed") {
    m_speed *= 1.5f;
    if (m_active && m_entityStates.find(entity) != m_entityStates.end()) {
      entity->setVelocity(m_entityStates[entity].currentDirection * m_speed);
    }
  } else if (message == "decrease_speed") {
    m_speed *= 0.75f;
    if (m_active && m_entityStates.find(entity) != m_entityStates.end()) {
      entity->setVelocity(m_entityStates[entity].currentDirection * m_speed);
    }
  } else if (message == "release_entities") {
    // Clear all entity state when asked to release entities
    entity->setVelocity(Vector2D(0, 0));
    // Clean up entity state for this specific entity
    m_entityStates.erase(entity);
  }
}

std::string WanderBehavior::getName() const { return "Wander"; }

std::shared_ptr<AIBehavior> WanderBehavior::clone() const {
  auto cloned = std::make_shared<WanderBehavior>(
      m_speed, m_changeDirectionInterval, m_areaRadius);
  cloned->setCenterPoint(m_centerPoint);
  cloned->setActive(m_active);
  return cloned;
}

void WanderBehavior::setCenterPoint(const Vector2D &centerPoint) {
  m_centerPoint = centerPoint;
}

void WanderBehavior::setAreaRadius(float radius) { m_areaRadius = radius; }

void WanderBehavior::setSpeed(float speed) { m_speed = speed; }

void WanderBehavior::setChangeDirectionInterval(float interval) {
  m_changeDirectionInterval = interval;
}

void WanderBehavior::chooseNewDirection(EntityPtr entity, float deltaTime) {
  if (!entity)
    return;

  // Get entity-specific state
  EntityState &state = m_entityStates[entity];

  // If movement hasn't started yet, just set the direction but don't apply velocity
  bool applyVelocity = state.movementStarted;

  // Generate a random angle using shared RNG
  float angle = s_angleDistribution(getSharedRNG());
  // Convert angle to direction vector
  float x = std::cos(angle);
  float y = std::sin(angle);

  // Set the new direction
  state.currentDirection = Vector2D(x, y);

  // Apply the new direction to the entity only if movement has started
  if (applyVelocity) {
    Vector2D intended = state.currentDirection * m_speed;
    // PERFORMANCE OPTIMIZATION: Use cached collision data if available
    if (!state.baseState.cachedNearbyPositions.empty()) {
      applySeparationWithCache(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                               state.baseState.lastSepVelocity, deltaTime, state.baseState.cachedNearbyPositions);
    } else {
      // Fallback for initialization
      applyDecimatedSeparation(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.baseState.separationTimer,
                               state.baseState.lastSepVelocity, deltaTime);
    }
  }

  // NPC class now handles sprite flipping based on velocity
}

void WanderBehavior::setupModeDefaults(WanderMode mode) {
  // Use world bounds to set center point for world-scale wandering
  float minX, minY, maxX, maxY;
  if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    // WorldManager returns bounds in PIXELS; use directly
    float worldWidth = (maxX - minX);
    float worldHeight = (maxY - minY);

    // Set center point to world center
    m_centerPoint = Vector2D(worldWidth * 0.5f, worldHeight * 0.5f);
  } else {
    // Use a reasonable default center for a medium-sized world
    m_centerPoint = Vector2D(1000.0f, 1000.0f);
  }

  switch (mode) {
  case WanderMode::SMALL_AREA:
    // Personal space: around a well, fountain, or small plaza (~12 tiles at 32px/tile)
    // Encourages tight clustering and high path reuse for cache efficiency
    m_areaRadius = 400.0f;
    m_changeDirectionInterval = 1500.0f;
    break;

  case WanderMode::MEDIUM_AREA:
    // Village/building area: wander around a village or district (~37 tiles)
    // Balanced path diversity with good cache hit rates for grouped entities
    m_areaRadius = 1200.0f;
    m_changeDirectionInterval = 2500.0f;
    break;

  case WanderMode::LARGE_AREA:
    // Village + outskirts: explore village and surrounding areas (~75 tiles)
    // Wider exploration while maintaining reasonable path reuse
    m_areaRadius = 2400.0f;
    m_changeDirectionInterval = 3500.0f;
    break;

  case WanderMode::EVENT_TARGET:
    // Wander around a specific target location - 10X larger
    m_areaRadius = 2500.0f;
    m_changeDirectionInterval = 2000.0f;
    break;
  }
}
