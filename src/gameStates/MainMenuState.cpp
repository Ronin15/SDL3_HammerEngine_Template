/* Copyright (c) 2025 Hammer Forged Games
 * All rights reserved.
 * Licensed under the MIT License - see LICENSE file for details
*/

#include "gameStates/MainMenuState.hpp"
#include "managers/UIManager.hpp"
#include "managers/InputManager.hpp"
#include "managers/FontManager.hpp"
#include "managers/GameStateManager.hpp"
#include "core/GameEngine.hpp"
#include "core/Logger.hpp"

#ifdef USE_SDL3_GPU
#include "gpu/GPURenderer.hpp"
#endif

#include <thread>
#include <chrono>

bool MainMenuState::enter() {
  GAMESTATE_INFO("Entering MAIN MENU State");

  // Pause all game managers to reduce power draw while at menu
  GameEngine::Instance().setGlobalPause(true);

  auto& ui = UIManager::Instance();
  auto& fontMgr = FontManager::Instance();

  // Wait briefly for fonts to be loaded before creating UI components.
  // Avoid unbounded waits on macOS where early resize/display events can trigger
  // a reload loop. Proceed after a short timeout and let UI relayout when fonts finish.
  constexpr int kMaxWaitMs = 1500; // 1.5s max wait
  int waitedMs = 0;
  while (!fontMgr.areFontsLoaded() && waitedMs < kMaxWaitMs) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    waitedMs += 1;
  }

  // Create title using auto-positioning
  ui.createTitleAtTop("mainmenu_title", "Hammer Game Engine - Main Menu", 60);

  // Create menu buttons with centered positioning
  int buttonWidth = 300;
  int buttonHeight = 50;
  int buttonSpacing = 20;

  // Calculate relative offsets from center for 8 buttons
  // Total height: 8 buttons * 50px + 7 gaps * 20px = 540px
  // Center the group by starting at -270 from center
  int buttonStep = buttonHeight + buttonSpacing; // 70px between each button
  int firstButtonOffset = -270; // Top of centered button group

  // Use createCenteredButton helper for streamlined button creation
  ui.createCenteredButton("mainmenu_start_game_btn", firstButtonOffset, buttonWidth, buttonHeight, "Start Game");
  ui.createCenteredButton("mainmenu_ai_demo_btn", firstButtonOffset + buttonStep, buttonWidth, buttonHeight, "AI Demo");
  ui.createCenteredButton("mainmenu_advanced_ai_demo_btn", firstButtonOffset + 2 * buttonStep, buttonWidth, buttonHeight, "Advanced AI Demo");
  ui.createCenteredButton("mainmenu_event_demo_btn", firstButtonOffset + 3 * buttonStep, buttonWidth, buttonHeight, "Event Demo");
  ui.createCenteredButton("mainmenu_ui_example_btn", firstButtonOffset + 4 * buttonStep, buttonWidth, buttonHeight, "UI Demo");
  ui.createCenteredButton("mainmenu_overlay_demo_btn", firstButtonOffset + 5 * buttonStep, buttonWidth, buttonHeight, "Overlay Demo");
  ui.createCenteredButton("mainmenu_settings_btn", firstButtonOffset + 6 * buttonStep, buttonWidth, buttonHeight, "Settings");

  // Exit button uses danger style (manual creation + positioning)
  ui.createButtonDanger("mainmenu_exit_btn", {ui.getLogicalWidth()/2 - buttonWidth/2, ui.getLogicalHeight()/2 + firstButtonOffset + 7 * buttonStep, buttonWidth, buttonHeight}, "Exit");
  ui.setComponentPositioning("mainmenu_exit_btn", {UIPositionMode::CENTERED_BOTH, 0, firstButtonOffset + 7 * buttonStep, buttonWidth, buttonHeight});

  // Set up button callbacks - capture mp_stateManager for proper architecture
  ui.setOnClick("mainmenu_start_game_btn", [this]() {
    mp_stateManager->changeState("GamePlayState");
  });

  ui.setOnClick("mainmenu_ai_demo_btn", [this]() {
    mp_stateManager->changeState("AIDemoState");
  });

  ui.setOnClick("mainmenu_advanced_ai_demo_btn", [this]() {
    mp_stateManager->changeState("AdvancedAIDemoState");
  });

  ui.setOnClick("mainmenu_event_demo_btn", [this]() {
    mp_stateManager->changeState("EventDemo");
  });

  ui.setOnClick("mainmenu_ui_example_btn", [this]() {
    mp_stateManager->changeState("UIExampleState");
  });

  ui.setOnClick("mainmenu_overlay_demo_btn", [this]() {
    mp_stateManager->changeState("OverlayDemoState");
  });

  ui.setOnClick("mainmenu_settings_btn", [this]() {
    mp_stateManager->changeState("SettingsMenuState");
  });

  ui.setOnClick("mainmenu_exit_btn", []() {
    GameEngine::Instance().setRunning(false);
  });

  return true;
}

void MainMenuState::update([[maybe_unused]] float deltaTime) {
  // Process UI input (click detection, hover states, callbacks)
  auto& ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.update(0.0f);
  }
}

void MainMenuState::render(SDL_Renderer* renderer, [[maybe_unused]] float interpolationAlpha) {
  // Render UI components (input handled in update())
  auto& ui = UIManager::Instance();
  ui.render(renderer);
}

bool MainMenuState::exit() {
  GAMESTATE_INFO("Exiting MAIN MENU State");

  // NOTE: Do NOT unpause here - gameplay states will unpause on enter
  // This keeps systems paused during menu-to-menu transitions

  // Clean up UI components using simplified method
  auto& ui = UIManager::Instance();
  ui.prepareForStateTransition();

  return true;
}

void MainMenuState::handleInput() {
  const auto& inputManager = InputManager::Instance();

  // Keyboard shortcuts for quick navigation
  if (inputManager.wasKeyPressed(SDL_SCANCODE_RETURN)) {
      mp_stateManager->changeState("GamePlayState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_A)) {
      mp_stateManager->changeState("AIDemoState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_E)) {
      mp_stateManager->changeState("EventDemo");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_U)) {
      mp_stateManager->changeState("UIExampleState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_O)) {
      mp_stateManager->changeState("OverlayDemoState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_S)) {
      mp_stateManager->changeState("SettingsMenuState");
  }

  if (inputManager.wasKeyPressed(SDL_SCANCODE_ESCAPE)) {
      GameEngine::Instance().setRunning(false);
  }
}

std::string MainMenuState::getName() const {
  return "MainMenuState";
}

#ifdef USE_SDL3_GPU
void MainMenuState::recordGPUVertices(HammerEngine::GPURenderer& gpuRenderer,
                                       [[maybe_unused]] float interpolationAlpha) {
  // MainMenuState uses UIManager for all rendering
  // UIManager records its vertices (buttons, panels, text)
  auto& ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.recordGPUVertices(gpuRenderer);
  }
}

void MainMenuState::renderGPUUI(HammerEngine::GPURenderer& gpuRenderer,
                                 SDL_GPURenderPass* swapchainPass) {
  // Render UIManager components to swapchain
  auto& ui = UIManager::Instance();
  if (!ui.isShutdown()) {
    ui.renderGPU(gpuRenderer, swapchainPass);
  }
}
#endif

