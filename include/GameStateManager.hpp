// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include <memory>
#include <vector>

class GameState;

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
   std::vector<std::unique_ptr<GameState>> states;
   GameState* currentState;  // Pointer to the current state, not owned

};

#endif  // GAME_STATE_MANAGER_HPP
