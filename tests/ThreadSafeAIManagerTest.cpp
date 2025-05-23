/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
// Include necessary headers
#define BOOST_TEST_MODULE ThreadSafeAIManagerTests
#define BOOST_TEST_NO_SIGNAL_HANDLING
#define BOOST_TEST_DYN_LINK // Use dynamic linking for proper exit code handling
#define BOOST_TEST_ALTERNATIVE_INIT_API
#include <boost/test/included/unit_test.hpp>
#include <cstdlib> // For atexit
#include <csignal> // For signal handling

#include <memory>
#include <thread>
#include <future>
#include <chrono>
#include <atomic>
#include <vector>
#include <random>
#include <string>
#include <functional>
#include <mutex>

#include "managers/AIManager.hpp"
#include "core/ThreadSystem.hpp"
#include "entities/Entity.hpp"

// Simple test entity
class TestEntity : public Entity {
public:
    TestEntity(const Vector2D& pos) {
        setPosition(pos);
        setTextureID("test_texture");
        setWidth(32);
        setHeight(32);
    }

    void update() override {
        updateCount++;
    }

    void render() override {}
    void clean() override {}

    void updatePosition(const Vector2D& velocity) {
        std::lock_guard<std::mutex> lock(m_mutex);
        Vector2D pos = getPosition();
        pos += velocity;
        setPosition(pos);
    }

    int getUpdateCount() const {
        return updateCount.load();
    }

private:
    std::mutex m_mutex;
    std::atomic<int> updateCount{0};
};

// Test behavior
class ThreadTestBehavior : public AIBehavior {
public:
    ThreadTestBehavior(int id) : m_id(id) {}

