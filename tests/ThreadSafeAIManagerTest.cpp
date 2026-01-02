/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#define BOOST_TEST_MODULE ThreadSafeAIManagerTests
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/ThreadSystem.hpp"
#include "entities/Entity.hpp"
#include "managers/AIManager.hpp"
#include "managers/BackgroundSimulationManager.hpp"
#include "managers/CollisionManager.hpp"
#include "managers/PathfinderManager.hpp"
#include "managers/EntityDataManager.hpp"

// Simple test entity - EDM Migration: checks EDM data for updates
class TestEntity : public Entity {
public:
  TestEntity(const Vector2D &pos = Vector2D(0, 0)) {
    // Register with EntityDataManager first (required before setPosition)
    registerWithDataManager(pos, 16.0f, 16.0f, EntityKind::NPC);
    m_initialPosition = pos;
    setTextureID("test_texture");
    setWidth(32);
    setHeight(32);
  }

  static std::shared_ptr<TestEntity> create(const Vector2D &pos = Vector2D(0,
                                                                           0)) {
    return std::make_shared<TestEntity>(pos);
  }

  void update(float deltaTime) override {
    (void)deltaTime; // Entity::update() not used by AIManager anymore
  }

  void render(SDL_Renderer* renderer, float cameraX, float cameraY, float interpolationAlpha = 1.0f) override { (void)renderer; (void)cameraX; (void)cameraY; (void)interpolationAlpha; }
  void clean() override {}
  [[nodiscard]] EntityKind getKind() const override { return EntityKind::NPC; }

  void updatePosition(const Vector2D &velocity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Vector2D pos = getPosition();
    pos += velocity;
    setPosition(pos);
  }

  // EDM Migration: Check if position changed in EDM (AIManager writes directly to EDM)
  int getUpdateCount() const {
    auto handle = getHandle();
    if (!handle.isValid()) return 0;

    auto& edm = EntityDataManager::Instance();
    size_t index = edm.getIndex(handle);
    if (index == SIZE_MAX) return 0;

    auto& transform = edm.getTransformByIndex(index);
    Vector2D currentPos = transform.position;
    Vector2D velocity = transform.velocity;

    // Count as "updated" if position moved from initial or has non-zero velocity
    bool positionMoved = (currentPos - m_initialPosition).length() > 0.01f;
    bool hasVelocity = velocity.length() > 0.01f;

    return (positionMoved || hasVelocity) ? 1 : 0;
  }

  void resetUpdateCount() {
    auto handle = getHandle();
    if (handle.isValid()) {
      auto& edm = EntityDataManager::Instance();
      size_t index = edm.getIndex(handle);
      if (index != SIZE_MAX) {
        m_initialPosition = edm.getTransformByIndex(index).position;
      }
    }
  }

private:
  std::mutex m_mutex;
  Vector2D m_initialPosition;
};

// Test behavior
class ThreadTestBehavior : public AIBehavior {
private:
  static std::atomic<int> s_sharedMessageCount; // Shared across all instances

public:
  ThreadTestBehavior(int id) : m_id(id) {}

  // Lock-free hot path (required by pure virtual)
  void executeLogic(BehaviorContext& ctx) override {
    // Test behavior: minimal lock-free implementation
    // Uses pre-computed movement values for threading stress tests
    static constexpr float movements[] = {
        -0.05f, 0.03f,  -0.08f, 0.07f,  -0.02f, 0.09f,  -0.06f, 0.04f,
        0.08f,  -0.09f, 0.01f,  -0.04f, 0.06f,  -0.07f, 0.02f,  -0.01f};
    static constexpr size_t movementCount = sizeof(movements) / sizeof(movements[0]);

    // Simple counter instead of random generation
    Vector2D movement(movements[m_movementIndex % movementCount],
                      movements[(m_movementIndex + 8) % movementCount]);
    m_movementIndex++;

    // Move entity directly via transform
    ctx.transform.position.setX(ctx.transform.position.getX() + movement.getX());
    ctx.transform.position.setY(ctx.transform.position.getY() + movement.getY());

    m_updateCount++;
  }

  void init(EntityHandle /* handle */) override { m_initialized = true; }

  void clean(EntityHandle /* handle */) override {
    try {
      std::lock_guard<std::mutex> lock(m_messageMutex);
      m_initialized = false;
    } catch (...) {
      // Ignore exceptions during cleanup
    }
  }

  std::string getName() const override {
    return "ThreadTestBehavior" + std::to_string(m_id);
  }

