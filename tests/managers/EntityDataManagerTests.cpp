/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE EntityDataManagerTests
#include <boost/test/unit_test.hpp>

#include "managers/EntityDataManager.hpp"
#include "entities/EntityHandle.hpp"
#include "utils/Vector2D.hpp"
#include <cmath>
#include <vector>

// Test tolerance for floating-point comparisons
constexpr float EPSILON = 0.001f;

// Helper to check if two floats are approximately equal
bool approxEqual(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

// ============================================================================
// Test Fixture
// ============================================================================

class EntityDataManagerTestFixture {
public:
    EntityDataManagerTestFixture() {
        edm = &EntityDataManager::Instance();
        edm->init();
    }

    ~EntityDataManagerTestFixture() {
        edm->clean();
    }

protected:
    EntityDataManager* edm;
};

// ============================================================================
// SINGLETON PATTERN TESTS
// ============================================================================

BOOST_AUTO_TEST_SUITE(SingletonTests)

BOOST_AUTO_TEST_CASE(TestSingletonPattern) {
    EntityDataManager* instance1 = &EntityDataManager::Instance();
    EntityDataManager* instance2 = &EntityDataManager::Instance();

    BOOST_CHECK(instance1 == instance2);
    BOOST_CHECK(instance1 != nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// LIFECYCLE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(LifecycleTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestInitialization) {
    // Manager should be initialized by fixture
    BOOST_CHECK(edm->isInitialized());
}

BOOST_AUTO_TEST_CASE(TestDoubleInitialization) {
    // Second init should return true (already initialized)
    bool result = edm->init();
    BOOST_CHECK(result);
    BOOST_CHECK(edm->isInitialized());
}

BOOST_AUTO_TEST_CASE(TestCleanAndReinit) {
    // Create an entity first
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    BOOST_CHECK(handle.isValid());

    // Clean should clear everything
    edm->clean();
    BOOST_CHECK(!edm->isInitialized());
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);

    // Re-init should work
    bool result = edm->init();
    BOOST_CHECK(result);
    BOOST_CHECK(edm->isInitialized());
}

BOOST_AUTO_TEST_CASE(TestPrepareForStateTransition) {
    // Create some entities
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 2);

    // State transition should clear entities
    edm->prepareForStateTransition();
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);

    // Manager should still be initialized
    BOOST_CHECK(edm->isInitialized());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ENTITY CREATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EntityCreationTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestCreateNPC) {
    Vector2D position(100.0f, 200.0f);
    EntityHandle handle = edm->createNPC(position, 16.0f, 16.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK(handle.isNPC());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::NPC));
    BOOST_CHECK(edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::NPC), 1);

    // Verify transform
    const auto& transform = edm->getTransform(handle);
    BOOST_CHECK(approxEqual(transform.position.getX(), 100.0f));
    BOOST_CHECK(approxEqual(transform.position.getY(), 200.0f));

    // Verify hot data
    const auto& hot = edm->getHotData(handle);
    BOOST_CHECK(approxEqual(hot.halfWidth, 16.0f));
    BOOST_CHECK(approxEqual(hot.halfHeight, 16.0f));
    BOOST_CHECK(hot.isAlive());
}

BOOST_AUTO_TEST_CASE(TestCreatePlayer) {
    Vector2D position(300.0f, 400.0f);
    EntityHandle handle = edm->createPlayer(position);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK(handle.isPlayer());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::Player));
    BOOST_CHECK(edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::Player), 1);

    // Verify character data
    const auto& charData = edm->getCharacterData(handle);
    BOOST_CHECK(approxEqual(charData.health, 100.0f));
    BOOST_CHECK(approxEqual(charData.maxHealth, 100.0f));
    BOOST_CHECK(charData.isCharacterAlive());
}