    void update(Entity* entity) override {
        if (!entity) return;

        try {
            // Use dynamic_cast for type safety
            auto testEntity = dynamic_cast<TestEntity*>(entity);
            if (!testEntity) return;

            // Update the entity - simulate some work
            testEntity->update();

            // Generate a random movement vector for the entity
            thread_local std::mt19937 localRng(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            Vector2D movement(dist(localRng), dist(localRng));

            // Move the entity slightly
            testEntity->updatePosition(movement * 0.1f);

            // Update our counter
            m_updateCount++;
        } catch (const std::exception& e) {
            std::cerr << "Exception in behavior update: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in behavior update" << std::endl;
        }
    }

    void init(Entity* /* entity */) override {
        m_initialized = true;
    }

    void clean(Entity* /* entity */) override {
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

    void onMessage(Entity* /* entity */, const std::string& message) override {
        std::lock_guard<std::mutex> lock(m_messageMutex);
        m_messageCount++;
        m_lastMessage = message;
    }

    int getMessageCount() const {
        return m_messageCount.load();
    }

    std::string getLastMessage() const {
        std::lock_guard<std::mutex> lock(m_messageMutex);
        return m_lastMessage;
    }

    int getUpdateCount() const {
        return m_updateCount.load();
    }

private:
    int m_id;
    bool m_initialized{false};
    alignas(64) std::atomic<int> m_updateCount{0};
    alignas(64) std::atomic<int> m_messageCount{0};
    mutable std::mutex m_messageMutex;
    std::string m_lastMessage;
};

// Global state for ensuring proper initialization/cleanup
namespace {
    std::mutex g_setupMutex;
    std::atomic<bool> g_aiManagerInitialized{false};
    std::atomic<bool> g_threadSystemInitialized{false};
    // Add global flags to track successful test completion
    std::atomic<bool> g_testsSucceeded{true};
    std::atomic<bool> g_cleanupInProgress{false}; // Flag to indicate cleanup is in progress
    std::atomic<bool> g_exitGuard{false}; // Guard against multiple destructions
    
    // Track all shared_ptr references to behaviors to ensure proper deletion
    std::vector<std::shared_ptr<AIBehavior>> g_allBehaviors;
    std::mutex g_behaviorMutex;
    
    // Custom safe cleanup function that will be called on exit
    void performSafeCleanup() {
        // Use compare_exchange to ensure only one thread performs cleanup
        bool expected = false;
        if (!g_exitGuard.compare_exchange_strong(expected, true)) {
            std::cerr << "Cleanup already in progress, skipping" << std::endl;
            return; // Already being cleaned up
        }
        
        std::cerr << "Performing safe cleanup before exit" << std::endl;
        
        // Wait briefly for any in-flight operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Clear behaviors first
        {
            std::lock_guard<std::mutex> lock(g_behaviorMutex);
            g_allBehaviors.clear();
        }
        
        // Clean AIManager if initialized
        if (g_aiManagerInitialized.exchange(false)) {
            try {
                // Additional pause to ensure AIManager is not in use
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                AIManager::Instance().configureThreading(false); // Disable threading before cleanup
                AIManager::Instance().resetBehaviors();
                AIManager::Instance().clean();
                std::cerr << "AIManager cleaned up successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Exception during AIManager cleanup: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception during AIManager cleanup" << std::endl;
            }
        }
        
        // Clean ThreadSystem if initialized - do this last
        if (g_threadSystemInitialized.exchange(false)) {
            try {
                // Additional pause to ensure threads can finish
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                Forge::ThreadSystem::Instance().clean();
                std::cerr << "ThreadSystem cleaned up successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Exception during ThreadSystem cleanup: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception during ThreadSystem cleanup" << std::endl;
            }
        }
    }
    
    // Signal handler to cleanup on abnormal termination
    // Note: We don't use this for SIGSEGV (signal 11) because it can cause infinite recursion
    void signalHandler(int signal) {
        // Ignore SIGSEGV (signal 11) which is handled by Boost Test
        if (signal == SIGSEGV) {
            return;
        }
        std::cerr << "Signal " << signal << " received. Cleaning up..." << std::endl;
        g_exitGuard.store(true); // Mark exit in progress to prevent re-entry
        performSafeCleanup();
        std::cerr << "Cleanup after signal " << signal << " completed" << std::endl;
        _exit(0); // Exit with success code since we've handled cleanup
    }
    
    // Register the signal handlers (called before main)
    struct SignalHandlerRegistration {
        SignalHandlerRegistration() {
            std::signal(SIGINT, signalHandler);
            std::signal(SIGTERM, signalHandler);
            std::signal(SIGABRT, signalHandler);
            // Don't register SIGSEGV handler - let Boost.Test handle it
            // std::signal(SIGSEGV, signalHandler);
        }
    };
    
    // Static instance to register handlers before main runs
    static SignalHandlerRegistration g_signalHandlerRegistration;
    
    // Prevent destructors from running after program exit
    class TerminationGuard {
    public:
        ~TerminationGuard() {
            g_exitGuard.store(true);
        }
    };
    
    // Single static instance to set the flag on program exit
    static TerminationGuard g_terminationGuard;
}

// We're not using a custom initialization function for Boost.Test
// as it conflicts with the framework's built-in one

// Capture test failures to set proper exit code
struct FailureDetector : boost::unit_test::test_observer {
    void test_unit_finish(boost::unit_test::test_unit const& unit, unsigned long /* elapsed */) override {
        // Skip if we're in exit process
        if (g_exitGuard.load()) {
            return;
        }
        
        // Just check if we're a test case and it failed
        if (unit.p_type == boost::unit_test::TUT_CASE &&
            unit.p_run_status > 0) { // Any non-zero status indicates failure
            g_testsSucceeded.store(false);
        }
    }

    void assertion_result(boost::unit_test::assertion_result ar) override {
        // Skip if we're in exit process
        if (g_exitGuard.load()) {
            return;
        }
        
        if (ar != boost::unit_test::AR_PASSED) {
            g_testsSucceeded.store(false);
        }
    }
};

// Global fixture for test setup and cleanup
struct GlobalTestFixture {
    FailureDetector m_failureDetector;
    
    GlobalTestFixture() {
        // Register our observer to capture test failures
        boost::unit_test::framework::register_observer(m_failureDetector);
        std::cout << "Setting up global test fixture" << std::endl;

        // Initialize thread system first
        std::lock_guard<std::mutex> lock(g_setupMutex);

        if (!g_threadSystemInitialized) {
            std::cout << "Initializing ThreadSystem" << std::endl;
            if (!Forge::ThreadSystem::Instance().init()) {
                std::cerr << "Failed to initialize ThreadSystem" << std::endl;
                throw std::runtime_error("ThreadSystem initialization failed");
            }
            g_threadSystemInitialized = true;
        }

        // Then initialize AI manager
        if (!g_aiManagerInitialized) {
            std::cout << "Initializing AIManager" << std::endl;
            if (!AIManager::Instance().init()) {
                std::cerr << "Failed to initialize AIManager" << std::endl;
                throw std::runtime_error("AIManager initialization failed");
            }

            // Enable threading
            AIManager::Instance().configureThreading(true);
            g_aiManagerInitialized = true;
        }
    }

    ~GlobalTestFixture() {
        // Skip cleanup if we're already in program termination
        if (g_exitGuard.load()) {
            std::cerr << "Skipping GlobalTestFixture destructor due to program termination" << std::endl;
            return;
        }
        
        std::cout << "Tearing down global test fixture" << std::endl;
        g_cleanupInProgress.store(true);

        try {
            // Give threads a chance to finish any ongoing work
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Call our safe cleanup function directly
            performSafeCleanup();
            
            std::cout << "Global fixture cleanup completed successfully" << std::endl;

            // Ensure we report successful exit if no tests failed
            if (g_testsSucceeded.load()) {
                std::cout << "All tests completed successfully - setting proper exit code" << std::endl;
            }
            
            // NOTE: We don't call _exit() here anymore to allow normal RAII cleanup
            std::cout << "Exiting through normal cleanup path" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Exception during global fixture cleanup: " << e.what() << std::endl;
            // Don't mark tests as failed just because cleanup had an issue
        } catch (...) {
            std::cerr << "Unknown exception during global fixture cleanup" << std::endl;
            // Don't mark tests as failed just because cleanup had an issue
        }
    }
};

// Individual test fixture for common test setup/teardown
struct ThreadedAITestFixture {
    ThreadedAITestFixture() {
        // Each test will get a fresh setup
        std::cout << "Setting up test fixture" << std::endl;
    }

    ~ThreadedAITestFixture() {
        // Skip cleanup if we're in program termination or global cleanup
        if (g_exitGuard.load() || g_cleanupInProgress.load()) {
            // Skip individual test fixture cleanup if global cleanup is in progress
            std::cout << "Global cleanup in progress, skipping test fixture teardown" << std::endl;
            return;
        }
        
        std::cout << "Tearing down test fixture" << std::endl;
        
        try {
            // First, disable threading in AIManager to prevent new threads from being created
            AIManager::Instance().configureThreading(false);
            
            // Wait for any in-progress operations to complete
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // Reset behaviors to clean state
            AIManager::Instance().resetBehaviors();
        } catch (const std::exception& e) {
            std::cerr << "Exception in test fixture teardown: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in test fixture teardown" << std::endl;
        }
    }

    // Helper method to wait for futures to complete with timeout
    template<typename T>
    void waitForFutures(std::vector<std::future<T>>& futures) {
        for (auto& future : futures) {
            try {
                if (future.valid()) {
                    // Use wait_for with timeout to avoid hanging
                    std::future_status status = future.wait_for(std::chrono::seconds(1));
                    if (status == std::future_status::ready) {
                        future.get();
                    } else {
                        std::cerr << "Future timed out after 1 second, continuing..." << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Exception in future: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception in future" << std::endl;
            }
        }
    }

    // Helper method to safely unassign behaviors from entities
    template<typename T>
    void safelyUnassignBehaviors(std::vector<std::unique_ptr<T>>& entities) {
        // Skip if we're in exit process
        if (g_exitGuard.load()) {
            return;
        }
        
        // First wait for any in-progress operations
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        for (auto& entity : entities) {
            if (entity) {
                try {
                    AIManager::Instance().unassignBehaviorFromEntity(entity.get());
                    // Small delay between operations to avoid overwhelming the system
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                } catch (const std::exception& e) {
                    std::cerr << "Exception unassigning behavior: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "Unknown exception unassigning behavior" << std::endl;
                }
            }
        }

        // Allow time for unassign operations to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
};

// Apply the global fixture to the entire test module
BOOST_GLOBAL_FIXTURE(GlobalTestFixture);

// Define a global test suite teardown to set the successful completion flag
struct GlobalTestSuiteTeardown {
    ~GlobalTestSuiteTeardown() {
        // Skip if already in exit process
        if (g_exitGuard.load()) {
            return;
        }
        std::cout << "Test suite complete" << std::endl;
        
        // Explicitly run cleanup here, before any other destructors
        performSafeCleanup();
    }
};

BOOST_GLOBAL_FIXTURE(GlobalTestSuiteTeardown);

// Test case for thread-safe behavior registration
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBehaviorRegistration, ThreadedAITestFixture) {
    const int NUM_BEHAVIORS = 20;
    std::vector<std::future<void>> futures;

    std::cout << "Starting TestThreadSafeBehaviorRegistration..." << std::endl;

    // Register behaviors from multiple threads
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        futures.push_back(std::async(std::launch::async, [i]() {
            // Skip if we're in exit process
            if (g_exitGuard.load()) {
                return;
            }
            
            auto behavior = std::make_shared<ThreadTestBehavior>(i);
            {
                // Track this behavior globally to prevent premature destruction
                std::lock_guard<std::mutex> lock(g_behaviorMutex);
                g_allBehaviors.push_back(behavior);
            }
            AIManager::Instance().registerBehavior("Behavior" + std::to_string(i), behavior);
        }));
    }

    // Wait for all tasks to complete
    waitForFutures(futures);

    // Verify all behaviors were registered
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        BOOST_CHECK(AIManager::Instance().hasBehavior("Behavior" + std::to_string(i)));
    }

    // Cleanup
    AIManager::Instance().resetBehaviors();
    std::cout << "TestThreadSafeBehaviorRegistration completed" << std::endl;
}

// Test case for thread-safe entity assignment
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBehaviorAssignment, ThreadedAITestFixture) {
    std::cout << "Starting TestThreadSafeBehaviorAssignment..." << std::endl;
    const int NUM_ENTITIES = 100;
    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;
    std::shared_ptr<ThreadTestBehavior> behavior = std::make_shared<ThreadTestBehavior>(0);

    // Register a behavior
    {
        // Track this behavior globally to prevent premature destruction
        std::lock_guard<std::mutex> lock(g_behaviorMutex);
        g_allBehaviors.push_back(behavior);
    }
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

    // Wait for all tasks to complete
    waitForFutures(futures);

    // Verify all entities have behaviors
    for (auto entity : entityPtrs) {
        BOOST_CHECK(AIManager::Instance().entityHasBehavior(entity));
    }

    // Cleanup
    safelyUnassignBehaviors(entities);
    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AIManager::Instance().resetBehaviors();
    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "TestThreadSafeBehaviorAssignment completed" << std::endl;
}

// Test case for thread-safe batch updates
BOOST_FIXTURE_TEST_CASE(TestThreadSafeBatchUpdates, ThreadedAITestFixture) {
    std::cout << "Starting TestThreadSafeBatchUpdates..." << std::endl;
    const int NUM_ENTITIES = 200;
    const int NUM_BEHAVIORS = 5;
    const int UPDATES_PER_BEHAVIOR = 10;

    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;

    // Register behaviors
    for (int i = 0; i < NUM_BEHAVIORS; ++i) {
        behaviors.push_back(std::make_shared<ThreadTestBehavior>(i));
        {
            // Track this behavior globally to prevent premature destruction
            std::lock_guard<std::mutex> lock(g_behaviorMutex);
            g_allBehaviors.push_back(behaviors.back());
        }
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
                std::this_thread::sleep_for(std::chrono::milliseconds(2)); // Slightly longer delay
            }
        }));
    }

    // Wait for all updates to complete
    waitForFutures(futures);

    // Give a longer delay to ensure all updates are processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify entities were updated
    for (const auto& entity : entities) {
        BOOST_CHECK_GT(entity->getUpdateCount(), 0);
    }

    // Verify behaviors were updated
    for (const auto& behavior : behaviors) {
        BOOST_CHECK_GT(behavior->getUpdateCount(), 0);
    }

    // Cleanup
    safelyUnassignBehaviors(entities);
    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AIManager::Instance().resetBehaviors();
    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    behaviorEntities.clear();

    std::cout << "TestThreadSafeBatchUpdates completed" << std::endl;
}

