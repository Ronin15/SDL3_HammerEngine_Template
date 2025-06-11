/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef WORKER_BUDGET_HPP
#define WORKER_BUDGET_HPP

#include <cstddef>
#include <algorithm>

namespace Forge {

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
    size_t totalWorkers;      // Total available worker threads
    size_t engineReserved;    // Workers reserved for GameEngine critical tasks
    size_t aiAllocated;       // Workers allocated to AIManager
    size_t eventAllocated;    // Workers allocated to EventManager
    size_t remaining;         // Buffer workers available for burst capacity
    
    /**
     * @brief Calculate optimal worker count for a subsystem based on workload
     * @param baseAllocation Guaranteed allocation for the subsystem
     * @param workloadSize Current workload (entities, events, etc.)
     * @param workloadThreshold Threshold above which buffer should be used
     * @return Optimal number of workers (base + buffer if appropriate)
     */
    size_t getOptimalWorkerCount(size_t baseAllocation, size_t workloadSize, size_t workloadThreshold) const {
        if (workloadSize > workloadThreshold && remaining > 0) {
            // Use burst capacity for high workloads - take half the buffer to be conservative
            size_t burstWorkers = std::min(remaining, baseAllocation);
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
 * @brief Worker allocation percentages and limits
 */
static constexpr size_t AI_WORKER_PERCENTAGE = 60;     // 60% of remaining workers
static constexpr size_t EVENT_WORKER_PERCENTAGE = 30;  // 30% of remaining workers
static constexpr size_t ENGINE_MIN_WORKERS = 1;        // Minimum workers for GameEngine
static constexpr size_t ENGINE_OPTIMAL_WORKERS = 2;    // Optimal workers for GameEngine on higher-end systems

/**
 * @brief Calculate optimal worker budget allocation with hardware-adaptive scaling
 * 
 * @param availableWorkers Total workers available in ThreadSystem
 * @return WorkerBudget Allocation strategy for all subsystems
 * 
 * Tiered Strategy (prevents over-allocation):
 * - Tier 1 (≤1 workers): GameLoop=1, AI=0 (single-threaded), Events=0 (single-threaded)
 * - Tier 2 (2-4 workers): GameLoop=1, AI=1 if possible, Events=0 (shares with AI)
 * - Tier 3 (5+ workers): GameLoop=2, AI=60% of remaining, Events=30% of remaining
 * 
 * Target minimum (7 workers): GameLoop=2, AI=3, Events=1, Buffer=1
 * Buffer threads can be used by AI/Events during high workload periods.
 */
inline WorkerBudget calculateWorkerBudget(size_t availableWorkers) {
    WorkerBudget budget;
    budget.totalWorkers = availableWorkers;
    
    // Handle edge cases for very low-end systems
    if (availableWorkers <= 1) {
        // Tier 1: Ultra low-end - GameLoop gets priority, others single-threaded
        budget.engineReserved = 1;
        budget.aiAllocated = 0;      // Single-threaded fallback
        budget.eventAllocated = 0;   // Single-threaded fallback
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
    
    if (remainingWorkers <= 1) {
        // Tier 2: Low-end - Conservative allocation
        budget.aiAllocated = (remainingWorkers >= 1) ? 1 : 0;
        budget.eventAllocated = 0;  // Share with AI or single-threaded
    } else {
        // Tier 3: High-end - Full multi-threading with percentages
        budget.aiAllocated = std::max(size_t(1), (remainingWorkers * AI_WORKER_PERCENTAGE) / 100);
        budget.eventAllocated = std::max(size_t(1), (remainingWorkers * EVENT_WORKER_PERCENTAGE) / 100);
    }
    
    // Calculate truly remaining workers (buffer for other tasks)
    size_t allocated = budget.aiAllocated + budget.eventAllocated;
    budget.remaining = (remainingWorkers > allocated) ? remainingWorkers - allocated : 0;
    
    // Validation: Ensure we never over-allocate
    size_t totalAllocated = budget.engineReserved + budget.aiAllocated + budget.eventAllocated;
    if (totalAllocated > availableWorkers) {
        // Emergency fallback - should never happen with correct logic above
        budget.aiAllocated = 0;
        budget.eventAllocated = 0;
        budget.remaining = availableWorkers - budget.engineReserved;
    }
    
    return budget;
}

} // namespace Forge

#endif // WORKER_BUDGET_HPP