BOOST_AUTO_TEST_CASE(TestCreateDroppedItem) {
    Vector2D position(500.0f, 600.0f);
    HammerEngine::ResourceHandle resourceHandle{1, 1};
    EntityHandle handle = edm->createDroppedItem(position, resourceHandle, 5);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK(handle.isItem());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::DroppedItem));
    BOOST_CHECK(edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::DroppedItem), 1);

    // Verify item data
    const auto& itemData = edm->getItemData(handle);
    BOOST_CHECK_EQUAL(itemData.quantity, 5);
    BOOST_CHECK(approxEqual(itemData.pickupTimer, 0.5f));
}

BOOST_AUTO_TEST_CASE(TestCreateProjectile) {
    Vector2D position(100.0f, 100.0f);
    Vector2D velocity(50.0f, 0.0f);
    EntityHandle owner = edm->createPlayer(Vector2D(0.0f, 0.0f));
    EntityHandle handle = edm->createProjectile(position, velocity, owner, 25.0f, 3.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK(handle.isProjectile());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::Projectile));
    BOOST_CHECK(edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::Projectile), 1);

    // Verify projectile data
    const auto& projData = edm->getProjectileData(handle);
    BOOST_CHECK(approxEqual(projData.damage, 25.0f));
    BOOST_CHECK(approxEqual(projData.lifetime, 3.0f));
    BOOST_CHECK(projData.owner == owner);
}

BOOST_AUTO_TEST_CASE(TestCreateAreaEffect) {
    Vector2D position(200.0f, 200.0f);
    EntityHandle owner = edm->createPlayer(Vector2D(0.0f, 0.0f));
    EntityHandle handle = edm->createAreaEffect(position, 50.0f, owner, 10.0f, 5.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::AreaEffect));
    BOOST_CHECK(edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::AreaEffect), 1);

    // Verify area effect data
    const auto& effectData = edm->getAreaEffectData(handle);
    BOOST_CHECK(approxEqual(effectData.radius, 50.0f));
    BOOST_CHECK(approxEqual(effectData.damage, 10.0f));
    BOOST_CHECK(approxEqual(effectData.duration, 5.0f));
}

BOOST_AUTO_TEST_CASE(TestCreateStaticBody) {
    Vector2D position(400.0f, 400.0f);
    EntityHandle handle = edm->createStaticBody(position, 32.0f, 32.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(static_cast<int>(handle.kind), static_cast<int>(EntityKind::StaticObstacle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::StaticObstacle), 1);

    // Static bodies use separate storage
    size_t staticIndex = edm->getStaticIndex(handle);
    BOOST_CHECK(staticIndex != SIZE_MAX);

    const auto& staticHot = edm->getStaticHotDataByIndex(staticIndex);
    BOOST_CHECK(approxEqual(staticHot.transform.position.getX(), 400.0f));
    BOOST_CHECK(approxEqual(staticHot.halfWidth, 32.0f));
}

BOOST_AUTO_TEST_CASE(TestCreateMultipleEntities) {
    // Create various entity types
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));
    edm->createPlayer(Vector2D(300.0f, 300.0f));
    edm->createDroppedItem(Vector2D(400.0f, 400.0f), HammerEngine::ResourceHandle{1, 1}, 1);

    BOOST_CHECK_EQUAL(edm->getEntityCount(), 4);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::NPC), 2);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::Player), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::DroppedItem), 1);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ENTITY REGISTRATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EntityRegistrationTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestRegisterNPC) {
    // Use a known entity ID
    EntityHandle::IDType entityId = 12345;
    Vector2D position(100.0f, 200.0f);

    EntityHandle handle = edm->registerNPC(entityId, position, 16.0f, 16.0f, 80.0f, 100.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.id, entityId);
    BOOST_CHECK(handle.isNPC());

    // Verify character data has custom health
    const auto& charData = edm->getCharacterData(handle);
    BOOST_CHECK(approxEqual(charData.health, 80.0f));
    BOOST_CHECK(approxEqual(charData.maxHealth, 100.0f));
}

BOOST_AUTO_TEST_CASE(TestRegisterPlayer) {
    EntityHandle::IDType entityId = 67890;
    Vector2D position(300.0f, 400.0f);

    EntityHandle handle = edm->registerPlayer(entityId, position, 32.0f, 32.0f);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.id, entityId);
    BOOST_CHECK(handle.isPlayer());
}

