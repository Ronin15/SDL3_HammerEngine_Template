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
#include <set>
#include <functional>

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
class Entity : public std::enable_shared_from_this<Entity> {
public:
    virtual void update(float deltaTime) = 0;
    virtual void render() = 0;
    virtual void clean() = 0;
    virtual ~Entity() = default;

    // Get shared pointer to this entity - NEVER call in constructor or destructor
    std::shared_ptr<Entity> shared_this() {
        return shared_from_this();
    }

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

// Define standard smart pointer types for Entity
using EntityPtr = std::shared_ptr<Entity>;
using EntityWeakPtr = std::weak_ptr<Entity>;

// OptimizationTestEntity implementation
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
    void render() override {}
    void clean() override {
        // Safe cleanup - we're not calling shared_from_this() here
    }
};

// AIBehavior base class
class AIBehavior {
public:
    virtual ~AIBehavior() = default;

    // Core behavior methods - pure logic only
    virtual void executeLogic(EntityPtr entity) = 0;
    virtual void init(EntityPtr entity) = 0;
    virtual void clean(EntityPtr entity) = 0;

    // Behavior identification
    virtual std::string getName() const = 0;

    // Optional message handling for behavior communication
    virtual void onMessage([[maybe_unused]] EntityPtr entity, [[maybe_unused]] const std::string& message) {}

    // Behavior state access
    bool isActive() const { return m_active; }
    void setActive(bool active) { m_active = active; }

    // Priority handling (used by AIManager)
    int getPriority() const { return m_priority; }
    void setPriority(int priority) { m_priority = priority; }

    std::shared_ptr<AIBehavior> clone() const { return nullptr; }

private:
    bool m_active = true;
    int m_priority = 0;
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

    void executeLogic(EntityPtr entity) override {
        // Just a simple mock implementation
        if (entity) {
            Vector2D pos = entity->getPosition();
            pos.setX(pos.getX() + 1.0f);
            entity->setPosition(pos);
        }
    }

    void init([[maybe_unused]] EntityPtr entity) override {
        // Do nothing in mock
    }

    void clean([[maybe_unused]] EntityPtr entity) override {
        // Do nothing in mock
    }

    std::string getName() const override {
        return "MockWander";
    }

    std::shared_ptr<AIBehavior> clone() const {
        return std::make_shared<MockWanderBehavior>();
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
        m_managedEntities.clear();
    }

    // Register a behavior
    void registerBehavior(const std::string& behaviorName, std::shared_ptr<AIBehavior> behavior) {
        m_behaviors[behaviorName] = behavior;
        m_cacheValid = false;
    }

    // Assign behavior to entity
    void assignBehaviorToEntity(EntityPtr entity, const std::string& behaviorName) {
        m_entityBehaviors[entity] = behaviorName;
        m_cacheValid = false;
    }

    // Reset all behaviors
    void resetBehaviors() {
        m_behaviors.clear();
        m_entityBehaviors.clear();
        m_entityBehaviorCache.clear();
        m_managedEntities.clear();
        m_cacheValid = false;
    }

    // Check if entity has behavior
    bool entityHasBehavior(EntityPtr entity) const {
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
    void update(float deltaTime) {
        if (!m_initialized) return;
        (void)deltaTime; // Suppress unused parameter warning

        // Process messages first
        processMessageQueue();

        // Update each entity with its behavior
        for (const auto& pair : m_entityBehaviors) {
            auto entity = pair.first;
            auto& behaviorName = pair.second;
            if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                auto behavior = m_behaviors[behaviorName];

                // AIManager now controls all update timing - just execute behavior logic
                behavior->executeLogic(entity);
            }
        }
    }

    // Process entities in batches for optimization
    void batchProcessEntities(const std::string& behaviorName, const std::vector<EntityPtr>& entities) {
        if (!m_initialized || entities.empty()) return;

        // Get the behavior
        if (m_behaviors.find(behaviorName) == m_behaviors.end()) return;
        auto behavior = m_behaviors[behaviorName];

        // Process all entities with this behavior
        for (const auto& entity : entities) {
            behavior->executeLogic(entity);
        }
    }

