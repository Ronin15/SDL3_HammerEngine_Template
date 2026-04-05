/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file ProjectileManagerTests.cpp
 * @brief Tests for ProjectileManager integration with EntityDataManager
 *
 * Covers:
 * - Projectile creation and EDM field verification
 * - Position integration (movement, boundary, lifetime)
 * - Collision-to-damage event wiring
 * - State transition cleanup
 * - Global pause and perf stats
 */

#define BOOST_TEST_MODULE ProjectileManagerTests
#include <boost/test/unit_test.hpp>

#include "collisions/CollisionBody.hpp"
#include "collisions/CollisionInfo.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "events/CollisionEvent.hpp"
#include "events/EntityEvents.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/ProjectileManager.hpp"
#include <atomic>
#include <memory>
#include <vector>

namespace {

struct ThreadSystemTestLifetime {
    ThreadSystemTestLifetime() {
        HAMMER_ENABLE_BENCHMARK_MODE();
        BOOST_REQUIRE_MESSAGE(HammerEngine::ThreadSystem::Instance().init(),
                              "Failed to initialize ThreadSystem");
    }
    ~ThreadSystemTestLifetime() {
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

ThreadSystemTestLifetime g_threadSystemTestLifetime{};

} // namespace

struct ProjectileTestFixture {
    ProjectileTestFixture() {
        EntityDataManager::Instance().init();
        PathfinderManager::Instance().init();
        CollisionManager::Instance().init();
        EventManager::Instance().init();
        ProjectileManager::Instance().init();
    }

    ~ProjectileTestFixture() {
        ProjectileManager::Instance().clean();
        EventManager::Instance().clean();
        CollisionManager::Instance().clean();
        PathfinderManager::Instance().clean();
        EntityDataManager::Instance().clean();
    }

    void prepareForTest() {
        ProjectileManager::Instance().prepareForStateTransition();
        EventManager::Instance().prepareForStateTransition();
        CollisionManager::Instance().prepareForStateTransition();
        EntityDataManager::Instance().prepareForStateTransition();
    }

    // Helper: process EDM destruction queue (simulates end-of-frame in GameEngine)
    void processDestructions() {
        EntityDataManager::Instance().processDestructionQueue();
    }

    // Helper: create a player entity to serve as projectile owner
    EntityHandle createPlayerOwner(const Vector2D& pos = Vector2D(500.0f, 500.0f)) {
        auto& edm = EntityDataManager::Instance();
        return edm.registerPlayer(1, pos);
    }

    // Helper: create an NPC target
    EntityHandle createNPCTarget(const Vector2D& pos = Vector2D(600.0f, 500.0f)) {
        auto& edm = EntityDataManager::Instance();
        return edm.createNPCWithRaceClass(pos, "Human", "Guard");
    }
};


// ============================================================================
// SUITE: Lifecycle Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(LifecycleTests, ProjectileTestFixture)

BOOST_AUTO_TEST_CASE(ProjectileCreation)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();

    EntityHandle owner = createPlayerOwner();
    Vector2D pos(100.0f, 200.0f);
    Vector2D vel(150.0f, 0.0f);
    float damage = 25.0f;
    float lifetime = 3.0f;

    EntityHandle proj = edm.createProjectile(pos, vel, owner, damage, lifetime);

    // Handle validity
    BOOST_REQUIRE(proj.isValid());
    BOOST_CHECK(proj.isProjectile());
    BOOST_CHECK_EQUAL(static_cast<int>(proj.kind), static_cast<int>(EntityKind::Projectile));

    // Hot data
    size_t idx = edm.getIndex(proj);
    BOOST_REQUIRE_NE(idx, SIZE_MAX);
    const auto& hot = edm.getHotDataByIndex(idx);
    BOOST_CHECK(hot.isAlive());
    BOOST_CHECK_EQUAL(static_cast<int>(hot.kind), static_cast<int>(EntityKind::Projectile));
    BOOST_CHECK_EQUAL(static_cast<int>(hot.tier), static_cast<int>(SimulationTier::Active));
    BOOST_CHECK_CLOSE(hot.transform.position.getX(), 100.0f, 0.01f);
    BOOST_CHECK_CLOSE(hot.transform.position.getY(), 200.0f, 0.01f);
    BOOST_CHECK_CLOSE(hot.transform.velocity.getX(), 150.0f, 0.01f);
    BOOST_CHECK_CLOSE(hot.transform.velocity.getY(), 0.0f, 0.01f);

