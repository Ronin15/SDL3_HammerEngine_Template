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
      Vector2D intended = state.currentDirection * m_speed;
      // Separation decimation: compute at most every 2 ticks
      applyDecimatedSeparation(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.lastSepTick,
                               state.lastSepVelocity);
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
    
    // PERFORMANCE FIX: Check local entity density less frequently to avoid expensive CollisionManager calls
    Vector2D position = entity->getPosition();
    float queryRadius = 120.0f;
    
    // Cache crowd analysis results to avoid expensive collision queries every frame
    int nearbyCount = state.cachedNearbyCount;
    std::vector<Vector2D> nearbyPositions = state.cachedNearbyPositions;
    
    // Only update crowd analysis every 20-30 frames (333-500ms at 60 FPS) + entity staggering  
    Uint32 frameInterval = 333 + (entity->getID() % 10) * 17; // 333-500ms range
    if (now - state.lastCrowdAnalysis > frameInterval) {
      nearbyCount = AIInternal::GetNearbyEntitiesWithPositions(entity, position, queryRadius, nearbyPositions);
      state.cachedNearbyCount = nearbyCount;
      state.cachedNearbyPositions = nearbyPositions;
      state.lastCrowdAnalysis = now;
    }
    
     // Dynamic distance adjustment based on crowding
     float moveDistance = baseDistance;
     
     if (nearbyCount > 5) {
       // Very high density: pick completely different target away from cluster
       moveDistance = baseDistance * 3.0f; // Very long distance to escape cluster
       
       // Calculate cluster center and move in opposite direction
       if (!nearbyPositions.empty()) {
         Vector2D crowdCenter(0, 0);
         for (const auto &pos : nearbyPositions) {
           crowdCenter = crowdCenter + pos;
         }
         crowdCenter = crowdCenter / static_cast<float>(nearbyPositions.size());
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
       if (!nearbyPositions.empty()) {
         Vector2D crowdCenter(0, 0);
         for (const auto &pos : nearbyPositions) {
           crowdCenter = crowdCenter + pos;
         }
         crowdCenter = crowdCenter / static_cast<float>(nearbyPositions.size());
         Vector2D awayFromCrowd = (position - crowdCenter).normalized();
         // Blend current direction with anti-crowd direction
         state.currentDirection = (state.currentDirection * 0.6f + awayFromCrowd * 0.4f).normalized();
       }
     } else if (nearbyCount > 1) {
       // Medium density: moderate expansion
       moveDistance = baseDistance * 1.3f;
     }
    
    Vector2D dest = position + state.currentDirection * moveDistance;
    
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
        // Fallback bounds for performance
        state.cachedBounds.minX = 0.0f;
        state.cachedBounds.minY = 0.0f;
        state.cachedBounds.maxX = 3200.0f;
        state.cachedBounds.maxY = 3200.0f;
      }
    }
    
    // Use cached bounds for validation
    const float MARGIN = 256.0f;
    dest.setX(std::clamp(dest.getX(), state.cachedBounds.minX + MARGIN, state.cachedBounds.maxX - MARGIN));
    dest.setY(std::clamp(dest.getY(), state.cachedBounds.minY + MARGIN, state.cachedBounds.maxY - MARGIN));
    
    // Additional validation: don't pathfind to current position
    float distanceToGoal = (dest - position).length();
    if (distanceToGoal < 64.0f) { // Too close to current position
      return; // Skip pathfinding request entirely
    }

    // CACHE-AWARE PATHFINDING: Check for existing path first
    bool needsNewPath = state.pathPoints.empty() || 
                       state.currentPathIndex >= state.pathPoints.size() ||
                       (now - state.lastPathUpdate) > 15000; // Only refresh after 15 seconds
    
    if (needsNewPath && state.cooldowns.canRequestPath(now)) {
      // SMART REQUEST: Only request if goal significantly different from last request
      static constexpr float MIN_GOAL_CHANGE = 200.0f; // Minimum distance change to justify new request
      bool goalChanged = true;
      if (!state.pathPoints.empty()) {
        Vector2D lastGoal = state.pathPoints.back();
        float goalDistance = (dest - lastGoal).length();
        goalChanged = (goalDistance >= MIN_GOAL_CHANGE);
      }
      
      if (goalChanged) {
        // ASYNC PATHFINDING: Use background processing for wandering behavior
        PathfinderManager::Instance().requestPath(
            entity->getID(), entity->getPosition(), dest,
            AIInternal::PathPriority::Normal,
            [this, entity](EntityID, const std::vector<Vector2D>& path) {
              auto stateIt = m_entityStates.find(entity);
              if (stateIt != m_entityStates.end() && !path.empty()) {
                stateIt->second.pathPoints = path;
                stateIt->second.currentPathIndex = 0;
                stateIt->second.lastPathUpdate = SDL_GetTicks();
              }
            });
        state.cooldowns.applyPathCooldown(now, 5000); // Aggressive cooldown: 5 seconds between path requests
      }
    }
    if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
      // Follow current path; velocity will be updated inside followPathStep
      bool following = PathfinderManager::Instance().followPathStep(
          entity, entity->getPosition(), state.pathPoints, state.currentPathIndex,
          m_speed, state.navRadius);
      if (following) {
        // Apply decimated separation while following
        applyDecimatedSeparation(entity, entity->getPosition(),
                                 entity->getVelocity(), m_speed, 28.0f, 0.30f,
                                 6, state.lastSepTick, state.lastSepVelocity);
      }
    } else {
      // Always apply base velocity (in case something external changed it)
      Vector2D intended = state.currentDirection * m_speed;
      applyDecimatedSeparation(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.lastSepTick,
                               state.lastSepVelocity);
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

    // Apply the stable velocity with separation
    applyDecimatedSeparation(entity, entity->getPosition(), stableVelocity,
                             m_speed, 28.0f, 0.30f, 6, state.lastSepTick,
                             state.lastSepVelocity);
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
      Vector2D intended = state.currentDirection * m_speed;
      applyDecimatedSeparation(entity, entity->getPosition(), intended,
                               m_speed, 28.0f, 0.30f, 6, state.lastSepTick,
                               state.lastSepVelocity);
    }
  }

  // No edge avoidance - let entities wander naturally like PatrolBehavior
  // The pathfinding system and world collision will handle actual boundaries
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
    Vector2D intended = state.currentDirection * m_speed;
    applyDecimatedSeparation(entity, entity->getPosition(), intended,
                             m_speed, 28.0f, 0.30f, 6, state.lastSepTick,
                             state.lastSepVelocity);
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
    // Use a reasonable default center for a medium-sized world
    m_centerPoint = Vector2D(1000.0f, 1000.0f);
  }

  switch (mode) {
  case WanderMode::SMALL_AREA:
    // Small personal space around current position - 10X larger
    m_areaRadius = 1200.0f;
    m_changeDirectionInterval = 1500.0f;
    break;

  case WanderMode::MEDIUM_AREA:
    // Room/building sized area - 10X larger for world scale
    m_areaRadius = 4000.0f;
    m_changeDirectionInterval = 2500.0f;
    break;

  case WanderMode::LARGE_AREA:
    // Village/district sized - 10X larger for true world-scale wandering
    m_areaRadius = 8000.0f;
    m_changeDirectionInterval = 3500.0f;
    break;

  case WanderMode::EVENT_TARGET:
    // Wander around a specific target location - 10X larger
    m_areaRadius = 2500.0f;
    m_changeDirectionInterval = 2000.0f;
    break;
  }
}
