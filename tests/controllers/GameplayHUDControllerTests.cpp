/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE GameplayHUDControllerTests
#include <boost/test/unit_test.hpp>

#include "controllers/ui/GameplayHUDController.hpp"
#include "collisions/CollisionInfo.hpp"
#include "events/EntityEvents.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/ProjectileManager.hpp"
#include "../events/EventManagerTestAccess.hpp"

class GameplayHUDControllerFixture
{
public:
    GameplayHUDControllerFixture()
    {
        EventManagerTestAccess::reset();
        EventManager::Instance().init();
        GameTimeManager::Instance().init(12.0f, 1.0f);
        BOOST_REQUIRE(EntityDataManager::Instance().init());
        BOOST_REQUIRE(ProjectileManager::Instance().init());
    }

    ~GameplayHUDControllerFixture()
    {
        ProjectileManager::Instance().clean();
        EntityDataManager::Instance().clean();
        EventManager::Instance().clean();
    }

protected:
    EntityHandle createPlayerHandle(EntityHandle::IDType id = 1,
                                    const Vector2D& pos = Vector2D(100.0f, 100.0f))
    {
        return EntityDataManager::Instance().registerPlayer(id, pos);
    }

    EntityHandle createNPCTarget(const Vector2D& pos = Vector2D(200.0f, 100.0f))
    {
        return EntityDataManager::Instance().createNPCWithRaceClass(pos, "Human", "Guard");
    }

    void dispatchDamage(EntityHandle source, EntityHandle target, float damage)
    {
        auto& eventMgr = EventManager::Instance();
        auto damageEvent = eventMgr.acquireDamageEvent();
        damageEvent->configure(source, target, damage, Vector2D(10.0f, 0.0f));
        eventMgr.dispatchEvent(damageEvent, EventManager::DispatchMode::Immediate);
    }

    void handleProjectileCollision(const VoidLight::CollisionInfo& info)
    {
        ProjectileManager::Instance().handleProjectileCollision(info);
    }
};

BOOST_FIXTURE_TEST_SUITE(GameplayHUDControllerTests, GameplayHUDControllerFixture)

BOOST_AUTO_TEST_CASE(TestControllerNameAndInitialState)
{
    GameplayHUDController controller(EntityHandle{});

    BOOST_CHECK_EQUAL(controller.getName(), "GameplayHUDController");
    BOOST_CHECK(!controller.hasActiveTarget());
    BOOST_CHECK_EQUAL(controller.getTargetHealth(), 0.0f);
    BOOST_CHECK_EQUAL(controller.getTargetLabel(), "Target");
}

BOOST_AUTO_TEST_CASE(TestPlayerMeleeHitSetsTargetState)
{
    EntityHandle player = createPlayerHandle();
    EntityHandle target = createNPCTarget();
    GameplayHUDController controller(player);
    controller.subscribe();

    dispatchDamage(player, target, 25.0f);

    BOOST_REQUIRE(controller.hasActiveTarget());
    const auto& charData = EntityDataManager::Instance().getCharacterData(target);
    const float expectedPercent = (charData.health / charData.maxHealth) * 100.0f;
    BOOST_CHECK_CLOSE(controller.getTargetHealth(), expectedPercent, 0.01f);
    BOOST_CHECK_EQUAL(controller.getTargetLabel(), "Human Guard");
}

BOOST_AUTO_TEST_CASE(TestPlayerProjectileHitSetsTargetState)
{
    auto& edm = EntityDataManager::Instance();

    EntityHandle player = createPlayerHandle();
    EntityHandle target = createNPCTarget();
    GameplayHUDController controller(player);
    controller.subscribe();

    EntityHandle projectile = edm.createProjectile(
        Vector2D(190.0f, 100.0f), Vector2D(220.0f, 0.0f), player, 30.0f, 5.0f);
    const size_t projectileIdx = edm.getIndex(projectile);
    const size_t targetIdx = edm.getIndex(target);
    BOOST_REQUIRE_NE(projectileIdx, SIZE_MAX);
    BOOST_REQUIRE_NE(targetIdx, SIZE_MAX);

    VoidLight::CollisionInfo info{};
    info.indexA = projectileIdx;
    info.indexB = targetIdx;
    info.normal = Vector2D(1.0f, 0.0f);
    info.penetration = 1.0f;
    info.isMovableMovable = true;

    handleProjectileCollision(info);
    EventManager::Instance().update();

    BOOST_REQUIRE(controller.hasActiveTarget());
    const auto& targetCharData = EntityDataManager::Instance().getCharacterData(target);
    const float expectedPct = (targetCharData.health / targetCharData.maxHealth) * 100.0f;
    BOOST_CHECK_CLOSE(controller.getTargetHealth(), expectedPct, 0.01f);
    BOOST_CHECK_EQUAL(controller.getTargetLabel(), "Human Guard");
}

BOOST_AUTO_TEST_CASE(TestNPCDamageToPlayerDoesNotSetTarget)
{
    EntityHandle player = createPlayerHandle();
    EntityHandle npc = createNPCTarget();
    GameplayHUDController controller(player);
    controller.subscribe();

    dispatchDamage(npc, player, 12.0f);

    BOOST_CHECK(!controller.hasActiveTarget());
}

BOOST_AUTO_TEST_CASE(TestLethalHitClearsTargetState)
{
    EntityHandle player = createPlayerHandle();
    EntityHandle target = createNPCTarget();
    GameplayHUDController controller(player);
    controller.subscribe();

    dispatchDamage(player, target, 500.0f);

    BOOST_CHECK(!controller.hasActiveTarget());
    BOOST_CHECK_EQUAL(controller.getTargetHealth(), 0.0f);
}

BOOST_AUTO_TEST_CASE(TestTimerExpiryClearsTargetState)
{
    EntityHandle player = createPlayerHandle();
    EntityHandle target = createNPCTarget();
    GameplayHUDController controller(player);
    controller.subscribe();

    dispatchDamage(player, target, 10.0f);
    BOOST_REQUIRE(controller.hasActiveTarget());

    controller.update(GameplayHUDController::TARGET_DISPLAY_DURATION + 0.01f);

    BOOST_CHECK(!controller.hasActiveTarget());
}

BOOST_AUTO_TEST_SUITE_END()
