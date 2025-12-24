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

    // Slow path: double-checked locking to prevent race on m_cachedBudget
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    if (!m_budgetValid.load(std::memory_order_relaxed)) {
        m_cachedBudget = calculateBudget();
        m_budgetValid.store(true, std::memory_order_release);
    }
    return m_cachedBudget;
}

size_t WorkerBudgetManager::getOptimalWorkers([[maybe_unused]] SystemType system,
                                               size_t workloadSize) {
    // No work = no workers needed
    if (workloadSize == 0) {
        return 0;
    }

    // Check queue pressure - if critically stressed, scale back
    double pressure = getQueuePressure();
    if (pressure > QUEUE_PRESSURE_CRITICAL) {
        return 1;  // Minimum threading under critical pressure
    }

    // Sequential execution model: each manager gets ALL workers during its window
    // Pre-allocated ThreadSystem means no overhead for using all threads
    return getBudget().totalWorkers;
}

std::pair<size_t, size_t> WorkerBudgetManager::getBatchStrategy(
    SystemType system,
    size_t workloadSize,
    size_t optimalWorkers) {

    if (workloadSize == 0 || optimalWorkers == 0) {
        return {1, workloadSize};  // Single batch with all items
    }

    auto& state = m_batchState[static_cast<size_t>(system)];

    // Simple, smart batch calculation:
    // - Use all available workers (pre-allocated ThreadSystem = minimal overhead)
    // - Only limit batches if items/batch would be < 8 (truly tiny work units)
    // - Let timing feedback fine-tune via multiplier

    constexpr size_t MIN_ITEMS_PER_BATCH = 8;

    // Maximum batches to avoid overhead-dominated execution
    size_t maxBatches = std::max(size_t{1}, workloadSize / MIN_ITEMS_PER_BATCH);

    // Target: use all workers, capped by workload constraints
    size_t targetBatches = std::min(optimalWorkers, maxBatches);

    // Apply timing feedback multiplier (0.25x to 2.0x fine-tuning)
    float multiplier = state.batchMultiplier.load(std::memory_order_relaxed);
    size_t batchCount = static_cast<size_t>(static_cast<float>(targetBatches) * multiplier);

    // Clamp: at least 1 batch, at most maxBatches
    batchCount = std::clamp(batchCount, size_t{1}, maxBatches);

    // Calculate batch size (round up to ensure all items are processed)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

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

    // Update rolling average (exponential smoothing with 15% weight to new sample)
    // Lower weight filters transient spikes better (~7 samples to reflect sustained change)
    double oldAvg = state.avgBatchTimeMs.load(std::memory_order_relaxed);
    double newAvg = oldAvg * 0.85 + perBatchTimeMs * 0.15;
    state.avgBatchTimeMs.store(newAvg, std::memory_order_relaxed);

    // Fine-tune multiplier based on per-batch time using dead-band
    float multiplier = state.batchMultiplier.load(std::memory_order_relaxed);

    if (perBatchTimeMs < BatchTuningState::DEAD_BAND_LOW_MS) {
        // Batches finishing too fast (< 100µs) - consolidate for efficiency
        multiplier -= BatchTuningState::ADJUST_RATE;
    } else if (perBatchTimeMs > BatchTuningState::DEAD_BAND_HIGH_MS) {
        // Batches taking too long (> 500µs) - split for better load balancing
        multiplier += BatchTuningState::ADJUST_RATE;
    }
    // Inside dead-band (100-500µs): optimal zone, no adjustment

    multiplier = std::clamp(multiplier,
                            BatchTuningState::MIN_MULTIPLIER,
                            BatchTuningState::MAX_MULTIPLIER);

    state.batchMultiplier.store(multiplier, std::memory_order_relaxed);
}

void WorkerBudgetManager::invalidateCache() {
    m_budgetValid.store(false, std::memory_order_release);
}

double WorkerBudgetManager::getQueuePressure() const {
    if (!ThreadSystem::Exists()) {
        return 0.0;
    }

    const auto& threadSystem = ThreadSystem::Instance();
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
    budget.totalWorkers = std::max(budget.totalWorkers, size_t{1});

    // Sequential execution model: all workers available to each manager during its window
    // No partitioning needed since managers don't run concurrently

    return budget;
}

} // namespace HammerEngine
