/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef AI_BEHAVIORS_HPP
#define AI_BEHAVIORS_HPP

/**
 * @file AIBehaviors.hpp
 * @brief Comprehensive include file for all AI behaviors
 * 
 * This header provides convenient access to all available AI behaviors
 * in the Hammer Game Engine. Include this file to access all behavior types.
 *
 * Available Behaviors:
 * - IdleBehavior: Minimal movement and stationary behavior
 * - WanderBehavior: Random movement within defined areas
 * - PatrolBehavior: Movement between waypoints
 * - ChaseBehavior: Pursuit of targets
 * - FleeBehavior: Escape and avoidance behavior
 * - FollowBehavior: Target following with formation support
 * - GuardBehavior: Area defense and threat detection
 * - AttackBehavior: Combat and assault behavior
 * 
 * Usage Example:
 * ```cpp
 * #include "ai/AIBehaviors.hpp"
 * #include "managers/AIManager.hpp"
 * 
 * // Register all behaviors
 * void registerAllBehaviors() {
 *     auto& aiManager = AIManager::Instance();
 *     
 *     // Register basic behaviors
 *     aiManager.registerBehavior("Idle", std::make_shared<IdleBehavior>());
 *     aiManager.registerBehavior("Wander", std::make_shared<WanderBehavior>());
 *     aiManager.registerBehavior("Patrol", std::make_shared<PatrolBehavior>(waypoints));
 *     aiManager.registerBehavior("Chase", std::make_shared<ChaseBehavior>());
 *     aiManager.registerBehavior("Flee", std::make_shared<FleeBehavior>());
 *     aiManager.registerBehavior("Follow", std::make_shared<FollowBehavior>());
 *     aiManager.registerBehavior("Guard", std::make_shared<GuardBehavior>(guardPos));
 *     aiManager.registerBehavior("Attack", std::make_shared<AttackBehavior>());
 * }
 * 
 * // Assign behavior to entity
 * void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName) {
 *     AIManager::Instance().assignBehaviorToEntity(entity, behaviorName);
 * }
 * ```
 */

// Core AI behavior interface
#include "managers/AIManager.hpp"

// Basic behaviors
#include "ai/behaviors/IdleBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"

// Movement behaviors
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"

// Advanced behaviors
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/AttackBehavior.hpp"

namespace AIBehaviors {
    
    /**
     * @brief Behavior factory for creating behavior instances
     */
    class BehaviorFactory {
    public:
        /**
         * @brief Create an idle behavior instance
         * @param mode Idle mode (stationary, sway, turn, fidget)
         * @param radius Idle movement radius
         * @return Shared pointer to idle behavior
         */
        static std::shared_ptr<IdleBehavior> createIdle(
            IdleBehavior::IdleMode mode = IdleBehavior::IdleMode::STATIONARY,
            float radius = 20.0f
        ) {
            return std::make_shared<IdleBehavior>(mode, radius);
        }
        
        /**
         * @brief Create a wander behavior instance
         * @param mode Wander mode (small, medium, large area)
         * @param speed Movement speed
         * @return Shared pointer to wander behavior
         */
        static std::shared_ptr<WanderBehavior> createWander(
            WanderBehavior::WanderMode mode = WanderBehavior::WanderMode::MEDIUM_AREA,
            float speed = 2.0f
        ) {
            return std::make_shared<WanderBehavior>(mode, speed);
        }
        
        /**
         * @brief Create a patrol behavior instance
         * @param waypoints List of patrol waypoints
         * @param speed Movement speed
         * @return Shared pointer to patrol behavior
         */
        static std::shared_ptr<PatrolBehavior> createPatrol(
            const std::vector<Vector2D>& waypoints,
            float speed = 2.0f
        ) {
            return std::make_shared<PatrolBehavior>(waypoints, speed);
        }
        