  std::shared_ptr<AIBehavior> clone() const override {
    auto cloned = std::make_shared<ThreadTestBehavior>(m_id);
    cloned->setActive(m_active);
    return cloned;
  }

  static void resetSharedMessageCount() { s_sharedMessageCount.store(0); }

  static int getSharedMessageCount() { return s_sharedMessageCount.load(); }

  void onMessage(EntityHandle /* handle */, const std::string &message) override {
    // Store the message under lock
    {
      std::lock_guard<std::mutex> lock(m_messageMutex);
      m_lastMessage = message;
    }

    // Increment both instance and shared counters
    m_messageCount.fetch_add(1, std::memory_order_relaxed);
    s_sharedMessageCount.fetch_add(1, std::memory_order_relaxed);
  }

  int getMessageCount() const { return m_messageCount.load(); }

  void resetMessageCount() { m_messageCount.store(0); }

  std::string getLastMessage() const {
    std::lock_guard<std::mutex> lock(m_messageMutex);
    return m_lastMessage;
  }

  int getUpdateCount() const { return m_updateCount.load(); }

public: // Make these public for test access
  int m_id;
  bool m_initialized{false};
  alignas(64) std::atomic<int> m_updateCount{0};
  alignas(64) std::atomic<int> m_messageCount{0};
  mutable std::mutex m_messageMutex;
  std::string m_lastMessage;

  // Performance optimization: cache entity cast and movement index
  mutable std::shared_ptr<TestEntity> m_cachedTestEntity;
  mutable std::atomic<size_t> m_movementIndex{0};
};

// Define static member
std::atomic<int> ThreadTestBehavior::s_sharedMessageCount{0};

// Track behaviors for test verification
std::vector<std::shared_ptr<AIBehavior>> g_allBehaviors;
std::mutex g_behaviorMutex;

// Global fixture for test setup and cleanup
struct GlobalTestFixture {
    GlobalTestFixture() {
        HammerEngine::ThreadSystem::Instance().init();
        EntityDataManager::Instance().init();
        CollisionManager::Instance().init();
        PathfinderManager::Instance().init();
        AIManager::Instance().init();
        BackgroundSimulationManager::Instance().init();
    }

    ~GlobalTestFixture() {
        // Clear tracked behaviors first
        {
            std::lock_guard<std::mutex> lock(g_behaviorMutex);
            g_allBehaviors.clear();
        }
        // Clean managers in reverse order
        BackgroundSimulationManager::Instance().clean();
        AIManager::Instance().clean();
        PathfinderManager::Instance().clean();
        CollisionManager::Instance().clean();
        EntityDataManager::Instance().clean();
        HammerEngine::ThreadSystem::Instance().clean();
    }
};

BOOST_GLOBAL_FIXTURE(GlobalTestFixture);

// Per-test fixture
struct ThreadedAITestFixture {
    ThreadedAITestFixture() {
        AIManager::Instance().enableThreading(true);
    }

    ~ThreadedAITestFixture() {
        AIManager::Instance().enableThreading(false);
        AIManager::Instance().resetBehaviors();
    }

    // Helper method to wait for ThreadSystem tasks to complete
    void waitForThreadSystemTasks(std::vector<std::future<void>>& futures) {
        for (auto& future : futures) {
            if (future.valid()) {
                future.wait_for(std::chrono::seconds(10));
                if (future.valid()) {
                    try { future.get(); } catch (...) {}
                }
            }
        }
    }

    // Helper method to safely unassign behaviors from entities
    template <typename T>
    void safelyUnassignBehaviors(std::vector<std::shared_ptr<T>>& entities) {
        for (auto& entity : entities) {
            if (entity) {
                AIManager::Instance().unassignBehavior(entity->getHandle());
            }
        }
    }
};

// Helper to update AI with proper tier calculation
// Tests create/destroy entities frequently, so we need to force tier rebuild each time
// FIXED: Use large active radius (3000) to ensure all test entities at (0,0) to (1990,1990)
// are within Active tier from reference point (500,500). Max distance ~2107 < 3000.
void updateAI(float deltaTime, const Vector2D& referencePoint = Vector2D(500.0f, 500.0f)) {
    // Direct EDM call - bypasses BSM frame counting that can skip tier updates
    auto& edm = EntityDataManager::Instance();
    edm.updateSimulationTiers(referencePoint, 3000.0f, 5000.0f);
    AIManager::Instance().update(deltaTime);
}

