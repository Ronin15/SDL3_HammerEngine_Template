/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/WorkerBudget.hpp"
#include "core/ThreadSystem.hpp"
#include <algorithm>

namespace HammerEngine {

// Static singleton instance with per-system threshold initialization
WorkerBudgetManager& WorkerBudgetManager::Instance() {
    static WorkerBudgetManager instance;
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        // Initialize per-system thresholds based on workload characteristics:
        // - Collision: Heavy per-item work (spatial hash, narrow phase) - benefits at lower counts
        // - AI: Medium work (behavior execution, pathfinding) - standard threshold
        // - Particle: Light per-item work - needs higher counts for threading benefit
        // - Event: Variable work - standard threshold
        instance.m_threadingState[static_cast<size_t>(SystemType::Collision)].threshold.store(150, std::memory_order_relaxed);
        instance.m_threadingState[static_cast<size_t>(SystemType::AI)].threshold.store(200, std::memory_order_relaxed);
        instance.m_threadingState[static_cast<size_t>(SystemType::Particle)].threshold.store(500, std::memory_order_relaxed);
        instance.m_threadingState[static_cast<size_t>(SystemType::Event)].threshold.store(200, std::memory_order_relaxed);
    });
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

    // =======================================================================
    // Simple and robust: base = workers, adjusted by throughput-tuned multiplier
    // No model assumptions - multiplier is tuned by measuring actual throughput
    // =======================================================================

    // Base batch count = all available workers (max parallelism)
    float multiplier = state.multiplier.load(std::memory_order_relaxed);
    double adjustedBatches = static_cast<double>(optimalWorkers) * static_cast<double>(multiplier);

    // Convert to integer
    size_t batchCount = static_cast<size_t>(std::max(1.0, adjustedBatches));

    // Cap based on minimum items per batch (prevent trivial batches)
    size_t maxBatches = std::max(size_t{1}, workloadSize / BatchTuningState::MIN_ITEMS_PER_BATCH);
    batchCount = std::min(batchCount, maxBatches);

    // Ensure at least 1 batch
    batchCount = std::max(batchCount, size_t{1});

    // Calculate batch size (round up to ensure all items are processed)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    return {batchCount, batchSize};
}

void WorkerBudgetManager::reportBatchCompletion(SystemType system,
                                                 size_t workloadSize,
                                                 size_t batchCount,
                                                 double totalTimeMs) {
    if (batchCount == 0 || workloadSize == 0 || totalTimeMs <= 0.0) {
        return;
    }

    auto& state = m_batchState[static_cast<size_t>(system)];

    // =======================================================================
    // THROUGHPUT-BASED HILL-CLIMBING
    // Simple and robust: measure throughput, adjust multiplier to maximize it
    // No model assumptions - just finds what works best experimentally
    // =======================================================================

    // Calculate current throughput (items per ms)
    double currentThroughput = static_cast<double>(workloadSize) / totalTimeMs;

    // Get previous values
    double prev = state.smoothedThroughput.load(std::memory_order_relaxed);
    double prevSmoothed = state.prevThroughput.load(std::memory_order_relaxed);

    // Update smoothed throughput
    double smoothed = prev * (1.0 - BatchTuningState::THROUGHPUT_SMOOTHING)
                    + currentThroughput * BatchTuningState::THROUGHPUT_SMOOTHING;
    state.smoothedThroughput.store(smoothed, std::memory_order_relaxed);
    state.prevThroughput.store(prev, std::memory_order_relaxed);

    // Skip first two frames (need history to compare)
    if (prevSmoothed <= 0.0) {
        return;
    }

    // Calculate relative change from previous smoothed value
    double change = (smoothed - prevSmoothed) / prevSmoothed;

    // Hill-climb multiplier
    float multiplier = state.multiplier.load(std::memory_order_relaxed);
    int8_t direction = state.direction.load(std::memory_order_relaxed);

    if (change > BatchTuningState::THROUGHPUT_TOLERANCE) {
        // Throughput improved - continue in same direction
        multiplier += static_cast<float>(direction) * BatchTuningState::ADJUST_RATE;
    } else if (change < -BatchTuningState::THROUGHPUT_TOLERANCE) {
        // Throughput degraded - reverse direction
        direction = static_cast<int8_t>(-direction);
        state.direction.store(direction, std::memory_order_relaxed);
        multiplier += static_cast<float>(direction) * BatchTuningState::ADJUST_RATE;
    }
    // Within tolerance: at equilibrium, hold steady

    // Clamp multiplier
    multiplier = std::clamp(multiplier,
                            BatchTuningState::MIN_MULTIPLIER,
                            BatchTuningState::MAX_MULTIPLIER);

    state.multiplier.store(multiplier, std::memory_order_relaxed);
}