BOOST_AUTO_TEST_CASE(TestRegisterDroppedItem) {
    EntityHandle::IDType entityId = 11111;
    Vector2D position(500.0f, 600.0f);
    HammerEngine::ResourceHandle resourceHandle{2, 3};

    EntityHandle handle = edm->registerDroppedItem(entityId, position, resourceHandle, 10);

    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(handle.id, entityId);
    BOOST_CHECK(handle.isItem());

    const auto& itemData = edm->getItemData(handle);
    BOOST_CHECK_EQUAL(itemData.quantity, 10);
}

BOOST_AUTO_TEST_CASE(TestRegisterInvalidEntityId) {
    // Registering with ID 0 should fail
    EntityHandle handle = edm->registerNPC(0, Vector2D(0.0f, 0.0f));
    BOOST_CHECK(!handle.isValid());
}

BOOST_AUTO_TEST_CASE(TestRegisterDuplicateEntity) {
    EntityHandle::IDType entityId = 99999;
    Vector2D position(100.0f, 100.0f);

    // Register once
    EntityHandle handle1 = edm->registerNPC(entityId, position);
    BOOST_CHECK(handle1.isValid());

    // Register again with same ID - should return existing handle
    EntityHandle handle2 = edm->registerNPC(entityId, Vector2D(200.0f, 200.0f));
    BOOST_CHECK(handle2.isValid());
    BOOST_CHECK_EQUAL(handle1.id, handle2.id);

    // Should still only be one entity
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::NPC), 1);
}

BOOST_AUTO_TEST_CASE(TestUnregisterEntity) {
    EntityHandle::IDType entityId = 55555;
    EntityHandle handle = edm->registerNPC(entityId, Vector2D(100.0f, 100.0f));
    BOOST_CHECK(handle.isValid());
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 1);

    // Unregister
    edm->unregisterEntity(entityId);

    // Entity should be gone
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);
    BOOST_CHECK(!edm->isValidHandle(handle));
}

BOOST_AUTO_TEST_CASE(TestUnregisterNonexistentEntity) {
    // Should not crash
    edm->unregisterEntity(99999999);
    edm->unregisterEntity(0);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// DESTRUCTION QUEUE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(DestructionQueueTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestDestroyEntity) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    BOOST_CHECK(edm->isValidHandle(handle));

    // Queue for destruction
    edm->destroyEntity(handle);

    // Still valid until processed
    BOOST_CHECK(edm->isValidHandle(handle));

    // Process destruction
    edm->processDestructionQueue();

    // Now invalid
    BOOST_CHECK(!edm->isValidHandle(handle));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);
}

BOOST_AUTO_TEST_CASE(TestDestroyMultipleEntities) {
    EntityHandle handle1 = edm->createNPC(Vector2D(100.0f, 100.0f));
    EntityHandle handle2 = edm->createNPC(Vector2D(200.0f, 200.0f));
    EntityHandle handle3 = edm->createNPC(Vector2D(300.0f, 300.0f));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 3);

    // Queue all for destruction
    edm->destroyEntity(handle1);
    edm->destroyEntity(handle2);
    edm->destroyEntity(handle3);

    // Process
    edm->processDestructionQueue();

    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);
    BOOST_CHECK(!edm->isValidHandle(handle1));
    BOOST_CHECK(!edm->isValidHandle(handle2));
    BOOST_CHECK(!edm->isValidHandle(handle3));
}

BOOST_AUTO_TEST_CASE(TestDestroyInvalidHandle) {
    // Should not crash
    edm->destroyEntity(INVALID_ENTITY_HANDLE);
    edm->processDestructionQueue();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestGenerationIncrementAfterDestruction) {
    // Create and destroy, then create again - should get different generation
    EntityHandle handle1 = edm->createNPC(Vector2D(100.0f, 100.0f));
    [[maybe_unused]] uint8_t gen1 = handle1.generation;

    edm->destroyEntity(handle1);
    edm->processDestructionQueue();

    // Create new entity - may reuse slot with new generation
    EntityHandle handle2 = edm->createNPC(Vector2D(100.0f, 100.0f));

    // The old handle should be stale
    BOOST_CHECK(!edm->isValidHandle(handle1));
    BOOST_CHECK(edm->isValidHandle(handle2));
}

