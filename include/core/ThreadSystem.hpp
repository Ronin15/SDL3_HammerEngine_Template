/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 *
 * Thread System: Handles task scheduling, thread pooling and task prioritization
*/

#ifndef THREAD_SYSTEM_HPP
#define THREAD_SYSTEM_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <array>
#include <chrono>
#include <map>
#include <string>
#include <thread>

#include <SDL3/SDL.h>
#include <boost/container/small_vector.hpp>

namespace Forge {

// Task priority levels
enum class TaskPriority {
    Critical = 0,   // Must execute ASAP (e.g., rendering, input handling)
    High = 1,       // Important tasks (e.g., physics, animation)
    Normal = 2,     // Default priority for most tasks
    Low = 3,        // Background tasks (e.g., asset loading)
    Idle = 4        // Only execute when nothing else is pending
};

// Task wrapper with priority information
struct PrioritizedTask {
    std::function<void()> task;
    TaskPriority priority;
    std::chrono::steady_clock::time_point enqueueTime;
    std::string description;

    // Constructor
    PrioritizedTask(std::function<void()> t, TaskPriority p, std::string desc = "")
        : task(std::move(t)),
          priority(p),
          enqueueTime(std::chrono::steady_clock::now()),
          description(std::move(desc)) {}

    // Comparison operator for priority queue
    bool operator<(const PrioritizedTask& other) const {
        // Higher priority (lower enum value) comes first
        if (priority != other.priority) {
            return priority > other.priority;
        }
        // If same priority, older tasks come first (FIFO)
        return enqueueTime > other.enqueueTime;
    }
};

/**
 * @brief Thread-safe prioritized task queue using separate queues per priority
 *
 * This class provides a thread-safe queue for tasks to be executed by
 * the worker threads. Uses separate queues for each priority level to
 * reduce lock contention and improve performance.
 *
 * The queues automatically grow as needed, but can also have capacity
 * reserved in advance for better performance when submitting large
 * numbers of tasks at once.
 */
class TaskQueue {

public:
    /**
     * @brief Construct a new Task Queue
     *
     * @param initialCapacity Initial capacity to reserve per priority (default: 256)
     * @param enableProfiling Enable detailed task profiling (default: false)
     */
    TaskQueue(size_t initialCapacity = 256, bool enableProfiling = false)
        : m_desiredCapacity(initialCapacity),
          m_enableProfiling(enableProfiling) {
        
        // Initialize separate queues for each priority level
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            m_priorityQueues[i].reserve(initialCapacity / 5); // Distribute capacity
            m_taskStats[static_cast<TaskPriority>(i)] = {0, 0, 0};
        }
    }

    void push(std::function<void()> task, TaskPriority priority = TaskPriority::Normal, const std::string& description = "") {
        int priorityIndex = static_cast<int>(priority);
        
        {
            std::unique_lock<std::mutex> lock(m_priorityMutexes[priorityIndex]);

            // Check if we need to increase capacity for this priority level
            auto& queue = m_priorityQueues[priorityIndex];
            if (queue.size() >= static_cast<size_t>((m_desiredCapacity / 5) * 9 / 10)) { // 90% of allocated capacity per priority
                size_t newCapacity = queue.capacity() * 2;
                queue.reserve(newCapacity);

                if (m_enableProfiling) {
                    std::cout << "Forge Game Engine - Priority " << priorityIndex 
                              << " queue capacity increased to " << newCapacity << std::endl;
                }
            }

            // Add the new task
            queue.emplace_back(std::move(task), priority, description);

            // Update statistics
            m_taskStats[priority].enqueued++;
            m_totalTasksEnqueued.fetch_add(1, std::memory_order_relaxed);

            // If profiling is enabled and this is a high priority task, log it
            if (m_enableProfiling && priority <= TaskPriority::High && !description.empty()) {
                std::cout << "Forge Game Engine - High priority task enqueued: "
                          << description << " (Priority: " << priorityIndex << ")" << std::endl;
            }
        }
        
        // Notify waiting threads
        condition.notify_one();
    }

