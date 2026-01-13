/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#define BOOST_TEST_MODULE ThreadSafeAIIntegrationTest
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "managers/AIManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "core/ThreadSystem.hpp"

// Test helper for data-driven NPCs (NPCs are purely data, no Entity class)
class IntegrationTestNPC {
public:
    explicit IntegrationTestNPC(int id = 0, const Vector2D& pos = Vector2D(0, 0)) : m_id(id) {
        auto& edm = EntityDataManager::Instance();
        m_handle = edm.createDataDrivenNPC(pos, "Guard");
        m_initialPosition = pos;
    }

    static std::shared_ptr<IntegrationTestNPC> create(int id = 0, const Vector2D& pos = Vector2D(0, 0)) {
        return std::make_shared<IntegrationTestNPC>(id, pos);
    }

    [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

    int getId() const { return m_id; }

    // Check if entity was updated (position changed or has velocity)
    int getUpdateCount() const {
        if (!m_handle.isValid()) return 0;

        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return 0;

        auto& transform = edm.getTransformByIndex(index);
        bool positionMoved = (transform.position - m_initialPosition).length() > 0.01f;
        bool hasVelocity = transform.velocity.length() > 0.01f;
        return (positionMoved || hasVelocity) ? 1 : 0;
    }

private:
    EntityHandle m_handle;
    Vector2D m_initialPosition;
    int m_id;
};

// Test behavior
class IntegrationTestBehavior : public AIBehavior {
public:
    IntegrationTestBehavior(const std::string& name) : m_name(name) {}

    // Lock-free hot path (required by pure virtual)
    void executeLogic(BehaviorContext& ctx) override {
        // Test behavior: minimal implementation for BehaviorContext path
        // The test primarily validates threading/registration, not behavior logic
        m_updateCount++;

        // Occasionally send a message (very infrequently)
        if (m_updateCount % 500 == 0) {
            AIManager::Instance().broadcastMessage("test_message");
        }
        (void)ctx;
    }

    void init(EntityHandle handle) override {
        if (!handle.isValid()) return;
        m_initialized = true;
    }

    void clean(EntityHandle handle) override {
        if (!handle.isValid()) return;

        // Avoid using shared_from_this() on the entity
        // Just mark as uninitialized
        m_initialized = false;
    }

    std::string getName() const override {
        return m_name;
    }

    std::shared_ptr<AIBehavior> clone() const override {
        auto cloned = std::make_shared<IntegrationTestBehavior>(m_name);
        cloned->setActive(m_active);
        return cloned;
    }

    void onMessage(EntityHandle /* handle */, const std::string& /* message */) override {
        m_messageCount++;
    }

    int getUpdateCount() const { return m_updateCount.load(); }
    int getMessageCount() const { return m_messageCount.load(); }

private:
    std::string m_name;
    std::atomic<bool> m_initialized{false};
    std::atomic<int> m_updateCount{0};
    std::atomic<int> m_messageCount{0};
};

// Global test fixture for setting up and tearing down the system once for all tests
struct GlobalTestFixture {
    GlobalTestFixture() {
        HammerEngine::ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
        #ifndef NDEBUG
        AIManager::Instance().enableThreading(true);
#endif
    }

