#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "GameState.hpp"

class GamePlayState : public GameState {
 public:
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;
};

#endif  // GAME_PLAY_STATE_HPP
