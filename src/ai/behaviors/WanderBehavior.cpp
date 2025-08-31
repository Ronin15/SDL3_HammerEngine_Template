/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/WanderBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/CollisionManager.hpp"
#include "ai/internal/PathFollow.hpp"
#include "collisions/AABB.hpp"
#include <algorithm>
#include <cmath>

// Static thread-local RNG pool for memory optimization
thread_local std::uniform_real_distribution<float>
    WanderBehavior::s_angleDistribution{0.0f, 2.0f * M_PI};
thread_local std::uniform_int_distribution<Uint64>
    WanderBehavior::s_delayDistribution{0, 5000};

std::mt19937 &WanderBehavior::getSharedRNG() {
  static thread_local std::mt19937 rng{std::random_device{}()};
  return rng;
}

WanderBehavior::WanderBehavior(float speed, float changeDirectionInterval,
                               float areaRadius)
    : m_speed(speed), m_changeDirectionInterval(changeDirectionInterval),
      m_areaRadius(areaRadius) {
  // Default to every frame, can be set by mode or setter
  m_updateFrequency = 1;
}

WanderBehavior::WanderMode
getDefaultModeForFrequency(WanderBehavior::WanderMode mode) {
  return mode;
}

WanderBehavior::WanderBehavior(WanderMode mode, float speed) : m_speed(speed) {
  setupModeDefaults(mode);
  // Set sensible default update frequency per mode
  switch (mode) {
  case WanderMode::SMALL_AREA:
    m_updateFrequency = 1; // every frame
    break;
  case WanderMode::MEDIUM_AREA:
    m_updateFrequency = 2; // every 2 frames
    break;
  case WanderMode::LARGE_AREA:
    m_updateFrequency = 4; // every 4 frames
    break;
  case WanderMode::EVENT_TARGET:
    m_updateFrequency = 1; // treat as small for responsiveness
    break;
  }
}

void WanderBehavior::init(EntityPtr entity) {
  if (!entity)
    return;

  // Initialize entity state
  EntityState &state = m_entityStates[entity];
  state.lastDirectionChangeTime = SDL_GetTicks();
  state.startDelay = s_delayDistribution(getSharedRNG());
  state.movementStarted = false;

  // Choose initial direction
  chooseNewDirection(entity);
}

