/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "entities/Player.hpp"
#include "gameStates/GameState.hpp"
#include <memory>

class GamePlayState : public GameState {
public:
  GamePlayState()
      : m_transitioningToPause{false}, mp_Player{nullptr},
        m_inventoryVisible{false} {}
  bool enter() override;
  void update(float deltaTime) override;
  void render(float deltaTime) override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;

private:
  bool m_transitioningToPause{
      false}; // Flag to indicate we're transitioning to pause state
  std::shared_ptr<Player> mp_Player{nullptr}; // Player object
  bool m_inventoryVisible{false}; // Flag to control inventory UI visibility

  // Inventory UI methods
  void initializeInventoryUI();
  void updateInventoryUI();
  void toggleInventoryDisplay();
  void addDemoResource(const std::string &resourceId, int quantity);
  void removeDemoResource(const std::string &resourceId, int quantity);
};

#endif // GAME_PLAY_STATE_HPP
