/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "core/GameLoop.hpp"
#include "core/Logger.hpp"
#include <exception>

GameLoop::GameLoop(float targetFPS, float fixedTimestep, bool threaded)
    : m_timestepManager(std::make_unique<TimestepManager>(targetFPS, fixedTimestep))
    , m_running(false)
    , m_paused(false)
    , m_stopRequested(false)
    , m_threaded(threaded)
    , m_updateThread(nullptr)
    , m_updateThreadRunning(false)
    , m_pendingUpdates(false)
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

    m_running.store(true);
    m_stopRequested.store(false);
    m_paused.store(false);

    // Reset timestep manager for clean start
    m_timestepManager->reset();

    try {
        if (m_threaded) {
            // Start update thread
            m_updateThreadRunning.store(true);
            m_updateThread = std::make_unique<std::thread>(&GameLoop::runUpdateThread, this);
        }

        // Run main thread loop
        runMainThread();

        // Clean shutdown
        if (m_threaded && m_updateThread) {
            m_updateThreadRunning.store(false);
            if (m_updateThread->joinable()) {
                m_updateThread->join();
            }
        }

        m_running.store(false);
        return true;

    } catch (const std::exception& e) {
        GAMELOOP_CRITICAL("Exception in main loop: " + std::string(e.what()));
        m_running.store(false);
        cleanup();
        return false;
    }
}

void GameLoop::stop() {
    m_stopRequested.store(true);
}

bool GameLoop::isRunning() const {
    return m_running.load();
}

void GameLoop::setPaused(bool paused) {
    bool wasPaused = m_paused.exchange(paused);

    // If transitioning from paused to unpaused, reset timing to avoid time jump
    if (wasPaused && !paused) {
        m_timestepManager->reset();
    }
}

bool GameLoop::isPaused() const {
    return m_paused.load();
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
    while (m_running.load() && !m_stopRequested.load()) {
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

void GameLoop::runUpdateThread() {
    while (m_updateThreadRunning.load() && !m_stopRequested.load()) {
        try {
            if (!m_paused.load()) {
                processUpdates();
            }

            // Calculate smart sleep time based on target FPS for reliable behavior
            // Sleep for approximately half the target frame time to balance responsiveness and CPU usage
            float targetFPS = m_timestepManager->getTargetFPS();
            float targetFrameTimeMs = 1000.0f / targetFPS;

            // Ensure minimum 1ms sleep, maximum 8ms for responsiveness
            uint32_t sleepTimeMs = static_cast<uint32_t>(std::max(1.0f, std::min(8.0f, targetFrameTimeMs * 0.5f)));
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));

        } catch (const std::exception& e) {
            GAMELOOP_ERROR("Exception in update thread: " + std::string(e.what()));
            // Continue running, but log the error
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
        m_updateCount.fetch_add(1);
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
    // Stop update thread if running
    if (m_updateThread) {
        m_updateThreadRunning.store(false);
        if (m_updateThread->joinable()) {
            m_updateThread->join();
        }
        m_updateThread.reset();
    }

    // Clear callbacks
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_eventHandler = nullptr;
        m_updateHandler = nullptr;
        m_renderHandler = nullptr;
    }

    m_running.store(false);
}
