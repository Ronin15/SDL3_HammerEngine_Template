/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "ai/behaviors/WanderBehavior.hpp"
#include "managers/AIManager.hpp"
#include "managers/WorldManager.hpp"
#include <algorithm>
#include <cmath>

// Static thread-local RNG pool for memory optimization
thread_local std::uniform_real_distribution<float>
    WanderBehavior::s_angleDistribution{0.0f, 2.0f * M_PI};
thread_local std::uniform_real_distribution<float>
    WanderBehavior::s_wanderOffscreenChance{0.0f, 1.0f};
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
  chooseNewDirection(entity, false);
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
    // Refresh short path periodically or when finished
    const Uint64 pathTTL = 2500;
    if ((state.pathPoints.empty() || state.currentPathIndex >= state.pathPoints.size() || (now - state.lastPathUpdate) > pathTTL) && now >= state.nextPathAllowed) {
      // Pick a target ahead in the current direction
      Vector2D dest = entity->getPosition() + state.currentDirection * std::min(200.0f, m_areaRadius);
      // Clamp destination to world bounds with a margin
      float minX, minY, maxX, maxY;
      if (WorldManager::Instance().getWorldBounds(minX, minY, maxX, maxY)) {
        const float TILE = 32.0f; const float margin = 16.0f;
        float worldMinX = minX * TILE + margin;
        float worldMinY = minY * TILE + margin;
        float worldMaxX = maxX * TILE - margin;
        float worldMaxY = maxY * TILE - margin;
        dest.setX(std::clamp(dest.getX(), worldMinX, worldMaxX));
        dest.setY(std::clamp(dest.getY(), worldMinY, worldMaxY));
      }
      AIManager::Instance().requestPath(entity, entity->getPosition(), dest);
      state.pathPoints = AIManager::Instance().getPath(entity);
      state.currentPathIndex = 0;
      state.lastPathUpdate = now;
      state.nextPathAllowed = now + 800; // cooldown even on success
    }
    if (!state.pathPoints.empty() && state.currentPathIndex < state.pathPoints.size()) {
      Vector2D node = state.pathPoints[state.currentPathIndex];
      Vector2D dir = node - entity->getPosition();
      float len = dir.length();
      if (len > 0.01f) {
        dir = dir * (1.0f / len);
        entity->setVelocity(dir * m_speed);
      }
      if ((node - entity->getPosition()).length() <= state.navRadius) {
        ++state.currentPathIndex;
      }
    } else {
      // Always apply base velocity (in case something external changed it)
      entity->setVelocity(state.currentDirection * m_speed);
    }
  }
}

void WanderBehavior::updateWanderState(EntityPtr entity) {
  if (!entity)
    return;

  EntityState &state = m_entityStates[entity];
  Uint64 currentTime = SDL_GetTicks();

  Vector2D position = entity->getPosition();
  Vector2D previousVelocity = entity->getVelocity();

  // Check if entity is well offscreen and reset if needed
  if (state.resetScheduled && isWellOffscreen(position)) {
    resetEntityPosition(entity);
    state.resetScheduled = false;
    return;
  }

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
        state.nextPathAllowed = currentTime + 600; // prevent immediate re-request
        state.stallStart = 0;
        return;
      }
    } else {
      state.stallStart = 0;
  }

  // Check if it's time to change direction
  if (currentTime - state.lastDirectionChangeTime >=
      m_changeDirectionInterval) {
    // Decide if we should wander offscreen
    bool shouldWanderOffscreen =
        (s_wanderOffscreenChance(getSharedRNG()) < m_offscreenProbability);
    chooseNewDirection(entity, shouldWanderOffscreen);
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
  cloned->setScreenDimensions(m_screenWidth, m_screenHeight);
  cloned->setOffscreenProbability(m_offscreenProbability);
  cloned->setActive(m_active);
  return cloned;
}

void WanderBehavior::setCenterPoint(const Vector2D &centerPoint) {
  m_centerPoint = centerPoint;

  // Estimate screen dimensions based on center point
  // (We'll assume the center is roughly in the middle of the screen)
  m_screenWidth = m_centerPoint.getX() * 2.0f;
  m_screenHeight = m_centerPoint.getY() * 2.0f;
}

void WanderBehavior::setAreaRadius(float radius) { m_areaRadius = radius; }

void WanderBehavior::setSpeed(float speed) { m_speed = speed; }

void WanderBehavior::setChangeDirectionInterval(float interval) {
  m_changeDirectionInterval = interval;
}

void WanderBehavior::setScreenDimensions(float width, float height) {
  m_screenWidth = width;
  m_screenHeight = height;
}

void WanderBehavior::setOffscreenProbability(float probability) {
  m_offscreenProbability = std::max(0.0f, std::min(1.0f, probability));
}

bool WanderBehavior::isWellOffscreen(const Vector2D &position) const {
  const float buffer =
      100.0f; // Distance past the edge to consider "well offscreen"
  return position.getX() < -buffer ||
         position.getX() > m_screenWidth + buffer ||
         position.getY() < -buffer || position.getY() > m_screenHeight + buffer;
}

