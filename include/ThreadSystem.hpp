// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details -test

#ifndef THREAD_SYSTEM_HPP
#define THREAD_SYSTEM_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <stdexcept>  // For std::runtime_error
#include <thread>
#include <type_traits>
#include <SDL3/SDL.h>
#include <boost/container/small_vector.hpp>

namespace Forge {

// Thread-safe task queue for the worker thread pool
class TaskQueue {
private:
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> stopping{false};

public:
    void push(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

    bool pop(std::function<void()>& task) {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] { return stopping || !tasks.empty(); });

        if (stopping && tasks.empty()) {
            return false;
        }

        task = std::move(tasks.front());
        tasks.pop();
        return true;
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stopping = true;
            // Clear any remaining tasks to avoid processing them during shutdown
            while (!tasks.empty()) {
                tasks.pop();
            }
        }
        // Wake up all waiting threads to check the stopping flag
        condition.notify_all();
    }

    bool isEmpty() {
        std::unique_lock<std::mutex> lock(queueMutex);
        return tasks.empty();
    }
};

// Thread pool for managing worker threads
class ThreadPool {
private:
    boost::container::small_vector<std::thread, 16> workers; // Optimized for up to 16 threads without heap allocation
    TaskQueue taskQueue;
    std::atomic<bool> isRunning{true};

    void workerThread() {
        std::function<void()> task;
        while (isRunning) {
            if (taskQueue.pop(task)) {
                task();
            }
        }
    }

public:
    ThreadPool(size_t numThreads) {
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
                    std::cerr << "Error joining thread: " << e.what() << std::endl;
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
};

// Singleton Thread System Manager
class ThreadSystem {
private:
    ThreadPool* mp_threadPool;
    unsigned int m_numThreads;
    std::atomic<bool> m_isShutdown{false}; // Flag to indicate shutdown status

    ThreadSystem() : mp_threadPool(nullptr), m_numThreads(0) {}
    // Prevent copying and assignment
    ThreadSystem(const ThreadSystem&) = delete;
    ThreadSystem& operator=(const ThreadSystem&) = delete;

public:
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

    static ThreadSystem& Instance() {
        static ThreadSystem instance; // Properly managed by the language
        return instance;
    }

    bool init() {
        // If already shutdown, don't allow re-initialization
        if (m_isShutdown) {
            std::cerr << "Forge Game Engine - Error: Attempted to use ThreadSystem after shutdown" << std::endl;
            return false;
        }

        // Determine optimal thread count (leave one for main thread)
        m_numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        mp_threadPool = new ThreadPool(m_numThreads);
        return mp_threadPool != nullptr;
    }

    void enqueueTask(std::function<void()> task) {
        // If shutdown or no thread pool, reject the task
        if (m_isShutdown || !mp_threadPool) {
            std::cerr << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown" << std::endl;
            return;
        }
        mp_threadPool->enqueue(std::move(task));
    }

    template<class F, class... Args>
    auto enqueueTaskWithResult(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        // If shutdown or no thread pool, throw an exception
        if (m_isShutdown || !mp_threadPool) {
            std::cerr << "Forge Game Engine - Warning: Attempted to enqueue task after ThreadSystem shutdown" << std::endl;
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
};

} // namespace Forge

#endif // THREAD_SYSTEM_HPP
