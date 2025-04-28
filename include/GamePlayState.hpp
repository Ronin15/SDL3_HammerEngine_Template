// Copyright (c) 2025 Hammer Forged Games
// Licensed under the MIT License - see LICENSE file for details

#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "GameState.hpp"
#include "Player.hpp"
#include <memory>

class GamePlayState : public GameState {
 public:
  GamePlayState() : m_transitioningToPause(false), mp_Player(nullptr) {}
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;

 private:
  bool m_transitioningToPause; // Flag to indicate we're transitioning to pause state
  std::unique_ptr<Player> mp_Player; // Player object
};

#endif  // GAME_PLAY_STATE_HPP