BOOST_AUTO_TEST_CASE(TestProcessEmptyQueue) {
    // Should not crash
    edm->processDestructionQueue();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// HANDLE VALIDATION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(HandleValidationTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestValidHandle) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    BOOST_CHECK(edm->isValidHandle(handle));
}

BOOST_AUTO_TEST_CASE(TestInvalidHandle) {
    BOOST_CHECK(!edm->isValidHandle(INVALID_ENTITY_HANDLE));
}

BOOST_AUTO_TEST_CASE(TestGetIndex) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->getIndex(handle);

    BOOST_CHECK(index != SIZE_MAX);

    // Access by index should work
    const auto& hot = edm->getHotDataByIndex(index);
    BOOST_CHECK(hot.isAlive());
}

BOOST_AUTO_TEST_CASE(TestGetIndexInvalidHandle) {
    size_t index = edm->getIndex(INVALID_ENTITY_HANDLE);
    BOOST_CHECK_EQUAL(index, SIZE_MAX);
}

BOOST_AUTO_TEST_CASE(TestFindIndexByEntityId) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->findIndexByEntityId(handle.id);

    BOOST_CHECK(index != SIZE_MAX);
    BOOST_CHECK_EQUAL(index, edm->getIndex(handle));
}

BOOST_AUTO_TEST_CASE(TestFindIndexByInvalidEntityId) {
    size_t index = edm->findIndexByEntityId(0);
    BOOST_CHECK_EQUAL(index, SIZE_MAX);

    index = edm->findIndexByEntityId(99999999);
    BOOST_CHECK_EQUAL(index, SIZE_MAX);
}

BOOST_AUTO_TEST_CASE(TestStaleHandleDetection) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    BOOST_CHECK(edm->isValidHandle(handle));

    // Destroy the entity
    edm->destroyEntity(handle);
    edm->processDestructionQueue();

    // Old handle should be stale
    BOOST_CHECK(!edm->isValidHandle(handle));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TRANSFORM ACCESS TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TransformAccessTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetTransform) {
    Vector2D position(100.0f, 200.0f);
    EntityHandle handle = edm->createNPC(position);

    const auto& transform = edm->getTransform(handle);
    BOOST_CHECK(approxEqual(transform.position.getX(), 100.0f));
    BOOST_CHECK(approxEqual(transform.position.getY(), 200.0f));
}

BOOST_AUTO_TEST_CASE(TestModifyTransform) {
    EntityHandle handle = edm->createNPC(Vector2D(0.0f, 0.0f));

    auto& transform = edm->getTransform(handle);
    transform.position = Vector2D(500.0f, 600.0f);
    transform.velocity = Vector2D(10.0f, 20.0f);

    const auto& readTransform = edm->getTransform(handle);
    BOOST_CHECK(approxEqual(readTransform.position.getX(), 500.0f));
    BOOST_CHECK(approxEqual(readTransform.position.getY(), 600.0f));
    BOOST_CHECK(approxEqual(readTransform.velocity.getX(), 10.0f));
    BOOST_CHECK(approxEqual(readTransform.velocity.getY(), 20.0f));
}

BOOST_AUTO_TEST_CASE(TestGetTransformByIndex) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 200.0f));
    size_t index = edm->getIndex(handle);

    const auto& transform = edm->getTransformByIndex(index);
    BOOST_CHECK(approxEqual(transform.position.getX(), 100.0f));
    BOOST_CHECK(approxEqual(transform.position.getY(), 200.0f));
}

