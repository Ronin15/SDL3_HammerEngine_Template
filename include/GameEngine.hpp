#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP

#include "GameStateManager.hpp"
#include "TextureManager.hpp"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>


class GameEngine {
 public:
  GameEngine() : m_windowWidth(1280), m_windowHeight(720) {}
  // Default values that will be updated based on display bounds during init()
  ~GameEngine() {}

  static GameEngine* Instance() {
    if (sp_Instance == nullptr) {
      sp_Instance = new GameEngine();
    }
    return sp_Instance;
  }

  bool init(const char* title, int width, int height, bool fullscreen);

  void handleEvents();
  void update();
  void render();
  void clean();

  GameStateManager* getGameStateManager() const { return mp_gameStateManager; }
  TextureManager* getTextureManager() const { return mp_textureManager; }

  void setRunning(bool running) { m_isRunning = running; }
  bool getRunning() const { return m_isRunning; }
  SDL_Renderer* getRenderer() const { return p_renderer; }

  // Window size methods
  int getWindowWidth() const { return m_windowWidth; }
  int getWindowHeight() const { return m_windowHeight; }
  void setWindowSize(int width, int height) { m_windowWidth = width; m_windowHeight = height; }

 private:
  GameStateManager* mp_gameStateManager{nullptr};
  TextureManager* mp_textureManager{nullptr};
  SDL_Window* p_window{nullptr};
  SDL_Renderer* p_renderer{nullptr};
  static GameEngine* sp_Instance;
  bool m_isRunning{false};
  int m_windowWidth;
  int m_windowHeight;
};
#endif  // GAME_ENGINE_HPP
