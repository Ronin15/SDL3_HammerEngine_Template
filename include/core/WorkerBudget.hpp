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

namespace HammerEngine {

/**
 * @brief Worker budget allocation for game engine subsystems
 *
 * Provides consistent thread allocation strategy across all managers
 * to prevent ThreadSystem overload and ensure fair resource distribution.
 *
 * Buffer threads (remaining) can be used dynamically by subsystems when their
 * workload exceeds what their base allocation can efficiently handle.
 */
struct WorkerBudget {
    size_t totalWorkers;       // Total available worker threads
    size_t engineReserved;     // Workers reserved for GameEngine critical tasks
    size_t aiAllocated;        // Workers allocated to AIManager
    size_t particleAllocated;  // Workers allocated to ParticleManager
    size_t eventAllocated;     // Workers allocated to EventManager
    size_t collisionAllocated; // Workers allocated to CollisionManager
    size_t remaining;          // Buffer workers available for burst capacity

    /**
     * @brief Calculate optimal worker count for a subsystem based on workload
     * @param baseAllocation Guaranteed allocation for the subsystem
     * @param workloadSize Current workload (entities, events, etc.)
     * @param workloadThreshold Threshold above which buffer should be used
     * @return Optimal number of workers (base + buffer if appropriate)
     */
    size_t getOptimalWorkerCount(size_t baseAllocation, size_t workloadSize, size_t workloadThreshold) const {
        if (workloadSize > workloadThreshold && remaining > 0) {
            // Use up to 75% of buffer capacity for burst workloads (increased from 50%)
            // Cap at 2x base allocation to prevent excessive thread spawning
            size_t bufferToUse = (remaining * 3) / 4;
            size_t burstWorkers = std::min(bufferToUse, baseAllocation * 2);
            return baseAllocation + burstWorkers;
        }
        return baseAllocation;
    }

    /**
     * @brief Check if buffer workers are available for burst usage
     * @return True if buffer workers are available
     */
    bool hasBufferCapacity() const {
        return remaining > 0;
    }

    /**
     * @brief Get maximum workers a subsystem can use (base + all buffer)
     * @param baseAllocation Guaranteed allocation for the subsystem
     * @return Maximum possible worker count
     */
    size_t getMaxWorkerCount(size_t baseAllocation) const {
        return baseAllocation + remaining;
    }
};

/**
 * @brief Adaptive batch state for performance-based tuning
 *
 * Tracks per-manager performance metrics and dynamically adjusts batch count
 * to hit target completion times. Each manager maintains its own adaptive state
 * to converge to optimal batching for current hardware and workload.
 */
struct AdaptiveBatchState {
    std::atomic<float> batchMultiplier{1.0f};     // Dynamic adjustment (0.5x to 1.5x)
    std::atomic<double> lastUpdateTimeMs{0.0};    // Previous frame's actual completion time

    static constexpr float MIN_MULTIPLIER = 0.5f;  // Never below 50% of calculated batches
    static constexpr float MAX_MULTIPLIER = 1.5f;  // Never above 150% of calculated batches
    static constexpr float ADAPT_RATE = 0.1f;      // 10% adjustment per frame (smooth convergence)
};

/**
 * @brief Batch processing configuration for consistent batching across managers
 *
 * Provides unified batch sizing strategy to eliminate inconsistent batch
 * calculations and enable easy performance tuning via configuration constants.
 *
 * Different manager types have different work granularity:
 * - AI: Complex behavior updates (small batches preferred)
 * - Particles: Simple physics updates (large batches preferred)
 * - Events: Mixed complexity (medium batches)
 */
struct BatchConfig {
    size_t baseDivisor;      // threshold / baseDivisor = base batch size
    size_t minBatchSize;     // Minimum items per batch (work granularity)
    size_t minBatchCount;    // Minimum number of batches to create
    size_t maxBatchCount;    // Maximum number of batches to create
    double targetUpdateTimeMs; // Target completion time for adaptive tuning

