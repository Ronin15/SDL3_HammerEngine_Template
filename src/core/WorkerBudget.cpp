/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/WorkerBudget.hpp"
#include "core/ThreadSystem.hpp"
#include "core/Logger.hpp"
#include <algorithm>
#include <format>

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

    auto& state = m_systemState[static_cast<size_t>(system)];

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
    size_t maxBatches = std::max(size_t{1}, workloadSize / SystemTuningState::MIN_ITEMS_PER_BATCH);
    batchCount = std::min(batchCount, maxBatches);

    // Ensure minimum batches: at least 2 when we have multiple workers
    // This guarantees actual parallelism when threading is decided
    if (optimalWorkers >= 2 && workloadSize >= SystemTuningState::MIN_ITEMS_PER_BATCH * 2) {
        batchCount = std::max(batchCount, size_t{2});
    } else {
        batchCount = std::max(batchCount, size_t{1});
    }

    // Calculate batch size (round up to ensure all items are processed)
    size_t batchSize = (workloadSize + batchCount - 1) / batchCount;

    return {batchCount, batchSize};
}

ThreadingDecision WorkerBudgetManager::shouldUseThreading(SystemType system, size_t workloadSize) {
    // ThreadSystem must exist for threading to be possible
    if (!ThreadSystem::Exists()) {
        return {false, 0};
    }

    // Single-core hardware: no parallelism possible, always single-threaded
    if (ThreadSystem::Instance().getThreadCount() <= 1) {
        return {false, 0};
    }

    // Always single-threaded for very small workloads
    if (workloadSize < SystemTuningState::MIN_WORKLOAD) {
        return {false, 0};
    }

    auto& state = m_systemState[static_cast<size_t>(system)];

    // Check if exploration of alternate mode should be triggered
    if (shouldExploreOtherMode(system)) {
        bool wasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);
        state.explorationPending.store(true, std::memory_order_relaxed);
        state.explorationWorkloadSize.store(workloadSize, std::memory_order_relaxed);
        // Try the opposite mode
        return {!wasThreaded, 1};  // probePhase=1 indicates exploration
    }

    // Get throughput data for both modes
    double singleTP = state.singleSmoothedThroughput.load(std::memory_order_relaxed);
    double multiTP = state.multiSmoothedThroughput.load(std::memory_order_relaxed);

#ifndef NDEBUG
    // Debug: Threading decision (every 300 frames)
    if (system == SystemType::Collision || system == SystemType::AI) {
        static thread_local uint64_t debugCounterColl = 0;
        static thread_local uint64_t debugCounterAI = 0;
        uint64_t& counter = (system == SystemType::Collision) ? debugCounterColl : debugCounterAI;
        if (++counter % 300 == 0) {
            size_t framesSince = state.framesSinceOtherMode.load(std::memory_order_relaxed);
            bool wasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);
            const char* name = (system == SystemType::Collision) ? "Collision" : "AI";
            HAMMER_DEBUG("WorkerBudget", std::format(
                "{}: wl={}, sTP={:.0f}, mTP={:.0f}, was={}, frames={}",
                name, workloadSize, singleTP, multiTP, wasThreaded, framesSince));
        }
    }
#endif

    // No data yet - use default behavior based on workload size
    if (singleTP <= 0.0 && multiTP <= 0.0) {
        // Start with threading for larger workloads until we have data
        bool shouldThread = workloadSize >= SystemTuningState::DEFAULT_THREADING_THRESHOLD;
        return {shouldThread, 0};
    }

    // Only one mode has data - we MUST try the other mode to collect data
    // This is critical: don't get stuck in one mode just because we started there
    if (singleTP <= 0.0) {
        // Only have multi-threaded data
        // Try single-threaded periodically to get comparison data
        size_t framesSince = state.framesSinceOtherMode.load(std::memory_order_relaxed);
        if (framesSince >= SystemTuningState::INITIAL_EXPLORATION_FRAMES) {
            state.explorationPending.store(true, std::memory_order_relaxed);
            return {false, 1};  // Try single-threaded
        }
        return {true, 0};  // Stay multi-threaded
    }
    if (multiTP <= 0.0) {
        // Only have single-threaded data - we NEED multi-threaded data
        // For large workloads, try threading immediately to get comparison data
        if (workloadSize >= SystemTuningState::DEFAULT_THREADING_THRESHOLD) {
            state.explorationPending.store(true, std::memory_order_relaxed);
            return {true, 1};  // Try multi-threaded to collect data
        }
        return {false, 0};  // Small workload, stay single
    }

    // Both modes have data - decide based on throughput comparison
    bool wasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);
    double currentTP = wasThreaded ? multiTP : singleTP;
    double otherTP = wasThreaded ? singleTP : multiTP;

    // Switch mode if other mode is significantly better (15% improvement)
    if (otherTP > currentTP * SystemTuningState::MODE_SWITCH_THRESHOLD) {
        return {!wasThreaded, 0};
    }

    // Stay in current mode
    return {wasThreaded, 0};
}

