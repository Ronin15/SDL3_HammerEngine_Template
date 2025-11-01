/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef FLEE_BEHAVIOR_HPP
#define FLEE_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <unordered_map>

class FleeBehavior : public AIBehavior {
public:
  enum class FleeMode {
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

  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity, float deltaTime) override;
  void clean(EntityPtr entity) override;
  void onMessage(EntityPtr entity, const std::string &message) override;
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
  
  struct EntityState {
    Vector2D lastThreatPosition{0, 0};
    Vector2D fleeDirection{0, 0};
    Vector2D lastKnownSafeDirection{0, 0};
    float fleeTimer{0.0f};
    float directionChangeTimer{0.0f};
    float panicTimer{0.0f};
    float currentStamina{100.0f};
    bool isFleeing{false};
    bool isInPanic{false};
    bool hasValidThreat{false};
    int zigzagDirection{1}; // For evasive maneuvers
    float zigzagTimer{0.0f};

    // Optional path-following state for strategic/seek-cover modes
    std::vector<Vector2D> pathPoints;
    size_t currentPathIndex{0};
    float pathUpdateTimer{0.0f};
    float progressTimer{0.0f};
    float lastNodeDistance{std::numeric_limits<float>::infinity()};
    float navRadius{18.0f};
    float pathCooldown{0.0f};
    float backoffTimer{0.0f};
    // Separation decimation
    float separationTimer{0.0f};
    Vector2D lastSepVelocity{0, 0};

    // Performance optimization: cached crowd analysis to avoid expensive CollisionManager calls
    int cachedNearbyCount{0};
    std::vector<Vector2D> cachedNearbyPositions;
    float lastCrowdAnalysis{0.0f};

    EntityState()
        : lastThreatPosition(0, 0), fleeDirection(0, 0),
          lastKnownSafeDirection(0, 0), fleeTimer(0.0f),
          directionChangeTimer(0.0f), panicTimer(0.0f), currentStamina(100.0f),
          isFleeing(false), isInPanic(false), hasValidThreat(false),
          zigzagDirection(1), zigzagTimer(0.0f), pathPoints(),
          currentPathIndex(0), pathUpdateTimer(0.0f), progressTimer(0.0f),
          lastNodeDistance(std::numeric_limits<float>::infinity()),
          navRadius(18.0f), pathCooldown(0.0f), backoffTimer(0.0f),
          separationTimer(0.0f), lastSepVelocity(0, 0) {}
  };

  // Safe zone structure
  struct SafeZone {
    Vector2D center;
    float radius;
    SafeZone(const Vector2D &c, float r) : center(c), radius(r) {}
  };

  // Map to store per-entity state
  std::unordered_map<EntityPtr, EntityState> m_entityStates;

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

  // Helper methods
  EntityPtr getThreat() const; // Gets player reference from AIManager
  bool isThreatInRange(EntityPtr entity, EntityPtr threat) const;
  Vector2D calculateFleeDirection(EntityPtr entity, EntityPtr threat,
                                  const EntityState &state) const;
  Vector2D findNearestSafeZone(const Vector2D &position) const;
  bool isPositionSafe(const Vector2D &position) const;
  bool isNearBoundary(const Vector2D &position) const;
  Vector2D avoidBoundaries(const Vector2D &position,
                           const Vector2D &direction) const;

  void updatePanicFlee(EntityPtr entity, EntityState &state, float deltaTime);
  void updateStrategicRetreat(EntityPtr entity, EntityState &state, float deltaTime);
  void updateEvasiveManeuver(EntityPtr entity, EntityState &state, float deltaTime);
  void updateSeekCover(EntityPtr entity, EntityState &state, float deltaTime);

  void updateStamina(EntityState &state, float deltaTime, bool fleeing);
  Vector2D normalizeVector(const Vector2D &direction) const;
  float calculateFleeSpeedModifier(const EntityState &state) const;

  // OPTIMIZATION: Extracted lambda for better compiler optimization
  bool tryFollowPathToGoal(EntityPtr entity, const Vector2D& currentPos, EntityState& state, const Vector2D& goal, float speed);

public:
};

#endif // FLEE_BEHAVIOR_HPP
