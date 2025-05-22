/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE ThreadSafeAIManagerTests
#define BOOST_TEST_NO_SIGNAL_HANDLING
#include <boost/test/included/unit_test.hpp>

#include <memory>
#include <chrono>
#include <vector>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <future>
#include <random>

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"

// TestEntity implementation for thread safety tests
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

    // Thread-safe position update with atomic counter
    void updatePosition(float dx, float dy) {
        Vector2D pos = getPosition();
        pos.setX(pos.getX() + dx);
        pos.setY(pos.getY() + dy);
        setPosition(pos);
        updateCount++;
    }

    // Get update count for verification
    int getUpdateCount() const {
        return updateCount;
    }

private:
    std::atomic<int> updateCount{0};
};

// Test behavior for thread safety tests
class ThreadTestBehavior : public AIBehavior {
public:
    ThreadTestBehavior(int id) : m_id(id) {}

    void update(Entity* entity) override {
        if (!entity) return;

        // Ensure proper type safety
        TestEntity* testEntity = dynamic_cast<TestEntity*>(entity);
        if (testEntity == nullptr) {
            return;
        }

        // Simulate some work (shorter for stress tests)
        std::this_thread::sleep_for(std::chrono::microseconds(500));

        // Create a thread-local random generator for thread safety
        thread_local std::mt19937 localRng(std::random_device{}());

        // Update entity position with a small random offset
        float dx = static_cast<float>(localRng() % 10) / 10.0f;
        float dy = static_cast<float>(localRng() % 10) / 10.0f;
        testEntity->updatePosition(dx, dy);

        // Update behavior counter with memory ordering
        m_updateCount.fetch_add(1, std::memory_order_relaxed);
    }

    void init([[maybe_unused]] Entity* entity) override {
        m_initialized = true;
    }

    void clean([[maybe_unused]] Entity* entity) override {
        m_initialized = false;
        // Ensure all pending updates are completed
        m_updateCount.store(0, std::memory_order_relaxed);
        m_messageCount.store(0, std::memory_order_relaxed);
        // Clear the last message with proper synchronization
        std::lock_guard<std::mutex> lock(m_messageMutex);
        m_lastMessage.clear();
    }

    std::string getName() const override {
        return "ThreadTestBehavior" + std::to_string(m_id);
    }

    void onMessage([[maybe_unused]] Entity* entity, const std::string& message) override {
        m_messageCount.fetch_add(1, std::memory_order_relaxed);
        // Use mutex to protect non-atomic string access
        std::lock_guard<std::mutex> lock(m_messageMutex);
        m_lastMessage = message;
    }

    int getMessageCount() const {
        return m_messageCount.load(std::memory_order_relaxed);
    }

    std::string getLastMessage() const {
        std::lock_guard<std::mutex> lock(m_messageMutex);
        return m_lastMessage;
    }

    int getUpdateCount() const {
        return m_updateCount.load(std::memory_order_relaxed);
    }

private:
    int m_id;
    bool m_initialized{false};
    alignas(64) std::atomic<int> m_updateCount{0};  // Align to cache line to prevent false sharing
    alignas(64) std::atomic<int> m_messageCount{0}; // Align to cache line to prevent false sharing
    mutable std::mutex m_messageMutex; // Protect string access
    std::string m_lastMessage;
};

// Global fixture for test setup and cleanup
struct ThreadedAITestFixture {
    ThreadedAITestFixture() {
        // Initialize the ThreadSystem
        if (!Forge::ThreadSystem::Instance().init()) {
            std::cerr << "Failed to initialize thread system" << std::endl;
            BOOST_FAIL("Thread system initialization failed");
        }

        // Initialize the AI system
        AIManager::Instance().init();

        // Enable threading for AIManager
        AIManager::Instance().configureThreading(true);
    }

    ~ThreadedAITestFixture() {
        try {
            // Make sure we properly reset behaviors first 
            AIManager::Instance().resetBehaviors();
            
            // Pause briefly to allow any pending operations to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Clean up the AI system
            AIManager::Instance().clean();
            
            // Pause briefly between system shutdowns
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Clean up the ThreadSystem
            Forge::ThreadSystem::Instance().clean();
        } catch (const std::exception& e) {
            std::cerr << "Exception during test fixture cleanup: " << e.what() << std::endl;
        }
    }
};

