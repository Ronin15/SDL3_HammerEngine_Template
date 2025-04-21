#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "GameState.hpp"
#include "Player.hpp"
#include <memory>

class GamePlayState : public GameState {
 public:
  GamePlayState() : m_transitioningToPause(false), m_pPlayer(nullptr) {}
  bool enter() override;
  void update() override;
  void render() override;
  bool exit() override;
  std::string getName() const override;

 private:
  bool m_transitioningToPause; // Flag to indicate we're transitioning to pause state
  std::unique_ptr<Player> m_pPlayer; // Player object
};

#endif  // GAME_PLAY_STATE_HPP
