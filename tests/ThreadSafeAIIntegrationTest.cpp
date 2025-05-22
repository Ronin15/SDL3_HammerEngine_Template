/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE ThreadSafeAIIntegrationTest
#include <boost/test/included/unit_test.hpp>

// iostream removed - not used directly
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>
#include <memory>

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"
// GameEngine.hpp removed - not used directly

// Simple test entity
class IntegrationTestEntity : public Entity {
public:
    IntegrationTestEntity(int id, const Vector2D& pos) : m_id(id) {
        setPosition(pos);
        setTextureID("test_texture");
        setWidth(32);
        setHeight(32);
    }
    
    void update() override { m_updateCount++; }
    void render() override {}
    void clean() override {}
    
    int getId() const { return m_id; }
    int getUpdateCount() const { return m_updateCount; }
    
private:
    int m_id;
    std::atomic<int> m_updateCount{0};
};

// Test behavior
class IntegrationTestBehavior : public AIBehavior {
public:
    IntegrationTestBehavior(const std::string& name) : m_name(name) {}
    
    void update(Entity* entity) override {
        // Cast to our test entity type
        auto testEntity = static_cast<IntegrationTestEntity*>(entity);
        if (!testEntity) return;
        
        // Update the entity - simulate some work
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        testEntity->update();
        
        // Update our counter
        m_updateCount++;
        
        // Occasionally send a message to another random entity
        if (m_updateCount % 10 == 0 && entity) {
            AIManager::Instance().broadcastMessage("test_message");
        }
    }
    
    void init(Entity* /* entity */) override {
        m_initialized = true;
    }
    
    void clean(Entity* /* entity */) override {
        m_initialized = false;
    }
    
    std::string getName() const override {
        return m_name;
    }
    
    void onMessage(Entity* /* entity */, const std::string& /* message */) override {
        m_messageCount++;
    }
    
    int getUpdateCount() const { return m_updateCount; }
    int getMessageCount() const { return m_messageCount; }
    
private:
    std::string m_name;
    bool m_initialized{false};
    std::atomic<int> m_updateCount{0};
    std::atomic<int> m_messageCount{0};
};

// Fixture for test setup/teardown
struct AIIntegrationTestFixture {
    AIIntegrationTestFixture() {
        // Initialize thread system
        BOOST_REQUIRE(Forge::ThreadSystem::Instance().init());
        
        // Initialize AI manager
        BOOST_REQUIRE(AIManager::Instance().init());
        
        // Enable threading
        AIManager::Instance().configureThreading(true);
        
        // Create test behaviors
        for (int i = 0; i < NUM_BEHAVIORS; ++i) {
            behaviors.push_back(std::make_shared<IntegrationTestBehavior>("Behavior" + std::to_string(i)));
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behaviors.back());
        }
        
        // Create test entities
        for (int i = 0; i < NUM_ENTITIES; ++i) {
            entities.push_back(std::make_unique<IntegrationTestEntity>(
                i, Vector2D(i * 10.0f, i * 10.0f)));
            
            // Assign a random behavior to each entity
            int behaviorIdx = i % NUM_BEHAVIORS;
            AIManager::Instance().assignBehaviorToEntity(
                entities.back().get(), "Behavior" + std::to_string(behaviorIdx));
        }
    }
    
    ~AIIntegrationTestFixture() {
        // Clean up in reverse order
        entities.clear();
        behaviors.clear();
        AIManager::Instance().clean();
        Forge::ThreadSystem::Instance().clean();
    }
    
    // Test parameters
    static constexpr int NUM_BEHAVIORS = 5;
    static constexpr int NUM_ENTITIES = 100;
    static constexpr int NUM_UPDATES = 20;
    
    std::vector<std::shared_ptr<IntegrationTestBehavior>> behaviors;
    std::vector<std::unique_ptr<IntegrationTestEntity>> entities;
};