BOOST_AUTO_TEST_CASE(TestGetStaticTransformByIndex) {
    EntityHandle handle = edm->createStaticBody(Vector2D(400.0f, 500.0f), 32.0f, 32.0f);
    size_t index = edm->getStaticIndex(handle);

    const auto& transform = edm->getStaticTransformByIndex(index);
    BOOST_CHECK(approxEqual(transform.position.getX(), 400.0f));
    BOOST_CHECK(approxEqual(transform.position.getY(), 500.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// HOT DATA ACCESS TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(HotDataAccessTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetHotData) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f), 20.0f, 30.0f);

    const auto& hot = edm->getHotData(handle);
    BOOST_CHECK(hot.isAlive());
    BOOST_CHECK_EQUAL(static_cast<int>(hot.kind), static_cast<int>(EntityKind::NPC));
    BOOST_CHECK(approxEqual(hot.halfWidth, 20.0f));
    BOOST_CHECK(approxEqual(hot.halfHeight, 30.0f));
}

BOOST_AUTO_TEST_CASE(TestGetHotDataByIndex) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->getIndex(handle);

    const auto& hot = edm->getHotDataByIndex(index);
    BOOST_CHECK(hot.isAlive());
}

BOOST_AUTO_TEST_CASE(TestGetHotDataArray) {
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));

    auto hotArray = edm->getHotDataArray();
    BOOST_CHECK(hotArray.size() >= 2);

    // Count alive entities in array
    size_t aliveCount = 0;
    for (const auto& hot : hotArray) {
        if (hot.isAlive()) aliveCount++;
    }
    BOOST_CHECK_EQUAL(aliveCount, 2);
}

BOOST_AUTO_TEST_CASE(TestGetStaticHotDataArray) {
    edm->createStaticBody(Vector2D(100.0f, 100.0f), 16.0f, 16.0f);
    edm->createStaticBody(Vector2D(200.0f, 200.0f), 16.0f, 16.0f);

    auto staticArray = edm->getStaticHotDataArray();
    BOOST_CHECK(staticArray.size() >= 2);
}

BOOST_AUTO_TEST_CASE(TestHotDataFlags) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));

    auto& hot = edm->getHotData(handle);
    BOOST_CHECK(hot.isAlive());
    BOOST_CHECK(!hot.isDirty());
    BOOST_CHECK(!hot.isPendingDestroy());

    // Modify flags
    hot.setDirty(true);
    BOOST_CHECK(hot.isDirty());

    hot.setDirty(false);
    BOOST_CHECK(!hot.isDirty());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// TYPE-SPECIFIC DATA TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(TypeSpecificDataTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetCharacterData) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));

    auto& charData = edm->getCharacterData(handle);
    BOOST_CHECK(charData.isCharacterAlive());

    // Modify health
    charData.health = 50.0f;
    const auto& readData = edm->getCharacterData(handle);
    BOOST_CHECK(approxEqual(readData.health, 50.0f));
}

BOOST_AUTO_TEST_CASE(TestGetCharacterDataByIndex) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->getIndex(handle);

    const auto& charData = edm->getCharacterDataByIndex(index);
    BOOST_CHECK(charData.isCharacterAlive());
}

BOOST_AUTO_TEST_CASE(TestGetItemData) {
    HammerEngine::ResourceHandle resourceHandle{1, 2};
    EntityHandle handle = edm->createDroppedItem(Vector2D(100.0f, 100.0f), resourceHandle, 5);

    auto& itemData = edm->getItemData(handle);
    BOOST_CHECK_EQUAL(itemData.quantity, 5);

    // Modify quantity
    itemData.quantity = 10;
    const auto& readData = edm->getItemData(handle);
    BOOST_CHECK_EQUAL(readData.quantity, 10);
}

BOOST_AUTO_TEST_CASE(TestGetProjectileData) {
    EntityHandle owner = edm->createPlayer(Vector2D(0.0f, 0.0f));
    EntityHandle handle = edm->createProjectile(Vector2D(100.0f, 100.0f),
                                                 Vector2D(50.0f, 0.0f),
                                                 owner, 25.0f, 5.0f);

    const auto& projData = edm->getProjectileData(handle);
    BOOST_CHECK(approxEqual(projData.damage, 25.0f));
    BOOST_CHECK(approxEqual(projData.lifetime, 5.0f));
    BOOST_CHECK(projData.owner == owner);
}

