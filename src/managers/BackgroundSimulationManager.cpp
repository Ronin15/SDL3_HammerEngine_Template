/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#include "managers/BackgroundSimulationManager.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include "managers/EntityDataManager.hpp"
#include <algorithm>
#include <chrono>
#include <format>

// Logger macros for BackgroundSimulationManager
#define BGSIM_DEBUG(msg) HAMMER_DEBUG("BackgroundSim", msg)
#define BGSIM_INFO(msg) HAMMER_INFO("BackgroundSim", msg)
#define BGSIM_WARNING(msg) HAMMER_WARN("BackgroundSim", msg)
#define BGSIM_ERROR(msg) HAMMER_ERROR("BackgroundSim", msg)

// ============================================================================
// LIFECYCLE
// ============================================================================

bool BackgroundSimulationManager::init() {
    if (m_initialized.load(std::memory_order_acquire)) {
        BGSIM_WARNING("BackgroundSimulationManager already initialized");
        return true;
    }

    // Verify dependencies
    if (!EntityDataManager::Instance().isInitialized()) {
        BGSIM_ERROR("EntityDataManager must be initialized before BackgroundSimulationManager");
        return false;
    }

    // Reserve buffers
    m_backgroundIndices.reserve(10000);  // Expect up to 10K background entities
    m_batchFutures.reserve(16);          // Reasonable batch count

    m_initialized.store(true, std::memory_order_release);
    BGSIM_INFO("BackgroundSimulationManager initialized successfully");
    return true;
}

void BackgroundSimulationManager::clean() {
    if (!m_initialized.load(std::memory_order_acquire)) {
        return;
    }

    BGSIM_INFO("Cleaning up BackgroundSimulationManager...");

    // Mark as shutdown to prevent new work
    m_isShutdown.store(true, std::memory_order_release);

    // Wait for any pending async work
    waitForAsyncCompletion();

    // Clear buffers
    m_backgroundIndices.clear();
    m_backgroundIndices.shrink_to_fit();

    m_initialized.store(false, std::memory_order_release);
    BGSIM_INFO("BackgroundSimulationManager cleaned up");
}

void BackgroundSimulationManager::prepareForStateTransition() {
    BGSIM_INFO("Preparing for state transition...");
    waitForAsyncCompletion();
    m_backgroundIndices.clear();
    m_tiersDirty.store(true, std::memory_order_release);
    m_framesSinceTierUpdate = 0;
    m_referencePointSet = false;  // Force reference point update on next state
    m_accumulator = 0.0;          // Reset timing for clean start
    BGSIM_INFO("State transition preparation complete");
}

// ============================================================================
// MAIN UPDATE (Accumulator Pattern - like TimestepManager)
// ============================================================================

void BackgroundSimulationManager::update(float deltaTime) {
    // Early exit checks (follows AIManager pattern)
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_isShutdown.load(std::memory_order_acquire)) {
        return;
    }

    // Skip processing if globally paused
    if (m_globallyPaused.load(std::memory_order_acquire)) {
        return;
    }

    // Accumulate real time passed
    m_accumulator += deltaTime;

    // Only process when enough time has accumulated for target update rate (e.g., 30Hz)
    if (m_accumulator < m_updateInterval) {
        return;  // Not time yet
    }

    // Process updates - consume accumulated time (handles catch-up for slow frames)
    while (m_accumulator >= m_updateInterval) {
        m_accumulator -= m_updateInterval;
        processBackgroundEntities(m_updateInterval);
    }
}