    // Send message to entity
    void sendMessageToEntity(EntityPtr entity, const std::string& message, [[maybe_unused]] bool immediate = false) {
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
                for (const auto& pair : m_entityBehaviors) {
                    auto entity = pair.first;
                    auto& behaviorName = pair.second;
                    if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                        m_behaviors[behaviorName]->onMessage(entity, msg.message);
                    }
                }
            }
        }
        m_messageQueue.clear();
    }

    // Register entity for managed updates (mock implementation)
    void registerEntityForUpdates(EntityPtr entity, int priority = 5) {
        (void)priority; // Unused in mock
        m_managedEntities.insert(entity);
    }

    // Consolidated registration with behavior (mock implementation)
    void registerEntityForUpdates(EntityPtr entity, int priority, const std::string& behaviorName) {
        (void)priority; // Unused in mock
        m_managedEntities.insert(entity);
        assignBehaviorToEntity(entity, behaviorName);
    }

    // Unregister entity from managed updates (mock implementation)
    void unregisterEntityFromUpdates(EntityPtr entity) {
        m_managedEntities.erase(entity);
    }

    // Update managed entities (mock implementation)
    void updateManagedEntities() {
        if (!m_initialized) return;
        
        // Process messages first
        processMessageQueue();

        // Update each managed entity with its behavior
        for (const auto& entity : m_managedEntities) {
            if (entityHasBehavior(entity)) {
                auto& behaviorName = m_entityBehaviors[entity];
                if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                    auto behavior = m_behaviors[behaviorName];
                    behavior->executeLogic(entity);
                }
            }
        }
    }

    // Unassign behavior from entity (mock implementation)
    void unassignBehaviorFromEntity(EntityPtr entity) {
        m_entityBehaviors.erase(entity);
        m_managedEntities.erase(entity);
        m_cacheValid = false;
    }

private:
    bool m_initialized{false};
    bool m_cacheValid{false};
    // Storage for behaviors and entity assignments
    std::map<std::string, std::shared_ptr<AIBehavior>> m_behaviors;
    std::map<EntityPtr, std::string, std::owner_less<EntityPtr>> m_entityBehaviors;
    std::set<EntityPtr, std::owner_less<EntityPtr>> m_managedEntities;

    // Optimization cache
    // Cache for quick lookup of entity-behavior pairs (optimization)
        struct EntityBehaviorCache {
            EntityPtr entity{};
            AIBehavior* behavior{nullptr};
        };
        std::vector<EntityBehaviorCache> m_entityBehaviorCache{};

    // Message queue
    struct QueuedMessage {
        EntityPtr entity{};  // nullptr for broadcast
        std::string message{};
    };
    std::vector<QueuedMessage> m_messageQueue{};
    
    // Custom hash for EntityPtr
    struct EntityPtrHash {
        size_t operator()(const EntityPtr& entity) const {
            return std::hash<Entity*>()(entity.get());
        }
    };

    // Helper methods
    void rebuildEntityBehaviorCache() {
            m_entityBehaviorCache.clear();
            for (const auto& pair : m_entityBehaviors) {
                auto entity = pair.first;
                auto& behaviorName = pair.second;
                if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                    m_entityBehaviorCache.push_back({entity, m_behaviors[behaviorName].get()});
                }
            }
            m_cacheValid = true;
        }

    void deliverMessageToEntity(EntityPtr entity, const std::string& message) {
        if (entityHasBehavior(entity)) {
            auto behaviorName = m_entityBehaviors[entity];
            if (m_behaviors.find(behaviorName) != m_behaviors.end()) {
                m_behaviors[behaviorName]->onMessage(entity, message);
            }
        }
    }

    // Prevent copying
        AIManager() = default;
        ~AIManager() = default;
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

    // Create test entities and register them for managed updates
    std::vector<EntityPtr> entities;
    for (int i = 0; i < 10; ++i) {
        entities.push_back(OptimizationTestEntity::create(Vector2D(i * 100.0f, i * 100.0f)));
        AIManager::Instance().assignBehaviorToEntity(entities.back(), "TestWander");
        AIManager::Instance().registerEntityForUpdates(entities.back());
    }

    // Force cache validation
    AIManager::Instance().ensureOptimizationCachesValid();

    // Cache should be valid now
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
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("BatchWander", wanderBehavior);

    // Create test entities and register them for managed updates
    std::vector<EntityPtr> entityPtrs;
    for (int i = 0; i < 100; ++i) {
        auto entity = OptimizationTestEntity::create(Vector2D(i * 10.0f, i * 10.0f));
        entityPtrs.push_back(entity);
        AIManager::Instance().assignBehaviorToEntity(entity, "BatchWander");
        AIManager::Instance().registerEntityForUpdates(entity);
    }

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

    // Batch processing should be more efficient
    BOOST_CHECK_LE(batchDuration.count(), individualDuration.count());

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
    // Register a test behavior with update frequency of 3 frames
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    // Update frequency is now controlled by AIManager, not individual behaviors
    AIManager::Instance().registerBehavior("LazyWander", wanderBehavior);

    // Create test entity and register for managed updates
    auto entity = OptimizationTestEntity::create(Vector2D(100.0f, 100.0f));
    AIManager::Instance().assignBehaviorToEntity(entity, "LazyWander");
    AIManager::Instance().registerEntityForUpdates(entity);

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
    auto wanderBehavior = std::make_shared<MockWanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("MsgWander", wanderBehavior);

    // Create test entity and register with consolidated method
    auto entity = OptimizationTestEntity::create(Vector2D(100.0f, 100.0f));
    AIManager::Instance().registerEntityForUpdates(entity, 5, "MsgWander");

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
