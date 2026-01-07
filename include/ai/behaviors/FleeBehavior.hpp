/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef FLEE_BEHAVIOR_HPP
#define FLEE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>

class FleeBehavior : public AIBehavior {
public:
  enum class FleeMode : uint8_t {
    PANIC_FLEE,        // Run away in panic (fast, erratic)
    STRATEGIC_RETREAT, // Calculated retreat (slower, planned)
    EVASIVE_MANEUVER,  // Zigzag pattern while fleeing
    SEEK_COVER         // Flee towards cover/safe zones
  };

  explicit FleeBehavior(float fleeSpeed = 4.0f, float detectionRange = 400.0f,
                        float safeDistance = 600.0f);

  // Constructor with mode
  explicit FleeBehavior(FleeMode mode, float fleeSpeed = 4.0f,
                        float detectionRange = 400.0f);

  // Constructor with config
  explicit FleeBehavior(const HammerEngine::FleeBehaviorConfig& config,
                        FleeMode mode = FleeMode::PANIC_FLEE);

  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
  std::string getName() const override;

  // Configuration methods
  void setFleeSpeed(float speed);
  void setDetectionRange(float range);
  void setSafeDistance(float distance);
  void setFleeMode(FleeMode mode);
  void setPanicDuration(float duration); // How long to flee in panic mode
  void setStaminaSystem(bool enabled, float maxStamina = 100.0f,
                        float staminaDrain = 10.0f);

  // Safe zone management
  void addSafeZone(const Vector2D &center, float radius);
  void clearSafeZones();

  // State queries
  bool isFleeing() const;
  bool isInPanic() const;
  float getDistanceToThreat() const;
  FleeMode getFleeMode() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;



private:
  // Entity state now stored in EDM BehaviorData (indexed by edmIndex)
  // No local m_entityStatesByIndex needed - eliminates sparse array memory waste

  // Safe zone structure
  struct SafeZone {
    Vector2D center;
    float radius;
    SafeZone(const Vector2D &c, float r) : center(c), radius(r) {}
  };

  // Configuration
  HammerEngine::FleeBehaviorConfig m_config;

  // Behavior parameters
  FleeMode m_fleeMode{FleeMode::PANIC_FLEE};
  float m_fleeSpeed{4.0f};
  float m_detectionRange{400.0f};
  float m_safeDistance{600.0f};
  float m_panicDuration{3.0f}; // 3 seconds of panic by default

  // Stamina system
  bool m_useStamina{false};
  float m_maxStamina{100.0f};
  float m_staminaDrain{10.0f};   // Stamina per second when fleeing
  float m_staminaRecovery{5.0f}; // Stamina per second when not fleeing

  // Safe zones and boundaries
  std::vector<SafeZone> m_safeZones;
  float m_boundaryPadding{
      100.0f}; // Distance from world edge to consider unsafe

  // Evasive maneuver parameters
  float m_zigzagAngle{45.0f};  // Degrees
  float m_zigzagInterval{0.5f}; // Seconds between direction changes

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_angleVariation{
      -0.5f, 0.5f}; // Radians
  mutable std::uniform_real_distribution<float> m_panicVariation{0.8f, 1.2f};

  // PATHFINDING CONSOLIDATION: All pathfinding now uses PathfindingScheduler pathway
  // (removed m_useAsyncPathfinding flag as it's no longer needed)

  // Helper methods (all entity state stored in EDM BehaviorData)
  EntityHandle getThreatHandle() const; // Gets player handle from AIManager
  Vector2D getThreatPosition() const;   // Gets player position from AIManager
  bool isThreatInRange(const Vector2D& entityPos, const Vector2D& threatPos) const;
  Vector2D calculateFleeDirection(const Vector2D& entityPos, const Vector2D& threatPos,
                                  const BehaviorData& data) const;
  Vector2D findNearestSafeZone(const Vector2D &position) const;
  bool isPositionSafe(const Vector2D &position) const;
  bool isNearBoundary(const Vector2D &position) const;
  Vector2D avoidBoundaries(const Vector2D &position,
                           const Vector2D &direction) const;

  void updatePanicFlee(BehaviorContext& ctx, BehaviorData& data, const Vector2D& threatPos);
  void updateStrategicRetreat(BehaviorContext& ctx, BehaviorData& data, const Vector2D& threatPos);
  void updateEvasiveManeuver(BehaviorContext& ctx, BehaviorData& data, const Vector2D& threatPos);
  void updateSeekCover(BehaviorContext& ctx, BehaviorData& data, const Vector2D& threatPos);

  void updateStamina(BehaviorData& data, float deltaTime, bool fleeing);
  Vector2D normalizeVector(const Vector2D &direction) const;
  float calculateFleeSpeedModifier(const BehaviorData& data) const;

  // OPTIMIZATION: Extracted lambda for better compiler optimization
  bool tryFollowPathToGoal(BehaviorContext& ctx, BehaviorData& data, const Vector2D& goal, float speed);
};

#endif // FLEE_BEHAVIOR_HPP