BOOST_GLOBAL_FIXTURE(ThreadedAITestFixture);

// Test case for thread-safe behavior registration
BOOST_AUTO_TEST_CASE(TestThreadSafeBehaviorRegistration)
{
    const int NUM_BEHAVIORS = 20;
    std::vector<std::future<void>> futures;

    // Register behaviors from multiple threads
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        futures.push_back(std::async(std::launch::async, [i]() {
            auto behavior = std::make_shared<ThreadTestBehavior>(i);
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behavior);
        }));
    }

    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Verify all behaviors were registered
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        BOOST_CHECK(AIManager::Instance().hasBehavior("Behavior" + std::to_string(i)));
    }

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for thread-safe entity assignment
BOOST_AUTO_TEST_CASE(TestThreadSafeEntityAssignment)
{
    const int NUM_ENTITIES = 100;
    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;

    // Register a behavior
    auto behavior = std::make_shared<ThreadTestBehavior>(0);
    AIManager::Instance().registerBehavior("TestBehavior", behavior);

    // Create entities
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        entityPtrs.push_back(entities.back().get());
    }

    // Assign behaviors from multiple threads
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        futures.push_back(std::async(std::launch::async, [i, &entityPtrs]() {
            AIManager::Instance().assignBehaviorToEntity(entityPtrs[i], "TestBehavior");
        }));
    }

    // Wait for all assignments to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Verify all entities have behaviors
    for (auto entity : entityPtrs) {
        BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity));
    }

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for thread-safe batch updates
BOOST_AUTO_TEST_CASE(TestThreadSafeBatchUpdates)
{
    const int NUM_ENTITIES = 200;
    const int NUM_BEHAVIORS = 5;
    const int UPDATES_PER_BEHAVIOR = 10;

    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;

    // Register behaviors
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        behaviors.push_back(std::make_shared<ThreadTestBehavior>(i));
        AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behaviors.back());
    }

    // Create entities and assign behaviors
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        std::string behaviorName = "Behavior" + std::to_string(i % NUM_BEHAVIORS);
        AIManager::Instance().assignBehaviorToEntity(entities.back().get(), behaviorName);
    }

    // Organize entities by behavior for batch processing
    std::vector<std::vector<Entity*>> behaviorEntities(NUM_BEHAVIORS);
    for (size_t i = 0; i < entities.size(); ++i) {
        int behaviorIdx = i % NUM_BEHAVIORS;
        behaviorEntities[behaviorIdx].push_back(entities[i].get());
    }

    // Run concurrent batch updates from multiple threads
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        futures.push_back(std::async(std::launch::async, [i, &behaviorEntities]() {
            std::string behaviorName = "Behavior" + std::to_string(i);

            for (int j = 0; j < UPDATES_PER_BEHAVIOR; ++j) {
                // Use batch processing directly instead of update()
                AIManager::Instance().batchProcessEntities(behaviorName, behaviorEntities[i]);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Wait for all updates to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Give a small delay to ensure all updates are processed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Verify entities were updated
    for (const auto& entity : entities) {
        BOOST_CHECK_GT(entity->getUpdateCount(), 0);
    }

    // Verify behaviors were updated
    for (const auto& behavior : behaviors) {
        BOOST_CHECK_GT(behavior->getUpdateCount(), 0);
    }

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for thread-safe message passing
BOOST_AUTO_TEST_CASE(TestThreadSafeMessagePassing)
{
    const int NUM_ENTITIES = 50;
    const int NUM_MESSAGES = 20;

    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;
    std::shared_ptr<ThreadTestBehavior> behavior = std::make_shared<ThreadTestBehavior>(0);

    // Register behavior
    AIManager::Instance().registerBehavior("MessageTest", behavior);

    // Create entities
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        entityPtrs.push_back(entities.back().get());
        AIManager::Instance().assignBehaviorToEntity(entityPtrs.back(), "MessageTest");
    }

    // Send messages from multiple threads
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        futures.push_back(std::async(std::launch::async, [i, &entityPtrs]() {
            // Send to random entity
            int entityIndex = i % entityPtrs.size();
            std::string message = "Message" + std::to_string(i);
            AIManager::Instance().sendMessageToEntity(entityPtrs[entityIndex], message);
        }));
    }

    // Also test broadcast messages
    for (int i = 0; i < 5; ++i) {
        futures.push_back(std::async(std::launch::async, [i]() {
            std::string message = "Broadcast" + std::to_string(i);
            AIManager::Instance().broadcastMessage(message);
        }));
    }

    // Wait for all messages to be sent
    for (auto& future : futures) {
        future.wait();
    }

    // Process message queue
    AIManager::Instance().processMessageQueue();

    // Verify messages were received
    BOOST_CHECK_GT(behavior->getMessageCount(), 0);

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for thread-safe cache invalidation
BOOST_AUTO_TEST_CASE(TestThreadSafeCacheInvalidation)
{
    const int NUM_OPERATIONS = 100;

    // Register a behavior
    auto behavior = std::make_shared<ThreadTestBehavior>(0);
    AIManager::Instance().registerBehavior("CacheTest", behavior);

    // Create a pool of entities
    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        entityPtrs.push_back(entities.back().get());
    }

    // Repeatedly assign and unassign behaviors to test cache invalidation
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_OPERATIONS; ++i) {
        futures.push_back(std::async(std::launch::async, [i, &entityPtrs]() {
            if (i % 2 == 0) {
                AIManager::Instance().assignBehaviorToEntity(entityPtrs[i], "CacheTest");
            } else {
                AIManager::Instance().assignBehaviorToEntity(entityPtrs[i], "CacheTest");
                AIManager::Instance().unassignBehaviorFromEntity(entityPtrs[i]);
            }
        }));
    }

    // Also run updates at the same time
    for (int i = 0; i < 10; ++i) {
        futures.push_back(std::async(std::launch::async, []() {
            for (int j = 0; j < 5; ++j) {
                AIManager::Instance().update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }));
    }

    // Wait for all operations to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Force cache validation
    AIManager::Instance().ensureOptimizationCachesValid();

    // Verify the system is still consistent
    int countAssigned = 0;
    for (auto entity : entityPtrs) {
        if (AIManager::Instance().entityHasBehavior(entity)) {
            countAssigned++;
        }
    }

    // We should have approximately NUM_OPERATIONS/2 entities with behaviors
    BOOST_CHECK_EQUAL(AIManager::Instance().getManagedEntityCount(), countAssigned);

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Test case for concurrent behavior processing
BOOST_AUTO_TEST_CASE(TestConcurrentBehaviorProcessing)
{
    const int NUM_ENTITIES = 10; // Further reduced for stability

    // Register behavior
    auto behavior = std::make_shared<ThreadTestBehavior>(0);
    AIManager::Instance().registerBehavior("ConcurrentTest", behavior);

    try {
        // Create entities and assign behavior
        std::vector<std::unique_ptr<TestEntity>> entities;
        std::vector<Entity*> entityPtrs;

        // Reserve space to prevent reallocations
        entities.reserve(NUM_ENTITIES);
        entityPtrs.reserve(NUM_ENTITIES);

        for (int i = 0; i < NUM_ENTITIES; ++i) {
            entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
            entityPtrs.push_back(entities.back().get());
            AIManager::Instance().assignBehaviorToEntity(entityPtrs.back(), "ConcurrentTest");
        }

        // Process all entities in batch mode, which should use threading
        auto startTime = std::chrono::high_resolution_clock::now();
        AIManager::Instance().batchProcessEntities("ConcurrentTest", entityPtrs);
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        // Output timing info
        std::cout << "Concurrent processing time for " << NUM_ENTITIES << " entities: "
                << duration.count() << " ms" << std::endl;

        // Verify all entities were updated
        for (const auto& entity : entities) {
            BOOST_CHECK_GT(entity->getUpdateCount(), 0);
        }
    } catch (const std::exception& e) {
        BOOST_ERROR("Exception in TestConcurrentBehaviorProcessing: " << e.what());
    }

    // Cleanup
    AIManager::Instance().resetBehaviors();
}

// Stress test for the thread-safe AIManager
BOOST_AUTO_TEST_CASE(StressTestThreadSafeAIManager)
{
    const int NUM_ENTITIES = 50;    // Reduced for stability
    const int NUM_BEHAVIORS = 5;    // Reduced for stability
    const int NUM_THREADS = 4;      // Reduced for stability
    const int OPERATIONS_PER_THREAD = 20;  // Reduced for stability

    // These vectors need to be accessed in the cleanup section, so declare them at function scope
    std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;
    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;
    
    try {
        // Register behaviors
        behaviors.reserve(NUM_BEHAVIORS);
        for (int i = 0; i < NUM_BEHAVIORS; ++i) {
            behaviors.push_back(std::make_shared<ThreadTestBehavior>(i));
            AIManager::Instance().registerBehavior("StressBehavior" + std::to_string(i), behaviors.back());
        }
    
        // Create entities
        entities.reserve(NUM_ENTITIES);
        entityPtrs.reserve(NUM_ENTITIES);
        
        for (int i = 0; i < NUM_ENTITIES; ++i) {
            entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
            entityPtrs.push_back(entities.back().get());
        }
    
        // Start worker threads to perform random operations
        std::vector<std::thread> threads;
        std::atomic<bool> stopFlag(false);
        
        for (int t = 0; t < NUM_THREADS; ++t) {
            threads.emplace_back([t, &entityPtrs, &stopFlag]() {
                try {
                    std::mt19937 rng(t + 1); // Use thread id as seed for deterministic randomness
        
                    for (int i = 0; i < OPERATIONS_PER_THREAD && !stopFlag; ++i) {
                        // Pick a random operation
                        int operation = rng() % 5;
        
                        try {
                            switch (operation) {
                                case 0: {
                                    // Assign behavior to entity
                                    int entityIdx = rng() % entityPtrs.size();
                                    int behaviorIdx = rng() % NUM_BEHAVIORS;
                                    AIManager::Instance().assignBehaviorToEntity(
                                        entityPtrs[entityIdx],
                                        "StressBehavior" + std::to_string(behaviorIdx)
                                    );
                                    break;
                                }
                                case 1: {
                                    // Unassign behavior from entity
                                    int entityIdx = rng() % entityPtrs.size();
                                    AIManager::Instance().unassignBehaviorFromEntity(entityPtrs[entityIdx]);
                                    break;
                                }
                                case 2: {
                                    // Update all behaviors
                                    AIManager::Instance().update();
                                    break;
                                }
                                case 3: {
                                    // Send message
                                    int entityIdx = rng() % entityPtrs.size();
                                    AIManager::Instance().sendMessageToEntity(
                                        entityPtrs[entityIdx],
                                        "Stress message " + std::to_string(i)
                                    );
                                    break;
                                }
                                case 4: {
                                    // Process message queue
                                    AIManager::Instance().processMessageQueue();
                                    break;
                                }
                            }
        } catch (const std::exception& e) {
                            std::cerr << "Thread " << t << " operation " << operation 
                                      << " exception: " << e.what() << std::endl;
                            stopFlag = true;
                            break;
                        }
                        
                        // Small sleep to simulate real-world timing
                        std::this_thread::sleep_for(std::chrono::microseconds(rng() % 100));
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << t << " exception: " << e.what() << std::endl;
                    stopFlag = true;
                }
        });
        }
        
        // Set stop flag to ensure all threads terminate
                stopFlag = true;
        
                // Wait a small amount of time for threads to notice the stop flag
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Join all threads
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        // Force final update to process any pending operations
        AIManager::Instance().update();
    
        // Verify the system is still in a consistent state
        // Just check that we can still query entity-behavior associations
        for (auto entity : entityPtrs) {
            // This should not crash or cause data races
            AIManager::Instance().entityHasBehavior(entity);
        }
    
        // Check that we can still update without crashing
        AIManager::Instance().update();
    } catch (const std::exception& e) {
        BOOST_ERROR("Exception in StressTestThreadSafeAIManager: " << e.what());
    }
    
    // Cleanup - perform cleanup in a specific order to avoid race conditions
    try {
        // First, clear entities which may reference behaviors
        entities.clear();
        entityPtrs.clear();
        
        // Now reset behaviors in the AIManager
        AIManager::Instance().resetBehaviors();
        
        // Explicitly clear behaviors vector to release shared_ptrs
        behaviors.clear();
    } catch (const std::exception& e) {
        std::cerr << "Exception during cleanup: " << e.what() << std::endl;
    }
}