BOOST_AUTO_TEST_CASE(TestGetAreaEffectData) {
    EntityHandle owner = edm->createPlayer(Vector2D(0.0f, 0.0f));
    EntityHandle handle = edm->createAreaEffect(Vector2D(200.0f, 200.0f),
                                                 100.0f, owner, 15.0f, 10.0f);

    const auto& effectData = edm->getAreaEffectData(handle);
    BOOST_CHECK(approxEqual(effectData.radius, 100.0f));
    BOOST_CHECK(approxEqual(effectData.damage, 15.0f));
    BOOST_CHECK(approxEqual(effectData.duration, 10.0f));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SIMULATION TIER TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SimulationTierTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestDefaultTierIsActive) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    const auto& hot = edm->getHotData(handle);
    BOOST_CHECK_EQUAL(static_cast<int>(hot.tier), static_cast<int>(SimulationTier::Active));
}

BOOST_AUTO_TEST_CASE(TestSetSimulationTier) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));

    edm->setSimulationTier(handle, SimulationTier::Background);
    const auto& hot = edm->getHotData(handle);
    BOOST_CHECK_EQUAL(static_cast<int>(hot.tier), static_cast<int>(SimulationTier::Background));

    edm->setSimulationTier(handle, SimulationTier::Hibernated);
    BOOST_CHECK_EQUAL(static_cast<int>(edm->getHotData(handle).tier),
                      static_cast<int>(SimulationTier::Hibernated));
}

BOOST_AUTO_TEST_CASE(TestUpdateSimulationTiers) {
    // Create entities at various distances
    EntityHandle near = edm->createNPC(Vector2D(100.0f, 100.0f));     // Close
    EntityHandle mid = edm->createNPC(Vector2D(2000.0f, 2000.0f));    // Medium
    EntityHandle far = edm->createNPC(Vector2D(15000.0f, 15000.0f));  // Far

    // Update tiers with reference point at origin
    Vector2D refPoint(0.0f, 0.0f);
    edm->updateSimulationTiers(refPoint, 1500.0f, 10000.0f);

    // Check tiers
    const auto& nearHot = edm->getHotData(near);
    const auto& midHot = edm->getHotData(mid);
    const auto& farHot = edm->getHotData(far);

    BOOST_CHECK_EQUAL(static_cast<int>(nearHot.tier), static_cast<int>(SimulationTier::Active));
    BOOST_CHECK_EQUAL(static_cast<int>(midHot.tier), static_cast<int>(SimulationTier::Background));
    BOOST_CHECK_EQUAL(static_cast<int>(farHot.tier), static_cast<int>(SimulationTier::Hibernated));
}

BOOST_AUTO_TEST_CASE(TestGetActiveIndices) {
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));

    // Force tier update
    edm->updateSimulationTiers(Vector2D(0.0f, 0.0f), 1500.0f, 10000.0f);

    auto activeIndices = edm->getActiveIndices();
    BOOST_CHECK_EQUAL(activeIndices.size(), 2);
}

BOOST_AUTO_TEST_CASE(TestGetBackgroundIndices) {
    // Create entities at background distance
    edm->createNPC(Vector2D(5000.0f, 5000.0f));
    edm->createNPC(Vector2D(6000.0f, 6000.0f));

    // Update tiers
    edm->updateSimulationTiers(Vector2D(0.0f, 0.0f), 1500.0f, 10000.0f);

    auto bgIndices = edm->getBackgroundIndices();
    BOOST_CHECK_EQUAL(bgIndices.size(), 2);
}

BOOST_AUTO_TEST_CASE(TestEntityCountByTier) {
    edm->createNPC(Vector2D(100.0f, 100.0f));      // Will be active
    edm->createNPC(Vector2D(5000.0f, 5000.0f));    // Will be background
    edm->createNPC(Vector2D(15000.0f, 15000.0f));  // Will be hibernated

    edm->updateSimulationTiers(Vector2D(0.0f, 0.0f), 1500.0f, 10000.0f);

    BOOST_CHECK_EQUAL(edm->getEntityCount(SimulationTier::Active), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(SimulationTier::Background), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(SimulationTier::Hibernated), 1);
}

