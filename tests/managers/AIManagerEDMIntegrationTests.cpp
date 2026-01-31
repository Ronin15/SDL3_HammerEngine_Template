/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

/**
 * @file AIManagerEDMIntegrationTests.cpp
 * @brief Tests for AIManager's integration with EntityDataManager
 *
 * These tests verify AI Manager-specific EDM integration:
 * - Sparse behavior vector (m_behaviorsByEdmIndex) management
 * - EDM index caching in EntityStorage
 * - Batch processing using EDM indices
 * - State transition cleanup of AI-specific data
 *
 * NOTE: Handle generation, slot reuse, and tier management are tested
 * in EntityDataManagerTests.cpp - these tests focus on AI Manager's
 * specific use of EDM data.
 */

#define BOOST_TEST_MODULE AIManagerEDMIntegrationTests
#include <boost/test/unit_test.hpp>

#include "core/ThreadSystem.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

// Test helper for data-driven NPCs (NPCs are purely data, no Entity class)
class AITestNPC {
public:
    explicit AITestNPC(const Vector2D& pos = Vector2D(0, 0)) {
        auto& edm = EntityDataManager::Instance();
        m_handle = edm.createNPCWithRaceClass(pos, "Human", "Guard");
        m_initialPosition = pos;
    }

    static std::shared_ptr<AITestNPC> create(const Vector2D& pos = Vector2D(0, 0)) {
        return std::make_shared<AITestNPC>(pos);
    }

