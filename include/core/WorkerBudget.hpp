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
 */
struct WorkerBudget {
    size_t totalWorkers;      // Total available worker threads
    size_t engineReserved;    // Workers reserved for GameEngine critical tasks
    size_t aiAllocated;       // Workers allocated to AIManager
    size_t eventAllocated;    // Workers allocated to EventManager
    size_t remaining;         // Remaining workers for other tasks
};

/**
 * @brief Worker allocation percentages and limits
 */
static constexpr size_t AI_WORKER_PERCENTAGE = 60;     // 60% of remaining workers
static constexpr size_t EVENT_WORKER_PERCENTAGE = 30;  // 30% of remaining workers
static constexpr size_t ENGINE_MIN_WORKERS = 1;        // Minimum workers for GameEngine
static constexpr size_t ENGINE_OPTIMAL_WORKERS = 2;    // Optimal workers for GameEngine on higher-end systems

/**
 * @brief Calculate optimal worker budget allocation
 * 
 * @param availableWorkers Total workers available in ThreadSystem
 * @return WorkerBudget Allocation strategy for all subsystems
 * 
 * Strategy:
 * - GameEngine gets 1 worker on low-end systems (≤4 cores), 2 workers on higher-end systems
 * - AI gets 60% of remaining workers
 * - Events get 30% of remaining workers  
 * - 10% buffer left for other tasks
 */
inline WorkerBudget calculateWorkerBudget(size_t availableWorkers) {
    WorkerBudget budget;
    budget.totalWorkers = availableWorkers;
    
    // Dynamic GameEngine worker allocation based on available cores
    // Low-end systems (≤4 workers): 1 worker for GameEngine coordination
    // Higher-end systems (>4 workers): 2 workers for GameEngine tasks
    if (availableWorkers <= 2) {
        budget.engineReserved = 1;  // Very limited systems
    } else if (availableWorkers <= 4) {
        budget.engineReserved = ENGINE_MIN_WORKERS;  // Low-end systems: 1 worker
    } else {
        budget.engineReserved = ENGINE_OPTIMAL_WORKERS;  // Higher-end systems: 2 workers
    }
    
    // Calculate remaining workers after engine reservation
    size_t remainingWorkers = availableWorkers - budget.engineReserved;
    
    // Allocate percentages of remaining workers (minimum 1 each)
    budget.aiAllocated = std::max(size_t(1), (remainingWorkers * AI_WORKER_PERCENTAGE) / 100);
    budget.eventAllocated = std::max(size_t(1), (remainingWorkers * EVENT_WORKER_PERCENTAGE) / 100);
    
    // Calculate truly remaining workers (buffer for other tasks)
    size_t allocated = budget.aiAllocated + budget.eventAllocated;
    budget.remaining = (remainingWorkers > allocated) ? remainingWorkers - allocated : 0;
    
    return budget;
}

} // namespace Forge

#endif // WORKER_BUDGET_HPP