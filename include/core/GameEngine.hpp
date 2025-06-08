/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP

#include "managers/GameStateManager.hpp"
#include <SDL3_image/SDL_image.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

// Forward declarations
class GameLoop;
class AIManager;
class EventManager;
class InputManager;

class GameEngine {
 public:

  ~GameEngine() = default;

  /**
   * @brief Gets the singleton instance of GameEngine
   * @return Reference to the GameEngine singleton instance
   */
  static GameEngine& Instance(){
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
   */
  bool init(const char* title, int width, int height, bool fullscreen);

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
   * @brief Renders the current frame with interpolation
   * @param interpolation Interpolation factor for smooth rendering
   */
  void render(float interpolation);
  
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
  bool loadResourcesAsync(const std::string& path);

  /**
   * @brief Waits for update thread to complete current frame
   */
  void waitForUpdate();
  
  /**
   * @brief Signals that update processing is complete
   */
  void signalUpdateComplete();
  
  /**
   * @brief Checks if a new frame is ready for rendering
   * @return true if new frame ready, false otherwise
   */
  bool hasNewFrameToRender() const;
  
  /**
   * @brief Checks if update thread is currently running
   * @return true if update is running, false otherwise
   */
  bool isUpdateRunning() const;
  
  /**
   * @brief Swaps double buffers for thread-safe rendering
   */
  void swapBuffers();
  
  /**
   * @brief Gets the current buffer index for double buffering
   * @return Current buffer index
   */
  size_t getCurrentBufferIndex() const;
  
  /**
   * @brief Gets the render buffer index for double buffering
   * @return Render buffer index
   */
  size_t getRenderBufferIndex() const;

  /**
   * @brief Gets pointer to the game state manager
   * @return Pointer to GameStateManager instance
   */
  GameStateManager* getGameStateManager() const { return mp_gameStateManager.get(); }

  /**
   * @brief Sets the game loop reference for delegation
   * @param gameLoop Shared pointer to the GameLoop instance
   */
  void setGameLoop(std::shared_ptr<GameLoop> gameLoop) { m_gameLoop = gameLoop; }
  
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
  SDL_Renderer* getRenderer() const { return mp_renderer.get(); }

  /**
   * @brief Gets current FPS from GameLoop's TimestepManager
   * @return Current frames per second
   */
  float getCurrentFPS() const;

  /**
   * @brief Gets the current window width
   * @return Window width in pixels
   */
  int getWindowWidth() const { return m_windowWidth; }
  
  /**
   * @brief Gets the current window height
   * @return Window height in pixels
   */
  int getWindowHeight() const { return m_windowHeight; }
  
  /**
   * @brief Sets the window size
   * @param width New window width in pixels
   * @param height New window height in pixels
   */
  void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }

  /**
   * @brief Sets the logical presentation mode for rendering
   * @param mode SDL logical presentation mode to use
   */
  void setLogicalPresentationMode(SDL_RendererLogicalPresentation mode);
  
  /**
   * @brief Gets the current logical presentation mode
   * @return Current SDL logical presentation mode
   */
  SDL_RendererLogicalPresentation getLogicalPresentationMode() const;

 private:
  std::unique_ptr<GameStateManager> mp_gameStateManager{nullptr};
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{nullptr, SDL_DestroyWindow};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{nullptr, SDL_DestroyRenderer};
  std::weak_ptr<GameLoop> m_gameLoop{};  // Non-owning weak reference to GameLoop
  int m_windowWidth{0};
  int m_windowHeight{0};
  
  // Cached manager references for zero-overhead performance
  // Step 2: Re-implementing manager caching with proper initialization order
  // InputManager not cached - handled in handleEvents() for proper SDL event polling architecture
  AIManager* mp_aiManager{nullptr};
  EventManager* mp_eventManager{nullptr};
  
  // Logical presentation settings
  SDL_RendererLogicalPresentation m_logicalPresentationMode{SDL_LOGICAL_PRESENTATION_LETTERBOX};

  // Multithreading synchronization
  std::mutex m_updateMutex{};
  std::condition_variable m_updateCondition{};
  // Using memory_order for thread synchronization
  std::atomic<bool> m_updateCompleted{false};
  std::atomic<bool> m_updateRunning{false};
  std::atomic<uint64_t> m_lastUpdateFrame{0};
  std::atomic<uint64_t> m_lastRenderedFrame{0};
  
  // Double buffering (ping-pong buffers)
  static constexpr size_t BUFFER_COUNT = 2;
  std::atomic<size_t> m_currentBufferIndex{0};
  std::atomic<size_t> m_renderBufferIndex{0};
  std::atomic<bool> m_bufferReady[BUFFER_COUNT]{false, false};
  
  // Synchronization barriers
  std::mutex m_bufferMutex{};
  std::condition_variable m_bufferCondition{};
  
  // Protection for high entity counts
  std::atomic<size_t> m_entityProcessingCount{0};

  // Render synchronization
  std::mutex m_renderMutex{};



  // Delete copy constructor and assignment operator
  GameEngine(const GameEngine&) = delete; // Prevent copying
  GameEngine& operator=(const GameEngine&) = delete; // Prevent assignment

  GameEngine() : m_windowWidth{1280}, m_windowHeight{720} {}
};
#endif  // GAME_ENGINE_HPP