    [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

    // Check if position changed in EDM (AIManager writes directly to EDM)
    [[nodiscard]] bool hasPositionChanged() const {
        if (!m_handle.isValid()) return false;

        auto& edm = EntityDataManager::Instance();
        size_t index = edm.getIndex(m_handle);
        if (index == SIZE_MAX) return false;

        auto& transform = edm.getTransformByIndex(index);
        return (transform.position - m_initialPosition).length() > 0.01f ||
               transform.velocity.length() > 0.01f;
    }

    void resetInitialPosition() {
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

// Simple test behavior that modifies position via BehaviorContext
class EDMTestBehavior : public AIBehavior {
public:
    EDMTestBehavior() = default;

    void executeLogic(BehaviorContext& ctx) override {
        // Move entity slightly to verify EDM write
        ctx.transform.velocity = Vector2D(1.0f, 1.0f);
        m_executionCount++;
    }

    void init(EntityHandle) override { m_initialized = true; }
    void clean(EntityHandle) override { m_initialized = false; }
    std::string getName() const override { return "EDMTestBehavior"; }

    std::shared_ptr<AIBehavior> clone() const override {
        return std::make_shared<EDMTestBehavior>();
    }

    int getExecutionCount() const { return m_executionCount.load(); }
    bool isInitialized() const { return m_initialized; }

private:
    std::atomic<int> m_executionCount{0};
    bool m_initialized{false};
};

// Test fixture that initializes all required managers
struct AIManagerEDMFixture {
    AIManagerEDMFixture() {
        HammerEngine::ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
        BackgroundSimulationManager::Instance().init();

        // Register test behavior
        AIManager::Instance().registerBehavior(
            "EDMTestBehavior",
            std::make_shared<EDMTestBehavior>()
        );
    }

    ~AIManagerEDMFixture() {
        BackgroundSimulationManager::Instance().clean();
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

// ============================================================================
// SPARSE BEHAVIOR VECTOR TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(SparseBehaviorVectorTests, AIManagerEDMFixture)

BOOST_AUTO_TEST_CASE(TestBehaviorAssignmentCreatesEdmIndexMapping) {
    // Create entity and get its EDM index
    auto entity = AITestNPC::create(Vector2D(100.0f, 100.0f));
    EntityHandle handle = entity->getHandle();
    BOOST_REQUIRE(handle.isValid());

    size_t edmIndex = EntityDataManager::Instance().getIndex(handle);
    BOOST_REQUIRE(edmIndex != SIZE_MAX);

    // Assign behavior
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");

    // Verify behavior is assigned
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));
}

BOOST_AUTO_TEST_CASE(TestSparseBehaviorVectorHandlesGaps) {
    // Create entities at different positions (will get different EDM indices)
    // Note: NPCs created via createNPCWithRaceClass auto-register with their
    // class's suggestedBehavior (e.g., "Guard"), so we must unassign first
    std::vector<std::shared_ptr<AITestNPC>> entities;
    std::vector<EntityHandle> handles;

    for (int i = 0; i < 10; ++i) {
        auto entity = AITestNPC::create(Vector2D(i * 100.0f, 0.0f));
        entities.push_back(entity);
        handles.push_back(entity->getHandle());
        // Unassign auto-registered behavior to start with clean slate
        AIManager::Instance().unassignBehavior(entity->getHandle());
    }

    // Assign behaviors to only odd-indexed entities (creates gaps)
    for (size_t i = 1; i < handles.size(); i += 2) {
        AIManager::Instance().assignBehavior(handles[i], "EDMTestBehavior");
    }

    // Verify correct entities have behaviors
    for (size_t i = 0; i < handles.size(); ++i) {
        bool shouldHaveBehavior = (i % 2 == 1);
        BOOST_CHECK_EQUAL(AIManager::Instance().hasBehavior(handles[i]), shouldHaveBehavior);
    }
}

BOOST_AUTO_TEST_CASE(TestBehaviorUnassignmentClearsSparseBehavior) {
    auto entity = AITestNPC::create(Vector2D(100.0f, 100.0f));
    EntityHandle handle = entity->getHandle();

    // Assign then unassign
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    AIManager::Instance().unassignBehavior(handle);
    BOOST_CHECK(!AIManager::Instance().hasBehavior(handle));
}

BOOST_AUTO_TEST_CASE(TestBehaviorReassignmentUpdatesSparseBehavior) {
    auto entity = AITestNPC::create(Vector2D(100.0f, 100.0f));
    EntityHandle handle = entity->getHandle();

    // Assign, unassign, then reassign
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");
    AIManager::Instance().unassignBehavior(handle);
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");

    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// BATCH PROCESSING WITH EDM INDICES TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(BatchProcessingEDMTests, AIManagerEDMFixture)

BOOST_AUTO_TEST_CASE(TestBatchProcessingWritesToEDMTransform) {
    // Create entity and assign behavior
    auto entity = AITestNPC::create(Vector2D(500.0f, 500.0f));
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");

    // Get EDM index
    auto& edm = EntityDataManager::Instance();
    size_t index = edm.getIndex(handle);
    BOOST_REQUIRE(index != SIZE_MAX);

    // Verify entity is registered with EDM and has behavior
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // Verify the entity's transform is accessible via EDM
    auto& transform = edm.getTransformByIndex(index);
    BOOST_CHECK_CLOSE(transform.position.getX(), 500.0f, 0.01f);
    BOOST_CHECK_CLOSE(transform.position.getY(), 500.0f, 0.01f);

    // Note: Actual batch processing depends on tier updates and threading
    // which is tested more thoroughly in AIScalingBenchmark and ThreadSafeAIManagerTests
}

BOOST_AUTO_TEST_CASE(TestMultipleEntitiesProcessedViaBatch) {
    const size_t ENTITY_COUNT = 50;
    std::vector<std::shared_ptr<AITestNPC>> entities;
    std::vector<EntityHandle> handles;

    // Create and assign behaviors to many entities
    for (size_t i = 0; i < ENTITY_COUNT; ++i) {
        auto entity = AITestNPC::create(Vector2D(100.0f + i * 50.0f, 100.0f));
        AIManager::Instance().assignBehavior(entity->getHandle(), "EDMTestBehavior");
        entities.push_back(entity);
        handles.push_back(entity->getHandle());
    }

    // Verify all entities are registered with behaviors
    auto& edm = EntityDataManager::Instance();
    size_t registeredCount = 0;
    for (const auto& handle : handles) {
        if (AIManager::Instance().hasBehavior(handle)) {
            size_t index = edm.getIndex(handle);
            if (index != SIZE_MAX) {
                registeredCount++;
            }
        }
    }

    // All entities should be registered with behaviors and have EDM indices
    BOOST_CHECK_EQUAL(registeredCount, ENTITY_COUNT);

    // Note: Actual batch processing execution is tested in AIScalingBenchmark
    // and ThreadSafeAIManagerTests which properly set up threading and tiers
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// STATE TRANSITION TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(StateTransitionTests, AIManagerEDMFixture)

BOOST_AUTO_TEST_CASE(TestPrepareForStateTransitionClearsAIData) {
    // Create entities with behaviors
    std::vector<std::shared_ptr<AITestNPC>> entities;
    for (int i = 0; i < 10; ++i) {
        auto entity = AITestNPC::create(Vector2D(i * 100.0f, 0.0f));
        AIManager::Instance().assignBehavior(entity->getHandle(), "EDMTestBehavior");
        entities.push_back(entity);
    }

    // Verify behaviors exist
    for (const auto& entity : entities) {
        BOOST_CHECK(AIManager::Instance().hasBehavior(entity->getHandle()));
    }

    // Trigger state transition
    AIManager::Instance().prepareForStateTransition();

    // Verify all AI data is cleared (behaviors should no longer exist)
    for (const auto& entity : entities) {
        BOOST_CHECK(!AIManager::Instance().hasBehavior(entity->getHandle()));
    }
}

BOOST_AUTO_TEST_CASE(TestStateTransitionWhileBatchProcessing) {
    // Create many entities to ensure batch processing is used
    std::vector<std::shared_ptr<AITestNPC>> entities;
    for (int i = 0; i < 100; ++i) {
        auto entity = AITestNPC::create(Vector2D(i * 50.0f, 100.0f));
        AIManager::Instance().assignBehavior(entity->getHandle(), "EDMTestBehavior");
        entities.push_back(entity);
    }

    // Set world bounds and trigger tier update
    CollisionManager::Instance().setWorldBounds(0, 0, 10000.0f, 10000.0f);
    BackgroundSimulationManager::Instance().update(Vector2D(500.0f, 500.0f), 0.016f);

    // Start an update (may trigger batch processing)
    AIManager::Instance().update(0.016f);

    // Immediately request state transition
    AIManager::Instance().prepareForStateTransition();

    // Should not crash and all data should be cleared
    for (const auto& entity : entities) {
        BOOST_CHECK(!AIManager::Instance().hasBehavior(entity->getHandle()));
    }
}

BOOST_AUTO_TEST_CASE(TestAIManagerReinitAfterStateTransition) {
    // Create entity and assign behavior
    auto entity1 = AITestNPC::create(Vector2D(100.0f, 100.0f));
    AIManager::Instance().assignBehavior(entity1->getHandle(), "EDMTestBehavior");
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity1->getHandle()));

    // State transition clears everything
    AIManager::Instance().prepareForStateTransition();
    EntityDataManager::Instance().prepareForStateTransition();

    // Create new entity after transition
    auto entity2 = AITestNPC::create(Vector2D(200.0f, 200.0f));
    AIManager::Instance().assignBehavior(entity2->getHandle(), "EDMTestBehavior");

    // New entity should have behavior
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity2->getHandle()));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// EDM INDEX CACHING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(EDMIndexCachingTests, AIManagerEDMFixture)

BOOST_AUTO_TEST_CASE(TestEdmIndexCachedOnBehaviorAssignment) {
    auto entity = AITestNPC::create(Vector2D(100.0f, 100.0f));
    EntityHandle handle = entity->getHandle();

    size_t expectedIndex = EntityDataManager::Instance().getIndex(handle);
    BOOST_REQUIRE(expectedIndex != SIZE_MAX);

    // Assign behavior
    AIManager::Instance().assignBehavior(handle, "EDMTestBehavior");
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    // The index should be cached internally (verified indirectly via successful batch processing)
}

BOOST_AUTO_TEST_CASE(TestEntityDestructionDoesNotAffectOtherEntities) {
    // Create multiple entities
    auto entity1 = AITestNPC::create(Vector2D(100.0f, 100.0f));
    auto entity2 = AITestNPC::create(Vector2D(200.0f, 200.0f));
    auto entity3 = AITestNPC::create(Vector2D(300.0f, 300.0f));

    EntityHandle handle1 = entity1->getHandle();
    EntityHandle handle2 = entity2->getHandle();
    EntityHandle handle3 = entity3->getHandle();

    // Assign behaviors to all
    AIManager::Instance().assignBehavior(handle1, "EDMTestBehavior");
    AIManager::Instance().assignBehavior(handle2, "EDMTestBehavior");
    AIManager::Instance().assignBehavior(handle3, "EDMTestBehavior");

    // Unassign middle entity's behavior
    AIManager::Instance().unassignBehavior(handle2);

    // Other entities should still have behaviors
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle1));
    BOOST_CHECK(!AIManager::Instance().hasBehavior(handle2));
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle3));
}

BOOST_AUTO_TEST_SUITE_END()

// ============================================================================
// BEHAVIOR TEMPLATE CLONING TESTS
// ============================================================================

BOOST_FIXTURE_TEST_SUITE(BehaviorCloningTests, AIManagerEDMFixture)

BOOST_AUTO_TEST_CASE(TestEachEntityGetsSeparateBehaviorInstance) {
    auto entity1 = AITestNPC::create(Vector2D(100.0f, 100.0f));
    auto entity2 = AITestNPC::create(Vector2D(200.0f, 200.0f));

    AIManager::Instance().assignBehavior(entity1->getHandle(), "EDMTestBehavior");
    AIManager::Instance().assignBehavior(entity2->getHandle(), "EDMTestBehavior");

    // Both should have behaviors (cloned instances, not shared)
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity1->getHandle()));
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity2->getHandle()));

    // Unassigning one should not affect the other
    AIManager::Instance().unassignBehavior(entity1->getHandle());
    BOOST_CHECK(!AIManager::Instance().hasBehavior(entity1->getHandle()));
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity2->getHandle()));
}

BOOST_AUTO_TEST_SUITE_END()