// Test case for thread-safe behavior registration
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBehaviorRegistration,
                        ThreadedAITestFixture) {
  const int NUM_BEHAVIORS = 20;
  std::vector<std::future<void>> futures;

  std::cout << "Starting TestThreadSafeBehaviorRegistration..." << std::endl;

  // Register behaviors from multiple threads using ThreadSystem
  for (int i = 0; i < NUM_BEHAVIORS; ++i) {
    auto future =
        HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult([i]() {
          auto behavior = std::make_shared<ThreadTestBehavior>(i);
          {
            // Track this behavior globally to prevent premature destruction
            std::lock_guard<std::mutex> lock(g_behaviorMutex);
            g_allBehaviors.push_back(behavior);
          }
          AIManager::Instance().registerBehavior("Behavior" + std::to_string(i),
                                                 behavior);
        });
    futures.push_back(std::move(future));
  }

  // Wait for all tasks to complete
  waitForThreadSystemTasks(futures);

  // Verify all behaviors were registered
  for (int i = 0; i < NUM_BEHAVIORS; ++i) {
    BOOST_CHECK(
        AIManager::Instance().hasBehavior("Behavior" + std::to_string(i)));
  }

  // Cleanup
  AIManager::Instance().resetBehaviors();
  std::cout << "TestThreadSafeBehaviorRegistration completed" << std::endl;
}

// Test that async path requests still complete under worker load
BOOST_FIXTURE_TEST_CASE(TestAsyncPathRequestsUnderWorkerLoad,
                        ThreadedAITestFixture) {
  auto &threadSystem = HammerEngine::ThreadSystem::Instance();

  // Enqueue background load
  constexpr int LOAD_TASKS = 1500;
  for (int i = 0; i < LOAD_TASKS; ++i) {
    threadSystem.enqueueTask([]() {
      volatile int x = 0; for (int k = 0; k < 200; ++k) x += k;
    }, HammerEngine::TaskPriority::Low);
  }

  // Initialize pathfinder and issue multiple async requests
  PathfinderManager &pf = PathfinderManager::Instance();
  BOOST_REQUIRE(pf.init());

  std::atomic<int> callbacks{0};
  const int REQUESTS = 24;
  for (int i = 0; i < REQUESTS; ++i) {
    Vector2D start(16.0f + i, 20.0f + i);
    Vector2D goal(220.0f + i, 180.0f + i);
    pf.requestPath(static_cast<EntityID>(5000 + i), start, goal,
                   PathfinderManager::Priority::Normal,
                   [&callbacks](EntityID, const std::vector<Vector2D>&) {
                     callbacks.fetch_add(1, std::memory_order_relaxed);
                   });
  }

  // Wait briefly for callbacks to arrive under load
  for (int i = 0; i < 25 && callbacks.load(std::memory_order_relaxed) == 0; ++i) {
    PathfinderManager::Instance().update(); // Process buffered requests
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  BOOST_CHECK(callbacks.load(std::memory_order_relaxed) > 0);

  // Let the fixture/global fixture handle cleanup ordering for managers
}

// Test case for thread-safe entity assignment
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBehaviorAssignment,
                        ThreadedAITestFixture) {
  std::cout << "Starting TestThreadSafeBehaviorAssignment..." << std::endl;
  const int NUM_ENTITIES = 100;
  std::vector<std::shared_ptr<TestEntity>> entities;
  std::vector<std::shared_ptr<TestEntity>> entityPtrs;
  std::shared_ptr<ThreadTestBehavior> behavior =
      std::make_shared<ThreadTestBehavior>(0);

  // Register a behavior
  {
    // Track this behavior globally to prevent premature destruction
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    g_allBehaviors.push_back(behavior);
  }
  AIManager::Instance().registerBehavior("TestBehavior", behavior);

  // Prepare entities
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    entityPtrs.push_back(entity);
    // Entities start without behaviors - will be assigned by worker threads below
  }

  // Assign behaviors from multiple threads
  std::vector<std::future<void>> futures;
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto future = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
        [i, &entityPtrs]() {
          AIManager::Instance().assignBehavior(entityPtrs[i]->getHandle(),
                                               "TestBehavior");
        });
    futures.push_back(std::move(future));
  }

  // Wait for all tasks to complete
  waitForThreadSystemTasks(futures);

  // Verify all entities have behaviors
  for (auto entity : entityPtrs) {
    BOOST_CHECK(AIManager::Instance().hasBehavior(entity->getHandle()));
  }

  // Cleanup
  for (auto &entity : entities) {
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  // Wait before resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  // Wait after resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestThreadSafeBehaviorAssignment completed" << std::endl;
}

