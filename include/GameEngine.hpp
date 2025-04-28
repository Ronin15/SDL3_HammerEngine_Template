// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP

#include "GameStateManager.hpp"
#include "TextureManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <atomic>
#include <condition_variable>
#include <mutex>

class GameEngine {
 public:
  GameEngine() : m_windowWidth(1280), m_windowHeight(720) {}
  // Default values that will be updated based on display bounds during init() fail safe.
  ~GameEngine() {}

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

  GameStateManager* getGameStateManager() const { return mp_gameStateManager; }
  TextureManager* getTextureManager() const { return mp_textureManager; }

  void setRunning(bool running) { m_isRunning = running; }
  bool getRunning() const { return m_isRunning; }
  SDL_Renderer* getRenderer() const { return mp_renderer; }

  // Window size methods
  int getWindowWidth() const { return m_windowWidth; }
  int getWindowHeight() const { return m_windowHeight; }
  void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }

 private:
  GameStateManager* mp_gameStateManager{nullptr};
  TextureManager* mp_textureManager{nullptr};
  SDL_Window* mp_window{nullptr};
  SDL_Renderer* mp_renderer{nullptr};
  std::atomic<bool> m_isRunning{false};
  int m_windowWidth;
  int m_windowHeight;

  // Multithreading synchronization
  std::mutex m_updateMutex;
  std::condition_variable m_updateCondition;
  std::atomic<bool> m_updateCompleted{false};

  // Render synchronization
  std::mutex m_renderMutex;
};
#endif  // GAME_ENGINE_HPP