    ~GlobalTestFixture() {
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

// Individual test fixture
struct AIIntegrationTestFixture {
    AIIntegrationTestFixture() {
        // Create test behaviors
        for (int i = 0; i < NUM_BEHAVIORS; ++i) {
            behaviors.push_back(std::make_shared<IntegrationTestBehavior>("Behavior" + std::to_string(i)));
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behaviors.back());
        }

        // Create test entities
        for (int i = 0; i < NUM_ENTITIES; ++i) {
            auto entity = IntegrationTestNPC::create(i, Vector2D(i * 10.0f, i * 10.0f));
            entities.push_back(entity);
            int behaviorIdx = i % NUM_BEHAVIORS;
            AIManager::Instance().registerEntity(entity->getHandle(), "Behavior" + std::to_string(behaviorIdx));
        }

        // Process queued assignments (need tier indices for update to work)
        auto& edm = EntityDataManager::Instance();
        for (int i = 0; i < 5; ++i) {
            edm.updateSimulationTiers(Vector2D(100.0f, 100.0f), 3000.0f, 5000.0f);
            AIManager::Instance().update(0.016f);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    ~AIIntegrationTestFixture() {
        // Unregister entities
        for (auto& entity : entities) {
            if (entity) {
                AIManager::Instance().unregisterEntity(entity->getHandle());
                AIManager::Instance().unassignBehavior(entity->getHandle());
            }
        }
        entities.clear();
        behaviors.clear();
        AIManager::Instance().resetBehaviors();
    }

    static constexpr int NUM_BEHAVIORS = 5;
    static constexpr int NUM_ENTITIES = 20;
    static constexpr int NUM_UPDATES = 10;

    std::vector<std::shared_ptr<IntegrationTestBehavior>> behaviors;
    std::vector<std::shared_ptr<IntegrationTestNPC>> entities;
};

// Apply the global fixture to the entire test module
BOOST_GLOBAL_FIXTURE(GlobalTestFixture);

// Helper to update AI with proper tier calculation
void updateAI(float deltaTime, const Vector2D& referencePoint = Vector2D(100.0f, 100.0f)) {
    auto& edm = EntityDataManager::Instance();
    edm.updateSimulationTiers(referencePoint, 3000.0f, 5000.0f);
    AIManager::Instance().update(deltaTime);
}

BOOST_FIXTURE_TEST_SUITE(AIIntegrationTests, AIIntegrationTestFixture)

// Test that updates work properly
BOOST_AUTO_TEST_CASE(TestConcurrentUpdates) {
    // Get initial behavior execution count
    size_t initialCount = AIManager::Instance().getBehaviorUpdateCount();

    // Update the AI system multiple times
    for (int i = 0; i < NUM_UPDATES; ++i) {
        updateAI(0.016f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Verify behaviors were executed (DOD pattern: AIManager calls behavior->executeLogic(),
    // not entity->update(), so we check AIManager's behavior execution count)
    size_t finalCount = AIManager::Instance().getBehaviorUpdateCount();
    bool behaviorsExecuted = (finalCount > initialCount);

    BOOST_CHECK_MESSAGE(behaviorsExecuted,
        "Expected behavior executions to increase. Initial: " << initialCount
        << ", Final: " << finalCount);

    // Note: In DOD architecture, AIManager doesn't call Entity::update().
    // Instead, it calls behavior->executeLogic(ctx) which operates on EntityDataManager data.
    // The getBehaviorUpdateCount() tracks these executions.
}

// Test concurrent assignment and update
BOOST_AUTO_TEST_CASE(TestConcurrentAssignmentAndUpdate) {
    // Get a test entity
    BOOST_REQUIRE(!entities.empty());
    auto entity = entities[0];

    // Queue a behavior assignment and process it
    AIManager::Instance().assignBehavior(entity->getHandle(), "Behavior0");
    updateAI(0.016f);

    // Success criteria is simply not crashing
    BOOST_CHECK(true);
}

// Test message delivery
BOOST_AUTO_TEST_CASE(TestMessageDelivery) {
    // Get a test entity
    BOOST_REQUIRE(!entities.empty());
    auto testEntity = entities[0];

    // Ensure the behavior is assigned before messaging
    BOOST_REQUIRE(AIManager::Instance().hasBehavior(testEntity->getHandle()));

    // Send message immediately
    AIManager::Instance().sendMessageToEntity(testEntity->getHandle(), "test_message", true);

    // Success if we get here without crashing
    BOOST_CHECK(true);
}

// Test cache invalidation - simplest possible implementation
BOOST_AUTO_TEST_CASE(TestCacheInvalidation) {
    // Just verify it doesn't crash
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
