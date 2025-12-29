/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef BACKGROUND_SIMULATION_MANAGER_HPP
#define BACKGROUND_SIMULATION_MANAGER_HPP

/**
 * @file BackgroundSimulationManager.hpp
 * @brief Simplified simulation for off-screen (Background tier) entities
 *
 * BackgroundSimulationManager processes entities that are outside the active
 * camera area but still need some basic simulation to maintain world consistency.
 *
 * Processing differences by tier:
 * - Active: Full AI, collision, rendering (handled by AIManager, CollisionManager)
 * - Background: Position-only updates, no collision, no rendering (this manager)
 * - Hibernated: No updates, data stored only
 *
 * Threading Model (follows AIManager pattern):
 * - Uses WorkerBudget for adaptive batch sizing
 * - Submits batches to ThreadSystem for parallel processing
 * - Runs during vsync wait via processBackgroundTasks()
 * - Low priority tasks that don't block frame rendering
 */

#include "entities/EntityHandle.hpp"
#include "utils/Vector2D.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations
class EntityDataManager;

class BackgroundSimulationManager {
public:
    static BackgroundSimulationManager& Instance() {
        static BackgroundSimulationManager instance;
        return instance;
    }

    /**
     * @brief Initialize the background simulation manager
     * @return true if initialization successful
     */
    bool init();

    /**
     * @brief Clean up resources
     */
    void clean();

    /**
     * @brief Prepare for state transition (clear pending work)
     */
    void prepareForStateTransition();

    /**
     * @brief Check if manager is initialized
     */
    [[nodiscard]] bool isInitialized() const noexcept {
        return m_initialized.load(std::memory_order_acquire);
    }

    /**
     * @brief Process background tier entities (main update)
     *
     * Called from game loop. Uses WorkerBudget to determine threading strategy.
     * For small entity counts: single-threaded
     * For large counts: batched parallel processing via ThreadSystem
     *
     * @param deltaTime Time since last update
     */
    void update(float deltaTime);

    /**
     * @brief Wait for any async background processing to complete
     *
     * Call before state transitions or when synchronization is needed.
     */
    void waitForAsyncCompletion();

    /**
     * @brief Set the reference point for tier calculations
     *
     * Typically the player position. Entities are tiered based on
     * distance from this point.
     *
     * @param position Reference position (usually player/camera)
     */
    void setReferencePoint(const Vector2D& position);

    /**
     * @brief Get current reference point
     */
    [[nodiscard]] Vector2D getReferencePoint() const { return m_referencePoint; }

    /**
     * @brief Update entity simulation tiers based on reference point
     *
     * Should be called periodically (e.g., every 60 frames) to reassign
     * entities to Active/Background/Hibernated tiers.
     * Uses EntityDataManager::updateSimulationTiers() internally.
     */
    void updateTiers();

    /**
     * @brief Force tier update on next frame
     */
    void invalidateTiers() { m_tiersDirty.store(true, std::memory_order_release); }

    // Configuration
    void setActiveRadius(float radius) { m_activeRadius = radius; }
    void setBackgroundRadius(float radius) { m_backgroundRadius = radius; }
    void setTierUpdateInterval(uint32_t frames) { m_tierUpdateInterval = frames; }

    [[nodiscard]] float getActiveRadius() const { return m_activeRadius; }
    [[nodiscard]] float getBackgroundRadius() const { return m_backgroundRadius; }
    [[nodiscard]] uint32_t getTierUpdateInterval() const { return m_tierUpdateInterval; }

    // Performance metrics (follows AIManager pattern)
    struct PerfStats {
        double lastUpdateMs{0.0};
        double avgUpdateMs{0.0};
        size_t lastEntitiesProcessed{0};
        size_t lastBatchCount{0};
        size_t lastTierChanges{0};
        uint64_t totalUpdates{0};
        bool lastWasThreaded{false};

        static constexpr double ALPHA = 0.05;  // EMA smoothing

        void updateAverage(double newMs) {
            if (totalUpdates == 0) {
                avgUpdateMs = newMs;
            } else {
                avgUpdateMs = ALPHA * newMs + (1.0 - ALPHA) * avgUpdateMs;
            }
            totalUpdates++;
        }
    };

    [[nodiscard]] const PerfStats& getPerfStats() const { return m_perf; }
    void resetPerfStats() { m_perf = PerfStats{}; }

private:
    BackgroundSimulationManager() = default;
    ~BackgroundSimulationManager() = default;

    BackgroundSimulationManager(const BackgroundSimulationManager&) = delete;
    BackgroundSimulationManager& operator=(const BackgroundSimulationManager&) = delete;

    // Batch processing (follows AIManager pattern)
    void processSingleThreaded(float deltaTime, const std::vector<size_t>& indices);
    void processMultiThreaded(float deltaTime, const std::vector<size_t>& indices,
                              size_t batchCount, size_t batchSize);
    void processBatch(float deltaTime, const std::vector<size_t>& indices,
                      size_t startIdx, size_t endIdx);

    // Type-specific simplified simulation
    void simulateNPC(float deltaTime, size_t index);
    void simulateItem(float deltaTime, size_t index);

    // Configuration
    float m_activeRadius{1500.0f};      // Entities within this are Active
    float m_backgroundRadius{10000.0f}; // Entities within this (outside active) are Background
    uint32_t m_tierUpdateInterval{60};  // Update tiers every N frames

    // State
    Vector2D m_referencePoint{0.0f, 0.0f};
    uint32_t m_framesSinceTierUpdate{0};
    std::atomic<bool> m_tiersDirty{true};
    std::atomic<bool> m_initialized{false};

    // Async task tracking (follows AIManager pattern)
    std::vector<std::future<void>> m_batchFutures;
    mutable std::mutex m_futuresMutex;

    // Reusable buffers (avoid per-frame allocation)
    std::vector<size_t> m_backgroundIndices;

    // Performance tracking
    PerfStats m_perf;

    // Threading thresholds (tuned for background simulation)
    static constexpr size_t MIN_ENTITIES_FOR_THREADING = 500;
    static constexpr size_t MIN_BATCH_SIZE = 64;
};

#endif // BACKGROUND_SIMULATION_MANAGER_HPP
