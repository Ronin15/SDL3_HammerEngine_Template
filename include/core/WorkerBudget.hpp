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
#include <string>
#include "Logger.hpp"

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
    size_t pathfindingAllocated; // Workers allocated to PathfinderManager
    size_t remaining;          // Buffer workers available for burst capacity

    // NOTE: collisionAllocated removed - CollisionManager is single-threaded

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
 * - AI entities require complex behavior updates → medium batches for balance (128 entities, max 8 batches)
 * - Particles require simple physics updates → larger batches (128+)
 * - Events have mixed complexity → medium batches (4+)
 *
 * JITTER REDUCTION STRATEGY: More, smaller batches reduce variance
 * - Old config: 4 batches @ 2500 entities → 0.5-1.5ms variance (jitter!)
 * - New config: 8 batches @ 1250 entities → 0.5-0.8ms variance (smooth!)
 * - Tradeoff: Slightly more overhead, but consistent frame times
 */
static constexpr BatchConfig AI_BATCH_CONFIG = {
    8,      // baseDivisor: threshold/8 for finer-grained parallelism
    128,    // minBatchSize: minimum 128 entities per batch (reduced for better distribution)
    2,      // minBatchCount: at least 2 batches for parallel execution
    8,      // maxBatchCount: max 8 batches for better load balancing (increased from 4)
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

static constexpr BatchConfig PATHFINDING_BATCH_CONFIG = {
    4,      // baseDivisor: threshold/4 for moderate parallelism
    16,     // minBatchSize: minimum 16 path requests per batch (larger batches for high-volume scenarios)
    2,      // minBatchCount: at least 2 batches
    8,      // maxBatchCount: up to 8 batches (better parallelism at scale)
    1.0     // targetUpdateTimeMs: 1ms target for pathfinding batch (adaptive tuning)
};

/**
 * @brief Worker allocation weights
 *
 * These weights determine:
 * 1. Base allocation distribution for guaranteed workers
 * 2. Priority access to buffer threads during burst workloads
 *
 * Higher weight = more base workers + higher priority for buffer access
 *
 * NOTE: CollisionManager is single-threaded and NOT included in allocation.
 * Worker distribution: AI (7/16 = 44%), Particle (4/16 = 25%), Event (2/16 = 12.5%), Pathfinding (3/16 = 19%)
 * This allocation prioritizes AI behavior updates while ensuring pathfinding gets dedicated workers
 * to prevent queue flooding during burst scenarios (500+ simultaneous path requests).
 */
static constexpr size_t AI_WORKER_WEIGHT = 7;        // Was 6, increased for higher computation load
static constexpr size_t PARTICLE_WORKER_WEIGHT = 4;  // Was 3, increased for parallel particle updates
static constexpr size_t PATHFINDING_WORKER_WEIGHT = 3; // NEW: Dedicated pathfinding allocation for burst coordination
static constexpr size_t EVENT_WORKER_WEIGHT = 2;     // Unchanged, lightweight event processing
static constexpr size_t ENGINE_WORKERS = 1;          // GameLoop uses 1 worker (update thread); main thread handles rendering

/**
 * @brief Unified queue pressure management thresholds
 *
 * Consistent queue pressure thresholds across all managers to eliminate
 * performance inconsistencies and ensure proper thread coordination.
 */
static constexpr float QUEUE_PRESSURE_WARNING = 0.70f;       // Early adaptation threshold (70%)
static constexpr float QUEUE_PRESSURE_CRITICAL = 0.90f;     // Fallback trigger for AI/Event managers (90%)
static constexpr float QUEUE_PRESSURE_PATHFINDING = 0.75f;  // PathfinderManager threshold (75%)
static constexpr double BUFFER_RESERVE_RATIO = 0.30;        // 30% of workers reserved for burst capacity

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
    size_t baseAllocation,
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
    // IMPORTANT: Only apply queue pressure overrides when NOT using buffer capacity
    // If optimalWorkers > baseAllocation, buffer workers are allocated and should be used fully
    bool usingBufferWorkers = (optimalWorkers > baseAllocation);

    if (queuePressure > QUEUE_PRESSURE_WARNING) {
        // High pressure: Use larger batches to reduce queue overhead
        // But DON'T reduce maxBatches if buffer workers are allocated - we need them to drain the queue!
        minItemsPerBatch = std::min(baseBatchSize * 2, threshold * 2);

        if (!usingBufferWorkers) {
            // Only limit batch count when using base allocation (no buffer)
            size_t highPressureMax = std::max(config.minBatchCount, optimalWorkers / 2);
            maxBatches = std::max(highPressureMax, static_cast<size_t>(1));
        }
        // else: Keep maxBatches scaled to optimalWorkers - use all allocated buffer workers
    } else if (queuePressure < (1.0 - QUEUE_PRESSURE_WARNING)) {
        // Low pressure: Use more, smaller batches for better parallelism
        size_t minLowPressure = std::max(threshold / (config.baseDivisor * 2), config.minBatchSize);
        minItemsPerBatch = std::max(baseBatchSize / 2, minLowPressure);
        // maxBatches already set correctly above based on optimalWorkers
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
    size_t baseAllocation,
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
    // IMPORTANT: Only apply queue pressure overrides when NOT using buffer capacity
    // If optimalWorkers > baseAllocation, buffer workers are allocated and should be used fully
    bool usingBufferWorkers = (optimalWorkers > baseAllocation);

    if (queuePressure > QUEUE_PRESSURE_WARNING) {
        // High pressure: Use larger batches to reduce queue overhead
        // But DON'T reduce maxBatches if buffer workers are allocated - we need them to drain the queue!
        minItemsPerBatch = std::min(baseBatchSize * 2, threshold * 2);

        if (!usingBufferWorkers) {
            // Only limit batch count when using base allocation (no buffer)
            size_t highPressureMax = std::max(config.minBatchCount, optimalWorkers / 2);
            maxBatches = highPressureMax;
        }
        // else: Keep maxBatches scaled to optimalWorkers - use all allocated buffer workers
    } else if (queuePressure < (1.0 - QUEUE_PRESSURE_WARNING)) {
        // Low pressure: Use more, smaller batches for better parallelism
        size_t minLowPressure = std::max(threshold / (config.baseDivisor * 2), config.minBatchSize);
        minItemsPerBatch = std::max(baseBatchSize / 2, minLowPressure);
        // maxBatches already set correctly above based on optimalWorkers
    }

    // Calculate batch count
    size_t batchCount = (workloadSize + minItemsPerBatch - 1) / minItemsPerBatch;
    batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);

    // Apply adaptive multiplier for performance-based tuning
    // IMPORTANT: When buffer workers are allocated, don't let adaptive tuning waste them
    // The buffer allocation itself is already adaptive (based on workload), so adaptive
    // batch reduction would just waste the workers we specifically allocated for this workload
    if (!usingBufferWorkers) {
        // Only apply adaptive reduction when using base allocation
        float multiplier = adaptiveState.batchMultiplier.load(std::memory_order_acquire);
        batchCount = static_cast<size_t>(batchCount * multiplier);
        batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);
    }
    // else: Using buffer workers - use all of them (batchCount already calculated correctly)

    // Calculate final batch size (distribute workload evenly)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    return {batchCount, batchSize};
}

