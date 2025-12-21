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
 * Provides consistent thread allocation strategy across all managers
 * to prevent ThreadSystem overload and ensure fair resource distribution.
 */
struct WorkerBudget {
    size_t totalWorkers{0};          // Total available worker threads
    size_t aiAllocated{0};           // Workers allocated to AIManager
    size_t particleAllocated{0};     // Workers allocated to ParticleManager
    size_t eventAllocated{0};        // Workers allocated to EventManager
    size_t pathfindingAllocated{0};  // Workers allocated to PathfinderManager
    size_t remaining{0};             // Buffer workers available for burst capacity
};

/**
 * @brief Batch processing configuration for consistent batching across managers
 */
struct BatchConfig {
    size_t baseDivisor;       // threshold / baseDivisor = base batch size
    size_t minBatchSize;      // Minimum items per batch (work granularity)
    size_t minBatchCount;     // Minimum number of batches to create
    size_t maxBatchCount;     // Maximum number of batches to create
    double targetUpdateTimeMs; // Target completion time (unused in new system)

    size_t getBaseBatchSize(size_t threshold) const {
        return std::max(threshold / baseDivisor, minBatchSize);
    }
};

/**
 * @brief Standard batch configurations per manager type
 */
static constexpr BatchConfig AI_BATCH_CONFIG = {
    8,      // baseDivisor
    128,    // minBatchSize
    2,      // minBatchCount
    8,      // maxBatchCount
    0.5     // targetUpdateTimeMs
};

static constexpr BatchConfig PARTICLE_BATCH_CONFIG = {
    4,      // baseDivisor
    128,    // minBatchSize
    2,      // minBatchCount
    8,      // maxBatchCount
    0.3     // targetUpdateTimeMs
};

static constexpr BatchConfig EVENT_BATCH_CONFIG = {
    2,      // baseDivisor
    4,      // minBatchSize
    2,      // minBatchCount
    4,      // maxBatchCount
    0.2     // targetUpdateTimeMs
};

static constexpr BatchConfig PATHFINDING_BATCH_CONFIG = {
    4,      // baseDivisor
    16,     // minBatchSize
    2,      // minBatchCount
    8,      // maxBatchCount
    1.0     // targetUpdateTimeMs
};

/**
 * @brief Worker allocation weights
 */
static constexpr size_t AI_WORKER_WEIGHT = 7;
static constexpr size_t PARTICLE_WORKER_WEIGHT = 4;
static constexpr size_t PATHFINDING_WORKER_WEIGHT = 3;
static constexpr size_t EVENT_WORKER_WEIGHT = 2;

/**
 * @brief Queue pressure thresholds
 */
static constexpr float QUEUE_PRESSURE_WARNING = 0.70f;
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;
static constexpr double BUFFER_RESERVE_RATIO = 0.30;

/**
 * @brief Centralized worker budget manager with adaptive batch tuning
 *
 * Provides:
 * 1. Smart worker allocation based on workload and queue pressure
 * 2. Adaptive batch sizing that maximizes parallelism and fine-tunes based on per-batch timing
 * 3. Cached budget calculation (computed once, reused)
 *
 * Thread Safety:
 * - All mutable state uses atomics
 * - Managers don't hold state - they call into this singleton
 * - BatchConfig constants are constexpr (immutable)
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
     * @brief Get optimal worker count for a system based on workload
     *
     * @param system The system type (AI, Particle, Pathfinding, Event)
     * @param workloadSize Current workload (entities, particles, etc.)
     * @param threshold Threshold above which buffer workers should be considered
     * @return Optimal number of workers (base + buffer if appropriate)
     */
    size_t getOptimalWorkers(SystemType system, size_t workloadSize, size_t threshold);

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
        std::atomic<float> batchMultiplier{1.0f};    // 0.5x to 2.0x adjustment
        std::atomic<double> avgBatchTimeMs{0.5};     // Rolling average per-batch time

        static constexpr float MIN_MULTIPLIER = 0.5f;
        static constexpr float MAX_MULTIPLIER = 2.0f;
        static constexpr float ADJUST_RATE = 0.1f;   // 10% per update

        // Per-batch time sweet spot
        static constexpr double MIN_BATCH_TIME_MS = 0.1;  // 100µs - below this, consolidate
        static constexpr double MAX_BATCH_TIME_MS = 2.0;  // 2ms - above this, split more
    };

    // Cached budget
    WorkerBudget m_cachedBudget{};
    std::atomic<bool> m_budgetValid{false};

    // Per-system batch tuning (uses atomics, thread-safe)
    std::array<BatchTuningState, static_cast<size_t>(SystemType::COUNT)> m_batchState{};

    /**
     * @brief Get BatchConfig for a system type
     */
    static const BatchConfig& getConfig(SystemType system);

    /**
     * @brief Get base allocation for a system from the budget
     */
    size_t getBaseAllocation(SystemType system) const;

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