    // Collision layers
    BOOST_CHECK(hot.collisionLayers & HammerEngine::CollisionLayer::Layer_Projectile);

    // ProjectileData
    const auto& projData = edm.getProjectileData(proj);
    BOOST_CHECK(projData.owner == owner);
    BOOST_CHECK_CLOSE(projData.damage, 25.0f, 0.01f);
    BOOST_CHECK_CLOSE(projData.lifetime, 3.0f, 0.01f);
    BOOST_CHECK_CLOSE(projData.speed, 150.0f, 0.5f);
    BOOST_CHECK_EQUAL(projData.flags, 0);
}

BOOST_AUTO_TEST_CASE(OwnerCollisionMask)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();

    // Player-owned projectile should hit enemies
    EntityHandle player = createPlayerOwner();
    EntityHandle playerProj = edm.createProjectile(
        Vector2D(100.0f, 100.0f), Vector2D(100.0f, 0.0f), player, 10.0f);
    size_t playerProjIdx = edm.getIndex(playerProj);
    BOOST_REQUIRE_NE(playerProjIdx, SIZE_MAX);
    const auto& playerProjHot = edm.getHotDataByIndex(playerProjIdx);
    BOOST_CHECK(playerProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Enemy);
    BOOST_CHECK(playerProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Environment);
    BOOST_CHECK(!(playerProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Player));

    // NPC-owned projectile should hit player
    EntityHandle npc = createNPCTarget(Vector2D(200.0f, 200.0f));
    EntityHandle npcProj = edm.createProjectile(
        Vector2D(200.0f, 200.0f), Vector2D(100.0f, 0.0f), npc, 10.0f);
    size_t npcProjIdx = edm.getIndex(npcProj);
    BOOST_REQUIRE_NE(npcProjIdx, SIZE_MAX);
    const auto& npcProjHot = edm.getHotDataByIndex(npcProjIdx);
    BOOST_CHECK(npcProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Player);
    BOOST_CHECK(npcProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Environment);
    BOOST_CHECK(!(npcProjHot.collisionMask & HammerEngine::CollisionLayer::Layer_Enemy));
}

BOOST_AUTO_TEST_CASE(StateTransitionCleanup)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    // Create multiple projectiles
    for (int i = 0; i < 10; ++i) {
        edm.createProjectile(
            Vector2D(100.0f + i * 10.0f, 100.0f),
            Vector2D(100.0f, 0.0f), owner, 10.0f, 5.0f);
    }
    BOOST_CHECK_EQUAL(edm.getEntityCount(EntityKind::Projectile), 10u);

    ProjectileManager::Instance().prepareForStateTransition();
    processDestructions();

    // All projectiles should be destroyed
    BOOST_CHECK_EQUAL(edm.getIndicesByKind(EntityKind::Projectile).size(), 0u);
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================================
// SUITE: Movement Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(MovementTests, ProjectileTestFixture)

BOOST_AUTO_TEST_CASE(ProjectileMovement)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    Vector2D startPos(500.0f, 500.0f);
    Vector2D velocity(200.0f, 0.0f);
    EntityHandle proj = edm.createProjectile(startPos, velocity, owner, 10.0f, 10.0f);

    // Update for 0.1 seconds — should move ~20 pixels in X
    ProjectileManager::Instance().update(0.1f);

    size_t idx = edm.getIndex(proj);
    BOOST_REQUIRE_NE(idx, SIZE_MAX);
    const auto& transform = edm.getHotDataByIndex(idx).transform;
    float expectedX = 500.0f + 200.0f * 0.1f;  // 520
    BOOST_CHECK_CLOSE(transform.position.getX(), expectedX, 1.0f);
    BOOST_CHECK_CLOSE(transform.position.getY(), 500.0f, 1.0f);
}