    /**
     * @brief Calculate base batch size from threading threshold
     * @param threshold Threading threshold for this manager
     * @return Base batch size for normal queue pressure conditions
     */
    size_t getBaseBatchSize(size_t threshold) const {
        return std::max(threshold / baseDivisor, minBatchSize);
    }
};

/**
 * @brief Standard batch configurations per manager type
 *
 * These configurations are tuned based on work granularity:
 * - AI entities require complex behavior updates → large batches to amortize sync overhead (250 entities, max 4 batches)
 * - Particles require simple physics updates → larger batches (128+)
 * - Events have mixed complexity → medium batches (4+)
 *
 * PERFORMANCE TUNING: Threading overhead dominates with small batches (11 batches @ 91 entities = 1.4ms).
 * Larger batches (4 batches @ 251 entities) reduce sync overhead by 64% for <1ms target.
 */
static constexpr BatchConfig AI_BATCH_CONFIG = {
    4,      // baseDivisor: threshold/4 for coarse-grained parallelism (reduced from 8)
    250,    // minBatchSize: minimum 250 entities per batch (increased from 64)
    2,      // minBatchCount: at least 2 batches for parallel execution
    4,      // maxBatchCount: max 4 batches to minimize sync overhead (reduced from 10)
    0.5     // targetUpdateTimeMs: 500µs target for AI updates (adaptive tuning)
};

static constexpr BatchConfig PARTICLE_BATCH_CONFIG = {
    4,      // baseDivisor: threshold/4 (increased from 1 for better parallelism)
    128,    // minBatchSize: minimum 128 particles (reduced from 256)
    2,      // minBatchCount: at least 2 batches
    8,      // maxBatchCount: up to 8 batches (increased from 4)
    0.3     // targetUpdateTimeMs: 300µs target for particle updates (adaptive tuning)
};

static constexpr BatchConfig EVENT_BATCH_CONFIG = {
    2,      // baseDivisor: threshold/2 for moderate parallelism
    4,      // minBatchSize: minimum 4 events per batch
    2,      // minBatchCount: at least 2 batches
    4,      // maxBatchCount: up to 4 batches
    0.2     // targetUpdateTimeMs: 200µs target for event updates (adaptive tuning)
};

/**
 * @brief Worker allocation weights
 *
 * These weights determine:
 * 1. Base allocation distribution for guaranteed workers
 * 2. Priority access to buffer threads during burst workloads
 *
 * Higher weight = more base workers + higher priority for buffer access
 */
static constexpr size_t AI_WORKER_WEIGHT = 6;
static constexpr size_t PARTICLE_WORKER_WEIGHT = 3;
static constexpr size_t EVENT_WORKER_WEIGHT = 2;
static constexpr size_t COLLISION_WORKER_WEIGHT = 3;
static constexpr size_t ENGINE_MIN_WORKERS = 1;        // Minimum workers for GameEngine
static constexpr size_t ENGINE_OPTIMAL_WORKERS = 2;    // Optimal workers for GameEngine on higher-end systems

/**
 * @brief Unified queue pressure management thresholds
 * 
 * Consistent queue pressure thresholds across all managers to eliminate
 * performance inconsistencies and ensure proper thread coordination.
 */
static constexpr float QUEUE_PRESSURE_WARNING = 0.70f;       // Early adaptation threshold (70%)
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;     // Fallback trigger for AI/Event managers (90%)
static constexpr float QUEUE_PRESSURE_PATHFINDING = 0.75f;  // PathfinderManager threshold (75%)

/**
 * @brief Calculate optimal batch count and size for threaded processing
 *
 * Unified batch calculation strategy that adapts to queue pressure and workload.
 * Replaces duplicated batch calculation logic across AIManager, ParticleManager,
 * and EventManager with a single, consistent implementation.
 *
 * @param config Batch configuration for this manager type (AI/Particle/Event)
 * @param workloadSize Total items to process (entities, particles, events)
 * @param threshold Threading threshold for this manager
 * @param optimalWorkers Optimal worker count from getOptimalWorkerCount()
 * @param queuePressure Current ThreadSystem queue pressure (0.0 to 1.0)
 * @return Pair of {batchCount, batchSize} for optimal parallel processing
 *
 * Queue Pressure Adaptation:
 * - High pressure (>70%): Fewer, larger batches to reduce queue overhead
 * - Low pressure (<30%): More, smaller batches for better parallelism
 * - Normal pressure: Use base configuration values
 */
inline std::pair<size_t, size_t> calculateBatchStrategy(
    const BatchConfig& config,
    size_t workloadSize,
    size_t threshold,
    size_t optimalWorkers,
    double queuePressure)
{
    // Handle empty workload
    if (workloadSize == 0) {
        return {1, 0};
    }

    // Calculate base batch parameters
    size_t baseBatchSize = config.getBaseBatchSize(threshold);
    size_t minItemsPerBatch = baseBatchSize;

    // Dynamic batch constraints: Scale with available workers while respecting config baseline
    // config.maxBatchCount is the minimum; scale up based on optimalWorkers for better parallelism
    // Cap at optimalWorkers to avoid excessive overhead (each batch has sync cost)
    size_t maxBatches = std::max(config.maxBatchCount,
                                 std::min(optimalWorkers, config.maxBatchCount * 2));

    // Adapt batch strategy based on ThreadSystem queue pressure
    if (queuePressure > QUEUE_PRESSURE_WARNING) {
        // High pressure: Use fewer, larger batches to reduce queue overhead
        // This prevents overwhelming the ThreadSystem when other subsystems are busy
        minItemsPerBatch = std::min(baseBatchSize * 2, threshold * 2);
        size_t highPressureMax = std::max(config.minBatchCount, optimalWorkers / 2);
        maxBatches = std::max(highPressureMax, static_cast<size_t>(1));
    } else if (queuePressure < (1.0 - QUEUE_PRESSURE_WARNING)) {
        // Low pressure: Use more, smaller batches for better parallelism
        // ThreadSystem has capacity, so maximize parallel execution
        size_t minLowPressure = std::max(threshold / (config.baseDivisor * 2), config.minBatchSize);
        minItemsPerBatch = std::max(baseBatchSize / 2, minLowPressure);
        // Scale with workers in low pressure (more cores = more parallelism opportunity)
        maxBatches = std::max(config.maxBatchCount,
                             std::min(optimalWorkers, config.maxBatchCount * 2));
    }

    // Calculate actual batch count based on workload and constraints
    size_t batchCount = (workloadSize + minItemsPerBatch - 1) / minItemsPerBatch;
    batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);