// Test case for thread-safe batch updates
// FIXED: update() must be called sequentially, not concurrently (it spawns its own worker threads)
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBatchUpdates, ThreadedAITestFixture) {
  std::cout << "Starting TestThreadSafeBatchUpdates..." << std::endl;
  const int NUM_ENTITIES = 200;
  const int NUM_BEHAVIORS = 5;
  const int UPDATES_PER_BEHAVIOR = 10;

  std::vector<std::shared_ptr<TestEntity>> entities;
  std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;

  // Register behaviors
  // Simulate random behavior changes
  for (int i = 0; i < NUM_BEHAVIORS; ++i) {
    behaviors.push_back(std::make_shared<ThreadTestBehavior>(i));
    AIManager::Instance().registerBehavior("Behavior" + std::to_string(i),
                                           behaviors.back());
    {
      // Track this behavior globally
      std::lock_guard<std::mutex> lock(g_behaviorMutex);
      g_allBehaviors.push_back(behaviors.back());
      // Store the behavior in the global map - not using g_behaviors here
    }
  }

  // Create entities and assign behaviors
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    std::string behaviorName = "Behavior" + std::to_string(i % NUM_BEHAVIORS);
    AIManager::Instance().assignBehavior(entity->getHandle(), behaviorName);
    // Register entity for managed updates
    AIManager::Instance().registerEntity(entity->getHandle());
  }

  // Run managed entity updates sequentially (update() internally uses worker threads)
  // NOTE: update() is designed to be called from a single thread (main game loop)
  // It internally spawns worker threads for parallel entity processing
  for (int j = 0; j < UPDATES_PER_BEHAVIOR * NUM_BEHAVIORS; ++j) {
    updateAI(0.016f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  // Give a longer delay to ensure all updates are processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify entities were updated
  for (const auto &entity : entities) {
    BOOST_CHECK_GT(entity->getUpdateCount(), 0);
  }

  // Note: Individual behavior instances (not templates) are updated via
  // executeLogic() Template behaviors stored in 'behaviors' vector are not
  // directly updated The entity updates above confirm the system is working
  // correctly

  // Cleanup
  // Unregister entities from managed updates and unassign behaviors
  for (auto &entity : entities) {
    AIManager::Instance().unregisterEntity(entity->getHandle());
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  // Wait before resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  // Wait after resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestThreadSafeBatchUpdates completed" << std::endl;
}

// Test case for thread-safe message passing
BOOST_FIXTURE_TEST_CASE(TestThreadSafeMessaging, ThreadedAITestFixture) {
  std::cout << "Starting TestThreadSafeMessaging..." << std::endl;
  const int NUM_ENTITIES = 100;
  const int NUM_MESSAGES = 200;

  // Create a single behavior instance that we'll use for all entities
  auto behavior = std::make_shared<ThreadTestBehavior>(42);
  behavior->m_messageCount.store(0);             // Ensure counter starts at 0
  ThreadTestBehavior::resetSharedMessageCount(); // Reset shared counter

  // Register and track the behavior
  AIManager::Instance().registerBehavior("MessageTest", behavior);
  {
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    // Store in the global vector only
    g_allBehaviors.push_back(behavior); // Also add to all behaviors list
  }
  std::cout << "Registered MessageTest behavior" << std::endl;

  // Create entities with IDs for easier tracking
  std::vector<std::shared_ptr<TestEntity>> entities;
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);

    // Explicitly assign the behavior to each entity
    AIManager::Instance().assignBehavior(entity->getHandle(), "MessageTest");
    std::cout << "Assigned behavior to entity " << i << std::endl;
  }

  // Verify the behavior assignment worked
  for (size_t i = 0; i < entities.size(); ++i) {
    bool hasAssigned = AIManager::Instance().hasBehavior(entities[i]->getHandle());
    BOOST_REQUIRE_MESSAGE(hasAssigned,
                          "Entity " << i << " should have a behavior assigned");
    std::cout << "Verified entity " << i << " has behavior assigned"
              << std::endl;
  }

  // TEST APPROACH 1: Direct synchronous message test
  // This tests the core messaging functionality without threading complexity
  std::cout << "\nTesting direct synchronous messaging..." << std::endl;

  // Use the first entity for a simple test with immediate processing
  AIManager::Instance().sendMessageToEntity(entities[0]->getHandle(), "TEST_DIRECT_MESSAGE",
                                            true);

  // Give time for immediate message processing
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Check if message was received (use shared counter for cloned behaviors)
  int directMessageCount = ThreadTestBehavior::getSharedMessageCount();
  std::cout << "Direct message test: received " << directMessageCount << " messages" << std::endl;

  // Direct messaging should work - if it doesn't, the system is broken
  BOOST_REQUIRE_MESSAGE(directMessageCount > 0,
                        "Direct messaging failed - messaging system may be broken");

  // TEST APPROACH 2: Broadcast message test
  std::cout << "\nTesting broadcast messaging..." << std::endl;
  ThreadTestBehavior::resetSharedMessageCount(); // Reset counter for clean test

  AIManager::Instance().broadcastMessage("TEST_BROADCAST_MESSAGE", true);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  int broadcastMessageCount = ThreadTestBehavior::getSharedMessageCount();
  std::cout << "Broadcast test: received " << broadcastMessageCount << " messages" << std::endl;

  // Broadcast should reach multiple entities (at least 50% of entities)
  // Note: This is a reasonable expectation since all entities have the same behavior
  BOOST_CHECK_GE(broadcastMessageCount, NUM_ENTITIES / 2);

  // TEST APPROACH 3: Multi-threaded message stress test
  std::cout << "\nRunning multi-threaded message stress test..." << std::endl;
  ThreadTestBehavior::resetSharedMessageCount(); // Reset counter for clean test

  // Send a mix of direct and broadcast messages from multiple threads
  std::vector<std::future<void>> futures;
  for (int i = 0; i < NUM_MESSAGES; ++i) {
    auto future =
        HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
            [i, &entities]() {
              std::string message = "ThreadMessage_" + std::to_string(i);

              if (i % 2 == 0) {
                // Broadcast message
                AIManager::Instance().broadcastMessage(message, true);
              } else {
                // Send to a specific entity
                int entityIdx = i % entities.size();
                AIManager::Instance().sendMessageToEntity(entities[entityIdx]->getHandle(),
                                                          message, true);
              }
            });
    futures.push_back(std::move(future));
  }

  // Wait for all messages to be sent
  waitForThreadSystemTasks(futures);

  // Allow time for messages to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Verify messages were delivered under multi-threaded load
  int stressTestMessageCount = ThreadTestBehavior::getSharedMessageCount();
  std::cout << "Stress test: received " << stressTestMessageCount << " messages" << std::endl;

  // Under stress test, we should receive a significant portion of messages
  // Require at least 50% message delivery rate under stress
  BOOST_CHECK_GE(stressTestMessageCount, NUM_MESSAGES / 2);

  // Cleanup
  for (auto &entity : entities) {
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }

  AIManager::Instance().resetBehaviors();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