        /**
         * @brief Create a patrol behavior with mode
         * @param mode Patrol mode (fixed, random area, circular, event target)
         * @param speed Movement speed
         * @return Shared pointer to patrol behavior
         */
        static std::shared_ptr<PatrolBehavior> createPatrolWithMode(
            PatrolBehavior::PatrolMode mode,
            float speed = 2.0f
        ) {
            return std::make_shared<PatrolBehavior>(mode, speed);
        }
        
        /**
         * @brief Create a chase behavior instance
         * @param speed Chase speed
         * @param maxRange Maximum chase range
         * @param minRange Minimum distance to maintain
         * @return Shared pointer to chase behavior
         */
        static std::shared_ptr<ChaseBehavior> createChase(
            float speed = 3.0f,
            float maxRange = 500.0f,
            float minRange = 50.0f
        ) {
            return std::make_shared<ChaseBehavior>(speed, maxRange, minRange);
        }
        
        /**
         * @brief Create a flee behavior instance
         * @param mode Flee mode (panic, strategic, evasive, seek cover)
         * @param speed Flee speed
         * @param detectionRange Range at which to start fleeing
         * @return Shared pointer to flee behavior
         */
        static std::shared_ptr<FleeBehavior> createFlee(
            FleeBehavior::FleeMode mode = FleeBehavior::FleeMode::PANIC_FLEE,
            float speed = 4.0f,
            float detectionRange = 400.0f
        ) {
            return std::make_shared<FleeBehavior>(mode, speed, detectionRange);
        }
        
        /**
         * @brief Create a follow behavior instance
         * @param mode Follow mode (close, loose, flanking, rear guard, formation)
         * @param speed Follow speed
         * @return Shared pointer to follow behavior
         */
        static std::shared_ptr<FollowBehavior> createFollow(
            FollowBehavior::FollowMode mode = FollowBehavior::FollowMode::LOOSE_FOLLOW,
            float speed = 2.5f
        ) {
            return std::make_shared<FollowBehavior>(mode, speed);
        }
        
        /**
         * @brief Create a guard behavior instance
         * @param guardPosition Position to guard
         * @param mode Guard mode (static, patrol, area, roaming, alert)
         * @param radius Guard area radius
         * @return Shared pointer to guard behavior
         */
        static std::shared_ptr<GuardBehavior> createGuard(
            const Vector2D& guardPosition,
            GuardBehavior::GuardMode mode = GuardBehavior::GuardMode::STATIC_GUARD,
            float radius = 200.0f
        ) {
            return std::make_shared<GuardBehavior>(mode, guardPosition, radius);
        }
        
        /**
         * @brief Create an attack behavior instance
         * @param mode Attack mode (melee, ranged, charge, ambush, coordinated, hit-and-run, berserker)
         * @param range Attack range
         * @param damage Attack damage
         * @return Shared pointer to attack behavior
         */
        static std::shared_ptr<AttackBehavior> createAttack(
            AttackBehavior::AttackMode mode = AttackBehavior::AttackMode::MELEE_ATTACK,
            float range = 80.0f,
            float damage = 10.0f
        ) {
            return std::make_shared<AttackBehavior>(mode, range, damage);
        }
    };
    
