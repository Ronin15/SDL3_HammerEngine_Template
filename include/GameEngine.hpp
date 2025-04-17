#ifndef GAME_ENGINE_HPP
#define GAME_ENGINE_HPP
#include <SDL3/SDL.h>
#include "GameStateManager.hpp"

class GameEngine {
 public:
  GameEngine() {}
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

  GameStateManager* getGameStateManager() { return mp_gameStateManager; }

  void setRunning(bool running) { m_isRunning = running; }
  bool getRunning() { return m_isRunning; }
  SDL_Renderer* getRenderer() { return p_renderer; }

 private:
  GameStateManager* mp_gameStateManager{nullptr};
  SDL_Window* p_window{nullptr};
  SDL_Renderer* p_renderer{nullptr};
  static GameEngine* sp_Instance;
  bool m_isRunning{false};
};
#endif  // GAME_ENGINE_HPP
