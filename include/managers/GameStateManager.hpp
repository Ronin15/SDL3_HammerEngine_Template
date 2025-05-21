/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include <memory>
#include <boost/container/small_vector.hpp>
#include "gameStates/GameState.hpp"

class GameStateManager {

 public:
  GameStateManager();
  void addState(std::unique_ptr<GameState> state);
  void setState(const std::string& stateName);
  void update();
  void render();
  bool hasState(const std::string& stateName) const;
  GameState* getState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void clearAllStates();

  private:
   boost::container::small_vector<std::unique_ptr<GameState>, 8> states{}; //increase to how many states you expect to have.
   GameState* currentState{nullptr};  // Pointer to the current state, not owned

};

#endif  // GAME_STATE_MANAGER_HPP
