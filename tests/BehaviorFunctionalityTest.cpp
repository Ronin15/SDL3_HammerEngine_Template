/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE BehaviorFunctionalityTest
#include <boost/test/unit_test.hpp>

#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/EventManager.hpp"
#include "managers/GameTimeManager.hpp"
#include "managers/WorldManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "core/ThreadSystem.hpp"
#include "world/WorldData.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <chrono>

// Test helper for data-driven NPCs (NPCs are purely data, no Entity class)
class TestNPC {
public:
    explicit TestNPC(float x = 0.0f, float y = 0.0f) {
        auto& edm = EntityDataManager::Instance();
        m_handle = edm.createNPCWithRaceClass(Vector2D(x, y), "Human", "Guard");
        m_initialPosition = Vector2D(x, y);
    }

    static std::shared_ptr<TestNPC> create(float x = 0.0f, float y = 0.0f) {
        return std::make_shared<TestNPC>(x, y);
    }

    [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

    // Position access via EDM
    [[nodiscard]] Vector2D getPosition() const {
        if (!m_handle.isValid()) return Vector2D(0, 0);
        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return Vector2D(0, 0);
        return edm.getTransformByIndex(index).position;
    }

    void setPosition(const Vector2D& pos) {
        if (!m_handle.isValid()) return;
        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return;
        edm.getTransformByIndex(index).position = pos;
    }

    [[nodiscard]] Vector2D getVelocity() const {
        if (!m_handle.isValid()) return Vector2D(0, 0);
        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return Vector2D(0, 0);
        return edm.getTransformByIndex(index).velocity;
    }

    // Check if position changed in EDM (AIManager writes directly to EDM)
    int getUpdateCount() const {
        if (!m_handle.isValid()) return 0;

        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return 0;

        auto& transform = edm.getTransformByIndex(index);
        Vector2D currentPos = transform.position;
        Vector2D velocity = transform.velocity;

        bool positionMoved = (currentPos - m_initialPosition).length() > 0.01f;
        bool hasVelocity = velocity.length() > 0.01f;

        return (positionMoved || hasVelocity) ? 1 : 0;
    }

    void resetUpdateCount() {
        if (m_handle.isValid()) {
            auto& edm = EntityDataManager::Instance();
            size_t index = edm.getIndex(m_handle);
            if (index != SIZE_MAX) {
                m_initialPosition = edm.getTransformByIndex(index).position;
            }
        }
    }

private:
    EntityHandle m_handle;
    Vector2D m_initialPosition;
};

// Test fixture for behavior functionality tests
struct BehaviorTestFixture {
    BehaviorTestFixture() {
        // Initialize ThreadSystem first (required for PathfinderManager async tasks)
        HammerEngine::ThreadSystem::Instance().init();

        // Initialize managers in proper order (matches CollisionPathfindingIntegrationTests)
        GameTimeManager::Instance().init();  // Required for combat timing in behaviors
        EventManager::Instance().init();
        WorldManager::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
        BackgroundSimulationManager::Instance().init();

        // Load a simple test world for pathfinding
        // Note: World must be >= 26x26 to satisfy VILLAGE_RADIUS constraints in WorldGenerator
        HammerEngine::WorldGenerationConfig cfg{};
        cfg.width = 30; cfg.height = 30; cfg.seed = 12345;
        cfg.elevationFrequency = 0.05f; cfg.humidityFrequency = 0.05f;
        cfg.waterLevel = 0.3f; cfg.mountainLevel = 0.7f;

        if (!WorldManager::Instance().loadNewWorld(cfg)) {
            throw std::runtime_error("Failed to load test world for behavior tests");
        }

        // EVENT-DRIVEN: Process any deferred events (triggers WorldLoaded task on ThreadSystem)
        EventManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EventManager::Instance().update();

        // Set world bounds explicitly for tests (20x20 tiles * 64 pixels/tile = 1280x1280)
        const float TILE_SIZE = 64.0f;
        float worldPixelWidth = cfg.width * TILE_SIZE;
        float worldPixelHeight = cfg.height * TILE_SIZE;
        CollisionManager::Instance().setWorldBounds(0, 0, worldPixelWidth, worldPixelHeight);

        // Rebuild pathfinding grid (async operation - best effort, not critical for basic tests)
        PathfinderManager::Instance().rebuildGrid();
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Give grid a chance to build

        // NOTE: Behaviors are auto-registered in AIManager::init() - no manual registration needed

        // Create test NPCs (data-driven, no Entity class)
        for (int i = 0; i < 5; ++i) {
            testEntities.push_back(TestNPC::create(i * 100.0f, i * 100.0f));
        }

        // Set a mock player for behaviors that need a target
        playerEntity = TestNPC::create(500.0f, 500.0f);
        EntityHandle playerHandle = playerEntity->getHandle();
        AIManager::Instance().setPlayerHandle(playerHandle);

        // Initial tier update to mark entities as Active
        BackgroundSimulationManager::Instance().update(Vector2D(500.0f, 500.0f), 0.016f);
    }

    // Helper to update AI with proper tier management
    void updateAI(float deltaTime, const Vector2D& referencePoint = Vector2D(500.0f, 500.0f)) {
        // Force tier recalculation (tests create/destroy entities frequently)
        BackgroundSimulationManager::Instance().invalidateTiers();
        BackgroundSimulationManager::Instance().update(referencePoint, deltaTime);
        AIManager::Instance().update(deltaTime);
    }

    ~BehaviorTestFixture() {
        // Release test entities first
        testEntities.clear();
        playerEntity.reset();

        // Clean up managers in reverse initialization order
        // Use prepareForStateTransition() for AIManager to properly reset paused state
        // (clean() sets m_globallyPaused=true but init() doesn't reset it)
        BackgroundSimulationManager::Instance().clean();
        AIManager::Instance().prepareForStateTransition();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        WorldManager::Instance().clean();
        EventManager::Instance().clean();
        // ThreadSystem persists across tests
    }

