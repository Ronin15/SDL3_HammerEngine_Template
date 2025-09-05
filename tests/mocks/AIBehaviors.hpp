/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef TESTS_MOCKS_AI_BEHAVIORS_HPP
#define TESTS_MOCKS_AI_BEHAVIORS_HPP

/**
 * @file AIBehaviors.hpp (test helper)
 * @brief Aggregates real AI behavior headers and offers simple factories/registrars
 *        for tests and benchmarks. Not part of the public engine API.
 */

// Public engine headers required by the behavior headers
#include "managers/AIManager.hpp"

// Real behavior headers
#include "ai/behaviors/IdleBehavior.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "ai/behaviors/PatrolBehavior.hpp"
#include "ai/behaviors/ChaseBehavior.hpp"
#include "ai/behaviors/FleeBehavior.hpp"
#include "ai/behaviors/FollowBehavior.hpp"
#include "ai/behaviors/GuardBehavior.hpp"
#include "ai/behaviors/AttackBehavior.hpp"

namespace AIBehaviors {

class BehaviorFactory {
public:
    static std::shared_ptr<IdleBehavior> createIdle(
        IdleBehavior::IdleMode mode = IdleBehavior::IdleMode::STATIONARY,
        float radius = 20.0f) {
        return std::make_shared<IdleBehavior>(mode, radius);
    }

    static std::shared_ptr<WanderBehavior> createWander(
        WanderBehavior::WanderMode mode = WanderBehavior::WanderMode::MEDIUM_AREA,
        float speed = 2.0f) {
        return std::make_shared<WanderBehavior>(mode, speed);
    }

    static std::shared_ptr<PatrolBehavior> createPatrol(
        const std::vector<Vector2D>& waypoints,
        float speed = 2.0f) {
        return std::make_shared<PatrolBehavior>(waypoints, speed);
    }

    static std::shared_ptr<PatrolBehavior> createPatrolWithMode(
        PatrolBehavior::PatrolMode mode,
        float speed = 2.0f) {
        return std::make_shared<PatrolBehavior>(mode, speed);
    }

    static std::shared_ptr<ChaseBehavior> createChase(
        float speed = 3.0f,
        float maxRange = 500.0f,
        float minRange = 50.0f) {
        return std::make_shared<ChaseBehavior>(speed, maxRange, minRange);
    }

    static std::shared_ptr<FleeBehavior> createFlee(
        FleeBehavior::FleeMode mode = FleeBehavior::FleeMode::PANIC_FLEE,
        float speed = 4.0f,
        float detectionRange = 400.0f) {
        return std::make_shared<FleeBehavior>(mode, speed, detectionRange);
    }

    static std::shared_ptr<FollowBehavior> createFollow(
        FollowBehavior::FollowMode mode = FollowBehavior::FollowMode::LOOSE_FOLLOW,
        float speed = 2.5f) {
        return std::make_shared<FollowBehavior>(mode, speed);
    }

    static std::shared_ptr<GuardBehavior> createGuard(
        const Vector2D& guardPosition,
        GuardBehavior::GuardMode mode = GuardBehavior::GuardMode::STATIC_GUARD,
        float radius = 200.0f) {
        return std::make_shared<GuardBehavior>(mode, guardPosition, radius);
    }

    static std::shared_ptr<AttackBehavior> createAttack(
        AttackBehavior::AttackMode mode = AttackBehavior::AttackMode::MELEE_ATTACK,
        float range = 80.0f,
        float damage = 10.0f) {
        return std::make_shared<AttackBehavior>(mode, range, damage);
    }
};

class BehaviorRegistrar {
public:
    static void registerAllBehaviors(class AIManager& aiManager) {
        aiManager.registerBehavior("Idle", BehaviorFactory::createIdle());
        aiManager.registerBehavior("Wander", BehaviorFactory::createWander());
        aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
        aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
        aiManager.registerBehavior("Follow", BehaviorFactory::createFollow());

        std::vector<Vector2D> defaultWaypoints = {
            Vector2D(0, 0), Vector2D(100, 0), Vector2D(100, 100), Vector2D(0, 100)
        };
        aiManager.registerBehavior("Patrol", BehaviorFactory::createPatrol(defaultWaypoints));

        Vector2D defaultGuardPos(0, 0);
        aiManager.registerBehavior("Guard", BehaviorFactory::createGuard(defaultGuardPos));
        aiManager.registerBehavior("Attack", BehaviorFactory::createAttack());

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

    static void registerEssentialBehaviors(class AIManager& aiManager) {
        aiManager.registerBehavior("Idle", BehaviorFactory::createIdle());
        aiManager.registerBehavior("Wander", BehaviorFactory::createWander());
        aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
        aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
    }

    static void registerCombatBehaviors(class AIManager& aiManager) {
        Vector2D defaultGuardPos(0, 0);
        aiManager.registerBehavior("Guard", BehaviorFactory::createGuard(defaultGuardPos));
        aiManager.registerBehavior("Attack", BehaviorFactory::createAttack());
        aiManager.registerBehavior("Chase", BehaviorFactory::createChase());
        aiManager.registerBehavior("Flee", BehaviorFactory::createFlee());
    }

    static void registerFormationBehaviors(class AIManager& aiManager) {
        aiManager.registerBehavior("Follow", BehaviorFactory::createFollow());
        aiManager.registerBehavior("FollowClose", BehaviorFactory::createFollow(FollowBehavior::FollowMode::CLOSE_FOLLOW));
        aiManager.registerBehavior("FollowFormation", BehaviorFactory::createFollow(FollowBehavior::FollowMode::ESCORT_FORMATION));
        aiManager.registerBehavior("FollowFlank", BehaviorFactory::createFollow(FollowBehavior::FollowMode::FLANKING_FOLLOW));
        aiManager.registerBehavior("FollowRear", BehaviorFactory::createFollow(FollowBehavior::FollowMode::REAR_GUARD));
    }
};

namespace Presets {
inline std::shared_ptr<IdleBehavior> createCivilianIdle() {
    return BehaviorFactory::createIdle(IdleBehavior::IdleMode::SUBTLE_SWAY, 15.0f);
}

inline std::shared_ptr<WanderBehavior> createCivilianWander() {
    return BehaviorFactory::createWander(WanderBehavior::WanderMode::SMALL_AREA, 1.0f);
}

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
} // namespace Presets

} // namespace AIBehaviors

#endif // TESTS_MOCKS_AI_BEHAVIORS_HPP