void BackgroundSimulationManager::processBackgroundEntities(float fixedDeltaTime) {
    auto t0 = std::chrono::steady_clock::now();

    // Periodic tier update (still frame-based for consistency with EDM)
    m_framesSinceTierUpdate++;
    if (m_tiersDirty.load(std::memory_order_acquire) ||
        m_framesSinceTierUpdate >= m_tierUpdateInterval) {
        updateTiers();
        m_framesSinceTierUpdate = 0;
        m_tiersDirty.store(false, std::memory_order_release);
    }

    // Get background tier indices from EntityDataManager
    auto& edm = EntityDataManager::Instance();
    auto backgroundSpan = edm.getBackgroundIndices();

    // Update work-tracking flag for hasWork() optimization
    bool hasBackground = !backgroundSpan.empty();
    m_hasNonActiveEntities.store(hasBackground, std::memory_order_release);

    if (!hasBackground) {
        m_perf.lastEntitiesProcessed = 0;
        m_perf.lastUpdateMs = 0.0;
        return;
    }

    // Copy to local buffer (span may be invalidated during processing)
    m_backgroundIndices.clear();
    m_backgroundIndices.insert(m_backgroundIndices.end(),
                               backgroundSpan.begin(), backgroundSpan.end());

    const size_t entityCount = m_backgroundIndices.size();

    // Use centralized WorkerBudgetManager for smart worker allocation (follows AIManager pattern)
    auto& budgetMgr = HammerEngine::WorkerBudgetManager::Instance();

    // Get optimal workers (WorkerBudget determines everything dynamically)
    // Use AI system type since background sim processes NPC-like entities
    size_t optimalWorkerCount = budgetMgr.getOptimalWorkers(
        HammerEngine::SystemType::AI, entityCount);

    // Get adaptive batch strategy
    auto [batchCount, batchSize] = budgetMgr.getBatchStrategy(
        HammerEngine::SystemType::AI, entityCount, optimalWorkerCount);

    // Decide threading strategy
    bool useThreading = entityCount >= MIN_ENTITIES_FOR_THREADING &&
                        batchCount > 1 &&
                        batchSize >= MIN_BATCH_SIZE;

    if (useThreading) {
        processMultiThreaded(fixedDeltaTime, m_backgroundIndices, batchCount, batchSize);
        m_perf.lastWasThreaded = true;
        m_perf.lastBatchCount = batchCount;
    } else {
        processSingleThreaded(fixedDeltaTime, m_backgroundIndices);
        m_perf.lastWasThreaded = false;
        m_perf.lastBatchCount = 1;
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    m_perf.lastEntitiesProcessed = entityCount;
    m_perf.lastUpdateMs = elapsedMs;
    m_perf.updateAverage(elapsedMs);

    // Periodic logging (every 300 frames, similar to other managers)
    if (m_perf.totalUpdates % 300 == 0 && entityCount > 0) {
        BGSIM_DEBUG(std::format(
            "Background Sim - Entities: {}, Update: {:.2f}ms, Avg: {:.2f}ms [{}]",
            entityCount, elapsedMs, m_perf.avgUpdateMs,
            useThreading ? std::format("{} batches", batchCount) : "Single-threaded"));
    }
}

void BackgroundSimulationManager::waitForAsyncCompletion() {
    std::lock_guard<std::mutex> lock(m_futuresMutex);
    for (auto& future : m_batchFutures) {
        if (future.valid()) {
            future.wait();
        }
    }
    m_batchFutures.clear();
}

// ============================================================================
// TIER MANAGEMENT
// ============================================================================

void BackgroundSimulationManager::setReferencePoint(const Vector2D& position) {
    // First call: always set reference point (m_referencePointSet starts false)
    if (!m_referencePointSet) {
        m_referencePoint = position;
        m_referencePointSet = true;
        m_tiersDirty.store(true, std::memory_order_release);
        return;
    }

    // Subsequent calls: only update if moved significantly (avoid excessive tier recalcs)
    float dx = position.getX() - m_referencePoint.getX();
    float dy = position.getY() - m_referencePoint.getY();
    float distSq = dx * dx + dy * dy;

    // Threshold: 100 units movement triggers tier recalculation
    constexpr float MOVEMENT_THRESHOLD_SQ = 100.0f * 100.0f;

    if (distSq > MOVEMENT_THRESHOLD_SQ) {
        m_referencePoint = position;
        m_tiersDirty.store(true, std::memory_order_release);
    }
}

void BackgroundSimulationManager::updateTiers() {
    auto& edm = EntityDataManager::Instance();
    if (!edm.isInitialized()) {
        return;
    }

    // Delegate to EntityDataManager
    edm.updateSimulationTiers(m_referencePoint, m_activeRadius, m_backgroundRadius);
}

// ============================================================================
// BATCH PROCESSING (follows AIManager pattern)
// ============================================================================

void BackgroundSimulationManager::processSingleThreaded(float deltaTime,
                                                        const std::vector<size_t>& indices) {
    processBatch(deltaTime, indices, 0, indices.size());
}

void BackgroundSimulationManager::processMultiThreaded(float deltaTime,
                                                       const std::vector<size_t>& indices,
                                                       size_t batchCount,
                                                       size_t batchSize) {
    auto& threadSystem = HammerEngine::ThreadSystem::Instance();

    // Clear previous futures (reuse capacity)
    {
        std::lock_guard<std::mutex> lock(m_futuresMutex);
        m_batchFutures.clear();
        m_batchFutures.reserve(batchCount);
    }

    const size_t totalCount = indices.size();
    size_t remainingEntities = totalCount % batchCount;

    for (size_t batch = 0; batch < batchCount; ++batch) {
        size_t startIdx = batch * batchSize;
        size_t endIdx = startIdx + batchSize;

        // Add remaining entities to last batch
        if (batch == batchCount - 1) {
            endIdx += remainingEntities;
        }

        if (startIdx >= endIdx || startIdx >= totalCount) continue;
        endIdx = std::min(endIdx, totalCount);

        // Submit task with future for completion tracking (follows AIManager pattern)
        auto future = threadSystem.enqueueTaskWithResult(
            [this, deltaTime, &indices, startIdx, endIdx]() -> void {
                try {
                    processBatch(deltaTime, indices, startIdx, endIdx);
                } catch (const std::exception& e) {
                    BGSIM_ERROR(std::format("Exception in background sim batch: {}", e.what()));
                } catch (...) {
                    BGSIM_ERROR("Unknown exception in background sim batch");
                }
            },
            HammerEngine::TaskPriority::Low,  // Background sim is low priority
            "BGSim_Batch"
        );

        {
            std::lock_guard<std::mutex> lock(m_futuresMutex);
            m_batchFutures.push_back(std::move(future));
        }
    }

    // Wait for all batches to complete
    waitForAsyncCompletion();
}

void BackgroundSimulationManager::processBatch(float deltaTime,
                                               const std::vector<size_t>& indices,
                                               size_t startIdx,
                                               size_t endIdx) {
    auto& edm = EntityDataManager::Instance();

    for (size_t i = startIdx; i < endIdx; ++i) {
        size_t index = indices[i];

        // Get entity kind to determine simulation type
        const auto& hot = edm.getHotDataByIndex(index);

        if (!hot.isAlive()) continue;

        switch (hot.kind) {
            case EntityKind::NPC:
                simulateNPC(deltaTime, index);
                break;
            case EntityKind::DroppedItem:
                simulateItem(deltaTime, index);
                break;
            // Projectiles and AreaEffects should always be Active tier
            // Containers, Harvestables, Props, Triggers don't need background sim
            default:
                break;
        }
    }
}

// ============================================================================
// TYPE-SPECIFIC SIMPLIFIED SIMULATION
// ============================================================================

void BackgroundSimulationManager::simulateNPC(float deltaTime, size_t index) {
    auto& edm = EntityDataManager::Instance();
    auto& transform = edm.getTransformByIndex(index);

    // Simplified NPC simulation for background tier:
    // - Apply basic velocity to position (no collision detection)
    // - Gradual velocity decay (simulates slowing down)
    // - No AI behavior execution (too expensive)

    // Store previous position for interpolation (if entity becomes Active again)
    transform.previousPosition = transform.position;

    // Apply velocity with decay
    constexpr float VELOCITY_DECAY = 0.98f;  // 2% decay per frame
    constexpr float MIN_VELOCITY_SQ = 0.1f;  // Stop if velocity is negligible

    float velMagSq = transform.velocity.getX() * transform.velocity.getX() +
                     transform.velocity.getY() * transform.velocity.getY();

    if (velMagSq > MIN_VELOCITY_SQ) {
        // Apply velocity to position
        transform.position = transform.position + (transform.velocity * deltaTime);

        // Decay velocity
        transform.velocity = transform.velocity * VELOCITY_DECAY;
    } else {
        // Stop movement
        transform.velocity = Vector2D(0.0f, 0.0f);
    }
}

void BackgroundSimulationManager::simulateItem(float deltaTime, size_t index) {
    auto& edm = EntityDataManager::Instance();

    // Items in background tier:
    // - Update pickup timer if still counting down
    // - Could implement despawn timer for dropped items

    // Get item data
    auto hotData = edm.getHotDataByIndex(index);
    if (hotData.kind != EntityKind::DroppedItem) return;

    // Items don't need position simulation - they stay where dropped
    // The main purpose of background item simulation is:
    // 1. Despawn after long time
    // 2. Keep timers updated

    // Note: Actual despawn logic would use ItemData from EntityDataManager
    // For now, items in background tier are just preserved
    (void)deltaTime;  // Unused for now
}
