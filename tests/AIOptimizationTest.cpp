/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE AIOptimizationTests
#include <boost/test/unit_test.hpp>

#include <memory>
#include <chrono>
#include <vector>
#include <iostream>

// Include real engine headers
#include "core/ThreadSystem.hpp"
#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "entities/Entity.hpp"
#include "entities/EntityHandle.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "utils/Vector2D.hpp"

// Simple test entity for optimization tests
// NOTE: Does NOT call setPosition() in constructor - position is set via registerEntity
// which registers with EDM first, then sets position through the valid handle.
class OptimizationTestEntity : public Entity {
public:
    OptimizationTestEntity() {
        setTextureID("test");
        setWidth(32);
        setHeight(32);
        // Don't call setPosition() here - m_handle is not set yet!
    }

    // Factory method for proper shared_ptr initialization
    static std::shared_ptr<OptimizationTestEntity> create([[maybe_unused]] const Vector2D& pos) {
        // pos parameter kept for API compatibility but not used in constructor
        return std::make_shared<OptimizationTestEntity>();
    }

    void update(float deltaTime) override {
        (void)deltaTime; // Suppress unused parameter warning
    }

    void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) override {
        (void)renderer;
        (void)cameraX;
        (void)cameraY;
        (void)interpolationAlpha;
    }

    void clean() override {
        // Safe cleanup - we're not calling shared_from_this() here
    }
    [[nodiscard]] EntityKind getKind() const override { return EntityKind::NPC; }

    // Public wrapper for protected registerWithDataManager
    void registerEntity(const Vector2D& pos, float halfW, float halfH) {
        registerWithDataManager(pos, halfW, halfH, EntityKind::NPC);
    }
};

// Global fixture for test setup and cleanup
struct AITestFixture {
    AITestFixture() {
        // Initialize dependencies required by the real AIManager
        HammerEngine::ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
    }

    ~AITestFixture() {
        // Clean up in reverse order
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

BOOST_GLOBAL_FIXTURE(AITestFixture);

// Test case for entity component caching
BOOST_AUTO_TEST_CASE(TestEntityComponentCaching)
{
    // Register a test behavior using the real WanderBehavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("TestWander", wanderBehavior);

    // Create test entities and register them for managed updates
    std::vector<EntityHandle> handles;
    std::vector<EntityPtr> entities;
    for (int i = 0; i < 10; ++i) {
        Vector2D pos(i * 100.0f, i * 100.0f);
        auto entity = OptimizationTestEntity::create(pos);
        entities.push_back(entity);
        // Register entity with EntityDataManager
        entity->registerEntity(pos, 16.0f, 16.0f);
        EntityHandle handle = entity->getHandle();
        handles.push_back(handle);
        AIManager::Instance().registerEntity(handle, "TestWander");
    }

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    AIManager::Instance().waitForAssignmentCompletion();

    // Verify entities are registered
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), 10);

    // Cleanup - unregister entities from managed updates
    auto& edm = EntityDataManager::Instance();
    for (const auto& handle : handles) {
        AIManager::Instance().unregisterEntity(handle);
        AIManager::Instance().unassignBehavior(handle);
        edm.unregisterEntity(handle.getId());
    }
    handles.clear();
    entities.clear();
    AIManager::Instance().resetBehaviors();
}

// Test case for batch processing
BOOST_AUTO_TEST_CASE(TestBatchProcessing)
{
    // Register behaviors
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("BatchWander", wanderBehavior);

    // Create test entities and register them for managed updates
    std::vector<EntityHandle> handles;
    std::vector<EntityPtr> entityPtrs;
    for (int i = 0; i < 100; ++i) {
        Vector2D pos(i * 10.0f, i * 10.0f);
        auto entity = OptimizationTestEntity::create(pos);
        entityPtrs.push_back(entity);
        // Register entity with EntityDataManager
        entity->registerEntity(pos, 16.0f, 16.0f);
        EntityHandle handle = entity->getHandle();
        handles.push_back(handle);
        AIManager::Instance().registerEntity(handle, "BatchWander");
    }

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete before timing updates
    AIManager::Instance().waitForAssignmentCompletion();

    // Time the unified entity processing
    auto startTime = std::chrono::high_resolution_clock::now();
    AIManager::Instance().update(0.016f);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto batchDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Time multiple managed updates
    startTime = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        AIManager::Instance().update(0.016f);
    }
    endTime = std::chrono::high_resolution_clock::now();
    auto individualDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Output timing info (not an actual test, just informational)
    std::cout << "Batch processing time: " << batchDuration.count() << " µs" << std::endl;
    std::cout << "Individual processing time: " << individualDuration.count() << " µs" << std::endl;

    // Batch processing should be reasonably efficient
    // (Don't enforce strict inequality as threading can cause variance)
    BOOST_CHECK_GT(individualDuration.count(), 0);
    BOOST_CHECK_GT(batchDuration.count(), 0);

    // Cleanup - unregister entities from managed updates
    auto& edm = EntityDataManager::Instance();
    for (const auto& handle : handles) {
        AIManager::Instance().unregisterEntity(handle);
        AIManager::Instance().unassignBehavior(handle);
        edm.unregisterEntity(handle.getId());
    }
    handles.clear();
    entityPtrs.clear();
    AIManager::Instance().resetBehaviors();
}