BOOST_FIXTURE_TEST_SUITE(AIIntegrationTests, AIIntegrationTestFixture)

// Test that updates work properly
BOOST_AUTO_TEST_CASE(TestConcurrentUpdates) {
    // Update the AI system multiple times
    for (int i = 0; i < NUM_UPDATES; ++i) {
        AIManager::Instance().update();
        
        // Let ThreadSystem process tasks
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    
    // Verify all entities were updated
    bool allUpdated = true;
    for (const auto& entity : entities) {
        if (entity->getUpdateCount() == 0) {
            allUpdated = false;
            break;
        }
    }
    
    BOOST_CHECK(allUpdated);
    
    // Verify all behaviors were executed
    for (const auto& behavior : behaviors) {
        BOOST_CHECK_GT(behavior->getUpdateCount(), 0);
    }
}

// Test concurrent assignment and update
BOOST_AUTO_TEST_CASE(TestConcurrentAssignmentAndUpdate) {
    std::vector<std::thread> threads;
    std::atomic<bool> stopFlag{false};
    
    // Thread 1: Continuously update AI
    threads.push_back(std::thread([&stopFlag]() {
        while (!stopFlag) {
            AIManager::Instance().update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }));
    
    // Thread 2: Continuously reassign behaviors
    threads.push_back(std::thread([&stopFlag, this]() {
        std::mt19937 rng(42); // Fixed seed for reproducibility
        while (!stopFlag) {
            // Pick random entity and behavior
            std::size_t entityIdx = rng() % entities.size();
            int behaviorIdx = rng() % NUM_BEHAVIORS;
            
            // Reassign behavior
            AIManager::Instance().assignBehaviorToEntity(
                entities[entityIdx].get(), 
                "Behavior" + std::to_string(behaviorIdx));
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }));
    
    // Thread 3: Send messages
    threads.push_back(std::thread([&stopFlag, this]() {
        std::mt19937 rng(43); // Different seed
        while (!stopFlag) {
            // Pick random entity
            std::size_t entityIdx = rng() % entities.size();
            
            // Send message
            AIManager::Instance().sendMessageToEntity(
                entities[entityIdx].get(), 
                "test_message_" + std::to_string(rng() % 100));
            
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }));
    
    // Let threads run for a while
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Signal threads to stop
    stopFlag = true;
    
    // Join threads
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Success criteria is simply not crashing or deadlocking
    BOOST_CHECK(true);
}

// Test message delivery
BOOST_AUTO_TEST_CASE(TestMessageDelivery) {
    // Send a bunch of broadcast messages
    for (int i = 0; i < 10; ++i) {
        AIManager::Instance().broadcastMessage("broadcast_" + std::to_string(i));
    }
    
    // Process messages
    AIManager::Instance().processMessageQueue();
    
    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Verify some messages were received
    bool someMessagesReceived = false;
    for (const auto& behavior : behaviors) {
        if (behavior->getMessageCount() > 0) {
            someMessagesReceived = true;
            break;
        }
    }
    
    BOOST_CHECK(someMessagesReceived);
}

// Test cache invalidation and rebuilding
BOOST_AUTO_TEST_CASE(TestCacheInvalidation) {
    // First update to build caches
    AIManager::Instance().update();
    
    // Remove all behaviors from entities
    for (const auto& entity : entities) {
        AIManager::Instance().unassignBehaviorFromEntity(entity.get());
    }
    
    // Verify entity count is zero
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), 0);
    
    // Reassign behaviors
    for (std::size_t i = 0; i < entities.size(); ++i) {
        int behaviorIdx = static_cast<int>(i) % NUM_BEHAVIORS;
        AIManager::Instance().assignBehaviorToEntity(
            entities[i].get(), "Behavior" + std::to_string(behaviorIdx));
    }
    
    // Verify entity count matches again
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), entities.size());
    
    // Update again to use rebuilt caches
    AIManager::Instance().update();
    
    // Success if we get here without crashes
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()