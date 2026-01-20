/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WORKER_BUDGET_HPP
#define WORKER_BUDGET_HPP

#include <cstddef>
#include <array>
#include <atomic>
#include <mutex>
#include <utility>

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
    Collision = 4,
    COUNT = 5
};

/**
 * @brief Threading decision result from WorkerBudgetManager
 *
 * WorkerBudget is the authoritative source for threading decisions.
 * Managers should use shouldThread directly without additional overrides.
 */
struct ThreadingDecision {
    bool shouldThread;  // true = use multi-threading, false = single-threaded
    int probePhase;     // 0=normal, non-zero=exploration (for debugging)
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

/**
 * @brief Centralized worker budget manager with unified adaptive tuning
 *
 * Optimized for sequential execution model: since managers execute one at a time
 * in the main loop, each manager gets ALL available workers during its window.
 * Pre-allocated ThreadSystem eliminates threading overhead.
 *
 * Unified Tuning Design:
 * - Both single-threaded and multi-threaded modes report throughput
 * - Threading decision based on throughput comparison (no forced probing)
 * - Batch tuning hill-climbs to find optimal granularity
 * - Exploration triggered by signals (multiplier trend, stale data), not timer
 *
 * Provides:
 * 1. Full worker allocation per manager (sequential execution = no contention)
 * 2. Adaptive batch sizing via timing feedback (auto-converges to optimal)
 * 3. Unified throughput tracking for informed threading decisions
 * 4. Queue pressure monitoring (prevents ThreadSystem overload)
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
     * Uses hill-climbing tuned multiplier to find optimal batch count.
     * Balances parallelism benefit against scheduling overhead.
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
     * @brief Determine if threading should be used for current workload
     *
     * WorkerBudget is the AUTHORITATIVE source for threading decisions.
     * Managers should use decision.shouldThread directly without overrides.
     *
     * Decision based on throughput comparison between modes:
     * - Tracks smoothed throughput for both single and multi-threaded
     * - Switches mode when other mode shows 15%+ improvement
     * - Exploration triggered by signals, not periodic forced probing
     *
     * @param system The system type
     * @param workloadSize Number of items to process
     * @return ThreadingDecision with shouldThread and probePhase
     */
    ThreadingDecision shouldUseThreading(SystemType system, size_t workloadSize);

    /**
     * @brief Report execution result for unified tuning
     *
     * Call after processing completes to update throughput tracking
     * and tune batch multiplier. Replaces separate reportBatchCompletion
     * and reportThreadingResult calls.
     *
     * @param system The system type
     * @param workloadSize Number of items that were processed
     * @param wasThreaded true if multi-threading was used
     * @param batchCount Number of batches (0 if single-threaded)
     * @param totalTimeMs Total time for processing to complete
     */
    void reportExecution(SystemType system, size_t workloadSize,
                         bool wasThreaded, size_t batchCount, double totalTimeMs);

    /**
     * @brief Get expected throughput for a mode (for debugging/logging)
     * @param system The system type
     * @param threaded true for multi-threaded, false for single-threaded
     * @return Smoothed throughput value (items per ms)
     */
    double getExpectedThroughput(SystemType system, bool threaded) const;

    /**
     * @brief Get current batch multiplier for a system (for debugging/logging)
     * @param system The system type
     * @return Current multiplier value
     */
    float getBatchMultiplier(SystemType system) const;

    /**
     * @brief Get learned threading threshold for a system
     * @param system The system type
     * @return Learned threshold (0 if not learned yet)
     */
    size_t getLearnedThreshold(SystemType system) const;

    /**
     * @brief Check if system's learned threshold is currently active
     * @param system The system type
     * @return true if threshold learned and workload is above hysteresis band
     */
    bool isThresholdActive(SystemType system) const;

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
     * @brief Unified per-system tuning state
     *
     * Threading decision via adaptive threshold learning:
     * - Single-threaded until completion time >= 1.0ms (learning phase)
     * - Once threshold learned, use multi-threading with hysteresis
     * - Re-learn when workload drops below threshold - 5%
     *
     * Batch tuning via hill-climbing to find optimal parallelism.
     * Thread-safe via atomics.
     */
    struct SystemTuningState {
        // Throughput tracking (kept for batch multiplier tuning)
        std::atomic<double> singleSmoothedThroughput{0.0};  // Items per ms when single-threaded
        std::atomic<double> multiSmoothedThroughput{0.0};   // Items per ms when multi-threaded

        // Batch tuning (hill-climbing)
        std::atomic<float> multiplier{1.0f};                // Applied to worker count
        std::atomic<double> prevMultiThroughput{0.0};       // Previous multi-threaded throughput for hill-climb
        std::atomic<int8_t> direction{1};                   // Hill-climb direction (+1 or -1)

        // Mode tracking (for batch multiplier tuning)
        std::atomic<bool> lastWasThreaded{false};           // What mode was used last frame

        // Batch tuning constants
        static constexpr float MIN_MULTIPLIER = 0.4f;       // Allow 2.5x consolidation (was 0.25)
        static constexpr float MAX_MULTIPLIER = 2.0f;       // Allow 2x expansion (was 2.5)
        static constexpr float ADJUST_RATE = 0.01f;         // 1% adjustment per frame (was 2%)

        static constexpr double THROUGHPUT_TOLERANCE = 0.10;    // 10% dead band (was 6%)
        static constexpr double THROUGHPUT_SMOOTHING = 0.15;    // 15% weight for new data

        static constexpr size_t MIN_ITEMS_PER_BATCH = 8;    // Prevent trivial batches

        // Mode selection constants
        static constexpr size_t MIN_WORKLOAD = 100;             // Always single-threaded below this

        // Adaptive threshold learning constants
        static constexpr double LEARNING_TIME_THRESHOLD_MS = 0.9;  // Single-thread struggling threshold
        static constexpr double HYSTERESIS_FACTOR = 0.95;          // 5% hysteresis band (deactivate at 95%)
        static constexpr double TIME_SMOOTHING = 0.25;             // 25% weight for new time samples (~6 frames to converge)

        // Adaptive threshold learning state
        std::atomic<size_t> learnedThreshold{0};      // Entity count where 1.0ms was hit (0 = learning)
        std::atomic<bool> thresholdActive{false};     // true = above threshold, using multi-threading
        std::atomic<double> smoothedSingleTime{0.0};  // Exponential moving average of single-threaded ms
    };

    // Cached budget (protected by double-checked locking)
    WorkerBudget m_cachedBudget{};
    std::atomic<bool> m_budgetValid{false};
    mutable std::mutex m_cacheMutex;

    // Per-system unified tuning state
    std::array<SystemTuningState, static_cast<size_t>(SystemType::COUNT)> m_systemState{};

    /**
     * @brief Get system name for debug logging
     * @param system The system type
     * @return Human-readable system name
     */
    static const char* getSystemName(SystemType system);

    /**
     * @brief Update batch multiplier via hill-climbing
     * @param state The system's tuning state
     * @param throughput Current measured throughput
     */
    void updateBatchMultiplier(SystemTuningState& state, double throughput);

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
