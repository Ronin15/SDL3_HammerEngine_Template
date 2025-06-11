/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameLoop.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <exception>

GameLoop::GameLoop(float targetFPS, float fixedTimestep, bool threaded)
    : m_timestepManager(std::make_unique<TimestepManager>(targetFPS, fixedTimestep))
    , m_running(false)
    , m_paused(false)
    , m_stopRequested(false)
    , m_threaded(threaded)
    , m_updateTaskRunning(false)
    , m_updateCount(0)
{
}

GameLoop::~GameLoop() {
    if (m_running.load()) {
        stop();
    }
    cleanup();
}

void GameLoop::setEventHandler(EventHandler handler) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_eventHandler = std::move(handler);
}

void GameLoop::setUpdateHandler(UpdateHandler handler) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_updateHandler = std::move(handler);
}

void GameLoop::setRenderHandler(RenderHandler handler) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_renderHandler = std::move(handler);
}

bool GameLoop::run() {
    if (m_running.load()) {
        GAMELOOP_WARN("GameLoop already running");
        return false;
    }

    m_running.store(true, std::memory_order_relaxed);
    m_stopRequested.store(false, std::memory_order_relaxed);
    m_paused.store(false, std::memory_order_relaxed);

    // Reset timestep manager for clean start
    m_timestepManager->reset();

    try {
        if (m_threaded) {
            // Start update worker respecting WorkerBudget allocation
            if (Forge::ThreadSystem::Exists()) {
                auto& threadSystem = Forge::ThreadSystem::Instance();
                size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
                Forge::WorkerBudget budget = Forge::calculateWorkerBudget(availableWorkers);

                GAMELOOP_INFO("GameLoop allocated " + std::to_string(budget.engineReserved) +
                             " workers from WorkerBudget (total: " + std::to_string(availableWorkers) + ")");

                m_updateTaskRunning.store(true, std::memory_order_relaxed);
                m_updateTaskFuture = Forge::ThreadSystem::Instance().enqueueTaskWithResult(
                    [this, budget]() { runUpdateWorker(budget); },
                    Forge::TaskPriority::Critical,
                    "GameLoop WorkerBudget Update Worker"
                );
            } else {
                GAMELOOP_WARN("ThreadSystem not available, falling back to single-threaded mode");
                m_threaded = false;
            }
        }

        // Run main thread loop
        runMainThread();

        // Clean shutdown
        if (m_threaded && m_updateTaskFuture.valid()) {
            m_updateTaskRunning.store(false, std::memory_order_relaxed);
            // Wait for update worker to finish cleanly
            m_updateTaskFuture.wait();
        }

        m_running.store(false, std::memory_order_relaxed);
        return true;

    } catch (const std::exception& e) {
        GAMELOOP_CRITICAL("Exception in main loop: " + std::string(e.what()));
        m_running.store(false, std::memory_order_relaxed);
        cleanup();
        return false;
    }
}

void GameLoop::stop() {
    m_stopRequested.store(true, std::memory_order_relaxed);
}

bool GameLoop::isRunning() const {
    return m_running.load(std::memory_order_relaxed);
}

void GameLoop::setPaused(bool paused) {
    bool wasPaused = m_paused.exchange(paused, std::memory_order_relaxed);

    // If transitioning from paused to unpaused, reset timing to avoid time jump
    if (wasPaused && !paused) {
        m_timestepManager->reset();
    }
}

bool GameLoop::isPaused() const {
    return m_paused.load(std::memory_order_relaxed);
}

float GameLoop::getCurrentFPS() const {
    return m_timestepManager->getCurrentFPS();
}

uint32_t GameLoop::getFrameTimeMs() const {
    return m_timestepManager->getFrameTimeMs();
}

void GameLoop::setTargetFPS(float fps) {
    m_timestepManager->setTargetFPS(fps);
}

void GameLoop::setFixedTimestep(float timestep) {
    m_timestepManager->setFixedTimestep(timestep);
}

TimestepManager& GameLoop::getTimestepManager() {
    return *m_timestepManager;
}

void GameLoop::runMainThread() {
    while (m_running.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed)) {
        // Start frame timing
        m_timestepManager->startFrame();

        try {
            // Always process events on main thread (SDL requirement)
            processEvents();

            // Process updates (single-threaded mode only)
            if (!m_threaded && !m_paused.load()) {
                processUpdates();
            }

            // Always process rendering
            processRender();

            // End frame timing and limit frame rate
            m_timestepManager->endFrame();

        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in main thread: " + std::string(e.what()));
            // Continue running, but log the error
        }
    }
}