    bool pop(std::function<void()>& task) {
        // Early check for stopping to prevent entering wait state during shutdown
        if (stopping.load(std::memory_order_acquire)) {
            return false;
        }

        // First try to get a task without waiting
        if (tryPopTask(task)) {
            return true;
        }

        // If no task available, wait for notification
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // Use a very short timeout for quick shutdown response
        condition.wait_for(lock,
            std::chrono::milliseconds(10),
            [this] { return stopping.load(std::memory_order_acquire) || hasAnyTasksLockFree(); });

        // Check stopping flag first with higher priority than tasks
        if (stopping.load(std::memory_order_acquire)) {
            return false;
        }

        // Release lock and try to get task again
        lock.unlock();
        return tryPopTask(task);
    }

    void stop() {
        // First set the stopping flag before locking to indicate shutdown quickly
        stopping.store(true, std::memory_order_release);

        // Wake up all threads immediately
        notifyAllThreads();

        // Now take locks to clear all priority queues
        std::unique_lock<std::mutex> lock(queueMutex);
        
        // Count and clear all priority queues
        size_t totalPending = 0;
        std::map<TaskPriority, int> pendingByPriority;
        
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[i]);
            auto& queue = m_priorityQueues[i];
            
            if (m_enableProfiling && !queue.empty()) {
                TaskPriority priority = static_cast<TaskPriority>(i);
                pendingByPriority[priority] = queue.size();
                totalPending += queue.size();
            }
            
            // Clear the queue
            queue.clear();
        }

        // Log statistics if any tasks were pending
        if (m_enableProfiling && totalPending > 0) {
            std::cout << "Forge Game Engine - Stopping task queues with "
                      << totalPending << " pending tasks" << std::endl;

            // Log pending tasks by priority
            for (const auto& [priority, count] : pendingByPriority) {
                std::cout << "  Priority " << static_cast<int>(priority)
                          << ": " << count << " tasks" << std::endl;
            }
        }

        // Memory fence for maximum visibility across all threads
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Wake all waiting threads again after clearing queues
        notifyAllThreads();

        // Small delay to let threads notice the stop signal
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    bool isEmpty() const {
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[i]);
            if (!m_priorityQueues[i].empty()) {
                return false;
            }
        }
        return true;
    }

    // Directly check if stopping without acquiring lock
    bool isStopping() const {
        return stopping.load(std::memory_order_acquire);
    }

    // Reserve capacity for all priority queues to reduce memory reallocations
    void reserve(size_t capacity) {
        // Only proceed if we're actually increasing capacity
        if (capacity <= m_desiredCapacity) {
            return;
        }

        // Reserve capacity for each priority queue
        size_t capacityPerQueue = capacity / 5;
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[i]);
            m_priorityQueues[i].reserve(capacityPerQueue);
        }

        m_desiredCapacity = capacity;

        if (m_enableProfiling) {
            std::cout << "Forge Game Engine - Task queue capacity manually set to "
                      << capacity << " (" << capacityPerQueue << " per priority)" << std::endl;
        }
    }

    // Get the current capacity of the task queue
    size_t capacity() const {
        // Return our tracked desired capacity
        return m_desiredCapacity;
    }

    // Get the current size of all task queues combined
    size_t size() const {
        size_t totalSize = 0;
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[i]);
            totalSize += m_priorityQueues[i].size();
        }
        return totalSize;
    }

    // Enable or disable profiling
    void setProfilingEnabled(bool enabled) {
        m_enableProfiling = enabled;
    }

    // Get task statistics
    struct TaskStats {
        size_t enqueued{0};
        size_t completed{0};
        size_t totalWaitTimeMs{0};

        double getAverageWaitTimeMs() const {
            return completed > 0 ? static_cast<double>(totalWaitTimeMs) / completed : 0.0;
        }
    };

    // Get statistics for a specific priority level
    TaskStats getTaskStats(TaskPriority priority) const {
        auto it = m_taskStats.find(priority);
        if (it != m_taskStats.end()) {
            return it->second;
        }
        return TaskStats{};
    }

    // Get total tasks processed and enqueued
    size_t getTotalTasksProcessed() const {
        return m_totalTasksProcessed.load(std::memory_order_relaxed);
    }

    size_t getTotalTasksEnqueued() const {
        return m_totalTasksEnqueued.load(std::memory_order_relaxed);
    }