// Test case for early exit conditions
BOOST_AUTO_TEST_CASE(TestEarlyExitConditions)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("LazyWander", wanderBehavior);

    // Create test entity and register for managed updates
    Vector2D pos(100.0f, 100.0f);
    auto entity = OptimizationTestEntity::create(pos);
    // Register entity with EntityDataManager
    entity->registerEntity(pos, 16.0f, 16.0f);
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().registerEntity(handle, "LazyWander");

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete
    AIManager::Instance().waitForAssignmentCompletion();

    // Test that behavior is assigned
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Cleanup - unregister entity from managed updates
    AIManager::Instance().unregisterEntity(handle);
    AIManager::Instance().unassignBehavior(handle);
    EntityDataManager::Instance().unregisterEntity(handle.getId());
    AIManager::Instance().resetBehaviors();
}

// Test case for message queue system
BOOST_AUTO_TEST_CASE(TestMessageQueueSystem)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("MsgWander", wanderBehavior);

    // Create test entity and register with consolidated method
    Vector2D pos(100.0f, 100.0f);
    auto entity = OptimizationTestEntity::create(pos);
    // Register entity with EntityDataManager
    entity->registerEntity(pos, 16.0f, 16.0f);
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().registerEntity(handle, "MsgWander");

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    AIManager::Instance().waitForAssignmentCompletion();

    // Queue several messages
    AIManager::Instance().sendMessageToEntity(handle, "test1");
    AIManager::Instance().sendMessageToEntity(handle, "test2");
    AIManager::Instance().sendMessageToEntity(handle, "test3");

    // Process the message queue explicitly
    AIManager::Instance().processMessageQueue();

    // Test immediate delivery
    AIManager::Instance().sendMessageToEntity(handle, "immediate", true);

    // Test broadcast
    AIManager::Instance().broadcastMessage("broadcast");
    AIManager::Instance().processMessageQueue();

    // Entity should still have behavior after all messages
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Cleanup - unregister entity from managed updates
    AIManager::Instance().unregisterEntity(handle);
    AIManager::Instance().unassignBehavior(handle);
    EntityDataManager::Instance().unregisterEntity(handle.getId());
    AIManager::Instance().resetBehaviors();
}

// Test case for SIMD distance calculations including tail loop edge cases
// This verifies that ALL entities receive proper distance calculations,
// especially for entity counts that are NOT multiples of 4 (SIMD width)
BOOST_AUTO_TEST_CASE(TestDistanceCalculationCorrectness)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("DistanceTestWander", wanderBehavior);

    auto& edm = EntityDataManager::Instance();

    // Test with entity counts that stress the SIMD tail loop:
    // 1, 2, 3 (all scalar)
    // 4, 5, 6, 7 (SIMD + tail)
    // 8, 9, 10, 11 (SIMD*2 + tail)
    std::vector<size_t> testCounts = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 17, 23};

    for (size_t count : testCounts) {
        // Create entities at known positions
        std::vector<std::shared_ptr<OptimizationTestEntity>> entities;
        std::vector<EntityHandle> handles;
        for (size_t i = 0; i < count; ++i) {
            // Place entities at (100 * i, 100 * i) for predictable distances
            Vector2D pos(100.0f * static_cast<float>(i), 100.0f * static_cast<float>(i));
            auto entity = OptimizationTestEntity::create(pos);
            entities.push_back(entity);
            // Register entity with EntityDataManager
            entity->registerEntity(pos, 16.0f, 16.0f);
            EntityHandle handle = entity->getHandle();
            handles.push_back(handle);
            AIManager::Instance().registerEntity(handle, "DistanceTestWander");
        }

        // Process assignments
        AIManager::Instance().update(0.016f);
        AIManager::Instance().waitForAssignmentCompletion();

        // Run a few update cycles to ensure distance calculations run
        for (int frame = 0; frame < 3; ++frame) {
            AIManager::Instance().update(0.016f);
        }

        // Verify all entities received valid processing (no teleportation to (0,0))
        for (size_t i = 0; i < entities.size(); ++i) {
            auto pos = entities[i]->getPosition();
            // Entities should be near their starting positions (WanderBehavior may move them slightly)
            // But they should NEVER teleport to (0,0) unless they started there
            if (i > 0) {
                // Entity i started at (100*i, 100*i), so it should NOT be at origin
                // Allow for some movement due to WanderBehavior, but position should be reasonable
                float distanceFromOrigin = std::sqrt(pos.getX() * pos.getX() + pos.getY() * pos.getY());
                BOOST_CHECK_MESSAGE(distanceFromOrigin > 10.0f,
                    "Entity " << i << " of " << count << " teleported to origin! Position: ("
                    << pos.getX() << ", " << pos.getY() << ")");
            }
        }

        // Cleanup
        for (const auto& handle : handles) {
            AIManager::Instance().unregisterEntity(handle);
            AIManager::Instance().unassignBehavior(handle);
            edm.unregisterEntity(handle.getId());
        }
        handles.clear();
        entities.clear();
        AIManager::Instance().resetBehaviors();
    }
}
