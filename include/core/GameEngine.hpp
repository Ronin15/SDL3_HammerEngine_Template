/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP

#include "managers/GameStateManager.hpp"
#include <SDL3_image/SDL_image.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string_view>

// Forward declarations
class GameLoop;
class AIManager;
class EventManager;
class InputManager;
class ParticleManager;
class PathfinderManager;
class ResourceTemplateManager;
class WorldResourceManager;
class WorldManager;
class CollisionManager;

#ifdef DEBUG
/**
 * @brief Buffer telemetry statistics for monitoring double/triple buffering performance
 * @details Tracks swap success/failures, render stalls, and timing metrics using rolling averages
 *
 * Only compiled in DEBUG builds for zero-overhead performance monitoring.
 * Follows EventManager's PerformanceStats pattern for consistency.
 */
struct BufferTelemetryStats {
    // Swap tracking
    uint64_t swapAttempts{0};        // Total buffer swap attempts
    uint64_t swapSuccesses{0};       // Successful swaps (buffer available)
    uint64_t swapBlocked{0};         // Swaps blocked (buffer not ready or rendering conflict)
    uint64_t casFailures{0};         // Compare-and-swap failures (atomic contention)

    // Render tracking
    uint64_t renderStalls{0};        // Render stalled (no buffer ready)
    uint64_t framesSkipped{0};       // Frames where lastUpdate == lastRendered

    // Timing (rolling averages over 60 frames)
    static constexpr size_t SAMPLE_SIZE = 60;
    std::array<double, SAMPLE_SIZE> mutexWaitTimes{};    // Time waiting for update mutex (ms)
    std::array<double, SAMPLE_SIZE> bufferReadyDelays{};  // Time from update complete to buffer ready (ms)
    size_t currentSample{0};

    double avgMutexWaitMs{0.0};
    double avgBufferReadyMs{0.0};

    /**
     * @brief Adds a timing sample to the rolling average
     * @param mutexWait Time spent waiting for update mutex (ms)
     * @param bufferDelay Time from update complete to buffer marked ready (ms)
     */
    void addTimingSample(double mutexWait, double bufferDelay) {
        mutexWaitTimes[currentSample] = mutexWait;
        bufferReadyDelays[currentSample] = bufferDelay;
        currentSample = (currentSample + 1) % SAMPLE_SIZE;

        // Calculate rolling averages
        double mutexSum = 0.0;
        double bufferSum = 0.0;
        for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
            mutexSum += mutexWaitTimes[i];
            bufferSum += bufferReadyDelays[i];
        }
        avgMutexWaitMs = mutexSum / SAMPLE_SIZE;
        avgBufferReadyMs = bufferSum / SAMPLE_SIZE;
    }

    /**
     * @brief Resets all telemetry counters and timing samples
     */
    void reset() {
        swapAttempts = 0;
        swapSuccesses = 0;
        swapBlocked = 0;
        casFailures = 0;
        renderStalls = 0;
        framesSkipped = 0;
        mutexWaitTimes.fill(0.0);
        bufferReadyDelays.fill(0.0);
        currentSample = 0;
        avgMutexWaitMs = 0.0;
        avgBufferReadyMs = 0.0;
    }

    /**
     * @brief Calculates swap success rate as percentage
     * @return Swap success rate (0.0 to 100.0)
     */
    double getSwapSuccessRate() const {
        if (swapAttempts == 0) return 100.0;
        return (static_cast<double>(swapSuccesses) / swapAttempts) * 100.0;
    }
};
#endif // DEBUG

class GameEngine {
public:
  ~GameEngine() = default;

  /**
   * @brief Gets the singleton instance of GameEngine
   * @return Reference to the GameEngine singleton instance
   */
  static GameEngine &Instance() {
    static GameEngine instance;
    return instance;
  }