private:
    // Separate queues for each priority level (reduces lock contention)
    mutable std::array<std::vector<PrioritizedTask>, 5> m_priorityQueues{};
    mutable std::array<std::mutex, 5> m_priorityMutexes{};

    mutable std::mutex queueMutex{};  // Main mutex for condition variable
    std::condition_variable condition{};
    std::atomic<bool> stopping{false};

    // Statistics tracking
    std::map<TaskPriority, TaskStats> m_taskStats{};
    std::atomic<size_t> m_totalTasksProcessed{0};
    std::atomic<size_t> m_totalTasksEnqueued{0};

    size_t m_desiredCapacity{256}; // Track desired capacity ourselves
    bool m_enableProfiling{false}; // Enable detailed performance metrics
    
    // Lock-free check for any tasks
    bool hasAnyTasksLockFree() const {
        for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[i], std::try_to_lock);
            if (!priorityLock.owns_lock()) {
                continue; // Skip if mutex is locked
            }
            if (!m_priorityQueues[i].empty()) {
                return true;
            }
        }
        return false;
    }

    // Try to pop a task without blocking
    bool tryPopTask(std::function<void()>& task) {
        // Try to get task from highest priority queues first
        for (int priorityIndex = 0; priorityIndex <= static_cast<int>(TaskPriority::Idle); ++priorityIndex) {
            std::unique_lock<std::mutex> priorityLock(m_priorityMutexes[priorityIndex], std::try_to_lock);
            if (!priorityLock.owns_lock()) {
                continue; // Skip if we can't get the lock immediately
            }
            
            auto& queue = m_priorityQueues[priorityIndex];
            if (!queue.empty()) {
                // Get the oldest task from this priority level (FIFO within priority)
                PrioritizedTask prioritizedTask = std::move(queue.front());
                queue.erase(queue.begin());

                // Calculate wait time for metrics
                auto now = std::chrono::steady_clock::now();
                auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - prioritizedTask.enqueueTime).count();

                // Update statistics if profiling is enabled
                if (m_enableProfiling) {
                    TaskPriority priority = static_cast<TaskPriority>(priorityIndex);
                    m_taskStats[priority].completed++;
                    m_taskStats[priority].totalWaitTimeMs += waitTime;

                    // Log long wait times for high priority tasks
                    if (priorityIndex <= static_cast<int>(TaskPriority::High) && waitTime > 100 && !prioritizedTask.description.empty()) {
                        std::cout << "Forge Game Engine - High priority task delayed: "
                                  << prioritizedTask.description
                                  << " waited " << waitTime << "ms" << std::endl;
                    }
                }

                // Return the actual task
                task = std::move(prioritizedTask.task);
                m_totalTasksProcessed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    // Memory fences for better cross-thread visibility
    void notifyStop() {
        stopping.store(true, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        condition.notify_all();
    }

public:
    // Wake up all waiting threads without clearing the queue
    void notifyAllThreads() {
        condition.notify_all();
    }

private:
};

// Thread pool for managing worker threads
class ThreadPool {

public:
    /**
     * @brief Construct a new Thread Pool
     *
     * @param numThreads Number of worker threads to create
     * @param initialQueueCapacity Initial capacity for the task queue (memory is pre-allocated)
     * @param enableProfiling Enable detailed task performance metrics
     */
    ThreadPool(size_t numThreads, size_t queueCapacity = 256, bool enableProfiling = false)
        : taskQueue(queueCapacity, enableProfiling) {

        // Set up worker threads
        m_workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this, i] {
                // Name the thread for easier debugging
                std::string threadName = "Worker-" + std::to_string(i);

                // Set thread name if platform supports it
                #ifdef _GNU_SOURCE
                pthread_setname_np(pthread_self(), threadName.c_str());
                #endif

                // Run the worker
                workerThread(i);
            });
        }

        if (enableProfiling) {
            std::cout << "Forge Game Engine - Thread pool created with "
                      << numThreads << " threads and profiling enabled" << std::endl;
        }
    }

    ~ThreadPool() {
        // Signal all threads to stop
        isRunning.store(false, std::memory_order_release);

        // Ensure all threads see the update immediately
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // First notify all threads without clearing the queue
        taskQueue.notifyAllThreads();

        // Small delay to allow threads to notice the shutdown signal
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Then stop and clear the queue
        taskQueue.stop();

        // Notify again after stopping
        taskQueue.notifyAllThreads();

        // A short delay to allow threads to exit naturally
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // Join all worker threads
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                try {
                    // Just join directly - we've given threads time to exit cleanly
                    worker.join();
                } catch (const std::exception& e) {
                    std::cout << "Forge Game Engine - Error joining thread: " << e.what() << std::endl;
                } catch (...) {
                    std::cout << "Forge Game Engine - Unknown error joining thread" << std::endl;
                }
            }
        }

        std::cout << "Forge Game Engine - ThreadPool shutdown completed" << std::endl;

        // Clear the worker threads
        m_workers.clear();
    }

    /**
     * @brief Enqueue a task with specified priority
     *
     * @param task The task to execute
     * @param priority The priority level (default: Normal)
     * @param description Optional description for debugging
     */
    void enqueue(std::function<void()> task,
                 TaskPriority priority = TaskPriority::Normal,
                 const std::string& description = "") {
        taskQueue.push(std::move(task), priority, description);
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

    /**
     * @brief Enqueue a task that returns a result with specified priority
     *
     * @param f The function to execute
     * @param priority The priority level (default: Normal)
     * @param description Optional description for debugging
     * @param args Function arguments
     * @return A future containing the result
     */
    template<class F, class... Args>
    auto enqueueWithResult(F&& f,
                          TaskPriority priority = TaskPriority::Normal,
                          const std::string& description = "",
                          Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> result = task->get_future();
        enqueue([task](){ (*task)(); }, priority, description);
        return result;
    }
private:
    boost::container::small_vector<std::thread, 16> m_workers{}; // Optimized for up to 16 threads without heap allocation
    TaskQueue taskQueue{};
    std::atomic<bool> isRunning{true};
    mutable std::mutex m_mutex{}; // For thread-safe access to members

    void workerThread(size_t threadIndex = 0) {
        std::function<void()> task;

        // For statistics tracking
        auto startTime = std::chrono::steady_clock::now();
        size_t tasksProcessed = 0;

        // Set thread as interruptible (platform-specific if needed)
        try {
            // Main worker loop
            while (isRunning.load(std::memory_order_acquire)) {
                // Check for shutdown immediately at loop start
                if (!isRunning.load(std::memory_order_acquire)) {
                    break;
                }

                // Use a very short timeout on task pop to quickly respond to shutdown
                bool gotTask = false;
                try {
                    gotTask = taskQueue.pop(task);
                } catch (...) {
                    // If any exception occurs during pop, check shutdown
                    if (!isRunning.load(std::memory_order_acquire)) {
                        break;
                    }
                    continue;
                }

                // Check for shutdown again after pop
                if (!isRunning.load(std::memory_order_acquire)) {
                    break;
                }

                if (gotTask) {
                    // Track execution time for profiling
                    auto taskStartTime = std::chrono::steady_clock::now();

                    try {
                        // Execute the task and increment counter
                        task();
                        tasksProcessed++;
                    } catch (const std::exception& e) {
                        std::cout << "Forge Game Engine - Error in worker thread " << threadIndex
                                << ": " << e.what() << std::endl;
                    } catch (...) {
                        std::cout << "Forge Game Engine - Unknown error in worker thread "
                                << threadIndex << std::endl;
                    }

                    // Track execution time
                    auto taskEndTime = std::chrono::steady_clock::now();
                    auto taskDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                        taskEndTime - taskStartTime).count();

                    // Log slow tasks if they exceed 16ms (60fps frame time)
                    if (taskDuration > 16) {
                        std::cout << "Forge Game Engine - Worker " << threadIndex
                                << " - Slow task: " << taskDuration << "ms" << std::endl;
                    }

                    // Clear task after execution to free resources
                    task = nullptr;
                } else {
                    // No task available, check shutdown and then sleep briefly
                    if (!isRunning.load(std::memory_order_acquire)) {
                        break;
                    }

                    // Short sleep to avoid CPU spinning but remain responsive
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } catch (const std::exception& e) {
            std::cout << "Forge Game Engine - Worker thread " << threadIndex
                    << " terminated with exception: " << e.what() << std::endl;
        } catch (...) {
            std::cout << "Forge Game Engine - Worker thread " << threadIndex
                    << " terminated with unknown exception" << std::endl;
        }

        // Log worker thread statistics on exit
        auto endTime = std::chrono::steady_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        std::cout << "Forge Game Engine - Worker " << threadIndex
                  << " exiting after processing " << tasksProcessed << " tasks over "
                  << totalDuration << "ms" << std::endl;
    }
};

// Singleton Thread System Manager
class ThreadSystem {

public:
    // Task queue settings
    static constexpr size_t DEFAULT_QUEUE_CAPACITY = 1024;

    // Timeout settings
    static constexpr int DEFAULT_SHUTDOWN_TIMEOUT_MS = 5000; // 5 seconds
    static constexpr int DEFAULT_TASK_TIMEOUT_MS = 30000;    // 30 seconds

    static ThreadSystem& Instance() {
        static ThreadSystem instance;
        return instance;
    }

    /**
     * @brief Check if the ThreadSystem has been initialized
     * @return True if the ThreadSystem has been initialized, false otherwise
     */
    static bool Exists() {
        return !Instance().m_isShutdown.load(std::memory_order_acquire);
    }

    void clean() {
        std::cout << "Forge Game Engine - ThreadSystem resources cleaned!" << std::endl;

        // Set shutdown flag first so any new accesses will be rejected
        m_isShutdown.store(true, std::memory_order_release);
        // Ensure visibility across all threads
        std::atomic_thread_fence(std::memory_order_seq_cst);

        if (m_threadPool) {
            // First signal the pool to stop accepting new tasks
            // We don't actually need to wait for pending tasks to complete here
            m_threadPool->getTaskQueue().notifyAllThreads();

            // Allow a very brief delay for threads to notice shutdown signal
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Log the number of pending tasks
            size_t pendingTasks = m_threadPool->getTaskQueue().size();
            if (pendingTasks > 0) {
                std::cout << "Forge Game Engine - Canceling " << pendingTasks
                          << " pending tasks during shutdown..." << std::endl;
            }

            // Reset the thread pool - this will trigger its destructor
            // which handles thread cleanup gracefully
            m_threadPool.reset();

            // Add a small delay to allow any final thread messages to print
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            std::cout << "Forge Game Engine - Thread pool successfully shut down" << std::endl;
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
     * @param customThreadCount Optional parameter to specify exact thread count (0 for auto-detect)
     * @param enableProfiling Enable detailed task profiling (default: false)
     * @return true if initialization succeeded, false otherwise
     */
    bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY,
              unsigned int customThreadCount = 0,
              bool enableProfiling = false) {
        // If already shutdown, don't allow re-initialization
        if (m_isShutdown.load(std::memory_order_acquire)) {
            if (m_enableDebugLogging) {
                std::cout << "Forge Game Engine - Note: ThreadSystem already shut down, ignoring init request" << std::endl;
            }
            return false;
        }

        // Set queue capacity
        m_queueCapacity = queueCapacity;
        m_enableProfiling = enableProfiling;

        // Determine optimal thread count based on hardware
        if (customThreadCount > 0) {
            m_numThreads = customThreadCount;
        } else {
            unsigned int hardwareThreads = std::thread::hardware_concurrency();
            // Ensure we have at least one thread and leave one for main thread
            m_numThreads = (hardwareThreads > 1) ? (hardwareThreads - 1) : 1;
        }

        // Create thread pool with profiling if enabled
        try {
            m_threadPool = std::make_unique<ThreadPool>(m_numThreads, m_queueCapacity, m_enableProfiling);

            std::cout << "Forge Game Engine - ThreadSystem initialized with "
                      << m_numThreads << " worker threads"
                      << (m_enableProfiling ? " (profiling enabled)" : "") << std::endl;

            return m_threadPool != nullptr;
        } catch (const std::exception& e) {
            std::cout << "Forge Game Engine - Failed to initialize ThreadSystem: "
                      << e.what() << std::endl;
            return false;
        }
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
     * @param priority The priority level for the task
     * @param description Optional description for debugging and monitoring
     */
    void enqueueTask(std::function<void()> task,
                     TaskPriority priority = TaskPriority::Normal,
                     const std::string& description = "") {
        // If shutdown or no thread pool, silently reject the task (for tests)
        if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
            if (m_enableDebugLogging) {
                std::cout << "Forge Game Engine - Ignoring task after shutdown";
                if (!description.empty()) {
                    std::cout << " (" << description << ")";
                }
                std::cout << std::endl;
            }
            return;
        }

        // If debug logging is enabled and we have a description, log it
        if (!description.empty() && m_enableDebugLogging) {
            std::cout << "Forge Game Engine - Enqueuing task: " << description << std::endl;
        }

        m_threadPool->enqueue(std::move(task), priority, description);
    }

    /**
     * @brief Enqueue a task that returns a result with priority
     *
     * This method adds a task to the thread pool and returns a future that
     * can be used to retrieve the result. The task will be executed by one
     * of the worker threads according to its priority level.
     *
     * @param f The function to execute
     * @param priority Priority level for the task (default: Normal)
     * @param description Optional description for debugging and monitoring
     * @param args The arguments to pass to the function
     * @return A future that will contain the result of the function call
     * @throws std::runtime_error if the ThreadSystem is shut down
     */
    template<class F, class... Args>
    auto enqueueTaskWithResult(F&& f,
                              TaskPriority priority = TaskPriority::Normal,
                              const std::string& description = "",
                              Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        // If shutdown or no thread pool, return a future with default value (for tests)
        if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
            // Create a promise/future pair with a default-constructed result
            using ResultType = typename std::invoke_result<F, Args...>::type;
            std::promise<ResultType> promise;

            if (m_enableDebugLogging) {
                std::cout << "Forge Game Engine - Returning default value for task after shutdown";
                if (!description.empty()) {
                    std::cout << " (" << description << ")";
                }
                std::cout << std::endl;
            }

            // Set the result using default construction if possible
            try {
                if constexpr (std::is_void_v<ResultType>) {
                    promise.set_value();
                }
                else if constexpr (std::is_default_constructible_v<ResultType>) {
                    promise.set_value(ResultType{});
                }
                else if constexpr (std::is_pointer_v<ResultType>) {
                    promise.set_value(nullptr);
                }
                else {
                    // For types like unique_ptr that can't be default constructed
                    // Set an exception instead
                    promise.set_exception(std::make_exception_ptr(
                        std::runtime_error("ThreadSystem shutdown: Cannot create default value")));
                }
            } catch (...) {
                promise.set_exception(std::current_exception());
            }

            return promise.get_future();
        }

        try {
            return m_threadPool->enqueueWithResult(std::forward<F>(f),
                                                  priority,
                                                  description,
                                                  std::forward<Args>(args)...);
        } catch (const std::exception& e) {
            std::cout << "Forge Game Engine - Error enqueueing task: " << e.what() << std::endl;
            throw;
        }
    }

    bool isBusy() const {
        // If shutdown or no thread pool, not busy anymore
        if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
            return false;
        }

        // Lock for thread safety when accessing mp_threadPool
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_threadPool && m_threadPool->busy();
    }

    unsigned int getThreadCount() const {
        return m_numThreads;
    }

    bool isShutdown() const {
        return m_isShutdown.load(std::memory_order_acquire);
    }

    // Get the current task queue capacity
    size_t getQueueCapacity() const {
        if (m_threadPool) {
            return m_threadPool->getTaskQueue().capacity();
        }
        return m_queueCapacity;
    }

    // Get the current number of tasks in the queue
    size_t getQueueSize() const {
        if (m_threadPool) {
            return m_threadPool->getTaskQueue().size();
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
        if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
            return false;
        }
        m_threadPool->getTaskQueue().reserve(capacity);
        return true;
    }

    // Get task statistics
    size_t getTotalTasksProcessed() const {
        if (m_threadPool) {
            return m_threadPool->getTaskQueue().getTotalTasksProcessed();
        }
        return 0;
    }

    size_t getTotalTasksEnqueued() const {
        if (m_threadPool) {
            return m_threadPool->getTaskQueue().getTotalTasksEnqueued();
        }
        return 0;
    }

    // Enable or disable debug logging
    void setDebugLogging(bool enable) {
        m_enableDebugLogging = enable;
    }

    bool isDebugLoggingEnabled() const {
        return m_enableDebugLogging;
    }

private:
    std::unique_ptr<ThreadPool> m_threadPool{nullptr};
    unsigned int m_numThreads{};
    size_t m_queueCapacity{DEFAULT_QUEUE_CAPACITY};
    std::atomic<bool> m_isShutdown{false}; // Flag to indicate shutdown status
    mutable std::mutex m_mutex{}; // For thread-safe access to members
    bool m_enableDebugLogging{false}; // Flag to control debug logging
    bool m_enableProfiling{false}; // Flag for detailed performance metrics

    // Prevent copying and assignment
    ThreadSystem(const ThreadSystem&) = delete;
    ThreadSystem& operator=(const ThreadSystem&) = delete;

    ThreadSystem() = default;
};

} // namespace Forge

#endif // THREAD_SYSTEM_HPP