    /**
     * @brief Behavior registration helper
     */
    class BehaviorRegistrar {
    public:
        /**
         * @brief Register all default behaviors with the AI Manager
         * @param aiManager Reference to AI Manager instance
         */
        static void registerAllBehaviors(class AIManager& aiManager) {
            // Register basic behaviors
            aiManager.registerBehavior("Idle", BehaviorFactory::createIdle());
            aiManager.registerBehavior("Wander", BehaviorFactory::createWander());
            aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
            aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
            aiManager.registerBehavior("Follow", BehaviorFactory::createFollow());
            
            // Register advanced behaviors with default configurations
            std::vector<Vector2D> defaultWaypoints = {
                Vector2D(0, 0), Vector2D(100, 0), Vector2D(100, 100), Vector2D(0, 100)
            };
            aiManager.registerBehavior("Patrol", BehaviorFactory::createPatrol(defaultWaypoints));
            
            Vector2D defaultGuardPos(0, 0);
            aiManager.registerBehavior("Guard", BehaviorFactory::createGuard(defaultGuardPos));
            aiManager.registerBehavior("Attack", BehaviorFactory::createAttack());
            
            // Register behavior variants
            aiManager.registerBehavior("IdleStationary", BehaviorFactory::createIdle(IdleBehavior::IdleMode::STATIONARY));
            aiManager.registerBehavior("IdleFidget", BehaviorFactory::createIdle(IdleBehavior::IdleMode::LIGHT_FIDGET));
            
            aiManager.registerBehavior("WanderSmall", BehaviorFactory::createWander(WanderBehavior::WanderMode::SMALL_AREA));
            aiManager.registerBehavior("WanderLarge", BehaviorFactory::createWander(WanderBehavior::WanderMode::LARGE_AREA));
            
            aiManager.registerBehavior("FollowClose", BehaviorFactory::createFollow(FollowBehavior::FollowMode::CLOSE_FOLLOW));
            aiManager.registerBehavior("FollowFormation", BehaviorFactory::createFollow(FollowBehavior::FollowMode::ESCORT_FORMATION));
            
            aiManager.registerBehavior("GuardPatrol", BehaviorFactory::createGuard(defaultGuardPos, GuardBehavior::GuardMode::PATROL_GUARD));
            aiManager.registerBehavior("GuardArea", BehaviorFactory::createGuard(defaultGuardPos, GuardBehavior::GuardMode::AREA_GUARD));
            
            aiManager.registerBehavior("AttackMelee", BehaviorFactory::createAttack(AttackBehavior::AttackMode::MELEE_ATTACK));
            aiManager.registerBehavior("AttackRanged", BehaviorFactory::createAttack(AttackBehavior::AttackMode::RANGED_ATTACK));
            aiManager.registerBehavior("AttackCharge", BehaviorFactory::createAttack(AttackBehavior::AttackMode::CHARGE_ATTACK));
            
            aiManager.registerBehavior("FleeEvasive", BehaviorFactory::createFlee(FleeBehavior::FleeMode::EVASIVE_MANEUVER));
            aiManager.registerBehavior("FleeStrategic", BehaviorFactory::createFlee(FleeBehavior::FleeMode::STRATEGIC_RETREAT));
        }
        
        /**
         * @brief Register essential behaviors only
         * @param aiManager Reference to AI Manager instance
         */
        static void registerEssentialBehaviors(class AIManager& aiManager) {
            aiManager.registerBehavior("Idle", BehaviorFactory::createIdle());
            aiManager.registerBehavior("Wander", BehaviorFactory::createWander());
            aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
            aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
        }
        
        /**
         * @brief Register combat behaviors
         * @param aiManager Reference to AI Manager instance
         */
        static void registerCombatBehaviors(class AIManager& aiManager) {
            Vector2D defaultGuardPos(0, 0);
            aiManager.registerBehavior("Guard", BehaviorFactory::createGuard(defaultGuardPos));
            aiManager.registerBehavior("Attack", BehaviorFactory::createAttack());
            aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
            aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
        }
        
        /**
         * @brief Register formation behaviors
         * @param aiManager Reference to AI Manager instance
         */
        static void registerFormationBehaviors(class AIManager& aiManager) {
            aiManager.registerBehavior("Follow", BehaviorFactory::createFollow());
            aiManager.registerBehavior("FollowClose", BehaviorFactory::createFollow(FollowBehavior::FollowMode::CLOSE_FOLLOW));
            aiManager.registerBehavior("FollowFormation", BehaviorFactory::createFollow(FollowBehavior::FollowMode::ESCORT_FORMATION));
            aiManager.registerBehavior("FollowFlank", BehaviorFactory::createFollow(FollowBehavior::FollowMode::FLANKING_FOLLOW));
            aiManager.registerBehavior("FollowRear", BehaviorFactory::createFollow(FollowBehavior::FollowMode::REAR_GUARD));
        }
    };
    