void WanderBehavior::executeLogic(EntityPtr entity) {
  if (!entity || !m_active)
    return;

  // Get entity state
  EntityState &state = m_entityStates[entity];
  Uint64 currentTime = SDL_GetTicks();

  // Check if we need to start movement after delay
  if (!state.movementStarted) {
    if (currentTime - state.lastDirectionChangeTime >= state.startDelay) {
      state.movementStarted = true;
      entity->setVelocity(state.currentDirection * m_speed);
    }
    return;
  }

  // Only run expensive logic on staggered frames
  // (AIBehavior base will call this every frame, but only staggered frames
  // should do the heavy work) This is handled by executeLogicWithStaggering in
  // the base class. Here, we assume executeLogic is called only on the correct
  // frames.
  updateWanderState(entity);

  // Try to follow a short path towards the current direction destination
  if (state.movementStarted) {
    Uint64 now = SDL_GetTicks();
    
    // Dynamic movement distance based on local density and world scale
    float baseDistance = std::min(600.0f, m_areaRadius * 1.5f); // Increased base distance
    
    // Check local entity density for dynamic area expansion
    Vector2D position = entity->getPosition();
    int nearbyCount = 0;
    float queryRadius = 120.0f;
    
    // Quick density check using collision manager
    AABB queryArea(position.getX() - queryRadius, position.getY() - queryRadius,
                   position.getX() + queryRadius, position.getY() + queryRadius);
    std::vector<EntityID> nearbyEntities;
    CollisionManager::Instance().queryArea(queryArea, nearbyEntities);
    nearbyCount = static_cast<int>(nearbyEntities.size()) - 1; // Exclude self
    
     // Dynamic distance adjustment based on crowding
     float moveDistance = baseDistance;
     bool needsAlternativeTarget = false;
     
     if (nearbyCount > 5) {
       // Very high density: pick completely different target away from cluster
       needsAlternativeTarget = true;
       moveDistance = baseDistance * 3.0f; // Very long distance to escape cluster
       
       // Calculate cluster center and move in opposite direction
       Vector2D crowdCenter(0, 0);
       int validPositions = 0;
       for (auto id : nearbyEntities) {
         if (id != entity->getID()) {
           Vector2D entityCenter;
           if (CollisionManager::Instance().getBodyCenter(id, entityCenter)) {
             crowdCenter = crowdCenter + entityCenter;
             validPositions++;
           }
         }
       }
       
       if (validPositions > 0) {
         crowdCenter = crowdCenter / static_cast<float>(validPositions);
         Vector2D escapeDirection = (position - crowdCenter).normalized();
         
         // Add some randomness to prevent all NPCs picking same escape route
         float randomAngle = ((entity->getID() % 180) - 90) * M_PI / 180.0f; // -90 to +90 degrees
         float cosAngle = cosf(randomAngle);
         float sinAngle = sinf(randomAngle);
         Vector2D rotatedEscape(
           escapeDirection.getX() * cosAngle - escapeDirection.getY() * sinAngle,
           escapeDirection.getX() * sinAngle + escapeDirection.getY() * cosAngle
         );
         
         // Override current direction with escape direction
         state.currentDirection = rotatedEscape;
       }
     } else if (nearbyCount > 3) {
       // High density: encourage longer wandering to spread out
       moveDistance = baseDistance * 2.0f; // Up to 1200px movement for spreading
       
       // Also bias direction away from crowd center
       Vector2D crowdCenter(0, 0);
       int validPositions = 0;
       for (auto id : nearbyEntities) {
         if (id != entity->getID()) {
           Vector2D entityCenter;
           if (CollisionManager::Instance().getBodyCenter(id, entityCenter)) {
             crowdCenter = crowdCenter + entityCenter;
             validPositions++;
           }
         }
       }
       
       if (validPositions > 0) {
         crowdCenter = crowdCenter / static_cast<float>(validPositions);
         Vector2D awayFromCrowd = (position - crowdCenter).normalized();
         // Blend current direction with anti-crowd direction
         state.currentDirection = (state.currentDirection * 0.6f + awayFromCrowd * 0.4f).normalized();
       }
     } else if (nearbyCount > 1) {
       // Medium density: moderate expansion
       moveDistance = baseDistance * 1.3f;
     }
    
    Vector2D dest = position + state.currentDirection * moveDistance;
    // Clamp destination
    dest = AIInternal::ClampToWorld(dest);

    // Refresh short path using unified cooldown system
    if (state.cooldowns.canRequestPath(now)) {
      using namespace AIInternal;
      PathPolicy policy;
      policy.pathTTL = 2500;            // a bit lazier than combat
      policy.noProgressWindow = 300;
      policy.nodeRadius = state.navRadius;
      policy.allowDetours = true;
      policy.lateralBias = 0.0f;        // wandering is free-form
      Uint64 prev = state.lastPathUpdate;
      if (m_useAsyncPathfinding) {
        RefreshPathWithPolicyAsync(entity, entity->getPosition(), dest,
                                 state.pathPoints, state.currentPathIndex,
                                 state.lastPathUpdate, state.lastProgressTime,
                                 state.lastNodeDistance, policy, 3); // Low priority for wander
      } else {
        RefreshPathWithPolicy(entity, entity->getPosition(), dest,
                            state.pathPoints, state.currentPathIndex,
                            state.lastPathUpdate, state.lastProgressTime,
                            state.lastNodeDistance, policy);
      }
      if (state.lastPathUpdate != prev) {
        state.cooldowns.applyPathCooldown(now, 800); // cooldown even on success
      }
    }
    if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
      using namespace AIInternal;
      FollowPathStepWithPolicy(entity, entity->getPosition(), state.pathPoints,
                               state.currentPathIndex, m_speed,
                               state.navRadius, 0.0f);
    } else {
      // Always apply base velocity (in case something external changed it)
      entity->setVelocity(state.currentDirection * m_speed);
    }
  }
}

