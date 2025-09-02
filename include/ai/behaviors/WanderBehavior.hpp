/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef WANDER_BEHAVIOR_HPP
#define WANDER_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
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

  explicit WanderBehavior(float speed = 1.5f,
                          float changeDirectionInterval = 2000.0f,
                          float areaRadius = 300.0f);

  // Constructor with mode - automatically configures behavior based on mode
  explicit WanderBehavior(WanderMode mode, float speed = 2.0f);

  // No state management - handled by AI Manager
  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity) override;
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



  // Set the update frequency for staggering (every N frames)
  void setUpdateFrequency(uint32_t frequency) {
    m_updateFrequency = frequency > 0 ? frequency : 1;
  }

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;

public:
  // --- Staggering system overrides ---
  bool useStaggering() const { return true; }
  uint32_t getUpdateFrequency() const { return m_updateFrequency; }

private:
  // Called only on staggered frames for expensive logic
  void updateWanderState(EntityPtr entity);

  // Entity-specific state data
  struct EntityState {
    Vector2D currentDirection{0, 0};
    Uint64 lastDirectionChangeTime{0};
    Uint64 lastDirectionFlip{0};
    Uint64 startDelay{0};        // Random delay before entity starts moving
    bool movementStarted{false}; // Flag to track if movement has started
    // Path-following state
    std::vector<Vector2D> pathPoints;
    size_t currentPathIndex{0};
    Uint64 lastPathUpdate{0};
    Uint64 lastProgressTime{0};
    float lastNodeDistance{std::numeric_limits<float>::infinity()};
    float navRadius{18.0f};
    // Improved stall detection
    Uint64 stallStart{0};
    Vector2D lastStallPosition{0, 0};
    float stallPositionVariance{0.0f};
    Uint64 lastUnstickTime{0};
    // Unified cooldown management
    struct {
        Uint64 nextPathRequest{0};
        Uint64 stallRecoveryUntil{0};
        Uint64 behaviorChangeUntil{0};
        
        bool canRequestPath(Uint64 now) const {
            return now >= nextPathRequest && now >= stallRecoveryUntil;
        }
        
        void applyPathCooldown(Uint64 now, Uint64 cooldownMs = 800) {
            nextPathRequest = now + cooldownMs;
        }
        
        void applyStallCooldown(Uint64 now, Uint64 stallId = 0) {
            stallRecoveryUntil = now + 250 + (stallId % 400);
        }
    } cooldowns;

    // Constructor to ensure proper initialization
    EntityState()
        : currentDirection(0, 0), lastDirectionChangeTime(0),
          lastDirectionFlip(0), startDelay(0), movementStarted(false),
          pathPoints(), currentPathIndex(0), lastPathUpdate(0), 
          lastProgressTime(0), lastNodeDistance(std::numeric_limits<float>::infinity()),
          navRadius(18.0f), stallStart(0), lastStallPosition(0, 0), 
          stallPositionVariance(0.0f), lastUnstickTime(0) {}
  };

  // Map to store per-entity state using shared_ptr as key
  std::unordered_map<EntityPtr, EntityState> m_entityStates;

  // Shared behavior parameters
  float m_speed{1.5f};
  float m_changeDirectionInterval{2000.0f}; // milliseconds
  float m_areaRadius{300.0f};
  Vector2D m_centerPoint{0, 0};

  // Staggering system
  uint32_t m_updateFrequency{1}; // Default: every frame, will be set by mode

  // Flip stability properties
  Uint64 m_minimumFlipInterval{
      800}; // Minimum time between flips (milliseconds)

  // Shared RNG optimization - use thread-local static RNG pool
  // instead of per-instance RNG to reduce memory overhead
  static std::mt19937 &getSharedRNG();
  static thread_local std::uniform_real_distribution<float> s_angleDistribution;
  static thread_local std::uniform_int_distribution<Uint64> s_delayDistribution;

  // Choose a new random direction for the entity
  void chooseNewDirection(EntityPtr entity);

  // Mode setup helper
  void setupModeDefaults(WanderMode mode);
  
  // PATHFINDING CONSOLIDATION: All pathfinding now uses PathfindingScheduler pathway
  // (removed m_useAsyncPathfinding flag as it's no longer needed)

public:
};

#endif // WANDER_BEHAVIOR_HPP
