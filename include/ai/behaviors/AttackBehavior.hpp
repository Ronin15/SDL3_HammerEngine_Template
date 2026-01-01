/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ATTACK_BEHAVIOR_HPP
#define ATTACK_BEHAVIOR_HPP

#include "ai/AIBehavior.hpp"
#include "ai/behaviors/AttackBehaviorConfig.hpp"
#include "entities/Entity.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/Vector2D.hpp"
#include <SDL3/SDL.h>
#include <random>
#include <unordered_map>
#include <vector>

class AttackBehavior : public AIBehavior {
public:
  enum class AttackMode : uint8_t {
    MELEE_ATTACK,       // Close combat attacks
    RANGED_ATTACK,      // Projectile-based attacks
    CHARGE_ATTACK,      // Rush towards target
    AMBUSH_ATTACK,      // Wait and strike
    COORDINATED_ATTACK, // Attack in formation
    HIT_AND_RUN,        // Quick strike then retreat
    BERSERKER_ATTACK    // Aggressive continuous assault
  };

  enum class AttackState : uint8_t {
    SEEKING,     // Looking for target
    APPROACHING, // Moving towards target
    POSITIONING, // Getting into attack position
    ATTACKING,   // Executing attack
    RECOVERING,  // Post-attack recovery
    RETREATING,  // Tactical retreat
    COOLDOWN     // Waiting between attacks
  };

  explicit AttackBehavior(float attackRange = 80.0f, float attackDamage = 10.0f,
                          float attackSpeed = 1.0f);

  // Constructor with mode
  explicit AttackBehavior(AttackMode mode, float attackRange = 80.0f,
                          float attackDamage = 10.0f);

  void init(EntityHandle handle) override;
  void executeLogic(BehaviorContext& ctx) override;
  void clean(EntityHandle handle) override;
  void onMessage(EntityHandle handle, const std::string &message) override;
  std::string getName() const override;

  // Configuration methods
  void setAttackMode(AttackMode mode);
  void setAttackRange(float range);
  void setAttackDamage(float damage);
  void setAttackSpeed(float speed);
  void setMovementSpeed(float speed);
  void setAttackCooldown(float cooldown);
  void setRecoveryTime(float recoveryTime);

  // Positioning and tactics
  void setOptimalRange(float range);
  void setMinimumRange(float range);
  void setCircleStrafe(bool enabled, float radius = 100.0f);
  void setFlankingEnabled(bool enabled);
  void setPreferredAttackAngle(float angleDegrees);

  // Damage and combat
  void setDamageVariation(float variation);
  void setCriticalHitChance(float chance);
  void setCriticalHitMultiplier(float multiplier);
  void setKnockbackForce(float force);

  // Tactical behavior
  void setRetreatThreshold(float healthPercentage);
  void setAggression(float aggression); // 0.0 to 1.0
  void setTeamwork(bool enabled);
  void setAvoidFriendlyFire(bool enabled);

  // Special abilities
  void setComboAttacks(bool enabled, int maxCombo = 3);
  void setSpecialAttackChance(float chance);
  void setAreaOfEffectRadius(float radius);
  void setChargeDamageMultiplier(float multiplier);

  // State queries
  bool isInCombat() const;
  bool isAttacking() const;
  bool canAttack() const;
  AttackState getCurrentAttackState() const;
  AttackMode getAttackMode() const;
  float getDistanceToTarget() const;
  float getLastAttackTime() const;
  int getCurrentCombo() const;

  // Animation state notification - triggers NPC animation state changes
  void notifyAnimationStateChange(EntityPtr entity, AttackState newState);

  // Clone method for creating unique behavior instances
  std::shared_ptr<AIBehavior> clone() const override;



private:
  
  struct EntityState {
    // Base AI behavior state (pathfinding, separation, cooldowns)
    AIBehaviorState baseState;

    // Attack-specific state
    Vector2D lastTargetPosition{0, 0};
    Vector2D attackPosition{0, 0};
    Vector2D retreatPosition{0, 0};
    Vector2D strafeVector{0, 0};

    AttackState currentState{AttackState::SEEKING};
    float attackTimer{0.0f};
    float stateChangeTimer{0.0f};
    float damageTimer{0.0f};
    float comboTimer{0.0f};
    float strafeTimer{0.0f};

