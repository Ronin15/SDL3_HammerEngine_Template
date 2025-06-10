/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include <memory>
#include <vector>
#include "gameStates/GameState.hpp"

class GameStateManager {

 public:
  GameStateManager();
  void addState(std::unique_ptr<GameState> state);
  void setState(const std::string& stateName);
  void update(float deltaTime);
  void render();
  bool hasState(const std::string& stateName) const;
  std::shared_ptr<GameState> getState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void clearAllStates();

  private:
    std::vector<std::shared_ptr<GameState>> states{}; // STL vector for game states
    // Non-owning observer to the current active state
    // This state is owned by the 'states' container above
    std::weak_ptr<GameState> currentState;
    float m_lastDeltaTime{0.0f}; // Store deltaTime from update to pass to render

};

#endif  // GAME_STATE_MANAGER_HPP
