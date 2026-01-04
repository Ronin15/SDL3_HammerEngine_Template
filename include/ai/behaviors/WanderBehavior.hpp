/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WANDER_BEHAVIOR_HPP
#define WANDER_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/Vector2D.hpp"

#include <SDL3/SDL.h>
#include <memory>
#include <random>

class WanderBehavior : public AIBehavior {
public:
  enum class WanderMode : uint8_t {
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

  // Core behavior methods
  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;  // Lock-free hot path
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
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
  // Cached world bounds (static - same for all entities, populated once)
  struct WorldBoundsCache {
    float minX{0.0f}, minY{0.0f}, maxX{0.0f}, maxY{0.0f};
    bool initialized{false};
  };
  static WorldBoundsCache s_worldBounds;

  // Helper methods for executeLogic refactoring (use BehaviorContext for lock-free access)
  // All entity state stored in EDM BehaviorData (indexed by edmIndex)
  void updateTimers(BehaviorData& data, float deltaTime, size_t edmIndex);
  bool handleStartDelay(BehaviorContext& ctx, BehaviorData& data);
  float calculateMoveDistance(const BehaviorData& data, const Vector2D& position, float baseDistance);
  void applyBoundaryAvoidance(BehaviorData& data, const Vector2D& position);
  void handlePathfinding(const BehaviorContext& ctx, const Vector2D& dest);
  void handleMovement(BehaviorContext& ctx, BehaviorData& data);

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

  // Choose a new random direction for the entity (lock-free version)
  void chooseNewDirection(BehaviorContext& ctx, BehaviorData& data);

  // Mode setup helper
  void setupModeDefaults(WanderMode mode);
  
  // PATHFINDING CONSOLIDATION: All pathfinding now uses PathfindingScheduler pathway
  // (removed m_useAsyncPathfinding flag as it's no longer needed)

public:
};

#endif // WANDER_BEHAVIOR_HPP