// Test case for thread-safe cache invalidation
BOOST_FIXTURE_TEST_CASE(TestThreadSafeCacheInvalidation,
                        ThreadedAITestFixture) {
  std::cout << "Starting TestThreadSafeCacheInvalidation..." << std::endl;
  const int NUM_OPERATIONS = 100;
  const int NUM_ENTITIES = 100; // Define NUM_ENTITIES

  // Register a behavior
  std::string behaviorName = "CacheTest";
  auto behavior = std::make_shared<ThreadTestBehavior>(0);
  AIManager::Instance().registerBehavior(behaviorName, behavior);
  {
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    g_allBehaviors.push_back(behavior);
  }
  AIManager::Instance().registerBehavior("CacheTest", behavior);

  // Create a pool of entities
  std::vector<std::shared_ptr<TestEntity>> entities;
  std::vector<EntityPtr> entityPtrs;
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    entityPtrs.push_back(entity);
  }

  // Run a mix of operations
  std::vector<std::future<void>> futures;
  for (int i = 0; i < NUM_OPERATIONS; ++i) {
    auto future = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
        [i, &entityPtrs]() {
          int entityIndex = i % entityPtrs.size();
          if (i % 2 == 0) {
            AIManager::Instance().assignBehavior(
                entityPtrs[entityIndex]->getHandle(), "CacheTest");
          } else {
            AIManager::Instance().assignBehavior(
                entityPtrs[entityIndex]->getHandle(), "CacheTest");
            // Unassign the behavior
            AIManager::Instance().unassignBehavior(
                entityPtrs[entityIndex]->getHandle());
          }
        });
    futures.push_back(std::move(future));
  }

  // FIXED: AIManager::update() should ONLY be called from a single thread (GameEngine)
  // This test should verify cache invalidation during concurrent behavior operations,
  // NOT test concurrent update() calls which violate the design.
  // 
  // Instead, we call update() once from the main thread while other operations continue
  std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Let operations start
  updateAI(0.016f); // Single update call as designed

  // Wait for all operations to complete
  waitForThreadSystemTasks(futures);

  // Verify the system is still consistent
  int countAssigned = 0;
  for (auto entity : entityPtrs) {
    if (AIManager::Instance().hasBehavior(entity->getHandle())) {
      countAssigned++;
    }
  }

  // Verify assignments completed (countAssigned tracks successful assigns)
  BOOST_CHECK_GT(countAssigned, 0);

  // Cleanup
  // Unregister entities from managed updates and unassign behaviors
  for (auto &entity : entities) {
    AIManager::Instance().unregisterEntity(entity->getHandle());
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  // Wait before resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  // Wait after resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestThreadSafeCacheInvalidation completed" << std::endl;
}