void WorkerBudgetManager::reportExecution(SystemType system, size_t workloadSize,
                                           bool wasThreaded, size_t batchCount,
                                           double totalTimeMs) {
    if (workloadSize == 0 || totalTimeMs <= 0.0) {
        return;
    }

    auto& state = m_systemState[static_cast<size_t>(system)];
    double throughput = static_cast<double>(workloadSize) / totalTimeMs;

    // Update appropriate throughput tracker
    if (wasThreaded) {
        // Update multi-threaded throughput
        double prev = state.multiSmoothedThroughput.load(std::memory_order_relaxed);
        double smoothed;
        if (prev <= 0.0) {
            smoothed = throughput;  // First sample
        } else {
            smoothed = prev * (1.0 - SystemTuningState::THROUGHPUT_SMOOTHING)
                     + throughput * SystemTuningState::THROUGHPUT_SMOOTHING;
        }
        state.multiSmoothedThroughput.store(smoothed, std::memory_order_relaxed);

        // Run batch tuning (only when multi-threaded)
        // Use smoothed throughput to filter noise from frame-to-frame variance
        if (batchCount > 0) {
            updateBatchMultiplier(state, smoothed);
        }

        // Reset frames counter for single-threaded sampling
        // (we're in multi mode, so count frames until we sample single again)
    } else {
        // Update single-threaded throughput
        double prev = state.singleSmoothedThroughput.load(std::memory_order_relaxed);
        double smoothed;
        if (prev <= 0.0) {
            smoothed = throughput;  // First sample
        } else {
            smoothed = prev * (1.0 - SystemTuningState::THROUGHPUT_SMOOTHING)
                     + throughput * SystemTuningState::THROUGHPUT_SMOOTHING;
        }
        state.singleSmoothedThroughput.store(smoothed, std::memory_order_relaxed);
        // Counter management handled at lines 212-216 (reset on mode change, increment otherwise)
    }

    // Update mode tracking
    bool prevWasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);
    state.lastWasThreaded.store(wasThreaded, std::memory_order_relaxed);

    // Increment frames since other mode if we stayed in same mode
    if (wasThreaded == prevWasThreaded) {
        state.framesSinceOtherMode.fetch_add(1, std::memory_order_relaxed);
    } else {
        state.framesSinceOtherMode.store(0, std::memory_order_relaxed);
    }

    // Clear exploration flag
    if (state.explorationPending.load(std::memory_order_relaxed)) {
        state.explorationPending.store(false, std::memory_order_relaxed);
    }
}

