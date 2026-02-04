/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ThreadSafeAIManagerTests
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "core/ThreadSystem.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"

// Test helper for data-driven NPCs
class TestNPC {
public:
  explicit TestNPC(const Vector2D &pos = Vector2D(0, 0)) {
    auto& edm = EntityDataManager::Instance();
    m_handle = edm.createNPCWithRaceClass(pos, "Human", "Guard");
    m_initialPosition = pos;
  }

  static std::shared_ptr<TestNPC> create(const Vector2D &pos = Vector2D(0, 0)) {
    return std::make_shared<TestNPC>(pos);
  }

  [[nodiscard]] EntityHandle getHandle() const { return m_handle; }

  // Check if entity was processed (position or velocity changed)
  bool wasUpdated() const {
    if (!m_handle.isValid()) return false;

    auto& edm = EntityDataManager::Instance();
    size_t index = edm.getIndex(m_handle);
    if (index == SIZE_MAX) return false;

    auto& transform = edm.getTransformByIndex(index);
    Vector2D currentPos = transform.position;
    Vector2D velocity = transform.velocity;

    bool positionMoved = (currentPos - m_initialPosition).length() > 0.01f;
    bool hasVelocity = velocity.length() > 0.01f;

    return positionMoved || hasVelocity;
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

// Global fixture for test setup and cleanup
struct ThreadSafeAIFixture {
  ThreadSafeAIFixture() {
    HammerEngine::ThreadSystem::Instance().init();
    EntityDataManager::Instance().init();
    CollisionManager::Instance().init();
    PathfinderManager::Instance().init();
    AIManager::Instance().init();
    BackgroundSimulationManager::Instance().init();
  }

  ~ThreadSafeAIFixture() {
    BackgroundSimulationManager::Instance().clean();
    AIManager::Instance().clean();
    PathfinderManager::Instance().clean();
    CollisionManager::Instance().clean();
    EntityDataManager::Instance().clean();
    HammerEngine::ThreadSystem::Instance().clean();
  }
};

BOOST_GLOBAL_FIXTURE(ThreadSafeAIFixture);

// Helper to update AI with proper tier calculation
void updateAI(float deltaTime, const Vector2D& referencePoint = Vector2D(500.0f, 500.0f)) {
  BackgroundSimulationManager::Instance().invalidateTiers();
  BackgroundSimulationManager::Instance().update(referencePoint, deltaTime);
  AIManager::Instance().update(deltaTime);
}

// ===========================================================================
// Test Cases
// ===========================================================================

BOOST_AUTO_TEST_SUITE(ThreadSafeAIManagerTests)

// Test basic entity registration and behavior assignment
BOOST_AUTO_TEST_CASE(BasicEntityRegistration)
{
  auto npc = TestNPC::create(Vector2D(100.0f, 100.0f));
  EntityHandle handle = npc->getHandle();

  BOOST_REQUIRE(handle.isValid());

  AIManager::Instance().assignBehavior(handle, "Wander");
  BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

  // Clean up
  AIManager::Instance().unassignBehavior(handle);
  auto& edm = EntityDataManager::Instance();
  edm.destroyEntity(handle);
}

// Test concurrent entity registration from multiple threads
BOOST_AUTO_TEST_CASE(ConcurrentEntityRegistration)
{
  constexpr int NUM_THREADS = 4;
  constexpr int ENTITIES_PER_THREAD = 25;

  std::vector<std::future<std::vector<EntityHandle>>> futures;
  std::atomic<int> successCount{0};

  for (int t = 0; t < NUM_THREADS; ++t) {
    futures.push_back(std::async(std::launch::async, [&, t]() {
      std::vector<EntityHandle> handles;
      for (int i = 0; i < ENTITIES_PER_THREAD; ++i) {
        Vector2D pos(t * 100.0f + i * 10.0f, t * 100.0f + i * 10.0f);
        auto npc = TestNPC::create(pos);
        EntityHandle handle = npc->getHandle();

        if (handle.isValid()) {
          AIManager::Instance().assignBehavior(handle, "Wander");
          handles.push_back(handle);
          successCount.fetch_add(1, std::memory_order_relaxed);
        }
      }
      return handles;
    }));
  }

  // Wait for all threads and collect handles
  std::vector<EntityHandle> allHandles;
  for (auto& f : futures) {
    auto handles = f.get();
    allHandles.insert(allHandles.end(), handles.begin(), handles.end());
  }

  BOOST_CHECK_EQUAL(successCount.load(), NUM_THREADS * ENTITIES_PER_THREAD);

  // Clean up
  auto& edm = EntityDataManager::Instance();
  for (const auto& handle : allHandles) {
    AIManager::Instance().unassignBehavior(handle);
    edm.destroyEntity(handle);
  }
}

// Test AI update with multiple entities
BOOST_AUTO_TEST_CASE(MultipleEntityUpdate)
{
  constexpr int NUM_ENTITIES = 50;
  std::vector<std::shared_ptr<TestNPC>> npcs;
  std::vector<EntityHandle> handles;

  // Create and register entities
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    Vector2D pos(i * 20.0f, i * 20.0f);
    auto npc = TestNPC::create(pos);
    EntityHandle handle = npc->getHandle();
    AIManager::Instance().assignBehavior(handle, "Wander");
    npcs.push_back(npc);
    handles.push_back(handle);
  }

  // Run AI update
  updateAI(0.016f);

  // Check that entities have behaviors assigned
  for (const auto& handle : handles) {
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));
  }

  // Clean up
  auto& edm = EntityDataManager::Instance();
  for (const auto& handle : handles) {
    AIManager::Instance().unassignBehavior(handle);
    edm.destroyEntity(handle);
  }
}