  /**
   * @brief Initializes the game engine, SDL subsystems, and all managers
   * @param title Window title for the game
   * @param width Initial window width (0 for auto-sizing)
   * @param height Initial window height (0 for auto-sizing)
   * @param fullscreen Whether to start in fullscreen mode
   * @return true if initialization successful, false otherwise
   *
   * @details Manager Initialization Dependency Graph:
   *
   * Phase 1 (No Dependencies - Core Infrastructure):
   *   - Logger (CRITICAL: Must be first for diagnostic output)
   *   - ThreadSystem (Required by all async operations)
   *   - InputManager (No dependencies)
   *   - TextureManager (No dependencies)
   *   - ResourceManager (No dependencies)
   *
   * Phase 2 (Requires Core Infrastructure):
   *   - EventManager (CRITICAL: Must precede all event subscribers)
   *   - SettingsManager (Independent of other game managers)
   *   - SaveManager (Independent of other game managers)
   *
   * Phase 3 (Requires EventManager for event subscriptions):
   *   - PathfinderManager (Subscribes to WorldLoaded and CollisionObstacleChanged events)
   *   - CollisionManager (Subscribes to WorldLoaded events)
   *   - UIManager (May subscribe to game events)
   *   - AudioManager (Independent but needs ThreadSystem)
   *
   * Phase 4 (Requires Pathfinder + Collision):
   *   - AIManager (CRITICAL: Depends on PathfinderManager and CollisionManager)
   *   - ParticleManager (Independent but initialized after core systems)
   *   - WorldManager (Needs CollisionManager for static geometry)
   *
   * Phase 5 (Post-Initialization Event Setup):
   *   - WorldManager::setupEventHandlers() (Requires EventManager fully initialized)
   *
   * Note: Initialization uses ThreadSystem futures to parallelize where possible
   * while respecting the dependency constraints above.
   */
  bool init(const std::string_view title, const int width, const int height,
            bool fullscreen);

  /**
   * @brief Handles SDL events and input processing
   */
  void handleEvents();

  /**
   * @brief Updates game logic with fixed timestep
   * @param deltaTime Time elapsed since last update in seconds
   */
  void update(float deltaTime);

  /**
   * @brief Main rendering function called from game loop
   */
  void render();

  /**
   * @brief Cleans up all engine resources and shuts down systems
   */
  void clean();

  /**
   * @brief Processes background tasks using the thread system
   */
  void processBackgroundTasks();

  /**
   * @brief Loads resources asynchronously in background threads
   * @param path Path to resources to load
   * @return true if loading started successfully, false otherwise
   */
  bool loadResourcesAsync(const std::string &path);

  /**
   * @brief Checks if there's a new frame ready to render
   * @return true if new frame available, false otherwise
   */
  bool hasNewFrameToRender() const noexcept;

  /**
   * @brief Checks if update is currently running
   * @return true if update in progress, false otherwise
   */
  bool isUpdateRunning() const noexcept;

  /**
   * @brief Gets the current buffer index being used for updates
   * @return Current buffer index (0 or 1)
   */
  size_t getCurrentBufferIndex() const noexcept;

  /**
   * @brief Gets the buffer index being used for rendering
   * @return Render buffer index (0 or 1)
   */
  size_t getRenderBufferIndex() const noexcept;

  /**
   * @brief Swaps double buffers for thread-safe rendering
   */
  void swapBuffers();

  /**
   * @brief Gets pointer to the game state manager
   * @return Pointer to GameStateManager instance
   */
  GameStateManager *getGameStateManager() const {
    return mp_gameStateManager.get();
  }

  /**
   * @brief Sets the game loop reference for delegation
   * @param gameLoop Shared pointer to the GameLoop instance
   */
  void setGameLoop(std::shared_ptr<GameLoop> gameLoop) {
    m_gameLoop = gameLoop;
  }

  /**
   * @brief Sets the running state of the engine
   * @param running New running state
   */
  void setRunning(bool running);

  /**
   * @brief Gets the current running state of the engine
   * @return true if engine is running, false otherwise
   */
  bool getRunning() const;

  /**
   * @brief Gets the GameLoop instance
   * @return Shared pointer to GameLoop, null if not set
   */
  std::shared_ptr<GameLoop> getGameLoop() const {
    return m_gameLoop.lock();
  }

