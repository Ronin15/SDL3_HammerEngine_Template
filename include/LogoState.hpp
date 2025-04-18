#ifndef LOGO_STATE_HPP
#define LOGO_STATE_HPP

#include "GameState.hpp"

class LogoState : public GameState {
 public:
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;
};

#endif  // LOGO_STATE_HPP