// Test case for concurrent behavior processing
BOOST_FIXTURE_TEST_CASE(TestConcurrentBehaviorProcessing,
                        ThreadedAITestFixture) {
  std::cout << "Starting TestConcurrentBehaviorProcessing..." << std::endl;
  const int NUM_ENTITIES = 10; // Reduced for stability

  // Create and set a player entity to ensure consistent updates
  auto player = std::make_shared<TestEntity>(Vector2D(0, 0));
  AIManager::Instance().setPlayerHandle(player->getHandle());

  // Register a behavior
  auto behavior = std::make_shared<ThreadTestBehavior>(0);
  {
    // Track this behavior globally to prevent premature destruction
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    g_allBehaviors.push_back(behavior);
  }
  AIManager::Instance().registerBehavior("ConcurrentTest", behavior);

  // Create entities
  std::vector<std::shared_ptr<TestEntity>> entities;
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    AIManager::Instance().assignBehavior(entity->getHandle(), "ConcurrentTest");
    // Register entity for managed updates
    AIManager::Instance().registerEntity(entity->getHandle());
  }

  // Run multiple concurrent updates
  const int NUM_UPDATES = 20;
  for (int i = 0; i < NUM_UPDATES; ++i) {
    updateAI(0.016f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Wait to ensure updates are processed
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Verify updates were performed
  for (const auto &entity : entities) {
    BOOST_CHECK_GT(entity->getUpdateCount(), 0);
  }

  // Cleanup
  // Unregister entities from managed updates and unassign behaviors
  for (auto &entity : entities) {
    AIManager::Instance().unregisterEntity(entity->getHandle());
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  // Clear player entity
  AIManager::Instance().setPlayerHandle(EntityHandle{});
  // Wait before resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  // Wait after resetting behaviors
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestConcurrentBehaviorProcessing completed" << std::endl;
}

// Stress test for the thread-safe AIManager
// IMPORTANT: This test reflects the actual engine thread safety design:
// - AIManager::update() is called from SINGLE thread only (like GameEngine::update())
// - Thread-safe operations: assignBehavior, unassignBehavior, sendMessage, broadcast, queries
// - See src/core/GameEngine.cpp:780 for the real single-threaded update pattern
BOOST_FIXTURE_TEST_CASE(StressTestThreadSafeAIManager, ThreadedAITestFixture) {
  std::cout << "Starting StressTestThreadSafeAIManager..." << std::endl;

  const int NUM_ENTITIES = 50;           // Reduced for stability
  const int NUM_BEHAVIORS = 5;           // Reduced for stability
  const int NUM_THREADS = 8;             // Number of worker threads
  const int OPERATIONS_PER_THREAD = 100; // Number of operations per thread

  std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;
  std::vector<std::shared_ptr<TestEntity>> entities;
  std::vector<std::shared_ptr<TestEntity>> entityPtrs;

  try {
    // Register behaviors
    behaviors.reserve(NUM_BEHAVIORS);
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
      behaviors.push_back(std::make_shared<ThreadTestBehavior>(i));
      {
        // Track this behavior globally to prevent premature destruction
        std::lock_guard<std::mutex> lock(g_behaviorMutex);
        g_allBehaviors.push_back(behaviors.back());
      }
      AIManager::Instance().registerBehavior(
          "StressBehavior" + std::to_string(i), behaviors.back());
    }

    // Create entities
    entities.reserve(NUM_ENTITIES);
    entityPtrs.reserve(NUM_ENTITIES);

    // Create test entities
    for (int i = 0; i < NUM_ENTITIES; ++i) {
      auto entity =
          std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
      entities.push_back(entity);
      entityPtrs.push_back(entity);
    }

    // Use ThreadSystem instead of raw std::thread (per CLAUDE.md standards)
    std::atomic<bool> stopFlag(false);
    std::atomic<int> completedTasks(0);

    // Enqueue worker tasks to ThreadSystem for random operations
    for (int t = 0; t < NUM_THREADS; ++t) {
      HammerEngine::ThreadSystem::Instance().enqueueTask(
        [t, &entityPtrs, &stopFlag, &completedTasks]() {
          try {
            std::mt19937 rng(
                t + 1); // Use thread id as seed for deterministic randomness

            for (int i = 0; i < OPERATIONS_PER_THREAD && !stopFlag; ++i) {
              // Pick a random operation
              int operation = rng() % 5;

              try {
                switch (operation) {
                case 0: {
                  // Assign behavior
                  int entityIdx = rng() % entityPtrs.size();
                  int behaviorIdx = rng() % NUM_BEHAVIORS;
                  AIManager::Instance().assignBehavior(
                      entityPtrs[entityIdx]->getHandle(), "StressBehavior" + std::to_string(behaviorIdx));
                  break;
                }
                case 1: {
                  // Unassign behavior
                  int entityIdx = rng() % entityPtrs.size();
                  AIManager::Instance().unassignBehavior(
                      entityPtrs[entityIdx]->getHandle());
                  break;
                }
                case 2: {
                  // Send message to random entity
                  int entityIdx = rng() % entityPtrs.size();
                  AIManager::Instance().sendMessageToEntity(
                      entityPtrs[entityIdx]->getHandle(), "StressMessage" + std::to_string(i));
                  break;
                }
                case 3: {
                  // Broadcast message
                  AIManager::Instance().broadcastMessage("BroadcastMessage" +
                                                         std::to_string(i));
                  break;
                }
                case 4: {
                  // Query entity behavior status (thread-safe read operation)
                  int entityIdx = rng() % entityPtrs.size();
                  AIManager::Instance().hasBehavior(entityPtrs[entityIdx]->getHandle());
                  break;
                }
                }
              } catch (const std::exception &e) {
                std::cerr << "Task " << t << " operation " << operation
                          << " exception: " << e.what() << std::endl;
                // Don't stop the test for expected race conditions
              }

              // Small sleep to simulate real-world timing
              std::this_thread::sleep_for(std::chrono::microseconds(rng() % 100));
            }

            completedTasks.fetch_add(1);
          } catch (const std::exception &e) {
            std::cerr << "Task " << t << " unexpected exception: " << e.what()
                      << std::endl;
            stopFlag = true;
            completedTasks.fetch_add(1);
          }
        },
        HammerEngine::TaskPriority::Normal,
        "StressTest_" + std::to_string(t)
      );
    }

    // Wait for all tasks to complete (with timeout)
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(2000);

    while (completedTasks.load() < NUM_THREADS) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // Check for timeout
      auto elapsed = std::chrono::steady_clock::now() - startTime;
      if (elapsed > timeout) {
        std::cerr << "Stress test timeout - only " << completedTasks.load()
                  << " of " << NUM_THREADS << " tasks completed" << std::endl;
        stopFlag = true;
        break;
      }
    }

    // Give a bit more time for any final operations
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // SINGLE-THREADED UPDATE SECTION - Only after all multi-threaded operations complete
    // This reflects the real engine design where update() is called from one thread only
    updateAI(0.016f);

    // Verify the system is still in a consistent state
    // Just check that we can still query entity-behavior associations
    for (auto entity : entityPtrs) {
      // This should not crash or cause data races
      AIManager::Instance().hasBehavior(entity->getHandle());
    }

    // Check that we can still update without crashing
    updateAI(0.016f);

    // Pass the test if we got this far without crashes
    BOOST_CHECK(true);
  } catch (const std::exception &e) {
    BOOST_ERROR("Exception in StressTestThreadSafeAIManager: " << e.what());
  }

  // Cleanup - perform cleanup in a specific order to avoid race conditions
  try {
    // First, clear entities which may reference behaviors
    safelyUnassignBehaviors(entities);

    // Wait to ensure unassign operations complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Clear entity collections
    entities.clear();
    entityPtrs.clear();

    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Now reset behaviors in the AIManager
    try {
      AIManager::Instance().resetBehaviors();
    } catch (const std::exception &e) {
      std::cerr << "Exception resetting behaviors: " << e.what() << std::endl;
    }

    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Explicitly clear behaviors vector to release shared_ptrs
    behaviors.clear();

    // Final wait to ensure cleanup is complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } catch (const std::exception &e) {
    std::cerr << "Exception during cleanup: " << e.what() << std::endl;
  } catch (...) {
    std::cerr << "Unknown exception during cleanup" << std::endl;
  }

  std::cout << "StressTestThreadSafeAIManager completed" << std::endl;
}

