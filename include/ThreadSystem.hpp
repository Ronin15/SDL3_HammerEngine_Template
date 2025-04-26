// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#ifndef THREAD_SYSTEM_HPP
#define THREAD_SYSTEM_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

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
        }
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
    std::vector<std::thread> workers;
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
        isRunning = false;
        taskQueue.stop();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void enqueue(std::function<void()> task) {
        taskQueue.push(std::move(task));
    }

    bool busy() {
        return !taskQueue.isEmpty();
    }

    template<class F, class... Args>
    auto enqueueWithResult(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

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
    static ThreadSystem* sp_instance;
    unsigned int m_numThreads;

    ThreadSystem() : mp_threadPool(nullptr), m_numThreads(0) {}

public:
    ~ThreadSystem() {
        clean();
    }

    static ThreadSystem& Instance() {
        static ThreadSystem* sp_instance = new ThreadSystem();
        return *sp_instance;
    }

    bool init() {
        // Determine optimal thread count (leave one for main thread)
        m_numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        mp_threadPool = new ThreadPool(m_numThreads);
        return mp_threadPool != nullptr;
    }

    void enqueueTask(std::function<void()> task) {
        if (mp_threadPool) {
            mp_threadPool->enqueue(std::move(task));
        }
    }

    template<class F, class... Args>
    auto enqueueTaskWithResult(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        return mp_threadPool->enqueueWithResult(std::forward<F>(f), std::forward<Args>(args)...);
    }

    bool isBusy() const {
        return mp_threadPool && mp_threadPool->busy();
    }

    unsigned int getThreadCount() const {
        return m_numThreads;
    }

    void clean() {
        if (mp_threadPool) {
            delete mp_threadPool;
            mp_threadPool = nullptr;
        }
    }
};

} // namespace Forge

#endif // THREAD_SYSTEM_HPP
