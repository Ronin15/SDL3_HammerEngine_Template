/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GUARD_BEHAVIOR_HPP
#define GUARD_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <unordered_map>
#include <vector>

class GuardBehavior : public AIBehavior {
public:
  enum class GuardMode {
    STATIC_GUARD,  // Stay at assigned position
    PATROL_GUARD,  // Patrol between waypoints
    AREA_GUARD,    // Guard a specific area
    ROAMING_GUARD, // Roam within guard zone
    ALERT_GUARD    // High alert state (faster response)
  };

  enum class AlertLevel {
    CALM = 0,          // Normal state, not alert
    SUSPICIOUS = 1,    // Something might be wrong
    INVESTIGATING = 2, // Actively looking for threats
    HOSTILE = 3,       // Threat detected, engaging
    ALARM = 4          // Maximum alert, calling for help
  };

  explicit GuardBehavior(const Vector2D &guardPosition,
                         float guardRadius = 200.0f,
                         float alertRadius = 300.0f);

  // Constructor with mode
  explicit GuardBehavior(GuardMode mode, const Vector2D &guardPosition,
                         float guardRadius = 200.0f);

  void init(EntityPtr entity) override;
  void executeLogic(EntityPtr entity) override;
  void clean(EntityPtr entity) override;
  void onMessage(EntityPtr entity, const std::string &message) override;
  std::string getName() const override;

  // Configuration methods
  void setGuardPosition(const Vector2D &position);
  void setGuardRadius(float radius);
  void setAlertRadius(float radius);
  void setGuardMode(GuardMode mode);
  void setMovementSpeed(float speed);
  void setAlertSpeed(float speed);
  void setInvestigationTime(float seconds);
  void setReturnToPostTime(float seconds);

  // Patrol and area setup
  void addPatrolWaypoint(const Vector2D &waypoint);
  void setPatrolWaypoints(const std::vector<Vector2D> &waypoints);
  void setGuardArea(const Vector2D &center, float radius);
  void setGuardArea(const Vector2D &topLeft, const Vector2D &bottomRight);

  // Alert system
  void setAlertLevel(AlertLevel level);
  void raiseAlert(EntityPtr entity, const Vector2D &alertPosition);
  void clearAlert(EntityPtr entity);
  void setAlertDecayTime(float seconds);

  // Threat detection
  void setThreatDetectionRange(float range);
  void setFieldOfView(float angleDegrees);
  void setLineOfSightRequired(bool required);

  // Communication and coordination
  void setCanCallForHelp(bool canCall);
  void setHelpCallRadius(float radius);
  void setGuardGroup(int groupId);

  // State queries
  bool isOnDuty() const;
  bool isAlerted() const;
  bool isInvestigating() const;
  AlertLevel getCurrentAlertLevel() const;
  GuardMode getGuardMode() const;
  Vector2D getGuardPosition() const;
  float getDistanceFromPost() const;

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;



private:
  
  struct EntityState {
    Vector2D assignedPosition{0, 0};
    Vector2D lastKnownThreatPosition{0, 0};
    Vector2D investigationTarget{0, 0};
    Vector2D currentPatrolTarget{0, 0};

    AlertLevel currentAlertLevel{AlertLevel::CALM};
    GuardMode currentMode{GuardMode::STATIC_GUARD};

    Uint64 lastThreatSighting{0};
    Uint64 alertStartTime{0};
    Uint64 investigationStartTime{0};
    Uint64 lastPositionCheck{0};
    Uint64 lastPatrolMove{0};
    Uint64 lastAlertDecay{0};

    size_t currentPatrolIndex{0};
    float currentHeading{0.0f};
    bool hasActiveThreat{false};
    bool isInvestigating{false};
    bool returningToPost{false};
    bool onDuty{true};
    bool alertRaised{false};
    bool helpCalled{false};

    // Roaming state
    Vector2D roamTarget{0, 0};
    Uint64 nextRoamTime{0};

    EntityState()
        : assignedPosition(0, 0), lastKnownThreatPosition(0, 0),
          investigationTarget(0, 0), currentPatrolTarget(0, 0),
          currentAlertLevel(AlertLevel::CALM),
          currentMode(GuardMode::STATIC_GUARD), lastThreatSighting(0),
          alertStartTime(0), investigationStartTime(0), lastPositionCheck(0),
          lastPatrolMove(0), lastAlertDecay(0), currentPatrolIndex(0),
          currentHeading(0.0f), hasActiveThreat(false), isInvestigating(false),
          returningToPost(false), onDuty(true), alertRaised(false),
          helpCalled(false), roamTarget(0, 0), nextRoamTime(0) {}
  };