  /**
   * @brief Gets the SDL renderer instance
   * @return Pointer to SDL renderer
   */
  SDL_Renderer *getRenderer() const noexcept { return mp_renderer.get(); }

  /**
   * @brief Gets the SDL window instance
   * @return Pointer to SDL window
   */
  SDL_Window *getWindow() const noexcept { return mp_window.get(); }

  /**
   * @brief Gets current FPS from GameLoop's TimestepManager
   * @return Current frames per second
   */
  float getCurrentFPS() const;

  /**
   * @brief Gets the current window width
   * @return Window width in pixels
   */
  int getWindowWidth() const noexcept { return m_windowWidth; }

  /**
   * @brief Gets the current window height
   * @return Window height in pixels
   */
  int getWindowHeight() const noexcept { return m_windowHeight; }

  /**
   * @brief Gets the logical rendering width used for UI positioning
   * @return Logical rendering width in pixels
   */
  int getLogicalWidth() const noexcept { return m_logicalWidth; }

  /**
   * @brief Gets the logical rendering height used for UI positioning
   * @return Logical rendering height in pixels
   */
  int getLogicalHeight() const noexcept { return m_logicalHeight; }

  /**
   * @brief Sets the window size
   * @param width New window width in pixels
   * @param height New window height in pixels
   */
  void setWindowSize(int width, int height) {
    m_windowWidth = width;
    m_windowHeight = height;

    // Track windowed size for restoration when exiting fullscreen
    // Only update when NOT in fullscreen mode (windowed resizes only)
    if (!m_isFullscreen) {
      m_windowedWidth = width;
      m_windowedHeight = height;
    }
  }

  /**
   * @brief Sets the logical rendering size
   * @param width New logical width in pixels
   * @param height New logical height in pixels
   */
  void setLogicalSize(int width, int height) {
    m_logicalWidth = width;
    m_logicalHeight = height;
  }

  /**
   * @brief Sets the logical presentation mode for rendering
   * @param mode SDL logical presentation mode to use
   */
  void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode);

  /**
   * @brief Gets the current logical presentation mode
   * @return Current logical presentation mode
   */
  SDL_RendererLogicalPresentation getLogicalPresentationMode() const noexcept;

  /**
   * @brief Gets the DPI scale factor calculated during initialization
   * @return DPI scale factor (1.0 for standard DPI, higher for high-DPI
   * displays)
   */
  float getDPIScale() const { return m_dpiScale; }

  /**
   * @brief Updates the DPI scale factor when window is resized
   * @param newScale New DPI scale factor to set
   */
  void setDPIScale(float newScale) { m_dpiScale = newScale; }

  /**
   * @brief Gets the optimal display index for the current platform
   * @return Display index (0 for macOS built-in screens, 1 for other platforms)
   */
  int getOptimalDisplayIndex() const;

  /**
   * @brief Checks if VSync is currently enabled
   * @return true if VSync is enabled, false otherwise
   */
  bool isVSyncEnabled() const noexcept;

  /**
   * @brief Toggles VSync on or off at runtime
   * @param enable true to enable VSync, false to disable
   * @return true if VSync setting was changed successfully, false otherwise
   */
  bool setVSyncEnabled(bool enable);

  /**
   * @brief Checks if the engine is running on a Wayland session.
   * @return true if Wayland is detected, false otherwise.
   */
  bool isWayland() const { return m_isWayland; }

  /**
   * @brief Checks if the engine is using software frame limiting.
   * @return true if using software frame limiting, false if using hardware VSync.
   */
  bool isUsingSoftwareFrameLimiting() const { return m_usingSoftwareFrameLimiting; }

  /**
   * @brief Toggles fullscreen mode at runtime
   */
  void toggleFullscreen();

  /**
   * @brief Sets fullscreen mode to a specific state
   * @param enabled true to enable fullscreen, false to disable
   */
  void setFullscreen(bool enabled);

  /**
   * @brief Checks if the engine is currently in fullscreen mode
   * @return true if fullscreen is enabled, false otherwise
   */
  bool isFullscreen() const noexcept { return m_isFullscreen; }

