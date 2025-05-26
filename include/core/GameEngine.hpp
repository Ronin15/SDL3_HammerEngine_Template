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
  void update();
  void render();
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

  // GameStateManager getter to let game engine access
  GameStateManager* getGameStateManager() const { return mp_gameStateManager.get(); }

  //TODO Remove TextureManager pointer when its initialization is fixed
  void setRunning(bool running) { m_isRunning = running; }
  bool getRunning() const { return m_isRunning; }
  SDL_Renderer* getRenderer() const { return mp_renderer.get(); }

  // Window size methods
  int getWindowWidth() const { return m_windowWidth; }
  int getWindowHeight() const { return m_windowHeight; }
  void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }

 private:
  std::unique_ptr<GameStateManager> mp_gameStateManager{nullptr};
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> mp_window{nullptr, SDL_DestroyWindow};
  std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> mp_renderer{nullptr, SDL_DestroyRenderer};
  std::atomic<bool> m_isRunning{false};
  int m_windowWidth{0};
  int m_windowHeight{0};

  // Multithreading synchronization
  std::mutex m_updateMutex{};
  std::condition_variable m_updateCondition{};
  std::atomic<bool> m_updateCompleted{false};
  std::atomic<bool> m_updateRunning{false};
  std::atomic<uint64_t> m_lastUpdateFrame{0};
  std::atomic<uint64_t> m_lastRenderedFrame{0};

  // Render synchronization
  std::mutex m_renderMutex{};

  // Delete copy constructor and assignment operator
  GameEngine(const GameEngine&) = delete; // Prevent copying
  GameEngine& operator=(const GameEngine&) = delete; // Prevent assignment

  GameEngine() : m_windowWidth{1280}, m_windowHeight{720} {}
};
#endif  // GAME_ENGINE_HPP
