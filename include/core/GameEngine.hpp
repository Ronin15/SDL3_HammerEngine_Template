/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP

#include "managers/GameStateManager.hpp"
#include "core/TimestepManager.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>

// Forward declarations
class AIManager;
class BackgroundSimulationManager;
class EventManager;
class InputManager;
class ParticleManager;
class PathfinderManager;
class ResourceTemplateManager;
class WorldResourceManager;
class WorldManager;
class CollisionManager;

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
   * @brief Processes non-critical background tasks using the thread system
   * @details Provides a designated entry point for asynchronous background work that
   *          runs on worker threads (not the main thread). Suitable for:
   *          - Asset pre-loading for upcoming game states
   *          - Background save game serialization
   *          - Analytics/telemetry data collection
   *          - Periodic cache cleanup or memory defragmentation
   *          - Network polling for non-latency-critical updates
   *
   * @note Global systems (EventManager, AIManager, etc.) are updated in the main
   *       update loop for deterministic ordering. This method is for truly
   *       asynchronous, non-critical tasks only.
   * @warning Any work added must be thread-safe and not require main-thread
   *          resources (SDL rendering, UI state, etc.).
   */
  void processBackgroundTasks();

  /**
   * @brief Gets pointer to the game state manager
   * @return Pointer to GameStateManager instance
   */
  GameStateManager *getGameStateManager() const {
    return mp_gameStateManager.get();
  }

  /**
   * @brief Gets the timestep manager for frame timing
   * @return Reference to the TimestepManager instance
   */
  TimestepManager& getTimestepManager() {
    return *m_timestepManager;
  }

  /**
   * @brief Gets the timestep manager for frame timing (const)
   * @return Const reference to the TimestepManager instance
   */
  const TimestepManager& getTimestepManager() const {
    return *m_timestepManager;
  }

  /**
   * @brief Checks if the engine is currently running
   * @return true if engine is running, false otherwise
   */
  bool isRunning() const { return m_running; }

  /**
   * @brief Stops the game engine
   */
  void stop() { m_running = false; }

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
   * @brief Gets current FPS from TimestepManager
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
  bool isUsingSoftwareFrameLimiting() const { return m_timestepManager->isUsingSoftwareFrameLimiting(); }

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

  /**
   * @brief Sets global pause state for all game managers
   * @param paused true to pause all managers, false to resume
   * @details Coordinates pause state across AIManager, ParticleManager,
   *          CollisionManager, and PathfinderManager. When paused, managers
   *          early-exit their update() methods, reducing CPU usage and
   *          allowing ThreadSystem to go idle.
   */
  void setGlobalPause(bool paused);

  /**
   * @brief Gets the current global pause state
   * @return true if game managers are globally paused
   */
  bool isGloballyPaused() const;

private:
  /**
   * @brief Verifies VSync state matches the requested setting
   * @param requested true if VSync should be enabled, false if disabled
   * @return true if VSync state verified to match requested, false otherwise
   * @details Sets TimestepManager's software frame limiting based on verification result.
   *          Used by both init() and setVSyncEnabled() to consolidate VSync logic.
   */
  bool verifyVSyncState(bool requested);

  /**
   * @brief Handles window resize events from SDL
   * @param event The SDL window resize event
   * @details Updates window dimensions, renderer logical presentation,
   *          reloads fonts, and notifies UIManager for repositioning.
   */
  void onWindowResize(const SDL_Event& event);

  /**
   * @brief Handles display change events from SDL
   * @param event The SDL display event (orientation, added, removed, moved, scale)
   * @details Normalizes UI scale, reloads fonts, and triggers UI repositioning.
   */
  void onDisplayChange(const SDL_Event& event);
  std::unique_ptr<GameStateManager> mp_gameStateManager{nullptr};
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{
      nullptr, SDL_DestroyWindow};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{
      nullptr, SDL_DestroyRenderer};
  std::unique_ptr<TimestepManager> m_timestepManager{nullptr};
  bool m_running{false};
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
  BackgroundSimulationManager *mp_backgroundSimManager{nullptr};
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
  bool m_isFullscreen{false};

  // Global pause state - propagated to managers which have their own atomics
  bool m_globallyPaused{false};

  // Delete copy constructor and assignment operator
  GameEngine(const GameEngine &) = delete;            // Prevent copying
  GameEngine &operator=(const GameEngine &) = delete; // Prevent assignment

  GameEngine() : m_windowWidth{1280}, m_windowHeight{720} {}
};
#endif // GAME_ENGINE_HPP
