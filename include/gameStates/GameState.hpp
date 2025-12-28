/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_HPP
#define GAME_STATE_HPP

#include <string>

// Forward declarations
struct SDL_Renderer;
class GameStateManager;

// pure virtual for inheritance
class GameState {
 public:
  virtual bool enter() = 0;
  virtual void update(float deltaTime) = 0;
  virtual void render(SDL_Renderer* renderer, float interpolationAlpha = 1.0f) = 0;
  virtual void handleInput() = 0;
  virtual bool exit() = 0;
  virtual void pause() {}
  virtual void resume() {}
  virtual std::string getName() const = 0;
  virtual ~GameState() = default;

  // State manager access - set by GameStateManager when state is registered
  void setStateManager(GameStateManager* manager) { mp_stateManager = manager; }

 protected:
  GameStateManager* mp_stateManager = nullptr;
};
#endif  // GAME_STATE_HPP