void WanderBehavior::updateWanderState(EntityPtr entity) {
  if (!entity)
    return;

  // Check if entity state exists before getting reference - prevents heap-use-after-free
  auto stateIt = m_entityStates.find(entity);
  if (stateIt == m_entityStates.end()) {
    return; // Entity state doesn't exist, nothing to update
  }
  
  EntityState &state = stateIt->second;
  Uint64 currentTime = SDL_GetTicks();

  Vector2D position = entity->getPosition();
  Vector2D previousVelocity = entity->getVelocity();

  // Control sprite flipping to avoid rapid flips
  Vector2D currentVelocity = entity->getVelocity();

  // Check if there was a direction change that would cause a flip
  bool wouldFlip =
      (previousVelocity.getX() > 0.5f && currentVelocity.getX() < -0.5f) ||
      (previousVelocity.getX() < -0.5f && currentVelocity.getX() > 0.5f);

  if (wouldFlip &&
      (currentTime - state.lastDirectionFlip < m_minimumFlipInterval)) {
    // Prevent the flip by maintaining previous direction's sign but with new
    // magnitude
    float magnitude = currentVelocity.length();
    float xDir = (previousVelocity.getX() < 0) ? -1.0f : 1.0f;
    float yVal = currentVelocity.getY();

    // Create a new direction that doesn't cause a flip
    Vector2D stableVelocity(xDir * magnitude * 0.8f, yVal);
    stableVelocity.normalize();
    stableVelocity = stableVelocity * m_speed;

    // Apply the stable velocity
    entity->setVelocity(stableVelocity);
  } else if (wouldFlip) {
    // Record the time of this flip
    state.lastDirectionFlip = currentTime;
  }

  // Stall detection: scale with configured wander speed to prevent constant false stalls
  float speed = entity->getVelocity().length();
  const float stallSpeed = std::max(0.5f, m_speed * 0.5f); // px/s
  const Uint64 stallMillis = 600; // ms
    if (speed < stallSpeed) {
      if (state.stallStart == 0) state.stallStart = currentTime;
      else if (currentTime - state.stallStart >= stallMillis) {
        // Clear path and pick a fresh direction to break clumps
        state.pathPoints.clear();
        state.currentPathIndex = 0;
        state.lastPathUpdate = currentTime;
        chooseNewDirection(entity);
        state.cooldowns.applyPathCooldown(currentTime, 600); // prevent immediate re-request
        state.stallStart = 0;
        return;
      }
    } else {
      state.stallStart = 0;
  }

  // Check if it's time to change direction
  if (currentTime - state.lastDirectionChangeTime >=
      m_changeDirectionInterval) {
    chooseNewDirection(entity);
    state.lastDirectionChangeTime = currentTime;
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
      entity->setVelocity(state.currentDirection * m_speed);
    }
  }

  // Edge avoidance: nudge direction inward if close to world edges
  float minX, minY, maxX, maxY;
  if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    const float TILE = 32.0f; const float margin = 24.0f;
    float worldMinX = minX * TILE + margin;
    float worldMinY = minY * TILE + margin;
    float worldMaxX = maxX * TILE - margin;
    float worldMaxY = maxY * TILE - margin;
    Vector2D pos = entity->getPosition();
    Vector2D push(0,0);
    if (pos.getX() < worldMinX) push.setX(1.0f);
    else if (pos.getX() > worldMaxX) push.setX(-1.0f);
    if (pos.getY() < worldMinY) push.setY(1.0f);
    else if (pos.getY() > worldMaxY) push.setY(-1.0f);
    if (push.length() > 0.1f) {
      push.normalize();
      state.currentDirection = push;
      state.pathPoints.clear();
      state.currentPathIndex = 0;
      entity->setVelocity(state.currentDirection * m_speed);
      state.lastDirectionChangeTime = currentTime;
    }
  }
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
    chooseNewDirection(entity);
  } else if (message == "new_direction") {
    chooseNewDirection(entity);
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

void WanderBehavior::chooseNewDirection(EntityPtr entity) {
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
    entity->setVelocity(state.currentDirection * m_speed);
  }

  // NPC class now handles sprite flipping based on velocity
}

void WanderBehavior::setupModeDefaults(WanderMode mode) {
  // Use world bounds to set center point for world-scale wandering
  float minX, minY, maxX, maxY;
  if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
    const float TILE = 32.0f;
    float worldWidth = (maxX - minX) * TILE;
    float worldHeight = (maxY - minY) * TILE;
    
    // Set center point to world center
    m_centerPoint = Vector2D(worldWidth * 0.5f, worldHeight * 0.5f);
  } else {
    // Fallback center point if world bounds unavailable
    m_centerPoint = Vector2D(1600.0f, 1600.0f); // Default center for a large world
  }

  switch (mode) {
  case WanderMode::SMALL_AREA:
    // Small personal space around current position
    m_areaRadius = 120.0f;
    m_changeDirectionInterval = 1500.0f;
    break;

  case WanderMode::MEDIUM_AREA:
    // Room/building sized area - much larger for world scale
    m_areaRadius = 400.0f;
    m_changeDirectionInterval = 2500.0f;
    break;

  case WanderMode::LARGE_AREA:
    // Village/district sized - true world-scale wandering
    m_areaRadius = 800.0f;
    m_changeDirectionInterval = 3500.0f;
    break;

  case WanderMode::EVENT_TARGET:
    // Wander around a specific target location
    m_areaRadius = 250.0f;
    m_changeDirectionInterval = 2000.0f;
    break;
  }
}