bool WorkerBudgetManager::shouldExploreOtherMode(SystemType system) const {
    const auto& state = m_systemState[static_cast<size_t>(system)];

    // Already exploring, don't start another exploration
    if (state.explorationPending.load(std::memory_order_relaxed)) {
        return false;
    }

    bool wasThreaded = state.lastWasThreaded.load(std::memory_order_relaxed);

    // Signal 1: Multiplier hitting floor suggests less parallelism wanted
    // If we're threading and multiplier is very low, single-threaded might be better
    // BUT: don't explore if threading is already confirmed better
    if (wasThreaded) {
        float mult = state.multiplier.load(std::memory_order_relaxed);
        if (mult <= SystemTuningState::MIN_MULTIPLIER * 1.5f) {
            // Check if threading is clearly better before exploring
            double singleTP = state.singleSmoothedThroughput.load(std::memory_order_relaxed);
            double multiTP = state.multiSmoothedThroughput.load(std::memory_order_relaxed);
            if (singleTP > 0.0 && multiTP > singleTP * SystemTuningState::MODE_SWITCH_THRESHOLD) {
                // Threading confirmed better, don't waste time exploring single
                return false;
            }
            return true;  // Try single-threaded
        }
    }

    // Signal 2: Stale data - haven't sampled other mode in a while
    size_t framesSince = state.framesSinceOtherMode.load(std::memory_order_relaxed);

    // CRITICAL: Force exploration if data is too stale, regardless of crossover band
    // Throughput characteristics change with hardware, workload, and game state
    // Don't let the system get stuck with old assumptions
    if (framesSince >= SystemTuningState::MAX_STALE_FRAMES) {
        return true;  // Must re-validate - we might be wrong
    }

    // Near crossover point - explore more frequently to maintain accurate data
    if (framesSince >= SystemTuningState::SAMPLE_INTERVAL) {
        double singleTP = state.singleSmoothedThroughput.load(std::memory_order_relaxed);
        double multiTP = state.multiSmoothedThroughput.load(std::memory_order_relaxed);

        // If we don't have data for the other mode, explore to get some
        if ((wasThreaded && singleTP <= 0.0) || (!wasThreaded && multiTP <= 0.0)) {
            return true;
        }

        // Near crossover = neither mode is clearly better
        if (singleTP > 0.0 && multiTP > 0.0) {
            double ratio = multiTP / singleTP;
            if (ratio > SystemTuningState::CROSSOVER_BAND_LOW &&
                ratio < SystemTuningState::CROSSOVER_BAND_HIGH) {
                return true;  // Explore to refine our knowledge
            }
        }
    }

    return false;
}

void WorkerBudgetManager::updateBatchMultiplier(SystemTuningState& state, double throughput) {
    // Get previous throughput for comparison
    double prevTP = state.prevMultiThroughput.load(std::memory_order_relaxed);
    state.prevMultiThroughput.store(throughput, std::memory_order_relaxed);

    // Skip first sample (need history to compare)
    if (prevTP <= 0.0) {
        return;
    }

    // Calculate relative change
    double change = (throughput - prevTP) / prevTP;

    // Hill-climb multiplier
    float multiplier = state.multiplier.load(std::memory_order_relaxed);
    int8_t direction = state.direction.load(std::memory_order_relaxed);

    if (change > SystemTuningState::THROUGHPUT_TOLERANCE) {
        // Throughput improved - continue in same direction
        multiplier += static_cast<float>(direction) * SystemTuningState::ADJUST_RATE;
    } else if (change < -SystemTuningState::THROUGHPUT_TOLERANCE) {
        // Throughput degraded - reverse direction
        direction = static_cast<int8_t>(-direction);
        state.direction.store(direction, std::memory_order_relaxed);
        multiplier += static_cast<float>(direction) * SystemTuningState::ADJUST_RATE;
    }
    // Within tolerance: at equilibrium, hold steady

    // Clamp multiplier
    multiplier = std::clamp(multiplier,
                            SystemTuningState::MIN_MULTIPLIER,
                            SystemTuningState::MAX_MULTIPLIER);

    state.multiplier.store(multiplier, std::memory_order_relaxed);
}

double WorkerBudgetManager::getExpectedThroughput(SystemType system, bool threaded) const {
    const auto& state = m_systemState[static_cast<size_t>(system)];
    if (threaded) {
        return state.multiSmoothedThroughput.load(std::memory_order_relaxed);
    }
    return state.singleSmoothedThroughput.load(std::memory_order_relaxed);
}

float WorkerBudgetManager::getBatchMultiplier(SystemType system) const {
    return m_systemState[static_cast<size_t>(system)].multiplier.load(std::memory_order_relaxed);
}

bool WorkerBudgetManager::isExploring(SystemType system) const {
    return m_systemState[static_cast<size_t>(system)].explorationPending.load(std::memory_order_relaxed);
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
