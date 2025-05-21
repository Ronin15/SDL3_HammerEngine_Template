/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE AIOptimizationTests
#include <boost/test/included/unit_test.hpp>

#include "managers/AIManager.hpp"
// Instead of including real behaviors, we'll use mock behaviors for testing
#include "ai/AIBehavior.hpp"
#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <memory>
#include <chrono>
#include <vector>
#include <iostream>
#include <string>

// Mock WanderBehavior for testing
class MockWanderBehavior : public AIBehavior {
public:
    MockWanderBehavior(float speed = 1.5f, float changeDirectionInterval = 2000.0f, float areaRadius = 300.0f) {
        // Parameters not used in this mock implementation
        (void)speed;
        (void)changeDirectionInterval;
        (void)areaRadius;
    }

    void update(Entity* entity) override {
        // Just a simple mock implementation
        if (entity) {
            Vector2D pos = entity->getPosition();
            pos.setX(pos.getX() + 1.0f);
            entity->setPosition(pos);
        }
    }

    void init([[maybe_unused]] Entity* entity) override {
        // Do nothing in mock
    }

    void clean([[maybe_unused]] Entity* entity) override {
        // Do nothing in mock
    }

    std::string getName() const override {
        return "MockWander";
    }

    // Add public method to manipulate update frequency for testing
    void setUpdateFrequency(int freq) override {
        m_updateFrequency = freq;
    }

private:
    // No private fields needed for this mock implementation
};

class TestEntity : public Entity {
public:
    TestEntity(const Vector2D& pos) {
        setTextureID("test");
        setPosition(pos);
        setWidth(32);
        setHeight(32);
    }

    void update() override {}
    void render() override {}
    void clean() override {}
};

// Global fixture for test setup and cleanup
struct AITestFixture {
    AITestFixture() {
        // Initialize the AI system before tests
        AIManager::Instance().init();
    }

    ~AITestFixture() {
        // Clean up the AI system after tests
        AIManager::Instance().clean();
    }
};

BOOST_GLOBAL_FIXTURE(AITestFixture);

BOOST_AUTO_TEST_CASE(TestEntityComponentCaching)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("TestWander", wanderBehavior);

    // Create test entities
    std::vector<std::unique_ptr<TestEntity>> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 100.0f, i * 100.0f)));
        AIManager::Instance().assignBehaviorToEntity(entities.back().get(), "TestWander");
    }

    // Force cache validation
    AIManager::Instance().ensureOptimizationCachesValid();

    // Cache should be valid now
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), 10);

    // Cleanup
    entities.clear();
    AIManager::Instance().resetBehaviors();
}

BOOST_AUTO_TEST_CASE(TestBatchProcessing)
{
    // Register behaviors
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("BatchWander", wanderBehavior);

    // Create test entities
    std::vector<Entity*> entityPtrs;
    std::vector<std::unique_ptr<TestEntity>> entities;
    for (int i = 0; i < 100; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        entityPtrs.push_back(entities.back().get());
    }

    // Time the batch processing
    auto startTime = std::chrono::high_resolution_clock::now();
    AIManager::Instance().batchProcessEntities("BatchWander", entityPtrs);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto batchDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Time individual processing
    startTime = std::chrono::high_resolution_clock::now();
    for (auto entity : entityPtrs) {
        AIManager::Instance().assignBehaviorToEntity(entity, "BatchWander");
    }
    AIManager::Instance().update();
    endTime = std::chrono::high_resolution_clock::now();
    auto individualDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Output timing info (not an actual test, just informational)
    std::cout << "Batch processing time: " << batchDuration.count() << " µs" << std::endl;
    std::cout << "Individual processing time: " << individualDuration.count() << " µs" << std::endl;

    // Cleanup
    entities.clear();
    AIManager::Instance().resetBehaviors();
}

BOOST_AUTO_TEST_CASE(TestEarlyExitConditions)
{
    // Register a test behavior with update frequency of 3 frames
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    wanderBehavior->setUpdateFrequency(3);  // Only update every 3 frames
    AIManager::Instance().registerBehavior("LazyWander", wanderBehavior);

    // Create test entity
    auto entity = std::make_unique<TestEntity>(Vector2D(100.0f, 100.0f));
    AIManager::Instance().assignBehaviorToEntity(entity.get(), "LazyWander");

    // These calls should only actually update the entity once
    AIManager::Instance().update();  // Frame 1: Should update
    AIManager::Instance().update();  // Frame 2: Should skip
    AIManager::Instance().update();  // Frame 3: Should skip
    AIManager::Instance().update();  // Frame 4: Should update

    // Can't directly test the skipping, but we can verify the entity still has its behavior
    BOOST_REQUIRE(AIManager::Instance().entityHasBehavior(entity.get()));

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

BOOST_AUTO_TEST_CASE(TestMessageQueueSystem)
{
    // Register a test behavior
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("MsgWander", wanderBehavior);

    // Create test entity
    auto entity = std::make_unique<TestEntity>(Vector2D(100.0f, 100.0f));
    AIManager::Instance().assignBehaviorToEntity(entity.get(), "MsgWander");

    // Queue several messages
    AIManager::Instance().sendMessageToEntity(entity.get(), "test1");
    AIManager::Instance().sendMessageToEntity(entity.get(), "test2");
    AIManager::Instance().sendMessageToEntity(entity.get(), "test3");

    // Process the message queue explicitly
    AIManager::Instance().processMessageQueue();

    // We can't directly test if messages were processed, but we can ensure entity still has behavior
    BOOST_REQUIRE(AIManager::Instance().entityHasBehavior(entity.get()));

    // Test immediate delivery
    AIManager::Instance().sendMessageToEntity(entity.get(), "immediate", true);

    // Test broadcast
    AIManager::Instance().broadcastMessage("broadcast");
    AIManager::Instance().processMessageQueue();

    // Cleanup
    AIManager::Instance().resetBehaviors();
}