// Test behavior assignment during update (concurrent access)
BOOST_AUTO_TEST_CASE(ConcurrentBehaviorAssignmentDuringUpdate)
{
  constexpr int NUM_ENTITIES = 20;
  std::vector<std::shared_ptr<TestNPC>> npcs;
  std::vector<EntityHandle> handles;

  // Create entities
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    Vector2D pos(i * 20.0f, i * 20.0f);
    auto npc = TestNPC::create(pos);
    handles.push_back(npc->getHandle());
    npcs.push_back(npc);
  }

  // Assign behaviors in one thread while updating in another
  auto assignThread = std::async(std::launch::async, [&]() {
    for (const auto& handle : handles) {
      AIManager::Instance().assignBehavior(handle, "Wander");
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  });

  auto updateThread = std::async(std::launch::async, [&]() {
    for (int i = 0; i < 10; ++i) {
      updateAI(0.016f);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  assignThread.wait();
  updateThread.wait();

  // Verify all entities have behaviors
  for (const auto& handle : handles) {
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));
  }

  // Clean up
  auto& edm = EntityDataManager::Instance();
  for (const auto& handle : handles) {
    AIManager::Instance().unassignBehavior(handle);
    edm.destroyEntity(handle);
  }
}

// Test message sending
BOOST_AUTO_TEST_CASE(MessageSending)
{
  auto npc = TestNPC::create(Vector2D(100.0f, 100.0f));
  EntityHandle handle = npc->getHandle();

  AIManager::Instance().assignBehavior(handle, "Idle");

  // Send messages (messages are queued in data-oriented system)
  AIManager::Instance().sendMessageToEntity(handle, "test_message");
  AIManager::Instance().broadcastMessage("broadcast_test");

  // Update to process messages
  updateAI(0.016f);

  // Clean up
  AIManager::Instance().unassignBehavior(handle);
  auto& edm = EntityDataManager::Instance();
  edm.destroyEntity(handle);

  BOOST_CHECK(true);  // Test passes if no crash/hang
}

// Test rapid assignment/unassignment
BOOST_AUTO_TEST_CASE(RapidAssignmentUnassignment)
{
  auto npc = TestNPC::create(Vector2D(100.0f, 100.0f));
  EntityHandle handle = npc->getHandle();

  for (int i = 0; i < 100; ++i) {
    AIManager::Instance().assignBehavior(handle, "Wander");
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    AIManager::Instance().unassignBehavior(handle);
    // After unassign, hasBehavior should return false
  }

  // Clean up
  auto& edm = EntityDataManager::Instance();
  edm.destroyEntity(handle);
}

// Test global pause
BOOST_AUTO_TEST_CASE(GlobalPause)
{
  auto npc = TestNPC::create(Vector2D(100.0f, 100.0f));
  EntityHandle handle = npc->getHandle();
  npc->resetInitialPosition();

  AIManager::Instance().assignBehavior(handle, "Wander");

  // Pause and update - entity should not be updated
  AIManager::Instance().setGlobalPause(true);
  BOOST_CHECK(AIManager::Instance().isGloballyPaused());

  updateAI(0.016f);

  // Resume
  AIManager::Instance().setGlobalPause(false);
  BOOST_CHECK(!AIManager::Instance().isGloballyPaused());

  // Clean up
  AIManager::Instance().unassignBehavior(handle);
  auto& edm = EntityDataManager::Instance();
  edm.destroyEntity(handle);
}

// Test different behavior types
BOOST_AUTO_TEST_CASE(DifferentBehaviorTypes)
{
  std::vector<std::string> behaviorTypes = {"Idle", "Wander", "Patrol", "Guard"};
  std::vector<std::shared_ptr<TestNPC>> npcs;
  std::vector<EntityHandle> handles;

  for (size_t i = 0; i < behaviorTypes.size(); ++i) {
    Vector2D pos(i * 50.0f, i * 50.0f);
    auto npc = TestNPC::create(pos);
    EntityHandle handle = npc->getHandle();

    AIManager::Instance().assignBehavior(handle, behaviorTypes[i]);
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));

    npcs.push_back(npc);
    handles.push_back(handle);
  }

  // Update all
  updateAI(0.016f);

  // Verify behaviors still assigned
  for (const auto& handle : handles) {
    BOOST_CHECK(AIManager::Instance().hasBehavior(handle));
  }

  // Clean up
  auto& edm = EntityDataManager::Instance();
  for (const auto& handle : handles) {
    AIManager::Instance().unassignBehavior(handle);
    edm.destroyEntity(handle);
  }
}