  // Map to store per-entity state
  std::unordered_map<EntityPtr, EntityState> m_entityStates;

  // Guard parameters
  GuardMode m_guardMode{GuardMode::STATIC_GUARD};
  Vector2D m_guardPosition{0, 0};
  float m_guardRadius{200.0f};
  float m_alertRadius{300.0f};
  float m_movementSpeed{1.5f};
  float m_alertSpeed{3.0f};

  // Patrol waypoints
  std::vector<Vector2D> m_patrolWaypoints;
  bool m_patrolReverse{false};

  // Area guarding (rectangular or circular)
  Vector2D m_areaCenter{0, 0};
  Vector2D m_areaTopLeft{0, 0};
  Vector2D m_areaBottomRight{0, 0};
  float m_areaRadius{0.0f};
  bool m_useCircularArea{false};

  // Timing parameters
  float m_investigationTime{5.0f}; // Seconds to investigate
  float m_returnToPostTime{10.0f}; // Seconds before returning to post
  float m_alertDecayTime{30.0f};   // Seconds for alert to decay
  float m_roamInterval{8.0f};      // Seconds between roam target changes

  // Threat detection
  float m_threatDetectionRange{250.0f};
  float m_fieldOfView{120.0f}; // Degrees
  bool m_lineOfSightRequired{true};

  // Communication
  bool m_canCallForHelp{true};
  float m_helpCallRadius{500.0f};
  int m_guardGroup{0}; // 0 = no group

  // Alert thresholds
  static constexpr Uint64 SUSPICIOUS_THRESHOLD = 2000;    // 2 seconds
  static constexpr Uint64 INVESTIGATING_THRESHOLD = 5000; // 5 seconds
  static constexpr Uint64 HOSTILE_THRESHOLD = 1000;       // 1 second in sight

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_angleDistribution{
      0.0f, 2.0f * M_PI};
  mutable std::uniform_real_distribution<float> m_radiusDistribution{0.3f,
                                                                     1.0f};

  // Helper methods
  EntityPtr detectThreat(EntityPtr entity, const EntityState &state) const;
  bool isThreatInRange(EntityPtr entity, EntityPtr threat) const;
  bool isThreatInFieldOfView(EntityPtr entity, EntityPtr threat,
                             const EntityState &state) const;
  bool hasLineOfSight(EntityPtr entity, EntityPtr threat) const;
  float calculateThreatDistance(EntityPtr entity, EntityPtr threat) const;

  void updateAlertLevel(EntityPtr entity, EntityState &state,
                        bool threatPresent);
  void handleThreatDetection(EntityPtr entity, EntityState &state,
                             EntityPtr threat);
  void handleInvestigation(EntityPtr entity, EntityState &state);
  void handleReturnToPost(EntityPtr entity, EntityState &state);

  // Mode-specific updates
  void updateStaticGuard(EntityPtr entity, EntityState &state);
  void updatePatrolGuard(EntityPtr entity, EntityState &state);
  void updateAreaGuard(EntityPtr entity, EntityState &state);
  void updateRoamingGuard(EntityPtr entity, EntityState &state);
  void updateAlertGuard(EntityPtr entity, EntityState &state);

  // Movement and positioning
  void moveToPosition(EntityPtr entity, const Vector2D &targetPos, float speed);
  Vector2D getNextPatrolWaypoint(const EntityState &state) const;
  Vector2D generateRoamTarget(EntityPtr entity, const EntityState &state) const;
  bool isAtPosition(const Vector2D &currentPos, const Vector2D &targetPos,
                    float threshold = 25.0f) const;
  bool isWithinGuardArea(const Vector2D &position) const;

  // Utility methods
  Vector2D normalizeDirection(const Vector2D &direction) const;
  float normalizeAngle(float angle) const;
  float calculateAngleToTarget(const Vector2D &from, const Vector2D &to) const;
  Vector2D clampToGuardArea(const Vector2D &position) const;

  // Communication helpers
  void callForHelp(EntityPtr entity, const Vector2D &threatPosition);
  void broadcastAlert(EntityPtr entity, AlertLevel level,
                      const Vector2D &alertPosition);
};

#endif // GUARD_BEHAVIOR_HPP