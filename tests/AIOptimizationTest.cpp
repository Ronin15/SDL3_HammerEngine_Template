/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>

#include "managers/AIManager.hpp"
// Instead of including real behaviors, we'll use mock behaviors for testing
#include "ai/AIBehavior.hpp"
#include "entities/Entity.hpp"
#include "utils/Vector2D.hpp"
#include <memory>
#include <chrono>
#include <vector>

// Mock WanderBehavior for testing
class MockWanderBehavior : public AIBehavior {
public:
    MockWanderBehavior(float speed = 1.5f, float changeDirectionInterval = 2000.0f, float areaRadius = 300.0f)
        : m_speed(speed), m_changeInterval(changeDirectionInterval), m_radius(areaRadius) {}

    void update(Entity* entity) override {
        // Just a simple mock implementation
        if (entity) {
            Vector2D pos = entity->getPosition();
            pos.setX(pos.getX() + 1.0f);
            entity->setPosition(pos);
        }
    }

    void init(Entity* entity) override {
        // Do nothing in mock
    }

    void clean(Entity* entity) override {
        // Do nothing in mock
    }

    std::string getName() const override {
        return "MockWander";
    }

    // Add public method to manipulate update frequency for testing
    void setUpdateFrequency(int freq) {
        m_updateFrequency = freq;
    }

private:
    float m_speed;
    float m_changeInterval;
    float m_radius;
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

TEST_CASE("AIManager Optimization Tests", "[ai][optimization]") {
    // Initialize AIManager
    AIManager::Instance().init();

    SECTION("Entity Component Caching") {
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
        REQUIRE(AIManager::Instance().getManagedEntityCount() == 10);

        // Cleanup
        entities.clear();
        AIManager::Instance().resetBehaviors();
    }

    SECTION("Batch Processing") {
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
        INFO("Batch processing time: " << batchDuration.count() << " µs");
        INFO("Individual processing time: " << individualDuration.count() << " µs");

        // Cleanup
        entities.clear();
        AIManager::Instance().resetBehaviors();
    }

    SECTION("Early Exit Conditions") {
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
        REQUIRE(AIManager::Instance().entityHasBehavior(entity.get()));

        // Cleanup
        AIManager::Instance().resetBehaviors();
    }

    SECTION("Message Queue System") {
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
        REQUIRE(AIManager::Instance().entityHasBehavior(entity.get()));

        // Test immediate delivery
        AIManager::Instance().sendMessageToEntity(entity.get(), "immediate", true);

        // Test broadcast
        AIManager::Instance().broadcastMessage("broadcast");
        AIManager::Instance().processMessageQueue();

        // Cleanup
        AIManager::Instance().resetBehaviors();
    }

    // Clean up AIManager
    AIManager::Instance().clean();
}