// Test high entity count
BOOST_AUTO_TEST_CASE(HighEntityCount)
{
  constexpr int NUM_ENTITIES = 500;
  std::vector<std::shared_ptr<TestNPC>> npcs;
  std::vector<EntityHandle> handles;

  // Create many entities
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    float x = static_cast<float>(i % 50) * 20.0f;
    float y = static_cast<float>(i / 50) * 20.0f;
    auto npc = TestNPC::create(Vector2D(x, y));
    EntityHandle handle = npc->getHandle();
    AIManager::Instance().assignBehavior(handle, "Wander");
    npcs.push_back(npc);
    handles.push_back(handle);
  }

  // Update multiple frames
  for (int frame = 0; frame < 5; ++frame) {
    updateAI(0.016f);
  }

  // Verify no crashes and entities have behaviors
  int assignedCount = 0;
  for (const auto& handle : handles) {
    if (AIManager::Instance().hasBehavior(handle)) {
      assignedCount++;
    }
  }

  BOOST_CHECK_EQUAL(assignedCount, NUM_ENTITIES);

  // Clean up
  auto& edm = EntityDataManager::Instance();
  for (const auto& handle : handles) {
    AIManager::Instance().unassignBehavior(handle);
    edm.destroyEntity(handle);
  }
}

BOOST_AUTO_TEST_SUITE_END()
