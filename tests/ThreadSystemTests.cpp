/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

// Define this to make Boost.Test a header-only library
#define BOOST_TEST_MODULE ThreadSystemTests
#include <boost/test/included/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <mutex>
#include <unordered_set>

// Include the ThreadSystem header
#include "core/ThreadSystem.hpp"

// Global fixture for test setup and cleanup
struct ThreadTestFixture {
    ThreadTestFixture() {
        // Initialize the thread system before tests
        Forge::ThreadSystem::Instance().init();
    }

    ~ThreadTestFixture() {
        // Clean up the thread system after tests
        if (!Forge::ThreadSystem::Instance().isShutdown()) {
            Forge::ThreadSystem::Instance().clean();
        }
    }
};

BOOST_GLOBAL_FIXTURE(ThreadTestFixture);

BOOST_AUTO_TEST_CASE(TestThreadPoolInitialization) {
    // Check that the thread system is initialized
    BOOST_CHECK(!Forge::ThreadSystem::Instance().isShutdown());

    // Check that the thread count is reasonable (at least 1)
    unsigned int threadCount = Forge::ThreadSystem::Instance().getThreadCount();
    BOOST_CHECK_GT(threadCount, 0);

    // Output the thread count for information
    std::cout << "Thread system initialized with " << threadCount << " threads." << std::endl;
}

BOOST_AUTO_TEST_CASE(TestSimpleTaskExecution) {
    // Create an atomic flag to be set by the task
    std::atomic<bool> taskExecuted{false};

    // Submit a simple task that sets the flag
    Forge::ThreadSystem::Instance().enqueueTask([&taskExecuted]() {
        taskExecuted = true;
    });

    // Wait for a short time to allow the task to execute
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Check that the task was executed
    BOOST_CHECK(taskExecuted);
}

BOOST_AUTO_TEST_CASE(TestTaskWithResult) {
    // Submit a task that returns a value
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
        return 42;
    });

    // Wait for the result and check it
    int result = future.get();
    BOOST_CHECK_EQUAL(result, 42);
}

/*
    5k upper limit processed repeatedly. Can reach 8k, but pushing the syste, and always fails at 10k.
    Setting to 512 for default as this is what the Thread Systems default limit is.
    This means 5000 individual entities or arrays can be processing concurrently. Definitely scalable and
    flexible and more than plenty for even a very complex game or simulation.
*/

BOOST_AUTO_TEST_CASE(TestMultipleTasks) {
    // Number of tasks to create
    const int numTasks = 512;
    // Atomic counter to be incremented by each task
    std::atomic<int> counter{0};

    // Submit multiple tasks
    for (int i = 0; i < numTasks; ++i) {
        Forge::ThreadSystem::Instance().enqueueTask([&counter]() {
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            counter++;
        });
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Check that all tasks were executed
    BOOST_CHECK_EQUAL(counter, numTasks);
}

BOOST_AUTO_TEST_CASE(TestConcurrentTaskResults) {
    // Number of tasks to create
    const int numTasks = 50;

    // Vector to store futures for each task
    std::vector<std::future<int>> futures;

    // Submit tasks that each return their index
    for (int i = 0; i < numTasks; ++i) {
        futures.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([i]() -> int {
            // Simulate varying work times
            std::this_thread::sleep_for(std::chrono::milliseconds(i % 10));
            return i;
        }));
    }

    // Collect results and verify
    std::unordered_set<int> results;
    for (auto& future : futures) {
        results.insert(future.get());
    }

    // Check that we got all expected values
    BOOST_CHECK_EQUAL(results.size(), numTasks);
    for (int i = 0; i < numTasks; ++i) {
        BOOST_CHECK(results.find(i) != results.end());
    }
}

BOOST_AUTO_TEST_CASE(TestTasksWithExceptions) {
    // Submit a task that throws an exception
    auto future = Forge::ThreadSystem::Instance().enqueueTaskWithResult([]() -> int {
        throw std::runtime_error("Test exception");
        return 0; // Never reached
    });

    // Check that the exception is properly propagated
    BOOST_CHECK_THROW(future.get(), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(TestConcurrencyIsolation) {
    // Create a shared resource
    int sharedValue = 0;
    std::mutex mutex;

    // Submit tasks that access the shared resource with proper synchronization
    const int numTasks = 100;
    std::vector<std::future<void>> futures;

    for (int i = 0; i < numTasks; ++i) {
        futures.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([&sharedValue, &mutex]() -> void {
            // Properly lock the shared resource
            std::lock_guard<std::mutex> lock(mutex);
            sharedValue++;
        }));
    }

    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Verify the result
    BOOST_CHECK_EQUAL(sharedValue, numTasks);
}

BOOST_AUTO_TEST_CASE(TestBusyFlag) {
    // Check initial busy state - should typically be false before we add work
    bool initialBusy = Forge::ThreadSystem::Instance().isBusy();
    BOOST_CHECK(!initialBusy);

    // Submit a long-running task
    Forge::ThreadSystem::Instance().enqueueTask([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    // Check if system reports as busy
    bool busyDuringTask = Forge::ThreadSystem::Instance().isBusy();

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check if system reports as not busy after task completion
    bool busyAfterTask = Forge::ThreadSystem::Instance().isBusy();

    // Check that the busy flag was set correctly
    // Note: These checks might be flaky due to timing, but should generally work
    BOOST_CHECK(busyDuringTask);
    BOOST_CHECK(!busyAfterTask);
}

BOOST_AUTO_TEST_CASE(TestNestedTasks) {
    // Atomic counter to track task execution
    std::atomic<int> counter{0};

    // Submit a task that itself submits another task
    Forge::ThreadSystem::Instance().enqueueTask([&counter]() {
        counter++;

        // Submit a nested task
        Forge::ThreadSystem::Instance().enqueueTask([&counter]() {
            counter++;
        });
    });

    // Wait for both tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Check that both tasks were executed
    BOOST_CHECK_EQUAL(counter, 2);
}

BOOST_AUTO_TEST_CASE(TestLoadBalancing) {
    // Number of tasks to create
    const int numTasks = 200;

    // Keep track of which thread executed each task
    std::vector<unsigned long> threadIds(numTasks);
    std::mutex idMutex;

    // Submit tasks that record their thread ID
    std::vector<std::future<void>> futures;
    for (int i = 0; i < numTasks; ++i) {
        futures.push_back(Forge::ThreadSystem::Instance().enqueueTaskWithResult([i, &threadIds, &idMutex]() -> void {
            // Get the current thread ID
            unsigned long threadId = std::hash<std::thread::id>{}(std::this_thread::get_id());

            // Record which thread executed this task
            {
                std::lock_guard<std::mutex> lock(idMutex);
                threadIds[i] = threadId;
            }

            // Small amount of work
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));
    }

    // Wait for all tasks to complete
    for (auto& future : futures) {
        future.wait();
    }

    // Count the number of unique thread IDs used
    std::unordered_set<unsigned long> uniqueThreads;
    for (auto id : threadIds) {
        uniqueThreads.insert(id);
    }

    // Ensure that multiple threads were used (should use at least 2 threads)
    unsigned int threadCount = Forge::ThreadSystem::Instance().getThreadCount();
    // In case of single-threaded system, test is still valid
    unsigned int minExpectedThreads = (threadCount > 1) ? 2 : 1;

    BOOST_CHECK_GE(uniqueThreads.size(), minExpectedThreads);
    std::cout << "Tasks were executed on " << uniqueThreads.size() << " different threads." << std::endl;
}