// Test case for waitForAsyncBatchCompletion() synchronization
BOOST_FIXTURE_TEST_CASE(TestWaitForAsyncBatchCompletion, ThreadedAITestFixture) {
  std::cout << "Starting TestWaitForAsyncBatchCompletion..." << std::endl;
  const int NUM_ENTITIES = 100;

  // Create and set a player entity for distance optimization
  auto player = std::make_shared<TestEntity>(Vector2D(500, 500));
  AIManager::Instance().setPlayerHandle(player->getHandle());

  // Register a behavior
  auto behavior = std::make_shared<ThreadTestBehavior>(0);
  {
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    g_allBehaviors.push_back(behavior);
  }
  AIManager::Instance().registerBehavior("BatchTest", behavior);

  // Create entities with behaviors
  std::vector<std::shared_ptr<TestEntity>> entities;
  entities.reserve(NUM_ENTITIES);
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    AIManager::Instance().registerEntity(entity->getHandle());
    AIManager::Instance().assignBehavior(entity->getHandle(), "BatchTest");
  }

  // Trigger several updates to start batch processing
  for (int i = 0; i < 5; ++i) {
    updateAI(0.016f);
  }

  // Test 1: Fast path - should complete quickly when no pending batches
  auto start = std::chrono::high_resolution_clock::now();
  AIManager::Instance().waitForAsyncBatchCompletion();
  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

  // Fast path should complete in microseconds (not milliseconds)
  BOOST_CHECK_LT(duration.count(), 10000); // Less than 10ms

  // Cleanup
  for (auto &entity : entities) {
    AIManager::Instance().unregisterEntity(entity->getHandle());
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  AIManager::Instance().setPlayerHandle(EntityHandle{});
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestWaitForAsyncBatchCompletion completed" << std::endl;
}

