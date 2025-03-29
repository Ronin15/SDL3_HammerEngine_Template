#ifndef PAUSE_STATE_HPP
#define PAUSE_STATE_HPP

#include "GameState.hpp"

class PauseState : public GameState {
public:
  void enter() override;
  void update() override;
  void render() override;
  void exit() override;
  std::string getName() const override;
};

#endif // PAUSE_STATE_HPP
