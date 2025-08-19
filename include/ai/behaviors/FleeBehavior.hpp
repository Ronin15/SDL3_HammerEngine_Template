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
  void executeLogic(EntityPtr entity) override;
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
  void setScreenBounds(float width, float height); // For boundary avoidance

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
    Uint64 fleeStartTime{0};
    Uint64 lastDirectionChange{0};
    Uint64 panicEndTime{0};
    float currentStamina{100.0f};
    bool isFleeing{false};
    bool isInPanic{false};
    bool hasValidThreat{false};
    int zigzagDirection{1}; // For evasive maneuvers
    Uint64 lastZigzagTime{0};

    EntityState()
        : lastThreatPosition(0, 0), fleeDirection(0, 0),
          lastKnownSafeDirection(0, 0), fleeStartTime(0),
          lastDirectionChange(0), panicEndTime(0), currentStamina(100.0f),
          isFleeing(false), isInPanic(false), hasValidThreat(false),
          zigzagDirection(1), lastZigzagTime(0) {}
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
  float m_panicDuration{3000.0f}; // 3 seconds of panic by default

  // Stamina system
  bool m_useStamina{false};
  float m_maxStamina{100.0f};
  float m_staminaDrain{10.0f};   // Stamina per second when fleeing
  float m_staminaRecovery{5.0f}; // Stamina per second when not fleeing

  // Safe zones and boundaries
  std::vector<SafeZone> m_safeZones;
  float m_screenWidth{1280.0f};
  float m_screenHeight{720.0f};
  float m_boundaryPadding{
      100.0f}; // Distance from screen edge to consider unsafe

  // Evasive maneuver parameters
  float m_zigzagAngle{45.0f};   // Degrees
  Uint64 m_zigzagInterval{500}; // Milliseconds between direction changes

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_angleVariation{
      -0.5f, 0.5f}; // Radians
  mutable std::uniform_real_distribution<float> m_panicVariation{0.8f, 1.2f};

  // Helper methods
  EntityPtr getThreat() const; // Gets player reference from AIManager
  bool isThreatInRange(EntityPtr entity, EntityPtr threat) const;
  Vector2D calculateFleeDirection(EntityPtr entity, EntityPtr threat,
                                  const EntityState &state);
  Vector2D findNearestSafeZone(const Vector2D &position) const;
  bool isPositionSafe(const Vector2D &position) const;
  bool isNearBoundary(const Vector2D &position) const;
  Vector2D avoidBoundaries(const Vector2D &position,
                           const Vector2D &direction) const;

  void updatePanicFlee(EntityPtr entity, EntityState &state);
  void updateStrategicRetreat(EntityPtr entity, EntityState &state);
  void updateEvasiveManeuver(EntityPtr entity, EntityState &state);
  void updateSeekCover(EntityPtr entity, EntityState &state);

  void updateStamina(EntityState &state, float deltaTime, bool fleeing);
  Vector2D normalizeVector(const Vector2D &direction) const;
  float calculateFleeSpeedModifier(const EntityState &state) const;
};

#endif // FLEE_BEHAVIOR_HPP