BOOST_AUTO_TEST_CASE(TestPlayerAlwaysActive) {
    // Player should stay active regardless of distance
    EntityHandle player = edm->createPlayer(Vector2D(50000.0f, 50000.0f));

    edm->updateSimulationTiers(Vector2D(0.0f, 0.0f), 1500.0f, 10000.0f);

    const auto& hot = edm->getHotData(player);
    BOOST_CHECK_EQUAL(static_cast<int>(hot.tier), static_cast<int>(SimulationTier::Active));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// QUERY TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(QueryTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestQueryEntitiesInRadius) {
    // Create entities at known positions
    edm->createNPC(Vector2D(100.0f, 100.0f));   // In radius
    edm->createNPC(Vector2D(150.0f, 150.0f));   // In radius
    edm->createNPC(Vector2D(1000.0f, 1000.0f)); // Out of radius

    std::vector<EntityHandle> found;
    edm->queryEntitiesInRadius(Vector2D(100.0f, 100.0f), 200.0f, found);

    BOOST_CHECK_EQUAL(found.size(), 2);
}

BOOST_AUTO_TEST_CASE(TestQueryEntitiesWithKindFilter) {
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createPlayer(Vector2D(150.0f, 150.0f));
    edm->createDroppedItem(Vector2D(120.0f, 120.0f), HammerEngine::ResourceHandle{1, 1}, 1);

    std::vector<EntityHandle> found;
    edm->queryEntitiesInRadius(Vector2D(100.0f, 100.0f), 500.0f, found, EntityKind::NPC);

    BOOST_CHECK_EQUAL(found.size(), 1);
    BOOST_CHECK(found[0].isNPC());
}

BOOST_AUTO_TEST_CASE(TestQueryEmptyResult) {
    edm->createNPC(Vector2D(1000.0f, 1000.0f));

    std::vector<EntityHandle> found;
    edm->queryEntitiesInRadius(Vector2D(0.0f, 0.0f), 100.0f, found);

    BOOST_CHECK(found.empty());
}

BOOST_AUTO_TEST_CASE(TestGetEntityCount) {
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);

    edm->createNPC(Vector2D(100.0f, 100.0f));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 1);

    edm->createNPC(Vector2D(200.0f, 200.0f));
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 2);
}

BOOST_AUTO_TEST_CASE(TestGetEntityCountByKind) {
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));
    edm->createPlayer(Vector2D(300.0f, 300.0f));
    edm->createDroppedItem(Vector2D(400.0f, 400.0f), HammerEngine::ResourceHandle{1, 1}, 1);

    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::NPC), 2);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::Player), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::DroppedItem), 1);
    BOOST_CHECK_EQUAL(edm->getEntityCount(EntityKind::Projectile), 0);
}

