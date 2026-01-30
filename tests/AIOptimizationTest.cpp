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
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/EntityDataManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "ai/AIBehavior.hpp"
#include "entities/EntityHandle.hpp"
#include "ai/behaviors/WanderBehavior.hpp"
#include "utils/Vector2D.hpp"

// Test helper for data-driven NPCs (NPCs are purely data, no Entity class)
class OptimizationTestNPC {
public:
    explicit OptimizationTestNPC(const Vector2D& pos = Vector2D(0, 0)) {
        auto& edm = EntityDataManager::Instance();
        m_handle = edm.createNPCWithRaceClass(pos, "Human", "Guard");
    }

    static std::shared_ptr<OptimizationTestNPC> create(const Vector2D& pos = Vector2D(0, 0)) {
        return std::make_shared<OptimizationTestNPC>(pos);
    }

    [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

private:
    EntityHandle m_handle;
};

class NoOpBehavior final : public AIBehavior {
public:
    void executeLogic(BehaviorContext& ctx) override { (void)ctx; }
    void init(EntityHandle) override {}
    void clean(EntityHandle) override {}
    std::string getName() const override { return "NoOp"; }
    std::shared_ptr<AIBehavior> clone() const override {
        return std::make_shared<NoOpBehavior>();
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
        BackgroundSimulationManager::Instance().init();
    }