private:
  std::unique_ptr<GameStateManager> mp_gameStateManager{nullptr};
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{
      nullptr, SDL_DestroyWindow};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{
      nullptr, SDL_DestroyRenderer};
  std::weak_ptr<GameLoop> m_gameLoop{}; // Non-owning weak reference to GameLoop
  int m_windowWidth{0};
  int m_windowHeight{0};
  int m_windowedWidth{1920};  // Windowed mode width (for restoring from fullscreen)
  int m_windowedHeight{1080}; // Windowed mode height (for restoring from fullscreen)
  int m_logicalWidth{1920};  // Logical rendering width for UI positioning
  int m_logicalHeight{1080}; // Logical rendering height for UI positioning

  // Cached manager references for zero-overhead performance
  // Step 2: Re-implementing manager caching with proper initialization order
  // InputManager not cached - handled in handleEvents() for proper SDL event
  // polling architecture
  AIManager *mp_aiManager{nullptr};
  EventManager *mp_eventManager{nullptr};
  ParticleManager *mp_particleManager{nullptr};
  PathfinderManager *mp_pathfinderManager{nullptr}; // Initialized by AIManager, cached by GameEngine
  ResourceTemplateManager *mp_resourceTemplateManager{nullptr};
  WorldResourceManager *mp_worldResourceManager{nullptr};
  WorldManager *mp_worldManager{nullptr};
  CollisionManager *mp_collisionManager{nullptr};

  // Logical presentation settings
  SDL_RendererLogicalPresentation m_logicalPresentationMode{
      SDL_LOGICAL_PRESENTATION_LETTERBOX};

  // DPI scaling
  float m_dpiScale{1.0f};

  // Platform-specific flags
  bool m_isWayland{false};
  bool m_usingSoftwareFrameLimiting{false};
  bool m_isFullscreen{false};

  // Multithreading synchronization
  std::mutex m_updateMutex{};
  std::condition_variable m_updateCondition{};

  // Using memory_order for thread synchronization
  std::atomic<bool> m_updateCompleted{false};
  std::atomic<bool> m_updateRunning{false};
  std::atomic<bool> m_stopRequested{false};
  std::atomic<uint64_t> m_lastUpdateFrame{0};
  std::atomic<uint64_t> m_lastRenderedFrame{0};

  // Double/Triple buffering (runtime configurable)
  static constexpr size_t MAX_BUFFER_COUNT = 3;  // Support up to 3 buffers
  size_t m_bufferCount{2};                        // Runtime buffer count (2 or 3)
  std::atomic<size_t> m_currentBufferIndex{0};
  std::atomic<size_t> m_renderBufferIndex{0};
  std::atomic<bool> m_bufferReady[MAX_BUFFER_COUNT]{false, false, false};

  // Buffer synchronization (lock-free atomic operations)
  std::condition_variable m_bufferCondition{};

  // Protection for high entity counts
  std::atomic<size_t> m_entityProcessingCount{0};

  // Render synchronization
  std::mutex m_renderMutex{};

#ifdef DEBUG
  // Buffer telemetry (debug-only, F3 to toggle overlay)
  mutable BufferTelemetryStats m_bufferTelemetry{};
  std::atomic<bool> m_showBufferTelemetry{false};
  mutable uint64_t m_telemetryLogFrame{0};
  static constexpr uint64_t TELEMETRY_LOG_INTERVAL = 300;  // Log every 300 frames (~5s @ 60fps)
#endif // DEBUG

  // Delete copy constructor and assignment operator
  GameEngine(const GameEngine &) = delete;            // Prevent copying
  GameEngine &operator=(const GameEngine &) = delete; // Prevent assignment

  GameEngine() : m_windowWidth{1280}, m_windowHeight{720} {}
};
#endif // GAME_ENGINE_HPP