BOOST_AUTO_TEST_CASE(LifetimeExpiry)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    EntityHandle proj = edm.createProjectile(
        Vector2D(500.0f, 500.0f), Vector2D(10.0f, 0.0f), owner, 10.0f, 0.05f);
    BOOST_REQUIRE(edm.isValidHandle(proj));

    // Update with dt that exceeds lifetime
    ProjectileManager::Instance().update(0.1f);
    processDestructions();

    BOOST_CHECK(!edm.isValidHandle(proj));
}

BOOST_AUTO_TEST_CASE(BoundaryDestruction)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    // Place projectile near the world edge moving fast toward it
    // Default world bounds fallback is 32000x32000
    EntityHandle proj = edm.createProjectile(
        Vector2D(31995.0f, 500.0f), Vector2D(5000.0f, 0.0f), owner, 10.0f, 10.0f);
    BOOST_REQUIRE(edm.isValidHandle(proj));

    // One update should push past boundary, triggering destroy
    ProjectileManager::Instance().update(0.1f);
    processDestructions();

    BOOST_CHECK(!edm.isValidHandle(proj));
}

BOOST_AUTO_TEST_CASE(GlobalPause)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    Vector2D startPos(500.0f, 500.0f);
    EntityHandle proj = edm.createProjectile(
        startPos, Vector2D(200.0f, 0.0f), owner, 10.0f, 10.0f);

    // Pause and update — position should NOT change
    ProjectileManager::Instance().setGlobalPause(true);
    ProjectileManager::Instance().update(1.0f);

    size_t idx = edm.getIndex(proj);
    BOOST_REQUIRE_NE(idx, SIZE_MAX);
    BOOST_CHECK_CLOSE(edm.getHotDataByIndex(idx).transform.position.getX(), 500.0f, 0.01f);

    // Unpause and update — position should change
    ProjectileManager::Instance().setGlobalPause(false);
    ProjectileManager::Instance().update(0.1f);

    idx = edm.getIndex(proj);
    BOOST_REQUIRE_NE(idx, SIZE_MAX);
    BOOST_CHECK_GT(edm.getHotDataByIndex(idx).transform.position.getX(), 510.0f);
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================================
// SUITE: Collision Tests
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(CollisionTests, ProjectileTestFixture)

BOOST_AUTO_TEST_CASE(CollisionDamage)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    auto& eventMgr = EventManager::Instance();

    EntityHandle owner = createPlayerOwner(Vector2D(100.0f, 100.0f));
    EntityHandle target = createNPCTarget(Vector2D(200.0f, 100.0f));

    float projSpeed = 200.0f;
    float projDamage = 30.0f;
    EntityHandle proj = edm.createProjectile(
        Vector2D(190.0f, 100.0f), Vector2D(projSpeed, 0.0f), owner, projDamage, 5.0f);

    size_t projIdx = edm.getIndex(proj);
    size_t targetIdx = edm.getIndex(target);
    BOOST_REQUIRE_NE(projIdx, SIZE_MAX);
    BOOST_REQUIRE_NE(targetIdx, SIZE_MAX);

    // Register a handler to capture the resulting DamageEvent
    std::atomic<bool> damageReceived{false};
    float receivedDamage = 0.0f;
    float receivedKnockbackX = 0.0f;
    auto combatToken = eventMgr.registerHandlerWithToken(
        EventTypeId::Combat,
        [&](const EventData& data) {
            auto dmgEvent = std::dynamic_pointer_cast<DamageEvent>(data.event);
            if (dmgEvent) {
                receivedDamage = dmgEvent->getDamage();
                receivedKnockbackX = dmgEvent->getKnockback().getX();
                damageReceived.store(true, std::memory_order_release);
            }
        });

    // Construct and dispatch a collision event between projectile and target
    HammerEngine::CollisionInfo info{};
    info.indexA = projIdx;
    info.indexB = targetIdx;
    info.normal = Vector2D(1.0f, 0.0f);
    info.penetration = 1.0f;
    info.isMovableMovable = true;

    auto collisionEvent = std::make_shared<CollisionEvent>(info);
    EventData collData;
    collData.typeId = EventTypeId::Collision;
    collData.setActive(true);
    collData.event = collisionEvent;

    // Enqueue collision event
    eventMgr.enqueueBatch({{EventTypeId::Collision, std::move(collData)}});

    // Process collision events — triggers ProjectileManager's handler
    eventMgr.update();
    processDestructions();

    // Process combat events — triggers our damage capture handler
    eventMgr.update();

    BOOST_CHECK(damageReceived.load(std::memory_order_acquire));
    BOOST_CHECK_CLOSE(receivedDamage, projDamage, 0.01f);

    // Knockback should be speed-proportional: base(30) + speed(200) * factor(0.1) = 50
    float expectedKnockback = 30.0f + projSpeed * 0.1f;
    BOOST_CHECK_CLOSE(receivedKnockbackX, expectedKnockback, 1.0f);

    // Non-piercing projectile should be destroyed
    BOOST_CHECK(!edm.isValidHandle(proj));

    eventMgr.removeHandler(combatToken);
}

