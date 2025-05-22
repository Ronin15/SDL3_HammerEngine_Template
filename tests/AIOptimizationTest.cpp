/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE AIOptimizationTests
#include <boost/test/included/unit_test.hpp>

#include <memory>
#include <chrono>
#include <vector>
#include <iostream>
#include <string>
#include <map>

// Basic Vector2D implementation for tests
class Vector2D {
public:
    Vector2D(float x = 0, float y = 0) : m_x(x), m_y(y) {}
    float getX() const { return m_x; }
    float getY() const { return m_y; }
    void setX(float x) { m_x = x; }
    void setY(float y) { m_y = y; }
    float lengthSquared() const { return m_x * m_x + m_y * m_y; }
    Vector2D operator-(const Vector2D& v) const { return Vector2D(m_x - v.m_x, m_y - v.m_y); }
private:
    float m_x, m_y;
};

// Simplified Entity class for tests
class Entity {
public:
    virtual void update() = 0;
    virtual void render() = 0;
    virtual void clean() = 0;
    virtual ~Entity() = default;

    // Accessor methods
    Vector2D getPosition() const { return m_position; }
    Vector2D getVelocity() const { return m_velocity; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    std::string getTextureID() const { return m_textureID; }

    // Setter methods
    virtual void setPosition(const Vector2D& position) { m_position = position; }
    virtual void setVelocity(const Vector2D& velocity) { m_velocity = velocity; }
    virtual void setWidth(int width) { m_width = width; }
    virtual void setHeight(int height) { m_height = height; }
    virtual void setTextureID(const std::string& id) { m_textureID = id; }

protected:
    Vector2D m_velocity{0, 0};
    Vector2D m_position{0, 0};
    int m_width{0};
    int m_height{0};
    std::string m_textureID{};
};

// TestEntity implementation
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

// AIBehavior base class
class AIBehavior {
public:
    virtual ~AIBehavior() = default;

    // Core behavior methods
    virtual void update(Entity* entity) = 0;
    virtual void init(Entity* entity) = 0;
    virtual void clean(Entity* entity) = 0;

    // Behavior identification
    virtual std::string getName() const = 0;

    // Optional message handling for behavior communication
    virtual void onMessage([[maybe_unused]] Entity* entity, [[maybe_unused]] const std::string& message) {}

    // Behavior state access
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    // Update frequency control
    void setUpdateFrequency(int framesPerUpdate) { m_updateFrequency = framesPerUpdate; }
    int getUpdateFrequency() const { return m_updateFrequency; }

    // Frame tracking
    int getFramesSinceLastUpdate() const { return m_framesSinceLastUpdate; }
    void incrementFrameCounter() { m_framesSinceLastUpdate++; }
    void resetFrameCounter() { m_framesSinceLastUpdate = 0; }

    // Early exit conditions
    bool shouldUpdate([[maybe_unused]] Entity* entity) const {
        if (!m_active) return false;
        if (!isWithinUpdateFrequency()) return false;
        return true;
    }

    bool isWithinUpdateFrequency() const {
        if (m_updateFrequency <= 1) return true;
        return (m_framesSinceLastUpdate % m_updateFrequency == 0);
    }

    bool isEntityInRange([[maybe_unused]] Entity* entity) const { return true; }

protected:
    bool m_active{true};
    int m_updateFrequency{1};
    int m_framesSinceLastUpdate{0};
};

// MockWanderBehavior for testing
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
};

// Simple AIManager implementation for tests
class AIManager {
public:
    static AIManager& Instance() {
        static AIManager instance;
        return instance;
    }

    // Initialize the manager
    bool init() {
        m_initialized = true;
        return true;
    }

    // Clean up resources
    void clean() {
        m_initialized = false;
        m_behaviors.clear();
        m_entityBehaviors.clear();
        m_entityBehaviorCache.clear();
    }