    // Calculate final batch size (distribute workload evenly across batches)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    return {batchCount, batchSize};
}

/**
 * @brief Calculate optimal batch count with adaptive performance-based tuning
 *
 * This overload extends the base batch calculation with adaptive feedback based on
 * measured completion times. It dynamically adjusts batch count to hit target times,
 * adapting to different hardware (2-16+ cores) and workloads (100-10K+ items).
 *
 * @param config Batch configuration for this manager type (AI/Particle/Event)
 * @param workloadSize Total items to process (entities, particles, events)
 * @param threshold Threading threshold for this manager
 * @param optimalWorkers Optimal worker count from getOptimalWorkerCount()
 * @param queuePressure Current ThreadSystem queue pressure (0.0 to 1.0)
 * @param adaptiveState Adaptive state tracking performance and multiplier
 * @param lastUpdateTimeMs Previous frame's actual completion time in milliseconds
 * @return Pair of {batchCount, batchSize} for optimal parallel processing
 *
 * Adaptive Behavior:
 * - If completion time > target * 1.15: Reduce batches (less sync overhead)
 * - If completion time < target * 0.85: Increase batches (more parallelism)
 * - Otherwise: Maintain current multiplier (within tolerance)
 * - Smooth 10% adjustments prevent oscillation
 * - Always respects configured min/max batch constraints
 */