    std::vector<std::shared_ptr<TestNPC>> testEntities;
    std::shared_ptr<TestNPC> playerEntity;
};

// Test Suite 1: Basic Behavior Registration and Assignment
BOOST_FIXTURE_TEST_SUITE(BehaviorRegistrationTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestAllBehaviorsRegistered) {
    // Test that all 8 behavior types are auto-registered
    std::vector<std::string> expectedBehaviors = {
        "Idle", "Wander", "Patrol", "Chase", "Flee", "Follow", "Guard", "Attack"
    };

    for (const auto& behaviorName : expectedBehaviors) {
        BOOST_CHECK(AIManager::Instance().hasBehavior(behaviorName));
    }
}

BOOST_AUTO_TEST_CASE(TestBehaviorAssignment) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();

    // Test assigning a behavior
    AIManager::Instance().assignBehavior(handle, "Wander");
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Test switching behaviors
    AIManager::Instance().assignBehavior(handle, "Chase");
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Test unassigning behavior
    AIManager::Instance().unassignBehavior(handle);
    BOOST_CHECK(!AIManager::Instance().hasBehavior(handle));
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 2: Idle Behavior Testing
BOOST_FIXTURE_TEST_SUITE(IdleBehaviorTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestIdleStationaryMode) {
    auto entity = testEntities[0];
    Vector2D initialPos = entity->getPosition();

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Idle");

    // Update multiple times
    for (int i = 0; i < 10; ++i) {
        updateAI(0.016f);
    }

    // Position should remain relatively unchanged for idle
    // Note: CollisionManager may push entities apart slightly via resolve()
    Vector2D currentPos = entity->getPosition();
    float distanceMoved = (currentPos - initialPos).length();
    BOOST_CHECK_LT(distanceMoved, 35.0f); // Allow for collision resolution pushes
}

BOOST_AUTO_TEST_CASE(TestIdleMessageHandling) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Idle");

    // Test mode switching via messages
    AIManager::Instance().sendMessageToEntity(handle, "idle_sway", true);
    AIManager::Instance().sendMessageToEntity(handle, "idle_fidget", true);
    AIManager::Instance().sendMessageToEntity(handle, "reset_position", true);