ThreadingDecision WorkerBudgetManager::shouldUseThreading(SystemType system, size_t entityCount) {
    // ThreadSystem must exist for threading to be possible
    if (!ThreadSystem::Exists()) {
        return {false, 0};
    }

    // Single-core hardware: no parallelism possible, always single-threaded
    // (ThreadSystem with 0-1 workers means no benefit from threading)
    if (ThreadSystem::Instance().getThreadCount() <= 1) {
        return {false, 0};
    }

    auto& state = m_threadingState[static_cast<size_t>(system)];

    // Always single-threaded for very small counts
    if (entityCount < ThreadingTuningState::MIN_THRESHOLD) {
        return {false, 0};
    }

    size_t threshold = state.threshold.load(std::memory_order_relaxed);
    bool lastWasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);
    int phase = state.probePhase.load(std::memory_order_relaxed);

    // Two-phase probing: force specific mode during probe frames
    if (phase == 1) {
        // Phase 1: Force single-threaded for this frame
        return {false, 1};
    }
    if (phase == 2) {
        // Phase 2: Force multi-threaded for this frame
        return {true, 2};
    }

    // Check if it's time to start a new probe cycle
    size_t framesSinceProbe = state.framesSinceProbe.fetch_add(1, std::memory_order_relaxed);
    if (framesSinceProbe >= ThreadingTuningState::PROBE_INTERVAL) {
        // Start two-phase probe: phase 1 = single-threaded
        state.framesSinceProbe.store(0, std::memory_order_relaxed);
        state.probePhase.store(1, std::memory_order_relaxed);
        state.probeEntityCount.store(entityCount, std::memory_order_relaxed);
        return {false, 1};  // Force single-threaded for phase 1
    }

    // Normal operation: use learned threshold with hysteresis band
    // Hysteresis prevents flip-flopping when entity count hovers near threshold
    bool shouldThread;
    if (lastWasThreaded) {
        // Was threaded - stay threaded unless count drops well below threshold
        shouldThread = entityCount >= (threshold - ThreadingTuningState::HYSTERESIS_BAND);
    } else {
        // Was single-threaded - stay single unless count rises well above threshold
        shouldThread = entityCount >= (threshold + ThreadingTuningState::HYSTERESIS_BAND);
    }
    return {shouldThread, 0};
}

void WorkerBudgetManager::reportThreadingResult(SystemType system, size_t entityCount,
                                                 bool wasThreaded, double throughputItemsPerMs) {
    if (entityCount == 0 || throughputItemsPerMs <= 0.0) {
        return;
    }

    auto& state = m_threadingState[static_cast<size_t>(system)];
    int phase = state.probePhase.load(std::memory_order_relaxed);

    // Two-phase probing: handle probe results
    if (phase == 1) {
        // Phase 1 complete: store single-threaded measurement, advance to phase 2
        state.probeSingleThroughput.store(throughputItemsPerMs, std::memory_order_relaxed);
        state.probePhase.store(2, std::memory_order_relaxed);
        return;
    }

    if (phase == 2) {
        // Phase 2 complete: store multi-threaded measurement, compare, adjust threshold
        state.probeMultiThroughput.store(throughputItemsPerMs, std::memory_order_relaxed);
        state.probePhase.store(0, std::memory_order_relaxed);  // Back to normal

        // Compare FRESH back-to-back measurements (both from last 2 frames)
        double singleThroughput = state.probeSingleThroughput.load(std::memory_order_relaxed);
        double multiThroughput = state.probeMultiThroughput.load(std::memory_order_relaxed);
        size_t probeCount = state.probeEntityCount.load(std::memory_order_relaxed);
        size_t currentThreshold = state.threshold.load(std::memory_order_relaxed);

        // Skip if measurements are invalid
        if (singleThroughput <= 0.0 || multiThroughput <= 0.0) {
            return;
        }

        // Determine if threshold should change based on which mode performed better
        size_t newThreshold = currentThreshold;

        if (multiThroughput > singleThroughput * ThreadingTuningState::IMPROVEMENT_THRESHOLD) {
            // Multi-threading was significantly better (30%+)
            // Lower threshold to enable threading at lower counts
            if (currentThreshold > probeCount) {
                // Threshold too high - lower to where threading helps
                newThreshold = probeCount;
            } else if (probeCount <= currentThreshold * 2) {
                // Probing near threshold - explore lower
                newThreshold = (currentThreshold > 25) ? currentThreshold - 25 : ThreadingTuningState::MIN_THRESHOLD;
            }
            // else: well above threshold, threading confirmed working, hold steady
        } else if (singleThroughput > multiThroughput * ThreadingTuningState::IMPROVEMENT_THRESHOLD) {
            // Single-threaded was significantly better (30%+)
            // Raise threshold above probeCount
            newThreshold = std::max(currentThreshold, probeCount + 50);
        }
        // Within 30% of each other - threshold is roughly correct, hold steady

        // Clamp to valid range
        newThreshold = std::clamp(newThreshold,
                                  ThreadingTuningState::MIN_THRESHOLD,
                                  ThreadingTuningState::MAX_THRESHOLD);

        state.threshold.store(newThreshold, std::memory_order_relaxed);
        return;
    }

    // Phase 0: Normal operation - just track state for hysteresis
    state.lastWasThreaded.store(wasThreaded, std::memory_order_relaxed);
    state.lastEntityCount.store(entityCount, std::memory_order_relaxed);
}

size_t WorkerBudgetManager::getThreadingThreshold(SystemType system) const {
    return m_threadingState[static_cast<size_t>(system)].threshold.load(std::memory_order_relaxed);
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
