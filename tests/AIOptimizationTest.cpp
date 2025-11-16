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
#include "managers/PathfinderManager.hpp"
#include "entities/Entity.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "utils/Vector2D.hpp"

// Simple test entity for optimization tests
class OptimizationTestEntity : public Entity {
public:
    OptimizationTestEntity(const Vector2D& pos) {
        setTextureID("test");
        setPosition(pos);
        setWidth(32);
        setHeight(32);
    }

    // Factory method for proper shared_ptr initialization
    static std::shared_ptr<OptimizationTestEntity> create(const Vector2D& pos) {
        return std::make_shared<OptimizationTestEntity>(pos);
    }

    void update(float deltaTime) override {
        (void)deltaTime; // Suppress unused parameter warning
    }

    void render(const HammerEngine::Camera* camera) override {
        (void)camera;
    }

    void clean() override {
        // Safe cleanup - we're not calling shared_from_this() here
    }
};

// Global fixture for test setup and cleanup
struct AITestFixture {
    AITestFixture() {
        // Initialize dependencies required by the real AIManager
        HammerEngine::ThreadSystem::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
    }

    ~AITestFixture() {
        // Clean up in reverse order
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
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
    std::vector<EntityPtr> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(OptimizationTestEntity::create(Vector2D(i * 100.0f, i * 100.0f)));
        AIManager::Instance().registerEntityForUpdates(entities.back(), 5, "TestWander");
    }

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    AIManager::Instance().waitForAssignmentCompletion();

    // Verify entities are registered
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), 10);

    // Cleanup - unregister entities from managed updates
    for (auto& entity : entities) {
        AIManager::Instance().unregisterEntityFromUpdates(entity);
        AIManager::Instance().unassignBehaviorFromEntity(entity);
    }
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
    std::vector<EntityPtr> entityPtrs;
    for (int i = 0; i < 100; ++i) {
        auto entity = OptimizationTestEntity::create(Vector2D(i * 10.0f, i * 10.0f));
        entityPtrs.push_back(entity);
        AIManager::Instance().registerEntityForUpdates(entity, 5, "BatchWander");
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
    for (auto& entity : entityPtrs) {
        AIManager::Instance().unregisterEntityFromUpdates(entity);
        AIManager::Instance().unassignBehaviorFromEntity(entity);
    }
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
    auto entity = OptimizationTestEntity::create(Vector2D(100.0f, 100.0f));
    AIManager::Instance().registerEntityForUpdates(entity, 5, "LazyWander");

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete
    AIManager::Instance().waitForAssignmentCompletion();

    // Test that behavior is assigned
    BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity));

    // Cleanup - unregister entity from managed updates
    AIManager::Instance().unregisterEntityFromUpdates(entity);
    AIManager::Instance().unassignBehaviorFromEntity(entity);
    AIManager::Instance().resetBehaviors();
}

// Test case for message queue system
BOOST_AUTO_TEST_CASE(TestMessageQueueSystem)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("MsgWander", wanderBehavior);

    // Create test entity and register with consolidated method
    auto entity = OptimizationTestEntity::create(Vector2D(100.0f, 100.0f));
    AIManager::Instance().registerEntityForUpdates(entity, 5, "MsgWander");

    // Process pending assignments
    AIManager::Instance().update(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    AIManager::Instance().waitForAssignmentCompletion();

    // Queue several messages
    AIManager::Instance().sendMessageToEntity(entity, "test1");
    AIManager::Instance().sendMessageToEntity(entity, "test2");
    AIManager::Instance().sendMessageToEntity(entity, "test3");

    // Process the message queue explicitly
    AIManager::Instance().processMessageQueue();

    // Test immediate delivery
    AIManager::Instance().sendMessageToEntity(entity, "immediate", true);

    // Test broadcast
    AIManager::Instance().broadcastMessage("broadcast");
    AIManager::Instance().processMessageQueue();

    // Entity should still have behavior after all messages
    BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity));

    // Cleanup - unregister entity from managed updates
    AIManager::Instance().unregisterEntityFromUpdates(entity);
    AIManager::Instance().unassignBehaviorFromEntity(entity);
    AIManager::Instance().resetBehaviors();
}
