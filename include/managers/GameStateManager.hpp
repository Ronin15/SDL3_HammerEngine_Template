/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#ifndef GAME_STATE_MANAGER_HPP
#define GAME_STATE_MANAGER_HPP

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "gameStates/GameState.hpp"

class GameStateManager {

 public:
  GameStateManager();
  void addState(std::unique_ptr<GameState> state);
  void pushState(const std::string& stateName);
  void popState();
  void changeState(const std::string& stateName); // Pops the current state and pushes a new one
  // Use requestStateChange() for all runtime transitions (from update/input/UI callbacks).
  // Deferred processing at end of update avoids destroying the active state mid-cycle.
  void requestStateChange(const std::string& stateName); // Request deferred state change

  void update(float deltaTime);
  void render();
  void handleInput();
  void notifyResize(int newLogicalWidth, int newLogicalHeight);

  bool hasState(const std::string& stateName) const;
  std::shared_ptr<GameState> getState(const std::string& stateName) const;
  void removeState(const std::string& stateName);
  void clearAllStates();

 private:
  // All registered states, available for activation
  std::unordered_map<std::string, std::shared_ptr<GameState>> m_registeredStates;
  // The stack of active states
  std::vector<std::shared_ptr<GameState>> m_activeStates;

  float m_lastDeltaTime{0.0f}; // Store deltaTime from update to pass to render
  
  // Deferred state change mechanism
  std::string m_pendingStateChange;
  bool m_hasPendingStateChange{false};
};

#endif  // GAME_STATE_MANAGER_HPP