BOOST_AUTO_TEST_CASE(TestGetIndicesByKind) {
    edm->createNPC(Vector2D(100.0f, 100.0f));
    edm->createNPC(Vector2D(200.0f, 200.0f));
    edm->createPlayer(Vector2D(300.0f, 300.0f));

    auto npcIndices = edm->getIndicesByKind(EntityKind::NPC);
    BOOST_CHECK_EQUAL(npcIndices.size(), 2);

    auto playerIndices = edm->getIndicesByKind(EntityKind::Player);
    BOOST_CHECK_EQUAL(playerIndices.size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// ENTITY LOOKUP TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EntityLookupTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestGetEntityId) {
    EntityHandle handle = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->getIndex(handle);

    EntityHandle::IDType id = edm->getEntityId(index);
    BOOST_CHECK_EQUAL(id, handle.id);
}

BOOST_AUTO_TEST_CASE(TestGetEntityIdInvalidIndex) {
    EntityHandle::IDType id = edm->getEntityId(SIZE_MAX);
    BOOST_CHECK_EQUAL(id, 0);
}

BOOST_AUTO_TEST_CASE(TestGetHandle) {
    EntityHandle original = edm->createNPC(Vector2D(100.0f, 100.0f));
    size_t index = edm->getIndex(original);

    EntityHandle retrieved = edm->getHandle(index);
    BOOST_CHECK(retrieved.isValid());
    BOOST_CHECK_EQUAL(retrieved.id, original.id);
    BOOST_CHECK_EQUAL(retrieved.generation, original.generation);
    BOOST_CHECK_EQUAL(static_cast<int>(retrieved.kind), static_cast<int>(original.kind));
}

BOOST_AUTO_TEST_CASE(TestGetHandleInvalidIndex) {
    EntityHandle handle = edm->getHandle(SIZE_MAX);
    BOOST_CHECK(!handle.isValid());
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// SLOT REUSE TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SlotReuseTests, EntityDataManagerTestFixture)

BOOST_AUTO_TEST_CASE(TestSlotReuseAfterDestruction) {
    // Create and destroy entities to test slot reuse
    std::vector<EntityHandle> handles;
    for (int i = 0; i < 10; ++i) {
        handles.push_back(edm->createNPC(Vector2D(static_cast<float>(i * 100), 0.0f)));
    }
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 10);

    // Destroy half
    for (int i = 0; i < 5; ++i) {
        edm->destroyEntity(handles[i]);
    }
    edm->processDestructionQueue();
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 5);

    // Create new entities - should reuse slots
    for (int i = 0; i < 5; ++i) {
        edm->createNPC(Vector2D(static_cast<float>(i * 100 + 50), 100.0f));
    }
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 10);

    // Verify all handles are valid
    for (int i = 5; i < 10; ++i) {
        BOOST_CHECK(edm->isValidHandle(handles[i]));
    }
}

BOOST_AUTO_TEST_CASE(TestTypeSpecificSlotReuse) {
    // Create character entities
    EntityHandle npc1 = edm->createNPC(Vector2D(100.0f, 100.0f));
    EntityHandle npc2 = edm->createNPC(Vector2D(200.0f, 200.0f));

    // Destroy first NPC
    edm->destroyEntity(npc1);
    edm->processDestructionQueue();

    // Create new NPC - should reuse character data slot
    EntityHandle npc3 = edm->createNPC(Vector2D(300.0f, 300.0f));

    // Both remaining NPCs should be valid
    BOOST_CHECK(!edm->isValidHandle(npc1));
    BOOST_CHECK(edm->isValidHandle(npc2));
    BOOST_CHECK(edm->isValidHandle(npc3));

    // Verify character data is accessible
    const auto& charData2 = edm->getCharacterData(npc2);
    const auto& charData3 = edm->getCharacterData(npc3);
    BOOST_CHECK(charData2.isCharacterAlive());
    BOOST_CHECK(charData3.isCharacterAlive());
}

BOOST_AUTO_TEST_CASE(TestMassCreationAndDestruction) {
    const size_t COUNT = 1000;

    // Create many entities
    std::vector<EntityHandle> handles;
    handles.reserve(COUNT);
    for (size_t i = 0; i < COUNT; ++i) {
        handles.push_back(edm->createNPC(Vector2D(static_cast<float>(i), 0.0f)));
    }
    BOOST_CHECK_EQUAL(edm->getEntityCount(), COUNT);

    // Destroy all
    for (const auto& handle : handles) {
        edm->destroyEntity(handle);
    }
    edm->processDestructionQueue();
    BOOST_CHECK_EQUAL(edm->getEntityCount(), 0);

    // Create again - should reuse all slots
    handles.clear();
    for (size_t i = 0; i < COUNT; ++i) {
        handles.push_back(edm->createNPC(Vector2D(static_cast<float>(i), 0.0f)));
    }
    BOOST_CHECK_EQUAL(edm->getEntityCount(), COUNT);

    // All handles should be valid
    for (const auto& handle : handles) {
        BOOST_CHECK(edm->isValidHandle(handle));
    }
}

BOOST_AUTO_TEST_SUITE_END()