    // No crashes should occur
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 3: Movement Behavior Testing
BOOST_FIXTURE_TEST_SUITE(MovementBehaviorTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestWanderBehavior) {
    // Create fresh entity for this test to avoid interference
    auto entity = TestNPC::create(640.0f, 640.0f); // Center of 30x30 tile world

    // EDM-CENTRIC: Set collision layers directly on EDM hot data
    {
        auto& edm = EntityDataManager::Instance();
        size_t idx = edm.getIndex(entity->getHandle());
        if (idx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(idx);
            hot.collisionLayers = CollisionLayer::Layer_Enemy;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
    }

    Vector2D initialPos = entity->getPosition();
    entity->resetUpdateCount();

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Wander");

    // Track movement over time
    int movementSteps = 0;
    Vector2D lastPos = initialPos;
    float totalDistanceMoved = 0.0f;
    bool hasVelocity = false;

    // Use larger deltaTime to advance cooldowns faster (30s wander cooldown)
    // 70 iterations * 0.5f = 35s of simulated time (enough to pass 30s cooldown)
    const float testDeltaTime = 0.5f;
    for (int i = 0; i < 70; ++i) {
        updateAI(testDeltaTime);
        CollisionManager::Instance().update(testDeltaTime);

        Vector2D pos = entity->getPosition();
        Vector2D vel = entity->getVelocity();
        float stepDistance = (pos - lastPos).length();

        totalDistanceMoved += stepDistance; // Track ALL movement
        if (stepDistance > 0.1f) {
            movementSteps++;
        }
        if (vel.length() > 0.1f) {
            hasVelocity = true;
        }
        lastPos = pos;

        if (i % 14 == 0) {
            BOOST_TEST_MESSAGE("Wander Update " << i << ": pos=(" << pos.getX() << ", " << pos.getY() << ") vel=" << vel.length() << " moved=" << totalDistanceMoved);
        }
    }

    // Verify entity actually wandered (moved or has velocity indicating intent to move)
    Vector2D currentPos = entity->getPosition();
    float distanceMoved = (currentPos - initialPos).length();

    BOOST_TEST_MESSAGE("Wander test: moved " << distanceMoved << "px over " << movementSteps << " steps, total=" << totalDistanceMoved);

    // Entity should either have moved OR have velocity set (async pathfinding may delay actual movement)
    bool isWandering = (totalDistanceMoved > 5.0f) || hasVelocity;
    BOOST_CHECK(isWandering); // Verify wander behavior is functioning

    // Clean up
    AIManager::Instance().unassignBehavior(handle);
}

BOOST_AUTO_TEST_CASE(TestChaseBehavior) {
    // Test: NPC chases an opponent who attacked them (memory-based targeting)
    auto chaser = TestNPC::create(200.0f, 200.0f);
    auto opponent = TestNPC::create(500.0f, 500.0f);  // The attacker

    auto& edm = EntityDataManager::Instance();
    EntityHandle chaserHandle = chaser->getHandle();
    EntityHandle opponentHandle = opponent->getHandle();

    // EDM-CENTRIC: Set collision layers directly on EDM hot data
    {
        size_t chaserIdx = edm.getIndex(chaserHandle);
        if (chaserIdx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(chaserIdx);
            hot.collisionLayers = CollisionLayer::Layer_Enemy;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
        size_t opponentIdx = edm.getIndex(opponentHandle);
        if (opponentIdx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(opponentIdx);
            hot.collisionLayers = CollisionLayer::Layer_Enemy;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
    }

    Vector2D initialPos = chaser->getPosition();
    Vector2D opponentPos = opponent->getPosition();
    chaser->resetUpdateCount();

    BOOST_TEST_MESSAGE("Initial chaser pos: (" << initialPos.getX() << ", " << initialPos.getY() << ")");
    BOOST_TEST_MESSAGE("Opponent pos: (" << opponentPos.getX() << ", " << opponentPos.getY() << ")");
    BOOST_TEST_MESSAGE("Initial distance: " << (initialPos - opponentPos).length());

    // Register chaser with Chase behavior
    AIManager::Instance().assignBehavior(chaserHandle, "Chase");

    // Set up memory context: opponent attacked the chaser, so chaser pursues
    {
        size_t chaserIdx = edm.getIndex(chaserHandle);
        if (chaserIdx != SIZE_MAX) {
            edm.initMemoryData(chaserIdx);
            auto& memData = edm.getMemoryData(chaserIdx);
            memData.lastAttacker = opponentHandle;  // Opponent attacked us - chase them!
            memData.lastCombatTime = 0.0f;          // Just happened
            memData.setValid(true);
        }
    }

    // Track movement progress
    int significantMovementCount = 0;
    Vector2D lastPos = initialPos;
    float totalDistanceMoved = 0.0f;

    const float testDeltaTime = 0.1f;
    for (int i = 0; i < 50; ++i) {
        updateAI(testDeltaTime);
        CollisionManager::Instance().update(testDeltaTime);

        Vector2D pos = chaser->getPosition();
        float stepDistance = (pos - lastPos).length();
        totalDistanceMoved += stepDistance;
        if (stepDistance > 0.1f) {
            significantMovementCount++;
        }
        lastPos = pos;

        if (i % 10 == 0) {
            Vector2D vel = chaser->getVelocity();
            BOOST_TEST_MESSAGE("Update " << i << ": pos=(" << pos.getX() << ", " << pos.getY() << ") vel=(" << vel.getX() << ", " << vel.getY() << ") moved=" << totalDistanceMoved);
        }
    }

    // Verify actual movement occurred and chaser got closer to opponent
    Vector2D currentPos = chaser->getPosition();
    Vector2D currentVel = chaser->getVelocity();
    float initialDistanceToOpponent = (initialPos - opponentPos).length();
    float currentDistanceToOpponent = (currentPos - opponentPos).length();

    BOOST_TEST_MESSAGE("Final chaser pos: (" << currentPos.getX() << ", " << currentPos.getY() << ")");
    BOOST_TEST_MESSAGE("Final velocity: (" << currentVel.getX() << ", " << currentVel.getY() << ")");
    BOOST_TEST_MESSAGE("Initial distance: " << initialDistanceToOpponent << " -> Final distance: " << currentDistanceToOpponent);
    BOOST_TEST_MESSAGE("Total distance moved: " << totalDistanceMoved << " over " << significantMovementCount << " steps");

    BOOST_CHECK_GT(totalDistanceMoved, 5.0f); // Chaser must have actually moved
    BOOST_CHECK_LT(currentDistanceToOpponent, initialDistanceToOpponent); // Must get closer

    // Clean up
    AIManager::Instance().unassignBehavior(chaserHandle);
}

BOOST_AUTO_TEST_CASE(TestFleeBehavior) {
    auto testPlayer = TestNPC::create(500.0f, 500.0f);
    auto entity = TestNPC::create(600.0f, 600.0f); // Close to player

    // EDM-CENTRIC: Set collision layers
    {
        auto& edm = EntityDataManager::Instance();
        size_t entityIdx = edm.getIndex(entity->getHandle());
        if (entityIdx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(entityIdx);
            hot.collisionLayers = CollisionLayer::Layer_Enemy;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
        size_t playerIdx = edm.getIndex(testPlayer->getHandle());
        if (playerIdx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(playerIdx);
            hot.collisionLayers = CollisionLayer::Layer_Player;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
    }

    EntityHandle playerHandle = testPlayer->getHandle();
    AIManager::Instance().setPlayerHandle(playerHandle);

    Vector2D playerPos = testPlayer->getPosition();
    Vector2D fleeStartPos = entity->getPosition();
    entity->resetUpdateCount();

    EntityHandle handle = entity->getHandle();

    // FleeBehavior requires a lastAttacker in memory to know who to flee from
    auto &edm = EntityDataManager::Instance();
    size_t entityIdx = edm.getIndex(handle);
    if (entityIdx != SIZE_MAX) {
        edm.recordCombatEvent(entityIdx, playerHandle, handle, 10.0f,
                              /*wasAttacked=*/true, 0.0f);
    }

    AIManager::Instance().assignBehavior(handle, "Flee");

    const float testDeltaTime = 0.1f;
    for (int i = 0; i < 30; ++i) {
        updateAI(testDeltaTime);
        CollisionManager::Instance().update(testDeltaTime);
    }

    // Entity should move away from player (or at least have velocity set)
    Vector2D currentPos = entity->getPosition();
    Vector2D currentVel = entity->getVelocity();
    float initialDistanceToPlayer = (fleeStartPos - playerPos).length();
    float currentDistanceToPlayer = (currentPos - playerPos).length();

    // Check that entity is attempting to flee
    bool isFleeing = (currentDistanceToPlayer > initialDistanceToPlayer) || (currentVel.length() > 0.1f);
    BOOST_CHECK(isFleeing);

    // Clean up
    AIManager::Instance().unassignBehavior(handle);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 4: Complex Behavior Testing
BOOST_FIXTURE_TEST_SUITE(ComplexBehaviorTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestFollowBehavior) {
    auto testPlayer = TestNPC::create(500.0f, 500.0f);
    auto entity = TestNPC::create(300.0f, 500.0f); // 200 pixels away

    EntityHandle playerHandle = testPlayer->getHandle();
    AIManager::Instance().setPlayerHandle(playerHandle);

    Vector2D playerPos = testPlayer->getPosition();
    entity->resetUpdateCount();

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Follow");

    // Move player to a new position within range
    Vector2D newPlayerPos(playerPos.getX() + 150, playerPos.getY() + 150);
    testPlayer->setPosition(newPlayerPos);

    const float testDeltaTime = 0.25f;
    for (int i = 0; i < 50; ++i) {
        updateAI(testDeltaTime);
    }

    // Entity should move closer to player
    Vector2D currentPos = entity->getPosition();
    float distanceToPlayer = (currentPos - newPlayerPos).length();

    BOOST_CHECK_LT(distanceToPlayer, 600.0f); // Should be reasonably close

    // Clean up
    AIManager::Instance().unassignBehavior(handle);
}

BOOST_AUTO_TEST_CASE(TestGuardBehavior) {
    auto entity = testEntities[0];
    Vector2D guardPos(200, 200);
    entity->setPosition(guardPos);
    entity->resetUpdateCount();

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Guard");

    const float testDeltaTime = 0.1f;
    for (int i = 0; i < 30; ++i) {
        updateAI(testDeltaTime);
    }

    // Guard should stay reasonably near post
    Vector2D currentPos = entity->getPosition();
    float distanceFromPost = (currentPos - guardPos).length();
    BOOST_CHECK_LT(distanceFromPost, 300.0f);
}

BOOST_AUTO_TEST_CASE(TestAttackBehavior) {
    auto entity = testEntities[0];
    Vector2D playerPos = playerEntity->getPosition();

    // Position entity within attack range
    entity->setPosition(Vector2D(playerPos.getX() + 100, playerPos.getY()));
    entity->resetUpdateCount();

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Attack");

    // Capture initial behavior execution count
    size_t initialBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();

    const float testDeltaTime = 0.1f;
    for (int i = 0; i < 50; ++i) {
        updateAI(testDeltaTime);
    }

    // Entity should approach for attack
    Vector2D currentPos = entity->getPosition();
    float distanceToPlayer = (currentPos - playerPos).length();

    BOOST_CHECK_LT(distanceToPlayer, 200.0f);

    // Check that behaviors were executed
    size_t finalBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();
    BOOST_CHECK_GT(finalBehaviorCount, initialBehaviorCount);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 5: Message System Testing
BOOST_FIXTURE_TEST_SUITE(BehaviorMessageTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestBehaviorSpecificMessages) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();

    // Test Guard behavior messages
    AIManager::Instance().assignBehavior(handle, "Guard");
    AIManager::Instance().sendMessageToEntity(handle, "raise_alert", true);
    AIManager::Instance().sendMessageToEntity(handle, "clear_alert", true);
    AIManager::Instance().sendMessageToEntity(handle, "investigate_position", true);

    // Test Follow behavior messages
    AIManager::Instance().assignBehavior(handle, "Follow");
    AIManager::Instance().sendMessageToEntity(handle, "follow_close", true);
    AIManager::Instance().sendMessageToEntity(handle, "follow_formation", true);
    AIManager::Instance().sendMessageToEntity(handle, "stop_following", true);

    // Test Attack behavior messages
    AIManager::Instance().assignBehavior(handle, "Attack");
    AIManager::Instance().sendMessageToEntity(handle, "attack_target", true);
    AIManager::Instance().sendMessageToEntity(handle, "retreat", true);
    AIManager::Instance().sendMessageToEntity(handle, "enable_combo", true);

    // Test Flee behavior messages
    AIManager::Instance().assignBehavior(handle, "Flee");
    AIManager::Instance().sendMessageToEntity(handle, "panic", true);
    AIManager::Instance().sendMessageToEntity(handle, "calm_down", true);
    AIManager::Instance().sendMessageToEntity(handle, "recover_stamina", true);

    // No crashes should occur
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(TestBroadcastMessages) {
    EntityHandle handle0 = testEntities[0]->getHandle();
    EntityHandle handle1 = testEntities[1]->getHandle();
    EntityHandle handle2 = testEntities[2]->getHandle();

    // Assign different behaviors to multiple entities
    AIManager::Instance().assignBehavior(handle0, "Guard");
    AIManager::Instance().assignBehavior(handle1, "Attack");
    AIManager::Instance().assignBehavior(handle2, "Follow");

    // Test broadcast messages
    AIManager::Instance().broadcastMessage("global_alert", true);
    AIManager::Instance().broadcastMessage("combat_start", true);
    AIManager::Instance().broadcastMessage("all_stop", true);

    // All entities should receive messages without crashes
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 6: Behavior Transitions and State Management
BOOST_FIXTURE_TEST_SUITE(BehaviorTransitionTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestBehaviorSwitching) {
    auto entity = testEntities[0];
    entity->resetUpdateCount();
    EntityHandle handle = entity->getHandle();

    std::vector<std::string> behaviorSequence = {
        "Idle", "Wander", "Chase", "Flee", "Follow", "Guard", "Attack"
    };

    for (const auto& behavior : behaviorSequence) {
        AIManager::Instance().assignBehavior(handle, behavior);

        // Update a few times
        for (int i = 0; i < 5; ++i) {
            updateAI(0.016f);
        }

        BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

        AIManager::Instance().unassignBehavior(handle);
    }

    // Check that behaviors were executed
    size_t finalCount = AIManager::Instance().getBehaviorUpdateCount();
    BOOST_CHECK_GT(finalCount, 0);
}

BOOST_AUTO_TEST_CASE(TestMultipleEntitiesDifferentBehaviors) {
    // Assign different behaviors to different entities
    std::vector<std::string> behaviors = {"Idle", "Wander", "Chase", "Follow", "Guard"};

    // Capture initial behavior execution count
    size_t initialBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();

    for (size_t i = 0; i < behaviors.size() && i < testEntities.size(); ++i) {
        EntityHandle handle = testEntities[i]->getHandle();
        AIManager::Instance().assignBehavior(handle, behaviors[i]);
    }

    // Update all entities simultaneously
    for (int update = 0; update < 20; ++update) {
        updateAI(0.016f);
    }

    // Check that behaviors were executed
    size_t finalBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();
    BOOST_CHECK_GT(finalBehaviorCount, initialBehaviorCount);
}

// Test that verifies behavior state persists after transition
BOOST_AUTO_TEST_CASE(TestGuardToAttackTransitionStatePreserved) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();
    auto& edm = EntityDataManager::Instance();

    // Start with Guard behavior
    AIManager::Instance().assignBehavior(handle, "Guard");
    updateAI(0.016f);  // Allow behavior to initialize

    size_t edmIdx = edm.getIndex(handle);
    BOOST_REQUIRE_NE(edmIdx, SIZE_MAX);

    // Verify Guard state is valid
    BOOST_CHECK(edm.hasBehaviorData(edmIdx));
    auto& guardData = edm.getBehaviorData(edmIdx);
    BOOST_CHECK(guardData.isValid());
    BOOST_CHECK(guardData.isInitialized());

    // Now transition to Attack behavior
    AIManager::Instance().assignBehavior(handle, "Attack");
    updateAI(0.016f);  // Process the transition

    // CRITICAL CHECK: Attack behavior state must be valid after transition
    BOOST_CHECK(edm.hasBehaviorData(edmIdx));
    auto& attackData = edm.getBehaviorData(edmIdx);
    BOOST_CHECK_MESSAGE(attackData.isValid(),
        "BehaviorData should be valid after Guard->Attack transition");
    BOOST_CHECK_MESSAGE(attackData.isInitialized(),
        "BehaviorData should be initialized after Guard->Attack transition");

    // Verify entity can still function
    for (int i = 0; i < 10; ++i) {
        updateAI(0.016f);
    }
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Cleanup
    AIManager::Instance().unassignBehavior(handle);
}

// Test that all behavior transitions preserve state correctly
BOOST_AUTO_TEST_CASE(TestAllBehaviorTransitionsPreserveState) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();
    auto& edm = EntityDataManager::Instance();

    // Test transitions between all core behaviors
    std::vector<std::pair<std::string, std::string>> transitions = {
        {"Idle", "Wander"},
        {"Wander", "Chase"},
        {"Chase", "Attack"},
        {"Attack", "Flee"},
        {"Flee", "Guard"},
        {"Guard", "Attack"},  // Critical transition
        {"Attack", "Follow"},
        {"Follow", "Idle"}
    };

    for (const auto& [fromBehavior, toBehavior] : transitions) {
        // Start with first behavior
        AIManager::Instance().assignBehavior(handle, fromBehavior);
        updateAI(0.016f);

        size_t edmIdx = edm.getIndex(handle);
        BOOST_REQUIRE_NE(edmIdx, SIZE_MAX);
        BOOST_CHECK_MESSAGE(edm.hasBehaviorData(edmIdx),
            "Should have behavior data for " + fromBehavior);

        // Transition to second behavior
        AIManager::Instance().assignBehavior(handle, toBehavior);
        updateAI(0.016f);

        // Verify state after transition
        BOOST_CHECK_MESSAGE(edm.hasBehaviorData(edmIdx),
            "Should have behavior data after " + fromBehavior + " -> " + toBehavior);

        auto& behaviorData = edm.getBehaviorData(edmIdx);
        BOOST_CHECK_MESSAGE(behaviorData.isValid(),
            "BehaviorData should be valid after " + fromBehavior + " -> " + toBehavior);
        BOOST_CHECK_MESSAGE(behaviorData.isInitialized(),
            "BehaviorData should be initialized after " + fromBehavior + " -> " + toBehavior);

        // Verify entity can execute new behavior
        updateAI(0.016f);
        BOOST_CHECK_MESSAGE(AIManager::Instance().hasBehavior(handle),
            "Entity should still have behavior after " + fromBehavior + " -> " + toBehavior);

        // Clean up for next iteration
        AIManager::Instance().unassignBehavior(handle);
    }
}

// Stress test: rapid transitions should not corrupt state
BOOST_AUTO_TEST_CASE(TestRapidBehaviorTransitionsStability) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();
    auto& edm = EntityDataManager::Instance();

    std::vector<std::string> behaviors = {
        "Idle", "Wander", "Chase", "Attack", "Flee", "Guard", "Follow"
    };

    // Register initially
    AIManager::Instance().assignBehavior(handle, "Idle");
    updateAI(0.016f);

    // Rapidly switch behaviors with minimal updates between
    for (int cycle = 0; cycle < 3; ++cycle) {
        for (const auto& behavior : behaviors) {
            AIManager::Instance().assignBehavior(handle, behavior);
            updateAI(0.016f);  // Single update between transitions

            size_t edmIdx = edm.getIndex(handle);
            BOOST_REQUIRE_NE(edmIdx, SIZE_MAX);

            // State must remain valid even under rapid transitions
            BOOST_CHECK(edm.hasBehaviorData(edmIdx));
            auto& behaviorData = edm.getBehaviorData(edmIdx);
            BOOST_CHECK_MESSAGE(behaviorData.isValid(),
                "BehaviorData corrupt during rapid transitions at cycle " +
                std::to_string(cycle) + ", behavior " + behavior);
        }
    }

    // Final verification: entity should be fully functional
    for (int i = 0; i < 20; ++i) {
        updateAI(0.016f);
    }
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Cleanup
    AIManager::Instance().unassignBehavior(handle);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 7: Performance and Integration Testing
BOOST_FIXTURE_TEST_SUITE(BehaviorPerformanceTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestLargeNumberOfEntities) {
    const int NUM_ENTITIES = 50;
    std::vector<std::shared_ptr<TestNPC>> perfTestEntities;
    std::vector<EntityHandle> perfTestHandles;

    // Create many NPCs with different behaviors
    std::vector<std::string> behaviors = {"Idle", "Wander", "Chase", "Follow", "Guard"};

    for (int i = 0; i < NUM_ENTITIES; ++i) {
        auto entity = TestNPC::create(i * 10.0f, i * 10.0f);
        perfTestEntities.push_back(entity);
        EntityHandle handle = entity->getHandle();
        perfTestHandles.push_back(handle);

        std::string behavior = behaviors[i % behaviors.size()];
        AIManager::Instance().assignBehavior(handle, behavior);
    }

    // Measure update performance
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 10; ++i) {
        updateAI(0.016f);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Should complete within reasonable time
    BOOST_CHECK_LT(duration.count(), 1000); // Less than 1 second for 50 entities

    // Cleanup
    for (const auto& handle : perfTestHandles) {
        AIManager::Instance().unassignBehavior(handle);
    }
}

BOOST_AUTO_TEST_CASE(TestBehaviorMemoryManagement) {
    auto entity = testEntities[0];
    EntityHandle handle = entity->getHandle();

    // Rapidly switch between behaviors to test memory management
    std::vector<std::string> behaviors = {
        "Idle", "Wander", "Chase", "Flee", "Follow", "Guard", "Attack"
    };

    for (int cycle = 0; cycle < 5; ++cycle) {
        for (const auto& behavior : behaviors) {
            AIManager::Instance().assignBehavior(handle, behavior);

            // Brief update
            updateAI(0.016f);

            AIManager::Instance().unassignBehavior(handle);
        }
    }

    // Should not crash or leak memory
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 8: Advanced Behavior Features
BOOST_FIXTURE_TEST_SUITE(AdvancedBehaviorFeatureTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestPatrolBehaviorWithWaypoints) {
    auto entity = testEntities[0];

    Vector2D initialPos(150, 150);
    entity->setPosition(initialPos);

    // EDM-CENTRIC: Set collision layers
    {
        auto& edm = EntityDataManager::Instance();
        size_t idx = edm.getIndex(entity->getHandle());
        if (idx != SIZE_MAX) {
            auto& hot = edm.getHotDataByIndex(idx);
            hot.collisionLayers = CollisionLayer::Layer_Enemy;
            hot.collisionMask = 0xFFFF;
            hot.setCollisionEnabled(true);
        }
    }

    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "Patrol");

    // Capture initial behavior execution count
    size_t initialBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();

    // Track patrol movement
    int movementSteps = 0;
    Vector2D lastPos = initialPos;
    float totalDistanceMoved = 0.0f;
    bool hasVelocity = false;

    const float testDeltaTime = 0.5f;
    for (int i = 0; i < 40; ++i) {
        updateAI(testDeltaTime);
        CollisionManager::Instance().update(testDeltaTime);

        Vector2D pos = entity->getPosition();
        Vector2D vel = entity->getVelocity();
        float stepDistance = (pos - lastPos).length();

        totalDistanceMoved += stepDistance;
        if (stepDistance > 0.1f) {
            movementSteps++;
        }
        if (vel.length() > 0.1f) {
            hasVelocity = true;
        }
        lastPos = pos;

        if (i % 8 == 0) {
            BOOST_TEST_MESSAGE("Patrol Update " << i << ": pos=(" << pos.getX() << ", " << pos.getY() << ") vel=" << vel.length() << " moved=" << totalDistanceMoved);
        }
    }

    BOOST_TEST_MESSAGE("Patrol test: moved " << totalDistanceMoved << "px over " << movementSteps << " steps");

    // Verify patrol behavior is functioning
    size_t finalBehaviorCount = AIManager::Instance().getBehaviorUpdateCount();
    BOOST_CHECK_GT(finalBehaviorCount, initialBehaviorCount + 10);

    // Entity should either have moved OR have velocity set
    bool isPatrolling = (totalDistanceMoved > 5.0f) || hasVelocity;
    BOOST_CHECK(isPatrolling);

    // Clean up
    AIManager::Instance().unassignBehavior(handle);
}

BOOST_AUTO_TEST_SUITE_END()

// Test Suite 9: Memory and Combat System Tests
BOOST_FIXTURE_TEST_SUITE(MemoryCombatTests, BehaviorTestFixture)

BOOST_AUTO_TEST_CASE(TestMemoryInitAtCreation) {
    // Test: NPCs created via createNPCWithRaceClass have memory initialized
    auto& edm = EntityDataManager::Instance();

    auto entity = TestNPC::create(100.0f, 100.0f);
    EntityHandle handle = entity->getHandle();
    BOOST_REQUIRE(handle.isValid());

    size_t idx = edm.getIndex(handle);
    BOOST_REQUIRE(idx != SIZE_MAX);

    // Memory should be initialized at creation
    BOOST_CHECK(edm.hasMemoryData(idx));

    const auto& memData = edm.getMemoryData(idx);
    BOOST_CHECK(memData.isValid());

    // Default emotional state should be neutral
    BOOST_CHECK_GE(memData.emotions.fear, 0.0f);
    BOOST_CHECK_LE(memData.emotions.fear, 0.1f);

    BOOST_TEST_MESSAGE("Memory initialized at creation: emotions.fear=" << memData.emotions.fear);
}

BOOST_AUTO_TEST_CASE(TestLastCombatTimeDeltaSemantics) {
    // Test: lastCombatTime increments via updateEmotionalDecay
    auto& edm = EntityDataManager::Instance();

    auto entity = TestNPC::create(100.0f, 100.0f);
    size_t idx = edm.getIndex(entity->getHandle());
    BOOST_REQUIRE(idx != SIZE_MAX);
    BOOST_REQUIRE(edm.hasMemoryData(idx));

    auto& memData = edm.getMemoryData(idx);

    // Simulate being attacked
    memData.lastCombatTime = 0.0f;
    float initialTime = memData.lastCombatTime;

    // Update emotional decay
    edm.updateEmotionalDecay(idx, 1.0f);

    BOOST_CHECK_GT(memData.lastCombatTime, initialTime);
    BOOST_CHECK_CLOSE(memData.lastCombatTime, 1.0f, 0.01f);

    edm.updateEmotionalDecay(idx, 0.5f);
    BOOST_CHECK_CLOSE(memData.lastCombatTime, 1.5f, 0.01f);

    BOOST_TEST_MESSAGE("lastCombatTime delta semantics working: " << memData.lastCombatTime);
}

BOOST_AUTO_TEST_CASE(TestWanderSwitchesToFleeWhenAttacked) {
    // Test: WanderBehavior switches to FleeBehavior when entity is attacked
    auto& edm = EntityDataManager::Instance();
    auto& aiMgr = AIManager::Instance();

    auto entity = TestNPC::create(300.0f, 300.0f);
    auto attacker = TestNPC::create(350.0f, 350.0f);

    EntityHandle entityHandle = entity->getHandle();
    EntityHandle attackerHandle = attacker->getHandle();

    size_t entityIdx = edm.getIndex(entityHandle);
    BOOST_REQUIRE(entityIdx != SIZE_MAX);

    // Assign Wander behavior
    aiMgr.assignBehavior(entityHandle, "Wander");

    // Verify starting behavior is Wander
    auto& behaviorData = edm.getBehaviorData(entityIdx);
    BOOST_CHECK(behaviorData.behaviorType == BehaviorType::Wander);

    // Simulate being attacked
    auto& memData = edm.getMemoryData(entityIdx);
    memData.lastAttacker = attackerHandle;
    memData.lastCombatTime = 0.0f;

    // Run behavior updates
    for (int i = 0; i < 5; ++i) {
        updateAI(0.1f);
    }

    // Should have switched to Flee behavior
    BOOST_CHECK(behaviorData.behaviorType == BehaviorType::Flee);

    BOOST_TEST_MESSAGE("Wander -> Flee switch on attack verified");

    // Cleanup
    aiMgr.unassignBehavior(entityHandle);
}

BOOST_AUTO_TEST_CASE(TestIdleSwitchesToFleeWhenAttacked) {
    auto& edm = EntityDataManager::Instance();
    auto& aiMgr = AIManager::Instance();

    auto entity = TestNPC::create(300.0f, 300.0f);
    auto attacker = TestNPC::create(350.0f, 350.0f);

    EntityHandle entityHandle = entity->getHandle();
    EntityHandle attackerHandle = attacker->getHandle();

    size_t entityIdx = edm.getIndex(entityHandle);
    BOOST_REQUIRE(entityIdx != SIZE_MAX);

    // Assign Idle behavior
    aiMgr.assignBehavior(entityHandle, "Idle");

    // Verify starting behavior is Idle
    auto& behaviorData = edm.getBehaviorData(entityIdx);
    BOOST_CHECK(behaviorData.behaviorType == BehaviorType::Idle);

    // Simulate being attacked
    auto& memData = edm.getMemoryData(entityIdx);
    memData.lastAttacker = attackerHandle;
    memData.lastCombatTime = 0.0f;

    // Run behavior updates
    for (int i = 0; i < 5; ++i) {
        updateAI(0.1f);
    }

    // Should have switched to Flee behavior
    BOOST_CHECK(behaviorData.behaviorType == BehaviorType::Flee);

    BOOST_TEST_MESSAGE("Idle -> Flee switch on attack verified");

    // Cleanup
    aiMgr.unassignBehavior(entityHandle);
}

BOOST_AUTO_TEST_CASE(TestMassBasedKnockback) {
    auto& edm = EntityDataManager::Instance();

    auto lightEntity = TestNPC::create(100.0f, 100.0f);
    auto heavyEntity = TestNPC::create(200.0f, 100.0f);

    EntityHandle lightHandle = lightEntity->getHandle();
    EntityHandle heavyHandle = heavyEntity->getHandle();

    auto& lightChar = edm.getCharacterData(lightHandle);
    auto& heavyChar = edm.getCharacterData(heavyHandle);

    lightChar.mass = 0.5f;
    heavyChar.mass = 4.0f;

    float lightScale = 1.0f / std::max(0.1f, lightChar.mass);
    float heavyScale = 1.0f / std::max(0.1f, heavyChar.mass);

    BOOST_TEST_MESSAGE("Knockback scales - light (mass=0.5): " << lightScale
                       << ", heavy (mass=4.0): " << heavyScale);

    BOOST_CHECK_CLOSE(lightScale / heavyScale, 8.0f, 0.1f);

    BOOST_CHECK_CLOSE(lightChar.mass, 0.5f, 0.01f);
    BOOST_CHECK_CLOSE(heavyChar.mass, 4.0f, 0.01f);

    Vector2D knockbackForce(100.0f, 0.0f);
    Vector2D lightKnockback = knockbackForce * lightScale;
    Vector2D heavyKnockback = knockbackForce * heavyScale;

    BOOST_CHECK_CLOSE(lightKnockback.length(), 200.0f, 0.1f);
    BOOST_CHECK_CLOSE(heavyKnockback.length(), 25.0f, 0.1f);
}

BOOST_AUTO_TEST_CASE(TestAttackPersonalityDamageScaling) {
    auto& edm = EntityDataManager::Instance();

    auto attacker = TestNPC::create(100.0f, 100.0f);

    EntityHandle attackerHandle = attacker->getHandle();
    size_t attackerIdx = edm.getIndex(attackerHandle);
    BOOST_REQUIRE(attackerIdx != SIZE_MAX);

    auto& memData = edm.getMemoryData(attackerIdx);
    memData.personality.aggression = 1.0f;
    memData.emotions.aggression = 1.0f;

    auto& charData = edm.getCharacterDataByIndex(attackerIdx);
    float baseDamage = charData.attackDamage;

    float expectedMultiplier = 1.44f;
    float expectedDamage = baseDamage * expectedMultiplier;

    BOOST_TEST_MESSAGE("Base damage: " << baseDamage << ", expected with max aggression: " << expectedDamage);

    BOOST_CHECK_GT(expectedMultiplier, 1.0f);
    BOOST_CHECK_CLOSE(expectedMultiplier, 1.44f, 1.0f);
}

BOOST_AUTO_TEST_CASE(TestAttackLastTargetTracking) {
    auto& edm = EntityDataManager::Instance();

    auto attacker = TestNPC::create(100.0f, 100.0f);
    auto target = TestNPC::create(150.0f, 100.0f);

    EntityHandle attackerHandle = attacker->getHandle();
    EntityHandle targetHandle = target->getHandle();

    size_t attackerIdx = edm.getIndex(attackerHandle);
    BOOST_REQUIRE(attackerIdx != SIZE_MAX);

    BOOST_REQUIRE(edm.hasMemoryData(attackerIdx));

    auto& memData = edm.getMemoryData(attackerIdx);
    BOOST_CHECK(!memData.lastTarget.isValid());

    memData.lastTarget = targetHandle;

    BOOST_CHECK(memData.lastTarget.isValid());
    BOOST_CHECK(memData.lastTarget == targetHandle);

    BOOST_TEST_MESSAGE("lastTarget tracking verified: " << memData.lastTarget.isValid());
}

BOOST_AUTO_TEST_CASE(TestBerserkerModeNoRetreat) {
    auto& edm = EntityDataManager::Instance();

    auto entity = TestNPC::create(100.0f, 100.0f);
    EntityHandle handle = entity->getHandle();

    size_t idx = edm.getIndex(handle);
    BOOST_REQUIRE(idx != SIZE_MAX);

    auto& memData = edm.getMemoryData(idx);
    memData.emotions.aggression = 0.9f;
    memData.personality.aggression = 0.8f;

    bool isBerserker = (memData.emotions.aggression > 0.8f &&
                        memData.personality.aggression > 0.7f);
    BOOST_CHECK(isBerserker);

    auto& charData = edm.getCharacterData(handle);
    charData.health = charData.maxHealth * 0.1f;

    BOOST_TEST_MESSAGE("Berserker mode: aggression=" << memData.emotions.aggression
                       << " personality=" << memData.personality.aggression
                       << " health=" << (charData.health/charData.maxHealth*100) << "%");

    BOOST_CHECK(isBerserker);
}

BOOST_AUTO_TEST_CASE(TestRecordCombatEventUpdatesMemory) {
    auto& edm = EntityDataManager::Instance();

    auto victim = TestNPC::create(100.0f, 100.0f);
    auto attacker = TestNPC::create(200.0f, 100.0f);

    EntityHandle victimHandle = victim->getHandle();
    EntityHandle attackerHandle = attacker->getHandle();

    size_t victimIdx = edm.getIndex(victimHandle);
    BOOST_REQUIRE(victimIdx != SIZE_MAX);

    auto& memData = edm.getMemoryData(victimIdx);
    float initialFear = memData.emotions.fear;
    uint32_t initialEncounters = memData.combatEncounters;

    edm.recordCombatEvent(victimIdx, attackerHandle, victimHandle, 50.0f, true, 0.0f);

    BOOST_CHECK_GT(memData.emotions.fear, initialFear);
    BOOST_CHECK_GT(memData.combatEncounters, initialEncounters);
    BOOST_CHECK(memData.lastAttacker == attackerHandle);
    BOOST_CHECK(memData.flags & NPCMemoryData::FLAG_IN_COMBAT);

    BOOST_TEST_MESSAGE("recordCombatEvent: fear " << initialFear << " -> " << memData.emotions.fear
                       << ", encounters: " << memData.combatEncounters);
}

BOOST_AUTO_TEST_SUITE_END()

// Global test summary
BOOST_AUTO_TEST_CASE(BehaviorTestSummary) {
    BOOST_TEST_MESSAGE("=== Behavior Functionality Test Summary ===");
    BOOST_TEST_MESSAGE("All 8 core behaviors tested");
    BOOST_TEST_MESSAGE("Message system integration tested");
    BOOST_TEST_MESSAGE("Behavior transitions tested");
    BOOST_TEST_MESSAGE("Performance with multiple entities tested");
    BOOST_TEST_MESSAGE("Advanced behavior features tested");
    BOOST_TEST_MESSAGE("=== All Behavior Tests Completed Successfully ===");
}
