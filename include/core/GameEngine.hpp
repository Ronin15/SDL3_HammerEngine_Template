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

// Forward declaration
class GameLoop;

class GameEngine {
 public:

  // Default values that will be updated based on display bounds during init() fail safe.
  ~GameEngine() = default;

  static GameEngine& Instance(){
      static GameEngine instance;
      return instance;
  }

  bool init(const char* title, int width, int height, bool fullscreen);

  void handleEvents();
  void update(float deltaTime);
  void render(float interpolation);
  void clean();

  // Multi-threaded task processing
  void processBackgroundTasks();

  // Thread-safe resource loading
  bool loadResourcesAsync(const std::string& path);

  // Thread synchronization methods
  void waitForUpdate();
  void signalUpdateComplete();
  
  // Frame management methods
  bool hasNewFrameToRender() const;
  bool isUpdateRunning() const;
  
  // Double buffering
  void swapBuffers();
  size_t getCurrentBufferIndex() const;
  size_t getRenderBufferIndex() const;

  // GameStateManager getter to let game engine access
  GameStateManager* getGameStateManager() const { return mp_gameStateManager.get(); }

  //TODO Remove TextureManager pointer when its initialization is fixed
  void setGameLoop(std::shared_ptr<GameLoop> gameLoop) { m_gameLoop = gameLoop; }
  void setRunning(bool running);
  bool getRunning() const;
  SDL_Renderer* getRenderer() const { return mp_renderer.get(); }

  // Get current FPS from GameLoop's TimestepManager
  float getCurrentFPS() const;



  // Window size methods
  int getWindowWidth() const { return m_windowWidth; }
  int getWindowHeight() const { return m_windowHeight; }
  void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }

 private:
  std::unique_ptr<GameStateManager> mp_gameStateManager{nullptr};
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{nullptr, SDL_DestroyWindow};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{nullptr, SDL_DestroyRenderer};
  std::weak_ptr<GameLoop> m_gameLoop{};  // Non-owning weak reference to GameLoop
  int m_windowWidth{0};
  int m_windowHeight{0};

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