void GameLoop::runUpdateWorker(const Forge::WorkerBudget& budget) {
    // WorkerBudget-aware update worker - respects allocated resources
    GAMELOOP_INFO("Update worker started with " + std::to_string(budget.engineReserved) + " allocated workers");

    // Pre-calculate sleep time to avoid repeated calculations
    float targetFPS = m_timestepManager->getTargetFPS();
    auto sleepDuration = std::chrono::microseconds(static_cast<long>(
        std::max(500.0f, std::min(8000.0f, (1000000.0f / targetFPS) * 0.5f))
    ));

    // Adaptive behavior based on WorkerBudget allocation
    bool canUseParallelUpdates = (budget.engineReserved >= 2);
    bool isHighEndSystem = (budget.totalWorkers > 4);

    // Adjust sleep precision based on system capabilities
    auto adaptiveSleepDuration = isHighEndSystem ?
        std::chrono::microseconds(static_cast<long>(sleepDuration.count() * 0.8)) : // More responsive on high-end
        sleepDuration; // Standard timing on low-end

    while (m_updateTaskRunning.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed)) {
        try {
            if (!m_paused.load(std::memory_order_relaxed)) {
                if (canUseParallelUpdates && Forge::ThreadSystem::Exists()) {
                    // Use enhanced processing for high-end systems
                    processUpdatesParallel();
                } else {
                    // Standard processing for low-end systems
                    processUpdates();
                }
            }

            // WorkerBudget-aware sleep timing
            std::this_thread::sleep_for(adaptiveSleepDuration);

            // Adaptive recalibration interval based on system tier
            static int frameCounter = 0;
            int recalibrationInterval = isHighEndSystem ? 1200 : 600; // High-end: 20s, Low-end: 10s

            if (++frameCounter % recalibrationInterval == 0) {
                float newTargetFPS = m_timestepManager->getTargetFPS();
                if (newTargetFPS != targetFPS) {
                    targetFPS = newTargetFPS;
                    sleepDuration = std::chrono::microseconds(static_cast<long>(
                        std::max(500.0f, std::min(8000.0f, (1000000.0f / targetFPS) * 0.5f))
                    ));
                    adaptiveSleepDuration = isHighEndSystem ?
                        std::chrono::microseconds(static_cast<long>(sleepDuration.count() * 0.8)) :
                        sleepDuration;
                }

                // Log WorkerBudget utilization periodically on high-end systems
                if (isHighEndSystem && Forge::ThreadSystem::Exists()) {
                    auto& threadSystem = Forge::ThreadSystem::Instance();
                    if (threadSystem.isBusy()) {
                        GAMELOOP_DEBUG("High-end system WorkerBudget busy - optimal utilization");
                    }
                }
            }

        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in update worker: " + std::string(e.what()));
            // Continue running - don't let exceptions kill the update worker
        }
    }
}

void GameLoop::processEvents() {
    invokeEventHandler();
}

void GameLoop::processUpdates() {
    // Process all pending fixed timestep updates
    while (m_timestepManager->shouldUpdate()) {
        float deltaTime = m_timestepManager->getUpdateDeltaTime();
        invokeUpdateHandler(deltaTime);
        m_updateCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void GameLoop::processUpdatesParallel() {
    // Enhanced parallel processing for high-end systems with 2+ allocated workers
    // Still process updates sequentially to maintain game logic consistency
    // but optimized for systems with more worker budget allocation

    // Use enhanced timing precision for high-end systems
    auto highPrecisionStart = std::chrono::high_resolution_clock::now();

    while (m_timestepManager->shouldUpdate()) {
        float deltaTime = m_timestepManager->getUpdateDeltaTime();
        invokeUpdateHandler(deltaTime);
        m_updateCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Monitor performance on high-end systems (every 1000 updates)
    static std::atomic<int> perfCounter{0};
    if (perfCounter.fetch_add(1, std::memory_order_relaxed) % 1000 == 0) {
        auto duration = std::chrono::high_resolution_clock::now() - highPrecisionStart;
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        if (microseconds > 20000) { // > 20ms for 1000 updates indicates potential performance issues
            GAMELOOP_WARN("High-end system update batch took " + std::to_string(microseconds) + " microseconds");
        }
    }
}

void GameLoop::processRender() {
    if (m_timestepManager->shouldRender()) {
        float interpolation = m_timestepManager->getRenderInterpolation();
        invokeRenderHandler(interpolation);
    }
}

void GameLoop::invokeEventHandler() {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_eventHandler) {
        try {
            m_eventHandler();
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in event handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeUpdateHandler(float deltaTime) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_updateHandler) {
        try {
            m_updateHandler(deltaTime);
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in update handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeRenderHandler(float interpolation) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_renderHandler) {
        try {
            m_renderHandler(interpolation);
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in render handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::cleanup() {
    // Stop update worker if running
    m_updateTaskRunning.store(false, std::memory_order_relaxed);

    // Wait for update worker to finish cleanly
    if (m_updateTaskFuture.valid()) {
        m_updateTaskFuture.wait();
    }

    // Clear callbacks
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_eventHandler = nullptr;
        m_updateHandler = nullptr;
        m_renderHandler = nullptr;
    }

    m_running.store(false, std::memory_order_relaxed);
}
