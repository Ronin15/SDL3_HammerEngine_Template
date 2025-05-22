/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef THREAD_SYSTEM_HPP
#define THREAD_SYSTEM_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <deque>

#include <thread>

#include <SDL3/SDL.h>
#include <boost/container/small_vector.hpp>

namespace Forge {

/**
 * @brief Thread-safe task queue for the worker thread pool
 *
 * This class provides a thread-safe queue for tasks to be executed by
 * the worker threads. It handles synchronization and provides methods
 * to push, pop, and manage tasks.
 *
 * The queue automatically grows as needed, but can also have capacity
 * reserved in advance for better performance when submitting large
 * numbers of tasks at once.
 */
class TaskQueue {

public:
    /**
     * @brief Construct a new Task Queue
     *
     * @param initialCapacity Initial capacity to reserve (default: 256)
     */
    TaskQueue(size_t initialCapacity = 256) {
        // Store the desired capacity
        m_desiredCapacity = initialCapacity;
        // std::deque doesn't support reserve, so we just track the capacity ourselves
    }

    void push(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            taskStorage.push_back(std::move(task));
        }
        condition.notify_one();
    }

    bool pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] { return stopping || !taskStorage.empty(); });

        if (stopping && taskStorage.empty()) {
            return false;
        }

        task = std::move(taskStorage.front());
        taskStorage.pop_front();
        return true;
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stopping = true;
            // Clear any remaining tasks to avoid processing them during shutdown
            taskStorage.clear();
        }
        // Wake up all waiting threads to check the stopping flag
        condition.notify_all();
    }

    bool isEmpty() const {
        std::unique_lock<std::mutex> lock(queueMutex);
        return taskStorage.empty();
    }

    // Reserve capacity for the task queue to reduce memory reallocations
    void reserve(size_t capacity) {
        // std::deque doesn't have reserve, so we'll just save the desired capacity
        // and potentially pre-allocate in the future
        std::unique_lock<std::mutex> lock(queueMutex);
        m_desiredCapacity = capacity;
    }

    // Get the current capacity of the task queue
    size_t capacity() const {
        // std::deque doesn't have capacity(), return our desired capacity instead
        std::unique_lock<std::mutex> lock(queueMutex);
        return m_desiredCapacity;
    }

    // Get the current size of the task queue
    size_t size() const {
        std::unique_lock<std::mutex> lock(queueMutex);
        return taskStorage.size();
    }

    private:
        std::deque<std::function<void()>> taskStorage{};
        mutable std::mutex queueMutex{};  // Must use std::mutex with condition_variable
        std::condition_variable condition{};
        std::atomic<bool> stopping{false};
        size_t m_desiredCapacity{256}; // Track desired capacity ourselves
};

// Thread pool for managing worker threads
class ThreadPool {

public:
    ThreadPool(size_t numThreads, size_t queueCapacity = 256) {
        // Reserve capacity in the task queue before creating worker threads
        taskQueue.reserve(queueCapacity);

        m_workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this] { workerThread(); });
        }
    }

    ~ThreadPool() {
        // Signal all threads to stop after finishing current tasks
        isRunning.store(false, std::memory_order_release);
        taskQueue.stop();

        // Wait for all worker threads to finish and join them
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                try {
                    worker.join();
                } catch (const std::exception& e) {
                    // Log the error but continue shutdown
                    std::cout << "Forge Game Engine - Error joining thread!: " << e.what() << std::endl;
                }
            }
        }
        // Clear the worker threads
        m_workers.clear();
    }

    void enqueue(std::function<void()> task) {
        taskQueue.push(std::move(task));
    }

    bool busy() const {
        return !taskQueue.isEmpty();
    }

    // Access the task queue for capacity management
    TaskQueue& getTaskQueue() {
        return taskQueue;
    }

    const TaskQueue& getTaskQueue() const {
        return taskQueue;
    }

    template<class F, class... Args>
    auto enqueueWithResult(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();
        enqueue([task](){ (*task)(); });
        return result;
    }
    private:
            boost::container::small_vector<std::thread, 16> m_workers{}; // Optimized for up to 16 threads without heap allocation
            TaskQueue taskQueue{};
            std::atomic<bool> isRunning{true};
            mutable std::mutex m_mutex{}; // For thread-safe access to members

        void workerThread() {
            std::function<void()> task;
            while (isRunning.load(std::memory_order_acquire)) {
                if (taskQueue.pop(task)) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::cout << "Forge Game Engine - Error in worker thread: " << e.what() << std::endl;
                                } catch (...) {
                                    std::cout << "Forge Game Engine - Unknown error in worker thread" << std::endl;
                    }
                }
            }
        }
};

// Singleton Thread System Manager
class ThreadSystem {

public:
    // Task queue settings
    static constexpr size_t DEFAULT_QUEUE_CAPACITY = 512;

    static ThreadSystem& Instance() {
        static ThreadSystem instance;
        return instance;
    }

    /**
     * @brief Check if the ThreadSystem has been initialized
     * @return True if the ThreadSystem has been initialized, false otherwise
     */
    static bool Exists() {
        return !Instance().isShutdown();
    }