    float currentHealth{100.0f};
    float maxHealth{100.0f};
    float currentStamina{100.0f};
    float targetDistance{0.0f};
    float attackChargeTime{0.0f};
    float recoveryTimer{0.0f};

    int currentCombo{0};
    int attacksInCombo{0};
    bool inCombat{false};
    bool hasTarget{false};
    bool isCharging{false};
    bool isRetreating{false};
    bool canAttack{true};
    bool lastAttackHit{false};
    bool specialAttackReady{false};

    // Tactical state
    bool circleStrafing{false};
    bool flanking{false};
    float preferredAttackAngle{0.0f};
    int strafeDirectionInt{1}; // 1 for clockwise, -1 for counter-clockwise

    EntityState() {
      baseState.navRadius = 18.0f; // Attack-specific nav radius
    }
  };

  // Helper methods for executeLogic refactoring
  void updateTimers(EntityState& state, float deltaTime);
  EntityState& ensureEntityState(EntityHandle::IDType entityId);
  void updateTargetTracking(const Vector2D& entityPos, EntityState& state, const Vector2D& targetPos, bool hasTarget);
  void updateTargetDistance(const Vector2D& entityPos, const Vector2D& targetPos, EntityState& state);
  void updateCombatState(EntityState& state);
  void handleNoTarget(EntityState& state);
  void dispatchModeUpdate(EntityPtr entity, EntityState& state, float deltaTime, const Vector2D& targetPos);

  // Configuration system
  void applyConfig(const HammerEngine::AttackBehaviorConfig& config);

  // Map to store per-entity state
  std::unordered_map<EntityHandle::IDType, EntityState> m_entityStates;

  // Cache EntityPtr for helper methods that still use it (temporary during migration)
  std::unordered_map<EntityHandle::IDType, EntityPtr> m_entityPtrCache;

  // Attack parameters
  AttackMode m_attackMode{AttackMode::MELEE_ATTACK};
  float m_attackRange{80.0f};
  float m_attackDamage{10.0f};
  float m_attackSpeed{1.0f};
  float m_movementSpeed{2.0f};
  float m_attackCooldown{1.0f}; // Seconds between attacks
  float m_recoveryTime{0.5f};   // Time to recover after attack

  // Positioning parameters
  float m_optimalRange{60.0f}; // Preferred attack distance
  float m_minimumRange{30.0f}; // Minimum distance to maintain
  bool m_circleStrafe{false};
  float m_strafeRadius{100.0f};
  bool m_flankingEnabled{true};
  float m_preferredAttackAngle{0.0f}; // Radians

  // Damage parameters
  float m_damageVariation{0.2f};   // ±20% damage variation
  float m_criticalHitChance{0.1f}; // 10% chance
  float m_criticalHitMultiplier{2.0f};
  float m_knockbackForce{50.0f};

  // Tactical parameters
  float m_retreatThreshold{0.3f}; // Retreat at 30% health
  float m_aggression{0.7f};       // 70% aggression
  bool m_teamwork{true};
  bool m_avoidFriendlyFire{true};

  // Special abilities
  bool m_comboAttacks{false};
  int m_maxCombo{3};
  float m_specialAttackChance{0.15f};
  float m_aoeRadius{0.0f};

  // Async pathfinding support
  // PATHFINDING CONSOLIDATION: Removed - all pathfinding now uses PathfindingScheduler
  // bool m_useAsyncPathfinding removed
  float m_chargeDamageMultiplier{1.5f};

  // Combat state transition thresholds
  static constexpr float COMBAT_ENTER_RANGE_MULT = 1.2f;  // Enter combat at 120% of attack range
  static constexpr float COMBAT_EXIT_RANGE_MULT = 2.0f;   // Exit combat at 200% of attack range

  // Timing constants
  static constexpr float COMBO_TIMEOUT = 3.0f; // 3 seconds
  static constexpr float CHARGE_TIME = 1.0f;   // 1 second charge
  static constexpr float STRAFE_INTERVAL = 2.0f; // 2 seconds between direction changes
  static constexpr float RETREAT_SPEED_MULTIPLIER = 1.5f;
  static constexpr float CHARGE_SPEED_MULTIPLIER = 2.0f;

