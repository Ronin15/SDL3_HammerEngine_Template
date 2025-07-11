/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_LOOP_HPP
#define GAME_LOOP_HPP

#include "core/TimestepManager.hpp"
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <future>

// Forward declaration
namespace HammerEngine{
    struct WorkerBudget;
}

/**
 * GameLoop manages the main game loop with industry-standard timing patterns.
 *
 * Uses callback-based architecture for clean separation of concerns:
 * - Event handling runs on main thread
 * - Updates run with fixed timestep (can be threaded)
 * - Rendering runs with variable timestep and interpolation
 *
 * Based on patterns used by professional game engines.
 */
class GameLoop {
public:
    // Callback function types
    using EventHandler = std::function<void()>;
    using UpdateHandler = std::function<void(float deltaTime)>;
    using RenderHandler = std::function<void()>;

    /**
     * Constructor
     * @param targetFPS Target frames per second for rendering (e.g., 60.0f)
     * @param fixedTimestep Fixed timestep for updates in seconds (e.g., 1.0f/60.0f)
     * @param threaded Whether to run updates on separate thread
     */
    explicit GameLoop(float targetFPS = 60.0f, float fixedTimestep = 1.0f/60.0f, bool threaded = true);

    /**
     * Destructor - ensures proper cleanup
     */
    ~GameLoop();

    /**
     * Set the event handling callback
     * Events are always processed on the main thread (SDL requirement)
     * @param handler Function to call for event processing
     */
    void setEventHandler(EventHandler handler);

    /**
     * Set the update callback
     * Updates run with fixed timestep for consistent game logic
     * @param handler Function to call for game logic updates
     */
    void setUpdateHandler(UpdateHandler handler);

    /**
     * Set the render callback
     * Rendering runs with variable timestep and interpolation
     * @param handler Function to call for rendering
     */
    void setRenderHandler(RenderHandler handler);

    /**
     * Start the main game loop
     * Blocks until stop() is called or an error occurs
     * @return true if loop completed successfully, false on error
     */
    bool run();

    /**
     * Stop the game loop
     * Thread-safe, can be called from any thread
     */
    void stop();

    /**
     * Check if the game loop is currently running
     * @return true if running
     */
    bool isRunning() const;

    /**
     * Pause the game loop (stops updates but continues rendering)
     * @param paused true to pause, false to resume
     */
    void setPaused(bool paused);

    /**
     * Check if the game loop is paused
     * @return true if paused
     */
    bool isPaused() const;

    /**
     * Get current FPS from the timestep manager
     * @return current frames per second
     */
    float getCurrentFPS() const;

    /**
     * Get current frame time in milliseconds
     * @return frame time in ms
     */
    uint32_t getFrameTimeMs() const;

    /**
     * Set new target FPS
     * @param fps new target frames per second
     */
    void setTargetFPS(float fps);

    /**
     * Set new fixed timestep for updates
     * @param timestep new fixed timestep in seconds
     */
    void setFixedTimestep(float timestep);

    /**
     * Get the timestep manager (for advanced configuration)
     * @return reference to internal timestep manager
     */
    TimestepManager& getTimestepManager();

private:
    // Timing management
    std::unique_ptr<TimestepManager> m_timestepManager;

    // Callback handlers
    EventHandler m_eventHandler;
    UpdateHandler m_updateHandler;
    RenderHandler m_renderHandler;

    // Loop state
    std::atomic<bool> m_running;
    std::atomic<bool> m_paused;
    std::atomic<bool> m_stopRequested;

    // Threading
    bool m_threaded;
    std::atomic<bool> m_updateTaskRunning;
    std::future<void> m_updateTaskFuture;

    // Update synchronization for threaded mode
    std::atomic<int> m_updateCount;
    std::mutex m_callbackMutex;

    // Internal methods
    void runMainThread();
    void runUpdateWorker(const HammerEngine::WorkerBudget& budget);
    void processEvents();
    void processUpdates();
    void processUpdatesParallel();
    void processRender();
    void cleanup();

    // Thread-safe callback invocation
    void invokeEventHandler();
    void invokeUpdateHandler(float deltaTime);
    void invokeRenderHandler();

    // Prevent copying
    GameLoop(const GameLoop&) = delete;
    GameLoop& operator=(const GameLoop&) = delete;
};

#endif // GAME_LOOP_HPP
