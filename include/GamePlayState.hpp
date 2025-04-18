#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "GameState.hpp"

class GamePlayState : public GameState {
 public:
  GamePlayState() : m_transitioningToPause(false) {}
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;

 private:
  bool m_transitioningToPause; // Flag to indicate we're transitioning to pause state
};

#endif  // GAME_PLAY_STATE_HPP