  // Combat multipliers
  static constexpr float COMBO_DAMAGE_PER_LEVEL = 0.2f;    // 20% damage increase per combo level
  static constexpr float SPECIAL_ATTACK_MULTIPLIER = 1.5f; // 1.5x damage/knockback for special attacks
  static constexpr float COMBO_FINISHER_MULTIPLIER = 2.0f; // 2x damage/knockback for combo finishers
  static constexpr float CHARGE_DISTANCE_THRESHOLD_MULT = 1.5f; // Charge when > 150% of optimal range

  // Random number generation
  mutable std::mt19937 m_rng{std::random_device{}()};
  mutable std::uniform_real_distribution<float> m_damageRoll{0.0f, 1.0f};
  mutable std::uniform_real_distribution<float> m_criticalRoll{0.0f, 1.0f};
  mutable std::uniform_real_distribution<float> m_specialRoll{0.0f, 1.0f};
  mutable std::uniform_real_distribution<float> m_angleVariation{-0.5f, 0.5f};

  // Helper methods
  EntityHandle getTargetHandle() const; // Gets player handle from AIManager
  Vector2D getTargetPosition() const;   // Gets player position from AIManager
  bool isTargetInRange(const Vector2D& entityPos, const Vector2D& targetPos) const;
  bool isTargetInAttackRange(const Vector2D& entityPos, const Vector2D& targetPos, const EntityState& state) const;
  float calculateDamage(const EntityState &state) const;
  Vector2D calculateOptimalAttackPosition(const Vector2D& entityPos, const Vector2D& targetPos,
                                          const EntityState &state) const;
  Vector2D calculateFlankingPosition(const Vector2D& entityPos, const Vector2D& targetPos) const;
  Vector2D calculateStrafePosition(const Vector2D& entityPos, const Vector2D& targetPos,
                                   const EntityState &state) const;

  // State management
  void changeState(EntityState &state, AttackState newState);
  void updateStateTimer(EntityState &state);
  bool shouldRetreat(const EntityState &state) const;
  bool shouldCharge(float distance, const EntityState &state) const;

  // Attack execution
  void executeAttack(EntityPtr entity, const Vector2D& targetPos, EntityState &state);
  void executeSpecialAttack(EntityPtr entity, const Vector2D& targetPos, EntityState &state);
  void executeComboAttack(EntityPtr entity, const Vector2D& targetPos, EntityState &state);
  void applyDamageToTarget(EntityHandle targetHandle, float damage, const Vector2D& knockback);
  void applyAreaOfEffectDamage(const Vector2D& entityPos, const Vector2D& targetPos, float damage);

  // Mode-specific updates
  void updateMeleeAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateRangedAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateChargeAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateAmbushAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateCoordinatedAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateHitAndRun(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateBerserkerAttack(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);

  // State-specific updates
  void updateSeeking(EntityState &state);
  void updateApproaching(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updatePositioning(EntityPtr entity, EntityState &state, float deltaTime, const Vector2D& targetPos);
  void updateAttacking(EntityPtr entity, EntityState &state, const Vector2D& targetPos);
  void updateRecovering(EntityState &state);
  void updateRetreating(EntityPtr entity, EntityState &state, const Vector2D& targetPos);
  void updateCooldown(EntityState &state);

  // Movement and positioning
  void moveToPosition(EntityPtr entity, const Vector2D &targetPos, float speed, float deltaTime);
  void maintainDistance(EntityPtr entity, const Vector2D& targetPos, float desiredDistance, float deltaTime);
  void circleStrafe(EntityPtr entity, const Vector2D& targetPos, EntityState &state, float deltaTime);
  void performFlankingManeuver(EntityPtr entity, const Vector2D& targetPos, EntityState &state, float deltaTime);

  // Utility methods
  bool isValidAttackPosition(const Vector2D &position, const Vector2D& targetPos) const;

  // Combat calculations
  float calculateEffectiveRange(const EntityState &state) const;
  float calculateAttackSuccessChance(const Vector2D& entityPos, const Vector2D& targetPos, const EntityState &state) const;
  Vector2D calculateKnockbackVector(const Vector2D& attackerPos, const Vector2D& targetPos) const;

  // Team coordination
  void coordinateWithTeam(const EntityState &state);
  bool isFriendlyFireRisk(const Vector2D& entityPos, const Vector2D& targetPos) const;

public:
};

#endif // ATTACK_BEHAVIOR_HPP
