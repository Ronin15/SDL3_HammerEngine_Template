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
#include <vector>
#include <stdexcept>  // For std::runtime_error
#include <thread>
#include <type_traits>
#include <SDL3/SDL.h>
#include <boost/container/small_vector.hpp>

namespace Forge {

// Thread-safe task queue for the worker thread pool with capacity reservation
class TaskQueue {

public:
    TaskQueue(size_t initialCapacity = 256) {
        // Pre-allocate memory for the task storage
        taskStorage.reserve(initialCapacity);
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
        taskStorage.erase(taskStorage.begin());
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

    bool isEmpty() {
        std::unique_lock<std::mutex> lock(queueMutex);
        return taskStorage.empty();
    }

    // Reserve capacity for the task queue to reduce memory reallocations
    void reserve(size_t capacity) {
        std::unique_lock<std::mutex> lock(queueMutex);
        taskStorage.reserve(capacity);
    }

    // Get the current capacity of the task queue
    size_t capacity() const {
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(queueMutex));
        return taskStorage.capacity();
    }

    // Get the current size of the task queue
    size_t size() const {
        std::unique_lock<std::mutex> lock(const_cast<std::mutex&>(queueMutex));
        return taskStorage.size();
    }

    private:
        std::vector<std::function<void()>> taskStorage{};
        std::mutex queueMutex{};
        std::condition_variable condition{};
        std::atomic<bool> stopping{false};
};

// Thread pool for managing worker threads
class ThreadPool {

public:
    ThreadPool(size_t numThreads, size_t queueCapacity = 256) {
        // Reserve capacity in the task queue before creating worker threads
        taskQueue.reserve(queueCapacity);

        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] { workerThread(); });
        }
    }

    ~ThreadPool() {
        // Signal all threads to stop after finishing current tasks
        isRunning = false;
        taskQueue.stop();

        // Wait for all worker threads to finish and join them
        for (auto& worker : workers) {
            if (worker.joinable()) {
                try {
                    worker.join();
                } catch (const std::exception& e) {
                    // Log the error but continue shutdown
                    std::cerr << "Forge Game Engine - Error joining thread!: " << e.what() << std::endl;
                }
            }
        }
        // Clear the worker threads
        workers.clear();
    }

    void enqueue(std::function<void()> task) {
        taskQueue.push(std::move(task));
    }

    bool busy() {
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
        boost::container::small_vector<std::thread, 16> workers{}; // Optimized for up to 16 threads without heap allocation
        TaskQueue taskQueue{};
        std::atomic<bool> isRunning{true};

        void workerThread() {
            std::function<void()> task;
            while (isRunning) {
                if (taskQueue.pop(task)) {
                    task();
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

    void clean() {
        std::cout << "Forge Game Engine - ThreadSystem resources cleaned!\n";
        // Set shutdown flag first so any new accesses will be rejected
        m_isShutdown = true;

        if (mp_threadPool) {
            // Make sure the thread pool finishes all pending tasks
            while (mp_threadPool->busy()) {
                SDL_Delay(1); // Short delay to avoid busy waiting
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

    bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY) {
        // If already shutdown, don't allow re-initialization
        if (m_isShutdown) {
            std::cerr << "Forge Game Engine - Error: Attempted to use ThreadSystem after shutdown!" << std::endl;
            return false;
        }

        // Set queue capacity
        m_queueCapacity = queueCapacity;

        // Determine optimal thread count (leave one for main thread)
        m_numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        mp_threadPool = new ThreadPool(m_numThreads, m_queueCapacity);
        return mp_threadPool != nullptr;
    }

    void enqueueTask(std::function<void()> task) {
        // If shutdown or no thread pool, reject the task
        if (m_isShutdown || !mp_threadPool) {
            std::cerr << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown!" << std::endl;
            return;
        }
        mp_threadPool->enqueue(std::move(task));
    }

    template<class F, class... Args>
    auto enqueueTaskWithResult(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        // If shutdown or no thread pool, throw an exception
        if (m_isShutdown || !mp_threadPool) {
            std::cerr << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown!" << std::endl;
            throw std::runtime_error("ThreadSystem is shut down");
        }
        return mp_threadPool->enqueueWithResult(std::forward<F>(f), std::forward<Args>(args)...);
    }

    bool isBusy() const {
        // If shutdown or no thread pool, not busy anymore
        if (m_isShutdown || !mp_threadPool) {
            return false;
        }
        return mp_threadPool->busy();
    }

    unsigned int getThreadCount() const {
        return m_numThreads;
    }

    bool isShutdown() const {
        return m_isShutdown;
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

    // Reserve capacity for the task queue
    bool reserveQueueCapacity(size_t capacity) {
        if (m_isShutdown || !mp_threadPool) {
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

            // Prevent copying and assignment
            ThreadSystem(const ThreadSystem&) = delete;
            ThreadSystem& operator=(const ThreadSystem&) = delete;

            ThreadSystem() : m_queueCapacity(DEFAULT_QUEUE_CAPACITY) {}
};

} // namespace Forge

#endif // THREAD_SYSTEM_HPP