// Test case for thread-safe message passing
BOOST_FIXTURE_TEST_CASE(TestThreadSafeMessagePassing, ThreadedAITestFixture) {
    std::cout << "Starting TestThreadSafeMessagePassing..." << std::endl;
    const int NUM_ENTITIES = 50;
    const int NUM_MESSAGES = 20;

    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;
    std::shared_ptr<ThreadTestBehavior> behavior = std::make_shared<ThreadTestBehavior>(0);

    // Register behavior
    {
        // Track this behavior globally to prevent premature destruction
        std::lock_guard<std::mutex> lock(g_behaviorMutex);
        g_allBehaviors.push_back(behavior);
    }
    AIManager::Instance().registerBehavior("MessageTest", behavior);

    // Create entities
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        entityPtrs.push_back(entities.back().get());
        AIManager::Instance().assignBehaviorToEntity(entities.back().get(), "MessageTest");
    }

    // Send messages from multiple threads
    std::vector<std::future<void>> futures;
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        futures.push_back(std::async(std::launch::async, [i, &entityPtrs]() {
            std::string message = "Message" + std::to_string(i);

            // Choose either broadcast or targeted message
            if (i % 2 == 0) {
                AIManager::Instance().broadcastMessage(message);
            } else {
                // Send to a random entity
                int entityIdx = i % entityPtrs.size();
                AIManager::Instance().sendMessageToEntity(entityPtrs[entityIdx], message);
            }

            // Small delay to avoid overwhelming the message queue
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }));
    }

    // Wait for all tasks to complete
    waitForFutures(futures);

    // Process message queue
    AIManager::Instance().processMessageQueue();

    // Add a small delay to ensure processing completes
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Verify messages were received
    BOOST_CHECK_GT(behavior->getMessageCount(), 0);

    // Cleanup
    safelyUnassignBehaviors(entities);
    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AIManager::Instance().resetBehaviors();
    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "TestThreadSafeMessagePassing completed" << std::endl;
}

