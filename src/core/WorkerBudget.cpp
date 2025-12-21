/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/WorkerBudget.hpp"
#include "core/ThreadSystem.hpp"
#include <algorithm>

namespace HammerEngine {

// Static singleton instance
WorkerBudgetManager& WorkerBudgetManager::Instance() {
    static WorkerBudgetManager instance;
    return instance;
}

const WorkerBudget& WorkerBudgetManager::getBudget() {
    // Fast path: return cached budget if valid
    if (m_budgetValid.load(std::memory_order_acquire)) {
        return m_cachedBudget;
    }

    // Slow path: calculate and cache
    m_cachedBudget = calculateBudget();
    m_budgetValid.store(true, std::memory_order_release);
    return m_cachedBudget;
}

size_t WorkerBudgetManager::getOptimalWorkers(SystemType system,
                                               size_t workloadSize,
                                               size_t threshold) {
    const auto& budget = getBudget();
    size_t baseAllocation = getBaseAllocation(system);

    // If workload is below threshold, use base allocation
    if (workloadSize <= threshold) {
        return baseAllocation;
    }

    // Check queue pressure - if queue is stressed, be conservative
    double pressure = getQueuePressure();
    if (pressure > QUEUE_PRESSURE_WARNING) {
        // High pressure: stick with base to avoid flooding queue
        return baseAllocation;
    }

    // Calculate extra workers based on workload severity
    float severity = static_cast<float>(workloadSize) / static_cast<float>(threshold);
    severity = std::clamp(severity, 1.0f, 4.0f);  // Cap at 4x

    // Grant buffer proportional to severity (up to 75% of buffer)
    size_t bufferToUse = static_cast<size_t>(
        static_cast<float>(budget.remaining) * 0.75f * (severity / 4.0f));
    bufferToUse = std::min(bufferToUse, baseAllocation);  // Cap at 2x total

    return baseAllocation + bufferToUse;
}

std::pair<size_t, size_t> WorkerBudgetManager::getBatchStrategy(
    SystemType system,
    size_t workloadSize,
    size_t optimalWorkers) {

    if (workloadSize == 0) {
        return {1, 0};
    }

    const auto& config = getConfig(system);
    auto& state = m_batchState[static_cast<size_t>(system)];

    // BASE: Maximize parallelism - 2x workers for load balancing
    size_t baseBatchCount = optimalWorkers * 2;

    // Apply fine-tuning multiplier from previous feedback
    float multiplier = state.batchMultiplier.load(std::memory_order_relaxed);
    size_t batchCount = static_cast<size_t>(static_cast<float>(baseBatchCount) * multiplier);

    // Clamp to valid range
    size_t maxBatches = std::min(workloadSize / config.minBatchSize, optimalWorkers * 4);
    maxBatches = std::max(maxBatches, config.minBatchCount);
    batchCount = std::clamp(batchCount, config.minBatchCount, maxBatches);

    // Calculate batch size (round up to ensure all items are processed)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    // Ensure minimum batch size
    batchSize = std::max(batchSize, config.minBatchSize);

    return {batchCount, batchSize};
}

void WorkerBudgetManager::reportBatchCompletion(SystemType system,
                                                 size_t batchCount,
                                                 double totalTimeMs) {
    if (batchCount == 0) {
        return;
    }

    auto& state = m_batchState[static_cast<size_t>(system)];

    // Calculate per-batch time
    double perBatchTimeMs = totalTimeMs / static_cast<double>(batchCount);

    // Update rolling average (exponential smoothing with 30% weight to new sample)
    double oldAvg = state.avgBatchTimeMs.load(std::memory_order_relaxed);
    double newAvg = oldAvg * 0.7 + perBatchTimeMs * 0.3;
    state.avgBatchTimeMs.store(newAvg, std::memory_order_relaxed);

    // Fine-tune multiplier based on per-batch time
    float multiplier = state.batchMultiplier.load(std::memory_order_relaxed);

    if (perBatchTimeMs < BatchTuningState::MIN_BATCH_TIME_MS) {
        // Batches too small (< 100µs) - consolidate to reduce overhead
        multiplier -= BatchTuningState::ADJUST_RATE;
    } else if (perBatchTimeMs > BatchTuningState::MAX_BATCH_TIME_MS) {
        // Batches too large (> 2ms) - split for better load balancing
        multiplier += BatchTuningState::ADJUST_RATE;
    }
    // Sweet spot (100µs - 2ms): no adjustment needed

    multiplier = std::clamp(multiplier,
                            BatchTuningState::MIN_MULTIPLIER,
                            BatchTuningState::MAX_MULTIPLIER);

    state.batchMultiplier.store(multiplier, std::memory_order_relaxed);
}

void WorkerBudgetManager::invalidateCache() {
    m_budgetValid.store(false, std::memory_order_release);
}

const BatchConfig& WorkerBudgetManager::getConfig(SystemType system) {
    switch (system) {
        case SystemType::AI:
            return AI_BATCH_CONFIG;
        case SystemType::Particle:
            return PARTICLE_BATCH_CONFIG;
        case SystemType::Pathfinding:
            return PATHFINDING_BATCH_CONFIG;
        case SystemType::Event:
            return EVENT_BATCH_CONFIG;
        default:
            return AI_BATCH_CONFIG;  // Fallback
    }
}

size_t WorkerBudgetManager::getBaseAllocation(SystemType system) const {
    switch (system) {
        case SystemType::AI:
            return m_cachedBudget.aiAllocated;
        case SystemType::Particle:
            return m_cachedBudget.particleAllocated;
        case SystemType::Pathfinding:
            return m_cachedBudget.pathfindingAllocated;
        case SystemType::Event:
            return m_cachedBudget.eventAllocated;
        default:
            return 1;
    }
}

double WorkerBudgetManager::getQueuePressure() const {
    if (!ThreadSystem::Exists()) {
        return 0.0;
    }

    auto& threadSystem = ThreadSystem::Instance();
    size_t queueSize = threadSystem.getQueueSize();
    size_t queueCapacity = threadSystem.getQueueCapacity();

    if (queueCapacity == 0) {
        return 0.0;
    }

    return static_cast<double>(queueSize) / static_cast<double>(queueCapacity);
}

WorkerBudget WorkerBudgetManager::calculateBudget() const {
    WorkerBudget budget;

    // Get available workers from ThreadSystem
    if (!ThreadSystem::Exists()) {
        // Fallback for tests or early initialization
        budget.totalWorkers = 4;
    } else {
        budget.totalWorkers = ThreadSystem::Instance().getThreadCount();
    }

    // Ensure minimum workers
    if (budget.totalWorkers == 0) {
        budget.totalWorkers = 1;
    }

    // Calculate total weight
    constexpr size_t totalWeight = AI_WORKER_WEIGHT + PARTICLE_WORKER_WEIGHT +
                                   PATHFINDING_WORKER_WEIGHT + EVENT_WORKER_WEIGHT;

    // Calculate buffer reserve (30% of workers for burst capacity)
    size_t bufferWorkers = static_cast<size_t>(
        static_cast<double>(budget.totalWorkers) * BUFFER_RESERVE_RATIO);

    // Ensure at least 1 buffer worker if we have more than 4 workers
    if (budget.totalWorkers > 4 && bufferWorkers == 0) {
        bufferWorkers = 1;
    }

    // Remaining workers for distribution
    size_t distributableWorkers = budget.totalWorkers - bufferWorkers;
    if (distributableWorkers == 0) {
        distributableWorkers = 1;
    }

    // Distribute workers based on weights
    auto calculateAllocation = [&](size_t weight) -> size_t {
        size_t allocation = (distributableWorkers * weight) / totalWeight;
        return std::max(allocation, static_cast<size_t>(1));  // Minimum 1 worker
    };

    budget.aiAllocated = calculateAllocation(AI_WORKER_WEIGHT);
    budget.particleAllocated = calculateAllocation(PARTICLE_WORKER_WEIGHT);
    budget.pathfindingAllocated = calculateAllocation(PATHFINDING_WORKER_WEIGHT);
    budget.eventAllocated = calculateAllocation(EVENT_WORKER_WEIGHT);

    // Remaining goes to buffer (may differ slightly from calculated due to rounding)
    size_t allocated = budget.aiAllocated + budget.particleAllocated +
                       budget.pathfindingAllocated + budget.eventAllocated;
    budget.remaining = (budget.totalWorkers > allocated) ?
                       (budget.totalWorkers - allocated) : 0;

    return budget;
}

} // namespace HammerEngine
