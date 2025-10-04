/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 *
 * Thread System: Handles task scheduling, thread pooling and task
 * prioritization
 */

#ifndef THREAD_SYSTEM_HPP
#define THREAD_SYSTEM_HPP

#include "Logger.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Platform-specific includes for thread affinity
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace HammerEngine {

// Task priority levels
enum class TaskPriority {
  Critical = 0, // Must execute ASAP (e.g., rendering, input handling)
  High = 1,     // Important tasks (e.g., physics, animation)
  Normal = 2,   // Default priority for most tasks
  Low = 3,      // Background tasks (e.g., asset loading)
  Idle = 4      // Only execute when nothing else is pending
};

// Task wrapper with priority information
struct PrioritizedTask {
  std::function<void()> task;
  TaskPriority priority;
  std::chrono::steady_clock::time_point enqueueTime;
  std::string description;

  // Default constructor
  PrioritizedTask()
      : priority(TaskPriority::Normal),
        enqueueTime(std::chrono::steady_clock::now()) {}

  // Constructor
  PrioritizedTask(std::function<void()> t, TaskPriority p,
                  std::string desc = "")
      : task(std::move(t)), priority(p),
        enqueueTime(std::chrono::steady_clock::now()),
        description(std::move(desc)) {}

  // Comparison operator for priority queue
  bool operator<(const PrioritizedTask &other) const {
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
// Forward declaration for work-stealing and budget integration
struct WorkerBudget;

class TaskQueue {

public:
  /**
   * @brief Construct a new Task Queue
   *
   * @param initialCapacity Initial capacity to reserve per priority (default:
   * 256)
   * @param enableProfiling Enable detailed task profiling (default: false)
   */
  explicit TaskQueue(size_t initialCapacity = 256, bool enableProfiling = false)
      : m_desiredCapacity(initialCapacity), m_enableProfiling(enableProfiling) {

    // Initialize atomic counters and realistic capacity distribution
    for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
      m_priorityCounts[i].count.store(0, std::memory_order_relaxed);
      m_taskStats[i] = {0, 0, 0};
    }
  }

  void push(std::function<void()> task,
            TaskPriority priority = TaskPriority::Normal,
            const std::string &description = "") {
    int priorityIndex = static_cast<int>(priority);

    {
      std::unique_lock<std::mutex> lock(m_priorityMutexes[priorityIndex]);

      // Add the new task (deque handles capacity automatically)
      m_priorityQueues[priorityIndex].emplace_back(std::move(task), priority,
                                                   description);

      // Update atomic counter
      m_priorityCounts[priorityIndex].count.fetch_add(1, std::memory_order_relaxed);

      // Set bitmask bit to indicate this queue has tasks
      m_queueBitmask.fetch_or(1 << priorityIndex, std::memory_order_relaxed);

      // Update statistics
      m_taskStats[priorityIndex].enqueued++;
      m_totalTasksEnqueued.fetch_add(1, std::memory_order_relaxed);

      // If profiling is enabled and this is a high priority task, log it
      if (m_enableProfiling && priority <= TaskPriority::High &&
          !description.empty()) {
        THREADSYSTEM_INFO("High priority task enqueued: " + description +
                          " (Priority: " + std::to_string(priorityIndex) + ")");
      }
    }

    // Smart notification: notify all for critical, otherwise notify one.
    std::lock_guard<std::mutex> lock(queueMutex);
    if (priority == TaskPriority::Critical) {
      condition.notify_all(); // Wake all for critical tasks to ensure immediate pickup.
    } else {
      condition.notify_one(); // Wake one for all other tasks to prevent a thundering herd.
    }
  }

  /**
   * @brief Batch enqueue multiple tasks with a single lock acquisition
   *
   * This method is highly optimized for scenarios where many tasks need to be
   * submitted at once (e.g., AI entity updates, particle batches). It reduces
   * lock contention from O(N) to O(1) by acquiring the mutex only once.
   *
   * @param tasks Vector of tasks to enqueue (will be moved from)
   * @param priority Priority level for all tasks in the batch
   * @param description Optional description prefix for debugging
   */
  void batchPush(std::vector<std::function<void()>>& tasks,
                 TaskPriority priority = TaskPriority::Normal,
                 const std::string& description = "") {
    if (tasks.empty()) {
      return;
    }

    int priorityIndex = static_cast<int>(priority);
    size_t batchSize = tasks.size();

    {
      std::unique_lock<std::mutex> lock(m_priorityMutexes[priorityIndex]);

      // Reserve space to avoid reallocations (deque doesn't have reserve, but this is good practice)
      for (auto& task : tasks) {
        m_priorityQueues[priorityIndex].emplace_back(std::move(task), priority, description);
      }

      // Update atomic counter once for entire batch
      m_priorityCounts[priorityIndex].count.fetch_add(batchSize, std::memory_order_relaxed);

      // Set bitmask bit to indicate this queue has tasks
      m_queueBitmask.fetch_or(1 << priorityIndex, std::memory_order_relaxed);

      // Update statistics
      m_taskStats[priorityIndex].enqueued += batchSize;
      m_totalTasksEnqueued.fetch_add(batchSize, std::memory_order_relaxed);

      // Log batch submission if profiling is enabled
      if (m_enableProfiling && !description.empty()) {
        THREADSYSTEM_INFO("Batch enqueued " + std::to_string(batchSize) + " tasks: " +
                          description + " (Priority: " + std::to_string(priorityIndex) + ")");
      }
    }

    // Wake multiple workers for batch processing
    std::lock_guard<std::mutex> lock(queueMutex);
    if (priority == TaskPriority::Critical || batchSize > 4) {
      condition.notify_all(); // Wake all workers for large batches
    } else {
      condition.notify_one();
    }
  }

  bool pop(std::function<void()> &task) {
    std::unique_lock<std::mutex> lock(queueMutex);
    condition.wait(lock, [this] {
      return stopping.load(std::memory_order_acquire) || hasAnyTasksLockFree();
    });

    if (stopping.load(std::memory_order_acquire)) {
      return false;
    }

    lock.unlock();
    return tryPopTask(task);
  }

  void stop() {
    stopping.store(true, std::memory_order_release);
    notifyAllThreads(); // Wake up all threads to exit

    std::lock_guard<std::mutex> lock(queueMutex);
    for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
      std::lock_guard<std::mutex> priorityLock(m_priorityMutexes[i]);
      m_priorityQueues[i].clear();
      m_priorityCounts[i].count.store(0, std::memory_order_relaxed);
    }
    // Clear all bitmask bits
    m_queueBitmask.store(0, std::memory_order_relaxed);
  }

  bool isEmpty() const {
    // Use atomic counters for lock-free checking
    for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
      if (m_priorityCounts[i].count.load(std::memory_order_relaxed) > 0) {
        return false;
      }
    }
    return true;
  }

  // Directly check if stopping without acquiring lock
  bool isStopping() const { return stopping.load(std::memory_order_acquire); }

  // Reserve capacity for all priority queues to reduce memory reallocations
  void reserve(size_t capacity) {
    // Only proceed if we're actually increasing capacity
    if (capacity <= m_desiredCapacity) {
      return;
    }

    // Note: std::deque doesn't have reserve(), but we track desired capacity
    m_desiredCapacity = capacity;

    if (m_enableProfiling) {
      THREADSYSTEM_INFO("Task queue capacity manually set to " +
                        std::to_string(capacity) +
                        " (deques grow automatically)");
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
      totalSize += m_priorityCounts[i].count.load(std::memory_order_relaxed);
    }
    return totalSize;
  }

  // Enable or disable profiling
  void setProfilingEnabled(bool enabled) { m_enableProfiling = enabled; }

  // Get task statistics
  struct TaskStats {
    size_t enqueued{0};
    size_t completed{0};
    size_t totalWaitTimeMs{0};

    double getAverageWaitTimeMs() const {
      return completed > 0 ? static_cast<double>(totalWaitTimeMs) / completed
                           : 0.0;
    }
  };

  // Get statistics for a specific priority level
  TaskStats getTaskStats(TaskPriority priority) const {
    int index = static_cast<int>(priority);
    if (index >= 0 && index <= static_cast<int>(TaskPriority::Idle)) {
      return m_taskStats[index];
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

  // Public accessors for condition variable and mutex
  std::condition_variable& getCondition() { return condition; }
  std::mutex& getMutex() { return queueMutex; }

  bool hasTasks() const {
    return hasAnyTasksLockFree();
  }
private:
  // Separate deques for each priority level (O(1) pop_front, reduces lock
  // contention)
  mutable std::array<std::deque<PrioritizedTask>, 5> m_priorityQueues{};

  // Cache-line aligned mutexes to prevent false sharing
  alignas(64) mutable std::array<std::mutex, 5> m_priorityMutexes{};

  // Cache-line aligned atomic counters to prevent false sharing
  struct alignas(64) AlignedAtomic {
    std::atomic<size_t> count{0};
  };
  mutable std::array<AlignedAtomic, 5> m_priorityCounts{};

  // Bitmask tracking non-empty queues for fast skip in tryPopTask
  std::atomic<uint8_t> m_queueBitmask{0};

  mutable std::mutex queueMutex{}; // Main mutex for condition variable
  std::condition_variable condition{};
  std::atomic<bool> stopping{false};

  // Statistics tracking with cache-friendly array instead of map
  mutable std::array<TaskStats, 5> m_taskStats{};
  std::atomic<size_t> m_totalTasksProcessed{0};
  std::atomic<size_t> m_totalTasksEnqueued{0};

  size_t m_desiredCapacity{256}; // Track desired capacity ourselves
  bool m_enableProfiling{false}; // Enable detailed performance metrics

  // Lock-free check for any tasks using atomic counters
  bool hasAnyTasksLockFree() const {
    for (int i = 0; i <= static_cast<int>(TaskPriority::Idle); ++i) {
      if (m_priorityCounts[i].count.load(std::memory_order_relaxed) > 0) {
        return true;
      }
    }
    return false;
  }

  // Try to pop a task without blocking
  bool tryPopTask(std::function<void()> &task) {
    // Fast-path: Check bitmask to skip empty queues
    uint8_t bitmask = m_queueBitmask.load(std::memory_order_relaxed);

    // Try to get task from highest priority queues first
    for (int priorityIndex = 0;
         priorityIndex <= static_cast<int>(TaskPriority::Idle);
         ++priorityIndex) {

      // Skip this priority level if bitmask indicates it's empty
      if (!(bitmask & (1 << priorityIndex))) {
        continue;
      }

      std::unique_lock<std::mutex> priorityLock(
          m_priorityMutexes[priorityIndex], std::try_to_lock);
      if (!priorityLock.owns_lock()) {
        continue; // Skip if we can't get the lock immediately
      }

      auto &queue = m_priorityQueues[priorityIndex];
      if (!queue.empty()) {
        // Get the oldest task from this priority level (FIFO within priority)
        PrioritizedTask prioritizedTask = std::move(queue.front());
        queue.pop_front(); // O(1) operation with deque

        // Update atomic counter
        size_t newCount = m_priorityCounts[priorityIndex].count.fetch_sub(1, std::memory_order_relaxed) - 1;

        // Clear bitmask bit if queue is now empty
        if (newCount == 0) {
          m_queueBitmask.fetch_and(~(1 << priorityIndex), std::memory_order_relaxed);
        }

        // Calculate wait time for metrics
        auto now = std::chrono::steady_clock::now();
        auto waitTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - prioritizedTask.enqueueTime)
                            .count();

        // Update statistics if profiling is enabled
        if (m_enableProfiling) {
          m_taskStats[priorityIndex].completed++;
          m_taskStats[priorityIndex].totalWaitTimeMs += waitTime;

          // Log long wait times for high priority tasks
          if (priorityIndex <= static_cast<int>(TaskPriority::High) &&
              waitTime > 100 && !prioritizedTask.description.empty()) {
            THREADSYSTEM_WARN(
                "High priority task delayed: " + prioritizedTask.description +
                " waited " + std::to_string(waitTime) + "ms");
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

public:
  // Wake up all waiting threads without clearing the queue
  void notifyAllThreads() {
    std::lock_guard<std::mutex> lock(queueMutex);
    condition.notify_all();
  }

private:
  // Internal notify without mutex - for use when mutex is already held
  void notifyAllThreadsUnsafe() { condition.notify_all(); }
};

// Thread pool for managing worker threads
// WorkerBudget-aware work-stealing queue for fair task distribution

class ThreadPool {

public:
  /**
   * @brief Construct a new Thread Pool object
   *
   * @param numThreads Number of worker threads to create
   * @param queueCapacity Capacity of the task queue (default: 256)
   * @param enableProfiling Enable detailed performance profiling (default:
   * false)
   */
  explicit ThreadPool(size_t numThreads, size_t queueCapacity = 256,
                      bool enableProfiling = false)
      : taskQueue(queueCapacity, enableProfiling) {

    // Work stealing removed for simplicity and reliability

    // Set up worker threads
    m_workers.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
      m_workers.emplace_back([this, i] {
// Set thread name and affinity if platform supports it
#ifdef __linux__
        // Linux: Set thread name
        std::string threadName = "Worker-" + std::to_string(i);
        pthread_setname_np(pthread_self(), threadName.c_str());

        // Linux: Set CPU affinity to pin thread to specific core
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % std::thread::hardware_concurrency(), &cpuset);
        int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (result != 0) {
          THREADSYSTEM_WARN("Failed to set CPU affinity for worker " +
                            std::to_string(i) + ": " + std::to_string(result));
        }
#elif defined(__APPLE__)
        // macOS: Set thread name
        std::string threadName = "Worker-" + std::to_string(i);
        pthread_setname_np(threadName.c_str());

        // macOS: Use thread affinity policy (soft hint, not hard pinning)
        thread_affinity_policy_data_t policy;
        policy.affinity_tag = static_cast<integer_t>(i);
        kern_return_t result = thread_policy_set(
            pthread_mach_thread_np(pthread_self()),
            THREAD_AFFINITY_POLICY,
            (thread_policy_t)&policy,
            THREAD_AFFINITY_POLICY_COUNT);
        if (result != KERN_SUCCESS) {
          THREADSYSTEM_WARN("Failed to set thread affinity for worker " +
                            std::to_string(i) + ": " + std::to_string(result));
        }
#elif defined(_WIN32)
        // Windows: Set thread affinity mask
        DWORD_PTR affinityMask = 1ULL << (i % std::thread::hardware_concurrency());
        DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), affinityMask);
        if (result == 0) {
          THREADSYSTEM_WARN("Failed to set thread affinity for worker " + std::to_string(i));
        }
#elif defined(_GNU_SOURCE)
        // Fallback for other systems with GNU extensions
        std::string threadName = "Worker-" + std::to_string(i);
        pthread_setname_np(pthread_self(), threadName.c_str());
#endif

        // Run the worker
        workerThread(i);
      });
    }

    if (enableProfiling) {
      THREADSYSTEM_INFO(
          "Thread pool created with " + std::to_string(numThreads) +
          " threads, simple queue-based threading, and profiling enabled");
    }
  }

  ~ThreadPool() {
    // Signal all threads to stop and wake them up
    isRunning.store(false, std::memory_order_release);
    taskQueue.stop(); // This will notify all threads

    // Join all worker threads
    for (auto &worker : m_workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    THREADSYSTEM_INFO("ThreadPool shutdown completed");
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
               const std::string &description = "") {
    // Simple single-queue design - all tasks go to global queue
    taskQueue.push(std::move(task), priority, description);
    // Update comprehensive statistics for all tasks
    m_totalTasksEnqueued.fetch_add(1, std::memory_order_relaxed);
  }

  /**
   * @brief Batch enqueue multiple tasks with optimized single lock acquisition
   *
   * Significantly reduces lock contention when submitting multiple tasks at once.
   * Ideal for AI updates, particle systems, and event processing batches.
   *
   * @param tasks Vector of tasks to enqueue (will be moved from)
   * @param priority Priority level for all tasks in the batch (default: Normal)
   * @param description Optional description prefix for debugging
   */
  void batchEnqueue(std::vector<std::function<void()>>& tasks,
                    TaskPriority priority = TaskPriority::Normal,
                    const std::string& description = "") {
    if (tasks.empty()) {
      return;
    }

    size_t batchSize = tasks.size();
    taskQueue.batchPush(tasks, priority, description);

    // Update comprehensive statistics for batch
    m_totalTasksEnqueued.fetch_add(batchSize, std::memory_order_relaxed);
  }

  bool busy() const {
    // Simple design - check global queue and active tasks
    if (!taskQueue.isEmpty()) {
      return true;
    }

    // Check if any worker threads are actively processing
    return m_activeTasks.load(std::memory_order_relaxed) > 0;
  }

  // Access the task queue for capacity management
  TaskQueue &getTaskQueue() { return taskQueue; }

  const TaskQueue &getTaskQueue() const { return taskQueue; }

  // Comprehensive task statistics (global + worker queues)
  size_t getTotalTasksEnqueued() const {
    return m_totalTasksEnqueued.load(std::memory_order_relaxed);
  }

  size_t getTotalTasksProcessed() const {
    return m_totalTasksProcessed.load(std::memory_order_relaxed);
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
  template <class F, class... Args>
  auto enqueueWithResult(F &&f, TaskPriority priority = TaskPriority::Normal,
                         const std::string &description = "", Args &&...args)
      -> std::future<typename std::invoke_result<F, Args...>::type> {
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> result = task->get_future();
    enqueue([task]() { (*task)(); }, priority, description);
    return result;
  }

private:
  std::vector<std::thread> m_workers; // Thread worker pool
  TaskQueue taskQueue; // Global priority queue for high/critical tasks
  std::atomic<bool> isRunning{true};
  mutable std::atomic<size_t> m_activeTasks{0}; // Track actively running tasks
  mutable std::mutex m_mutex{}; // For thread-safe access to members

  // Simple reliable threading - unused fields removed

  // Comprehensive task statistics tracking
  std::atomic<size_t> m_totalTasksEnqueued{
      0}; // All tasks (global + worker queues)
  std::atomic<size_t> m_totalTasksProcessed{0}; // All tasks processed

  void workerThread(size_t threadIndex = 0) {
    std::function<void()> task;

    // For statistics tracking
    auto startTime = std::chrono::steady_clock::now();
    size_t tasksProcessed = 0;
    size_t highPriorityTasks = 0;

    // For adaptive idle sleep optimization
    auto lastTaskTime = std::chrono::steady_clock::now();
    bool isIdle = false;

    // Set thread as interruptible (platform-specific if needed)
    try {

      // Main worker loop
      while (isRunning.load(std::memory_order_acquire)) {
        // Check for shutdown immediately at loop start
        if (!isRunning.load(std::memory_order_acquire)) {
          break;
        }

        bool gotTask = false;

        try {
          // WorkerBudget-aware task acquisition priority order:
          // 1. Global queue for high/critical priority tasks (WorkerBudget
          // engine/urgent tasks)
          if (taskQueue.pop(task)) {
            gotTask = true;
            highPriorityTasks++;
            // Reset idle tracking when we get a task
            lastTaskTime = std::chrono::steady_clock::now();
            isIdle = false;
          }
          // All tasks go through single global queue - simple and reliable
        } catch (...) {
          // If any exception occurs during pop, check shutdown
          if (!isRunning.load(std::memory_order_acquire)) {
            break;
          }
          continue;
        }

        // Check for shutdown again after getting task
        if (!isRunning.load(std::memory_order_acquire)) {
          break;
        }

        if (gotTask) {
          // Optimized: Only increment counter when we actually have work
          const size_t activeCount =
              m_activeTasks.fetch_add(1, std::memory_order_relaxed) + 1;

          // Track execution time for profiling
          auto taskStartTime = std::chrono::steady_clock::now();

          try {
            // Execute the task and increment counter
            task();
            tasksProcessed++;

            // Update comprehensive statistics
            m_totalTasksProcessed.fetch_add(1, std::memory_order_relaxed);
          } catch (const std::exception &e) {
            THREADSYSTEM_ERROR("Error in worker thread " +
                               std::to_string(threadIndex) + ": " +
                               std::string(e.what()));
          } catch (...) {
            THREADSYSTEM_ERROR("Unknown error in worker thread " +
                               std::to_string(threadIndex));
          }

          // Decrement with relaxed ordering - order doesn't matter for simple
          // counting
          m_activeTasks.fetch_sub(1, std::memory_order_relaxed);

          // Track execution time
          auto taskEndTime = std::chrono::steady_clock::now();
          auto taskDuration =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  taskEndTime - taskStartTime)
                  .count();

          // Log slow tasks if they exceed 100ms (truly problematic tasks)
          if (taskDuration > 100) {
            THREADSYSTEM_WARN(
                "Worker " + std::to_string(threadIndex) +
                " - Slow task: " + std::to_string(taskDuration) + "ms" +
                (highPriorityTasks > 0 ? " (HIGH PRIORITY)" : ""));
          }

          // Clear task after execution to free resources
          task = nullptr;

          // Unused variable warning suppression
          (void)activeCount;
        } else {
          // No task available - check idle time for adaptive sleep
          auto now = std::chrono::steady_clock::now();
          auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - lastTaskTime).count();

          // Adaptive idle sleep: After 1 second of idleness, reduce wake frequency
          // This saves CPU cycles and power on idle systems
          if (idleTime > 1000) {
            if (!isIdle) {
              isIdle = true;
              // Log transition to idle mode if profiling is enabled
              // (Disabled by default to avoid log spam)
            }
            // Sleep for 10ms to reduce CPU wake frequency
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
          }

          // Wait on condition variable for new tasks
          std::unique_lock<std::mutex> lock(taskQueue.getMutex());
          taskQueue.getCondition().wait(lock, [this] {
              return !isRunning.load(std::memory_order_acquire) || taskQueue.hasTasks();
          });
        }
      }
    } catch (const std::exception &e) {
      THREADSYSTEM_ERROR(
          "Worker thread " + std::to_string(threadIndex) +
          " terminated with exception: " + std::string(e.what()));
    } catch (...) {
      THREADSYSTEM_ERROR("Worker thread " + std::to_string(threadIndex) +
                         " terminated with unknown exception");
    }

    // Log worker thread statistics on exit
    auto endTime = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                             endTime - startTime)
                             .count();

    THREADSYSTEM_INFO("Worker " + std::to_string(threadIndex) +
                      " exiting after processing " +
                      std::to_string(tasksProcessed) + " tasks over " +
                      std::to_string(totalDuration) + "ms");

    // Suppress unused variable warnings in release builds
    (void)tasksProcessed;
    (void)totalDuration;
    (void)highPriorityTasks;
  }
};

// Singleton Thread System Manager
class ThreadSystem {

public:
  // Task queue settings
  static constexpr size_t DEFAULT_QUEUE_CAPACITY = 4096;

  // Timeout settings
  static constexpr int DEFAULT_SHUTDOWN_TIMEOUT_MS = 5000; // 5 seconds
  static constexpr int DEFAULT_TASK_TIMEOUT_MS = 30000;    // 30 seconds

  static ThreadSystem &Instance() {
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
    THREADSYSTEM_INFO("ThreadSystem resources cleaned!");

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
        THREADSYSTEM_INFO("Canceling " + std::to_string(pendingTasks) +
                          " pending tasks during shutdown...");
      }

      // Reset the thread pool - this will trigger its destructor
      // which handles thread cleanup gracefully
      m_threadPool.reset();

      // Add a small delay to allow any final thread messages to print
      std::this_thread::sleep_for(std::chrono::milliseconds(50));

      THREADSYSTEM_INFO("Thread pool successfully shut down");
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
   * @param queueCapacity Initial capacity for the task queue (default: 1024)
   * @param customThreadCount Optional parameter to specify exact thread count
   * (0 for auto-detect)
   * @param enableProfiling Enable detailed task profiling (default: false)
   * @return true if initialization succeeded, false otherwise
   */
  bool init(size_t queueCapacity = DEFAULT_QUEUE_CAPACITY,
            unsigned int customThreadCount = 0, bool enableProfiling = false) {
    // If already shutdown, don't allow re-initialization
    if (m_isShutdown.load(std::memory_order_acquire)) {
      if (m_enableDebugLogging) {
        THREADSYSTEM_WARN(
            "ThreadSystem already shut down, ignoring init request");
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
      m_threadPool = std::make_unique<ThreadPool>(m_numThreads, m_queueCapacity,
                                                  m_enableProfiling);

      THREADSYSTEM_INFO("ThreadSystem initialized with " +
                        std::to_string(m_numThreads) + " worker threads" +
                        (m_enableProfiling ? " (profiling enabled)" : ""));

      return m_threadPool != nullptr;
    } catch (const std::exception &e) {
      THREADSYSTEM_ERROR("Failed to initialize ThreadSystem: " +
                         std::string(e.what()));
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
                   const std::string &description = "") {
    // If shutdown or no thread pool, silently reject the task (for tests)
    if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
      if (m_enableDebugLogging) {
        THREADSYSTEM_DEBUG(
            "Ignoring task after shutdown" +
            (description.empty() ? "" : " (" + description + ")"));
      }
      return;
    }

    // If debug logging is enabled and we have a description, log it
    if (!description.empty() && m_enableDebugLogging) {
      THREADSYSTEM_DEBUG("Enqueuing task: " + description);
    }

    m_threadPool->enqueue(std::move(task), priority, description);
  }

  /**
   * @brief Batch enqueue multiple tasks with optimized performance
   *
   * This method is highly optimized for submitting multiple tasks at once,
   * reducing lock contention from O(N) to O(1). Use this when submitting
   * batches of tasks from AI updates, particle systems, or event processing.
   *
   * Example usage:
   *   std::vector<std::function<void()>> tasks;
   *   for (auto& entity : entities) {
   *     tasks.push_back([&entity](){ processEntity(entity); });
   *   }
   *   ThreadSystem::Instance().batchEnqueueTasks(tasks, TaskPriority::Normal, "AI Batch");
   *
   * @param tasks Vector of tasks to enqueue (will be moved from)
   * @param priority Priority level for all tasks in the batch (default: Normal)
   * @param description Optional description prefix for debugging and monitoring
   */
  void batchEnqueueTasks(std::vector<std::function<void()>>& tasks,
                         TaskPriority priority = TaskPriority::Normal,
                         const std::string& description = "") {
    // If shutdown or no thread pool, silently reject the tasks
    if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
      if (m_enableDebugLogging) {
        THREADSYSTEM_DEBUG(
            "Ignoring batch of " + std::to_string(tasks.size()) + " tasks after shutdown" +
            (description.empty() ? "" : " (" + description + ")"));
      }
      return;
    }

    if (tasks.empty()) {
      return;
    }

    // If debug logging is enabled, log the batch submission
    if (m_enableDebugLogging && !description.empty()) {
      THREADSYSTEM_DEBUG("Batch enqueuing " + std::to_string(tasks.size()) +
                         " tasks: " + description);
    }

    m_threadPool->batchEnqueue(tasks, priority, description);
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
  template <class F, class... Args>
  auto
  enqueueTaskWithResult(F &&f, TaskPriority priority = TaskPriority::Normal,
                        const std::string &description = "", Args &&...args)
      -> std::future<typename std::invoke_result<F, Args...>::type> {
    // If shutdown or no thread pool, return a future with default value (for
    // tests)
    if (m_isShutdown.load(std::memory_order_acquire) || !m_threadPool) {
      // Create a promise/future pair with a default-constructed result
      using ResultType = typename std::invoke_result<F, Args...>::type;
      std::promise<ResultType> promise;

      if (m_enableDebugLogging) {
        THREADSYSTEM_DEBUG(
            "Returning default value for task after shutdown" +
            (description.empty() ? "" : " (" + description + ")"));
      }

      // Set the result using default construction if possible
      try {
        if constexpr (std::is_void_v<ResultType>) {
          promise.set_value();
        } else if constexpr (std::is_default_constructible_v<ResultType>) {
          promise.set_value(ResultType{});
        } else if constexpr (std::is_pointer_v<ResultType>) {
          promise.set_value(nullptr);
        } else {
          // For types like unique_ptr that can't be default constructed
          // Set an exception instead
          promise.set_exception(std::make_exception_ptr(std::runtime_error(
              "ThreadSystem shutdown: Cannot create default value")));
        }
      } catch (...) {
        promise.set_exception(std::current_exception());
      }

      return promise.get_future();
    }

    try {
      return m_threadPool->enqueueWithResult(std::forward<F>(f), priority,
                                             description,
                                             std::forward<Args>(args)...);
    } catch (const std::exception &e) {
      THREADSYSTEM_ERROR("Error enqueueing task: " + std::string(e.what()));
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
    return m_threadPool->busy();
  }

  unsigned int getThreadCount() const { return m_numThreads; }

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

  // Get comprehensive task statistics (global + worker queues)
  size_t getTotalTasksProcessed() const {
    if (m_threadPool) {
      return m_threadPool->getTotalTasksProcessed();
    }
    return 0;
  }

  size_t getTotalTasksEnqueued() const {
    if (m_threadPool) {
      return m_threadPool->getTotalTasksEnqueued();
    }
    return 0;
  }

  // Enable or disable debug logging
  void setDebugLogging(bool enable) { m_enableDebugLogging = enable; }

  bool isDebugLoggingEnabled() const { return m_enableDebugLogging; }

private:
  std::unique_ptr<ThreadPool> m_threadPool{nullptr};
  unsigned int m_numThreads{};
  size_t m_queueCapacity{DEFAULT_QUEUE_CAPACITY};
  std::atomic<bool> m_isShutdown{false}; // Flag to indicate shutdown status
  mutable std::mutex m_mutex{};          // For thread-safe access to members
  bool m_enableDebugLogging{false};      // Flag to control debug logging
  bool m_enableProfiling{false}; // Flag for detailed performance metrics

  // Prevent copying and assignment
  ThreadSystem(const ThreadSystem &) = delete;
  ThreadSystem &operator=(const ThreadSystem &) = delete;

  ThreadSystem() = default;
};

} // namespace HammerEngine

#endif // THREAD_SYSTEM_HPP
