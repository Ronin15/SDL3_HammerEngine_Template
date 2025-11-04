/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef ATTACKBEHAVIORCONFIG_HPP
#define ATTACKBEHAVIORCONFIG_HPP

namespace HammerEngine {

/**
 * @brief Configuration structure for AttackBehavior
 *
 * Defines all parameters for attack behavior modes. Each mode has preset configurations
 * that can be created via static factory methods.
 */
struct AttackBehaviorConfig {
    // Range parameters (in pixels)
    float attackRange = 80.0f;          // Maximum attack range
    float optimalRangeMultiplier = 0.8f; // Optimal range as % of attack range
    float minimumRangeMultiplier = 0.3f; // Minimum range as % of attack range

    // Combat parameters
    float attackSpeed = 1.0f;            // Attacks per second
    float movementSpeed = 2.0f;          // Movement speed during combat (px/frame)
    float attackCooldown = 1.0f;         // Seconds between attacks
    float recoveryTime = 0.5f;           // Seconds to recover after attack

    // Damage parameters
    float attackDamage = 10.0f;          // Base damage per attack
    float damageVariation = 0.2f;        // ±20% damage variation
    float criticalHitChance = 0.1f;      // 10% chance for critical hit
    float criticalHitMultiplier = 2.0f;  // Critical hit damage multiplier
    float knockbackForce = 50.0f;        // Knockback force on hit

    // Positioning parameters
    bool circleStrafe = false;           // Enable circle strafing around target
    float strafeRadius = 100.0f;         // Radius for circle strafing (px)
    bool flankingEnabled = true;         // Attempt to flank target
    float preferredAttackAngle = 0.0f;   // Preferred attack angle in radians

    // Tactical parameters
    float retreatThreshold = 0.3f;       // Retreat when health drops below 30%
    float aggression = 0.7f;             // 70% aggression (affects decision making)
    bool teamwork = true;                // Coordinate with allies
    bool avoidFriendlyFire = true;       // Avoid hitting allies

    // Special abilities
    bool comboAttacks = false;           // Enable combo attack system
    int maxCombo = 3;                    // Maximum combo chain length
    float specialAttackChance = 0.15f;   // 15% chance for special attack
    float aoeRadius = 0.0f;              // Area of effect radius (0 = disabled)

    // Mode-specific parameters
    float chargeDamageMultiplier = 1.5f; // Damage multiplier for charge attacks

    /**
     * @brief Create configuration for MELEE_ATTACK mode
     * Close-range combat with high mobility
     */
    static AttackBehaviorConfig createMeleeConfig(float baseRange = 100.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.8f;
        config.minimumRangeMultiplier = 0.3f;
        config.attackSpeed = 1.2f;
        config.movementSpeed = 2.5f;
        config.attackDamage = 10.0f;
        return config;
    }

    /**
     * @brief Create configuration for RANGED_ATTACK mode
     * Long-range combat with kiting behavior
     */
    static AttackBehaviorConfig createRangedConfig(float baseRange = 200.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.7f;
        config.minimumRangeMultiplier = 0.4f;
        config.attackSpeed = 0.8f;
        config.movementSpeed = 2.0f;
        config.attackDamage = 8.0f;
        return config;
    }

    /**
     * @brief Create configuration for CHARGE_ATTACK mode
     * High-speed charge with increased damage
     */
    static AttackBehaviorConfig createChargeConfig(float baseRange = 150.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange * 1.5f;
        config.optimalRangeMultiplier = 1.0f; // Optimal range is max range for charge
        config.minimumRangeMultiplier = 0.0f; // No minimum for charge
        config.attackSpeed = 0.5f;
        config.movementSpeed = 3.5f;
        config.attackDamage = 15.0f;
        config.chargeDamageMultiplier = 2.0f;
        return config;
    }

    /**
     * @brief Create configuration for AMBUSH_ATTACK mode
     * Stealth-based attacks with high critical hit chance
     */
    static AttackBehaviorConfig createAmbushConfig(float baseRange = 80.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.6f;
        config.minimumRangeMultiplier = 0.3f;
        config.attackSpeed = 2.0f;
        config.movementSpeed = 1.5f;
        config.criticalHitChance = 0.3f;
        config.attackDamage = 12.0f;
        return config;
    }

    /**
     * @brief Create configuration for COORDINATED_ATTACK mode
     * Team-based combat with flanking
     */
    static AttackBehaviorConfig createCoordinatedConfig(float baseRange = 80.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.8f;
        config.minimumRangeMultiplier = 0.3f;
        config.attackSpeed = 1.0f;
        config.movementSpeed = 2.2f;
        config.teamwork = true;
        config.flankingEnabled = true;
        config.attackDamage = 10.0f;
        return config;
    }

    /**
     * @brief Create configuration for HIT_AND_RUN mode
     * High mobility with frequent retreats
     */
    static AttackBehaviorConfig createHitAndRunConfig(float baseRange = 80.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.8f;
        config.minimumRangeMultiplier = 0.3f;
        config.attackSpeed = 1.5f;
        config.movementSpeed = 3.0f;
        config.retreatThreshold = 0.8f; // Retreat early
        config.attackDamage = 8.0f;
        return config;
    }

    /**
     * @brief Create configuration for BERSERKER_ATTACK mode
     * Aggressive close-combat with combo attacks
     */
    static AttackBehaviorConfig createBerserkerConfig(float baseRange = 100.0f) {
        AttackBehaviorConfig config;
        config.attackRange = baseRange;
        config.optimalRangeMultiplier = 0.8f;
        config.minimumRangeMultiplier = 0.3f;
        config.attackSpeed = 1.8f;
        config.movementSpeed = 2.8f;
        config.aggression = 1.0f; // Maximum aggression
        config.retreatThreshold = 0.1f; // Almost never retreat
        config.comboAttacks = true;
        config.maxCombo = 5;
        config.attackDamage = 12.0f;
        return config;
    }
};

} // namespace HammerEngine

#endif // ATTACKBEHAVIORCONFIG_HPP