void WanderBehavior::resetEntityPosition(EntityPtr entity) {
  if (!entity)
    return;

  // Ensure entity state exists
  m_entityStates.try_emplace(entity, EntityState{});

  // Calculate entry point on the opposite side of the screen
  Vector2D position = entity->getPosition();
  Vector2D newPosition(0.0f, 0.0f);

  // Determine which side to come in from (opposite of where the entity exited)
  if (position.getX() < 0) {
    // Went off left side, come in from right using shared RNG
    newPosition.setX(m_screenWidth - 50.0f);
    newPosition.setY(s_wanderOffscreenChance(getSharedRNG()) * m_screenHeight);
  } else if (position.getX() > m_screenWidth) {
    // Went off right side, come in from left using shared RNG
    newPosition.setX(50.0f);
    newPosition.setY(s_wanderOffscreenChance(getSharedRNG()) * m_screenHeight);
  } else if (position.getY() < 0) {
    // Went off top, come in from bottom using shared RNG
    newPosition.setX(s_wanderOffscreenChance(getSharedRNG()) * m_screenWidth);
    newPosition.setY(m_screenHeight - 50.0f);
  } else {
    // Went off bottom, come in from top using shared RNG
    newPosition.setX(s_wanderOffscreenChance(getSharedRNG()) * m_screenWidth);
    newPosition.setY(50.0f);
  }
  // Set new position and choose a new direction
  entity->setPosition(newPosition);
  chooseNewDirection(entity, false);
}

void WanderBehavior::chooseNewDirection(EntityPtr entity,
                                        bool wanderOffscreen) {
  if (!entity)
    return;

  // Get entity-specific state
  EntityState &state = m_entityStates[entity];

  // Track if we're currently wandering offscreen
  state.currentlyWanderingOffscreen = wanderOffscreen;

  // If movement hasn't started yet, just set the direction but don't apply
  // velocity
  bool applyVelocity = state.movementStarted;

  if (wanderOffscreen) {
    // Start wandering toward edge of screen by picking a direction toward
    // nearest edge
    Vector2D position = entity->getPosition();

    // Find closest edge and set direction toward it
    float distToLeft = position.getX();
    float distToRight = m_screenWidth - position.getX();
    float distToTop = position.getY();
    float distToBottom = m_screenHeight - position.getY();

    // Find minimum distance to edge
    float minDist =
        std::min({distToLeft, distToRight, distToTop, distToBottom});

    // Set direction toward closest edge
    if (minDist == distToLeft) {
      state.currentDirection = Vector2D(-1.0f, 0.0f);
    } else if (minDist == distToRight) {
      state.currentDirection = Vector2D(1.0f, 0.0f);
    } else if (minDist == distToTop) {
      state.currentDirection = Vector2D(0.0f, -1.0f);
    } else {
      state.currentDirection = Vector2D(0.0f, 1.0f);
    }

    // Add some randomness to the direction
    float randomAngle = (s_angleDistribution(getSharedRNG()) - M_PI) *
                        0.2f; // Small angle variation
    float x = state.currentDirection.getX() * std::cos(randomAngle) -
              state.currentDirection.getY() * std::sin(randomAngle);
    float y = state.currentDirection.getX() * std::sin(randomAngle) +
              state.currentDirection.getY() * std::cos(randomAngle);
    state.currentDirection = Vector2D(x, y);
    state.currentDirection.normalize();

    // Schedule a reset once we go offscreen
    state.resetScheduled = true;
  } else {
    // Generate a random angle using shared RNG
    float angle = s_angleDistribution(getSharedRNG());
    // Convert angle to direction vector
    float x = std::cos(angle);
    float y = std::sin(angle);

    // Set the new direction
    state.currentDirection = Vector2D(x, y);
  }

  // Apply the new direction to the entity only if movement has started
  if (applyVelocity) {
    entity->setVelocity(state.currentDirection * m_speed);
  }

  // NPC class now handles sprite flipping based on velocity
}

void WanderBehavior::setupModeDefaults(WanderMode mode, float screenWidth,
                                       float screenHeight) {
  m_screenWidth = screenWidth;
  m_screenHeight = screenHeight;

  // Set center point to screen center by default
  m_centerPoint = Vector2D(screenWidth * 0.5f, screenHeight * 0.5f);

  switch (mode) {
  case WanderMode::SMALL_AREA:
    // Small wander area - personal space around position
    m_areaRadius = 75.0f;
    m_changeDirectionInterval = 1500.0f; // Change direction more frequently
    m_offscreenProbability = 0.05f;      // Very low chance to go offscreen
    break;

  case WanderMode::MEDIUM_AREA:
    // Medium wander area - room/building sized
    m_areaRadius = 200.0f;
    m_changeDirectionInterval = 2500.0f; // Moderate direction changes
    m_offscreenProbability = 0.10f;      // Low chance to go offscreen
    break;

  case WanderMode::LARGE_AREA:
    // Large wander area - village/district sized
    m_areaRadius = 450.0f;
    m_changeDirectionInterval = 3500.0f; // Less frequent direction changes
    m_offscreenProbability = 0.20f;      // Higher chance to explore offscreen
    break;

  case WanderMode::EVENT_TARGET:
    // Wander around a specific target location (will be set later)
    m_areaRadius = 150.0f;
    m_changeDirectionInterval = 2000.0f; // Standard direction changes
    m_offscreenProbability = 0.05f;      // Stay near the target
    break;
  }
}