/**
 * @brief Calculate optimal worker budget allocation with hardware-adaptive scaling
 *
 * IMPORTANT: CollisionManager is NOT included in worker allocation.
 * CollisionManager uses single-threaded collision detection optimized for
 * cache-friendly SOA access patterns. Future parallelization would require
 * per-batch spatial hashes with significant memory overhead.
 *
 * Worker Distribution (based on weights):
 * - AI: 7/16 (~44%) - Highest priority, most computation-heavy
 * - Particle: 4/16 (~25%) - Second priority, parallel particle updates
 * - Pathfinding: 3/16 (~19%) - NEW: Dedicated workers for coordinated burst handling
 * - Event: 2/16 (~12.5%) - Lowest priority, lightweight event processing
 * - Buffer: Remaining workers for burst workloads
 *
 * @param availableWorkers Total workers available in ThreadSystem
 * @return WorkerBudget Allocation strategy for all subsystems
 *
 * Tiered Strategy (prevents over-allocation):
 * - Tier 1 (≤1 workers): GameLoop=1, AI=0 (single-threaded), Events=0, Pathfinding=0 (single-threaded)
 * - Tier 2 (2-4 workers): GameLoop=1, AI=1 if possible, Pathfinding=0, Events=0 (shares with AI)
 * - Tier 3 (5+ workers): GameLoop=1, weighted base allocation (AI/Particle/Pathfinding/Event), 30% buffer reserve
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
        budget.pathfindingAllocated = 0; // Single-threaded fallback
        budget.remaining = 0;
        return budget;
    }

    // GameLoop worker allocation: 1 worker for update thread (all systems)
    // Main thread handles rendering and events; update worker runs game logic
    budget.engineReserved = ENGINE_WORKERS;

    // Calculate remaining workers after engine reservation
    size_t remainingWorkers = availableWorkers - budget.engineReserved;

    if (availableWorkers <= 4) {
        // Tier 2: Low-end systems (2-4 workers) - Conservative allocation
        budget.aiAllocated = (remainingWorkers >= 1) ? 1 : 0;
        budget.particleAllocated = (remainingWorkers >= 3) ? 1 : 0;
        budget.eventAllocated = 0; // Remains single-threaded on low-end
        budget.pathfindingAllocated = 0; // Remains single-threaded on low-end
        // NOTE: collisionAllocated removed - CollisionManager is single-threaded
    } else {
        // Tier 3: High-end systems (5+ workers) - Base allocation + buffer
        // Reserve 30% of remaining workers as buffer for burst capacity
        size_t bufferReserve = std::max(size_t(1), static_cast<size_t>(remainingWorkers * BUFFER_RESERVE_RATIO));
        size_t workersToAllocate = remainingWorkers - bufferReserve;

        // Weighted distribution for base allocations only (CollisionManager excluded - single-threaded)
        const size_t totalWeight = AI_WORKER_WEIGHT + PARTICLE_WORKER_WEIGHT + PATHFINDING_WORKER_WEIGHT + EVENT_WORKER_WEIGHT;

        std::array<size_t, 4> weights = {AI_WORKER_WEIGHT, PARTICLE_WORKER_WEIGHT, PATHFINDING_WORKER_WEIGHT, EVENT_WORKER_WEIGHT};
        std::array<double, 4> rawShares{};
        std::array<size_t, 4> shares{};

        for (size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] == 0 || workersToAllocate == 0) {
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

        // Ensure minimum allocation for high-priority subsystems (CollisionManager excluded)
        std::array<size_t, 4> priorityOrder = {0, 1, 2, 3}; // AI, Particle, Pathfinding, Event
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
        budget.pathfindingAllocated = shares[2];
        budget.eventAllocated = shares[3];

        // Buffer was already reserved upfront
        budget.remaining = bufferReserve;
    }

    // For low-end systems, calculate remaining after allocation
    if (availableWorkers <= 4) {
        size_t allocated = budget.aiAllocated + budget.particleAllocated + budget.pathfindingAllocated + budget.eventAllocated;
        budget.remaining = (remainingWorkers > allocated) ? remainingWorkers - allocated : 0;
    }

    // Validation: Ensure we never over-allocate
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated +
                            budget.particleAllocated + budget.pathfindingAllocated + budget.eventAllocated + budget.remaining;

    if (totalAllocated != availableWorkers) {
        // LOG WARNING with detailed breakdown for debugging
        std::string breakdown = "Worker allocation mismatch: " +
            std::to_string(totalAllocated) + " allocated vs " +
            std::to_string(availableWorkers) + " available\n" +
            "  Engine Reserved: " + std::to_string(budget.engineReserved) + "\n" +
            "  AI: " + std::to_string(budget.aiAllocated) + "\n" +
            "  Particle: " + std::to_string(budget.particleAllocated) + "\n" +
            "  Pathfinding: " + std::to_string(budget.pathfindingAllocated) + "\n" +
            "  Event: " + std::to_string(budget.eventAllocated) + "\n" +
            "  Buffer: " + std::to_string(budget.remaining);
        THREADSYSTEM_WARN(breakdown);

        // Adjust buffer to fix discrepancy
        if (totalAllocated > availableWorkers) {
            size_t excess = totalAllocated - availableWorkers;
            budget.remaining = (budget.remaining > excess) ? (budget.remaining - excess) : 0;
        } else {
            budget.remaining += (availableWorkers - totalAllocated);
        }
    }

    return budget;
}

} // namespace HammerEngine

#endif // WORKER_BUDGET_HPP
