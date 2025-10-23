/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WANDER_BEHAVIOR_HPP
#define WANDER_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "utils/Vector2D.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

class WanderBehavior : public AIBehavior {
public:
  enum class WanderMode {
    SMALL_AREA,  // Small wander area (around current position)
    MEDIUM_AREA, // Medium wander area (room/building sized)
    LARGE_AREA,  // Large wander area (village/district sized)
    EVENT_TARGET // Wander around a specific target location
  };

  explicit WanderBehavior(const HammerEngine::WanderBehaviorConfig& config = HammerEngine::WanderBehaviorConfig{});

  // Legacy constructors for backward compatibility
  explicit WanderBehavior(float speed, float changeDirectionInterval, float areaRadius);

  // Constructor with mode - automatically configures behavior based on mode
  explicit WanderBehavior(WanderMode mode, float speed = 2.0f);

  // No state management - handled by AI Manager
  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity, float deltaTime) override;
  void clean(EntityPtr entity) override;
  void onMessage(EntityPtr entity, const std::string &message) override;
  std::string getName() const override;

  // Set a new center point for wandering
  void setCenterPoint(const Vector2D &centerPoint);

  // Set the area radius for wandering
  void setAreaRadius(float radius);

  // Set the speed of movement
  void setSpeed(float speed);

  // Set how often the direction changes
  void setChangeDirectionInterval(float interval);



  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;

private:
  void updateWanderState(EntityPtr entity, float deltaTime);

  // Entity-specific state data
  struct EntityState {
    // Base AI behavior state (pathfinding, separation, cooldowns, crowd cache)
    AIBehaviorState baseState;

    // Wander-specific state
    Vector2D currentDirection{0, 0};
    Vector2D previousVelocity{0, 0}; // Store previous frame velocity for flip detection
    float directionChangeTimer{0.0f}; // Accumulates deltaTime
    float lastDirectionFlip{0.0f};    // Time since last flip
    float startDelay{0.0f};           // Random delay before entity starts moving
    bool movementStarted{false};      // Flag to track if movement has started

    // Improved stall detection
    float stallTimer{0.0f};
    Vector2D lastStallPosition{0, 0};
    float stallPositionVariance{0.0f};
    float unstickTimer{0.0f};

    // Performance optimization: cached world bounds to avoid repeated WorldManager calls
    struct {
      float minX{0.0f}, minY{0.0f}, maxX{0.0f}, maxY{0.0f};
    } cachedBounds;

    // Constructor to ensure proper initialization
    EntityState() {
      baseState.navRadius = 18.0f; // Wander-specific nav radius
    }
  };

  // Map to store per-entity state using shared_ptr as key
  std::unordered_map<EntityPtr, EntityState> m_entityStates;

  // Configuration
  HammerEngine::WanderBehaviorConfig m_config;

  // Shared behavior parameters (legacy - now derived from config)
  float m_speed{1.5f};
  float m_changeDirectionInterval{2000.0f}; // milliseconds
  float m_areaRadius{300.0f};
  Vector2D m_centerPoint{0, 0};

  // Flip stability properties
  Uint64 m_minimumFlipInterval{
      800}; // Minimum time between flips (milliseconds)

  // Shared RNG optimization - use thread-local static RNG pool
  // instead of per-instance RNG to reduce memory overhead
  static std::mt19937 &getSharedRNG();
  static thread_local std::uniform_real_distribution<float> s_angleDistribution;
  static thread_local std::uniform_int_distribution<Uint64> s_delayDistribution;

  // Choose a new random direction for the entity
  void chooseNewDirection(EntityPtr entity, float deltaTime);

  // Mode setup helper
  void setupModeDefaults(WanderMode mode);
  
  // PATHFINDING CONSOLIDATION: All pathfinding now uses PathfindingScheduler pathway
  // (removed m_useAsyncPathfinding flag as it's no longer needed)

public:
};

#endif // WANDER_BEHAVIOR_HPP
