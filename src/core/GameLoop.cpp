/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameLoop.hpp"
#include "core/Logger.hpp"
#include "core/ThreadSystem.hpp"
#include "core/WorkerBudget.hpp"
#include <chrono>
#include <exception>
#include <format>

GameLoop::GameLoop(float targetFPS, float fixedTimestep, bool threaded)
    : m_timestepManager(std::make_unique<TimestepManager>(targetFPS, fixedTimestep))
    , m_running(false)
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

    // Reset timestep manager for clean start
    m_timestepManager->reset();

    try {
        if (m_threaded) {
            // Start update worker respecting WorkerBudget allocation
            if (HammerEngine::ThreadSystem::Exists()) {
                const auto& threadSystem = HammerEngine::ThreadSystem::Instance();
                size_t availableWorkers = static_cast<size_t>(threadSystem.getThreadCount());
                size_t remainingForManagers = (availableWorkers > 1) ? (availableWorkers - 1) : 0;

                GAMELOOP_INFO("GameLoop using 1 worker (" +
                             std::to_string(remainingForManagers) + " remaining for managers, " +
                             std::to_string(availableWorkers) + " total)");

                m_updateTaskRunning.store(true, std::memory_order_relaxed);
                m_updateTaskFuture = HammerEngine::ThreadSystem::Instance().enqueueTaskWithResult(
                    [this]() { runUpdateWorker(); },
                    HammerEngine::TaskPriority::Critical,
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

float GameLoop::getCurrentFPS() const {
    return m_timestepManager->getCurrentFPS();
}

uint32_t GameLoop::getFrameTimeMs() const {
    return m_timestepManager->getFrameTimeMs();
}

void GameLoop::setTargetFPS(float fps) {
    m_timestepManager->setTargetFPS(fps);
}

float GameLoop::getTargetFPS() const {
    return m_timestepManager->getTargetFPS();
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
            if (!m_threaded) {
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

void GameLoop::runUpdateWorker() {
    // Update worker running on dedicated thread from WorkerBudget allocation

    // Adaptive timing system
    float targetFPS = m_timestepManager->getTargetFPS();
    const auto targetFrameTime = std::chrono::microseconds(static_cast<long>(1000000.0f / targetFPS));

    // Performance tracking for adaptive sleep
    auto lastUpdateStart = std::chrono::high_resolution_clock::now();
    auto avgUpdateTime = std::chrono::microseconds(0);
    const int performanceSamples = 10;
    int sampleCount = 0;

    // Initial fixed sleep duration for consistent frame timing
    const auto fixedSleepDuration = std::chrono::microseconds(
        static_cast<long>(targetFrameTime.count() * 0.92f)
    );

        while (m_updateTaskRunning.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed)) {
        try {
            auto updateStart = std::chrono::high_resolution_clock::now();

            // Process all pending updates sequentially
            processUpdates();

            auto updateEnd = std::chrono::high_resolution_clock::now();
            auto updateDuration = std::chrono::duration_cast<std::chrono::microseconds>(
                updateEnd - updateStart
            );

            // Update rolling average of update time
            if (sampleCount < performanceSamples) {
                avgUpdateTime = (avgUpdateTime * sampleCount + updateDuration) / (sampleCount + 1);
                sampleCount++;
            } else {
                avgUpdateTime = (avgUpdateTime * (performanceSamples - 1) + updateDuration) / performanceSamples;
            }

            // WAYLAND FRAME TIMING STABILIZATION
            // Calculate frame timing with consistent sleep to eliminate jitter
            auto frameTimeElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                updateEnd - lastUpdateStart
            );
            auto remainingTime = targetFrameTime - frameTimeElapsed;

            if (remainingTime.count() > 0) {
                // Use FIXED sleep timing for consistent frame delivery on Wayland
                // This eliminates the adaptive sleep variations that cause NPC bouncing
                
                // Only use fixed timing if we have sufficient remaining time
                if (remainingTime >= fixedSleepDuration) {
                    std::this_thread::sleep_for(fixedSleepDuration);
                    
                    // Micro-sleep for remaining time to hit precise timing
                    auto afterSleep = std::chrono::high_resolution_clock::now();
                    auto actualRemaining = targetFrameTime - std::chrono::duration_cast<std::chrono::microseconds>(
                        afterSleep - lastUpdateStart
                    );
                    
                    if (actualRemaining.count() > 50) { // Only micro-sleep if >50μs remaining
                        std::this_thread::sleep_for(std::chrono::microseconds(actualRemaining.count() / 2));
                    }
                } else {
                    // Fallback to shorter fixed sleep if running behind
                    const auto shortSleep = std::chrono::microseconds(100);
                    std::this_thread::sleep_for(shortSleep);
                }
            } else {
                // Running behind - yield to other threads
                std::this_thread::yield();
            }

            lastUpdateStart = updateStart;

            // Periodic recalibration and logging (reduced frequency to minimize jitter)
            const int recalibrationInterval = 1800; // Every 30 seconds at 60 FPS (was 5 seconds)

            if (++m_updateCount % recalibrationInterval == 0) {
                float newTargetFPS = m_timestepManager->getTargetFPS();
                if (std::abs(newTargetFPS - targetFPS) > 0.1f) {
                    targetFPS = newTargetFPS;
                    GAMELOOP_DEBUG(std::format("Target FPS changed to: {}", targetFPS));
                }

                // Log performance metrics (only if logging is enabled and not in benchmark mode)
                #ifdef DEBUG
                if (avgUpdateTime.count() > 0 && !HammerEngine::Logger::IsBenchmarkMode()) {
                    GAMELOOP_DEBUG(std::format("Update performance: {}ms avg ({}% frame budget)",
                                             avgUpdateTime.count() / 1000.0f,
                                             (avgUpdateTime.count() / 1000.0f) / (1000.0f / targetFPS) * 100.0f));
                }
                #endif
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

void GameLoop::processRender() {
    if (m_timestepManager->shouldRender()) {
        invokeRenderHandler();
    }
}

void GameLoop::invokeEventHandler() {
    EventHandler handlerCopy;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        handlerCopy = m_eventHandler;
    }
    if (handlerCopy) {
        try {
            handlerCopy();
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in event handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeUpdateHandler(float deltaTime) {
    UpdateHandler handlerCopy;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        handlerCopy = m_updateHandler;
    }
    if (handlerCopy) {
        try {
            handlerCopy(deltaTime);
        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in update handler: " + std::string(e.what()));
        }
    }
}

void GameLoop::invokeRenderHandler() {
    RenderHandler handlerCopy;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        handlerCopy = m_renderHandler;
    }
    if (handlerCopy) {
        try {
            handlerCopy();
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
