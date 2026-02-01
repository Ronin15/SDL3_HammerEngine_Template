/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GUARD_BEHAVIOR_HPP
#define GUARD_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/BehaviorConfig.hpp"
#include "entities/EntityHandle.hpp"
#include "managers/EntityDataManager.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <vector>

class GuardBehavior : public AIBehavior {
public:
  enum class GuardMode : uint8_t {
    STATIC_GUARD,  // Stay at assigned position
    PATROL_GUARD,  // Patrol between waypoints
    AREA_GUARD,    // Guard a specific area
    ROAMING_GUARD, // Roam within guard zone
    ALERT_GUARD    // High alert state (faster response)
  };

  enum class AlertLevel : uint8_t {
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

  // Constructor with config
  explicit GuardBehavior(const HammerEngine::GuardBehaviorConfig& config,
                         const Vector2D& guardPosition,
                         GuardMode mode = GuardMode::STATIC_GUARD);

  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
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
  void raiseAlert(EntityHandle handle, const Vector2D &alertPosition);
  void clearAlert(EntityHandle handle);
  void setAlertDecayTime(float seconds);

  // Threat detection
  void setThreatDetectionRange(float range);
  void setFieldOfView(float angleDegrees);
  void setLineOfSightRequired(bool required);

  // Combat engagement
  void setAttackEngageRange(float range);

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

  // Configuration
  HammerEngine::GuardBehaviorConfig m_config;

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

  // Combat engagement
  float m_attackEngageRange{80.0f}; // Range at which guard transitions to Attack

  // Communication
  bool m_canCallForHelp{true};
  float m_helpCallRadius{500.0f};
  int m_guardGroup{0}; // 0 = no group

  // Alert thresholds
  static constexpr float SUSPICIOUS_THRESHOLD = 2.0f;    // 2 seconds
  static constexpr float INVESTIGATING_THRESHOLD = 5.0f; // 5 seconds
  static constexpr float HOSTILE_THRESHOLD = 1.0f;       // 1 second in sight

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_angleDistribution{
      0.0f, 2.0f * M_PI};
  mutable std::uniform_real_distribution<float> m_radiusDistribution{0.3f,
                                                                      1.0f};

  // Reusable buffers to avoid per-frame allocations
  mutable std::vector<EntityHandle> m_nearbyBuffer;  // Reused for threat detection

  // PATHFINDING CONSOLIDATION: Removed - all pathfinding now uses PathfindingScheduler
  // bool m_useAsyncPathfinding removed

  // Helper methods - all entity state stored in EDM BehaviorData (accessed via ctx.behaviorData)
  EntityHandle detectThreat(const BehaviorContext& ctx) const;
  bool isThreatInRange(const Vector2D& entityPos, const Vector2D& threatPos) const;
  bool isThreatInFieldOfView(const Vector2D& entityPos, const Vector2D& threatPos,
                             const BehaviorData& data) const;
  bool hasLineOfSight(const Vector2D& entityPos, const Vector2D& threatPos) const;
  float calculateThreatDistance(const Vector2D& entityPos, const Vector2D& threatPos) const;

  void updateAlertLevel(BehaviorData& data, bool threatPresent, EntityHandle threat) const;
  void handleThreatDetection(BehaviorContext& ctx, EntityHandle threat);
  void handleInvestigation(BehaviorContext& ctx);
  void handleReturnToPost(BehaviorContext& ctx);

  // Mode-specific updates
  void updateStaticGuard(BehaviorContext& ctx);
  void updatePatrolGuard(BehaviorContext& ctx);
  void updateAreaGuard(BehaviorContext& ctx);
  void updateRoamingGuard(BehaviorContext& ctx);
  void updateAlertGuard(BehaviorContext& ctx);

  // Movement and positioning
  void moveToPositionDirect(BehaviorContext& ctx, const Vector2D &targetPos, float speed,
                           int priority = 1);
  Vector2D getNextPatrolWaypoint(const BehaviorData& data) const;
  Vector2D generateRoamTarget() const;
  bool isAtPosition(const Vector2D &currentPos, const Vector2D &targetPos,
                    float threshold = 25.0f) const;
  bool isWithinGuardArea(const Vector2D &position) const;
  Vector2D clampToGuardArea(const Vector2D &position) const;

  // Communication helpers
  void callForHelp(const Vector2D &threatPosition);
  void broadcastAlert(AlertLevel level, const Vector2D &alertPosition);
};

#endif // GUARD_BEHAVIOR_HPP