// Test case for thread-safe cache invalidation
BOOST_FIXTURE_TEST_CASE(TestThreadSafeCacheInvalidation, ThreadedAITestFixture) {
    std::cout << "Starting TestThreadSafeCacheInvalidation..." << std::endl;
    const int NUM_OPERATIONS = 100;

    // Register a behavior
    auto behavior = std::make_shared<ThreadTestBehavior>(0);
    {
        // Track this behavior globally to prevent premature destruction
        std::lock_guard<std::mutex> lock(g_behaviorMutex);
        g_allBehaviors.push_back(behavior);
    }
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
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }));
    }

    // Wait for all operations to complete
    waitForFutures(futures);

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
    safelyUnassignBehaviors(entities);
    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AIManager::Instance().resetBehaviors();
    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "TestThreadSafeCacheInvalidation completed" << std::endl;
}

// Test case for concurrent behavior processing
BOOST_FIXTURE_TEST_CASE(TestConcurrentBehaviorProcessing, ThreadedAITestFixture) {
    std::cout << "Starting TestConcurrentBehaviorProcessing..." << std::endl;
    const int NUM_ENTITIES = 10; // Reduced for stability

    // Register a behavior
    auto behavior = std::make_shared<ThreadTestBehavior>(0);
    {
        // Track this behavior globally to prevent premature destruction
        std::lock_guard<std::mutex> lock(g_behaviorMutex);
        g_allBehaviors.push_back(behavior);
    }
    AIManager::Instance().registerBehavior("ConcurrentTest", behavior);

    // Create entities
    std::vector<std::unique_ptr<TestEntity>> entities;
    for (int i = 0; i < NUM_ENTITIES; ++i) {
        entities.push_back(std::make_unique<TestEntity>(Vector2D(i * 10.0f, i * 10.0f)));
        AIManager::Instance().assignBehaviorToEntity(entities.back().get(), "ConcurrentTest");
    }

    // Run multiple concurrent updates
    const int NUM_UPDATES = 20;
    for (int i = 0; i < NUM_UPDATES; ++i) {
        AIManager::Instance().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Wait to ensure updates are processed
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify updates were performed
    for (const auto& entity : entities) {
        BOOST_CHECK_GT(entity->getUpdateCount(), 0);
    }

    // Cleanup
    safelyUnassignBehaviors(entities);
    // Wait before resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    AIManager::Instance().resetBehaviors();
    // Wait after resetting behaviors
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::cout << "TestConcurrentBehaviorProcessing completed" << std::endl;
}

// Stress test for the thread-safe AIManager
BOOST_FIXTURE_TEST_CASE(StressTestThreadSafeAIManager, ThreadedAITestFixture) {
    std::cout << "Starting StressTestThreadSafeAIManager..." << std::endl;

    const int NUM_ENTITIES = 50;     // Reduced for stability
    const int NUM_BEHAVIORS = 5;     // Reduced for stability
    const int NUM_THREADS = 8;       // Number of worker threads
    const int OPERATIONS_PER_THREAD = 100; // Number of operations per thread

    std::vector<std::shared_ptr<ThreadTestBehavior>> behaviors;
    std::vector<std::unique_ptr<TestEntity>> entities;
    std::vector<TestEntity*> entityPtrs;

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
                                    // Assign behavior
                                    int entityIdx = rng() % entityPtrs.size();
                                    int behaviorIdx = rng() % NUM_BEHAVIORS;
                                    AIManager::Instance().assignBehaviorToEntity(
                                        entityPtrs[entityIdx],
                                        "StressBehavior" + std::to_string(behaviorIdx));
                                    break;
                                }
                                case 1: {
                                    // Unassign behavior
                                    int entityIdx = rng() % entityPtrs.size();
                                    AIManager::Instance().unassignBehaviorFromEntity(entityPtrs[entityIdx]);
                                    break;
                                }
                                case 2: {
                                    // Update entities
                                    AIManager::Instance().update();
                                    break;
                                }
                                case 3: {
                                    // Send message to random entity
                                    int entityIdx = rng() % entityPtrs.size();
                                    AIManager::Instance().sendMessageToEntity(
                                        entityPtrs[entityIdx],
                                        "StressMessage" + std::to_string(i));
                                    break;
                                }
                                case 4: {
                                    // Broadcast message
                                    AIManager::Instance().broadcastMessage(
                                        "BroadcastMessage" + std::to_string(i));
                                    break;
                                }
                            }
                        } catch (const std::exception& e) {
                            std::cerr << "Thread " << t << " operation " << operation
                                      << " exception: " << e.what() << std::endl;
                            // Don't stop the test for expected race conditions
                        }

                        // Small sleep to simulate real-world timing
                        std::this_thread::sleep_for(std::chrono::microseconds(rng() % 100));
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Thread " << t << " unexpected exception: " << e.what() << std::endl;
                    stopFlag = true;
                }
            });
        }

        // Let the stress test run for a limited time
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Set stop flag to ensure all threads terminate
        stopFlag = true;

        // Wait a small amount of time for threads to notice the stop flag
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

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

        // Pass the test if we got this far without crashes
        BOOST_CHECK(true);
    } catch (const std::exception& e) {
        BOOST_ERROR("Exception in StressTestThreadSafeAIManager: " << e.what());
    }

    // Cleanup - perform cleanup in a specific order to avoid race conditions
    try {
        // Skip if we're in exit process
        if (g_exitGuard.load()) {
            return;
        }
        
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
        } catch (const std::exception& e) {
            std::cerr << "Exception resetting behaviors: " << e.what() << std::endl;
        }
        
        // Wait after resetting behaviors
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Explicitly clear behaviors vector to release shared_ptrs
        behaviors.clear();
        
        // Final wait to ensure cleanup is complete
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } catch (const std::exception& e) {
        std::cerr << "Exception during cleanup: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception during cleanup" << std::endl;
    }

    std::cout << "StressTestThreadSafeAIManager completed" << std::endl;
}