inline std::pair<size_t, size_t> calculateBatchStrategy(
    const BatchConfig& config,
    size_t workloadSize,
    size_t threshold,
    size_t optimalWorkers,
    double queuePressure,
    AdaptiveBatchState& adaptiveState,
    double lastUpdateTimeMs)
{
    // Update adaptive multiplier based on previous frame's performance
    if (lastUpdateTimeMs > 0.0 && config.targetUpdateTimeMs > 0.0) {
        float currentMultiplier = adaptiveState.batchMultiplier.load(std::memory_order_acquire);

        if (lastUpdateTimeMs > config.targetUpdateTimeMs * 1.15) {
            // Too slow (>15% over target): reduce batches to minimize sync overhead
            currentMultiplier = std::max(AdaptiveBatchState::MIN_MULTIPLIER,
                                        currentMultiplier - AdaptiveBatchState::ADAPT_RATE);
        } else if (lastUpdateTimeMs < config.targetUpdateTimeMs * 0.85) {
            // Under budget (<15% below target): can increase parallelism
            currentMultiplier = std::min(AdaptiveBatchState::MAX_MULTIPLIER,
                                        currentMultiplier + AdaptiveBatchState::ADAPT_RATE);
        }
        // else: within 85-115% of target - no adjustment needed

        adaptiveState.batchMultiplier.store(currentMultiplier, std::memory_order_release);
        adaptiveState.lastUpdateTimeMs.store(lastUpdateTimeMs, std::memory_order_release);
    }

    // Handle empty workload
    if (workloadSize == 0) {
        return {1, 0};
    }

    // Calculate base batch parameters
    size_t baseBatchSize = config.getBaseBatchSize(threshold);
    size_t minItemsPerBatch = baseBatchSize;

    // Dynamic batch constraints: Scale with available workers while respecting config baseline
    // config.maxBatchCount is the minimum; scale up based on optimalWorkers for better parallelism
    // Cap at optimalWorkers to avoid excessive overhead (each batch has sync cost)
    size_t maxBatches = std::max(config.maxBatchCount,
                                 std::min(optimalWorkers, config.maxBatchCount * 2));

    // Adapt batch strategy based on ThreadSystem queue pressure
    if (queuePressure > QUEUE_PRESSURE_WARNING) {
        // High pressure: Use fewer, larger batches to reduce queue overhead
        minItemsPerBatch = std::min(baseBatchSize * 2, threshold * 2);
        size_t highPressureMax = std::max(config.minBatchCount, optimalWorkers / 2);
        maxBatches = highPressureMax;
    } else if (queuePressure < (1.0 - QUEUE_PRESSURE_WARNING)) {
        // Low pressure: Use more, smaller batches for better parallelism
        size_t minLowPressure = std::max(threshold / (config.baseDivisor * 2), config.minBatchSize);
        minItemsPerBatch = std::max(baseBatchSize / 2, minLowPressure);
        // Scale with workers in low pressure (more cores = more parallelism opportunity)
        maxBatches = std::max(config.maxBatchCount,
                             std::min(optimalWorkers, config.maxBatchCount * 2));
    }

    // Calculate batch count
    size_t batchCount = (workloadSize + minItemsPerBatch - 1) / minItemsPerBatch;
    batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);

    // Apply adaptive multiplier for performance-based tuning
    float multiplier = adaptiveState.batchMultiplier.load(std::memory_order_acquire);
    batchCount = static_cast<size_t>(batchCount * multiplier);
    batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);

    // Calculate final batch size (distribute workload evenly)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    return {batchCount, batchSize};
}

/**
 * @brief Calculate optimal worker budget allocation with hardware-adaptive scaling
 *
 * @param availableWorkers Total workers available in ThreadSystem
 * @return WorkerBudget Allocation strategy for all subsystems
 *
 * Tiered Strategy (prevents over-allocation):
 * - Tier 1 (≤1 workers): GameLoop=1, AI=0 (single-threaded), Events=0 (single-threaded)
 * - Tier 2 (2-4 workers): GameLoop=1, AI=1 if possible, Events=0 (shares with AI)
 * - Tier 3 (5+ workers): GameLoop=2, weighted base allocation, 30% buffer reserve
 *
 * Buffer Strategy:
 * - 30% of available workers reserved as buffer for burst capacity
 * - Base allocations use weighted distribution of remaining 70%
 * - Subsystems can dynamically use buffer threads based on workload
 * - Weights determine both base allocation AND priority access to buffer
 */