    /**
     * @brief Behavior configuration presets
     */
    namespace Presets {
        // Civilian behaviors
        inline std::shared_ptr<IdleBehavior> createCivilianIdle() {
            return BehaviorFactory::createIdle(IdleBehavior::IdleMode::SUBTLE_SWAY, 15.0f);
        }
        
        inline std::shared_ptr<WanderBehavior> createCivilianWander() {
            return BehaviorFactory::createWander(WanderBehavior::WanderMode::SMALL_AREA, 1.0f);
        }
        
        // Guard behaviors
        inline std::shared_ptr<GuardBehavior> createSentryGuard(const Vector2D& position) {
            auto guard = BehaviorFactory::createGuard(position, GuardBehavior::GuardMode::STATIC_GUARD, 150.0f);
            guard->setThreatDetectionRange(200.0f);
            guard->setFieldOfView(180.0f);
            return guard;
        }
        
        inline std::shared_ptr<GuardBehavior> createPatrolGuard(const Vector2D& position, const std::vector<Vector2D>& waypoints) {
            auto guard = BehaviorFactory::createGuard(position, GuardBehavior::GuardMode::PATROL_GUARD, 100.0f);
            guard->setPatrolWaypoints(waypoints);
            guard->setMovementSpeed(1.5f);
            return guard;
        }
        
        // Combat behaviors
        inline std::shared_ptr<AttackBehavior> createWarrior() {
            auto attack = BehaviorFactory::createAttack(AttackBehavior::AttackMode::MELEE_ATTACK, 60.0f, 15.0f);
            attack->setAttackSpeed(1.2f);
            attack->setAggression(0.8f);
            attack->setComboAttacks(true, 3);
            return attack;
        }
        
        inline std::shared_ptr<AttackBehavior> createArcher() {
            auto attack = BehaviorFactory::createAttack(AttackBehavior::AttackMode::RANGED_ATTACK, 300.0f, 12.0f);
            attack->setOptimalRange(200.0f);
            attack->setMinimumRange(100.0f);
            return attack;
        }
        
        inline std::shared_ptr<AttackBehavior> createBerserker() {
            auto attack = BehaviorFactory::createAttack(AttackBehavior::AttackMode::BERSERKER_ATTACK, 80.0f, 20.0f);
            attack->setAggression(1.0f);
            attack->setRetreatThreshold(0.1f);
            attack->setAttackSpeed(2.0f);
            return attack;
        }
        
        // Animal behaviors
        inline std::shared_ptr<FleeBehavior> createPreyAnimal() {
            auto flee = BehaviorFactory::createFlee(FleeBehavior::FleeMode::PANIC_FLEE, 5.0f, 300.0f);
            flee->setSafeDistance(500.0f);
            flee->setPanicDuration(5.0f);
            return flee;
        }
        
        inline std::shared_ptr<ChaseBehavior> createPredator() {
            auto chase = BehaviorFactory::createChase(4.0f, 400.0f, 30.0f);
            chase->setChaseSpeed(4.5f);
            return chase;
        }
        
        // Companion behaviors
        inline std::shared_ptr<FollowBehavior> createLoyalCompanion() {
            auto follow = BehaviorFactory::createFollow(FollowBehavior::FollowMode::CLOSE_FOLLOW, 3.0f);
            follow->setFollowDistance(80.0f);
            follow->setCatchUpSpeed(2.0f);
            follow->setPredictiveFollowing(true, 0.8f);
            return follow;
        }
        
        inline std::shared_ptr<FollowBehavior> createEscortGuard() {
            auto follow = BehaviorFactory::createFollow(FollowBehavior::FollowMode::ESCORT_FORMATION, 2.5f);
            follow->setFollowDistance(120.0f);
            follow->setMaxDistance(300.0f);
            return follow;
        }
    }
}

#endif // AI_BEHAVIORS_HPP