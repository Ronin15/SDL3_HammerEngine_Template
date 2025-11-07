/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
 */

#ifndef GAME_PLAY_STATE_HPP
#define GAME_PLAY_STATE_HPP

#include "entities/Player.hpp"
#include "gameStates/GameState.hpp"
#include "utils/ResourceHandle.hpp"
#include "utils/Camera.hpp"
#include <memory>

class GamePlayState : public GameState {
public:
  GamePlayState()
      : m_transitioningToPause{false}, m_transitioningToLoading{false},
        mp_Player{nullptr}, m_inventoryVisible{false}, m_initialized{false} {}
  bool enter() override;
  void update(float deltaTime) override;
  void render() override;
  void handleInput() override;
  bool exit() override;
  std::string getName() const override;
  void onWindowResize(int newLogicalWidth, int newLogicalHeight) override;

private:
  bool m_transitioningToPause{
      false}; // Flag to indicate we're transitioning to pause state
  bool m_transitioningToLoading{
      false}; // Flag to indicate we're transitioning to loading state
  std::shared_ptr<Player> mp_Player{nullptr}; // Player object
  bool m_inventoryVisible{false}; // Flag to control inventory UI visibility
  bool m_initialized{false}; // Flag to track if state is already initialized (for pause/resume)
  
  // Camera for world navigation and player following
  std::unique_ptr<HammerEngine::Camera> m_camera{nullptr};
  
  // Camera transformation state (calculated in update, used in render)
  float m_cameraOffsetX{0.0f};
  float m_cameraOffsetY{0.0f};

  // Resource handles resolved at initialization (resource handle system
  // compliance)
  HammerEngine::ResourceHandle m_goldHandle;
  HammerEngine::ResourceHandle m_healthPotionHandle;
  HammerEngine::ResourceHandle m_ironOreHandle;
  HammerEngine::ResourceHandle m_woodHandle;

  // Track whether world has been loaded (prevents re-entering LoadingState)
  bool m_worldLoaded{false};

  // Track if we need to transition to loading screen on first update
  bool m_needsLoading{false};

  // Inventory UI methods
  void initializeInventoryUI();
  void toggleInventoryDisplay();
  void addDemoResource(HammerEngine::ResourceHandle resourceHandle,
                       int quantity);
  void removeDemoResource(HammerEngine::ResourceHandle resourceHandle,
                          int quantity);
  void
  initializeResourceHandles(); // Resolve names to handles during initialization

  // Camera management methods
  void initializeCamera();
  void updateCamera(float deltaTime);
  // Camera auto-manages world bounds; no state-level setup needed
};

#endif // GAME_PLAY_STATE_HPP