inline WorkerBudget calculateWorkerBudget(size_t availableWorkers) {
    WorkerBudget budget;
    budget.totalWorkers = availableWorkers;

    // Handle edge cases for very low-end systems
    if (availableWorkers <= 1) {
        // Tier 1: Ultra low-end - GameLoop gets priority, others single-threaded
        budget.engineReserved = 1;
        budget.aiAllocated = 0;         // Single-threaded fallback
        budget.particleAllocated = 0;   // Single-threaded fallback
        budget.eventAllocated = 0;      // Single-threaded fallback
        budget.collisionAllocated = 0;  // Single-threaded fallback
        budget.remaining = 0;
        return budget;
    }

    // Dynamic GameLoop worker allocation based on available cores
    // Low-end systems (≤4 workers): 1 worker for GameLoop coordination
    // Higher-end systems (>4 workers): 2 workers for GameLoop tasks
    if (availableWorkers <= 2) {
        budget.engineReserved = 1;  // Very limited systems
    } else if (availableWorkers <= 4) {
        budget.engineReserved = ENGINE_MIN_WORKERS;  // Low-end systems: 1 worker
    } else {
        budget.engineReserved = ENGINE_OPTIMAL_WORKERS;  // Higher-end systems: 2 workers
    }

    // Calculate remaining workers after engine reservation
    size_t remainingWorkers = availableWorkers - budget.engineReserved;

    if (availableWorkers <= 4) {
        // Tier 2: Low-end systems (2-4 workers) - Conservative allocation
        budget.aiAllocated = (remainingWorkers >= 1) ? 1 : 0;
        budget.particleAllocated = (remainingWorkers >= 3) ? 1 : 0;
        budget.eventAllocated = 0; // Remains single-threaded on low-end
        budget.collisionAllocated = (remainingWorkers >= 2) ? 1 : 0;
    } else {
        // Tier 3: High-end systems (5+ workers) - Base allocation + buffer
        // Reserve 30% of remaining workers as buffer for burst capacity
        size_t bufferReserve = std::max(size_t(1), static_cast<size_t>(remainingWorkers * 0.3));
        size_t workersToAllocate = remainingWorkers - bufferReserve;

        // Weighted distribution for base allocations only
        const size_t totalWeight = AI_WORKER_WEIGHT + PARTICLE_WORKER_WEIGHT +
                                   EVENT_WORKER_WEIGHT + COLLISION_WORKER_WEIGHT;

        std::array<size_t, 4> weights = {AI_WORKER_WEIGHT, PARTICLE_WORKER_WEIGHT,
                                         EVENT_WORKER_WEIGHT, COLLISION_WORKER_WEIGHT};
        std::array<double, 4> rawShares{};
        std::array<size_t, 4> shares{};

        for (size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] == 0 || totalWeight == 0 || workersToAllocate == 0) {
                rawShares[i] = 0.0;
                shares[i] = 0;
            } else {
                rawShares[i] = (static_cast<double>(weights[i]) /
                                static_cast<double>(totalWeight)) *
                               static_cast<double>(workersToAllocate);
                shares[i] = static_cast<size_t>(std::floor(rawShares[i]));
            }
        }

        size_t used = shares[0] + shares[1] + shares[2] + shares[3];

        // Ensure minimum allocation for high-priority subsystems
        std::array<size_t, 4> priorityOrder = {0, 3, 1, 2}; // AI, Collision, Particle, Event
        for (size_t index : priorityOrder) {
            if (weights[index] > 0 && shares[index] == 0 && used < workersToAllocate) {
                shares[index] = 1;
                ++used;
            }
        }

        // Distribute any rounding remainder
        size_t leftover = (used < workersToAllocate) ? (workersToAllocate - used) : 0;
        while (leftover > 0) {
            bool assigned = false;
            for (size_t index : priorityOrder) {
                if (leftover == 0) {
                    break;
                }
                if (weights[index] == 0) {
                    continue;
                }
                shares[index] += 1;
                --leftover;
                assigned = true;
                if (leftover == 0) {
                    break;
                }
            }
            if (!assigned) {
                break;
            }
        }

        budget.aiAllocated = shares[0];
        budget.particleAllocated = shares[1];
        budget.eventAllocated = shares[2];
        budget.collisionAllocated = shares[3];

        // Buffer was already reserved upfront
        budget.remaining = bufferReserve;
    }

    // For low-end systems, calculate remaining after allocation
    if (availableWorkers <= 4) {
        size_t allocated = budget.aiAllocated + budget.particleAllocated +
                          budget.eventAllocated + budget.collisionAllocated;
        budget.remaining = (remainingWorkers > allocated) ? remainingWorkers - allocated : 0;
    }

    // Validation: Ensure we never over-allocate
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated +
                            budget.particleAllocated + budget.eventAllocated +
                            budget.collisionAllocated;
    if (totalAllocated > availableWorkers) {
        // Emergency fallback - should never happen with correct logic above
        budget.aiAllocated = 0;
        budget.particleAllocated = 0;
        budget.eventAllocated = 0;
        budget.collisionAllocated = 0;
        budget.remaining = availableWorkers - budget.engineReserved;
    }

    return budget;
}

} // namespace HammerEngine

#endif // WORKER_BUDGET_HPP