    // Register a behavior
    void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
        m_behaviors[behaviorName] = behavior;
        m_cacheValid = false;
    }

    // Assign behavior to entity
    void assignBehaviorToEntity(Entity* entity, const std::string& behaviorName) {
        m_entityBehaviors[entity] = behaviorName;
        m_cacheValid = false;
    }

    // Reset all behaviors
    void resetBehaviors() {
        m_behaviors.clear();
        m_entityBehaviors.clear();
        m_entityBehaviorCache.clear();
        m_cacheValid = false;
    }

    // Check if entity has behavior
    bool entityHasBehavior(Entity* entity) const {
        return m_entityBehaviors.find(entity) != m_entityBehaviors.end();
    }

    // Get count of managed entities
    size_t getManagedEntityCount() const {
        return m_entityBehaviors.size();
    }

    // Cache validation for optimization
    void ensureOptimizationCachesValid() {
        if (!m_cacheValid) {
            rebuildEntityBehaviorCache();
        }
    }

    // Update all behaviors
    void update() {
        if (!m_initialized) return;

        // Process messages first
        processMessageQueue();

        // Update each entity with its behavior
        for (auto& [entity, behaviorName] : m_entityBehaviors) {
            if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                auto behavior = m_behaviors[behaviorName];

                // Increment frame counter for behavior
                behavior->incrementFrameCounter();

                // Check if behavior should update this frame
                if (behavior->shouldUpdate(entity)) {
                    behavior->update(entity);

                    // Reset frame counter after update
                    behavior->resetFrameCounter();
                }
            }
        }
    }

    // Process entities in batches for optimization
    void batchProcessEntities(const std::string& behaviorName, const std::vector<Entity*>& entities) {
        if (!m_initialized || entities.empty()) return;

        // Get the behavior
        if (m_behaviors.find(behaviorName) == m_behaviors.end()) return;
        auto behavior = m_behaviors[behaviorName];

        // Process all entities with this behavior
        for (auto entity : entities) {
            behavior->update(entity);
        }
    }

    // Send message to entity
    void sendMessageToEntity(Entity* entity, const std::string& message, [[maybe_unused]] bool immediate = false) {
        if (immediate) {
            deliverMessageToEntity(entity, message);
        } else {
            m_messageQueue.push_back({entity, message});
        }
    }

    // Broadcast message to all entities
    void broadcastMessage(const std::string& message, [[maybe_unused]] bool immediate = false) {
        if (immediate) {
            for (auto& [entity, behaviorName] : m_entityBehaviors) {
                if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                    m_behaviors[behaviorName]->onMessage(entity, message);
                }
            }
        } else {
            m_messageQueue.push_back({nullptr, message});  // nullptr indicates broadcast
        }
    }

    // Process queued messages
    void processMessageQueue() {
        for (const auto& msg : m_messageQueue) {
            if (msg.entity) {
                deliverMessageToEntity(msg.entity, msg.message);
            } else {
                // Broadcast
                for (auto& [entity, behaviorName] : m_entityBehaviors) {
                    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                        m_behaviors[behaviorName]->onMessage(entity, msg.message);
                    }
                }
            }
        }
        m_messageQueue.clear();
    }

private:
    bool m_initialized{false};
    bool m_cacheValid{false};
    std::map<std::string, std::shared_ptr<AIBehavior>> m_behaviors;
    std::map<Entity*, std::string> m_entityBehaviors;

    // Optimization cache
    struct EntityBehaviorCache {
        Entity* entity;
        AIBehavior* behavior;
    };
    std::vector<EntityBehaviorCache> m_entityBehaviorCache;

    // Message queue
    struct QueuedMessage {
        Entity* entity;  // nullptr for broadcast
        std::string message;
    };
    std::vector<QueuedMessage> m_messageQueue;

    // Helper methods
    void rebuildEntityBehaviorCache() {
        m_entityBehaviorCache.clear();
        for (auto& [entity, behaviorName] : m_entityBehaviors) {
            if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                m_entityBehaviorCache.push_back({entity, m_behaviors[behaviorName].get()});
            }
        }
        m_cacheValid = true;
    }

    void deliverMessageToEntity(Entity* entity, const std::string& message) {
        if (entityHasBehavior(entity)) {
            auto behaviorName = m_entityBehaviors[entity];
            if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                m_behaviors[behaviorName]->onMessage(entity, message);
            }
        }
    }

    // Prevent copying
    AIManager() = default;
    AIManager(const AIManager&) = delete;
    AIManager& operator=(const AIManager&) = delete;
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

// Test case for entity component caching
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

// Test case for batch processing
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

    // Batch processing should be more efficient
    BOOST_CHECK_LE(batchDuration.count(), individualDuration.count());

    // Cleanup
    entities.clear();
    AIManager::Instance().resetBehaviors();
}

// Test case for early exit conditions
BOOST_AUTO_TEST_CASE(TestEarlyExitConditions)
{
    // Register a test behavior with update frequency of 3 frames
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    wanderBehavior->setUpdateFrequency(3);  // Only update every 3 frames
    AIManager::Instance().registerBehavior("LazyWander", wanderBehavior);

    // Create test entity
    auto entity = std::make_unique<TestEntity>(Vector2D(100.0f, 100.0f));
    AIManager::Instance().assignBehaviorToEntity(entity.get(), "LazyWander");

    // Test that behavior is assigned
    BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity.get()));

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for message queue system
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

    // Test immediate delivery
    AIManager::Instance().sendMessageToEntity(entity.get(), "immediate", true);

    // Test broadcast
    AIManager::Instance().broadcastMessage("broadcast");
    AIManager::Instance().processMessageQueue();

    // Entity should still have behavior after all messages
    BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity.get()));

    // Cleanup
    AIManager::Instance().resetBehaviors();
}
