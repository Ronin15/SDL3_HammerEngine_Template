/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WORKER_BUDGET_HPP
#define WORKER_BUDGET_HPP

#include <cstddef>
#include <algorithm>
#include <cmath>
#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include "Logger.hpp"

namespace HammerEngine {

// Forward declaration
class ThreadSystem;

/**
 * @brief System types for WorkerBudgetManager
 */
enum class SystemType : uint8_t {
    AI = 0,
    Particle = 1,
    Pathfinding = 2,
    Event = 3,
    COUNT = 4
};

/**
 * @brief Worker budget allocation for game engine subsystems
 *
 * Simplified for sequential execution model: each manager gets ALL workers
 * during its execution window since managers don't run concurrently.
 */
struct WorkerBudget {
    size_t totalWorkers{0};  // Total available worker threads (all usable per manager)
};

/**
 * @brief Queue pressure thresholds
 * Used to prevent queue overflow when ThreadSystem is under heavy load
 */
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;

// Note: MIN_ITEMS_PER_BATCH is defined locally in WorkerBudget.cpp (= 8)
// This keeps the batch logic simple and in one place.

/**
 * @brief Centralized worker budget manager with adaptive batch tuning
 *
 * Optimized for sequential execution model: since managers execute one at a time
 * in the main loop, each manager gets ALL available workers during its window.
 * Pre-allocated ThreadSystem eliminates threading overhead.
 *
 * Provides:
 * 1. Full worker allocation per manager (sequential execution = no contention)
 * 2. Adaptive batch sizing via timing feedback (auto-converges to optimal)
 * 3. Queue pressure monitoring (prevents ThreadSystem overload)
 *
 * Thread Safety:
 * - All mutable state uses atomics
 * - Managers don't hold state - they call into this singleton
 */
class WorkerBudgetManager {
public:
    /**
     * @brief Get singleton instance
     */
    static WorkerBudgetManager& Instance();

    /**
     * @brief Get cached worker budget
     * Calculates once and caches the result until invalidated
     */
    const WorkerBudget& getBudget();

    /**
     * @brief Get optimal worker count for a system
     *
     * Sequential execution model: returns ALL workers for any active workload.
     * Only scales back under critical queue pressure (>90%).
     *
     * @param system The system type (AI, Particle, Pathfinding, Event)
     * @param workloadSize Current workload (entities, particles, etc.)
     * @return All available workers (or 0 if workloadSize is 0)
     */
    size_t getOptimalWorkers(SystemType system, size_t workloadSize);

    /**
     * @brief Get adaptive batch strategy
     *
     * Maximizes parallelism by default (2x workers for load balancing).
     * Fine-tunes based on per-batch timing feedback:
     * - Batch time < 100µs → consolidate (overhead dominates)
     * - Batch time > 2ms → split more (straggler risk)
     * - Sweet spot → maintain
     *
     * @param system The system type
     * @param workloadSize Total items to process
     * @param optimalWorkers Worker count from getOptimalWorkers()
     * @return Pair of {batchCount, batchSize}
     */
    std::pair<size_t, size_t> getBatchStrategy(SystemType system,
                                                size_t workloadSize,
                                                size_t optimalWorkers);

    /**
     * @brief Report batch completion for fine-tuning
     *
     * Calculates per-batch time and adjusts the batch multiplier for next frame.
     *
     * @param system The system type
     * @param batchCount Number of batches that were executed
     * @param totalTimeMs Total time for all batches to complete
     */
    void reportBatchCompletion(SystemType system, size_t batchCount, double totalTimeMs);

    /**
     * @brief Invalidate cached budget (call when ThreadSystem changes)
     */
    void invalidateCache();

private:
    WorkerBudgetManager() = default;
    ~WorkerBudgetManager() = default;

    // Non-copyable
    WorkerBudgetManager(const WorkerBudgetManager&) = delete;
    WorkerBudgetManager& operator=(const WorkerBudgetManager&) = delete;

    /**
     * @brief Per-system batch tuning state
     *
     * Uses atomics for thread safety. Fine-tunes batch count based on
     * per-batch timing to find the sweet spot (100µs - 2ms per batch).
     */
    struct BatchTuningState {
        std::atomic<float> batchMultiplier{1.0f};    // Scales dynamically (floor: 0.25x)
        std::atomic<double> avgBatchTimeMs{0.5};     // Rolling average per-batch time

        static constexpr float MIN_MULTIPLIER = 0.25f;  // Allows 4x consolidation
        // No artificial cap - maxBatches (workload/8) provides natural ceiling
        // Timing feedback converges to optimal value within physical constraints
        static constexpr float ADJUST_RATE = 0.1f;   // 10% per update

        // Per-batch time outer bounds
        static constexpr double MIN_BATCH_TIME_MS = 0.1;  // 100µs - below this, consolidate
        static constexpr double MAX_BATCH_TIME_MS = 2.0;  // 2ms - above this, split more

        // Dead-band: inner stable zone where multiplier doesn't adjust
        // Tighter band for faster convergence to optimal batch size
        static constexpr double DEAD_BAND_LOW_MS = 0.1;   // 100µs (target minimum)
        static constexpr double DEAD_BAND_HIGH_MS = 0.5;  // 500µs (target maximum)
    };

    // Cached budget (protected by double-checked locking)
    WorkerBudget m_cachedBudget{};
    std::atomic<bool> m_budgetValid{false};
    mutable std::mutex m_cacheMutex;

    // Per-system batch tuning (uses atomics, thread-safe)
    std::array<BatchTuningState, static_cast<size_t>(SystemType::COUNT)> m_batchState{};

    /**
     * @brief Get current queue pressure from ThreadSystem (0.0 to 1.0)
     */
    double getQueuePressure() const;

    /**
     * @brief Calculate worker budget (internal)
     */
    WorkerBudget calculateBudget() const;
};

} // namespace HammerEngine

#endif // WORKER_BUDGET_HPP