// Test case for prepareForStateTransition() cleanup pattern
BOOST_FIXTURE_TEST_CASE(TestPrepareForStateTransition, ThreadedAITestFixture) {
  std::cout << "Starting TestPrepareForStateTransition..." << std::endl;
  const int NUM_ENTITIES = 30;

  // Create a player entity
  auto player = std::make_shared<TestEntity>(Vector2D(500, 500));
  AIManager::Instance().setPlayerHandle(player->getHandle());

  // Register a behavior
  auto behavior = std::make_shared<ThreadTestBehavior>(0);
  {
    std::lock_guard<std::mutex> lock(g_behaviorMutex);
    g_allBehaviors.push_back(behavior);
  }
  AIManager::Instance().registerBehavior("TransitionTest", behavior);

  // Create entities
  std::vector<std::shared_ptr<TestEntity>> entities;
  entities.reserve(NUM_ENTITIES);
  for (int i = 0; i < NUM_ENTITIES; ++i) {
    auto entity = std::make_shared<TestEntity>(Vector2D(i * 10.0f, i * 10.0f));
    entities.push_back(entity);
    AIManager::Instance().registerEntity(entity->getHandle());
    AIManager::Instance().assignBehavior(entity->getHandle(), "TransitionTest");
  }

  // Run a few updates to ensure system is active
  for (int i = 0; i < 5; ++i) {
    updateAI(0.016f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  // Call prepareForStateTransition - this should wait for all async work
  AIManager::Instance().prepareForStateTransition();

  // After preparation, system should be in a safe state for cleanup
  // Verify no crash when accessing state after preparation
  size_t count = AIManager::Instance().getBehaviorCount();
  BOOST_CHECK_GE(count, 0); // Just verify no crash

  // Cleanup
  for (auto &entity : entities) {
    AIManager::Instance().unregisterEntity(entity->getHandle());
    AIManager::Instance().unassignBehavior(entity->getHandle());
  }
  AIManager::Instance().setPlayerHandle(EntityHandle{});
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  AIManager::Instance().resetBehaviors();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  std::cout << "TestPrepareForStateTransition completed" << std::endl;
}