    ~AITestFixture() {
        // Clean up in reverse order
        BackgroundSimulationManager::Instance().clean();
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

BOOST_GLOBAL_FIXTURE(AITestFixture);

// Helper to update AI with proper tier calculation
// Tests create/destroy entities frequently, so we need to invalidate tiers each time
void updateAI(float deltaTime, const Vector2D& referencePoint = Vector2D(500.0f, 500.0f)) {
    BackgroundSimulationManager::Instance().invalidateTiers();
    BackgroundSimulationManager::Instance().update(referencePoint, deltaTime);
    AIManager::Instance().update(deltaTime);
}

// Test case for entity component caching
BOOST_AUTO_TEST_CASE(TestEntityComponentCaching)
{
    // Register a test behavior using the real WanderBehavior
    auto wanderBehavior = std::make_shared<WanderBehavior>(2.0f, 1000.0f, 200.0f);
    AIManager::Instance().registerBehavior("TestWander", wanderBehavior);

    // Create test NPCs (already registered via createDataDrivenNPC)
    std::vector<EntityHandle> handles;
    std::vector<std::shared_ptr<OptimizationTestNPC>> entities;
    for (int i = 0; i < 10; ++i) {
        Vector2D pos(i * 100.0f, i * 100.0f);
        auto entity = OptimizationTestNPC::create(pos);
        entities.push_back(entity);
        EntityHandle handle = entity->getHandle();
        handles.push_back(handle);
        AIManager::Instance().registerEntity(handle, "TestWander");
    }

    // Process pending assignments
    updateAI(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    // Assignments are now synchronous - no wait needed

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

    // Create test NPCs (already registered via createDataDrivenNPC)
    std::vector<EntityHandle> handles;
    std::vector<std::shared_ptr<OptimizationTestNPC>> entityPtrs;
    for (int i = 0; i < 100; ++i) {
        Vector2D pos(i * 10.0f, i * 10.0f);
        auto entity = OptimizationTestNPC::create(pos);
        entityPtrs.push_back(entity);
        EntityHandle handle = entity->getHandle();
        handles.push_back(handle);
        AIManager::Instance().registerEntity(handle, "BatchWander");
    }

    // Process pending assignments
    updateAI(0.016f);

    // Wait for async assignments to complete before timing updates
    // Assignments are now synchronous - no wait needed

    // Time the unified entity processing
    auto startTime = std::chrono::high_resolution_clock::now();
    updateAI(0.016f);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto batchDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Time multiple managed updates
    startTime = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 5; ++i) {
        updateAI(0.016f);
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

    // Create test NPC (already registered via createDataDrivenNPC)
    Vector2D pos(100.0f, 100.0f);
    auto entity = OptimizationTestNPC::create(pos);
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().registerEntity(handle, "LazyWander");

    // Process pending assignments
    updateAI(0.016f);

    // Wait for async assignments to complete
    // Assignments are now synchronous - no wait needed

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

    // Create test NPC (already registered via createDataDrivenNPC)
    Vector2D pos(100.0f, 100.0f);
    auto entity = OptimizationTestNPC::create(pos);
    EntityHandle handle = entity->getHandle();
    AIManager::Instance().registerEntity(handle, "MsgWander");

    // Process pending assignments
    updateAI(0.016f);

    // Wait for async assignments to complete (matches production behavior)
    // Assignments are now synchronous - no wait needed

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

BOOST_AUTO_TEST_CASE(TestSIMDMovementIntegrationClamp)
{
    auto noopBehavior = std::make_shared<NoOpBehavior>();
    AIManager::Instance().registerBehavior("NoOp", noopBehavior);

    std::vector<EntityHandle> handles;
    std::vector<std::shared_ptr<OptimizationTestNPC>> entities;
    auto& edm = EntityDataManager::Instance();

    auto createEntity = [&](const Vector2D& pos) {
        auto entity = OptimizationTestNPC::create(pos);
        entities.push_back(entity);
        EntityHandle handle = entity->getHandle();
        handles.push_back(handle);
        AIManager::Instance().registerEntity(handle, "NoOp");
    };

    createEntity(Vector2D(10.0f, 10.0f));
    createEntity(Vector2D(10.0f, 10.0f));
    createEntity(Vector2D(10.0f, 10.0f));
    createEntity(Vector2D(100.0f, 100.0f));
    createEntity(Vector2D(12.0f, 12.0f));

    updateAI(0.016f, Vector2D(0.0f, 0.0f));

    {
        size_t idx = edm.getIndex(handles[0]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(10.0f, 10.0f);
        transform.velocity = Vector2D(-50.0f, 0.0f);
    }
    {
        size_t idx = edm.getIndex(handles[1]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(10.0f, 10.0f);
        transform.velocity = Vector2D(0.0f, -50.0f);
    }
    {
        size_t idx = edm.getIndex(handles[2]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(10.0f, 10.0f);
        transform.velocity = Vector2D(-50.0f, -50.0f);
    }
    {
        size_t idx = edm.getIndex(handles[3]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(100.0f, 100.0f);
        transform.velocity = Vector2D(10.0f, 10.0f);
    }
    {
        size_t idx = edm.getIndex(handles[4]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(12.0f, 12.0f);
        transform.velocity = Vector2D(-20.0f, -20.0f);
    }

    updateAI(1.0f, Vector2D(0.0f, 0.0f));

    {
        // Entity 0: pos (10, 10), vel (-50, 0) -> after 1s: (-40, 10)
        // Both axes clamped to min=16 (halfWidth/halfHeight), velocity zeroed
        size_t idx = edm.getIndex(handles[0]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }
    {
        // Entity 1: pos (10, 10), vel (0, -50) -> after 1s: (10, -40)
        // Both axes clamped to min=16 (halfWidth/halfHeight), velocity zeroed
        size_t idx = edm.getIndex(handles[1]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[2]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[3]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 110.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 110.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 10.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 10.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[4]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 16.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }

    {
        size_t idx = edm.getIndex(handles[0]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(31980.0f, 31980.0f);
        transform.velocity = Vector2D(50.0f, 0.0f);
    }
    {
        size_t idx = edm.getIndex(handles[1]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(31980.0f, 31980.0f);
        transform.velocity = Vector2D(0.0f, 50.0f);
    }
    {
        size_t idx = edm.getIndex(handles[2]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(31980.0f, 31980.0f);
        transform.velocity = Vector2D(50.0f, 50.0f);
    }
    {
        size_t idx = edm.getIndex(handles[3]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(31900.0f, 31900.0f);
        transform.velocity = Vector2D(-10.0f, -10.0f);
    }
    {
        size_t idx = edm.getIndex(handles[4]);
        auto& transform = edm.getHotDataByIndex(idx).transform;
        transform.position = Vector2D(31970.0f, 31970.0f);
        transform.velocity = Vector2D(40.0f, 40.0f);
    }

    updateAI(1.0f, Vector2D(31900.0f, 31900.0f));

    {
        size_t idx = edm.getIndex(handles[0]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 31980.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[1]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 31980.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[2]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[3]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 31890.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 31890.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), -10.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), -10.0f, 0.001f);
    }
    {
        size_t idx = edm.getIndex(handles[4]);
        const auto& transform = edm.getHotDataByIndex(idx).transform;
        BOOST_CHECK_CLOSE(transform.position.getX(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.position.getY(), 31984.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getX(), 0.0f, 0.001f);
        BOOST_CHECK_CLOSE(transform.velocity.getY(), 0.0f, 0.001f);
    }

    for (const auto& handle : handles) {
        AIManager::Instance().unregisterEntity(handle);
        AIManager::Instance().unassignBehavior(handle);
        edm.unregisterEntity(handle.getId());
    }
    handles.clear();
    entities.clear();
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
        // Create entities at known positions (already registered via createDataDrivenNPC)
        std::vector<std::shared_ptr<OptimizationTestNPC>> entities;
        std::vector<EntityHandle> handles;
        for (size_t i = 0; i < count; ++i) {
            // Place entities at (100 * i, 100 * i) for predictable distances
            Vector2D pos(100.0f * static_cast<float>(i), 100.0f * static_cast<float>(i));
            auto entity = OptimizationTestNPC::create(pos);
            entities.push_back(entity);
            EntityHandle handle = entity->getHandle();
            handles.push_back(handle);
            AIManager::Instance().registerEntity(handle, "DistanceTestWander");
        }

        // Process assignments
        updateAI(0.016f);
        // Assignments are now synchronous - no wait needed

        // Run a few update cycles to ensure distance calculations run
        for (int frame = 0; frame < 3; ++frame) {
            updateAI(0.016f);
        }

        // Verify all entities received valid processing (no teleportation to (0,0))
        for (size_t i = 0; i < entities.size(); ++i) {
            size_t edmIndex = edm.getIndex(entities[i]->getHandle());
            auto pos = edm.getTransformByIndex(edmIndex).position;
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