BOOST_AUTO_TEST_CASE(PiercingFlag)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    auto& eventMgr = EventManager::Instance();

    EntityHandle owner = createPlayerOwner(Vector2D(100.0f, 100.0f));
    EntityHandle target = createNPCTarget(Vector2D(200.0f, 100.0f));

    EntityHandle proj = edm.createProjectile(
        Vector2D(190.0f, 100.0f), Vector2D(200.0f, 0.0f), owner, 15.0f, 5.0f);

    // Set piercing flag
    auto& projData = edm.getProjectileData(proj);
    projData.flags |= ProjectileData::FLAG_PIERCING;

    size_t projIdx = edm.getIndex(proj);
    size_t targetIdx = edm.getIndex(target);

    HammerEngine::CollisionInfo info{};
    info.indexA = projIdx;
    info.indexB = targetIdx;
    info.normal = Vector2D(1.0f, 0.0f);
    info.penetration = 1.0f;
    info.isMovableMovable = true;

    auto collisionEvent = std::make_shared<CollisionEvent>(info);
    EventData collData;
    collData.typeId = EventTypeId::Collision;
    collData.setActive(true);
    collData.event = collisionEvent;

    eventMgr.enqueueBatch({{EventTypeId::Collision, std::move(collData)}});
    eventMgr.update();

    // Piercing projectile should survive
    BOOST_CHECK(edm.isValidHandle(proj));
}

BOOST_AUTO_TEST_SUITE_END()


// ============================================================================
// SUITE: Performance Stats
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(PerfStatsTests, ProjectileTestFixture)

BOOST_AUTO_TEST_CASE(PerfStatsTracking)
{
    prepareForTest();
    auto& edm = EntityDataManager::Instance();
    EntityHandle owner = createPlayerOwner();

    // Create a batch of projectiles
    for (int i = 0; i < 50; ++i) {
        edm.createProjectile(
            Vector2D(500.0f + i * 10.0f, 500.0f),
            Vector2D(100.0f, 0.0f), owner, 10.0f, 999.0f);
    }

    // Run several updates
    for (int i = 0; i < 5; ++i) {
        ProjectileManager::Instance().update(0.016f);
    }

    const auto& stats = ProjectileManager::Instance().getPerfStats();
    BOOST_CHECK_GT(stats.totalUpdates, 0u);
    BOOST_CHECK_EQUAL(stats.lastEntitiesProcessed, 50u);
    BOOST_CHECK_GT(stats.lastUpdateMs, 0.0);
    BOOST_CHECK_GT(stats.avgUpdateMs, 0.0);
}

BOOST_AUTO_TEST_SUITE_END()