    void clean() {
        std::cout << "Forge Game Engine - ThreadSystem resources cleaned!" << std::endl;
        // Set shutdown flag first so any new accesses will be rejected
        m_isShutdown.store(true, std::memory_order_release);

        if (mp_threadPool) {
            // Avoid potential deadlock by using a timeout
            const int MAX_WAIT_MS = 5000; // 5 seconds max wait
            int waited = 0;

            while (mp_threadPool->busy() && waited < MAX_WAIT_MS) {
                SDL_Delay(10); // Short delay to avoid busy waiting
                waited += 10;
            }

            // If still busy after timeout, log a warning
            if (mp_threadPool->busy()) {
                std::cout << "Forge Game Engine - Warning: ThreadSystem shutdown with pending tasks!" << std::endl;
            }

            delete mp_threadPool;
            mp_threadPool = nullptr;
        }
    }

    ~ThreadSystem() {
        if (!m_isShutdown) {
            clean();
        }
    }

    /**
     * @brief Initialize the ThreadSystem
     *
     * This method initializes the thread pool with an optimal number of worker
     * threads based on the hardware and a default task queue capacity.
     * After initialization, the task queue can grow dynamically as needed.
     *
     * @param queueCapacity Initial capacity for the task queue (default: 512)
     * @return true if initialization succeeded, false otherwise
     */
    bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY) {
        // If already shutdown, don't allow re-initialization
        if (m_isShutdown.load(std::memory_order_acquire)) {
            std::cout << "Forge Game Engine - Error: Attempted to use ThreadSystem after shutdown!" << std::endl;
            return false;
        }

        // Set queue capacity
        m_queueCapacity = queueCapacity;

        // Determine optimal thread count (leave one for main thread)
        m_numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        mp_threadPool = new ThreadPool(m_numThreads, m_queueCapacity);
        return mp_threadPool != nullptr;
    }

    /**
     * @brief Enqueue a task for execution by the thread pool
     *
     * This method adds a task to the thread pool's queue for execution.
     * The task will be executed by one of the worker threads as soon as
     * a thread becomes available. Tasks are executed in approximately
     * the order they are submitted.
     *
     * @param task The task to execute
     */
    void enqueueTask(std::function<void()> task) {
        // If shutdown or no thread pool, reject the task
        if (m_isShutdown.load(std::memory_order_acquire) || !mp_threadPool) {
            std::cerr << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown!" << std::endl;
            return;
        }
        mp_threadPool->enqueue(std::move(task));
    }

    /**
     * @brief Enqueue a task that returns a result
     *
     * This method adds a task to the thread pool and returns a future that
     * can be used to retrieve the result. The task will be executed by one
     * of the worker threads as soon as a thread becomes available.
     *
     * @param f The function to execute
     * @param args The arguments to pass to the function
     * @return A future that will contain the result of the function call
     * @throws std::runtime_error if the ThreadSystem is shut down
     */
    template<class F, class... Args>
    auto enqueueTaskWithResult(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        // If shutdown or no thread pool, throw an exception
        if (m_isShutdown.load(std::memory_order_acquire) || !mp_threadPool) {
            std::cout << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown!" << std::endl;
            throw std::runtime_error("ThreadSystem is shut down");
        }

        try {
            return mp_threadPool->enqueueWithResult(std::forward<F>(f), std::forward<Args>(args)...);
        } catch (const std::exception& e) {
            std::cout << "Forge Game Engine - Error enqueueing task: " << e.what() << std::endl;
            throw;
        }
    }

    bool isBusy() const {
        // If shutdown or no thread pool, not busy anymore
        if (m_isShutdown.load(std::memory_order_acquire) || !mp_threadPool) {
            return false;
        }

        // Lock for thread safety when accessing mp_threadPool
        std::lock_guard<std::mutex> lock(m_mutex);
        return mp_threadPool && mp_threadPool->busy();
    }

    unsigned int getThreadCount() const {
        return m_numThreads;
    }

    bool isShutdown() const {
        return m_isShutdown.load(std::memory_order_acquire);
    }

    // Get the current task queue capacity
    size_t getQueueCapacity() const {
        if (mp_threadPool) {
            return mp_threadPool->getTaskQueue().capacity();
        }
        return m_queueCapacity;
    }

    // Get the current number of tasks in the queue
    size_t getQueueSize() const {
        if (mp_threadPool) {
            return mp_threadPool->getTaskQueue().size();
        }
        return 0;
    }

    /**
     * @brief Reserve capacity for the task queue
     *
     * NOTE: In most cases, you should NOT need to call this method directly.
     * The ThreadSystem is designed to manage its own capacity internally,
     * and will automatically grow as needed. This method is provided
     * primarily for specialized use cases where you know in advance
     * exactly how many tasks will be submitted.
     *
     * @param capacity The new capacity to reserve
     * @return true if capacity was reserved, false if ThreadSystem is shut down
     */
    bool reserveQueueCapacity(size_t capacity) {
        if (m_isShutdown.load(std::memory_order_acquire) || !mp_threadPool) {
            return false;
        }
        mp_threadPool->getTaskQueue().reserve(capacity);
        return true;
    }

    private:
            ThreadPool* mp_threadPool{nullptr};
            unsigned int m_numThreads{};
            size_t m_queueCapacity{};
            std::atomic<bool> m_isShutdown{false}; // Flag to indicate shutdown status
            mutable std::mutex m_mutex{}; // For thread-safe access to members

            // Prevent copying and assignment
            ThreadSystem(const ThreadSystem&) = delete;
            ThreadSystem& operator=(const ThreadSystem&) = delete;

            ThreadSystem() : m_queueCapacity(DEFAULT_QUEUE_CAPACITY) {}
};

} // namespace Forge

#endif // THREAD_SYSTEM_HPP
